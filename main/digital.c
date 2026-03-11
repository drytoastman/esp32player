
#include "all.h"
#include "esp_log.h"
#include <sys/time.h>

#define VOLUME_MIN 0
#define VOLUME_MAX 100

static const char *TAG = "digital";

void pcactl(bool level) {
    pi4ioe5v6416_write_pin(&iox, output_params.iox.pactl, level);
}

void nfc_cs(bool level) {
    pi4ioe5v6416_write_pin(&iox, output_params.iox.nfc_cs, level);
}

void nfc_irq(bool level) {
    pi4ioe5v6416_write_pin(&iox, output_params.iox.nfc_irq, level);
}

bool nfc_irq_check() {
    return pi4ioe5v6416_read_pin(&iox, input_params.iox.nfc_irq);
}

void display_cs(int display, bool level) {
    if ((display < 0) || (display >= 4)) {
        ESP_LOGE(TAG, "display cs out of range (%d)", display);
        return;
    }
    pi4ioe5v6416_write_pin(&iox, output_params.iox.display[display], level);
}


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

rotary_encoder rotary_volume;
rotary_encoder rotary_rightknob;

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


void digital_init()
{
    //pi4ioe5v6416_t pi4ioe5v6416_dev;
    pi4ioe5v6416_init(&iox);
    pi4ioe5v6416_write_reg(&iox, PI4IOE5V6416_CONFIG_PORT0,      0x30);
    pi4ioe5v6416_write_reg(&iox, PI4IOE5V6416_CONFIG_PORT1,      0xAF);
    pi4ioe5v6416_write_reg(&iox, PI4IOE5V6416_PULL_ENABLE_PORT0, 0x30);
    pi4ioe5v6416_write_reg(&iox, PI4IOE5V6416_PULL_ENABLE_PORT1, 0xA8);
    pi4ioe5v6416_write_reg(&iox, PI4IOE5V6416_PULL_DIR_PORT0,    0x30);
    pi4ioe5v6416_write_reg(&iox, PI4IOE5V6416_PULL_DIR_PORT1,    0xA8);
    pi4ioe5v6416_write_reg(&iox, PI4IOE5V6416_OUTPUT_PORT0,      0xFF);
    pi4ioe5v6416_write_reg(&iox, PI4IOE5V6416_OUTPUT_PORT1,      0xFF);
    vTaskDelay(pdMS_TO_TICKS(1)); // Short delay to ensure pins are set before proceeding with SPI transactions

    pcactl(0);

    rotary_init(&rotary_volume, input_params.gpio.volume_a, input_params.gpio.volume_b);
    //rightknob_pcnt = rotary_init(input_params.gpio.right_a, input_params.gpio.right_b);
}


/**
 * Task for all the polling junk. A lots of this is the loading of the
 * two registers of the IOX chip and parsing out the individual bits
 */
void digital_processor(void *ignored)
{
     /* Light sleep is not required to be able to use the PCNT unit, but it can be used to save power between events. */
#if CONFIG_EXAMPLE_LIGHT_SLEEP
     ESP_LOGI(TAG, "Enabling light sleep. The rotaries task will wake up the chip when an event is detected.");
     // EC11 channel output high level in normal state, so we set "low level" to wake up the chip
     ESP_ERROR_CHECK(gpio_wakeup_enable(EXAMPLE_EC11_GPIO_A, GPIO_INTR_LOW_LEVEL));
     ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());
