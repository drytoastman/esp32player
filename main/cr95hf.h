#ifndef CR95HF_H
#define CR95HF_H

#include "driver/spi_master.h"
#include "driver/gpio.h"

void cr95hf_init(spi_device_handle_t *dev);
void cr95hf_info(spi_device_handle_t dev);

#endif // CR95HF_H_