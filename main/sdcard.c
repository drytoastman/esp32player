#include "all.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_vfs_fat.h"

static const char *TAG = "sdcard";

// #define ESP_SD_PIN_CLK            GPIO_NUM_14
// #define ESP_SD_PIN_CMD            GPIO_NUM_22 //mosi
// #define ESP_SD_PIN_D0             GPIO_NUM_34 //miso
// #define ESP_SD_PIN_D3             GPIO_NUM_15 //cs

void mount_sdcard() {
    esp_err_t ret;
    sdmmc_card_t* card;
    const char mount_point[] = MOUNT_POINT;
    ESP_LOGI(TAG, "Initializing SD card");

    // Configure SPI host
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = GPIO_NUM_22,
        .miso_io_num = GPIO_NUM_34,
        .sclk_io_num = GPIO_NUM_14,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    // Initialize the SPI bus
    ret = spi_bus_initialize(host.slot, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize bus.");
        return;
    }

    // Configure SPI slot
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = GPIO_NUM_15;
    slot_config.host_id = host.slot;

    // Mount configuration
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    // Mount the SD card
    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card (%s)", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "SD card mounted at '%s'", mount_point);
}
