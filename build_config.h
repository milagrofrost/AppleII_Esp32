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

// IIe double-hi-res presentation/reduction modes for the 280-column Apple
// output area on the confirmed 320x200 FabGL canvas.
#define DHR_COLOR_140     0
#define DHR_MONO_PAIR_OR  1
#define DHR_MONO_EVEN     2
#define DHR_MONO_ODD      3
#define DHR_MONO_PAIR_AND 4
#define DHR_MONO_PAIR_XOR 5
#ifndef DHR_RENDER_MODE
#define DHR_RENDER_MODE DHR_COLOR_140
#endif

#if DHR_RENDER_MODE < DHR_COLOR_140 || DHR_RENDER_MODE > DHR_MONO_PAIR_XOR
#error "DHR_RENDER_MODE must select one of the defined DHR diagnostic modes"
#endif

#if ESPAPPLEII_RELEASE
  #define ENABLE_SERIAL_DEBUG 0
  #define ENABLE_CPU_TRACE 0
  #define ENABLE_DISK_DIAGNOSTICS 0
  #define ENABLE_STACK_TELEMETRY 0
#else
  #define ENABLE_SERIAL_DEBUG 1
  #define ENABLE_CPU_TRACE 1
  #define ENABLE_DISK_DIAGNOSTICS 1
  #define ENABLE_STACK_TELEMETRY 1
#endif
