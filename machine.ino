#include "common.h"
#include "esp32/spiram.h"
#include "soc/soc.h"

static const char * IIE_ROM_PATH = "/SD/apple2/roms/apple2e.rom";
static const char * IIE_ENHANCED_ROM_PATH = "/SD/apple2/roms/apple2e-enhanced.rom";

AppleMachineProfile MachineProfile = APPLE_II_PLUS_64K;
unsigned char * IIE_ROM = NULL;
bool IIeROMAvailable = false;
static unsigned char * OriginalIIeROM = NULL;
static unsigned char * EnhancedIIeROM = NULL;
static bool EnhancedIIeROMAvailable = false;

bool IsIIeMode() {
  return MachineProfile != APPLE_II_PLUS_64K;
}

bool Is65C02Mode() {
  return MachineProfile == APPLE_IIE_ENHANCED_128K;
}

unsigned char * OriginalIIeROMBuffer() {
  return OriginalIIeROM;
}

unsigned char * EnhancedIIeROMBuffer() {
  return EnhancedIIeROM;
}

bool EnhancedIIeROMIsAvailable() {
  return EnhancedIIeROMAvailable;
}

const char * MachineProfileName() {
  if (Is65C02Mode()) return "ENHANCED IIE 128K";
  return IsIIeMode() ? "APPLE IIE 128K" : "APPLE II+ 64K";
}

bool SetMachineProfile(AppleMachineProfile profile) {
  if (profile == APPLE_IIE_128K && (!IIeROMAvailable || !AUXRAM))
    return false;
  if (profile == APPLE_IIE_ENHANCED_128K && (!EnhancedIIeROMAvailable || !AUXRAM))
    return false;
  MachineProfile = profile;
  if (profile == APPLE_IIE_128K) IIE_ROM = OriginalIIeROM;
  if (profile == APPLE_IIE_ENHANCED_128K) IIE_ROM = EnhancedIIeROM;
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
  OriginalIIeROM = AUXRAMEXT + 0x4000;
  EnhancedIIeROM = OriginalIIeROM + 0x4000;
  IIE_ROM = OriginalIIeROM;

  const size_t romSize = 0x4000;
  const char * paths[2] = { IIE_ROM_PATH, IIE_ENHANCED_ROM_PATH };
  unsigned char * targets[2] = { OriginalIIeROM, EnhancedIIeROM };
  bool * available[2] = { &IIeROMAvailable, &EnhancedIIeROMAvailable };
  for (int index = 0; index < 2; index++) {
    FILE * rom = fopen(paths[index], "rb");
    if (!rom) {
      DEBUG_PRINTF("[MACHINE] optional ROM not found: %s\n", paths[index]);
      continue;
    }
    fseek(rom, 0, SEEK_END);
    long size = ftell(rom);
    rewind(rom);
    if (size == (long) romSize && fread(targets[index], 1, romSize, rom) == romSize) {
      *available[index] = true;
      DEBUG_PRINTF("[MACHINE] loaded %s\n", paths[index]);
    } else {
      DEBUG_PRINTF("[MACHINE] ROM must be exactly %u bytes: %s\n", (unsigned) romSize, paths[index]);
    }
    fclose(rom);
  }

  // The original Apple IIe 128K is the broadest game-compatibility default:
  // it provides IIe memory/video features while retaining the NMOS 6502
  // behavior expected by software such as the Carmen Sandiego 4am crack.
  // Enhanced IIe and Apple II+ remain selectable from the disk menu with Tab.
  if (IIeROMAvailable)
    SetMachineProfile(APPLE_IIE_128K);
  else if (EnhancedIIeROMAvailable)
    SetMachineProfile(APPLE_IIE_ENHANCED_128K);
  else
    SetMachineProfile(APPLE_II_PLUS_64K);

  return IIeROMAvailable || EnhancedIIeROMAvailable;
}
