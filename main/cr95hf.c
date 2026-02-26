
#include "cr95hf.h"
#include "all.h"
#include "esp_rom_sys.h"

void cr95hfv5_init(cr95hfv5_t *dev) {
    ESP_LOGI("CR95HF", "Add device");

    ESP_ERROR_CHECK(spi_bus_add_device(VSPI_HOST, &(spi_device_interface_config_t) {
        .clock_speed_hz = 2000000,  // start conservative (e.g. 2 MHz)
        .mode = 0, // SPI,
        .spics_io_num = -1,
        .queue_size = 3
    }, &dev->spi_handle));
}

void cr95hf_info(cr95hfv5_t *dev) {
    // Example: Send IDN command
    // uint8_t cmd = CMD_IDN;
    char out[3] = { 0, CMD_IDN, 0 };
    char buffer[18] = {0}; // Buffer for response;
    spi_transaction_t t = {
        .length = 3 * 8,
        .tx_buffer = &out,
        .rx_buffer = &buffer
    };

    ESP_LOGI("CR95HF", "Starting IDN transaction...");

    pi4ioe5v6416_write_pin(&iox, dev->cs, 0); // CS
    esp_rom_delay_us(10);
    ESP_ERROR_CHECK(spi_device_transmit(dev->spi_handle, &t));
    pi4ioe5v6416_write_pin(&iox, dev->cs, 1); // CS
    while (0) {
        bool read;
        pi4ioe5v6416_read_pin(&iox,dev->irq_out,&read);
        if (!read) break;
        vTaskDelay(pdMS_TO_TICKS(1));
        // Wait for IRQ out to go high, indicating data is ready
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    char read[18] = { 0 };
    read[0] = 0x02;

    t.tx_buffer = &read;
    memset(t.rx_buffer, 0, sizeof(buffer));
    t.length = 18 * 8; // Expecting 16 bytes of data
    t.rxlength = 0;

    pi4ioe5v6416_write_pin(&iox, dev->cs, 0); // CS
    esp_rom_delay_us(10);
    ESP_ERROR_CHECK(spi_device_transmit(dev->spi_handle, &t));
    pi4ioe5v6416_write_pin(&iox, dev->cs, 1); // CS

    ESP_LOGI("CR95HF", "Received %d bits", t.rxlength);
    for (int i = 0; i < 18; i++) {
        ESP_LOGI("CR95HF", "Received byte %d: 0x%X %c", i, buffer[i],buffer[i]);
    }
}