
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

stmdev_ctx_t accel = {
    .address = LIS2DH12_ADDR,
};


spi_device_handle_t cr95hf, ht16d35a;
SemaphoreHandle_t spi_bus_mutex;
i2c_bus_handle_t i2c_bus;

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

    // Start our other SPI bus
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

    // Make sure we have started the I2C bus, must be EXACTLY the same as the ADF version
    // For some reason, they keep their initialized handle in static file global and not
    // somewhere shared.
    esp_err_t res;
    i2c_config_t es_i2c_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000
    };
    res = get_i2c_pins(I2C_NUM_0, &es_i2c_cfg);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "getting i2c pins error: %d", res);
        return;  // Failed to get I2C pins
    }
    i2c_bus = i2c_bus_create(I2C_NUM_0, &es_i2c_cfg);

    accel.i2c_handle = i2c_bus;
    iox.i2c_handle = i2c_bus;

    digital_init();
    analog_init();

    lis2dh12_init(&accel);

    cr95hf_init(&cr95hf);
    vTaskDelay(pdMS_TO_TICKS(10)); // Short delay to ensure CR95HF is ready before proceeding
    cr95hf_info(cr95hf);

    ht16d35a_init(&ht16d35a);

    xTaskCreatePinnedToCore(digital_processor, "digital_processor", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(analog_processor, "analog_processor", 1024, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(sound_main, "sound_main", 4096, NULL, 5, NULL, 1); // streams will go on 0?

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

