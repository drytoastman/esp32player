
#include "all.h"
#include "esp_log.h"

#define VOLUME_MIN 0
#define VOLUME_MAX 100

static const char *TAG = "digital";

pcnt_unit_handle_t volume_pcnt, rightknob_pcnt;

void turn_off_pa() {
    ESP_LOGI(TAG, "Turning off PA");
    pi4ioe5v6416_write_pin(&iox, input_params.iox.power, 0);
}

void turn_on_pa() {
    ESP_LOGI(TAG, "Turning on PA");
    pi4ioe5v6416_write_pin(&iox, input_params.iox.power, 1);
}


pcnt_unit_handle_t rotary_init(int inputA, int inputB)
{
    ESP_LOGI(TAG, "install pcnt unit");
    pcnt_unit_config_t unit_config = {
        .high_limit = SHRT_MAX,
        .low_limit = SHRT_MIN,
    };

    pcnt_unit_handle_t pcnt_unit = NULL;
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));

    ESP_LOGI(TAG, "set glitch filter");
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 1000,
    };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));

    ESP_LOGI(TAG, "install pcnt channels");
    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num = inputA,
        .level_gpio_num = inputB,
    };
    pcnt_channel_handle_t pcnt_chan_a = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_a_config, &pcnt_chan_a));

    pcnt_chan_config_t chan_b_config = {
        .edge_gpio_num = inputB,
        .level_gpio_num = inputA,
    };
    pcnt_channel_handle_t pcnt_chan_b = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_b_config, &pcnt_chan_b));

    ESP_LOGI(TAG, "set edge and level actions for pcnt channels");
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    ESP_LOGI(TAG, "enable pcnt unit");
    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
    ESP_LOGI(TAG, "clear pcnt unit");
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
    ESP_LOGI(TAG, "start pcnt unit");
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));

#if 0
    // EC11 channel output high level in normal state, so we set "low level" to wake up the chip
    ESP_ERROR_CHECK(gpio_wakeup_enable(EXAMPLE_EC11_GPIO_A, GPIO_INTR_LOW_LEVEL));
    ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());
    ESP_ERROR_CHECK(esp_light_sleep_start());
#endif

    return pcnt_unit;
}


void digital_init()
{
    //pi4ioe5v6416_t pi4ioe5v6416_dev;
    pi4ioe5v6416_init(&iox);
    pi4ioe5v6416_write_reg(&iox, PI4IOE5V6416_CONFIG_PORT0,      0x30);
    pi4ioe5v6416_write_reg(&iox, PI4IOE5V6416_CONFIG_PORT1,      0xAF);
    pi4ioe5v6416_write_reg(&iox, PI4IOE5V6416_PULL_ENABLE_PORT0, 0x30);
    pi4ioe5v6416_write_reg(&iox, PI4IOE5V6416_PULL_ENABLE_PORT1, 0xA8);
    pi4ioe5v6416_write_reg(&iox, PI4IOE5V6416_PULL_DIR_PORT0,    0x30);
    pi4ioe5v6416_write_reg(&iox, PI4IOE5V6416_PULL_DIR_PORT1,    0xA8);
    pi4ioe5v6416_write_reg(&iox, PI4IOE5V6416_OUTPUT_PORT0,      0xFF);
    pi4ioe5v6416_write_reg(&iox, PI4IOE5V6416_OUTPUT_PORT1,      0xFF);
    vTaskDelay(pdMS_TO_TICKS(1)); // Short delay to ensure pins are set before proceeding with SPI transactions

    pi4ioe5v6416_write_pin(&iox, 6, 0); // Mute audio initially
    pi4ioe5v6416_write_pin(&iox, 7, 0); // Set irqin low for CR95HF
    esp_rom_delay_us(20);
    pi4ioe5v6416_write_pin(&iox, 7, 1); // Set irqin high for CR95HF
    vTaskDelay(pdMS_TO_TICKS(10)); // Short delay to ensure CR95HF is ready before proceeding


    volume_pcnt    = rotary_init(input_params.gpio.volume_a, input_params.gpio.volume_b);
    rightknob_pcnt = rotary_init(input_params.gpio.right_a, input_params.gpio.right_b);
}


/**
 * Task for all the polling junk. A lots of this is the loading of the
 * two registers of the IOX chip and parsing out the individual bits
 */
void digital_processor(void *ignored)
{

     /* Light sleep is not required to be able to use the PCNT unit, but it can be used to save power between events. */
#if CONFIG_EXAMPLE_LIGHT_SLEEP
     ESP_LOGI(TAG, "Enabling light sleep. The rotaries task will wake up the chip when an event is detected.");
     // EC11 channel output high level in normal state, so we set "low level" to wake up the chip
     ESP_ERROR_CHECK(gpio_wakeup_enable(EXAMPLE_EC11_GPIO_A, GPIO_INTR_LOW_LEVEL));
     ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());
