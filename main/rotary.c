
#include "all.h"
#include "esp_log.h"
#include <sys/time.h>

#define VOLUME_MIN 0
#define VOLUME_MAX 100

static const char *TAG = "rotary";

typedef struct {
    int pina;
    int pinb;
    int state;
    QueueHandle_t queue;
} rotary_encoder;

#define DIR_NONE 0x0
#define DIR_CW 0x10
#define DIR_CCW 0x20
#define R_START 0x0
#define R_CW_FINAL 0x1
#define R_CW_BEGIN 0x2
#define R_CW_NEXT 0x3
#define R_CCW_BEGIN 0x4
#define R_CCW_FINAL 0x5
#define R_CCW_NEXT 0x6

const unsigned char ttable[7][4] = {
  { R_START,    R_CW_BEGIN,  R_CCW_BEGIN, R_START           }, // R_START
  { R_CW_NEXT,  R_START,     R_CW_FINAL,  R_START | DIR_CW  }, // R_CW_FINAL
  { R_CW_NEXT,  R_CW_BEGIN,  R_START,     R_START           }, // R_CW_BEGIN
  { R_CW_NEXT,  R_CW_BEGIN,  R_CW_FINAL,  R_START           }, // R_CW_NEXT
  { R_CCW_NEXT, R_START,     R_CCW_BEGIN, R_START           }, // R_CCW_BEGIN
  { R_CCW_NEXT, R_CCW_FINAL, R_START,     R_START | DIR_CCW }, // R_CCW_FINAL
  { R_CCW_NEXT, R_CCW_FINAL, R_CCW_BEGIN, R_START           }, // R_CCW_NEXT
};

static void IRAM_ATTR rotary_isr_handler(void* arg)
{
	rotary_encoder *encoder = arg;

    int pinstate = (gpio_get_level(encoder->pinb) << 1) | gpio_get_level(encoder->pina);
    encoder->state = ttable[encoder->state & 0xf][pinstate];
    int movement = encoder->state & 0x30;
    if (movement) {
        xQueueSendFromISR(encoder->queue, &movement, NULL);
    }
}

void rotary_init(rotary_encoder *encoder, int inputA, int inputB)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_ANYEDGE,    // Interrupt on rising edge, can use GPIO_INTR_NEGEDGE, etc.
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL<<inputA) | (1ULL<<inputB),
        .pull_down_en = 0,
        .pull_up_en = 1,
    };
    gpio_config(&io_conf);

    encoder->pina = inputA;
    encoder->pinb = inputB;
    encoder->state = 0;
    encoder->queue = xQueueCreate(10, sizeof(int));

    gpio_install_isr_service(0); // default interrupt allocation flag if not already there
    gpio_isr_handler_add(inputA, rotary_isr_handler, (void*)encoder);
    gpio_isr_handler_add(inputB, rotary_isr_handler, (void*)encoder);
}


static rotary_encoder rotary_volume;
static rotary_encoder rotary_rightknob;


void rotary_processor(void *ignored)
{
    int rightknob = 0;

    rotary_init(&rotary_volume,    input_params.gpio.volume_a, input_params.gpio.volume_b);
    rotary_init(&rotary_rightknob, input_params.gpio.right_a,  input_params.gpio.right_b);

    // Create a set and add queues to it
    QueueSetHandle_t queueSet = xQueueCreateSet(20);
    xQueueAddToSet(rotary_volume.queue, queueSet);
    xQueueAddToSet(rotary_rightknob.queue, queueSet);

    for (;;) {
        int measure;
        QueueSetMemberHandle_t member = xQueueSelectFromSet(queueSet, portMAX_DELAY);

        if(member == rotary_volume.queue) {
            xQueueReceive(rotary_volume.queue, &measure, 0);
            // Handle rotary_volume message
             ESP_LOGD(TAG, "Rotary volume event: %d", measure);
             if (measure == DIR_CW) {
                system_state.volume++;
                if (system_state.volume > VOLUME_MAX) system_state.volume = VOLUME_MAX;
                ESP_LOGI(TAG, "Volume: %d", system_state.volume);
                playback_inject_event(AUDIO_EVENT_VOLUME, system_state.volume);
            } else if (measure == DIR_CCW) {
                system_state.volume--;
                if (system_state.volume < VOLUME_MIN) system_state.volume = VOLUME_MIN;
                ESP_LOGI(TAG, "Volume: %d", system_state.volume);
                playback_inject_event(AUDIO_EVENT_VOLUME, system_state.volume);
            }

            load_icon(system_state.baseIcon);

        } else if(member == rotary_rightknob.queue) {
            xQueueReceive(rotary_rightknob.queue, &measure, 0);
            // Handle rotary_rightknob message
             ESP_LOGD(TAG, "Rotary rightknob event: %d", measure);
             if (measure == DIR_CW) {
                rightknob++;
            } else if (measure == DIR_CCW) {
                rightknob--;
            }
            ESP_LOGI(TAG, "Rightknob: %d", rightknob);
        }
    }
}

