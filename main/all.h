#ifndef __ALL_H__
#define __ALL_H__

#include "board.h"
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

#include "pi4ioe5v6416.h"
#include "cr95hf.h"

typedef struct {
    struct {
        int volume_a;
        int volume_b;
        int right_a;
        int right_b;
    } gpio;

    struct {
        int volume;
        int right;
        int nfc_irq;
        int hp_detect;
        int tilt;
        int power;
        int plug_stat;
        int charge_stat;
    } iox;
} input_config;

typedef struct {
    int light_sensor;
    int battery_voltage;
} analog_config;

typedef struct {
    int night_red;
    int night_green;
    int night_blue;
    struct {
        int display_0;
        int display_1;
        int display_2;
        int display_3;
        int pactl;
        int nfc_irq;
        int nfc_cs;
        int vin_hold;
    } iox;
} output_cfg;


extern input_config input_params;
extern analog_config analog_params;
extern output_cfg output_params;
extern audio_board_handle_t board_handle;
extern pi4ioe5v6416_t iox;
extern SemaphoreHandle_t spi_bus_mutex;

#define MAX_SPI_WAIT_MS 100

void digital_init();
void digital_processor(void *ignored);
void analog_init();
void analog_processor(void *ignored);

void pcactl(bool level);
void nfc_cs(bool level);
void nfc_irq(bool level);
bool nfc_irq_check();
void display_cs(int display, bool level);

void sound_main(void *ignored);

#endif