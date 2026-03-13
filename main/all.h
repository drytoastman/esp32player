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


// cr95hf
void cr95hf_init(spi_device_handle_t *dev);
void cr95hf_poke();
void cr95hf_wait();
void cr95hf_info(spi_device_handle_t dev);
void cr95hf_protocol(spi_device_handle_t dev);
esp_err_t cr95hf_poll(spi_device_handle_t dev, bool wake_up, uint8_t *atqa, int *atqalen);
esp_err_t cr95hf_select(spi_device_handle_t dev, uint8_t *atqa, uint8_t *uid, int *uidlen);
esp_err_t cr95hf_read(spi_device_handle_t dev, int page_start, uint8_t *inbuf, int *inbuflen);
esp_err_t cr95hf_halt(spi_device_handle_t dev);


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


typedef struct {

    int volume;
    char *baseIcon;
    int displayNumber;
    time_t displayTimeout;  // <=0 for not on, > 0 for on

} SystemState;


extern SystemState system_state;
extern input_config input_params;
extern analog_config analog_params;
extern output_cfg output_params;
extern pi4ioe5v6416_t iox;
extern spi_device_handle_t display;
extern SemaphoreHandle_t spi_bus_mutex;


#define MAX_SPI_WAIT_MS 100
#define MOUNT_POINT "/sdcard"

void mount_sdcard();

void rotary_processor(void *ignored);
void poller_task(void *ignored);

void nfc_processor(void *ignored);

esp_err_t load_icon(char *path);

void pcactl(bool level);
void nfc_cs(bool level);
void nfc_irq(bool level);
bool nfc_irq_check();
void display_cs(int display, bool level);

void start_wifi(void);
void start_webserver();

#define AUDIO_EVENT_PLAYPAUSE 201
#define AUDIO_EVENT_NEXT 202
#define AUDIO_EVENT_PREV 203
#define AUDIO_EVENT_SET_TRACK 204
#define AUDIO_EVENT_VOLUME 205

void playback_task(void *ignored);
void playback_inject_event(int keypress_cmd, int data);

void debugio_task(void* arg);



#endif