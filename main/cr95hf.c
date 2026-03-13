
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "all.h"

// Command Codes
#define CMD_IDN 0x01
#define CMD_PROTOCOL 0x02
#define CMD_SENDRECV 0x04
#define CMD_ECHO 0x55

// Response Codes
#define RSP_SUCCESS 0x00
#define RSP_DATA 0x80
#define EXTRA_LENGTH_FLAG 0x80
#define EXTRA_LENGTH_MASK 0x60
#define ERROR_MASK 0x0F

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
#define ISO14443A_READ 0x30
#define ISO14443A_WRITE 0xA0
#define ISO14443A_HALT 0x50

#define ISO14443A_KEYA 0x60
#define ISO14443A_KEYA 0x61

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

void nfc_cs(bool level) {
    pi4ioe5v6416_write_pin(&iox, output_params.iox.nfc_cs, level);
}

void nfc_irq(bool level) {
    pi4ioe5v6416_write_pin(&iox, output_params.iox.nfc_irq, level);
}

bool nfc_irq_check() {
    return pi4ioe5v6416_read_pin(&iox, input_params.iox.nfc_irq);
}


void cr95hf_init(spi_device_handle_t *dev) {
    ESP_LOGI(TAG, "Add device");

    ESP_ERROR_CHECK(spi_bus_add_device(VSPI_HOST, &(spi_device_interface_config_t) {
        .clock_speed_hz = 2000000,  // start conservative (e.g. 2 MHz)
        .mode = 0, // SPI,
        .spics_io_num = -1,
        .queue_size = 3
    }, dev));

    // 'wake' NFC chip
    cr95hf_poke();
}


