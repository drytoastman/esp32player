#ifndef CR95HF_H
#define CR95HF_H

#include "driver/spi_master.h"
#include "driver/gpio.h"

void cr95hf_init(spi_device_handle_t *dev);
void cr95hf_poke();
void cr95hf_wait();
void cr95hf_info(spi_device_handle_t dev);
void cr95hf_protocol(spi_device_handle_t dev);
esp_err_t cr95hf_poll(spi_device_handle_t dev, bool wake_up, uint8_t *atqa, int *atqalen);
esp_err_t cr95hf_select(spi_device_handle_t dev, uint8_t *atqa, uint8_t *uid, int *uidlen);
esp_err_t cr95hf_read(spi_device_handle_t dev, int page_start, uint8_t *inbuf, int *inbuflen);
esp_err_t cr95hf_halt(spi_device_handle_t dev);

#endif // CR95HF_H_

