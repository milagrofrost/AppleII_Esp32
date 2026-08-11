/*****************************************************************************

    File: "EspAppleII.ino"
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
//meow

#include "build_config.h"

#if ENABLE_SERIAL_DEBUG
#define DEBUG_PROG
#define SHOWTASKCORE
#endif

// Video mode (define only one)
#define VIDEO_400X300_OVER_640X480  // 400x300 canvas inside standard 640x480 VGA
//#define VGA_320x200_70Hz      // Smaller screen

// Safe-area inset inside the framebuffer. Some VGA televisions auto-center
// porch changes, so moving the rendered canvas is more reliable than changing
// sync timing. This translates pixels only and does not scale the 4:3 output.
#define VGA_CANVAS_OFFSET_X 0
#define VGA_CANVAS_OFFSET_Y 0
#define VGA_ALIGNMENT_MARKERS 0

// SD-backed boot disk support
#define DOS_33

#include "Timer.h"                    // https://github.com/JChristensen/Timer
#include "fabgl.h"
#include "common.h"

fabgl::PS2Controller    PS2Controller;          // PS/2 keyboard controller
fabgl::VGA16Controller  VGAController;          // VGA video controller
fabgl::Canvas           canvas(&VGAController);

void DrawVGAAlignmentMarkers() {
#if VGA_ALIGNMENT_MARKERS
  // Some televisions auto-position VGA from the bounding box of non-black
  // pixels. Anchor all four edges so later Apple II content cannot cause the
  // television to reinterpret the left edge. Work in physical canvas
  // coordinates, independently of the Apple II safe-area origin.
  fabgl::Point savedOrigin = canvas.getOrigin();
  canvas.setOrigin(0, 0);
  int right = canvas.getWidth() - 1;
  int bottom = canvas.getHeight() - 1;
  canvas.setPixel(0, 0, Color::BrightWhite);
  canvas.setPixel(right, 0, Color::BrightWhite);
  canvas.setPixel(0, bottom, Color::BrightWhite);
  canvas.setPixel(right, bottom, Color::BrightWhite);
  canvas.setOrigin(savedOrigin);
#endif
}

#define FATOR_TIMER_FLASH 250

#ifdef DOS_33
Timer timerFlash;                               //instantiate the timer object
#endif

/* videomodes */
#define modetext40	  0
#define modelres40  	1
#define modehres	    2

static unsigned char inverse;
static unsigned int virtmodedown;
static unsigned int virttextpage;       /* 0x0400 or 0x0800 */
static unsigned int virthrespage;       /* 0x2000 or 0x4000 */
static unsigned int virtsplit;

static unsigned char flashChar = 0xFF;

TaskHandle_t Task1;
TaskHandle_t Task2;

/* virtual screen buffer */
unsigned char RAM[0xC000];
unsigned char RAMEXT[0x4000];
unsigned char RAM_TXT_BACK[0x400];
unsigned char RAM_HGR_BACK[0x2000];

#ifdef DEBUG_PROG
#define BAUD_RATE  115200
#define SERIAL_SIZE_RX  32    // used in Serial.setRxBufferSize()
#define SERIAL_SIZE_TX  512   // keep startup diagnostics from being truncated
#endif

#ifdef DOS_33
/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
void flashCaracter()
{
  flashChar ^= 0xFF;
}
#endif

#ifdef SHOWTASKCORE
/***************************************************************************************************************************************/
// showTaskCore
/***************************************************************************************************************************************/
void showTaskCore ()  {
  DEBUG_PRINT("Task running on core ");
  DEBUG_PRINTLN(xPortGetCoreID());

  setCpuFrequencyMhz (240);
  uint32_t Freq = getCpuFrequencyMhz();
  DEBUG_PRINT("CPU Freq = ");
  DEBUG_PRINT(Freq);
  DEBUG_PRINTLN(" MHz");
  Freq = getXtalFrequencyMhz();
  DEBUG_PRINT("XTAL Freq = ");

  DEBUG_PRINT(Freq);
  DEBUG_PRINTLN(" MHz");
  Freq = getApbFrequency();
  DEBUG_PRINT("APB Freq = ");
  DEBUG_PRINT(Freq);
  DEBUG_PRINTLN(" Hz");
}
#endif

/***************************************************************************************************************************************/
//initTasks
/***************************************************************************************************************************************/
void initTasks() {

  //create a task that will be executed in the Task1code() function, with priority 1 and executed on core 0
  BaseType_t task1Result = xTaskCreatePinnedToCore(
                    Task1code,   /* Task function. */
                    "Task1",     /* name of task. */
                    8192,        /* Stack size in bytes */
                    NULL,        /* parameter of the task */
                    1,           /* priority of the task */
                    &Task1,      /* Task handle to keep track of created task */
                    0);          /* pin task to core 0 */                  
  DEBUG_PRINTF("[TASK] CPU task create result=%ld handle=%p\n", (long) task1Result, Task1);
  delay(500); 

  //create a task that will be executed in the Task2code() function, with priority 1 and executed on core 1
  BaseType_t task2Result = xTaskCreatePinnedToCore(
                    Task2code,   /* Task function. */
                    "Task2",     /* name of task. */
                    8192,        /* Stack size in bytes */
                    NULL,        /* parameter of the task */
                    2,           /* priority of the task */
                    &Task2,      /* Task handle to keep track of created task */
                    1);          /* pin task to core 1 */
  DEBUG_PRINTF("[TASK] Video task create result=%ld handle=%p\n", (long) task2Result, Task2);
  delay(500);
}