void cr95hf_poke() {
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


void cr95hf_tx(spi_device_handle_t dev, uint8_t *tx, int tx_len) {
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


// char buf[530];
esp_err_t cr95hf_rx(spi_device_handle_t dev, uint8_t *rx, int *rx_len) {
    esp_err_t response_code = ESP_OK;
    int response_length;
    uint8_t read[1] = {SPI_READ};

    spi_transaction_t t = {
        .length = 1 * 8,
        .tx_buffer = &read,
        .rx_buffer = rx,
        .rxlength = 0
    };

    if (xSemaphoreTake(spi_bus_mutex, pdMS_TO_TICKS(MAX_SPI_WAIT_MS)) == pdTRUE) {
        nfc_cs(0);
        esp_rom_delay_us(10);
        t.length = 3 * 8; // read the junk byte, response and length byte
        ESP_ERROR_CHECK(spi_device_transmit(dev, &t));

        response_code = rx[1];

        if ((response_code & ERROR_MASK) == 0) {
            if (response_code & EXTRA_LENGTH_FLAG) { // extra len data
                response_length = ((response_code & EXTRA_LENGTH_MASK) << 3) | rx[2];
            } else {
                response_length = rx[2];
            }

            if (response_length == 0) {
                // nothing to do
                response_code = ESP_OK;
            } else if (*rx_len >= response_length) {
                // tx len bytes to get the data
                t.length = response_length * 8;
                t.rxlength = 0;
                t.tx_buffer = NULL;
                esp_err_t err = spi_device_transmit(dev, &t);
                if (err == ESP_OK) {
                    *rx_len = response_length;
                    response_code = ESP_OK;
                } else {
                    ESP_LOGE(TAG, "spi_device_transmit error 0x%X", err);
                    response_code = err;
                }
            } else {
                ESP_LOGE(TAG, "Buffer %d too small for response %d", response_length, *rx_len);
                response_code = ESP_ERR_NO_MEM;
            }
        } else {
            // ESP_LOGE(TAG, "Error response 0x%X", response_code);
            // noisy when polling and expecting 0x87
        }

        nfc_cs(1);
        xSemaphoreGive(spi_bus_mutex);
    } else {
        ESP_LOGE(TAG, "Failed to acquire SPI bus mutex");
        response_code = ESP_ERR_TIMEOUT;
    }

    return response_code;
}


void cr95hf_info(spi_device_handle_t dev) {
    uint8_t out[3] = { 0, CMD_IDN, 0 };
    cr95hf_tx(dev, out, sizeof(out));
    cr95hf_wait();

    uint8_t in[16];
    int datalen = sizeof(in);
    cr95hf_rx(dev, in, &datalen);

    ESP_LOGI(TAG, "cr95hf_info (%d) '%s'", datalen, in);
}


void cr95hf_protocol(spi_device_handle_t dev) {
    ESP_LOGI(TAG, "PROTOCOL");
    uint8_t out[] = { 0, CMD_PROTOCOL, 2, PROTO_ISO14443A, 0x00 };  // default options for TypeA
    cr95hf_tx(dev, out, sizeof(out));
    cr95hf_wait();

    uint8_t in[16];
    int datalen = sizeof(in);
    cr95hf_rx(dev, in, &datalen);
}


// Send REQA or WUPA and get ATQA
esp_err_t cr95hf_poll(spi_device_handle_t dev, bool wake_up, uint8_t *atqa, int *atqalen)
{
    uint8_t reqwup[] = { SPI_WRITE, CMD_SENDRECV, 0x02, ISO14443A_REQA, FLAG_SHORTFRAME };
    if (wake_up) {
        reqwup[3] = ISO14443A_WUPA;
    }
    cr95hf_tx(dev, reqwup, sizeof(reqwup));
    cr95hf_wait();

    return cr95hf_rx(dev, atqa, atqalen); // expect back ATQA(44 00) CRCOK 00 00
}


/**
 * Blindly assume CL2 tag UID just cause
 */
esp_err_t cr95hf_select(spi_device_handle_t dev, uint8_t *atqa, uint8_t *uid, int *uidlen) {
    uint8_t inbuf[32];
    int buflen = sizeof(inbuf);
    esp_err_t err;

    // AntiCollision (cascade level 1)
    ESP_LOGI(TAG, "ANTICOL1");
    uint8_t anticol[] = { SPI_WRITE, CMD_SENDRECV, 0x03, ISO14443A_SEL_CL1, ISO14443A_NVB_ANTICOLL, FLAG_STD};
    cr95hf_tx(dev, anticol, sizeof(anticol));
    cr95hf_wait();

    buflen = sizeof(inbuf);
    err = cr95hf_rx(dev, inbuf, &buflen); // expect back ISO14443A_CT U1 U2 U3 BCC CRCOK 00 00
    if (err) return err;
    if (*uidlen >= 3) {
        memcpy(uid, &inbuf[1], 3);
    }

    // Select card 1
    ESP_LOGI(TAG, "SELECT1");
    uint8_t select[] = { SPI_WRITE, CMD_SENDRECV, 0x08, ISO14443A_SEL_CL1, ISO14443A_NVB_SELECT, 0, 0, 0, 0, 0, FLAG_STD_CRC };
    memcpy(&select[5], inbuf, 5); // copy ISO14443A_CT, UID and BCC
    cr95hf_tx(dev, select, sizeof(select));
    cr95hf_wait();

    buflen = sizeof(inbuf);
    err = cr95hf_rx(dev, inbuf, &buflen); // expect SAK(04) CRC_A (DA 17) OK 00 00
    ESP_LOGI(TAG, "SAK = 0x%X", inbuf[0]);
    if (err)  return err;

    // AntiCol 2
    ESP_LOGI(TAG, "ANTICOL2");
    uint8_t anticol2[] = { SPI_WRITE, CMD_SENDRECV, 0x03, ISO14443A_SEL_CL2, ISO14443A_NVB_ANTICOLL, FLAG_STD };
    cr95hf_tx(dev, anticol2, sizeof(anticol2));
    cr95hf_wait();

    buflen = sizeof(inbuf);
    err = cr95hf_rx(dev, inbuf, &buflen); // expect U4 (no cascade) U5 U6 U7 U8 CRCOK 00 00
    if (err) return err;
    if (*uidlen >= 8) {
        memcpy(&uid[3], inbuf, 5);
        *uidlen = 8;
    }


    // Select 2
    ESP_LOGI(TAG, "SELECT2");
    uint8_t select2[] = { SPI_WRITE, CMD_SENDRECV, 0x08, ISO14443A_SEL_CL2, ISO14443A_NVB_SELECT, 0, 0, 0, 0, 0, FLAG_STD_CRC };
    memcpy(&select2[5], inbuf, 5); // copy UID bits
    cr95hf_tx(dev, select2, sizeof(select2));
    cr95hf_wait();

    buflen = sizeof(inbuf);
    return cr95hf_rx(dev, inbuf, &buflen); // expect SAK(00) CRC_A(FE 51) OK 00 00
}


esp_err_t cr95hf_read(spi_device_handle_t dev, int pagestart, uint8_t *inbuf, int *inbuflen) {
    // Read page
    uint8_t read[] = { SPI_WRITE, CMD_SENDRECV, 0x03, ISO14443A_READ, pagestart, FLAG_STD_CRC };
    cr95hf_tx(dev, read, sizeof(read));
    cr95hf_wait();

    return cr95hf_rx(dev, inbuf, inbuflen);
}


esp_err_t cr95hf_halt(spi_device_handle_t dev)
{
    ESP_LOGI(TAG, "HALT");
    uint8_t halt[] = { SPI_WRITE, CMD_SENDRECV, 0x02, ISO14443A_HALT, FLAG_STD_CRC };
    cr95hf_tx(dev, halt, sizeof(halt));
    cr95hf_wait();

    uint8_t result[8];
    int len = sizeof(result);
    return cr95hf_rx(dev, result, &len);
}