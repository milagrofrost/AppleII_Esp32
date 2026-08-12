#include "common.h"
#include "esp32/spiram.h"
#include "soc/soc.h"

static const char * IIE_ROM_PATH = "/SD/apple2/roms/apple2e.rom";

AppleMachineProfile MachineProfile = APPLE_II_PLUS_64K;
unsigned char * IIE_ROM = NULL;
bool IIeROMAvailable = false;

bool IsIIeMode() {
  return MachineProfile == APPLE_IIE_128K;
}

const char * MachineProfileName() {
  return IsIIeMode() ? "APPLE IIE 128K" : "APPLE II+ 64K";
}

bool SetMachineProfile(AppleMachineProfile profile) {
  if (profile == APPLE_IIE_128K && (!IIeROMAvailable || !AUXRAM))
    return false;
  MachineProfile = profile;
  ResetMemorySoftSwitches();
  InvalidateVideoCaches();
  DEBUG_PRINTF("[MACHINE] selected %s\n", MachineProfileName());
  return true;
}

bool InitializeMachineProfiles() {
  // Disk images occupy the first two fixed PSRAM regions. The catalog allocator
  // already reserves those regions, so place auxiliary RAM immediately after
  // them and extend the reservation in disk.ino accordingly.
  if (esp_spiram_get_size() > 0)
    AUXRAM = (unsigned char *) SOC_EXTRAM_DATA_LOW + 2 * 143360UL;
  if (!AUXRAM) {
    DEBUG_PRINTLN("[MACHINE] IIe disabled: auxiliary RAM unavailable");
    return false;
  }
  memset(AUXRAM, 0, 0x10000);
  AUXRAMEXT = AUXRAM + 0x10000;
  memset(AUXRAMEXT, 0, 0x4000);
  IIE_ROM = AUXRAMEXT + 0x4000;

  FILE * rom = fopen(IIE_ROM_PATH, "rb");
  if (!rom) {
    DEBUG_PRINTF("[MACHINE] IIe ROM not found: %s\n", IIE_ROM_PATH);
    return false;
  }
  fseek(rom, 0, SEEK_END);
  long size = ftell(rom);
  rewind(rom);
  const size_t romSize = 0x4000;
  if (size != (long) romSize || fread(IIE_ROM, 1, romSize, rom) != romSize) {
    fclose(rom);
    DEBUG_PRINTF("[MACHINE] IIe ROM must be exactly %u bytes\n", (unsigned) romSize);
    return false;
  }
  fclose(rom);
  IIeROMAvailable = true;
  DEBUG_PRINTF("[MACHINE] Apple IIe ROM loaded from %s\n", IIE_ROM_PATH);
  return true;
}
