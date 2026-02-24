#include <board.h>
#ifndef __ALL_H__
#define __ALL_H__

#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

#include "pi4ioe5v6416.h"


typedef struct {
    pcnt_unit_handle_t volume;
    pcnt_unit_handle_t rightknob;
    adc_oneshot_unit_handle_t light_sensor;

    struct {
        int volume_a;
        int volume_b;
        int right_a;
        int right_b;
        int light_sensor;
        int battery_voltage;
    } gpio;

    struct {
        int volume;
        int right;
        int nfc_irq_out;
        int hp_detect;
        int tilt;
        int power;
        int plug_stat;
        int charge_stat;
        pi4ioe5v6416_t dev;
    } iox;

} input_task_params_t;

extern audio_board_handle_t board_handle;

pcnt_unit_handle_t rotary_init(int inputA, int inputB);

void input_processor(void *inputParameters);
void sound_main(void *inputParameters);

#endif