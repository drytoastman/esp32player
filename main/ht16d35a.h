#ifndef ht16d35a_H
#define ht16d35a_H

#include "driver/spi_master.h"

void ht16d35a_init(spi_device_handle_t *dev);
void ht16d35a_info(spi_device_handle_t dev);
void ht16d35a_try(spi_device_handle_t dev, int val);
void ht16d35a_load_icon(spi_device_handle_t dev, uint8_t *buf, int buf_len);
void ht16d35a_poke(spi_device_handle_t dev);

#endif // ht16d35a_H_