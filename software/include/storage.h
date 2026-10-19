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

#endif
