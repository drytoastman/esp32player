#ifndef _PI4IOE5V6416_H_
#define _PI4IOE5V6416_H_

#include "i2c_bus.h"
#include "esp_err.h"
#include <stdint.h>

#define PI4IOE5V6416_INPUT_PORT0      0x00
#define PI4IOE5V6416_INPUT_PORT1      0x01
#define PI4IOE5V6416_OUTPUT_PORT0     0x02
#define PI4IOE5V6416_OUTPUT_PORT1     0x03
#define PI4IOE5V6416_POLARITY_PORT0   0x04
#define PI4IOE5V6416_POLARITY_PORT1   0x05
#define PI4IOE5V6416_CONFIG_PORT0     0x06
#define PI4IOE5V6416_CONFIG_PORT1     0x07

#define PI4IOE5V6416_PULL_ENABLE_PORT0  0x46
#define PI4IOE5V6416_PULL_ENABLE_PORT1  0x47
#define PI4IOE5V6416_PULL_DIR_PORT0     0x48
#define PI4IOE5V6416_PULL_DIR_PORT1     0x49
#define PI4IOE5V6416_INT_MASK_PORT0     0x4A
#define PI4IOE5V6416_INT_MASK_PORT1     0x4B
#define PI4IOE5V6416_INT_STATUS_PORT0   0x4C
#define PI4IOE5V6416_INT_STATUS_PORT1   0x4D

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