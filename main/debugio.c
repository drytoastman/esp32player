#include "all.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "string.h"

#define SERIAL_BUFFER_SIZE 256
static const char *TAG = "debugio";
EXT_RAM_BSS_ATTR static uint8_t debugio_buffer[SERIAL_BUFFER_SIZE];

void debugio_task(void* arg) {
    int buf_index = 0;

    // Reconfigure UART0 for both input and output
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };

    uart_param_config(UART_NUM_0, &uart_config);
    uart_set_pin(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    // Install driver to enable reading
    uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);

    while (1) {
        uint8_t ch;
        int len = uart_read_bytes(UART_NUM_0, &ch, 1, pdMS_TO_TICKS(1000));

        if (len == 1) {
            // Echo character back
            uart_write_bytes(UART_NUM_0, (const char*)&ch, 1);

            if (ch == '\n' || ch == '\r') {
                // End of line - process command
                debugio_buffer[buf_index] = '\0';

                if (buf_index > 0) {
                    ESP_LOGI(TAG, "Command: %s", (char*)debugio_buffer);
                    switch(debugio_buffer[0]) {
                        case 'b':
                            //display_brightness(&ht16d35a, atoi((char*)debugio_buffer + 1)%64);
                            break;
                        case 'v':
                            playback_inject_event(AUDIO_EVENT_VOLUME, atoi((char*)debugio_buffer + 1)%100);
                            break;
                        case 'P':
                            playback_inject_event(AUDIO_EVENT_PLAYPAUSE, atoi((char*)debugio_buffer + 1));
                            break;
                        case 'p':
                            playback_inject_event(AUDIO_EVENT_PREV, 0);
                            break;
                        case 'n':
                            playback_inject_event(AUDIO_EVENT_NEXT, 0);
                            break;
                        case 's':
                            playback_inject_event(AUDIO_EVENT_SET_TRACK, atoi((char*)debugio_buffer + 1));
                            break;
                        case 'N':
                            uint8_t arcb = strtol((char*)debugio_buffer + 1, NULL, 16);
                            uint8_t tw   = strtol((char*)debugio_buffer + 4, NULL, 16);
                            nfc_adjust(arcb, tw);
                            break;
                        default:
                            ESP_LOGW(TAG, "Unknown command: %s", (char*)debugio_buffer);
                    }
                }

                // Reset debugio_buffer
                buf_index = 0;
                uart_write_bytes(UART_NUM_0, "\n", 1);
                uart_write_bytes(UART_NUM_0, "> ", 2);

            } else if (ch == '\b' || ch == 0x7F) {
                // Backspace
                if (buf_index > 0) {
                    buf_index--;
                    uart_write_bytes(UART_NUM_0, " \b", 2);  // Erase character
                }

            } else if (buf_index < SERIAL_BUFFER_SIZE - 1) {
                // Add character to debugio_buffer
                debugio_buffer[buf_index++] = ch;
            }
        }
    }
}