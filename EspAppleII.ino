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

// Hardware-confirmed native VGA16 framebuffer mode.
#define VGA_320x200_70Hz

#define VGA_ALIGNMENT_MARKERS 0

// SD-backed boot disk support
#define DOS_33

#include "Timer.h"                    // https://github.com/JChristensen/Timer
#include "fabgl.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "common.h"

fabgl::PS2Controller    PS2Controller;          // PS/2 keyboard controller
fabgl::VGA16Controller  VGAController;          // VGA video controller
fabgl::Canvas           canvas(&VGAController);

static const fabgl::RGB888 appleIIPalette[16] = {
  {  0,   0,   0}, {170,   0,  85}, {  0,   0, 170}, {170,   0, 255},
  {  0,  85,   0}, { 85,  85,  85}, {  0, 170, 255}, {170, 170, 255},
  { 85,  85,   0}, {255,  85,   0}, {170, 170, 170}, {255, 170, 255},
  { 85, 255,   0}, {255, 255,   0}, { 85, 255, 170}, {255, 255, 255}
};

static void ConfigureAppleIIPalette() {
  for (int index = 0; index < 16; index++)
    VGAController.setPaletteItem(index, appleIIPalette[index]);
}

#if ENABLE_VGA_PALETTE_TEST
static void DrawVGAPaletteTest() {
  canvas.setOrigin(0, 0);
  int width = canvas.getWidth();
  int height = canvas.getHeight();
  int cellWidth = width / 4;
  int cellHeight = height / 4;
  for (int index = 0; index < 16; index++) {
    int left = (index & 3) * cellWidth;
    int top = (index >> 2) * cellHeight;
    int right = (index & 3) == 3 ? width - 1 : left + cellWidth - 1;
    int bottom = (index >> 2) == 3 ? height - 1 : top + cellHeight - 1;
    canvas.setBrushColor((Color) index);
    canvas.fillRectangle(left, top, right, bottom);
    canvas.setPenColor(index == 0 || index == 2 || index == 4 || index == 5 || index == 8
                       ? (Color) 15 : (Color) 0);
    char label[2] = { index < 10 ? (char) ('0' + index) : (char) ('A' + index - 10), '\0' };
    canvas.drawText(left + 8, top + 8, label);
  }
}
#endif