/***************************************************************************************************************************************/
// Task1code - 6502 Runtime
/***************************************************************************************************************************************/
uint64_t EmulationTimingBaseCycles = 0;
uint64_t EmulationTimingBaseMicros = 0;
bool EmulationTimingReady = false;

void PaceSpeakerToEmulatedCycle() {
  if (!EmulationTimingReady)
    return;

  const uint64_t appleClockHz = 1023000ULL;
  uint64_t targetMicros = EmulationTimingBaseMicros
    + ((TotalCycles - EmulationTimingBaseCycles) * 1000000ULL) / appleClockHz;
  uint64_t nowMicros = esp_timer_get_time();
  if (targetMicros > nowMicros)
    delayMicroseconds((unsigned int) (targetMicros - nowMicros));
}

void Task1code( void * pvParameters ){
  
#ifdef SHOWTASKCORE
  showTaskCore();
#endif

  initCode();  
  DEBUG_PRINTF("[CPU] reset complete PC=%04X SP=%02X SR=%02X\n", PC, STP, SR);
  unsigned long instructionCount = 0;
  unsigned long nextTrace = 1000000UL;
  unsigned int tracesRemaining = 8;
  unsigned long cpuTraceStartedAt = millis();
  const uint64_t appleClockHz = 1023000ULL;
  const uint64_t pacingIntervalCycles = 1024ULL;
  EmulationTimingBaseCycles = TotalCycles;
  EmulationTimingBaseMicros = esp_timer_get_time();
  EmulationTimingReady = true;
  uint64_t nextPacingCycle = TotalCycles + pacingIntervalCycles;
#if ENABLE_STACK_TELEMETRY
  unsigned long nextStackReport = millis() + 10000;
#endif
  
  // Loop Task1
  for(;;) {
    execCode();
#if ENABLE_CPU_TRACE
    instructionCount++;
    if (tracesRemaining && instructionCount == nextTrace) {
      unsigned long elapsedMs = millis() - cpuTraceStartedAt;
      unsigned long instructionsPerSecond = elapsedMs
        ? (unsigned long) (((uint64_t) instructionCount * 1000ULL) / elapsedMs)
        : 0;
      DEBUG_PRINTF("[CPU] instructions=%lu elapsed=%lums rate=%lu/s PC=%04X A=%02X X=%02X Y=%02X SP=%02X SR=%02X\n",
                   instructionCount, elapsedMs, instructionsPerSecond,
                   PC, A, X, Y, STP, SR);
      if (instructionCount == 1000000UL) {
        DEBUG_PRINT("[CPU] RAM $BD90-$BDAF:");
        for (unsigned int address = 0xBD90; address <= 0xBDAF; address++)
          DEBUG_PRINTF(" %02X", RAM[address]);
        DEBUG_PRINTLN();
      }
      unsigned short diskBlock = RAM[0x48] | ((unsigned short) RAM[0x49] << 8);
      if (diskBlock < 0xBFF0) {
        DEBUG_PRINTF("[CPU] $48=%04X block:", diskBlock);
        for (unsigned int offset = 0; offset < 16; offset++)
          DEBUG_PRINTF(" %02X", RAM[diskBlock + offset]);
        DEBUG_PRINTLN();
      }
      nextTrace *= 2;
      tracesRemaining--;
    }
#endif

#if ENABLE_STACK_TELEMETRY
    if ((long) (millis() - nextStackReport) >= 0) {
      DEBUG_PRINTF("[TASK] CPU minimum free stack=%u bytes\n",
                   (unsigned) uxTaskGetStackHighWaterMark(NULL));
      nextStackReport += 10000;
    }
#endif

    // Pace the emulated machine from accumulated 6502 cycles. This gives the
    // CPU, speaker soft switch, video changes, and Disk II one shared clock.
    if (TotalCycles >= nextPacingCycle) {
      uint64_t targetMicros = EmulationTimingBaseMicros
        + ((TotalCycles - EmulationTimingBaseCycles) * 1000000ULL) / appleClockHz;
      uint64_t nowMicros = esp_timer_get_time();
      if (targetMicros > nowMicros) {
        uint64_t waitMicros = targetMicros - nowMicros;
        if (waitMicros > 1000ULL)
          delay((unsigned long) (waitMicros / 1000ULL));
        else
          delayMicroseconds((unsigned int) waitMicros);
      } else if (nowMicros - targetMicros > 250000ULL) {
        // Serial output or a slow host-side operation can stall this task.
        // Rebase instead of trying to run above Apple II speed to catch up.
        EmulationTimingBaseCycles = TotalCycles;
        EmulationTimingBaseMicros = nowMicros;
      }
      nextPacingCycle = TotalCycles + pacingIntervalCycles;
    }
  } 
}

