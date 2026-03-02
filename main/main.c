
#include "all.h"
#include "esp_rom_sys.h"
#include "driver/spi_master.h"

static const char *TAG = "MAIN";

input_config input_params = {
    .gpio = {
        .volume_a = GPIO_NUM_5,
        .volume_b = GPIO_NUM_13,
        .right_a = GPIO_NUM_4,
        .right_b = GPIO_NUM_35
    },

    .iox = {
        .volume = 4,
        .right = 5,
        .hp_detect = 9,
        .nfc_irq = 8,
        .tilt = 10,
        .power = 11,
        .plug_stat = 13,
        .charge_stat = 15,
    }
};

analog_config analog_params = {
    .light_sensor = ADC_CHANNEL_0, // ADC1_0
    .battery_voltage = ADC_CHANNEL_3, // ADC1_3
};

output_cfg output_params = {
    .night_red = GPIO_NUM_12,
    .night_green = GPIO_NUM_23,
    .night_blue = GPIO_NUM_19,

    .iox = {
        .display = { 3, 2, 1, 0 },
        .pactl = 6,
        .nfc_irq = 7,
        .nfc_cs = 12,
        .vin_hold = 14
    }
};

pi4ioe5v6416_t iox = {
    .address = PI4IOE_ADDR,
};

spi_device_handle_t cr95hf, ht16d35a;
SemaphoreHandle_t spi_bus_mutex;

void app_main(void)
{
    ESP_LOGI(TAG, "Free internal memory: 0x%X bytes", heap_caps_get_free_size(MALLOC_CAP_INTERNAL)/1024);
    ESP_LOGI(TAG, "Free spiram memory: 0x%X bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM)/1024);

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("spi_hal", ESP_LOG_DEBUG);
    esp_log_level_set("spi_master", ESP_LOG_DEBUG);
    esp_log_level_set("spi_slave", ESP_LOG_DEBUG);
    esp_log_level_set("spi", ESP_LOG_DEBUG);
    esp_log_level_set("spi_flash", ESP_LOG_DEBUG);

    digital_init();
    analog_init();

    // SDCARD uses HSPI, something else is using SPI so we use VSPI
    spi_bus_config_t buscfg = {
        .mosi_io_num = GPIO_NUM_18,
        .miso_io_num = GPIO_NUM_21,
        .sclk_io_num = GPIO_NUM_2,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 0 // cr95h can send up to 528 bytes of data
    };
    ESP_ERROR_CHECK(spi_bus_initialize(VSPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    spi_bus_mutex = xSemaphoreCreateMutex();
    if (spi_bus_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create SPI bus mutex");
    }

    /*
    cr95hf_init(&cr95hf);
    vTaskDelay(pdMS_TO_TICKS(10)); // Short delay to ensure CR95HF is ready before proceeding
    cr95hf_info(cr95hf);
    */

    ht16d35a_init(&ht16d35a);

    xTaskCreatePinnedToCore(digital_processor, "digital_processor", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(analog_processor, "analog_processor", 1024, NULL, 5, NULL, 1);
    //xTaskCreatePinnedToCore(sound_main, "sound_main", 4096, NULL, 5, NULL, 1); // streams will go on 0?

    start_wifi();
    start_webserver();

    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "Free internal memory: 0x%X bytes", heap_caps_get_free_size(MALLOC_CAP_INTERNAL)/1024);
    ESP_LOGI(TAG, "Free spiram memory: 0x%X bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM)/1024);

    /**
    while (1) {
        char buffer[1024];
        vTaskGetRunTimeStats(buffer);
        printf("%s\n", buffer);
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
        */
}

