#include "storage.h"

#include <Arduino.h>
#include <EEPROM.h>

#include "config.h"
#include "modes.h"
#include "presets.h"
#include "recorder.h"
#include "sequencer.h"
#include "system.h"

// EEPROM layout: directory, slots, SYS settings
static const int DIRECTORY_ADDRESS = 0;
static const int SLOT_ADDRESS = 16;
static const int SLOT_SIZE = 256;
static const int SYSTEM_ADDRESS = SLOT_ADDRESS + NUM_SLOTS * SLOT_SIZE;
static const int EEPROM_SIZE = 1080; // Teensy 4.0

// the layout before the rows had their own quantizer and range: smaller slots
static const int SLOT_SIZE_V1 = 224;
static const int SYSTEM_ADDRESS_V1 = SLOT_ADDRESS + NUM_SLOTS * SLOT_SIZE_V1;

static const uint32_t DIRECTORY_MAGIC = 0x4D4F4432;    // "MOD2"
static const uint32_t DIRECTORY_MAGIC_V1 = 0x4D4F4431; // "MOD1": slots of SLOT_SIZE_V1
static const uint32_t SLOT_MAGIC = 0x4D4F5041;         // "MOPA": the slider settings packed, with CTRL
static const uint32_t SLOT_MAGIC_V9 = 0x4D4F5039;      // "MOP9": the slider settings as in memory, without CTRL
static const uint32_t SLOT_MAGIC_V8 = 0x4D4F5038;      // "MOP8": without the row speeds
static const uint32_t SLOT_MAGIC_V7 = 0x4D4F5037;      // "MOP7": the stages in cents
static const uint32_t SLOT_MAGIC_V6 = 0x4D4F5036;      // "MOP6": one quantizer for the rows
static const uint32_t SLOT_MAGIC_V5 = 0x4D4F5035;   // "MOP5": without the sequencer's quantizer
static const uint32_t SLOT_MAGIC_V4 = 0x4D4F5034;   // "MOP4": a length per row, one direction
static const uint32_t SLOT_MAGIC_V3 = 0x4D4F5033;   // "MOP3": one sequence length for all rows
static const uint32_t SLOT_MAGIC_V2 = 0x4D4F5032;   // "MOP2": without the sequencer settings
static const uint32_t SLOT_MAGIC_V1 = 0x4D4F5031;   // "MOP1": also the var presets as DAC values
static const uint32_t SYSTEM_MAGIC = 0x4D4F5934;    // "MOY4": AUTOSAVE off by default
static const uint32_t SYSTEM_MAGIC_V3 = 0x4D4F5933; // "MOY3": AUTOSAVE was on by default
static const uint32_t SYSTEM_MAGIC_V2 = 0x4D4F5932; // "MOY2": without AUTOSAVE
static const uint32_t LEGACY_MAGIC = 0x4D4F5431;    // "MOT1": the single save of earlier firmware

struct Directory
{
    uint32_t magic;
    uint8_t current;
    uint8_t used[NUM_SLOTS];
};

// a patch in memory
struct Slot
{
    uint32_t magic;
    uint16_t presets[NUM_PRESETS][NUM_VARS];
    SliderSettings sliders[NUM_SLIDERS];
    SeqSettings sequencer;
};

// a slider's settings as stored: packed, so a slot still fits its 256 bytes
struct __attribute__((packed)) StoredSlider
{
    uint8_t mode : 1, latch : 1, slideGate : 2, lfoRate : 2, lfoDepth : 2;
    uint8_t lfoShape : 3, lfoReset : 1, lfoGate : 2, freeze : 2;
    uint8_t scale : 4, root : 4;
    uint8_t lfoFreq, lfoPhase;
    int8_t slant;
    uint8_t loop;
    int16_t base, range;
    uint16_t slewMs, delayMs;
};

// a patch as stored
struct StoredSlot
{
    uint32_t magic;
    uint16_t presets[NUM_PRESETS][NUM_VARS];
    StoredSlider sliders[NUM_SLIDERS];
    SeqSettings sequencer;
};
static_assert(sizeof(StoredSlot) <= SLOT_SIZE, "a slot does not fit its EEPROM space");

