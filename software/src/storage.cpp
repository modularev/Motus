#include "storage.h"

#include <Arduino.h>
#include <EEPROM.h>

#include "config.h"
#include "modes.h"
#include "presets.h"
#include "sequencer.h"
#include "system.h"

// EEPROM layout: directory, slots, SYS settings
static const int DIRECTORY_ADDRESS = 0;
static const int SLOT_ADDRESS = 16;
static const int SLOT_SIZE = 224;
static const int SYSTEM_ADDRESS = SLOT_ADDRESS + NUM_SLOTS * SLOT_SIZE;

static const uint32_t DIRECTORY_MAGIC = 0x4D4F4431; // "MOD1"
static const uint32_t SLOT_MAGIC = 0x4D4F5034;      // "MOP4"
static const uint32_t SLOT_MAGIC_V3 = 0x4D4F5033;   // "MOP3": one sequence length for all rows
static const uint32_t SLOT_MAGIC_V2 = 0x4D4F5032;   // "MOP2": without the sequencer settings
static const uint32_t SLOT_MAGIC_V1 = 0x4D4F5031;   // "MOP1": also the var presets as DAC values
static const uint32_t SYSTEM_MAGIC = 0x4D4F5932;    // "MOY2"
static const uint32_t LEGACY_MAGIC = 0x4D4F5431;    // "MOT1": the single save of earlier firmware

struct Directory
{
    uint32_t magic;
    uint8_t current;
    uint8_t used[NUM_SLOTS];
};

struct Slot
{
    uint32_t magic;
    uint16_t presets[NUM_PRESETS][NUM_VARS];
    SliderSettings sliders[NUM_SLIDERS];
    SeqSettings sequencer;
};
static_assert(sizeof(Slot) <= SLOT_SIZE, "a slot does not fit its EEPROM space");

struct SystemStore
{
    uint32_t magic;
    SystemSettings settings;
};

static Directory directory;
static Slot saved;                // the current slot as saved, for the unsaved marker
static SystemSettings savedSystem;

static int slotAddress(int slot)
{
    return SLOT_ADDRESS + slot * SLOT_SIZE;
}

static void capture(Slot &slot)
{
    slot.magic = SLOT_MAGIC;
    presetsGetAll(slot.presets);
    memcpy(slot.sliders, sliderSettings, sizeof(slot.sliders));
    slot.sequencer = seqSettings;
}

static void writeSystem()
{
    SystemStore store = {SYSTEM_MAGIC, systemSettings};
    EEPROM.put(SYSTEM_ADDRESS, store);
    savedSystem = systemSettings;
}

void storageSetup()
{
    EEPROM.get(DIRECTORY_ADDRESS, directory);
    if (directory.magic == DIRECTORY_MAGIC && directory.current < NUM_SLOTS)
    {
        SystemStore store;
        EEPROM.get(SYSTEM_ADDRESS, store);
        if (store.magic == SYSTEM_MAGIC)
            systemSettings = store.settings;
        savedSystem = systemSettings;

        modesDefaults();
        if (!storageLoad(directory.current))
            capture(saved);
        return;
    }

    // no slots yet: the single save of earlier firmware becomes slot 1, or
    // a fresh unit starts with the defaults
    uint32_t legacy;
    EEPROM.get(DIRECTORY_ADDRESS, legacy);
    memset(&directory, 0, sizeof(directory));
    directory.magic = DIRECTORY_MAGIC;

    modesLoadLegacy(); // the defaults when there is no earlier save
    if (legacy == LEGACY_MAGIC)
    {
        presetsLoadLegacy();
        systemLoadLegacy();
        storageSave(0);
    }
    else
    {
        EEPROM.put(DIRECTORY_ADDRESS, directory);
        writeSystem();
        capture(saved);
    }
}

int storageCurrentSlot()
{
    return directory.current;
}

bool storageSlotUsed(int slot)
{
    return directory.used[slot];
}

void storageSave(int slot)
{
    capture(saved);
    EEPROM.put(slotAddress(slot), saved);
    writeSystem();

    directory.used[slot] = 1;
    directory.current = slot;
    EEPROM.put(DIRECTORY_ADDRESS, directory);
}

bool storageLoad(int slot)
{
    if (!directory.used[slot])
        return false;

    Slot loaded;
    EEPROM.get(slotAddress(slot), loaded);
    if (loaded.magic == SLOT_MAGIC_V3)
    {
        // the same layout up to the sequencer settings, which had one length
        SeqSettingsV1 old;
        EEPROM.get(slotAddress(slot) + offsetof(Slot, sequencer), old);
        loaded.sequencer = seqFromV1(old);
        loaded.magic = SLOT_MAGIC;
    }
    if (loaded.magic == SLOT_MAGIC_V1)
        presetsCodesToCents(loaded.presets);
    if (loaded.magic == SLOT_MAGIC_V1 || loaded.magic == SLOT_MAGIC_V2)
    {
        sequencerDefaults();
        loaded.sequencer = seqSettings;
        loaded.magic = SLOT_MAGIC;
    }
    else if (loaded.magic != SLOT_MAGIC)
        return false;

    presetsSetAll(loaded.presets);
    memcpy(sliderSettings, loaded.sliders, sizeof(loaded.sliders));
    seqSettings = loaded.sequencer;
    saved = loaded;

    directory.current = slot;
    EEPROM.put(DIRECTORY_ADDRESS, directory);
    return true;
}

bool storageUnsaved()
{
    Slot now;
    capture(now);
    return memcmp(&now, &saved, sizeof(now)) != 0 || memcmp(&systemSettings, &savedSystem, sizeof(savedSystem)) != 0;
}