#endif

    int volume = 50;  // TODO, initialize to current volume
    input_config current, previous;  // use as mass organized variables
    uint8_t banka, bankb;
    int both;
    int current_mute = 0;
    time_t power_button_time = time(NULL);

    // Initialize previous state of IOX input pins
    pi4ioe5v6416_read_reg(&iox, 0, &banka);
    pi4ioe5v6416_read_reg(&iox, 1, &bankb);
    both = banka | (bankb << 8);

    previous.iox.volume      = (both >> input_params.iox.volume) & 0x01;
    previous.iox.right       = (both >> input_params.iox.right) & 0x01;
    previous.iox.nfc_irq     = (both >> input_params.iox.nfc_irq) & 0x01;
    previous.iox.hp_detect   = (both >> input_params.iox.hp_detect) & 0x01;
    previous.iox.tilt        = (both >> input_params.iox.tilt) & 0x01;
    previous.iox.power       = (both >> input_params.iox.power) & 0x01;
    previous.iox.plug_stat   = (both >> input_params.iox.plug_stat) & 0x01;
    previous.iox.charge_stat = (both >> input_params.iox.charge_stat) & 0x01;

    for (;;) {
        int measure;

        // IOX input pins
        pi4ioe5v6416_read_reg(&iox, 0, &banka);
        pi4ioe5v6416_read_reg(&iox, 1, &bankb);
        both = banka | (bankb << 8);

        current.iox.volume      = (both >> input_params.iox.volume) & 0x01;
        current.iox.right       = (both >> input_params.iox.right) & 0x01;
        current.iox.nfc_irq     = (both >> input_params.iox.nfc_irq) & 0x01;
        current.iox.hp_detect   = (both >> input_params.iox.hp_detect) & 0x01;
        current.iox.tilt        = (both >> input_params.iox.tilt) & 0x01;
        current.iox.power       = (both >> input_params.iox.power) & 0x01;
        current.iox.plug_stat   = (both >> input_params.iox.plug_stat) & 0x01;
        current.iox.charge_stat = (both >> input_params.iox.charge_stat) & 0x01;

        if (current.iox.volume != previous.iox.volume) {
            ESP_LOGI(TAG, "IOX Volume pin changed to: %d", current.iox.volume);

            if (!current.iox.volume) {
                current_mute = !current_mute; // Toggle mute state
                if (!current_mute) { // inverted
                    ESP_LOGI(TAG, "Muting audio");
                    pcactl(0);
                } else {
                    ESP_LOGI(TAG, "Unmuting audio");
                    pcactl(1);
                }
            }
        }
        if (current.iox.right != previous.iox.right) {
            ESP_LOGI(TAG, "IOX Right pin changed to: %d", current.iox.right);
            if (current.iox.right) {
                playback_inject_event(AUDIO_EVENT_PLAYPAUSE, 0);
            }
        }

        if (current.iox.power) {
            power_button_time = time(NULL);
        } else {
            // If power button is held for more than 5 seconds, trigger a reset, power circuit will turn off
            if (time(NULL) - power_button_time > 5) {
                ESP_LOGI(TAG, "Power button held for more than 5 seconds, shutting down");
                esp_restart();
            }
        }
        // current.iox.nfc_irq
        // This is polled by the nfc_irq_check function, so we don't need to log it here and it gets noisy

        if (current.iox.hp_detect != previous.iox.hp_detect) {
            // I don't know if this is tracking correctly as it sometimes flips when I plug/unplug headphones,
            // but other times it doesn't.
            ESP_LOGI(TAG, "IOX HP Detect pin changed to: %d", current.iox.hp_detect);
        }
        if (current.iox.tilt != previous.iox.tilt) {
            ESP_LOGI(TAG, "IOX Tilt pin changed to: %d", current.iox.tilt);
        }
        if (current.iox.plug_stat != previous.iox.plug_stat) {
            ESP_LOGI(TAG, "IOX Plug Stat pin changed to: %d", current.iox.plug_stat);
        }
        if (current.iox.charge_stat != previous.iox.charge_stat) {
            ESP_LOGI(TAG, "IOX Charge Stat pin changed to: %d", current.iox.charge_stat);
        }
        previous = current;

        // vTaskDelay(pdMS_TO_TICKS(10));
        if (xQueueReceive(rotary_volume.queue, &measure, pdMS_TO_TICKS(10)) == pdTRUE) {
             ESP_LOGI(TAG, "Rotary volume event: %d", measure);
             if (measure == DIR_CW) {
                volume++;
                if (volume > VOLUME_MAX) volume = VOLUME_MAX;
                ESP_LOGI(TAG, "Volume: %d", volume);
                playback_inject_event(AUDIO_EVENT_VOLUME, volume);
            } else if (measure == DIR_CCW) {
                volume--;
                if (volume < VOLUME_MIN) volume = VOLUME_MIN;
                ESP_LOGI(TAG, "Volume: %d", volume);
                playback_inject_event(AUDIO_EVENT_VOLUME, volume);
            }
        }
    }
}