// every earlier layout: the slider settings as they were in memory
struct SlotV9
{
    uint32_t magic;
    uint16_t presets[NUM_PRESETS][NUM_VARS];
    SliderSettingsV9 sliders[NUM_SLIDERS];
    SeqSettings sequencer;
};

static StoredSlot encode(const Slot &slot)
{
    StoredSlot stored;
    memset(&stored, 0, sizeof(stored));
    stored.magic = SLOT_MAGIC;
    memcpy(stored.presets, slot.presets, sizeof(stored.presets));
    for (int i = 0; i < NUM_SLIDERS; i++)
    {
        const SliderSettings &s = slot.sliders[i];
        StoredSlider &p = stored.sliders[i];
        p.mode = s.mode;
        p.latch = s.latch;
        p.slideGate = s.slideGate;
        p.lfoRate = s.lfoRate;
        p.lfoDepth = s.lfoDepth;
        p.lfoShape = s.lfoShape;
        p.lfoReset = s.lfoReset;
        p.lfoGate = s.lfoGate;
        p.freeze = s.freeze;
        p.scale = s.scale;
        p.root = s.root;
        p.lfoFreq = s.lfoFreq;
        p.lfoPhase = s.lfoPhase;
        p.slant = s.slant;
        p.loop = s.loop;
        p.base = s.base;
        p.range = s.range;
        p.slewMs = s.slewMs;
        p.delayMs = s.delayMs;
    }
    stored.sequencer = slot.sequencer;
    return stored;
}

static void decodeStored(const StoredSlot &stored, Slot &slot)
{
    memset(&slot, 0, sizeof(slot));
    slot.magic = SLOT_MAGIC;
    memcpy(slot.presets, stored.presets, sizeof(slot.presets));
    for (int i = 0; i < NUM_SLIDERS; i++)
    {
        const StoredSlider &p = stored.sliders[i];
        SliderSettings &s = slot.sliders[i];
        s.mode = p.mode;
        s.latch = p.latch;
        s.slideGate = p.slideGate;
        s.lfoRate = p.lfoRate;
        s.lfoDepth = p.lfoDepth;
        s.lfoShape = p.lfoShape;
        s.lfoReset = p.lfoReset;
        s.lfoGate = p.lfoGate;
        s.freeze = p.freeze;
        s.scale = p.scale;
        s.root = p.root;
        s.lfoFreq = p.lfoFreq;
        s.lfoPhase = p.lfoPhase;
        s.slant = p.slant;
        s.loop = p.loop;
        s.base = p.base;
        s.range = p.range;
        s.slewMs = p.slewMs;
        s.delayMs = p.delayMs;
    }
    slot.sequencer = stored.sequencer;
}

struct SystemStore
{
    uint32_t magic;
    SystemSettings settings;
};
static_assert(SYSTEM_ADDRESS + sizeof(SystemStore) <= EEPROM_SIZE, "the SYS settings do not fit the EEPROM");

static const uint32_t AUTOSAVE_IDLE_MS = 10000; // untouched this long, changes are saved
static const uint32_t AUTOSAVE_CHECK_MS = 1000;

static Directory directory;
static bool autosaveAllowed = true; // not over a slot after a template, until SAVE or LOAD
static Slot saved;                // the current slot as saved, for the unsaved marker
static SystemSettings savedSystem;

static int slotAddress(int slot)
{
    return SLOT_ADDRESS + slot * SLOT_SIZE;
}

static void migrateLayout();

