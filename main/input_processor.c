
#include "all.h"
#include "esp_log.h"

#define VOLUME_MIN 0
#define VOLUME_MAX 100

static const char *TAG = "inputer";

#if 0
void IRAM_ATTR brownout_isr() {
    Serial.println("Brownout interrupt triggered! Turn off PA");
    // Your safe shutdown code here
    pi4ioe5v6416_write_pin(&params->iox.dev, 6, 0);
}
#endif

void input_processor(void *inputParameters)
{
    input_task_params_t *params = (input_task_params_t *)inputParameters;
    // pcnt_unit_handle_t pcnt_unit = params->volume;

     /* Light sleep is not required to be able to use the PCNT unit, but it can be used to save power between events. */
#if CONFIG_EXAMPLE_LIGHT_SLEEP
     ESP_LOGI(TAG, "Enabling light sleep. The rotaries task will wake up the chip when an event is detected.");
     // EC11 channel output high level in normal state, so we set "low level" to wake up the chip
     ESP_ERROR_CHECK(gpio_wakeup_enable(EXAMPLE_EC11_GPIO_A, GPIO_INTR_LOW_LEVEL));
     ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());
#endif

    int volume = 50;  // start wherever you like
    input_task_params_t current, previous;
    uint8_t banka, bankb;
    int both;
    int current_volume;
    int current_rightknob;
    int current_light_sensor = 0;
    int current_mute = 0;

    // Initialize previous state of IOX input pins
    pi4ioe5v6416_read_reg(&params->iox.dev, 0, &banka);
    pi4ioe5v6416_read_reg(&params->iox.dev, 1, &bankb);
    both = banka | (bankb << 8);

    previous.iox.volume      = (both >> params->iox.volume) & 0x01;
    previous.iox.right       = (both >> params->iox.right) & 0x01;
    previous.iox.nfc_irq_out = (both >> params->iox.nfc_irq_out) & 0x01;
    previous.iox.hp_detect   = (both >> params->iox.hp_detect) & 0x01;
    previous.iox.tilt        = (both >> params->iox.tilt) & 0x01;
    previous.iox.power       = (both >> params->iox.power) & 0x01;
    previous.iox.plug_stat   = (both >> params->iox.plug_stat) & 0x01;
    previous.iox.charge_stat = (both >> params->iox.charge_stat) & 0x01;

    // Initialize baseline
    pcnt_unit_get_count(params->volume, &current_volume);
    pcnt_unit_get_count(params->rightknob, &current_rightknob);

    for (;;) {
        int measure;
        int delta;

        // volume
        ESP_ERROR_CHECK(pcnt_unit_get_count(params->volume, &measure));
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
        ESP_ERROR_CHECK(pcnt_unit_get_count(params->rightknob, &measure));
        delta = measure - current_rightknob;
        if (delta != 0) {
            current_rightknob = measure;
            ESP_LOGI(TAG, "Right knob: %d", measure);
        }

        // light sensor
        ESP_ERROR_CHECK(adc_oneshot_read(params->light_sensor, 0, &measure));
        if (measure != current_light_sensor) {
            current_light_sensor = measure;
            ESP_LOGI(TAG, "Light sensor: %d", measure);
        }


        // IOX input pins
        pi4ioe5v6416_read_reg(&params->iox.dev, 0, &banka);
        pi4ioe5v6416_read_reg(&params->iox.dev, 1, &bankb);
        both = banka | (bankb << 8);

        current.iox.volume      = (both >> params->iox.volume) & 0x01;
        current.iox.right       = (both >> params->iox.right) & 0x01;
        current.iox.nfc_irq_out = (both >> params->iox.nfc_irq_out) & 0x01;
        current.iox.hp_detect   = (both >> params->iox.hp_detect) & 0x01;
        current.iox.tilt        = (both >> params->iox.tilt) & 0x01;
        current.iox.power       = (both >> params->iox.power) & 0x01;
        current.iox.plug_stat   = (both >> params->iox.plug_stat) & 0x01;
        current.iox.charge_stat = (both >> params->iox.charge_stat) & 0x01;

        if (current.iox.volume != previous.iox.volume) {
            ESP_LOGI(TAG, "IOX Volume pin changed to: %d", current.iox.volume);

            if (!current.iox.volume) {
                current_mute = !current_mute; // Toggle mute state
                if (!current_mute) { // inverted
                    ESP_LOGI(TAG, "Muting audio");
                } else {
                    ESP_LOGI(TAG, "Unmuting audio");
                }
                pi4ioe5v6416_write_pin(&params->iox.dev, 6, current_mute);
            }
        }
        if (current.iox.right != previous.iox.right) {
            ESP_LOGI(TAG, "IOX Right pin changed to: %d", current.iox.right);
        }
        if (current.iox.nfc_irq_out != previous.iox.nfc_irq_out) {
            ESP_LOGI(TAG, "IOX NFC IRQ OUT pin changed to: %d", current.iox.nfc_irq_out);
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