void DrawVGAAlignmentMarkers() {
#if VGA_ALIGNMENT_MARKERS
  // Some televisions auto-position VGA from the bounding box of non-black
  // pixels. Anchor all four edges so later Apple II content cannot cause the
  // television to reinterpret the left edge. Work in physical canvas
  // coordinates, independently of the Apple II safe-area origin.
  canvas.setOrigin(0, 0);
  int right = canvas.getWidth() - 1;
  int bottom = canvas.getHeight() - 1;
  canvas.setPixel(0, 0, Color::BrightWhite);
  canvas.setPixel(right, 0, Color::BrightWhite);
  canvas.setPixel(0, bottom, Color::BrightWhite);
  canvas.setPixel(right, bottom, Color::BrightWhite);
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
unsigned char * AUXRAM = NULL;
unsigned char * AUXRAMEXT = NULL;
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
volatile uint32_t CPUInstructionHeartbeat = 0;
volatile unsigned short CPUInstructionStartPC = 0;
volatile unsigned char CPUInstructionOpcode = 0xFF;
volatile unsigned char CPUExecutionStage = 0;
unsigned short CPURecentPC[16] = { 0 };
unsigned char CPURecentOpcode[16] = { 0 };
unsigned short CPURecentArgument[16] = { 0 };
unsigned char CPURecentIndex = 0;

static bool EmulationTargetMicros(uint64_t nowMicros, uint64_t * targetMicros) {
  uint64_t total = TotalCycles;
  uint64_t baseCycles = EmulationTimingBaseCycles;
  uint64_t baseMicros = EmulationTimingBaseMicros;
  if (total < baseCycles || nowMicros < baseMicros) {
    EmulationTimingBaseCycles = total;
    EmulationTimingBaseMicros = nowMicros;
    *targetMicros = nowMicros;
    return false;
  }

  uint64_t elapsedCycles = total - baseCycles;
  // Rebase periodically. Besides keeping the math compact, this ensures a
  // damaged timestamp cannot turn speaker or CPU pacing into a huge delay.
  if (elapsedCycles > 10230000ULL) {
    EmulationTimingBaseCycles = total;
    EmulationTimingBaseMicros = nowMicros;
    *targetMicros = nowMicros;
    return false;
  }
  *targetMicros = baseMicros + (elapsedCycles * 1000000ULL) / 1023000ULL;
  return true;
}

void PaceSpeakerToEmulatedCycle() {
  if (!EmulationTimingReady)
    return;

  uint64_t nowMicros = esp_timer_get_time();
  uint64_t targetMicros;
  if (EmulationTargetMicros(nowMicros, &targetMicros) && targetMicros > nowMicros) {
    uint64_t waitMicros = targetMicros - nowMicros;
    if (waitMicros <= 20000ULL)
      delayMicroseconds((unsigned int) waitMicros);
  }
}

void Task1code( void * pvParameters ){
  
#ifdef SHOWTASKCORE
  showTaskCore();
#endif

  initCode();  
  DEBUG_PRINTF("[CPU] reset complete PC=%04X SP=%02X SR=%02X\n", PC, STP, SR);
  ArmDiskPointerWatchpoint();
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
  uint32_t previousReportHeartbeat = CPUInstructionHeartbeat;
  unsigned short previousReportPC = PC;
#endif
  
  // Loop Task1
  for(;;) {
    CPUInstructionStartPC = PC;
    CPUInstructionOpcode = 0xFF;
    unsigned char traceA = A, traceX = X, traceY = Y;
    unsigned char traceSP = STP, traceSR = SR;
    uint64_t traceCycles = TotalCycles;
    CPUExecutionStage = 1; // fetching/executing a 6502 instruction
    execCode();
    RecordD6ControlFlow(CPUInstructionStartPC, CPUInstructionOpcode,
                        argument_addr, traceA, traceX, traceY, traceSP,
                        traceSR, traceCycles, PC);
    CPURecentPC[CPURecentIndex] = CPUInstructionStartPC;
    CPURecentOpcode[CPURecentIndex] = CPUInstructionOpcode;
    CPURecentArgument[CPURecentIndex] = argument_addr;
    CPURecentIndex = (CPURecentIndex + 1) & 0x0F;
    CPUExecutionStage = 0;
    CPUInstructionHeartbeat++;
    uint32_t resumedDiskTransaction;
    unsigned long diskFlushElapsed;
    if (ConsumeDiskFlushResume(&resumedDiskTransaction, &diskFlushElapsed)) {
      DEBUG_PRINTF("[HOSTIO] CPU resumed after disk transaction=%lu "
                   "flushElapsed=%lums nextPC=%04X heartbeat=%lu\n",
                   (unsigned long) resumedDiskTransaction, diskFlushElapsed,
                   PC, (unsigned long) CPUInstructionHeartbeat);
    }
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
      uint32_t heartbeat = CPUInstructionHeartbeat;
      uint32_t instructionDelta = heartbeat - previousReportHeartbeat;
      DEBUG_PRINTF("[CPU-LIVE] heartbeat=%lu delta=%lu PC=%04X opcode=%02X A=%02X X=%02X Y=%02X SP=%02X SR=%02X stack=%u",
                   (unsigned long) heartbeat, (unsigned long) instructionDelta,
                   CPUInstructionStartPC, CPUInstructionOpcode,
                   A, X, Y, STP, SR,
                   (unsigned) uxTaskGetStackHighWaterMark(NULL));
      PrintDiskRuntimeState();
      DEBUG_PRINTF(" joy=%u,%u button=%u\n",
                   (unsigned) HostPaddleValue(0), (unsigned) HostPaddleValue(1),
                   (unsigned) HostJoystickButton(0));
      CheckDiskLoaderSearch();
      if (instructionDelta && CPUInstructionStartPC == previousReportPC) {
        DEBUG_PRINT("[CPU-LIVE] repeating-PC history:");
        for (int historyOffset = 0; historyOffset < 16; historyOffset++) {
          unsigned char historyIndex = (CPURecentIndex + historyOffset) & 0x0F;
          DEBUG_PRINTF(" %04X:%02X@%04X", CPURecentPC[historyIndex],
                       CPURecentOpcode[historyIndex], CPURecentArgument[historyIndex]);
        }
        DEBUG_PRINTLN();
      }
      previousReportHeartbeat = heartbeat;
      previousReportPC = CPUInstructionStartPC;
      nextStackReport += 10000;
    }
#endif

    // Pace the emulated machine from accumulated 6502 cycles. This gives the
    // CPU, speaker soft switch, video changes, and Disk II one shared clock.
    if (TotalCycles >= nextPacingCycle) {
      uint64_t nowMicros = esp_timer_get_time();
      uint64_t targetMicros;
      bool targetValid = EmulationTargetMicros(nowMicros, &targetMicros);
      if (targetValid && targetMicros > nowMicros) {
        uint64_t waitMicros = targetMicros - nowMicros;
        if (waitMicros > 20000ULL) {
          // A normal pacing correction is at most about one 1024-cycle slice.
          // Never let corrupt/stale timing arithmetic park the CPU task for an
          // observable length of time.
          DEBUG_PRINTF("[CPU] pacing anomaly=%lluus; clock rebased\n", waitMicros);
          EmulationTimingBaseCycles = TotalCycles;
          EmulationTimingBaseMicros = nowMicros;
        } else if (waitMicros > 1000ULL)
          delay((unsigned long) (waitMicros / 1000ULL));
        else
          delayMicroseconds((unsigned int) waitMicros);
      } else if (targetValid && nowMicros - targetMicros > 250000ULL) {
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
  uint32_t lastCPUHeartbeat = CPUInstructionHeartbeat;
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
      uint32_t currentCPUHeartbeat = CPUInstructionHeartbeat;
      if (currentCPUHeartbeat < lastCPUHeartbeat &&
          !(lastCPUHeartbeat > 0xF0000000UL && currentCPUHeartbeat < 0x0FFFFFFFUL)) {
        DEBUG_PRINTF("[TASK] CPU HEARTBEAT REGRESSION previous=%lu current=%lu taskState=%d stage=%u PC=%04X opcode=%02X SP=%02X cycles=%llu freeHeap=%u maxAlloc=%u resetReason=%d\n",
                     (unsigned long) lastCPUHeartbeat,
                     (unsigned long) currentCPUHeartbeat,
                     Task1 ? (int) eTaskGetState(Task1) : -1,
                     (unsigned) CPUExecutionStage, CPUInstructionStartPC,
                     CPUInstructionOpcode, STP, TotalCycles,
                     ESP.getFreeHeap(), ESP.getMaxAllocHeap(),
                     (int) esp_reset_reason());
      }
      if (currentCPUHeartbeat == lastCPUHeartbeat) {
        PrintDiskHostIOState();
        if (!DiskHostIOActive) {
          DEBUG_PRINTF("[TASK] CPU STALLED taskState=%d stage=%u PC=%04X opcode=%02X cycle=%lu\n",
                       Task1 ? (int) eTaskGetState(Task1) : -1,
                       (unsigned) CPUExecutionStage, CPUInstructionStartPC,
                       CPUInstructionOpcode, cycle);
        }
      }
      lastCPUHeartbeat = currentCPUHeartbeat;
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
  DEBUG_PRINTF("[BOOT] resetReason=%d freeHeap=%u maxAlloc=%u\n",
               (int) esp_reset_reason(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());

#if ENABLE_CPU_VALIDATION_TEST
  RunCPUValidationTests();
  for (;;)
    delay(1000);
#endif

#if ENABLE_IIE_BANKING_VALIDATION_TEST
  RunIIeBankingValidationTests();
  for (;;)
    delay(1000);
#endif

#if ENABLE_KLAUS_CPU_TEST
  RunKlausCPUValidationTests();
  for (;;)
    delay(1000);
#endif

#if ENABLE_DISK_II_VALIDATION_TEST
  RunDiskIIValidationTests();
  for (;;)
    delay(1000);
#endif

#if ENABLE_DISK_OVERLAY_VALIDATION_TEST
  RunDiskOverlayValidationTests();
  for (;;)
    delay(1000);
#endif

  // Disable the watchdog on both cores
  disableCore0WDT();
  delay(100); // experienced crashes without this delay!
  disableCore1WDT();

  VGAController.queueSize = 256;  // trade UI speed using less RAM and allow both WiFi
  VGAController.begin();
  
  #ifdef VGA_320x200_70Hz
  VGAController.setResolution(VGA_320x200_70Hz);
  #endif

  canvas.setOrigin(0, 0);
  fabgl::Point physicalOrigin = canvas.getOrigin();
  DEBUG_PRINTF("[VIDEO] canvas=%dx%d origin=%d,%d screen=%dx%d freeHeap=%u maxAlloc=%u\n",
               canvas.getWidth(), canvas.getHeight(), physicalOrigin.X, physicalOrigin.Y,
               VGAController.getScreenWidth(), VGAController.getScreenHeight(), ESP.getFreeHeap(),
               ESP.getMaxAllocHeap());
  if (canvas.getWidth() != 320 || canvas.getHeight() != 200) {
    DEBUG_PRINTF("[VIDEO] WARNING framebuffer mismatch requested=320x200 actual=%dx%d\n",
                 canvas.getWidth(), canvas.getHeight());
  }

  // FabGL restores its default VGA16 palette in setResolution(), so install
  // the Apple II palette only after the selected resolution is active.
  ConfigureAppleIIPalette();

  // this speed-up display but may generate flickering
	VGAController.enableBackgroundPrimitiveExecution(false);
  VGAController.enableBackgroundPrimitiveTimeout(false);

  canvas.setBrushColor(Color::Black);
  canvas.clear();
  canvas.setOrigin(0, 0);
  DrawVGAAlignmentMarkers();

  canvas.selectFont(&fabgl::FONT_8x8);
 
  canvas.setGlyphOptions(GlyphOptions().FillBackground(true));

#if ENABLE_VGA_PALETTE_TEST
  DrawVGAPaletteTest();
  DEBUG_PRINTLN("[VIDEO] raw VGA16 Apple II palette test active");
  for (;;)
    delay(1000);
#endif

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

  InitializeLCProvenanceDiagnostics();

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
