#pragma once

// Set ESPAPPLEII_RELEASE=1 in the compiler flags for a quiet production build.
#ifndef ESPAPPLEII_RELEASE
#define ESPAPPLEII_RELEASE 0
#endif

// Set to 1 to show the raw VGA16 palette slots and stop before emulation.
// This bypasses all Apple II graphics decoding.
#ifndef ENABLE_VGA_PALETTE_TEST
#define ENABLE_VGA_PALETTE_TEST 0
#endif

// Set to 1 for a serial-only CPU validation run. The test stops before VGA,
// PS/2, SD, and normal emulation startup.
#ifndef ENABLE_CPU_VALIDATION_TEST
#define ENABLE_CPU_VALIDATION_TEST 0
#endif

// Set to 1 for a serial-only Apple IIe banking/soft-switch validation run.
#ifndef ENABLE_IIE_BANKING_VALIDATION_TEST
#define ENABLE_IIE_BANKING_VALIDATION_TEST 0
#endif

// Set to 1 to load and run the official 64K Klaus Dormann CPU test binaries
// from /apple2/tests on SD. This is a serial-only flat-memory CPU test.
#ifndef ENABLE_KLAUS_CPU_TEST
#define ENABLE_KLAUS_CPU_TEST 0
#endif

// Set to 1 for an isolated, serial-only Disk II controller/write-path test.
// It uses only an in-memory track and never creates or modifies an SD file.
#ifndef ENABLE_DISK_II_VALIDATION_TEST
#define ENABLE_DISK_II_VALIDATION_TEST 0
#endif

// Set to 1 for an isolated SD copy-on-write overlay validation run. It creates
// and removes only /SD/apple2/tests/espappleii-overlay-validation*.dsk files.
#ifndef ENABLE_DISK_OVERLAY_VALIDATION_TEST
#define ENABLE_DISK_OVERLAY_VALIDATION_TEST 0
#endif

// Set to 1 only when individual sector offsets are needed. Printing every
// sector synchronously delays the first Disk II track builds on hardware.
#ifndef ENABLE_DISK_SECTOR_TRACE
#define ENABLE_DISK_SECTOR_TRACE 0
#endif

// One-shot writable-memory provenance and control-flow diagnostic. Change
// only these bounds to retarget the existing bounded framework.
#ifndef MEMORY_DIAGNOSTIC_FIRST
#define MEMORY_DIAGNOSTIC_FIRST 0xD6C0
#endif
#ifndef MEMORY_DIAGNOSTIC_LAST
#define MEMORY_DIAGNOSTIC_LAST 0xD700
#endif
#ifndef MEMORY_DIAGNOSTIC_TRIGGER_FIRST
#define MEMORY_DIAGNOSTIC_TRIGGER_FIRST 0xD6E8
#endif
#ifndef MEMORY_DIAGNOSTIC_TRIGGER_LAST
#define MEMORY_DIAGNOSTIC_TRIGGER_LAST 0xD6F0
#endif
#ifndef MEMORY_DIAGNOSTIC_SOURCE_FIRST
#define MEMORY_DIAGNOSTIC_SOURCE_FIRST 0x58C0
#endif
#ifndef MEMORY_DIAGNOSTIC_SOURCE_LAST
#define MEMORY_DIAGNOSTIC_SOURCE_LAST 0x5900
#endif

#if (ENABLE_CPU_VALIDATION_TEST + ENABLE_IIE_BANKING_VALIDATION_TEST + \
     ENABLE_KLAUS_CPU_TEST + ENABLE_DISK_II_VALIDATION_TEST + \
     ENABLE_DISK_OVERLAY_VALIDATION_TEST) > 1
#error "Enable only one isolated validation harness at a time"
#endif

// IIe double-hi-res presentation/reduction modes for the 280-column Apple
// output area on the confirmed 320x200 FabGL canvas.
#define DHR_COLOR_140     0
#define DHR_MONO_PAIR_OR  1
#define DHR_MONO_EVEN     2
#define DHR_MONO_ODD      3
#define DHR_MONO_PAIR_AND 4
#define DHR_MONO_PAIR_XOR 5
#define DHR_COLOR_280     6
#ifndef DHR_RENDER_MODE
#define DHR_RENDER_MODE DHR_COLOR_280
#endif

#if DHR_RENDER_MODE < DHR_COLOR_140 || DHR_RENDER_MODE > DHR_COLOR_280
#error "DHR_RENDER_MODE must select one of the defined DHR diagnostic modes"
#endif

#if ESPAPPLEII_RELEASE
  #define ENABLE_SERIAL_DEBUG 0
  #define ENABLE_CPU_TRACE 0
  #define ENABLE_DISK_DIAGNOSTICS 0
  #define ENABLE_STACK_TELEMETRY 0
  #define ENABLE_DISK_POINTER_WATCHPOINT 0
#else
  #define ENABLE_SERIAL_DEBUG 1
  #define ENABLE_CPU_TRACE 1
  #define ENABLE_DISK_DIAGNOSTICS 1
  #define ENABLE_STACK_TELEMETRY 1
  // Panic at the exact native store that overwrites the stable disk-owner
  // pointer block. This is a temporary compatibility diagnostic.
  #define ENABLE_DISK_POINTER_WATCHPOINT 1
#endif