/***************************************************************************************************************************************/
//Task2code - Video AppleII
/***************************************************************************************************************************************/
void Task2code( void * pvParameters ){
  auto keyboard = PS2Controller.keyboard();

#ifdef SHOWTASKCORE
  showTaskCore ();
#endif

#if ENABLE_STACK_TELEMETRY
  unsigned long nextStackReport = millis() + 10000;
#endif

  // Loop Task2
  for(;;) {

#ifdef DOS_33
    timerFlash.update();
#endif
//    if (cycles >= lastCycles) {
//		  lastCycles = lastCycles + lineCycles;
      // Refresh the image
      for (int rasterline = 0; rasterline<24; rasterline++)  
	      virtline(rasterline);
//	  }
    // Read keyboard input
    if (keyboard->scancodeAvailable()) {
        keyboard_In (keyboard->getNextScancode());
    }
#if ENABLE_STACK_TELEMETRY
    if ((long) (millis() - nextStackReport) >= 0) {
      DEBUG_PRINTF("[TASK] Video minimum free stack=%u bytes\n",
                   (unsigned) uxTaskGetStackHighWaterMark(NULL));
      nextStackReport += 10000;
    }
#endif
  }
}

/***************************************************************************************************************************************/
// Setup
/***************************************************************************************************************************************/
void setup()
{  
#ifdef DEBUG_PROG
  Serial.begin(BAUD_RATE);
  Serial.setRxBufferSize(SERIAL_SIZE_RX);
  Serial.setTxBufferSize(SERIAL_SIZE_TX);
  delay(1000); 
#endif
  
  DEBUG_PRINTLN( "EspApple II Emulator (ESP32) ");

  // Disable the watchdog on both cores
  disableCore0WDT();
  delay(100); // experienced crashes without this delay!
  disableCore1WDT();

  VGAController.queueSize = 256;  // trade UI speed using less RAM and allow both WiFi
  VGAController.begin();
  
  #ifdef VIDEO_400X300_OVER_640X480
  // Use standard 640x480 sync/timing for television compatibility, while
  // retaining the existing centered 400x300 framebuffer. Both are 4:3.
  VGAController.setResolution(VGA_640x480_60Hz, 400, 300);
  #endif

  #ifdef VGA_320x200_70Hz
  VGAController.setResolution(VGA_320x200_70Hz);
  #endif

  // this speed-up display but may generate flickering
	VGAController.enableBackgroundPrimitiveExecution(false);
  VGAController.enableBackgroundPrimitiveTimeout(false);

  canvas.setBrushColor(Color::Black);
  canvas.clear();
  canvas.setOrigin(VGA_CANVAS_OFFSET_X, VGA_CANVAS_OFFSET_Y);
  DrawVGAAlignmentMarkers();
  DEBUG_PRINTF("[VIDEO] canvas inset x=%d y=%d, signal=%dx%d canvas=%dx%d\n",
               VGA_CANVAS_OFFSET_X, VGA_CANVAS_OFFSET_Y,
               VGAController.getScreenWidth(), VGAController.getScreenHeight(),
               canvas.getWidth(), canvas.getHeight());

  canvas.selectFont(&fabgl::FONT_8x8);
 
  canvas.setGlyphOptions(GlyphOptions().FillBackground(true));
  
  // Initialize the PS/2 keyboard
  PS2Controller.begin(PS2Preset::KeyboardPort0, KbdMode::NoVirtualKeys);

  // Discover, select, and load the boot disk before starting emulator tasks.
  bool diskReady = LoadBootDiskFromSD();

  // Splash screen
  canvas.setBrushColor(Color::Black);
  canvas.clear();
  DrawVGAAlignmentMarkers();
  canvas.setPenColor(Color::BrightYellow );
  canvas.drawText(20, 25, " Apple II - ESP32 FABGL (2023) ");

  if (!diskReady) {
    canvas.setPenColor(Color::BrightRed);
    canvas.drawText(20, 50, "SD DISK ERROR");
    canvas.drawText(20, 65, DiskLoadError);
    DEBUG_PRINTLN("[BOOT] Emulator stopped because the boot disk is unavailable.");
    return;
  }

  canvas.setPenColor(Color::BrightGreen);
  canvas.drawText(20, 50, "Disk loaded from SD:");
  canvas.drawText(20, 65, LoadedDiskName);

  DEBUG_PRINTF("free heap:%.1fkb", (float)esp_get_free_heap_size() / 1024.0);
  DEBUG_PRINTLN();
	
  delay (2000);

#ifdef DOS_33
  // Install the timer used for character flashing
  timerFlash.every(FATOR_TIMER_FLASH, flashCaracter);
#endif

  speaker_begin();

  sei();

  initTasks();
}

/***************************************************************************************************************************************/
// Loop
/***************************************************************************************************************************************/
void loop()
{
}
