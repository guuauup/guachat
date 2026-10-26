/**
 * bsp_storage.h — TF 卡（SDMMC 1 线）挂载信息
 */
#ifndef BSP_STORAGE_H
#define BSP_STORAGE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_STORAGE_CARD_TYPE_MAX 16

typedef struct {
    bool     mounted;
    uint64_t total_bytes;
    uint64_t free_bytes;
    uint32_t bus_freq_hz;
    char     card_type[BSP_STORAGE_CARD_TYPE_MAX];
} bsp_storage_info_t;

/** 挂载 TF 卡到 /sd；失败会重试。返回是否挂载成功 */
bool bsp_storage_init(void);

/** 读取当前挂载信息（不触发重新挂载） */
void bsp_storage_info(bsp_storage_info_t *out);

/** 卸载（热插拔前调用） */
void bsp_storage_unmount(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_STORAGE_H */
