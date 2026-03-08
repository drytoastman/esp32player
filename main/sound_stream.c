
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

#if 0
audio_board_handle_t board_handle = NULL;
audio_element_handle_t mp3_decoder = NULL;
audio_element_handle_t opus_decoder = NULL;

/*
i2s stack     = 6 KB
fatfs stack   = 4 KB

You can usually run safely with:

i2s stack     = 3 KB
fatfs stack   = 2 KB
*/
void sound_init()
{
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_decoder = mp3_decoder_init(&mp3_cfg);

    opus_decoder_cfg_t opus_cfg = DEFAULT_OPUS_DECODER_CONFIG();
    opus_cfg.task_prio = 6;
    opus_decoder = decoder_opus_init(&opus_cfg);
}


/**

Switch out codec based on file
audio_pipeline_unregister(pipeline, decoder);
decoder = ogg_decoder;
audio_pipeline_register(pipeline, decoder, "decoder");


Near-instant track switching (~20–40 ms)
Instead of stopping the whole pipeline, only reset the reader + decoder.
Fast switch pattern

audio_element_stop(file_stream);
audio_element_stop(decoder);

audio_element_reset_state(file_stream);
audio_element_reset_state(decoder);

audio_element_set_uri(file_stream, newfile);

audio_element_run(file_stream);
audio_element_run(decoder);
*/

void sound_main(void *inputParameters)
{
    sound_init();

    // Example of linking elements into an audio pipeline -- START
    audio_pipeline_handle_t pipeline;
    audio_element_handle_t fatfs_stream_reader, i2s_stream_writer;

    ESP_LOGI(TAG, "[ 2 ] Start codec chip");
    board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, AUDIO_HAL_CTRL_START);
    audio_hal_set_volume(board_handle->audio_hal, 60);  // 0–100

    ESP_LOGI(TAG, "[3.0] Create audio pipeline for playback");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);
    mem_assert(pipeline);

    ESP_LOGI(TAG, "[3.1] Create fatfs stream to read data from sdcard");
    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = AUDIO_STREAM_READER;
    fatfs_stream_reader = fatfs_stream_init(&fatfs_cfg);

    ESP_LOGI(TAG, "[3.2] Create i2s stream to write data to codec chip");
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_cfg.chan_cfg.dma_desc_num = 8;
    i2s_cfg.chan_cfg.dma_frame_num = 512;
    i2s_cfg.std_cfg.clk_cfg.sample_rate_hz = 48000;
    i2s_stream_writer = i2s_stream_init(&i2s_cfg);

    es8388_write_reg(ES8388_DACCONTROL26, 0x1E);  // LOUT2 gain (0db vs 1.5 or -45)
    es8388_write_reg(ES8388_DACCONTROL27, 0x1E);  // ROUT2 gain (0db vs 1.5 or -45)

    ESP_LOGI(TAG, "[3.4] Register all elements to audio pipeline");
    audio_pipeline_register(pipeline, fatfs_stream_reader, "file");
    audio_pipeline_register(pipeline, opus_decoder, "dec");
    audio_pipeline_register(pipeline, i2s_stream_writer, "i2s");

    ESP_LOGI(TAG, "[3.5] Link it together [sdcard]-->fatfs_stream-->music_decoder-->i2s_stream-->[codec_chip]");
    const char *link_tag[3] = {"file", "dec", "i2s"};
    audio_pipeline_link(pipeline, &link_tag[0], 3);

    ESP_LOGI(TAG, "[3.6] Set up uri:");
    audio_element_set_uri(fatfs_stream_reader, "/sdcard/test.mp3");
    //audio_element_set_uri(fatfs_stream_reader, "/sdcard/afDCk/U7lT54DKU16tRK61rB5oYA_YdVxEx7o4Uvh5tuq5lWo");

    ESP_LOGI(TAG, "[ 4 ] Set up  event listener");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);

    ESP_LOGI(TAG, "[4.1] Listening event from all elements of pipeline");
    audio_pipeline_set_listener(pipeline, evt);

    ESP_LOGI(TAG, "[4.2] Listening event from peripherals");
    //audio_event_iface_set_listener(esp_periph_set_get_event_iface(set), evt);

    ESP_LOGI(TAG, "[ 5 ] Start audio_pipeline");
    audio_pipeline_run(pipeline);
    // Example of linking elements into an audio pipeline -- END

    ESP_LOGI(TAG, "[ 6 ] Listen for all pipeline events");
    while (1) {
        audio_event_iface_msg_t msg;
        esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[ * ] Event interface error : %d", ret);
            continue;
        }

        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *) opus_decoder
            && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
            audio_element_info_t music_info = {0};
            audio_element_getinfo(opus_decoder, &music_info);

            ESP_LOGI(TAG, "[ * ] Receive music info from music decoder, sample_rates=%d, bits=%d, ch=%d",
                     music_info.sample_rates, music_info.bits, music_info.channels);

            audio_element_setinfo(i2s_stream_writer, &music_info);
            i2s_stream_set_clk(i2s_stream_writer, music_info.sample_rates, music_info.bits, music_info.channels);
            continue;
        }

        /* Stop when the last pipeline element (i2s_stream_writer in this case) receives stop event */
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *) i2s_stream_writer
            && msg.cmd == AEL_MSG_CMD_REPORT_STATUS
            && (((int)msg.data == AEL_STATUS_STATE_STOPPED) || ((int)msg.data == AEL_STATUS_STATE_FINISHED))) {
            ESP_LOGW(TAG, "[ * ] Stop event received");
            break;
        }
    }

    ESP_LOGI(TAG, "[ 7 ] Stop audio_pipeline");
    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    audio_pipeline_reset_ringbuffer(pipeline);
    audio_pipeline_reset_elements(pipeline);

