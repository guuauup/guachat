/**
 * bsp_i2c.h — I2C0 诊断
 */
#ifndef BSP_I2C_H
#define BSP_I2C_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 扫描 I2C0（Wire）并把应答地址打印到 Serial */
void bsp_i2c_scan(void);

/** 读一个 8-bit 寄存器；成功返回 true */
bool bsp_i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *value);

#ifdef __cplusplus
}
#endif

#endif /* BSP_I2C_H */
