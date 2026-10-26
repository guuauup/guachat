/**
 * bsp_storage.cpp — SDCardManager 的板级入口
 */
#include "bsp_storage.h"
#include "bsp_pins.h"

#include <string.h>

#include "storage/SDCardManager.h"

bool bsp_storage_init(void)
{
    return SDCardManager::getInstance()->begin();
}

void bsp_storage_info(bsp_storage_info_t *out)
{
    if (!out) {
        return;
    }
    *out = {};

    SDCardManager *sd = SDCardManager::getInstance();
    out->mounted     = sd->isMounted();
    out->total_bytes = sd->getTotalBytes();
    out->free_bytes  = sd->getFreeBytes();
    out->bus_freq_hz = sd->getBusFreq();

    const char *type = sd->getCardType();
    if (type) {
        strncpy(out->card_type, type, sizeof(out->card_type) - 1);
        out->card_type[sizeof(out->card_type) - 1] = '\0';
    }
}

void bsp_storage_unmount(void)
{
    SDCardManager::getInstance()->unmount();
}
