#include "all.h"

static const char *TAG = "nfc";

void cr95hf_init(spi_device_handle_t *dev);
void cr95hf_poke();
void cr95hf_wait();
void cr95hf_info(spi_device_handle_t dev);
void cr95hf_protocol(spi_device_handle_t dev);
void cr95hf_adjust(spi_device_handle_t dev, uint8_t arcb, uint8_t tw);
esp_err_t cr95hf_poll(spi_device_handle_t dev, bool wake_up, uint8_t *atqa, int *atqalen);
esp_err_t cr95hf_select(spi_device_handle_t dev, uint8_t *atqa, uint8_t *uid, int *uidlen);
esp_err_t cr95hf_read(spi_device_handle_t dev, int page_start, uint8_t *outbuf, int *outbuflen);
esp_err_t cr95hf_write4(spi_device_handle_t dev, uint8_t pagestart, uint8_t *fourbytes);
esp_err_t cr95hf_halt(spi_device_handle_t dev);


#define NFC_TYPE_WRITE 1
#define NFC_TYPE_ADJUST 2
typedef struct {
    uint8_t type;
    union {
        struct {
            uint8_t page;
            uint8_t data[4];
        } write;
        struct {
            uint8_t arcb;
            uint8_t tw;
        } adjust;
    };
} nfc_write_event;

#define NFC_EVENT_QUEUE_SIZE 36
#define NFC_EVENT_QUEUE_ITEM_SIZE sizeof(nfc_write_event)

static uint8_t queue_storage_area[NFC_EVENT_QUEUE_SIZE * NFC_EVENT_QUEUE_ITEM_SIZE];
static StaticQueue_t static_queue;
static QueueHandle_t nfc_event_queue;
static spi_device_handle_t cr95hf;
static bool active = false;
static int repeated = 0;
static uint8_t uid[16];
static int uidlen;
static uint8_t data[32];
static int datalen;


void nfc_init() {
    ESP_LOGI(TAG, "init");

    nfc_event_queue = xQueueCreateStatic(
        NFC_EVENT_QUEUE_SIZE,           // Number of items
        NFC_EVENT_QUEUE_ITEM_SIZE,      // Size of each item (in bytes)
        queue_storage_area,             // Storage buffer
        &static_queue                   // Static queue structure
    );

    cr95hf_init(&cr95hf);
    vTaskDelay(pdMS_TO_TICKS(10)); // Short delay to ensure CR95HF is ready before proceeding
    cr95hf_info(cr95hf);
    cr95hf_protocol(cr95hf);
}


esp_err_t nfc_adjust(uint8_t arcb, uint8_t tw) {
    nfc_write_event event = {
        .type = NFC_TYPE_ADJUST,
        .adjust.arcb = arcb,
        .adjust.tw = tw
    };

    if (xQueueSend(nfc_event_queue, &event, pdMS_TO_TICKS(100)) != pdPASS) {
        ESP_LOGE(TAG, "Failed to send NFC write event to queue");
        return ESP_FAIL;
    }
    return ESP_OK;
}


esp_err_t nfc_write(uint8_t page, uint8_t data[4]) {
    nfc_write_event event = {
        .type = NFC_TYPE_WRITE,
        .write.page = page,
        .write.data = { data[0], data[1], data[2], data[3] }
    };

    if (xQueueSend(nfc_event_queue, &event, pdMS_TO_TICKS(100)) != pdPASS) {
        ESP_LOGE(TAG, "Failed to send NFC write event to queue");
        return ESP_FAIL;
    }
    return ESP_OK;
}


void nfc_processor(void *ignored) {
    active = false;
    ESP_LOGI(TAG, "Loop");

    while (true) {
        nfc_write_event event;
        if (xQueueReceive(nfc_event_queue, &event, pdMS_TO_TICKS(100)) == pdPASS) {
            switch (event.type) {
                case NFC_TYPE_WRITE:
                    if (active) {
                        datalen = sizeof(data);
                        if (cr95hf_select(cr95hf, data, uid, &uidlen) == ESP_OK) {
                            cr95hf_write4(cr95hf, event.write.page, event.write.data);
                        }
                    } else {
                        ESP_LOGW(TAG, "Received NFC write event but no card is active");
                    }
                    break;

                case NFC_TYPE_ADJUST:
                    cr95hf_adjust(cr95hf, event.adjust.arcb, event.adjust.tw);
                    break;
            }
            continue;
        }

        if (!active) {
            datalen = sizeof(data);
            if (cr95hf_poll(cr95hf, false, data, &datalen) == ESP_OK) {
                ESP_LOGI(TAG, "Card detected, ATQA=%02X%02X", data[0], data[1]);

                uidlen = sizeof(uid);
                if (cr95hf_select(cr95hf, data, uid, &uidlen) == ESP_OK) {
                    active = true;
                    cr95hf_halt(cr95hf);


                    /**
                    uint8_t data[4] = {0xDE, 0xAD, 0xBE, 0xEF};
                    cr95hf_write4(cr95hf, 17, data);
                    vTaskDelay(pdMS_TO_TICKS(200));


                    ESP_LOGI(TAG, "Read %d from: ", uidlen);
                    ESP_LOG_BUFFER_HEX(TAG, uid, uidlen);

                    for (int ii = 0; ii < 40; ii+=4) {
                        datalen = sizeof(data);
                        memset(data, 0, datalen);
                        if (cr95hf_read(cr95hf, ii, data, &datalen) == ESP_OK) {
                            ESP_LOG_BUFFER_HEXDUMP(TAG, data, 16, ESP_LOG_INFO);
                        } else {
                            //cr95hf_halt(cr95hf);
                            break;
                        }
                    }
                    */
                }
            }
        } else {
            datalen = sizeof(data);
            if (cr95hf_poll(cr95hf, true, data, &datalen) != ESP_OK) {
                repeated++;
                if (repeated > 4) {
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