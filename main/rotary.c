
#include "all.h"

typedef struct {
    int pina;
    int pinb;
    int state;
    int event;
} rotary_encoder;

static const char *TAG = "rotary";
static rotary_encoder rotary_volume;
static rotary_encoder rotary_rightknob;

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
        gct_send_isr(encoder->event, 0, movement);
    }
}

void rotary_internal_init(rotary_encoder *encoder, int inputA, int inputB, int event)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_ANYEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL<<inputA) | (1ULL<<inputB),
        .pull_down_en = 0,
        .pull_up_en = 1,
    };
    gpio_config(&io_conf);

    encoder->pina = inputA;
    encoder->pinb = inputB;
    encoder->state = 0;
    encoder->event = event;

    gpio_install_isr_service(0); // default interrupt allocation flag if not already there
    gpio_isr_handler_add(inputA, rotary_isr_handler, (void*)encoder);
    gpio_isr_handler_add(inputB, rotary_isr_handler, (void*)encoder);
}


void rotary_init()
{
    rotary_internal_init(&rotary_volume,    input_params.gpio.volume_a, input_params.gpio.volume_b, APP_ROTARY_VOLUME);
    rotary_internal_init(&rotary_rightknob, input_params.gpio.right_a,  input_params.gpio.right_b, APP_ROTARY_TRACK);
}

