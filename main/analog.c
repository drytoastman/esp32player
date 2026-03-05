#include "all.h"

static const char *TAG = "analog";

adc_oneshot_chan_cfg_t light_sensor_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12
};

adc_oneshot_chan_cfg_t battery_voltage_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12
};

adc_oneshot_unit_handle_t adc_handle;

void analog_init() {
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&(adc_oneshot_unit_init_cfg_t) {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE
    }, &adc_handle));

    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, analog_params.light_sensor, &light_sensor_config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, analog_params.battery_voltage, &battery_voltage_config));
}

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

void analog_processor(void *ignored)
{
    uint32_t cycle = 0;

    for (;;)
    {
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
            ESP_LOGI(TAG, "Light: %u", light_filtered); // 0 - 4095
            ESP_LOGI(TAG, "Voltage: %u", voltage_filtered);

            int scaled = light_filtered >> 6;
            ht16d35a_brightness(&ht16d35a, scaled ? scaled : 1); // reduce to 1-63 range
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
