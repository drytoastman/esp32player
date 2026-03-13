#include "i2c_bus.h"
#include "esp_log.h"
#include "all.h"

static const char *TAG = "lis2dh12";

#define LIS2DH12_WHO_AM_I             0x0FU
#define LIS2DH12_CTRL_REG1            0x20U
#define LIS2DH12_CTRL_REG3            0x22U
#define LIS2DH12_CTRL_REG5            0x24U
#define LIS2DH12_CTRL_REG6            0x25U
#define LIS2DH12_INT1_CFG             0x30U
#define LIS2DH12_INT1_SRC             0x31U
#define LIS2DH12_INT1_THS             0x32U
#define LIS2DH12_INT1_DURATION        0x33U

typedef enum
{
  LIS2DH12_POWER_DOWN                      = 0x00,
  LIS2DH12_ODR_1Hz                         = 0x01,
  LIS2DH12_ODR_10Hz                        = 0x02,
  LIS2DH12_ODR_25Hz                        = 0x03,
  LIS2DH12_ODR_50Hz                        = 0x04,
  LIS2DH12_ODR_100Hz                       = 0x05,
  LIS2DH12_ODR_200Hz                       = 0x06,
  LIS2DH12_ODR_400Hz                       = 0x07,
  LIS2DH12_ODR_1kHz620_LP                  = 0x08,
  LIS2DH12_ODR_5kHz376_LP_1kHz344_NM_HP    = 0x09,
} lis2dh12_odr_t;

/**
  * Read generic device register
  */
int32_t lis2dh12_read_reg(const lis2dh12_ctx *ctx, uint8_t reg, uint8_t *data, uint16_t len)
{
  return i2c_bus_read_bytes(ctx->i2c_handle, ctx->address, &reg, sizeof(reg), data, len);
}

/**
  * Write generic device register
  */
int32_t lis2dh12_write_reg(const lis2dh12_ctx *ctx, uint8_t reg, uint8_t *data, uint16_t len)
{
  return i2c_bus_write_bytes(ctx->i2c_handle, ctx->address, &reg, sizeof(reg), data, len);
}


/**
 * Setup the accel to trigger on x/y tilts via int1 pin
 */
void lis2dh12_init(lis2dh12_ctx *ctx) {
  uint8_t buff = 0xFF;
  lis2dh12_read_reg(ctx, LIS2DH12_WHO_AM_I, &buff, 1);
  ESP_LOGI(TAG, "whoami on address 0x%x returned 0x%x", ctx->address, buff);

  // power down
  lis2dh12_read_reg(ctx, LIS2DH12_CTRL_REG1, &buff, 1);
  buff = (0x0F & buff) | (LIS2DH12_POWER_DOWN << 4);
  lis2dh12_write_reg(ctx, LIS2DH12_CTRL_REG1, &buff, 1);

  // disable int
  buff = 0;
  lis2dh12_write_reg(ctx, LIS2DH12_INT1_CFG, &buff, 1);

  // set polarity
  lis2dh12_read_reg(ctx, LIS2DH12_CTRL_REG6, &buff, 1);
  buff |= 0x02; //Set INT_POLARITY bit for active low
  lis2dh12_write_reg(ctx, LIS2DH12_CTRL_REG6, &buff, 1);


  // set INT1 interrupt
  lis2dh12_read_reg(ctx, LIS2DH12_CTRL_REG3, &buff, 1);
  buff |= 0x40; //Enable IA1 on INT1
  lis2dh12_write_reg(ctx, LIS2DH12_CTRL_REG3, &buff, 1);

  //Set INT1 threshold
  //INT1_THS = 500mg / 16mb = 31 // 45?
  //accel.setInt1Threshold(62); //90 degree tilt before interrupt
  buff = 31;
  lis2dh12_write_reg(ctx, LIS2DH12_INT1_THS, &buff, 1);

  //Set INT1 Duration
  //INT1_DURATION = 500
  //accel.setInt1Duration(30);
  buff = 9;
  lis2dh12_write_reg(ctx, LIS2DH12_INT1_DURATION, &buff, 1);

  //Latch interrupt 1, CTRL_REG5, LIR_INT1
  //accel.setInt1Latch(true);
  //accel.setInt1Latch(false);
  lis2dh12_read_reg(ctx, LIS2DH12_CTRL_REG5, &buff, 1);
  buff &= ~0x08; // Set int1 latch bit to 0 for non-latching
  lis2dh12_write_reg(ctx, LIS2DH12_CTRL_REG5, &buff, 1);

  //Clear the interrupt
  lis2dh12_read_reg(ctx, LIS2DH12_INT1_SRC, &buff, 1);

  //accel.setDataRate(LIS2DH12_ODR_1Hz); //Very low power
  //accel.setDataRate(LIS2DH12_ODR_100Hz);
  lis2dh12_read_reg(ctx, LIS2DH12_CTRL_REG1, &buff, 1);
  buff = (0x0F & buff) | (LIS2DH12_ODR_25Hz << 4);
  lis2dh12_write_reg(ctx, LIS2DH12_CTRL_REG1, &buff, 1);

  // enable int
  buff = 0x0A; //Enable X and Y high event on INT1
  lis2dh12_write_reg(ctx, LIS2DH12_INT1_CFG, &buff, 1);
}
