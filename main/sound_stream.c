
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "fatfs_stream.h"
#include "i2s_stream.h"
#include "mp3_decoder.h"
#include "opus_decoder.h"
#include "esp_peripherals.h"
#include "periph_sdcard.h"
#include "board.h"
#include "pi4ioe5v6416.h"
#include "es8388.h"

#include "all.h"

static const char *TAG = "sound";

// Add this function to handle song/decoder changes
typedef enum {
    AUDIO_CODEC_UNKNOWN = -1,
    AUDIO_CODEC_WAV,
    AUDIO_CODEC_FLAC,
    AUDIO_CODEC_AAC,
    AUDIO_CODEC_OGG,
    AUDIO_CODEC_OPUS,
    AUDIO_CODEC_MP3,
} audio_codec_t;

typedef struct {
    audio_pipeline_handle_t pipeline;
    audio_element_handle_t fatfs_stream_reader;
    audio_element_handle_t i2s_stream_writer;
    audio_element_handle_t current_decoder;
    audio_element_handle_t opus_decoder;
    audio_element_handle_t mp3_decoder;
    audio_event_iface_handle_t evt;
    audio_event_iface_handle_t pub;
    audio_codec_t current_codec;
    audio_board_handle_t board_handle;
    char *playlist[128];
    int current_song_index;
    int song_count;
    bool paused;
} audio_player_t;


audio_codec_t audio_player_detect(const char* filepath) {
    if (!filepath) {
        ESP_LOGE(TAG, "Invalid filepath");
        return AUDIO_CODEC_UNKNOWN;
    }

    FILE* f = fopen(filepath, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file: %s", filepath);
        return AUDIO_CODEC_UNKNOWN;
    }

    uint8_t header[36];  // Increased to 36 bytes for Opus detection
    size_t bytes_read = fread(header, 1, 36, f);
    fclose(f);

    if (bytes_read < 4) {
        ESP_LOGE(TAG, "File too small to detect codec");
        return AUDIO_CODEC_UNKNOWN;
    }

    // Check for MP3 (MPEG frame sync: 0xFFE or 0xFFF)
    if (header[0] == 0xFF && (header[1] & 0xE0) == 0xE0) {
        ESP_LOGI(TAG, "Detected: MP3");
        return AUDIO_CODEC_MP3;
    }

    // Check for FLAC (0x664C6143 = "fLaC")
    if (header[0] == 0x66 && header[1] == 0x4C &&
        header[2] == 0x61 && header[3] == 0x43) {
        ESP_LOGI(TAG, "Detected: FLAC");
        return AUDIO_CODEC_FLAC;
    }

    // Check for Ogg container (0x4F676753 = "OggS")
    if (header[0] == 0x4F && header[1] == 0x67 &&
        header[2] == 0x67 && header[3] == 0x53) {

        // Check for Opus: "OpusHead" at bytes 28-35
        if (bytes_read >= 36 &&
            header[28] == 0x4F && header[29] == 0x70 &&
            header[30] == 0x75 && header[31] == 0x73 &&
            header[32] == 0x48 && header[33] == 0x65 &&
            header[34] == 0x61 && header[35] == 0x64) {
            ESP_LOGI(TAG, "Detected: OPUS");
            return AUDIO_CODEC_OPUS;
        }

        ESP_LOGI(TAG, "Detected: OGG");
        return AUDIO_CODEC_OGG;
    }

    // Check for WAV (0x52494646 = "RIFF")
    if (header[0] == 0x52 && header[1] == 0x49 &&
        header[2] == 0x46 && header[3] == 0x46) {
        ESP_LOGI(TAG, "Detected: WAV");
        return AUDIO_CODEC_WAV;
    }

    // Check for AAC (ADTS: 0xFFF or 0xFFE)
    if (header[0] == 0xFF && (header[1] & 0xF0) == 0xF0) {
        ESP_LOGI(TAG, "Detected: AAC");
        return AUDIO_CODEC_AAC;
    }

    ESP_LOGW(TAG, "Unknown codec: %02X %02X %02X %02X", header[0], header[1], header[2], header[3]);

    return AUDIO_CODEC_UNKNOWN;
}


