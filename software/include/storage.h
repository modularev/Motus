#ifndef STORAGE_H
#define STORAGE_H

// Patch storage in the EEPROM: 4 slots, each with all slider settings, the
// var presets and the sequencer settings. The SYS settings are global and saved with every save.
// At start the slot used last is loaded.

constexpr int NUM_SLOTS = 4;

void storageSetup();

int storageCurrentSlot();
bool storageSlotUsed(int slot);

void storageSave(int slot);
bool storageLoad(int slot); // false if the slot is empty

// something differs from the current slot (or the SYS settings) as saved
bool storageUnsaved();

// call once per loop: after a pause in playing (idle: no slider, pad or button
// touched) changes are saved into the current slot, if AUTOSAVE is on; not over
// a slot while a template is loaded, until SAVE or LOAD
void storageAutosave(bool idle);

// fresh starting points: faders, lfos, keys, split (4 keys, 4 LFOs); they
// replace the sliders' settings, the sequencer and its values, unsaved
constexpr int NUM_TEMPLATES = 4;
const char *storageTemplateName(int index);
void storageLoadTemplate(int index);

#endif
