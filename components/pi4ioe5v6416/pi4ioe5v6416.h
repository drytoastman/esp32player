#ifndef _PI4IOE5V6416_H_
#define _PI4IOE5V6416_H_

#include "i2c_bus.h"
#include "esp_err.h"
#include <stdint.h>

typedef struct {
    //i2c_port_t i2c_port;
    uint8_t address;
    i2c_bus_handle_t i2c_handle;
} pi4ioe5v6416_t;

esp_err_t pi4ioe5v6416_init(pi4ioe5v6416_t *dev);
esp_err_t pi4ioe5v6416_write_reg(pi4ioe5v6416_t *dev, uint8_t reg_add, uint8_t data);
esp_err_t pi4ioe5v6416_read_reg(pi4ioe5v6416_t *dev, uint8_t reg_add, uint8_t *p_data);

esp_err_t pi4ioe5v6416_set_direction(pi4ioe5v6416_t *dev, uint8_t pin, bool output);
esp_err_t pi4ioe5v6416_write_pin(pi4ioe5v6416_t *dev, uint8_t pin, bool level);
esp_err_t pi4ioe5v6416_read_pin(pi4ioe5v6416_t *dev, uint8_t pin, bool *level);

#endif /* _PI4IOE5V6416_H_ */