// Switch codec (opus <-> mp3) and play file
esp_err_t audio_player_play(audio_player_t* player, const char* filepath) {
    if (filepath == NULL) {
        return ESP_OK;  // No file to play, just return
    }
    ESP_LOGI(TAG, "Playing file: %s", filepath);

    // Stop playback
    audio_pipeline_stop(player->pipeline);
    audio_pipeline_wait_for_stop(player->pipeline);
    audio_pipeline_reset_ringbuffer(player->pipeline);
    audio_pipeline_reset_elements(player->pipeline);
    audio_pipeline_change_state(player->pipeline, AEL_STATE_INIT);

    audio_codec_t codec = audio_player_detect(filepath);
    if (codec == player->current_codec) {
        // Same codec, just change file
        // Set new file URI
        audio_element_set_uri(player->fatfs_stream_reader, filepath);

        // Resume playback
        return audio_pipeline_run(player->pipeline);
    }

    // Different codec - need to unlink and relink
    ESP_LOGI(TAG, "Codec change: %d -> %d", player->current_codec, codec);

    // Select new decoder
    audio_element_handle_t new_decoder;
    const char *link_tag[3] = {"file", "dec", "i2s"};
    switch (codec) {
        case AUDIO_CODEC_OPUS:
            new_decoder = player->opus_decoder;
            link_tag[1] = "opus_dec";
            break;
        case AUDIO_CODEC_MP3:
            new_decoder = player->mp3_decoder;
            link_tag[1] = "mp3_dec";
            break;
        default:
            ESP_LOGE(TAG, "Unsupported codec: %d", codec);
            return ESP_ERR_NOT_SUPPORTED;
    }

    // Reset new decoder, just to be sure
    audio_element_reset_state(new_decoder);

    // Unlink entire pipeline
    audio_pipeline_unlink(player->pipeline);

    // Relink with new decoder
    audio_pipeline_link(player->pipeline, &link_tag[0], 3);

    // Create a new listener as the old is now dead
    if (player->evt) {
        audio_event_iface_destroy(player->evt);
    }
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    player->evt = audio_event_iface_init(&evt_cfg);
    audio_pipeline_set_listener(player->pipeline, player->evt);
    audio_event_iface_set_listener(player->pub, player->evt);

    // Update current decoder
    player->current_decoder = new_decoder;
    player->current_codec = codec;

    // Set file and resume
    audio_element_set_uri(player->fatfs_stream_reader, filepath);
    audio_pipeline_run(player->pipeline);

    ESP_LOGI(TAG, "Codec switch complete");
    return ESP_OK;
}


// Initialize player (modified from your original code)
void audio_player_init(audio_player_t* player) {
    ESP_LOGI(TAG, "[ 1 ] Initialize audio player");

    // Initialize board and codec
    player->board_handle = audio_board_init();
    audio_hal_ctrl_codec(player->board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, AUDIO_HAL_CTRL_START);
    es8388_set_voice_mute(true);
    audio_hal_set_volume(player->board_handle->audio_hal, 0);

    // Create pipeline
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    // pipeline_cfg.rb_size = 65536;
    player->pipeline = audio_pipeline_init(&pipeline_cfg);

    // Create FATFS reader (reusable for any file)
    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = AUDIO_STREAM_READER;
    // fatfs_cfg.out_rb_size = 32768;
    player->fatfs_stream_reader = fatfs_stream_init(&fatfs_cfg);

    ESP_LOGI(TAG, "[ 2 ] i2sinit");
    // Create I2S writer
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    //i2s_cfg.i2s_config.sample_rate = 48000;
    i2s_cfg.chan_cfg.dma_desc_num = 8;
    i2s_cfg.chan_cfg.dma_frame_num = 512;
    player->i2s_stream_writer = i2s_stream_init(&i2s_cfg);

    audio_hal_set_volume(player->board_handle->audio_hal, 50);
    es8388_set_voice_mute(false);

    ESP_LOGI(TAG, "[ 2 ] pa init");
    es8388_write_reg(ES8388_DACCONTROL26, 0x1E);  // LOUT2 gain (0db vs 1.5 or -45)
    es8388_write_reg(ES8388_DACCONTROL27, 0x1E);  // ROUT2 gain (0db vs 1.5 or -45)

    // Create both decoders upfront
    opus_decoder_cfg_t opus_cfg = DEFAULT_OPUS_DECODER_CONFIG();
    // opus_cfg.task_prio = 25;
    // opus_cfg.out_rb_size = 32768;
    player->opus_decoder = decoder_opus_init(&opus_cfg);

    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    // mp3_cfg.task_prio = 25;
    // mp3_cfg.out_rb_size = 32768;
    player->mp3_decoder = mp3_decoder_init(&mp3_cfg);

    // Register all elements (even though not all linked initially)
    audio_pipeline_register(player->pipeline, player->fatfs_stream_reader, "file");
    audio_pipeline_register(player->pipeline, player->opus_decoder, "opus_dec");
    audio_pipeline_register(player->pipeline, player->mp3_decoder, "mp3_dec");
    audio_pipeline_register(player->pipeline, player->i2s_stream_writer, "i2s");

    player->current_decoder = NULL;
    player->current_codec = AUDIO_CODEC_UNKNOWN;
    player->evt = NULL;
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    player->pub = audio_event_iface_init(&evt_cfg);
}

