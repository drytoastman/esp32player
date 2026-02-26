#include "all.h"

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


void analog_processor(void *ignored) {
    int current_light_sensor = 0;
    for (;;) {
        int measure;

        // light sensor
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, analog_params.light_sensor, &measure));
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, analog_params.light_sensor, &measure));
        if (measure != current_light_sensor) {
            current_light_sensor = measure;
            //ESP_LOGI(TAG, "Light sensor: %d", measure);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

}
