#include "system.h"

#include <Arduino.h>
#include <EEPROM.h>

static const int STORAGE_ADDRESS = 320;           // the single save of earlier firmware
static const uint32_t STORAGE_MAGIC = 0x4D4F5931; // "MOY1"

static const SystemSettings DEFAULT_SETTINGS = {4, 6, 100, 100, 100, 2, 1, 0};

struct Storage
{
    uint32_t magic;
    SystemSettings settings;
};

SystemSettings systemSettings = DEFAULT_SETTINGS;

void systemLoadLegacy()
{
    Storage storage;
    EEPROM.get(STORAGE_ADDRESS, storage);
    if (storage.magic == STORAGE_MAGIC)
        systemSettings = storage.settings;
}
