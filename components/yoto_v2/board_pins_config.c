#include "esp_log.h"
#include "driver/gpio.h"
#include <string.h>
#include "board.h"
#include "audio_error.h"
#include "audio_mem.h"
#include "soc/io_mux_reg.h"
#include "soc/soc_caps.h"

static const char *TAG = "YOTO_V2";

esp_err_t get_i2c_pins(i2c_port_t port, i2c_config_t *i2c_config)
{
    AUDIO_NULL_CHECK(TAG, i2c_config, return ESP_FAIL);
    if (port == I2C_NUM_0 || port == I2C_NUM_1) {
        i2c_config->sda_io_num = GPIO_NUM_26;
        i2c_config->scl_io_num = GPIO_NUM_27;
    } else {
        i2c_config->sda_io_num = -1;
        i2c_config->scl_io_num = -1;
        ESP_LOGE(TAG, "i2c port %d is not supported", port);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t get_i2s_pins(int port, board_i2s_pin_t *i2s_config)
{
    AUDIO_NULL_CHECK(TAG, i2s_config, return ESP_FAIL);
    if (port == 0 || port == 1) {
        i2s_config->mck_io_num = GPIO_NUM_0;
        i2s_config->bck_io_num = GPIO_NUM_25; // GPIO_NUM_5;
        i2s_config->ws_io_num = GPIO_NUM_32;
        i2s_config->data_out_num = GPIO_NUM_33; // GPIO_NUM_26;
        i2s_config->data_in_num = -1; //GPIO_NUM_35;
    } else {
        memset(i2s_config, -1, sizeof(board_i2s_pin_t));
        ESP_LOGE(TAG, "i2s port %d is not supported", port);
        return ESP_FAIL;
    }
    return ESP_OK;
}

// sdcard

// int8_t get_sdcard_intr_gpio(void)
// {
//     return SDCARD_INTR_GPIO;
// }

// int8_t get_sdcard_open_file_num_max(void)
// {
//     return SDCARD_OPEN_FILE_NUM_MAX;
// }

// // input-output pins

// int8_t get_auxin_detect_gpio(void)
// {
//     return AUXIN_DETECT_GPIO;
// }

int8_t get_pa_enable_gpio(void)
{
    return PA_ENABLE_GPIO;
}

// button pins
/*
int8_t get_input_rec_id(void)
{
    return BUTTON_REC_ID;
}

int8_t get_input_mode_id(void)
{
    return BUTTON_MODE_ID;
}

// touch pins

int8_t get_input_set_id(void)
{
    return BUTTON_SET_ID;
}

int8_t get_input_play_id(void)
{
    return BUTTON_PLAY_ID;
}

int8_t get_input_volup_id(void)
{
    return BUTTON_VOLUP_ID;
}

int8_t get_input_voldown_id(void)
{
    return BUTTON_VOLDOWN_ID;
}
*/

// led pins

// int8_t get_red_led_gpio(void)
// {
//     return RED_LED_GPIO;
// }

// int8_t get_green_led_gpio(void)
// {
//     return GREEN_LED_GPIO;
// }