#if startanotherfile
    audio_pipeline_unregister(pipeline, http_stream_reader);

    audio_pipeline_register(pipeline, fatfs_stream_reader, "file");
    audio_pipeline_link(pipeline, (const char *[]) {"file", "decoder", "i2s"}, 3);

    audio_element_set_uri(fatfs_stream_reader, "/sdcard/song.mp3");
#endif

#if startanhttpstream
    audio_pipeline_unregister(pipeline, fatfs_stream_reader);

    audio_pipeline_register(pipeline, http_stream_reader, "http");
    audio_pipeline_link(pipeline, (const char *[]) {"http", "decoder", "i2s"}, 3);

    audio_element_set_uri(http_stream_reader, "http://example.com/song.mp3");
#endif

#if noshutdown
    audio_pipeline_terminate(pipeline);

    audio_pipeline_unregister(pipeline, fatfs_stream_reader);
    audio_pipeline_unregister(pipeline, i2s_stream_writer);
    audio_pipeline_unregister(pipeline, music_decoder);

    /* Terminal the pipeline before removing the listener */
    audio_pipeline_remove_listener(pipeline);

    /* Stop all periph before removing the listener */
    esp_periph_set_stop_all(set);
    audio_event_iface_remove_listener(esp_periph_set_get_event_iface(set), evt);

    /* Make sure audio_pipeline_remove_listener & audio_event_iface_remove_listener are called before destroying event_iface */
    audio_event_iface_destroy(evt);

    /* Release all resources */
    audio_pipeline_deinit(pipeline);
    audio_element_deinit(fatfs_stream_reader);

    audio_element_deinit(i2s_stream_writer);
    audio_element_deinit(music_decoder);
    esp_periph_set_destroy(set);  // <<< Problem child
#endif

    vTaskDelete(NULL);
}
#endif

// Add this function to handle song/decoder changes
typedef enum {
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
    audio_codec_t current_codec;
} audio_player_t;

// Switch to a different song (same codec)
esp_err_t audio_player_play_file(audio_player_t* player, const char* filepath) {
    ESP_LOGI(TAG, "Switching to file: %s", filepath);

    // Stop current playback
    audio_pipeline_stop(player->pipeline);
    audio_pipeline_wait_for_stop(player->pipeline);

    // Reset elements to clean state
    audio_pipeline_reset_ringbuffer(player->pipeline);
    audio_pipeline_reset_elements(player->pipeline);
    audio_pipeline_change_state(player->pipeline, AEL_STATE_INIT);

    // Set new file URI
    audio_element_set_uri(player->fatfs_stream_reader, filepath);

    // Resume playback
    audio_pipeline_run(player->pipeline);

    return ESP_OK;
}

