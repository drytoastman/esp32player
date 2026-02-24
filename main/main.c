
#include "all.h"

static const char *TAG = "MAIN";
#define PI4IOE_ADDR 0x40  // or 0x42 depending on the state of the ADDR pin

input_task_params_t input_task_params = {
    .gpio = {
        .volume_a = GPIO_NUM_5,
        .volume_b = GPIO_NUM_13,
        .right_a = GPIO_NUM_4,
        .right_b = GPIO_NUM_35,
        .light_sensor = GPIO_NUM_36, // ADC1_0
        .battery_voltage = GPIO_NUM_39,  // ADC1_3
    },

    .iox = {
        .volume = 4,
        .right = 5,
        .nfc_irq_out = 8,
        .hp_detect = 9,
        .tilt = 10,
        .power = 11,
        .plug_stat = 13,
        .charge_stat = 15,
        .dev = {
            .address = PI4IOE_ADDR,
        },
    },

    .volume = NULL,
    .rightknob = NULL,
    .light_sensor = NULL,
};


void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);

    //pi4ioe5v6416_t pi4ioe5v6416_dev;
    pi4ioe5v6416_init(&input_task_params.iox.dev);
    pi4ioe5v6416_write_reg(&input_task_params.iox.dev, PI4IOE5V6416_CONFIG_PORT0,      0x30);
    pi4ioe5v6416_write_reg(&input_task_params.iox.dev, PI4IOE5V6416_CONFIG_PORT1,      0xAF);
    pi4ioe5v6416_write_reg(&input_task_params.iox.dev, PI4IOE5V6416_PULL_ENABLE_PORT0, 0x30);
    pi4ioe5v6416_write_reg(&input_task_params.iox.dev, PI4IOE5V6416_PULL_ENABLE_PORT1, 0xAA);
    pi4ioe5v6416_write_reg(&input_task_params.iox.dev, PI4IOE5V6416_PULL_DIR_PORT0,    0x30);
    pi4ioe5v6416_write_reg(&input_task_params.iox.dev, PI4IOE5V6416_PULL_DIR_PORT1,    0xAA);

    input_task_params.volume    = rotary_init(input_task_params.gpio.volume_a, input_task_params.gpio.volume_b);
    input_task_params.rightknob = rotary_init(input_task_params.gpio.right_a, input_task_params.gpio.right_b);

    ESP_ERROR_CHECK(adc_oneshot_new_unit(&(adc_oneshot_unit_init_cfg_t) {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE
    }, &input_task_params.light_sensor));

    // pi4ioe5v6416_write_pin(&pi4ioe5v6416_dev, 6, false); // Set pin 6 low to disable speaker

    xTaskCreate(input_processor, "input_processor", 4096, &input_task_params, 5, NULL);
    xTaskCreate(sound_main, "sound_main", 4096, NULL, 5, NULL);

}