static void capture(Slot &slot)
{
    memset(&slot, 0, sizeof(slot)); // the padding too, as it is written to the EEPROM
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
    if (directory.magic == DIRECTORY_MAGIC_V1 && directory.current < NUM_SLOTS)
        migrateLayout();
    if (directory.magic == DIRECTORY_MAGIC && directory.current < NUM_SLOTS)
    {
        SystemStore store;
        EEPROM.get(SYSTEM_ADDRESS, store);
        if (store.magic == SYSTEM_MAGIC || store.magic == SYSTEM_MAGIC_V3 || store.magic == SYSTEM_MAGIC_V2)
            systemSettings = store.settings;
        if (store.magic != SYSTEM_MAGIC)
            systemSettings.autosave = 0; // off by default now, to spare the EEPROM
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
    autosaveAllowed = true;
    capture(saved);
    EEPROM.put(slotAddress(slot), encode(saved));
    writeSystem();

    directory.used[slot] = 1;
    directory.current = slot;
    EEPROM.put(DIRECTORY_ADDRESS, directory);
}

// a slot in an earlier layout, where the slider settings were stored as in
// memory then, without CTRL; those layouts are the same up to the sequencer
static bool decodeSlotV9(const uint8_t *raw, Slot &result)
{
    SlotV9 slot;
    memcpy(&slot, raw, sizeof(slot));
    const uint8_t *sequencer = raw + offsetof(SlotV9, sequencer);
    bool positions = false; // the stages are positions already, not cents
    switch (slot.magic)
    {
    case SLOT_MAGIC_V9:
        positions = true;
        break;
    case SLOT_MAGIC_V8:
    case SLOT_MAGIC_V7:
    {
        SeqSettingsV4 old;
        memcpy(&old, sequencer, sizeof(old));
        slot.sequencer = seqFromV4(old);
        positions = slot.magic == SLOT_MAGIC_V8;
        break;
    }
    case SLOT_MAGIC_V6:
    case SLOT_MAGIC_V5:
    {
        SeqSettingsV3 old;
        memcpy(&old, sequencer, sizeof(old));
        if (slot.magic == SLOT_MAGIC_V5)
            old.quantRows = 0; // the quantizer was not there yet
        slot.sequencer = seqFromV3(old);
        break;
    }
    case SLOT_MAGIC_V4:
    {
        SeqSettingsV2 old;
        memcpy(&old, sequencer, sizeof(old));
        slot.sequencer = seqFromV2(old);
        break;
    }
    case SLOT_MAGIC_V3:
    {
        // one length for all rows
        SeqSettingsV1 old;
        memcpy(&old, sequencer, sizeof(old));
        slot.sequencer = seqFromV1(old);
        break;
    }
    case SLOT_MAGIC_V1:
    case SLOT_MAGIC_V2:
        if (slot.magic == SLOT_MAGIC_V1)
            presetsCodesToCents(slot.presets);
        sequencerDefaults();
        slot.sequencer = seqSettings;
        break;
    default:
        return false;
    }
    // the stages held cents: now positions in their rows' ranges
    if (!positions)
        presetsCentsToPositions(slot.presets, slot.sequencer);

    memset(&result, 0, sizeof(result));
    result.magic = SLOT_MAGIC;
    memcpy(result.presets, slot.presets, sizeof(result.presets));
    for (int i = 0; i < NUM_SLIDERS; i++)
        result.sliders[i] = modesFromV9(slot.sliders[i]);
    result.sequencer = slot.sequencer;
    return true;
}

// a slot as stored, in any layout, as the current one
static bool decodeSlot(const uint8_t *raw, Slot &result)
{
    uint32_t magic;
    memcpy(&magic, raw, sizeof(magic));
    if (magic != SLOT_MAGIC)
        return decodeSlotV9(raw, result);
    StoredSlot stored;
    memcpy(&stored, raw, sizeof(stored));
    decodeStored(stored, result);
    return true;
}

// the earlier layout with smaller slots: every slot and the SYS settings move
// to their new places, in the current layout
static void migrateLayout()
{
    static uint8_t raw[NUM_SLOTS][SLOT_SIZE];
    memset(raw, 0, sizeof(raw));
    for (int slot = 0; slot < NUM_SLOTS; slot++)
        for (int i = 0; i < SLOT_SIZE_V1; i++)
            raw[slot][i] = EEPROM.read(SLOT_ADDRESS + slot * SLOT_SIZE_V1 + i);
    SystemStore store;
    EEPROM.get(SYSTEM_ADDRESS_V1, store);

    for (int slot = 0; slot < NUM_SLOTS; slot++)
    {
        Slot converted;
        if (directory.used[slot] && decodeSlot(raw[slot], converted))
            EEPROM.put(slotAddress(slot), encode(converted));
        else
            directory.used[slot] = 0;
    }
    EEPROM.put(SYSTEM_ADDRESS, store);
    directory.magic = DIRECTORY_MAGIC;
    EEPROM.put(DIRECTORY_ADDRESS, directory);
}

bool storageLoad(int slot)
{
    if (!directory.used[slot])
        return false;

    static uint8_t raw[SLOT_SIZE];
    for (int i = 0; i < SLOT_SIZE; i++)
        raw[i] = EEPROM.read(slotAddress(slot) + i);
    Slot loaded;
    if (!decodeSlot(raw, loaded))
        return false;

    autosaveAllowed = true;
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
    // field by field: the slot's padding bytes are not part of the patch
    Slot now;
    capture(now);
    return memcmp(now.presets, saved.presets, sizeof(now.presets)) != 0 ||
           memcmp(now.sliders, saved.sliders, sizeof(now.sliders)) != 0 ||
           memcmp(&now.sequencer, &saved.sequencer, sizeof(now.sequencer)) != 0 ||
           memcmp(&systemSettings, &savedSystem, sizeof(savedSystem)) != 0;
}

// Templates: fresh starting points, offered by LOAD after the slots
static const char *TEMPLATE_NAMES[NUM_TEMPLATES] = {"faders", "lfos", "keys", "split"};

const char *storageTemplateName(int index)
{
    return TEMPLATE_NAMES[index];
}

static void makeLfo(SliderSettings &s)
{
    s.mode = MODE_LFO;
    s.lfoShape = LFO_SINE;
    s.lfoRate = 1; // mid
    s.lfoGate = 1; // a clock: high for the first half of every cycle
    s.base = 0;
    s.range = 5 * CENTS_PER_VOLT;
}

static void makeKeys(SliderSettings &s)
{
    s.scale = SCALE_MAJOR + 1;
    s.root = 0;
    s.base = 2 * CENTS_PER_VOLT; // C2
    s.range = 2 * CENTS_PER_VOLT; // two octaves
}

void storageAutosave(bool idle)
{
    static uint32_t activeMs = 0;
    static uint32_t checkedMs = 0;
    uint32_t now = millis();
    if (!idle || !systemSettings.autosave || !autosaveAllowed)
    {
        activeMs = now;
        return;
    }
    if (now - activeMs < AUTOSAVE_IDLE_MS || now - checkedMs < AUTOSAVE_CHECK_MS)
        return;
    checkedMs = now;
    if (storageUnsaved())
        storageSave(directory.current);
}

void storageLoadTemplate(int index)
{
    autosaveAllowed = false;
    modesDefaults(); // faders: 0-8 V
    for (int slider = 0; slider < NUM_SLIDERS; slider++)
    {
        SliderSettings &s = sliderSettings[slider];
        if (index == 1)
        {
            makeLfo(s);
            s.lfoPhase = slider * 256 / NUM_SLIDERS; // 0, 45, 90 ... 315 degrees
        }
        else if (index == 3 && slider >= NUM_SLIDERS / 2)
        {
            makeLfo(s);
            s.lfoPhase = (slider - NUM_SLIDERS / 2) * 256 / (NUM_SLIDERS / 2); // 0, 90, 180, 270 degrees
        }
        else if (index == 2 || index == 3)
            makeKeys(s);
    }

    uint16_t zero[NUM_PRESETS][NUM_VARS] = {};
    sequencerDefaults();
    if (index == 2)
    {
        // keys: the rows write and play notes like the sliders
        for (int row = 0; row < NUM_VARS; row++)
        {
            SeqRow &r = seqSettings.rows[row];
            r.scale = SCALE_MAJOR + 1;
            r.base = 2 * CENTS_PER_VOLT;
            r.range = 2 * CENTS_PER_VOLT;
        }
    }
    presetsSetAll(zero);
    recorderReset();     // no loops or values left over from before
    modesResetSliders();
}
