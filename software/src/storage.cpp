#include "storage.h"

#include <Arduino.h>
#include <EEPROM.h>

#include "config.h"
#include "modes.h"
#include "pads.h"
#include "presets.h"
#include "recorder.h"
#include "sequencer.h"
#include "system.h"

// EEPROM layout: directory, slots, SYS settings. Saves of earlier layouts are
// not read: a new layout starts with empty slots and the default settings.
static const int DIRECTORY_ADDRESS = 0;
static const int SLOT_ADDRESS = 16;
static const int SLOT_SIZE = 256;
static const int SYSTEM_ADDRESS = SLOT_ADDRESS + NUM_SLOTS * SLOT_SIZE;
static const int EEPROM_SIZE = 1080; // Teensy 4.0

static const uint32_t DIRECTORY_MAGIC = 0x4D4F4433; // "MOD3"
static const uint32_t SLOT_MAGIC = 0x4D4F5043;      // "MOPC": with MOD and the envelope
static const uint32_t SYSTEM_MAGIC = 0x4D4F5934;    // "MOY4"

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
    PadSettings pads[NUM_PADS];
};

// a slider's settings as stored: every field in the bits it needs, so a slot
// fits its 256 bytes
struct __attribute__((packed)) StoredSlider
{
    uint8_t lfoFreq, lfoPhase;
    int8_t slant;
    // one run of bits, 112 in all
    uint32_t mode : 1, latch : 1, slideGate : 2, lfoRate : 2, lfoDepth : 2, lfoShape : 3, lfoReset : 1, lfoGate : 2,
        freeze : 2, scale : 4, root : 4, modSlide : 2, modLfo : 2, envHold : 1, envVel : 1;
    uint32_t loop : 7, base : 14, slewMs : 14, delayMs : 13, envLength : 7, envRise : 7;
    int32_t range : 15, envCurve : 5;
};
static_assert(sizeof(StoredSlider) == 17, "a slider is stored in 17 bytes");

// a patch as stored
struct __attribute__((packed)) StoredSlot
{
    uint32_t magic;
    uint16_t presets[NUM_PRESETS][NUM_VARS];
    StoredSlider sliders[NUM_SLIDERS];
    SeqSettings sequencer;
    PadSettings pads[NUM_PADS];
};
static_assert(sizeof(StoredSlot) <= SLOT_SIZE, "a slot does not fit its EEPROM space");

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
        p.modSlide = s.modSlide;
        p.modLfo = s.modLfo;
        p.envHold = s.envHold;
        p.envVel = s.envVel;
        p.lfoFreq = s.lfoFreq;
        p.lfoPhase = s.lfoPhase;
        p.slant = s.slant;
        p.loop = s.loop;
        p.base = s.base;
        p.slewMs = s.slewMs;
        p.delayMs = s.delayMs;
        p.envLength = s.envLength;
        p.envRise = s.envRise;
        p.range = s.range;
        p.envCurve = s.envCurve / ENV_CURVE_STEP;
    }
    stored.sequencer = slot.sequencer;
    memcpy(stored.pads, slot.pads, sizeof(stored.pads));
    return stored;
}

static void decode(const StoredSlot &stored, Slot &slot)
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
        s.modSlide = p.modSlide;
        s.modLfo = p.modLfo;
        s.envHold = p.envHold;
        s.envVel = p.envVel;
        s.lfoFreq = p.lfoFreq;
        s.lfoPhase = p.lfoPhase;
        s.slant = p.slant;
        s.loop = p.loop;
        s.base = p.base;
        s.slewMs = p.slewMs;
        s.delayMs = p.delayMs;
        s.envLength = p.envLength;
        s.envRise = p.envRise;
        s.range = p.range;
        s.envCurve = p.envCurve * ENV_CURVE_STEP;
    }
    slot.sequencer = stored.sequencer;
    memcpy(slot.pads, stored.pads, sizeof(slot.pads));
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
static bool autosaveAllowed = true; // not over a slot after a template, until a save or load
static Slot saved;                  // the current slot as saved, for the unsaved marker
static SystemSettings savedSystem;

static int slotAddress(int slot)
{
    return SLOT_ADDRESS + slot * SLOT_SIZE;
}

static void capture(Slot &slot)
{
    memset(&slot, 0, sizeof(slot)); // the padding too, so slots compare field by field
    slot.magic = SLOT_MAGIC;
    presetsGetAll(slot.presets);
    memcpy(slot.sliders, sliderSettings, sizeof(slot.sliders));
    slot.sequencer = seqSettings;
    memcpy(slot.pads, padSettings, sizeof(slot.pads));
}

static void writeSystem()
{
    SystemStore store = {SYSTEM_MAGIC, systemSettings};
    EEPROM.put(SYSTEM_ADDRESS, store);
    savedSystem = systemSettings;
}

void storageSetup()
{
    // the SYS settings are kept when they are there
    SystemStore store;
    EEPROM.get(SYSTEM_ADDRESS, store);
    if (store.magic == SYSTEM_MAGIC)
        systemSettings = store.settings;
    savedSystem = systemSettings;

    modesDefaults();
    padsDefaults();
    EEPROM.get(DIRECTORY_ADDRESS, directory);
    if (directory.magic == DIRECTORY_MAGIC && directory.current < NUM_SLOTS)
    {
        if (!storageLoad(directory.current))
            capture(saved);
        return;
    }

    // a fresh unit, or saves of an earlier layout: empty slots, the defaults
    memset(&directory, 0, sizeof(directory));
    directory.magic = DIRECTORY_MAGIC;
    EEPROM.put(DIRECTORY_ADDRESS, directory);
    writeSystem();
    capture(saved);
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

bool storageLoad(int slot)
{
    if (!directory.used[slot])
        return false;
    StoredSlot stored;
    EEPROM.get(slotAddress(slot), stored);
    if (stored.magic != SLOT_MAGIC)
        return false;
    Slot loaded;
    decode(stored, loaded);

    autosaveAllowed = true;
    presetsSetAll(loaded.presets);
    memcpy(sliderSettings, loaded.sliders, sizeof(loaded.sliders));
    seqSettings = loaded.sequencer;
    memcpy(padSettings, loaded.pads, sizeof(loaded.pads));
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
           memcmp(now.pads, saved.pads, sizeof(now.pads)) != 0 ||
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
    padsDefaults();  // the pads: XY
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
