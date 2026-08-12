#pragma once

enum AppleMachineProfile : unsigned char {
  APPLE_II_PLUS_64K = 0,
  APPLE_IIE_128K = 1,
};

extern AppleMachineProfile MachineProfile;
extern unsigned char * AUXRAM;
extern unsigned char * AUXRAMEXT;
extern unsigned char * IIE_ROM;
extern bool IIeROMAvailable;

bool InitializeMachineProfiles();
bool SetMachineProfile(AppleMachineProfile profile);
const char * MachineProfileName();
void ResetIIeSoftSwitches();
bool IsIIeMode();
bool IIe80StoreEnabled();
