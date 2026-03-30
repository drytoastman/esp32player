#ifndef __ALL_H__
#define __ALL_H__

#include <time.h>

#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_adc/adc_oneshot.h"

#include "pi4ioe5v6416.h"

// lis2dh12
#define LIS2DH12_ADDR  0x30
typedef struct {
    uint8_t address;
    i2c_bus_handle_t i2c_handle;
} lis2dh12_ctx;
void lis2dh12_init(lis2dh12_ctx *ctx);


// display
void display_init(spi_device_handle_t *dev);
void display_brightness(spi_device_handle_t *dev, uint8_t level);
void display_load_fb(spi_device_handle_t dev, uint8_t *buf, int buf_len);

// images
extern const uint8_t numberoverlay[];
esp_err_t images_set_base(char *path);
void images_set_overlay(uint8_t *overlay);
void images_set_number(int number);

// cr95hf, nfc
void nfc_processor(void *ignored);
void nfc_init();
esp_err_t nfc_write(uint8_t page, uint8_t data[4]);
esp_err_t nfc_adjust(uint8_t arcb, uint8_t tw);


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
        int display[4];
        int pactl;
        int nfc_irq;
        int nfc_cs;
        int vin_hold;
    } iox;
} output_cfg;


extern input_config input_params;
extern analog_config analog_params;
extern output_cfg output_params;
extern pi4ioe5v6416_t iox;
extern spi_device_handle_t display;
extern SemaphoreHandle_t spi_bus_mutex;

#define MAX_SPI_WAIT_MS 100
#define MOUNT_POINT "/sdcard"

void mount_sdcard();

// rotary
#define DIR_NONE 0x0
#define DIR_CW 0x10
#define DIR_CCW 0x20
void rotary_init();

// poller
void poller_init();
void poller_task(void *ignored);


esp_err_t load_icon(char *path);
void start_wifi(void);
void start_webserver();

// playback
#define AUDIO_EVENT_PLAYPAUSE 201
#define AUDIO_EVENT_NEXT 202
#define AUDIO_EVENT_PREV 203
#define AUDIO_EVENT_SET_TRACK 204
#define AUDIO_EVENT_VOLUME 205

void playback_task(void *ignored);
void playback_inject_event(int keypress_cmd, int data);

// debugio
void debugio_task(void* arg);


#define APP_ROTARY_VOLUME 301
#define APP_ROTARY_TRACK  302
#define APP_LEFT_BUTTON   303
#define APP_RIGHT_BUTTON  304
#define APP_POWER_BUTTON  305
#define APP_TILT          306
#define APP_HEADPHONE     307
#define APP_POWER         308
#define APP_PLUG          309
#define APP_CHARGE        310
#define APP_BATTERY       311
#define APP_LIGHT         312

// GCT - Grand Central Task
typedef struct {
    int event_type;
    int id;
    int data;
} app_event;

void gct_init();
void grand_central_task(void *ignored);
void gct_send(int event_type, int id, int data);
void gct_send_isr(int event_type, int id, int data);

#endif