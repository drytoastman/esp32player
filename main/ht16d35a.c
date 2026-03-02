
#include "ht16d35a.h"
#include "all.h"
#include "esp_rom_sys.h"

static char *TAG = "ht16d35a";
/*
Set COM output Control
Set ROW output Control
Set Binary/Gray mode
Set number of COM output
Set constant current ratio
Set global brightness control
Set system control
(System oscillator on)
*/
void ht16d35a_tx(spi_device_handle_t dev, char *tx, int tx_len, int display);
void ht16d35a_rx(spi_device_handle_t dev, char *rx, int *rx_len, int display);


void ht16d35a_init(spi_device_handle_t *dev) {
    ESP_LOGI(TAG, "Add device");

    ESP_ERROR_CHECK(spi_bus_add_device(VSPI_HOST, &(spi_device_interface_config_t) {
        .clock_speed_hz = 4000000,
        .mode = 0, // SPI,
        .spics_io_num = -1,
        .queue_size = 1,
        .flags = SPI_DEVICE_3WIRE | SPI_DEVICE_HALFDUPLEX,
        .command_bits = 0,
        .address_bits = 0
    }, dev));


    char com[] = {0x41, 0xFF}; // COM pin
    char row[] = {0x42, 0xFF, 0xFF, 0xFF, 0xFF}; // Row pins
    char gray[] = {0x31, 0x00};  // gray mode
    char cout[] = {0x32, 0x07};  // COM outputs scan
    char bright[] = {0x37, 0x40};  // brightness
    //0x36, 0x06,  // current
    char on[] = { 0x35, 0x03 };  // system on

    char clear[226] = {0};
    clear[0] = 0x80;
    clear[1] = 0x00;

    for (int ii = 0; ii < 4; ii++) {
        ht16d35a_tx(*dev, gray, sizeof(gray), ii);
        ht16d35a_tx(*dev, com, sizeof(com), ii);
        ht16d35a_tx(*dev, row, sizeof(row), ii);
        ht16d35a_tx(*dev, cout, sizeof(cout), ii);
        ht16d35a_tx(*dev, bright, sizeof(bright), ii);
        ht16d35a_tx(*dev, on, sizeof(on), ii);
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    ht16d35a_tx(*dev, clear, sizeof(clear), 0);
    ht16d35a_tx(*dev, clear, sizeof(clear), 1);
    ht16d35a_tx(*dev, clear, sizeof(clear), 2);
    ht16d35a_tx(*dev, clear, sizeof(clear), 3);
}


void ht16d35a_load_icon(spi_device_handle_t dev, uint8_t *fb, int fblen) {
    int fbidx = 0;
    /**
    spi_transaction_t datat = {
        .rx_buffer = NULL,
        .rxlength = 0,
        //.tx_buffer = panel,
        //.length = sizeof(panel) * 8
    };
    */

    uint8_t panel[226] = { 0x80, 0x00 };
    // TBD, assuming 768 bytes in fb (16x16x3)
    // multiple memcpy followed by one SPI transaction vs no copy but 8 SPI transactions?
    // addressing did not work for me so I've memcpy'ing data to the right offsets and
    // sending it in one go
    fbidx = 0;
    if (xSemaphoreTake(spi_bus_mutex, pdMS_TO_TICKS(MAX_SPI_WAIT_MS)) == pdTRUE) {

        for (int display = 0; display < 4; display++) {
            memcpy(&panel[2+0],   &fb[fbidx], 24); fbidx += 24;
            memcpy(&panel[2+28],  &fb[fbidx], 24); fbidx += 24;
            memcpy(&panel[2+56],  &fb[fbidx], 24); fbidx += 24;
            memcpy(&panel[2+84],  &fb[fbidx], 24); fbidx += 24;
            memcpy(&panel[2+112], &fb[fbidx], 24); fbidx += 24;
            memcpy(&panel[2+140], &fb[fbidx], 24); fbidx += 24;
            memcpy(&panel[2+168], &fb[fbidx], 24); fbidx += 24;
            memcpy(&panel[2+196], &fb[fbidx], 24); fbidx += 24;

            spi_device_acquire_bus(dev, portMAX_DELAY);
            display_cs(display, 0);
            esp_rom_delay_us(5);

            spi_transaction_t datat = {0};
            datat.tx_buffer = panel;
            datat.length = 226*8;
            datat.rxlength = 0;
            datat.rx_buffer = NULL;
            ESP_ERROR_CHECK(spi_device_transmit(dev, &datat));

            esp_rom_delay_us(5);
            display_cs(display, 1);
            spi_device_release_bus(dev);
        }

        xSemaphoreGive(spi_bus_mutex);
    } else {
        ESP_LOGE(TAG, "Failed to acquire SPI bus mutex");
    }
}

void ht16d35a_poke(spi_device_handle_t dev) {
    char off[] = { 0x35, 0x00 };
    char on[] = { 0x35, 0x03 };

    spi_transaction_t t = {
        .length = 2 * 8,
        .tx_buffer = off,
        .rx_buffer = NULL,
        .rxlength = 0
    };

    if (xSemaphoreTake(spi_bus_mutex, pdMS_TO_TICKS(MAX_SPI_WAIT_MS)) == pdTRUE) {
        for (int ii = 0; ii < 4; ii++) {
            display_cs(ii, 0);
            t.tx_buffer = off;
            ESP_ERROR_CHECK(spi_device_transmit(dev, &t));
            display_cs(ii, 1);
            xSemaphoreGive(spi_bus_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(1));
        for (int ii = 0; ii < 4; ii++) {
            display_cs(ii, 0);
            t.tx_buffer = on;
            ESP_ERROR_CHECK(spi_device_transmit(dev, &t));
            display_cs(ii, 1);
            xSemaphoreGive(spi_bus_mutex);
        }
    } else {
        ESP_LOGE(TAG, "Failed to acquire SPI bus mutex");
    }
}

void ht16d35a_tx(spi_device_handle_t dev, char *tx, int tx_len, int display) {
    spi_transaction_t t = {
        .length = tx_len * 8,
        .tx_buffer = tx,
        .rx_buffer = NULL,
        .rxlength = 0
    };

    if (xSemaphoreTake(spi_bus_mutex, pdMS_TO_TICKS(MAX_SPI_WAIT_MS)) == pdTRUE) {
        display_cs(display, 0);
        ESP_ERROR_CHECK(spi_device_transmit(dev, &t));
        display_cs(display, 1);
        xSemaphoreGive(spi_bus_mutex);
    } else {
        ESP_LOGE(TAG, "Failed to acquire SPI bus mutex");
    }
}


void ht16d35a_rx(spi_device_handle_t dev, char *rx, int *rx_len, int display) {
    int response_code;
    char read[2] = {0x81, 0x00};
    char output[60];

    spi_transaction_t t = {
        .length = 2 * 8,
        .tx_buffer = &read,
        .rx_buffer = NULL, //output,
        .rxlength = 0, // 60 * 8
    };

    spi_transaction_t r = {
        .length = 0, //2 * 8,
        .tx_buffer = NULL, //&read,
        .rx_buffer = output,
        .rxlength = 60 * 8
    };

    if (xSemaphoreTake(spi_bus_mutex, pdMS_TO_TICKS(MAX_SPI_WAIT_MS)) == pdTRUE) {
        memset(output, 0, sizeof(output));
        display_cs(display, 0);
        ESP_ERROR_CHECK(spi_device_transmit(dev, &t));
        ESP_ERROR_CHECK(spi_device_transmit(dev, &r));
        display_cs(display, 1);
        xSemaphoreGive(spi_bus_mutex);

        for (int ii = 0; ii < 60; ii++) {
            ESP_LOGI(TAG, "%d,%d: 0x%X", display, ii, output[ii]);
        }
    } else {
        ESP_LOGE(TAG, "Failed to acquire SPI bus mutex");
    }
}

int whack = 2;
void ht16d35a_try(spi_device_handle_t dev, int val) {

    char everything[226] = {0};
    everything[0] = 0x80;
    everything[1] = 0x00; // start of data

    ht16d35a_tx(dev, everything, sizeof(everything), 0);
    ht16d35a_tx(dev, everything, sizeof(everything), 1);
    ht16d35a_tx(dev, everything, sizeof(everything), 2);
    ht16d35a_tx(dev, everything, sizeof(everything), 3);

    for (int ii = 2; ii < 226; ii++) {
        everything[ii] = 0x00;
    }
    everything[whack] = 0x10;
    whack++;

    ht16d35a_tx(dev, everything, sizeof(everything), 0);
}

void ht16d35a_info(spi_device_handle_t dev) {
    char out[3] = { 0, 0, 0 };
    ht16d35a_tx(dev, out, sizeof(out), 0);
    //ht16d35a_wait();

    int datalen;
    //ht16d35a_rx(dev, buf, &datalen);

   // ESP_LOGI(TAG, "IDN (%d) '%s'", datalen, buf);
}