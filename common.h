/*****************************************************************************

    File: "common.h"
    Date:  20/07/2023
    Copyright (C) 2023, Francisco J A Souza

    This file is part of EspAppleII Project.

    EspAppleII is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    EspAppleII is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

*****************************************************************************/

#include "build_config.h"
#include "machine.h"

#if ENABLE_SERIAL_DEBUG
  #define DEBUG_PRINTF(x,...) Serial.printf(x, ##__VA_ARGS__)
  #define DEBUG_PRINT(x,...) Serial.print(x, ##__VA_ARGS__)
  #define DEBUG_PRINTLN(x,...) Serial.println(x, ##__VA_ARGS__)
#else
  #define DEBUG_PRINTF(x,...)
  #define DEBUG_PRINT(x,...)
  #define DEBUG_PRINTLN(x,...)
#endif

// CPU runtime symbols used from the main sketch task loop and emitted by cpu.ino.
extern unsigned short PC;
extern unsigned char STP, A, X, Y, SR;
extern unsigned char opcode, opflags;
extern unsigned short argument_addr;
extern const unsigned char scancode_to_apple[];
extern unsigned long cycle;
extern uint64_t TotalCycles;
extern uint64_t EmulationTimingBaseCycles;
extern uint64_t EmulationTimingBaseMicros;
extern bool EmulationTimingReady;
extern unsigned short CPURecentPC[16];
extern unsigned char CPURecentOpcode[16];
extern unsigned short CPURecentArgument[16];
extern unsigned char CPURecentIndex;
void PaceSpeakerToEmulatedCycle();

// CPU entry points required by the runtime task.
void initCode();
void execCode();
void ResetMemorySoftSwitches();
void InvalidateVideoCaches();
extern bool iie80Column;
extern bool iieAltCharset;
extern bool iieDoubleHires;
unsigned char HostPaddleValue(int paddle);
bool HostJoystickButton(int button);
void PrintDiskRuntimeState();

// Host-side disk image management: startup boot image and second-drive runtime swaps.
bool LoadBootDiskFromSD();
void FindDiskImages();
int SelectDiskImage();
bool LoadDiskImageForDrive(int drive, const char * path);
void DrawVGAAlignmentMarkers();

extern char DiskLoadError[96];
extern char LoadedDiskName[64];