// Switch codec (opus <-> mp3) and play file
esp_err_t audio_player_play_with_codec(audio_player_t* player,
                                       const char* filepath,
                                       audio_codec_t codec) {
    ESP_LOGI(TAG, "Switching codec and file");

    if (codec == player->current_codec) {
        // Same codec, just change file
        return audio_player_play_file(player, filepath);
    }

    // Different codec - need to unlink and relink
    ESP_LOGI(TAG, "Codec change: %d -> %d", player->current_codec, codec);

    // Stop playback
    audio_pipeline_stop(player->pipeline);
    audio_pipeline_wait_for_stop(player->pipeline);

    // Unlink entire pipeline
    audio_pipeline_unlink(player->pipeline);

    // Select new decoder
    audio_element_handle_t new_decoder;
    if (codec == AUDIO_CODEC_OPUS) {
        new_decoder = player->opus_decoder;
    } else {
        new_decoder = player->mp3_decoder;
    }

    // Reset all elements
    // audio_pipeline_reset_ringbuffer(player->pipeline);
    // audio_element_reset_state(player->fatfs_stream_reader);
    // audio_element_reset_state(player->current_decoder);
    // audio_element_reset_state(new_decoder);
    // audio_element_reset_state(player->i2s_stream_writer);

    // Relink with new decoder
    const char *link_tag[3];
    if (codec == AUDIO_CODEC_OPUS) {
        link_tag[0] = "file";
        link_tag[1] = "opus_dec";
        link_tag[2] = "i2s";
    } else {
        link_tag[0] = "file";
        link_tag[1] = "mp3_dec";
        link_tag[2] = "i2s";
    }
    audio_pipeline_link(player->pipeline, &link_tag[0], 3);

    // Update current decoder
    player->current_decoder = new_decoder;
    player->current_codec = codec;

    // Set file and resume
    audio_element_set_uri(player->fatfs_stream_reader, filepath);
    audio_pipeline_run(player->pipeline);

    return ESP_OK;
}

//audio_board_handle_t board_handle = NULL;


// Initialize player (modified from your original code)
void audio_player_init(audio_player_t* player) {
    ESP_LOGI(TAG, "[ 1 ] Initialize audio player");

    // Initialize board and codec
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, AUDIO_HAL_CTRL_START);
    audio_hal_set_volume(board_handle->audio_hal, 60);

    // Create pipeline
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    // pipeline_cfg.rb_size = 65536;
    player->pipeline = audio_pipeline_init(&pipeline_cfg);

    // Create FATFS reader (reusable for any file)
    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = AUDIO_STREAM_READER;
    // fatfs_cfg.out_rb_size = 32768;
    player->fatfs_stream_reader = fatfs_stream_init(&fatfs_cfg);

    // Create I2S writer
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    //i2s_cfg.i2s_config.sample_rate = 48000;
    i2s_cfg.chan_cfg.dma_desc_num = 8;
    i2s_cfg.chan_cfg.dma_frame_num = 512;
    player->i2s_stream_writer = i2s_stream_init(&i2s_cfg);

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

    // Start with opus
    player->current_decoder = player->opus_decoder;
    player->current_codec = AUDIO_CODEC_OPUS;

    const char *link_tag[3] = {"file", "opus_dec", "i2s"};
    audio_pipeline_link(player->pipeline, &link_tag[0], 3);

    // Setup event listener
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    player->evt = audio_event_iface_init(&evt_cfg);
    audio_pipeline_set_listener(player->pipeline, player->evt);
}


// Your main playback loop
void playback_task(void* arg) {
    audio_player_t player = {0};
    audio_player_init(&player);

    // Start with opus file
    //audio_player_play_with_codec(&player, "/sdcard/afDCk/U7lT54DKU16tRK61rB5oYA_YdVxEx7o4Uvh5tuq5lWo", AUDIO_CODEC_OPUS);
    audio_player_play_with_codec(&player, "/sdcard/hMkni/7Ebgo8FrUdag_-94-Wx_5CrQFgU3QDDBlrl5Q8_q_jk", AUDIO_CODEC_OPUS);
    //audio_player_play_with_codec(&player, "/sdcard/test.mp3", AUDIO_CODEC_MP3);


    while (1) {
        audio_event_iface_msg_t msg;
        esp_err_t ret = audio_event_iface_listen(player.evt, &msg, pdMS_TO_TICKS(500));

        if (ret != ESP_OK) continue;

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

            ESP_LOGW(TAG, "Song finished");
            QueueHandle_t queue = audio_event_iface_get_queue_handle(player.evt);  // Clear event queue to avoid processing old events on next song
            xQueueReset(queue);
            // Auto-play next song or wait for command
            //vTaskDelay(pdMS_TO_TICKS(20000));
            //load_icon("/sdcard/afDCk/AjJaUh665wfnb72_y5uQ3M0w3JtobwIVfGua_A_j6i8");
            //audio_player_play_with_codec(&player, "/sdcard/test.mp3", AUDIO_CODEC_MP3);
            audio_player_play_with_codec(&player, "/sdcard/hMkni/7Ebgo8FrUdag_-94-Wx_5CrQFgU3QDDBlrl5Q8_q_jk", AUDIO_CODEC_OPUS);
        }
    }
}
