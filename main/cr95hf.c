
#include "cr95hf.h"
#include "all.h"
#include "esp_rom_sys.h"

// Command Codes
#define CMD_IDN 0x01
#define CMD_PROTOCOL 0x02
#define CMD_SENDRECV 0x04
#define CMD_ECHO 0x55

// Response Codes
#define RSP_SUCCESS 0x00
#define RSP_DATA 0x80

// SPI Commans
#define SPI_WRITE 0x00
#define SPI_READ 0x02

// Protocol Codes
#define PROTO_OFF 0x00
#define PROTO_ISO14443A 0x02

// ISO14443-A Commands
#define ISO14443A_REQA 0x26
#define ISO14443A_WUPA 0x52
#define ISO14443A_CT 0x88
#define ISO14443A_SEL_CL1 0x93
#define ISO14443A_SEL_CL2 0x95
#define ISO14443A_NVB_ANTICOLL 0x20
#define ISO14443A_NVB_SELECT 0x70

// Transmit Flags
#define FLAG_SHORTFRAME 0x07
#define FLAG_STD 0x08
#define FLAG_STD_CRC 0x28

// SAK Card Types
#define SAK_MIFARE_UL 0x00
#define SAK_MIFARE_1K 0x08
#define SAK_MIFARE_MINI 0x09
#define SAK_MIFARE_4K 0x18

static char *TAG = "cr95hf";

void cr95hf_init(spi_device_handle_t *dev) {
    ESP_LOGI(TAG, "Add device");

    ESP_ERROR_CHECK(spi_bus_add_device(VSPI_HOST, &(spi_device_interface_config_t) {
        .clock_speed_hz = 2000000,  // start conservative (e.g. 2 MHz)
        .mode = 0, // SPI,
        .spics_io_num = -1,
        .queue_size = 3
    }, dev));

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


void cr95hf_tx(spi_device_handle_t dev, char *tx, int tx_len) {
    spi_transaction_t t = {
        .length = tx_len * 8,
        .tx_buffer = tx,
        .rx_buffer = NULL,
        .rxlength = 0
    };

    if (xSemaphoreTake(spi_bus_mutex, pdMS_TO_TICKS(MAX_SPI_WAIT_MS)) == pdTRUE) {
        nfc_cs(0);
        esp_rom_delay_us(10);
        ESP_ERROR_CHECK(spi_device_transmit(dev, &t));
        nfc_cs(1);
        xSemaphoreGive(spi_bus_mutex);
    } else {
        ESP_LOGE(TAG, "Failed to acquire SPI bus mutex");
    }
}


char buf[530];
void cr95hf_rx(spi_device_handle_t dev, char *rx, int *rx_len) {
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
        ESP_ERROR_CHECK(spi_device_transmit(dev, &t));

        response_code = rx[1];
        *rx_len = rx[2];

        // tx len bytes to get the data
        t.length = *rx_len * 8;
        t.rxlength = 0;
        t.tx_buffer = NULL;

        ESP_ERROR_CHECK(spi_device_transmit(dev, &t));
        nfc_cs(1);
        xSemaphoreGive(spi_bus_mutex);
    } else {
        ESP_LOGE(TAG, "Failed to acquire SPI bus mutex");
    }
}


void cr95hf_info(spi_device_handle_t dev) {
    char out[3] = { 0, CMD_IDN, 0 };
    cr95hf_tx(dev, out, sizeof(out));
    cr95hf_wait();

    int datalen;
    cr95hf_rx(dev, buf, &datalen);

    ESP_LOGI(TAG, "IDN (%d) '%s'", datalen, buf);
}