#pragma once

// Set ESPAPPLEII_RELEASE=1 in the compiler flags for a quiet production build.
#ifndef ESPAPPLEII_RELEASE
#define ESPAPPLEII_RELEASE 0
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
