#include "all.h"

static const char *TAG = "nfc";
static spi_device_handle_t cr95hf;
static bool active = false;
static int repeated = 0;
static uint8_t uid[16];
static int uidlen;
static uint8_t data[32];
static int datalen;

void nfc_processor(void *ignored) {
    ESP_LOGI(TAG, "Start");

    cr95hf_init(&cr95hf);
    vTaskDelay(pdMS_TO_TICKS(10)); // Short delay to ensure CR95HF is ready before proceeding
    cr95hf_info(cr95hf);
    cr95hf_protocol(cr95hf);

    active = false;

    ESP_LOGI(TAG, "Loop");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(100));

        if (!active) {
            datalen = sizeof(data);
            if (cr95hf_poll(cr95hf, false, data, &datalen) == ESP_OK) {
                ESP_LOGI(TAG, "Card detected, ATQA=%02X%02X", data[0], data[1]);

                uidlen = sizeof(uid);
                if (cr95hf_select(cr95hf, data, uid, &uidlen) == ESP_OK) {
                    active = true;
                    ESP_LOGI(TAG, "Read %d from: ");
                    ESP_LOG_BUFFER_HEX(TAG, uid, uidlen);

                    for (int ii = 0; ii < 40; ii+=4) {
                        datalen = sizeof(data);
                        memset(data, 0, datalen);
                        if (cr95hf_read(cr95hf, ii, data, &datalen) == ESP_OK) {
                            ESP_LOG_BUFFER_HEXDUMP(TAG, data, 16, ESP_LOG_INFO);
                        } else {
                            cr95hf_halt(cr95hf);
                            break;
                        }
                    }
                }
            }
        } else {
            datalen = sizeof(data);
            if (cr95hf_poll(cr95hf, true, data, &datalen) != ESP_OK) {
                repeated++;
                if (repeated > 5) {
                    ESP_LOGI(TAG, "Card removed!");
                    repeated = 0;
                    active = false;
                }
            } else {
                repeated = 0;
                // ESP_LOGI(TAG, "Card still present");
            }
        }
    }
}