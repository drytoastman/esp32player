
#include "cr95hf.h"
#include "all.h"
#include "esp_rom_sys.h"

static char *TAG = "cr95hf";

void cr95hfv5_init(cr95hfv5_t *dev) {
    ESP_LOGI(TAG, "Add device");

    ESP_ERROR_CHECK(spi_bus_add_device(VSPI_HOST, &(spi_device_interface_config_t) {
        .clock_speed_hz = 2000000,  // start conservative (e.g. 2 MHz)
        .mode = 0, // SPI,
        .spics_io_num = -1,
        .queue_size = 3
    }, &dev->spi_handle));

    // 'wake' NFC chip
    nfc_irq(0);
    esp_rom_delay_us(20);
    nfc_irq(1);
}


void cr95hf_wait() {
    while (1) {
        if (!nfc_irq_check()) break; // NFC irq_out went low
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}


void cr95hf_tx(cr95hfv5_t *dev, char *tx, int tx_len) {
    spi_transaction_t t = {
        .length = tx_len * 8,
        .tx_buffer = tx,
        .rx_buffer = NULL,
        .rxlength = 0
    };

    if (xSemaphoreTake(spi_bus_mutex, pdMS_TO_TICKS(MAX_SPI_WAIT_MS)) == pdTRUE) {
        nfc_cs(0);
        esp_rom_delay_us(10);
        ESP_ERROR_CHECK(spi_device_transmit(dev->spi_handle, &t));
        nfc_cs(1);
        xSemaphoreGive(spi_bus_mutex);
    } else {
        ESP_LOGE(TAG, "Failed to acquire SPI bus mutex");
    }
}


char buf[530];
void cr95hf_rx(cr95hfv5_t *dev, char *rx, int *rx_len) {
    int response_code;
    char read[1] = {SPI_READ};

    spi_transaction_t t = {
        .length = 1 * 8,
        .tx_buffer = &read,
        .rx_buffer = rx,
        .rxlength = 0
    };

    if (xSemaphoreTake(spi_bus_mutex, pdMS_TO_TICKS(MAX_SPI_WAIT_MS)) == pdTRUE) {
        nfc_cs(0);
        esp_rom_delay_us(10);
        t.length = 3 * 8; // read the junk, response and length byte
        ESP_ERROR_CHECK(spi_device_transmit(dev->spi_handle, &t));

        response_code = rx[1];
        *rx_len = rx[2];

        // tx len bytes to get the data
        t.length = *rx_len * 8;
        t.rxlength = 0;
        t.tx_buffer = NULL;

        ESP_ERROR_CHECK(spi_device_transmit(dev->spi_handle, &t));
        nfc_cs(1);
        xSemaphoreGive(spi_bus_mutex);
    } else {
        ESP_LOGE(TAG, "Failed to acquire SPI bus mutex");
    }
}


void cr95hf_info(cr95hfv5_t *dev) {
    char out[3] = { 0, CMD_IDN, 0 };
    cr95hf_tx(dev, out, sizeof(out));
    cr95hf_wait();

    int datalen;
    cr95hf_rx(dev, buf, &datalen);

    ESP_LOGI(TAG, "IDN (%d) '%s'", datalen, buf);
}