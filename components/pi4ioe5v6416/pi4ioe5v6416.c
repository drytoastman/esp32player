#include "pi4ioe5v6416.h"
#include "esp_log.h"

#define REG_INPUT_PORT0      0x00
#define REG_INPUT_PORT1      0x01
#define REG_OUTPUT_PORT0     0x02
#define REG_OUTPUT_PORT1     0x03
#define REG_POLARITY_PORT0   0x04
#define REG_POLARITY_PORT1   0x05
#define REG_CONFIG_PORT0     0x06
#define REG_CONFIG_PORT1     0x07

#define REG_INT_MASK_PORT0     0x4A
#define REG_INT_MASK_PORT1     0x4B
#define REG_INT_STATUS_PORT0   0x4C
#define REG_INT_STATUS_PORT1   0x4D

#define PI4IOE_ADDR 0x40  // or 0x42 depending on the state of the ADDR pin

static const char *TAG = "PI4IOE5V6416";

esp_err_t get_i2c_pins(i2c_port_t port, i2c_config_t *i2c_config);

int pi4ioe5v6416_init(pi4ioe5v6416_t *dev)
{
    esp_err_t res;
    i2c_config_t es_i2c_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000
    };
    res = get_i2c_pins(I2C_NUM_0, &es_i2c_cfg);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "getting i2c pins error: %d", res);
        return res;  // Failed to get I2C pins
    }
    dev->i2c_handle = i2c_bus_create(I2C_NUM_0, &es_i2c_cfg);
    dev->address = PI4IOE_ADDR;

    esp_err_t err;
    uint8_t dummy;

    // 1️⃣ Check device presence (read input register)
    err = pi4ioe5v6416_read_reg(dev, REG_INPUT_PORT0, &dummy);
    if (err != ESP_OK) {
        return err;  // Device not responding
    }

    // 2️⃣ Set all pins to INPUT (safe default)
    err = pi4ioe5v6416_write_reg(dev, REG_CONFIG_PORT0, 0xFF);
    if (err != ESP_OK) return err;

    err = pi4ioe5v6416_write_reg(dev, REG_CONFIG_PORT1, 0xFF);
    if (err != ESP_OK) return err;

    // 3️⃣ Clear output registers
    err = pi4ioe5v6416_write_reg(dev, REG_OUTPUT_PORT0, 0x00);
    if (err != ESP_OK) return err;

    err = pi4ioe5v6416_write_reg(dev, REG_OUTPUT_PORT1, 0x00);
    if (err != ESP_OK) return err;

    // 4 Default polaity
    err = pi4ioe5v6416_write_reg(dev, REG_POLARITY_PORT0, 0x00);
    if (err != ESP_OK) return err;

    err = pi4ioe5v6416_write_reg(dev, REG_POLARITY_PORT1, 0x00);
    if (err != ESP_OK) return err;

    // 6 Mask all interrupts initially
    err = pi4ioe5v6416_write_reg(dev, REG_INT_MASK_PORT0, 0xFF);
    if (err != ESP_OK) return err;

    err = pi4ioe5v6416_write_reg(dev, REG_INT_MASK_PORT1, 0xFF);
    if (err != ESP_OK) return err;

    return ESP_OK;
}

esp_err_t pi4ioe5v6416_write_reg(pi4ioe5v6416_t *dev, uint8_t reg_add, uint8_t data)
{
    return i2c_bus_write_bytes(dev->i2c_handle, dev->address, &reg_add, sizeof(reg_add), &data, sizeof(data));
}

esp_err_t pi4ioe5v6416_read_reg(pi4ioe5v6416_t *dev, uint8_t reg_add, uint8_t *p_data)
{
    return i2c_bus_read_bytes(dev->i2c_handle, dev->address, &reg_add, sizeof(reg_add), p_data, 1);
}

esp_err_t pi4ioe5v6416_set_direction(pi4ioe5v6416_t *dev, uint8_t pin, bool output)
{
    uint8_t reg = (pin < 8) ? REG_CONFIG_PORT0 : REG_CONFIG_PORT1;
    uint8_t bit = pin % 8;

    uint8_t val;
    pi4ioe5v6416_read_reg(dev, reg, &val);

    if (output)
        val &= ~(1 << bit);   // 0 = output
    else
        val |= (1 << bit);    // 1 = input

    return pi4ioe5v6416_write_reg(dev, reg, val);
}


esp_err_t pi4ioe5v6416_write_pin(pi4ioe5v6416_t *dev, uint8_t pin, bool level)
{
    uint8_t reg = (pin < 8) ? REG_OUTPUT_PORT0 : REG_OUTPUT_PORT1;
    uint8_t bit = pin % 8;

    uint8_t val;
    pi4ioe5v6416_read_reg(dev, reg, &val);

    if (level)
        val |= (1 << bit);
    else
        val &= ~(1 << bit);

    return pi4ioe5v6416_write_reg(dev, reg, val);
}


esp_err_t pi4ioe5v6416_read_pin(pi4ioe5v6416_t *dev, uint8_t pin, bool *level)
{
    uint8_t reg = (pin < 8) ? REG_INPUT_PORT0 : REG_INPUT_PORT1;
    uint8_t bit = pin % 8;

    uint8_t val;
    pi4ioe5v6416_read_reg(dev, reg, &val);

    *level = (val >> bit) & 0x01;
    return ESP_OK;
}


esp_err_t pi4ioe5v6416_enable_interrupt(pi4ioe5v6416_t *dev, uint8_t pin)
{
    uint8_t reg = (pin < 8) ? REG_INT_MASK_PORT0 : REG_INT_MASK_PORT1;
    uint8_t bit = pin % 8;

    uint8_t val;
    pi4ioe5v6416_read_reg(dev, reg, &val);

    val &= ~(1 << bit);  // clear mask → enable interrupt

    return pi4ioe5v6416_write_reg(dev, reg, val);
}


#if infohere

gpio_config_t io_conf = {
    .pin_bit_mask = 1ULL << GPIO_NUM_4,
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .intr_type = GPIO_INTR_NEGEDGE,  // INT is active low
};

gpio_config(&io_conf);
gpio_install_isr_service(0);
gpio_isr_handler_add(GPIO_NUM_4, expander_isr, NULL);

static void IRAM_ATTR expander_isr(void *arg)
{
    BaseType_t higher_task_woken = pdFALSE;
    vTaskNotifyGiveFromISR(expander_task_handle, &higher_task_woken);
    portYIELD_FROM_ISR(higher_task_woken);
}

void expander_task(void *arg)
{
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        uint8_t status0, status1;

        pi4ioe5v6416_read_reg(&exp, REG_INT_STATUS_PORT0, &status0);
        pi4ioe5v6416_read_reg(&exp, REG_INT_STATUS_PORT1, &status1);

        // Now you know which pin triggered
        printf("Interrupt status: %02X %02X\n", status0, status1);

        // Reading input register often clears interrupt
        pi4ioe5v6416_read_reg(&exp, REG_INPUT_PORT0, &status0);
        pi4ioe5v6416_read_reg(&exp, REG_INPUT_PORT1, &status1);
    }
}

#endif