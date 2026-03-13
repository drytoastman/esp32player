
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "board.h"

#include "all.h"

static const char *TAG = "main";

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

lis2dh12_ctx accel = {
    .address = LIS2DH12_ADDR,
};


spi_device_handle_t display;
SemaphoreHandle_t spi_bus_mutex;
i2c_bus_handle_t i2c_bus;


void shutdown_handler() {
    ESP_LOGI(TAG, "System is shutting down");
}

void app_main(void)
{
    ESP_LOGI(TAG, "Free internal memory: 0x%X bytes", heap_caps_get_free_size(MALLOC_CAP_INTERNAL)/1024);
    ESP_LOGI(TAG, "Free spiram memory: 0x%X bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM)/1024);

    esp_log_level_set("*", ESP_LOG_INFO);

    // Mount the SD card and grab the first SPI bus
    mount_sdcard();

    esp_register_shutdown_handler(shutdown_handler);

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
    spi_bus_initialize(VSPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
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

    gct_init();
    rotary_init();
    poller_init();
    lis2dh12_init(&accel);
    display_init(&display);  // requires poller_init for using IOX chip selects

    xTaskCreatePinnedToCore(grand_central_task, "gct",      4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(poller_task,        "poller",   4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(nfc_processor,      "nfc",      3072, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(playback_task,      "playback", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(debugio_task,       "debugio",  4096, NULL, 4, NULL, 1);
    // audio stuff (decoder, etc) put themselves on CPU 0

    //start_wifi();
    //start_webserver();

    vTaskDelay(pdMS_TO_TICKS(1000));
    while (0) {
        char buffer[1024];
        vTaskGetRunTimeStats(buffer);
        ESP_LOGI(TAG, "%s", buffer);
        ESP_LOGI(TAG, "Free internal memory: %d kB", heap_caps_get_free_size(MALLOC_CAP_INTERNAL)/1024);
        ESP_LOGI(TAG, "Free spiram memory: %d kB", heap_caps_get_free_size(MALLOC_CAP_SPIRAM)/1024);
        ESP_LOGI(TAG, "DMA free: %d kB", heap_caps_get_free_size(MALLOC_CAP_DMA)/1024);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

