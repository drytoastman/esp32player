#include "all.h"

static const char *TAG = "gct";

void pcactl(bool level) {
    pi4ioe5v6416_write_pin(&iox, output_params.iox.pactl, level);
}

#define VOLUME_MIN 0
#define VOLUME_MAX 100

struct {
    int volume;
    char *base_icon;
    int display_number;
    time_t display_number_timeout;  // <=0 for not on, > 0 for on
} system_state = {
    .volume = 0,
    .base_icon = NULL,
    .display_number = 0,
    .display_number_timeout = 0
};


#define APP_EVENT_QUEUE_SIZE 20
#define APP_EVENT_QUEUE_ITEM_SIZE sizeof(app_event)

static uint8_t queue_storage_area[APP_EVENT_QUEUE_SIZE * APP_EVENT_QUEUE_ITEM_SIZE];
static StaticQueue_t static_queue;
static QueueHandle_t app_event_queue;


void gct_send_isr(int event_type, int id, int data) {
    app_event event = {
        .event_type = event_type,
        .id = id,
        .data = data
    };

    if (xQueueSendFromISR(app_event_queue, &event, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to send event to queue");
    }
}

void gct_send(int event_type, int id, int data) {
    app_event event = {
        .event_type = event_type,
        .id = id,
        .data = data
    };

    if (xQueueSend(app_event_queue, &event, pdMS_TO_TICKS(1000)) != pdPASS) {
        ESP_LOGE(TAG, "Failed to send event to queue");
    }
}

void gct_init() {
    app_event_queue = xQueueCreateStatic(
        APP_EVENT_QUEUE_SIZE,           // Number of items
        APP_EVENT_QUEUE_ITEM_SIZE,      // Size of each item (in bytes)
        queue_storage_area,             // Storage buffer
        &static_queue                   // Static queue structure
    );
}

void grand_central_task(void *ignored) {
    app_event event;

    int rightknob = 0; // for testing, remove later
    int current_pause = 1; // for testing, remove later

    system_state.base_icon = "/sdcard/afDCk/CfeVWoQtDUMnxmlOJ8_0TfqlkFsjDtGZZrsVb5A08RI";
    images_set_base(system_state.base_icon);

    while (1) {
        if (xQueueReceive(app_event_queue, &event, portMAX_DELAY) == pdFAIL) {
            ESP_LOGE(TAG, "Failed to receive event from queue");
            continue;
        }

        ESP_LOGD(TAG, "Received event: type=%d, id=%d, data=%d", event.event_type, event.id, event.data);
        // Process the event based on its type and data
        // For example:
        switch (event.event_type) {
            case APP_ROTARY_VOLUME:
                // Handle volume change event, using event.data as the new volume level
                if (event.data == DIR_CW) {
                    system_state.volume++;
                    if (system_state.volume > VOLUME_MAX) system_state.volume = VOLUME_MAX;
                    ESP_LOGI(TAG, "Volume: %d", system_state.volume);
                    playback_inject_event(AUDIO_EVENT_VOLUME, system_state.volume);
                } else if (event.data == DIR_CCW) {
                    system_state.volume--;
                    if (system_state.volume < VOLUME_MIN) system_state.volume = VOLUME_MIN;
                    ESP_LOGI(TAG, "Volume: %d", system_state.volume);
                    playback_inject_event(AUDIO_EVENT_VOLUME, system_state.volume);
                }

                images_set_number(system_state.volume);
                break;


            case APP_ROTARY_TRACK:
                // Handle rotary_rightknob message
                if (event.data == DIR_CW) {
                    rightknob++;
                } else if (event.data == DIR_CCW) {
                    rightknob--;
                }
                ESP_LOGI(TAG, "Rightknob: %d", rightknob);
                break;


            case APP_LEFT_BUTTON:
                current_pause = !current_pause; // Toggle pause state
                if (!current_pause) { // inverted
                    ESP_LOGI(TAG, "Stopping play");
                    playback_inject_event(AUDIO_EVENT_PLAYPAUSE, -1);
                    pcactl(0);
                } else {
                    ESP_LOGI(TAG, "Starting play");
                    playback_inject_event(AUDIO_EVENT_PLAYPAUSE, +1);
                    pcactl(1);
                }
                break;


            case APP_RIGHT_BUTTON:
                // Handle right button event, using event.data as the button state (pressed/released)
                ESP_LOGI(TAG, "Right button state: %d", event.data);
                if (event.data) { // pressed
                    uint8_t data[4] = {0xDE, 0xAD, 0xBE, 0xEF};
                    nfc_write(17, data); // for testing, remove later
                }
                break;


            case APP_POWER_BUTTON:
                // Handle power button event
                ESP_LOGI(TAG, "Power button held for more than 5 seconds, shutting down");
                esp_restart();
                break;


            case APP_TILT:
                // Handle tilt event, using event.data as the tilt state
                ESP_LOGI(TAG, "Tilt state: %d", event.data);
                break;

            case APP_LIGHT:
                int scaled = event.data >> 6;
                display_brightness(&display, scaled ? scaled : 1); // reduce to 1-63 range
                break;

            case APP_PLUG:
            case APP_CHARGE:
            case APP_BATTERY:
                break;

            default:
                ESP_LOGW(TAG, "Unknown event type: %d", event.event_type);
        }
    }
}