static audio_player_t player;

void playback_inject_event(int keypress_cmd, int data) {
    if (player.pub == NULL) {
        ESP_LOGW(TAG, "Audio event interface not initialized yet, cannot inject event");
        return;
    }

    audio_event_iface_msg_t msg = {0};
    msg.source_type = AUDIO_ELEMENT_TYPE_ELEMENT;
    msg.source = (void*)0xDEADBEEF;  // Custom identifier for keypresses
    msg.cmd = keypress_cmd;
    msg.data = (void*)data;
    char **intro = (char**)player.pub;
    ESP_LOGI(TAG, "Inject cmd: %d, data: %d to %p => %p, %p", msg.cmd, data, player.pub, intro[0], intro[1]);

    //audio_event_iface_cmd(player.pipeline, &msg);
    audio_event_iface_sendout(player.pub, &msg);
}

// Your main playback loop
void playback_task(void* arg) {
    memset(&player, 0, sizeof(player));
    audio_player_init(&player);

    player.playlist[0] = "/sdcard/6GEA3/53Ihh9ptfoGNfhMmytoDcRxave_A94jiuA01kjjp2r4";
    player.playlist[1] = "/sdcard/6GEA3/4qxBSmewoTS9SmiXCby3BFogVmBu9dfAKBPP9F9NWaU";
    player.playlist[2] = "/sdcard/6GEA3/Y6S3oFa548kWAUlbSvBg3EWT84fLx3W4WsD7XbJv2v0";
    player.playlist[3] = "/sdcard/6GEA3/Y1qyVuD4OFA19AAf1dt6M3xGnmIYmq3VFKimvfNfsV0";
    player.playlist[4] = "/sdcard/6GEA3/q_6k7gd_nucqKBcA0tQMuzCi_Ykf8CWc54XEaS8MKDk";
    player.playlist[5] = "/sdcard/6GEA3/23U1RvnW60a5VaOc_WS06IHnXrOOFVlem0tRtzk8oSE";
    player.playlist[6] = "/sdcard/6GEA3/kJAqNRpK1ymTomVMVEvxIua1DSBVX9qq_DQR25Q6dVs";
    player.playlist[7] = "/sdcard/6GEA3/cJ8rTPo4x-PCVo5dPVXdQdTY3rb-wSq-5PjLDAUwB_c";
    player.playlist[8] = "/sdcard/6GEA3/oNfsLmbJu0fdOZuyFuJsTQUu5TbXZiBzypy8cf8PyMo";
    player.playlist[9] = "/sdcard/6GEA3/CVz4c5igAQaU7J75NWWQySfmPOtphshqFv0LX7OZiCk";
    player.playlist[10] = "/sdcard/6GEA3/8jci1yaGY7uJDrU6XudQAQNXH_JKPUy73nvXLBwfr_k";
    player.playlist[11] = "/sdcard/6GEA3/gJSg4WNeA9oUIMVYuEsERLxKsj_GaS7N3z4d7x4ehwo";
    player.playlist[12] = "/sdcard/6GEA3/yWpURq4KVWNIO_-34Fe5XFMDe8zakKclr_iIGC8mN_A";
    player.playlist[13] = "/sdcard/6GEA3/dVt-9PmQwyEJUFAgOepbgT4_jYBPISyMav2eEjtbCfY";
    player.song_count = 14;

    audio_player_play(&player, player.playlist[player.current_song_index++]);

    while (1) {
        audio_event_iface_msg_t msg;
        esp_err_t ret = audio_event_iface_listen(player.evt, &msg, portMAX_DELAY);

        if (ret != ESP_OK) {
            // slow down loop on error to avoid taking all the available CPU time
            ESP_LOGW(TAG, "AudioEventError %d", ret);
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        ESP_LOGW(TAG, "AudioEvent %d from %p", (int)msg.cmd, player.evt);

        // Handle music info from decoder
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT &&
            msg.source == (void *) player.current_decoder &&
            msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
            audio_element_info_t music_info = {0};
            audio_element_getinfo(player.current_decoder, &music_info);
            audio_element_setinfo(player.i2s_stream_writer, &music_info);
            ESP_LOGI(TAG, "Received music info event %d %d %d",
                              music_info.sample_rates,
                              music_info.bits,
                              music_info.channels);
            i2s_stream_set_clk(player.i2s_stream_writer,
                              music_info.sample_rates,
                              music_info.bits,
                              music_info.channels);
        }

        // Stop event
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT &&
            msg.source == (void *) player.i2s_stream_writer &&
            msg.cmd == AEL_MSG_CMD_REPORT_STATUS &&
            (((int)msg.data == AEL_STATUS_STATE_STOPPED) ||
             ((int)msg.data == AEL_STATUS_STATE_FINISHED))) {

            ESP_LOGW(TAG, "Song status %d", (int)msg.data);
            // Auto-play next song or wait for command
            if (msg.data == (void*)AEL_STATUS_STATE_FINISHED) {
                ESP_LOGI(TAG, "Auto-playing next song...");
                audio_player_play(&player, player.playlist[player.current_song_index++]);
            }
        }

        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT &&
            msg.source == (void*)0xDEADBEEF) {  // Our custom keypress events
            int data = (int)msg.data;
            ESP_LOGI(TAG, "Deadbeef event cmd: %d, data: %d", msg.cmd, data);

            switch (msg.cmd) {
                case AUDIO_EVENT_PLAYPAUSE:
                    if (data < 0) {
                        ESP_LOGI(TAG, "Pause");
                        audio_pipeline_pause(player.pipeline);
                        player.paused = true;
                    } else if (data > 0) {
                        ESP_LOGI(TAG, "Resume");
                        audio_pipeline_resume(player.pipeline);
                        player.paused = false;
                    } else {
                        ESP_LOGI(TAG, "Toggling play/pause");
                        if (player.paused) {
                            audio_pipeline_resume(player.pipeline);
                        } else {
                            audio_pipeline_pause(player.pipeline);
                        }
                        player.paused = !player.paused;
                    }
                    break;
                case AUDIO_EVENT_NEXT:
                    ESP_LOGI(TAG, "Playing next song");
                    player.current_song_index = (player.current_song_index + 1) % player.song_count;
                    audio_player_play(&player, player.playlist[player.current_song_index]);
                    break;
                case AUDIO_EVENT_PREV:
                    ESP_LOGI(TAG, "Playing previous song");
                    player.current_song_index = (player.current_song_index - 1 + player.song_count) % player.song_count;
                    audio_player_play(&player, player.playlist[player.current_song_index]);
                    break;
                case AUDIO_EVENT_SET_TRACK:
                    ESP_LOGI(TAG, "Setting track to index %d", data);
                    if (data >= 0 && data < sizeof(player.playlist)/sizeof(player.playlist[0]) - 1) {
                        player.current_song_index = data;
                        audio_player_play(&player, player.playlist[player.current_song_index]);
                    }
                    break;
                case AUDIO_EVENT_VOLUME:
                    ESP_LOGI(TAG, "Setting volume to %d", data);
                    audio_hal_set_volume(player.board_handle->audio_hal, data);
                    break;
                default:
                    ESP_LOGW(TAG, "Unknown event cmd: %d", msg.cmd);
            }
        }
    }
}