#endif

    int volume = 50;  // TODO, initialize to current volume
    input_config current, previous;  // use as mass organized variables
    uint8_t banka, bankb;
    int both;
    int current_volume;
    int current_rightknob;
    int current_mute = 0;

    // Initialize previous state of IOX input pins
    pi4ioe5v6416_read_reg(&iox, 0, &banka);
    pi4ioe5v6416_read_reg(&iox, 1, &bankb);
    both = banka | (bankb << 8);

    previous.iox.volume      = (both >> input_params.iox.volume) & 0x01;
    previous.iox.right       = (both >> input_params.iox.right) & 0x01;
    previous.iox.nfc_irq     = (both >> input_params.iox.nfc_irq) & 0x01;
    previous.iox.hp_detect   = (both >> input_params.iox.hp_detect) & 0x01;
    previous.iox.tilt        = (both >> input_params.iox.tilt) & 0x01;
    previous.iox.power       = (both >> input_params.iox.power) & 0x01;
    previous.iox.plug_stat   = (both >> input_params.iox.plug_stat) & 0x01;
    previous.iox.charge_stat = (both >> input_params.iox.charge_stat) & 0x01;

    // Initialize baseline
    pcnt_unit_get_count(volume_pcnt, &current_volume);
    pcnt_unit_get_count(rightknob_pcnt, &current_rightknob);

    for (;;) {
        int measure;
        int delta;

        // volume
        ESP_ERROR_CHECK(pcnt_unit_get_count(volume_pcnt, &measure));
        delta = measure - current_volume;
        if (delta != 0) {
            current_volume = measure;
            volume += delta;
            if (volume > VOLUME_MAX) volume = VOLUME_MAX;
            if (volume < VOLUME_MIN) volume = VOLUME_MIN;

            ESP_LOGI(TAG, "Volume: %d", volume);

            // Apply to codec here
            if (board_handle && board_handle->audio_hal) {
                audio_hal_set_volume(board_handle->audio_hal, volume);
            } else {
                ESP_LOGW(TAG, "Audio board not initialized yet, cannot set volume");
            }
        }

        // right knob
        ESP_ERROR_CHECK(pcnt_unit_get_count(rightknob_pcnt, &measure));
        delta = measure - current_rightknob;
        if (delta != 0) {
            current_rightknob = measure;
            ESP_LOGI(TAG, "Right knob: %d", measure);
        }

        // IOX input pins
        pi4ioe5v6416_read_reg(&iox, 0, &banka);
        pi4ioe5v6416_read_reg(&iox, 1, &bankb);
        both = banka | (bankb << 8);

        current.iox.volume      = (both >> input_params.iox.volume) & 0x01;
        current.iox.right       = (both >> input_params.iox.right) & 0x01;
        current.iox.nfc_irq     = (both >> input_params.iox.nfc_irq) & 0x01;
        current.iox.hp_detect   = (both >> input_params.iox.hp_detect) & 0x01;
        current.iox.tilt        = (both >> input_params.iox.tilt) & 0x01;
        current.iox.power       = (both >> input_params.iox.power) & 0x01;
        current.iox.plug_stat   = (both >> input_params.iox.plug_stat) & 0x01;
        current.iox.charge_stat = (both >> input_params.iox.charge_stat) & 0x01;

        if (current.iox.volume != previous.iox.volume) {
            ESP_LOGI(TAG, "IOX Volume pin changed to: %d", current.iox.volume);

            if (!current.iox.volume) {
                current_mute = !current_mute; // Toggle mute state
                if (!current_mute) { // inverted
                    ESP_LOGI(TAG, "Muting audio");
                    turn_off_pa();
                } else {
                    ESP_LOGI(TAG, "Unmuting audio");
                    turn_on_pa();
                }
            }
        }
        if (current.iox.right != previous.iox.right) {
            ESP_LOGI(TAG, "IOX Right pin changed to: %d", current.iox.right);
        }
        if (current.iox.nfc_irq != previous.iox.nfc_irq) {
            ESP_LOGI(TAG, "IOX NFC IRQ pin changed to: %d", current.iox.nfc_irq);
        }
        if (current.iox.hp_detect != previous.iox.hp_detect) {
            ESP_LOGI(TAG, "IOX HP Detect pin changed to: %d", current.iox.hp_detect);
        }
        if (current.iox.tilt != previous.iox.tilt) {
            ESP_LOGI(TAG, "IOX Tilt pin changed to: %d", current.iox.tilt);
        }
        if (current.iox.power != previous.iox.power) {
            ESP_LOGI(TAG, "IOX Power pin changed to: %d", current.iox.power);
        }
        if (current.iox.plug_stat != previous.iox.plug_stat) {
            ESP_LOGI(TAG, "IOX Plug Stat pin changed to: %d", current.iox.plug_stat);
        }
        if (current.iox.charge_stat != previous.iox.charge_stat) {
            ESP_LOGI(TAG, "IOX Charge Stat pin changed to: %d", current.iox.charge_stat);
        }
        previous = current;

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

