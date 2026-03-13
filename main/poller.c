
#include "esp_log.h"
#include "all.h"

#define VOLUME_MIN 0
#define VOLUME_MAX 100

static const char *TAG = "poller";

adc_oneshot_chan_cfg_t light_sensor_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12
};

adc_oneshot_chan_cfg_t battery_voltage_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12
};

adc_oneshot_unit_handle_t adc_handle;

// EMA parameters (alpha = 1/2 for light, 1/32 for voltage)
#define ALPHA_LIGHT_SHIFT 1
#define ALPHA_VOLT_SHIFT 5

// Oversample count (optional for extra smoothing)
#define OVERSAMPLE 4

// Global accumulator variables
static uint32_t voltage_acc = 0;
static uint32_t light_acc = 0;

// Filtered output
static uint32_t voltage_filtered = 0;
static uint32_t light_filtered = 0;

void poller_init()
{
    pi4ioe5v6416_init(&iox);
    pi4ioe5v6416_write_reg(&iox, PI4IOE5V6416_CONFIG_PORT0,      0x30);
    pi4ioe5v6416_write_reg(&iox, PI4IOE5V6416_CONFIG_PORT1,      0xAF);
    pi4ioe5v6416_write_reg(&iox, PI4IOE5V6416_PULL_ENABLE_PORT0, 0x30);
    pi4ioe5v6416_write_reg(&iox, PI4IOE5V6416_PULL_ENABLE_PORT1, 0xA8);
    pi4ioe5v6416_write_reg(&iox, PI4IOE5V6416_PULL_DIR_PORT0,    0x30);
    pi4ioe5v6416_write_reg(&iox, PI4IOE5V6416_PULL_DIR_PORT1,    0xA8);
    pi4ioe5v6416_write_reg(&iox, PI4IOE5V6416_OUTPUT_PORT0,      0xFF);
    pi4ioe5v6416_write_reg(&iox, PI4IOE5V6416_OUTPUT_PORT1,      0xFF);
    pi4ioe5v6416_write_pin(&iox, output_params.iox.pactl,           0);  // make sure PA is off at startup


    adc_oneshot_new_unit(&(adc_oneshot_unit_init_cfg_t) {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE
    }, &adc_handle);
    adc_oneshot_config_channel(adc_handle, analog_params.light_sensor, &light_sensor_config);
    adc_oneshot_config_channel(adc_handle, analog_params.battery_voltage, &battery_voltage_config);
}


/**
 * Task for all the polling junk. A lots of this is the loading of the
 * two registers of the IOX chip and parsing out the individual bits
 */
void poller_task(void *ignored)
{
     /* Light sleep is not required to be able to use the PCNT unit, but it can be used to save power between events. */
#if CONFIG_EXAMPLE_LIGHT_SLEEP
     ESP_LOGI(TAG, "Enabling light sleep. The rotaries task will wake up the chip when an event is detected.");
     // EC11 channel output high level in normal state, so we set "low level" to wake up the chip
     ESP_ERROR_CHECK(gpio_wakeup_enable(EXAMPLE_EC11_GPIO_A, GPIO_INTR_LOW_LEVEL));
     ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());
#endif

    input_config current, previous;  // use as mass organized variables
    uint8_t banka, bankb;
    int both;
    time_t power_button_time = time(NULL);
    uint32_t cycle = 0;

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

    while (true) {
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
            ESP_LOGD(TAG, "IOX Volume pin changed to: %d", current.iox.volume);
            gct_send(APP_LEFT_BUTTON, 0, current.iox.volume);
        }
        if (current.iox.right != previous.iox.right) {
            ESP_LOGD(TAG, "IOX Right pin changed to: %d", current.iox.right);
            gct_send(APP_RIGHT_BUTTON, 0, current.iox.right);
        }

        if (current.iox.power) {
            power_button_time = time(NULL);
        } else {
            // If power button is held for more than 5 seconds, trigger a reset, power circuit will turn off
            if (time(NULL) - power_button_time > 5) {
                gct_send(APP_POWER_BUTTON, 0, 0);
            }
        }

        // current.iox.nfc_irq
        // This is polled by the nfc_irq_check function, so we don't need to log it here and it gets noisy

        if (current.iox.hp_detect != previous.iox.hp_detect) {
            // I don't know if this is tracking correctly as it sometimes flips when I plug/unplug headphones,
            // but other times it doesn't.
            ESP_LOGI(TAG, "IOX HP Detect pin changed to: %d", current.iox.hp_detect);
        }
        if (current.iox.tilt != previous.iox.tilt) {
            gct_send(APP_TILT, 0, current.iox.tilt);
            ESP_LOGD(TAG, "IOX Tilt pin changed to: %d", current.iox.tilt);
        }
        if (current.iox.plug_stat != previous.iox.plug_stat) {
            gct_send(APP_PLUG, 0, current.iox.plug_stat);
            ESP_LOGD(TAG, "IOX Plug Stat pin changed to: %d", current.iox.plug_stat);
        }
        if (current.iox.charge_stat != previous.iox.charge_stat) {
            gct_send(APP_CHARGE, 0, current.iox.plug_stat);
            ESP_LOGD(TAG, "IOX Charge Stat pin changed to: %d", current.iox.charge_stat);
        }
        previous = current;

        /*********** Analog *********/
        int measure = 0;
        cycle++;

        // --- Voltage Sensor ---
        uint32_t voltage_sum = 0;
        for (int i = 0; i < OVERSAMPLE; i++) {
            if (adc_oneshot_read(adc_handle, analog_params.battery_voltage, &measure) == ESP_OK) {
                voltage_sum += measure;
            }
        }
        // average oversample
        measure = voltage_sum / OVERSAMPLE;

        // EMA filter
        voltage_acc = voltage_acc - (voltage_acc >> ALPHA_VOLT_SHIFT) + measure;
        voltage_filtered = voltage_acc >> ALPHA_VOLT_SHIFT;

        // --- Light Sensor ---
        uint32_t light_sum = 0;
        for (int i = 0; i < OVERSAMPLE; i++) {
            if (adc_oneshot_read(adc_handle, analog_params.light_sensor, &measure) == ESP_OK) {
                light_sum += measure;
            }
        }
        measure = light_sum / OVERSAMPLE;

        light_acc = light_acc - (light_acc >> ALPHA_LIGHT_SHIFT) + measure;
        light_filtered = light_acc >> ALPHA_LIGHT_SHIFT;

        // --- Logging every 64 cycles (~1.28s if delay=20ms) ---
        if ((cycle % 64) == 0) {
            gct_send(APP_BATTERY, 0, voltage_filtered);
            gct_send(APP_LIGHT, 0, light_filtered);
        }


        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

