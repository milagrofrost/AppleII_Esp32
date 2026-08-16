/*****************************************************************************

    File: "disk.ino"
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

#include "common.h"
#include "esp32-hal-psram.h"
#include "esp_cpu.h"
#include <dirent.h>
#include <sys/stat.h>
extern "C" {
  #include "esp32/spiram.h"
}

unsigned long cycle;
uint64_t TotalCycles;

static const size_t DISK_IMAGE_SIZE = 143360;
static const int DISK_TRACK_COUNT = 35;
static const int MAX_DISK_HALF_TRACK = (DISK_TRACK_COUNT - 1) * 2;
static const char * DISK_MOUNT_PATH = "/SD";
static const char * DISK_DIRECTORY = "/SD/apple2/disks";
static const char * DISK_INDEX_PATH = "/SD/apple2/disks/apple2-index.txt";
static const char * DISK_INDEX_HEADER = "ESPAPPLEII-DISK-INDEX-1";
static const int MAX_DISK_IMAGES_PSRAM = 4096;
static const int MAX_DISK_IMAGES_FALLBACK = 32;
static const int MAX_DISK_SCAN_DEPTH = 8;
static const size_t MAX_DISK_PATH = 384;
unsigned char * DiskImage = NULL;
unsigned char * DriveDiskImage[2] = { NULL, NULL };
static bool DiskPSRAMReady = false;
static bool DriveDiskImageHeapAllocated[2] = { false, false };
static char DriveSavePath[2][MAX_DISK_PATH + 16] = {{0}, {0}};
char DiskLoadError[96] = "Disk image has not been loaded";
char LoadedDiskName[64] = "";
#if ENABLE_DISK_SECTOR_TRACE
static unsigned int DiskReadTraceCount = 0;
#endif
static unsigned int DiskTrackTraceCount = 0;
volatile bool DiskHostIOActive = false;
static volatile uint32_t DiskHostIOTransaction = 0;
static volatile unsigned long DiskHostIOStartedAt = 0;
static volatile int DiskHostIODrive = -1;
static volatile size_t DiskHostIOOffset = 0;
static volatile size_t DiskHostIOSize = 0;
static const char * volatile DiskHostIOOperation = "idle";
static unsigned int DiskHostIODepth = 0;
static volatile bool DiskFlushResumePending = false;
static volatile uint32_t DiskFlushResumeTransaction = 0;
static volatile unsigned long DiskFlushResumeElapsed = 0;
static uint32_t DiskWriteSession = 0;
static unsigned int DiskWriteSessionBytes = 0;
static unsigned int DiskWriteTraceCount = 0;
static unsigned char * DiskMountedBuffer[2] = { NULL, NULL };
static bool DiskBufferCorruptionReported = false;
#if ENABLE_DISK_DIAGNOSTICS
static unsigned int DiskIOTraceCount = 0;
static unsigned long DiskHeadPositionChangedAt = 0;

// Timing-critical Disk II reads are captured in RAM. Serial output occurs
// only after a prolonged, same-track loader search has been detected.
struct DiskLoaderTraceEvent {
  uint32_t cycle;
  uint16_t pc;
  uint16_t index;
  uint16_t delta;
  uint8_t address;
  uint8_t latch;
  uint8_t track;
  uint8_t flags;
};
// Keep this in internal RAM: controller access is timing-critical and this
// target has little linker-visible DRAM headroom. Forty-eight events cover
// several complete custom-loader polling sequences.
static const unsigned int DISK_LOADER_TRACE_SIZE = 48;
static DiskLoaderTraceEvent DiskLoaderTrace[DISK_LOADER_TRACE_SIZE];
static unsigned int DiskLoaderTraceIndex = 0;
static uint32_t DiskLoaderLastAccessCycle = 0;
static uint32_t DiskLoaderReadCount = 0;
static uint32_t DiskLoaderWriteCount = 0;
static uint32_t DiskLoaderCheckReadCount = 0;
static uint32_t DiskLoaderCheckWriteCount = 0;
static int DiskLoaderCheckDrive = -1;
static int DiskLoaderCheckTrack = -1;
static unsigned char DiskLoaderStagnantIntervals = 0;
static bool DiskLoaderSnapshotDumped = false;
#endif

static uint32_t BeginDiskHostIO(const char * operation, int drive,
                                size_t offset, size_t size) {
  if (DiskHostIODepth++ == 0) {
    DiskHostIOTransaction++;
    DiskHostIOOperation = operation;
    DiskHostIODrive = drive;
    DiskHostIOOffset = offset;
    DiskHostIOSize = size;
    DiskHostIOStartedAt = millis();
    DiskHostIOActive = true;
    DEBUG_PRINTF("[HOSTIO] BEGIN transaction=%lu op=%s drive=%d offset=%u "
                 "size=%u PC=%04X heartbeat=%lu\n",
                 (unsigned long) DiskHostIOTransaction, operation, drive + 1,
                 (unsigned) offset, (unsigned) size, PC,
                 (unsigned long) CPUInstructionHeartbeat);
  }
  return DiskHostIOTransaction;
}

static unsigned long EndDiskHostIO(uint32_t transaction, bool success) {
  if (!DiskHostIODepth)
    return 0;
  if (--DiskHostIODepth)
    return millis() - DiskHostIOStartedAt;

  unsigned long elapsed = millis() - DiskHostIOStartedAt;
  DEBUG_PRINTF("[HOSTIO] END transaction=%lu op=%s drive=%d success=%d "
               "elapsed=%lums PC=%04X heartbeat=%lu\n",
               (unsigned long) transaction, DiskHostIOOperation,
               DiskHostIODrive + 1, success, elapsed, PC,
               (unsigned long) CPUInstructionHeartbeat);
  DiskHostIOActive = false;
  DiskHostIOOperation = "idle";
  return elapsed;
}

void PrintDiskHostIOState() {
  if (!DiskHostIOActive)
    return;
  DEBUG_PRINTF("[TASK] CPU HOST-IO-WAIT transaction=%lu op=%s drive=%d "
               "offset=%u size=%u elapsed=%lums taskState=%d PC=%04X\n",
               (unsigned long) DiskHostIOTransaction, DiskHostIOOperation,
               DiskHostIODrive + 1, (unsigned) DiskHostIOOffset,
               (unsigned) DiskHostIOSize, millis() - DiskHostIOStartedAt,
               Task1 ? (int) eTaskGetState(Task1) : -1,
               CPUInstructionStartPC);
}

bool ConsumeDiskFlushResume(uint32_t * transaction, unsigned long * elapsedMs) {
  if (!DiskFlushResumePending)
    return false;
  if (transaction)
    *transaction = DiskFlushResumeTransaction;
  if (elapsedMs)
    *elapsedMs = DiskFlushResumeElapsed;
  DiskFlushResumePending = false;
  return true;
}

void ArmDiskPointerWatchpoint() {
#if ENABLE_DISK_POINTER_WATCHPOINT
  // DriveDiskImage[0..1] and DiskImage occupy 12 bytes. Watch the enclosing
  // aligned 16-byte block, which excludes the adjacent TotalCycles counter.
  uintptr_t watchAddress = ((uintptr_t) &DriveDiskImage[0]) & ~(uintptr_t) 0x0F;
  esp_err_t result = esp_cpu_set_watchpoint(0, (void *) watchAddress, 16,
                                            ESP_WATCHPOINT_STORE);
  DEBUG_PRINTF("[WATCH] disk-owner store watchpoint result=%d core=%d "
               "address=%p size=16 owners=%p legacy=%p cycles=%p\n",
               (int) result, xPortGetCoreID(), (void *) watchAddress,
               (void *) &DriveDiskImage[0], (void *) &DiskImage,
               (void *) &TotalCycles);
#endif
}

static const char * ConfigureDriveSavePath(int drive, const char * sourcePath) {
  if (drive < 0 || drive > 1 || !sourcePath ||
      snprintf(DriveSavePath[drive], sizeof(DriveSavePath[drive]),
               "%s.sav.dsk", sourcePath) >= (int) sizeof(DriveSavePath[drive])) {
    if (drive >= 0 && drive <= 1)
      DriveSavePath[drive][0] = '\0';
    return sourcePath;
  }

  FILE * save = fopen(DriveSavePath[drive], "rb");
  if (save) {
    fseek(save, 0, SEEK_END);
    long saveSize = ftell(save);
    fclose(save);
    if (saveSize == (long) DISK_IMAGE_SIZE) {
      DEBUG_PRINTF("[DISK] drive %d loading writable save image %s\n",
                   drive + 1, DriveSavePath[drive]);
      DEBUG_PRINTF("[HOSTIO] OVERLAY-READY drive=%d created=0 cpuTaskState=%d "
                   "path=%s\n", drive + 1,
                   Task1 ? (int) eTaskGetState(Task1) : -1,
                   DriveSavePath[drive]);
      return DriveSavePath[drive];
    }
    DEBUG_PRINTF("[DISK] replacing invalid save image size=%ld path=%s\n",
                 saveSize, DriveSavePath[drive]);
  }

  // Create the copy-on-write base while emulation is stopped in the disk
  // selector. Runtime track flushes can then update only their 256-byte
  // sectors instead of blocking the CPU to write a complete image.
  uint32_t hostTransaction = BeginDiskHostIO("overlay-create", drive, 0,
                                             DISK_IMAGE_SIZE);
  FILE * source = fopen(sourcePath, "rb");
  save = source ? fopen(DriveSavePath[drive], "wb") : NULL;
  unsigned char * copyBuffer = save ? (unsigned char *) malloc(16384) : NULL;
  size_t copied = 0;
  while (copyBuffer && copied < DISK_IMAGE_SIZE) {
    size_t chunk = DISK_IMAGE_SIZE - copied;
    if (chunk > 16384)
      chunk = 16384;
    size_t bytesRead = fread(copyBuffer, 1, chunk, source);
    if (bytesRead != chunk || fwrite(copyBuffer, 1, chunk, save) != chunk)
      break;
    copied += chunk;
  }
  if (copyBuffer)
    free(copyBuffer);
  if (save)
    fclose(save);
  if (source)
    fclose(source);

  if (copied == DISK_IMAGE_SIZE) {
    DEBUG_PRINTF("[DISK] drive %d prepared writable save image %s\n",
                 drive + 1, DriveSavePath[drive]);
    unsigned long elapsed = EndDiskHostIO(hostTransaction, true);
    DEBUG_PRINTF("[HOSTIO] OVERLAY-READY drive=%d created=1 elapsed=%lums "
                 "cpuTaskState=%d path=%s\n", drive + 1, elapsed,
                 Task1 ? (int) eTaskGetState(Task1) : -1,
                 DriveSavePath[drive]);
    return DriveSavePath[drive];
  }

  remove(DriveSavePath[drive]);
  DEBUG_PRINTF("[DISK] unable to prepare writable save image; drive %d will be read-only\n",
               drive + 1);
  DriveSavePath[drive][0] = '\0';
  EndDiskHostIO(hostTransaction, false);
  return sourcePath;
}

struct DiskMenuEntry {
  uint32_t pathOffset;
  uint16_t nameOffset;
};

static DiskMenuEntry DiskMenuFallback[MAX_DISK_IMAGES_FALLBACK];
static char DiskStringFallback[MAX_DISK_IMAGES_FALLBACK * MAX_DISK_PATH];
static DiskMenuEntry * DiskMenu = DiskMenuFallback;
static char * DiskStringPool = DiskStringFallback;
static size_t DiskStringPoolCapacity = sizeof(DiskStringFallback);
static size_t DiskStringPoolUsed = 0;
static int DiskMenuCapacity = MAX_DISK_IMAGES_FALLBACK;
static int DiskMenuCount = 0;
static uint16_t DiskMenuMatchesFallback[MAX_DISK_IMAGES_FALLBACK];
static uint16_t * DiskMenuMatches = DiskMenuMatchesFallback;
static int DiskMenuMatchCapacity = MAX_DISK_IMAGES_FALLBACK;
static int DiskMenuMatchCount = 0;
static char DiskMenuSearch[32] = "";
static int DiskMenuMarqueeOffset = 0;
static unsigned long DiskMenuMarqueeLastStep = 0;
static unsigned long DiskMenuMarqueeResumeAt = 0;
static unsigned char * DiskIndexScratch = NULL;
static size_t DiskIndexScratchCapacity = 0;
static bool DiskCatalogReady = false;
static unsigned int DiskScanDirectoryCount = 0;
static unsigned int DiskScanPathTooLongCount = 0;
static unsigned int DiskScanCapacitySkippedCount = 0;
static unsigned int DiskScanEntryCount = 0;
static unsigned long DiskScanStartedAt = 0;
static unsigned long DiskScanLastSerialProgressAt = 0;
static unsigned long DiskScanLastVGAProgressAt = 0;

static size_t ReadFileThroughInternalBuffer(FILE * file, unsigned char * destination,
                                            size_t size, const char * label) {
  const size_t transferBufferSize = 8192;
  unsigned char * transferBuffer = (unsigned char *) malloc(transferBufferSize);
  if (!transferBuffer) {
    DEBUG_PRINTF("[SD] No staging RAM for %s; using direct transfer\n", label);
    return fread(destination, 1, size, file);
  }

  size_t total = 0;
  unsigned long startedAt = millis();
  while (total < size) {
    size_t requested = size - total;
    if (requested > transferBufferSize)
      requested = transferBufferSize;
    size_t received = fread(transferBuffer, 1, requested, file);
    if (!received)
      break;
    memcpy(destination + total, transferBuffer, received);
    total += received;
  }
  free(transferBuffer);
  DEBUG_PRINTF("[SD] Buffered %s transfer: %u bytes in %lums\n",
               label, (unsigned) total, millis() - startedAt);
  return total;
}

static void ReportDiskScanProgress(const char * currentPath, bool force = false) {
  unsigned long now = millis();
  bool reportSerial = force || now - DiskScanLastSerialProgressAt >= 1000;
  bool reportVGA = force || now - DiskScanLastVGAProgressAt >= 5000;
  if (!reportSerial && !reportVGA)
    return;

  const char * folder = currentPath ? strrchr(currentPath, '/') : NULL;
  folder = folder ? folder + 1 : (currentPath ? currentPath : "");
  char shortFolder[43];
  snprintf(shortFolder, sizeof(shortFolder), "%.42s", folder);

  if (reportSerial) {
    DiskScanLastSerialProgressAt = now;
    DEBUG_PRINTF("[SD] Scanning: dirs=%u entries=%u disks=%d elapsed=%lums folder=%s\n",
                 DiskScanDirectoryCount, DiskScanEntryCount, DiskMenuCount,
                 now - DiskScanStartedAt, shortFolder);
  }

  if (!reportVGA)
    return;
  DiskScanLastVGAProgressAt = now;

  fabgl::Point savedOrigin = canvas.getOrigin();
  canvas.setOrigin(0, 0);
  canvas.setBrushColor(Color::Black);
  canvas.fillRectangle(0, 0, canvas.getWidth() - 1, 92);
  canvas.setPenColor(Color::BrightYellow);
  canvas.drawText(20, 18,
                  currentPath && !strcmp(currentPath, "PREBUILT INDEX")
                    ? "LOADING PREBUILT DISK INDEX"
                    : "SCANNING APPLE II DISK ARCHIVE");
  canvas.setPenColor(Color::White);
  char status[64];
  snprintf(status, sizeof(status), "FOLDERS %u   FILES %u", DiskScanDirectoryCount, DiskScanEntryCount);
  canvas.drawText(20, 38, status);
  snprintf(status, sizeof(status), "VALID DSK %d   TIME %lus", DiskMenuCount,
           (now - DiskScanStartedAt) / 1000UL);
  canvas.drawText(20, 54, status);
  canvas.setPenColor(Color::BrightCyan);
  canvas.drawText(20, 72, shortFolder);
  canvas.setOrigin(savedOrigin);
}

static void PrepareDiskMenuStorage() {
  DiskMenu = DiskMenuFallback;
  DiskStringPool = DiskStringFallback;
  DiskStringPoolCapacity = sizeof(DiskStringFallback);
  DiskStringPoolUsed = 0;
  DiskMenuCapacity = MAX_DISK_IMAGES_FALLBACK;
  DiskMenuMatches = DiskMenuMatchesFallback;
  DiskMenuMatchCapacity = MAX_DISK_IMAGES_FALLBACK;
  DiskIndexScratch = NULL;
  DiskIndexScratchCapacity = 0;

  if (!DiskPSRAMReady)
    return;

  // esp_spiram_get_size() reports all physical PSRAM (8 MB on this board),
  // but classic ESP32 code can directly address only SOC_EXTRAM_DATA_SIZE
  // (4 MB). The remaining memory requires the separate himem API.
  size_t physicalPSRAMSize = esp_spiram_get_size();
  size_t psramSize = physicalPSRAMSize < SOC_EXTRAM_DATA_SIZE
                   ? physicalPSRAMSize : SOC_EXTRAM_DATA_SIZE;
  // Two disk buffers plus IIe auxiliary main and language-card storage.
  size_t reservedForDrives = 2 * DISK_IMAGE_SIZE + 0x1C000UL;
  if (psramSize <= reservedForDrives)
    return;

  size_t available = psramSize - reservedForDrives;
  size_t capacity = MAX_DISK_IMAGES_PSRAM;
  size_t recordBytes = capacity * (sizeof(DiskMenuEntry) + sizeof(uint16_t));
  if (available <= recordBytes)
    return;
  if (capacity < MAX_DISK_IMAGES_FALLBACK)
    return;

  DiskMenu = (DiskMenuEntry *) ((unsigned char *) SOC_EXTRAM_DATA_LOW + reservedForDrives);
  DiskMenuCapacity = (int) capacity;
  DiskMenuMatches = (uint16_t *) ((unsigned char *) DiskMenu
                    + DiskMenuCapacity * sizeof(DiskMenuEntry));
  DiskMenuMatchCapacity = DiskMenuCapacity;
  unsigned char * usedEnd = (unsigned char *) DiskMenuMatches
                           + DiskMenuMatchCapacity * sizeof(uint16_t);
  unsigned char * psramEnd = (unsigned char *) SOC_EXTRAM_DATA_LOW + psramSize;
  DiskStringPool = (char *) usedEnd;
  DiskStringPoolCapacity = psramEnd - usedEnd;
  DiskStringPoolUsed = 0;
  DEBUG_PRINTF("[MEM] Compact disk index in PSRAM: physical=%u mapped=%u capacity=%d record=%u bytes records=%u stringPool=%u bytes\n",
               (unsigned) physicalPSRAMSize, (unsigned) psramSize,
               DiskMenuCapacity, (unsigned) sizeof(DiskMenuEntry),
               (unsigned) recordBytes, (unsigned) DiskStringPoolCapacity);
}

static const char * DiskEntryPath(int index) {
  return DiskStringPool + DiskMenu[index].pathOffset;
}

static const char * DiskEntryName(int index) {
  return DiskEntryPath(index) + DiskMenu[index].nameOffset;
}

static void ReleaseDiskIndexScratch() {
  if (DiskPSRAMReady && DiskStringPool != DiskStringFallback) {
    unsigned char * psramEnd = (unsigned char *) SOC_EXTRAM_DATA_HIGH;
    DiskStringPoolCapacity = psramEnd - (unsigned char *) DiskStringPool;
  }
  DiskIndexScratch = NULL;
  DiskIndexScratchCapacity = 0;
}

static bool HasDSKExtension(const char * name) {
  size_t length = strlen(name);
  if (length < 4)
    return false;
  const char * extension = name + length - 4;
  return extension[0] == '.' &&
         (extension[1] == 'd' || extension[1] == 'D') &&
         (extension[2] == 's' || extension[2] == 'S') &&
         (extension[3] == 'k' || extension[3] == 'K');
}

static bool AddDiskMenuEntry(const char * path, const char * name, long knownSize = -1) {
  (void) name;
  if (DiskMenuCount >= DiskMenuCapacity) {
    DiskScanCapacitySkippedCount++;
    return false;
  }

  long fileSize = knownSize;
  if (fileSize < 0) {
    struct stat fileInfo;
    if (stat(path, &fileInfo) != 0 || !S_ISREG(fileInfo.st_mode))
      return false;
    fileSize = fileInfo.st_size;
  }
  if (fileSize != (long) DISK_IMAGE_SIZE) {
    DEBUG_PRINTF("[SD] Skipping %s: %ld bytes (need %u)\n",
                 path, fileSize, (unsigned) DISK_IMAGE_SIZE);
    return false;
  }

  size_t pathBytes = strlen(path) + 1;
  if (pathBytes > MAX_DISK_PATH || DiskStringPoolUsed > DiskStringPoolCapacity ||
      pathBytes > DiskStringPoolCapacity - DiskStringPoolUsed) {
    DiskScanPathTooLongCount++;
    return false;
  }
  char * storedPath = DiskStringPool + DiskStringPoolUsed;
  memcpy(storedPath, path, pathBytes);
  const char * storedName = strrchr(storedPath, '/');
  storedName = storedName ? storedName + 1 : storedPath;
  DiskMenu[DiskMenuCount].pathOffset = DiskStringPoolUsed;
  DiskMenu[DiskMenuCount].nameOffset = (uint16_t) (storedName - storedPath);
  DiskStringPoolUsed += pathBytes;
  DiskMenuCount++;
  if ((DiskMenuCount % 250) == 0)
    DEBUG_PRINTF("[SD] Indexed %d disk images in %lums...\n",
                 DiskMenuCount, millis() - DiskScanStartedAt);
  return true;
}

static void ScanDiskDirectory(const char * directoryPath, int depth) {
  if (depth > MAX_DISK_SCAN_DEPTH || DiskMenuCount >= DiskMenuCapacity)
    return;

  DIR * directory = opendir(directoryPath);
  if (!directory)
    return;
  DiskScanDirectoryCount++;
  ReportDiskScanProgress(directoryPath);

  struct dirent * item;
  while ((item = readdir(directory)) != NULL) {
    if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, ".."))
      continue;
    DiskScanEntryCount++;
    ReportDiskScanProgress(directoryPath);

    char path[MAX_DISK_PATH];
    int pathLength = snprintf(path, sizeof(path), "%s/%s", directoryPath, item->d_name);
    if (pathLength < 0 || pathLength >= (int) sizeof(path)) {
      DiskScanPathTooLongCount++;
      continue;
    }

    bool isDirectory = item->d_type == DT_DIR;
    bool isRegular = item->d_type == DT_REG;
    long knownSize = -1;
    if (item->d_type == DT_UNKNOWN) {
      struct stat fileInfo;
      if (stat(path, &fileInfo) != 0)
        continue;
      isDirectory = S_ISDIR(fileInfo.st_mode);
      isRegular = S_ISREG(fileInfo.st_mode);
      knownSize = fileInfo.st_size;
    }

    if (isDirectory) {
      ScanDiskDirectory(path, depth + 1);
    } else if (isRegular && HasDSKExtension(item->d_name)) {
      AddDiskMenuEntry(path, item->d_name, knownSize);
    }

    if (DiskMenuCount >= DiskMenuCapacity)
      break;
  }
  closedir(directory);
}

static int CompareDiskMenuEntries(const void * left, const void * right) {
  const DiskMenuEntry * a = (const DiskMenuEntry *) left;
  const DiskMenuEntry * b = (const DiskMenuEntry *) right;
  const char * aPath = DiskStringPool + a->pathOffset;
  const char * bPath = DiskStringPool + b->pathOffset;
  return strcasecmp(aPath + a->nameOffset, bPath + b->nameOffset);
}

static bool LoadDiskIndex() {
  // FabGL drawing and the FAT/newlib FILE implementation both use FreeRTOS
  // queues. Finish the progress-screen work before opening the index so a
  // live FILE mutex is never carried through VGA primitive processing.
  ReportDiskScanProgress("PREBUILT INDEX", true);

  FILE * indexFile = fopen(DISK_INDEX_PATH, "r");
  if (!indexFile)
    return false;

  // The ESP32 FAT stdio layer makes thousands of fgets() calls very costly.
  // Read the text index in one transfer, then split its lines in spare PSRAM.
  fseek(indexFile, 0, SEEK_END);
  long indexSize = ftell(indexFile);
  rewind(indexFile);
  if (DiskPSRAMReady && indexSize > 0) {
    unsigned char * psramEnd = (unsigned char *) SOC_EXTRAM_DATA_HIGH;
    unsigned char * scratch = psramEnd - ((size_t) indexSize + 1);
    unsigned char * stringsUsedEnd = (unsigned char *) DiskStringPool + DiskStringPoolUsed;
    if (scratch > stringsUsedEnd) {
      DiskIndexScratch = scratch;
      DiskIndexScratchCapacity = (size_t) indexSize + 1;
      DiskStringPoolCapacity = scratch - (unsigned char *) DiskStringPool;
    }
  }
  if (DiskIndexScratch && indexSize > 0 && (size_t) indexSize + 1 <= DiskIndexScratchCapacity) {
    DEBUG_PRINTF("[SD] Loading %ld-byte prebuilt disk index\n", indexSize);
    size_t bytesRead = ReadFileThroughInternalBuffer(indexFile, DiskIndexScratch,
                                                     (size_t) indexSize, "index");
    fclose(indexFile);
    if (bytesRead != (size_t) indexSize)
      return false;
    DiskIndexScratch[indexSize] = '\0';

    char * cursor = (char *) DiskIndexScratch;
    char * end = cursor + indexSize;
    char * lineEnd = (char *) memchr(cursor, '\n', end - cursor);
    if (!lineEnd)
      return false;
    *lineEnd = '\0';
    if (lineEnd > cursor && lineEnd[-1] == '\r')
      lineEnd[-1] = '\0';
    if (strcmp(cursor, DISK_INDEX_HEADER) != 0) {
      DEBUG_PRINTF("[SD] Ignoring incompatible disk index: %s\n", DISK_INDEX_PATH);
      return false;
    }

    DEBUG_PRINTLN("[SD] Parsing prebuilt disk index from PSRAM buffer");
    cursor = lineEnd + 1;
    while (cursor < end && DiskMenuCount < DiskMenuCapacity) {
      lineEnd = (char *) memchr(cursor, '\n', end - cursor);
      if (!lineEnd)
        lineEnd = end;
      char savedEnd = *lineEnd;
      *lineEnd = '\0';
      size_t length = strlen(cursor);
      if (length && cursor[length - 1] == '\r')
        cursor[--length] = '\0';
      DiskScanEntryCount++;
      if (length && cursor[0] != '/' && !strstr(cursor, "../") && HasDSKExtension(cursor)) {
        char path[MAX_DISK_PATH];
        int pathLength = snprintf(path, sizeof(path), "%s/%s", DISK_DIRECTORY, cursor);
        if (pathLength >= 0 && pathLength < (int) sizeof(path)) {
          const char * name = strrchr(cursor, '/');
          AddDiskMenuEntry(path, name ? name + 1 : cursor, DISK_IMAGE_SIZE);
        } else {
          DiskScanPathTooLongCount++;
        }
      }
      *lineEnd = savedEnd;
      cursor = lineEnd < end ? lineEnd + 1 : end;
    }
    DEBUG_PRINTF("[SD] Loaded %d indexed .dsk image(s) in %lums\n",
                 DiskMenuCount, millis() - DiskScanStartedAt);
    ReleaseDiskIndexScratch();
    return DiskMenuCount > 0;
  }

  char line[384];
  if (!fgets(line, sizeof(line), indexFile)) {
    fclose(indexFile);
    return false;
  }
  line[strcspn(line, "\r\n")] = '\0';
  if (strcmp(line, DISK_INDEX_HEADER) != 0) {
    DEBUG_PRINTF("[SD] Ignoring incompatible disk index: %s\n", DISK_INDEX_PATH);
    fclose(indexFile);
    return false;
  }

  DEBUG_PRINTF("[SD] Loading prebuilt disk index: %s\n", DISK_INDEX_PATH);
  while (DiskMenuCount < DiskMenuCapacity && fgets(line, sizeof(line), indexFile)) {
    size_t length = strcspn(line, "\r\n");
    bool completeLine = line[length] == '\r' || line[length] == '\n' || feof(indexFile);
    line[length] = '\0';
    DiskScanEntryCount++;
    if (!completeLine || !line[0] || line[0] == '/' || strstr(line, "../") || !HasDSKExtension(line))
      continue;

    char path[MAX_DISK_PATH];
    int pathLength = snprintf(path, sizeof(path), "%s/%s", DISK_DIRECTORY, line);
    if (pathLength < 0 || pathLength >= (int) sizeof(path)) {
      DiskScanPathTooLongCount++;
      continue;
    }
    const char * name = strrchr(line, '/');
    name = name ? name + 1 : line;
    // The desktop generator already checked file size. Avoid thousands of
    // random FAT metadata reads and validate the selected file when opened.
    AddDiskMenuEntry(path, name, DISK_IMAGE_SIZE);
  }
  fclose(indexFile);
  DEBUG_PRINTF("[SD] Loaded %d indexed .dsk image(s) in %lums\n",
               DiskMenuCount, millis() - DiskScanStartedAt);
  return DiskMenuCount > 0;
}

void FindDiskImages() {
  if (DiskCatalogReady && DiskMenuCount > 0) {
    DEBUG_PRINTF("[SD] Reusing %d cached disk-index entries\n", DiskMenuCount);
    return;
  }

  PrepareDiskMenuStorage();
  DiskMenuCount = 0;
  DiskScanDirectoryCount = 0;
  DiskScanPathTooLongCount = 0;
  DiskScanCapacitySkippedCount = 0;
  DiskScanEntryCount = 0;
  DiskScanStartedAt = millis();
  DiskScanLastSerialProgressAt = 0;
  DiskScanLastVGAProgressAt = 0;

  if (LoadDiskIndex()) {
    DiskCatalogReady = true;
    DEBUG_PRINTF("[SD] Ready with %d prebuilt index entries (capacity=%d pathTooLong=%u)\n",
                 DiskMenuCount, DiskMenuCapacity, DiskScanPathTooLongCount);
    return;
  }

  ReleaseDiskIndexScratch();
  DEBUG_PRINTF("[SD] No usable disk index at %s; directory scan disabled\n", DISK_INDEX_PATH);
}

static bool ContainsCaseInsensitive(const char * text, const char * query) {
  if (!query[0])
    return true;
  size_t queryLength = strlen(query);
  for (; *text; text++) {
    size_t index = 0;
    while (index < queryLength && text[index] &&
           tolower((unsigned char) text[index]) == tolower((unsigned char) query[index]))
      index++;
    if (index == queryLength)
      return true;
  }
  return false;
}

static bool FuzzySubsequenceMatch(const char * text, const char * query) {
  while (*text && *query) {
    if (tolower((unsigned char) *text) == tolower((unsigned char) *query))
      query++;
    text++;
  }
  return *query == '\0';
}

static void RebuildDiskMenuMatches() {
  DiskMenuMatchCount = 0;
  // Strong partial-string matches are shown before looser fuzzy matches.
  for (int pass = 0; pass < 2; pass++) {
    for (int index = 0; index < DiskMenuCount; index++) {
      bool substring = ContainsCaseInsensitive(DiskEntryName(index), DiskMenuSearch);
      bool matches = pass == 0 ? substring
                               : (!substring && FuzzySubsequenceMatch(DiskEntryName(index), DiskMenuSearch));
      if (matches && DiskMenuMatchCount < DiskMenuMatchCapacity)
        DiskMenuMatches[DiskMenuMatchCount++] = (uint16_t) index;
    }
  }
}

static int DiskMenuVisibleRows() {
  const int listTop = 60;
  const int footerHeight = 22;
  int rows = (canvas.getHeight() - listTop - footerHeight) / 12;
  return rows > 0 ? rows : 1;
}

static int DiskMenuFilenameCharacters() {
  int characters = (canvas.getWidth() - 20) / 8 - 2;
  return characters > 0 ? characters : 1;
}

static void FormatDiskMenuName(char * output, size_t outputSize, const char * name,
                               int characters, int marqueeOffset) {
  size_t nameLength = strlen(name);
  if ((int) nameLength <= characters || marqueeOffset < 0) {
    snprintf(output, outputSize, "%.*s", characters, name);
    return;
  }

  int cycleLength = (int) nameLength + 3;
  int count = characters;
  if (count >= (int) outputSize)
    count = outputSize - 1;
  for (int index = 0; index < count; index++) {
    int source = (marqueeOffset + index) % cycleLength;
    output[index] = source < (int) nameLength ? name[source] : ' ';
  }
  output[count] = '\0';
}

static void ResetDiskMenuMarquee() {
  DiskMenuMarqueeOffset = 0;
  DiskMenuMarqueeLastStep = millis();
  DiskMenuMarqueeResumeAt = DiskMenuMarqueeLastStep + 800;
}

static void DrawDiskMenuMarqueeRow(int selected) {
  if (selected < 0 || selected >= DiskMenuMatchCount)
    return;
  int visibleRows = DiskMenuVisibleRows();
  int first = selected >= visibleRows ? selected - visibleRows + 1 : 0;
  int y = 60 + (selected - first) * 12;
  int diskIndex = DiskMenuMatches[selected];
  int filenameCharacters = DiskMenuFilenameCharacters();
  if ((int) strlen(DiskEntryName(diskIndex)) <= filenameCharacters)
    return;

  char visibleName[70];
  FormatDiskMenuName(visibleName, sizeof(visibleName), DiskEntryName(diskIndex),
                     filenameCharacters, DiskMenuMarqueeOffset);
  canvas.setBrushColor(Color::BrightGreen);
  canvas.fillRectangle(20, y, canvas.getWidth() - 1, y + 8);
  canvas.setPenColor(Color::Black);
  char line[72];
  snprintf(line, sizeof(line), "> %s", visibleName);
  canvas.drawText(20, y, line);
}

static void FlashSelectedDiskRow(int selected) {
  if (selected < 0 || selected >= DiskMenuMatchCount)
    return;
  int visibleRows = DiskMenuVisibleRows();
  int first = selected >= visibleRows ? selected - visibleRows + 1 : 0;
  int y = 60 + (selected - first) * 12;
  canvas.invertRectangle(20, y, canvas.getWidth() - 1, y + 8);
  delay(50);
  canvas.invertRectangle(20, y, canvas.getWidth() - 1, y + 8);
}

void DrawDiskLoadingStatus(int drive, const char * path) {
  const char * name = path ? strrchr(path, '/') : NULL;
  name = name ? name + 1 : (path ? path : "");

  fabgl::Point savedOrigin = canvas.getOrigin();
  canvas.setOrigin(0, 0);
  canvas.setBrushColor(Color::Black);
  canvas.clear();
  DrawVGAAlignmentMarkers();

  canvas.setPenColor(Color::BrightYellow);
  char heading[32];
  snprintf(heading, sizeof(heading), "LOADING DRIVE %d", drive + 1);
  canvas.drawText(20, 62, heading);

  canvas.setPenColor(Color::White);
  char visibleName[36];
  snprintf(visibleName, sizeof(visibleName), "%.35s", name);
  canvas.drawText(20, 82, visibleName);
  canvas.setPenColor(Color::BrightCyan);
  canvas.drawText(20, 106, "PREPARING WRITABLE DISK...");
  canvas.drawText(20, 122, "PLEASE WAIT");
  canvas.setOrigin(savedOrigin);
}

static char DiskMenuScancodeToCharacter(int scanCode) {
  if (scanCode < 0 || scanCode > 0x7F)
    return 0;
  unsigned char appleKey = pgm_read_byte_near(scancode_to_apple + scanCode);
  char character = (char) (appleKey & 0x7F);
  return character >= 0x20 && character <= 0x7E ? character : 0;
}

static bool BuildDiskSavePath(const char * sourcePath, char * savePath,
                              size_t savePathSize) {
  return sourcePath && savePath && savePathSize &&
         snprintf(savePath, savePathSize, "%s.sav.dsk", sourcePath) <
           (int) savePathSize;
}

static bool DiskSaveFileExists(const char * savePath) {
  FILE * save = savePath ? fopen(savePath, "rb") : NULL;
  if (!save)
    return false;
  fclose(save);
  return true;
}

static void DrawDeleteSaveConfirmation(int diskIndex) {
  canvas.setBrushColor(Color::Black);
  canvas.clear();
  DrawVGAAlignmentMarkers();
  canvas.setPenColor(Color::BrightRed);
  canvas.drawText(20, 28, "DELETE WRITABLE SAVE?");
  canvas.setPenColor(Color::White);
  canvas.drawText(20, 52, "ORIGINAL DISK WILL NOT BE DELETED");
  char visibleName[36];
  snprintf(visibleName, sizeof(visibleName), "%.35s", DiskEntryName(diskIndex));
  canvas.setPenColor(Color::BrightYellow);
  canvas.drawText(20, 82, visibleName);
  canvas.setPenColor(Color::BrightCyan);
  canvas.drawText(20, 118, "PRESS Y TO DELETE");
  canvas.drawText(20, 136, "PRESS N OR ESC TO CANCEL");
}

static bool DeleteDiskSaveFile(int diskIndex) {
  char savePath[MAX_DISK_PATH + 16];
  if (!BuildDiskSavePath(DiskEntryPath(diskIndex), savePath,
                         sizeof(savePath))) {
    DEBUG_PRINTF("[DISK] save path is too long; delete refused: %s\n",
                 DiskEntryPath(diskIndex));
    return false;
  }
  if (remove(savePath) != 0) {
    DEBUG_PRINTF("[DISK] failed to delete save image %s\n", savePath);
    return false;
  }

  // A runtime selector can delete media that is still mounted in the other
  // drive. Keep its in-memory contents readable, but prevent a later flush
  // from recreating the deleted overlay with stale data.
  for (int drive = 0; drive < 2; drive++) {
    if (DriveSavePath[drive][0] && strcmp(DriveSavePath[drive], savePath) == 0) {
      DriveSavePath[drive][0] = '\0';
    }
  }
  DEBUG_PRINTF("[DISK] deleted writable save image %s\n", savePath);
  return true;
}

static void DrawDiskMenu(int selected) {
  int visibleRows = DiskMenuVisibleRows();
  int first = 0;
  if (selected >= visibleRows)
    first = selected - visibleRows + 1;
  int last = first + visibleRows;
  if (last > DiskMenuMatchCount)
    last = DiskMenuMatchCount;

  canvas.setBrushColor(Color::Black);
  canvas.clear();
  DrawVGAAlignmentMarkers();
  canvas.setPenColor(Color::BrightYellow);
  canvas.drawText(20, 15, "APPLE II DISK SELECTOR");
  canvas.setPenColor(Color::White);
  canvas.drawText(20, 30, "UP/DOWN ENTER TAB=MACHINE DEL=SAVE");

  canvas.setPenColor(Color::BrightCyan);
  char searchLine[48];
  int searchCharacters = (canvas.getWidth() - 20) / 8 - 9;
  if (searchCharacters < 1)
    searchCharacters = 1;
  snprintf(searchLine, sizeof(searchLine), "SEARCH: %.*s_", searchCharacters, DiskMenuSearch);
  canvas.drawText(20, 42, searchLine);

  int filenameCharacters = DiskMenuFilenameCharacters();

  for (int index = first; index < last; index++) {
    int diskIndex = DiskMenuMatches[index];
    int y = 60 + (index - first) * 12;
    if (index == selected) {
      canvas.setBrushColor(Color::BrightGreen);
      canvas.setPenColor(Color::Black);
    } else {
      canvas.setBrushColor(Color::Black);
      canvas.setPenColor(Color::White);
    }
    char line[70];
    char visibleName[68];
    FormatDiskMenuName(visibleName, sizeof(visibleName), DiskEntryName(diskIndex),
                       filenameCharacters, index == selected ? DiskMenuMarqueeOffset : -1);
    snprintf(line, sizeof(line), "%c %s", index == selected ? '>' : ' ', visibleName);
    canvas.drawText(20, y, line);
  }

  canvas.setBrushColor(Color::Black);
  canvas.setPenColor(Color::BrightCyan);
  char status[64];
  if (DiskMenuMatchCount)
    snprintf(status, sizeof(status), "MATCH %d OF %d   TOTAL %d", selected + 1, DiskMenuMatchCount, DiskMenuCount);
  else
    snprintf(status, sizeof(status), "NO MATCHES   TOTAL %d", DiskMenuCount);
  canvas.drawText(20, canvas.getHeight() - 14, status);
  canvas.setPenColor(IsIIeMode() ? Color::BrightGreen : Color::BrightYellow);
  canvas.drawText(canvas.getWidth() - 150, 15, MachineProfileName());
}

int SelectDiskImage() {
  auto keyboard = PS2Controller.keyboard();
  canvas.setOrigin(0, 0);
  int selected = 0;
  bool released = false;
  int exitScanCode = 0;
  bool confirmingSaveDelete = false;
  int saveDeleteDiskIndex = -1;
  DiskMenuSearch[0] = '\0';
  RebuildDiskMenuMatches();
  ResetDiskMenuMarquee();
  DrawDiskMenu(selected);

  for (;;) {
    if (!keyboard->scancodeAvailable()) {
      unsigned long now = millis();
      if ((long) (now - DiskMenuMarqueeResumeAt) >= 0 &&
          now - DiskMenuMarqueeLastStep >= 250) {
        DiskMenuMarqueeLastStep = now;
        DiskMenuMarqueeOffset++;
        DrawDiskMenuMarqueeRow(selected);
      }
      delay(10);
      continue;
    }
    int scanCode = keyboard->getNextScancode();
    if (scanCode == 0xE0) {
      continue;
    }
    if (scanCode == 0xF0) {
      released = true;
      continue;
    }
    if (released) {
      bool exitReleased = scanCode == exitScanCode;
      released = false;
      if (exitReleased)
        break;
      continue;
    }

    if (confirmingSaveDelete) {
      char confirmation = DiskMenuScancodeToCharacter(scanCode);
      if (confirmation == 'Y') {
        bool deleted = DeleteDiskSaveFile(saveDeleteDiskIndex);
        canvas.setBrushColor(Color::Black);
        canvas.clear();
        DrawVGAAlignmentMarkers();
        canvas.setPenColor(deleted ? Color::BrightGreen : Color::BrightRed);
        canvas.drawText(20, 88, deleted ? "SAVE FILE DELETED" : "SAVE DELETE FAILED");
        canvas.setPenColor(Color::White);
        canvas.drawText(20, 108, deleted ? "NEXT LOAD STARTS CLEAN" : "CHECK SERIAL LOG");
        delay(1000);
        confirmingSaveDelete = false;
        DrawDiskMenu(selected);
      } else if (confirmation == 'N' || scanCode == 0x76) {
        DEBUG_PRINTLN("[DISK] save deletion cancelled");
        confirmingSaveDelete = false;
        DrawDiskMenu(selected);
      }
      continue;
    }

    if (scanCode == 0x0D) {
      AppleMachineProfile next = MachineProfile;
      bool changed = false;
      for (int attempt = 0; attempt < 3 && !changed; attempt++) {
        next = next == APPLE_II_PLUS_64K ? APPLE_IIE_128K
             : next == APPLE_IIE_128K ? APPLE_IIE_ENHANCED_128K
             : APPLE_II_PLUS_64K;
        changed = SetMachineProfile(next);
      }
      if (!changed) {
        canvas.setPenColor(Color::BrightRed);
        canvas.drawText(20, canvas.getHeight() - 28, "NO USABLE IIE ROM IN /apple2/roms");
        delay(900);
      }
      DrawDiskMenu(selected);
    } else if (scanCode == 0x71 && DiskMenuMatchCount > 0) {
      saveDeleteDiskIndex = DiskMenuMatches[selected];
      char savePath[MAX_DISK_PATH + 16];
      if (!BuildDiskSavePath(DiskEntryPath(saveDeleteDiskIndex), savePath,
                             sizeof(savePath)) ||
          !DiskSaveFileExists(savePath)) {
        canvas.setPenColor(Color::BrightYellow);
        canvas.drawText(20, canvas.getHeight() - 28, "NO SAVE FILE FOR THIS DISK");
        DEBUG_PRINTF("[DISK] no writable save image to delete for %s\n",
                     DiskEntryPath(saveDeleteDiskIndex));
        delay(900);
        DrawDiskMenu(selected);
      } else {
        confirmingSaveDelete = true;
        DrawDeleteSaveConfirmation(saveDeleteDiskIndex);
      }
    } else if (scanCode == 0x75) {
      if (DiskMenuMatchCount) {
        selected = selected > 0 ? selected - 1 : DiskMenuMatchCount - 1;
        ResetDiskMenuMarquee();
        DrawDiskMenu(selected);
      }
    } else if (scanCode == 0x72) {
      if (DiskMenuMatchCount) {
        selected = selected + 1 < DiskMenuMatchCount ? selected + 1 : 0;
        ResetDiskMenuMarquee();
        DrawDiskMenu(selected);
      }
    } else if (scanCode == 0x5A && DiskMenuMatchCount > 0) {
      // Consume Enter's break sequence too, so it is not delivered as an
      // Apple II key after the selector closes.
      FlashSelectedDiskRow(selected);
      exitScanCode = scanCode;
    } else if (scanCode == 0x76) {
      if (DiskMenuSearch[0]) {
        DiskMenuSearch[0] = '\0';
        selected = 0;
        RebuildDiskMenuMatches();
        ResetDiskMenuMarquee();
        DrawDiskMenu(selected);
      } else {
        selected = 0;
        exitScanCode = scanCode;
      }
    } else if (scanCode == 0x66) {
      size_t length = strlen(DiskMenuSearch);
      if (length) {
        DiskMenuSearch[length - 1] = '\0';
        selected = 0;
        RebuildDiskMenuMatches();
        ResetDiskMenuMarquee();
        DrawDiskMenu(selected);
      }
    } else {
      char character = DiskMenuScancodeToCharacter(scanCode);
      size_t length = strlen(DiskMenuSearch);
      if (character && length + 1 < sizeof(DiskMenuSearch)) {
        DiskMenuSearch[length] = character;
        DiskMenuSearch[length + 1] = '\0';
        selected = 0;
        RebuildDiskMenuMatches();
        ResetDiskMenuMarquee();
        DrawDiskMenu(selected);
      }
    }
  }

  return DiskMenuMatchCount ? DiskMenuMatches[selected] : 0;
}

bool LoadBootDiskFromSD() {
  bool diskImageInPSRAM = false;
  DEBUG_PRINTLN("[MEM] Initializing onboard PSRAM for disk buffer...");
  if (esp_spiram_init() == ESP_OK) {
#ifndef BOARD_HAS_PSRAM
    esp_spiram_init_cache();
#endif
    DiskPSRAMReady = true;
    DEBUG_PRINTLN("[MEM] PSRAM initialized");
  } else {
    DEBUG_PRINTLN("[MEM] PSRAM unavailable; will try internal RAM");
  }

  DEBUG_PRINTLN("[SD] Initializing ESP32-SBC-FabGL SD card...");
  if (!fabgl::FileBrowser::mountSDCard(false, DISK_MOUNT_PATH, 2)) {
    snprintf(DiskLoadError, sizeof(DiskLoadError), "SD initialization failed");
    DEBUG_PRINTF("[SD] ERROR: %s\n", DiskLoadError);
    return false;
  }

  FindDiskImages();
  InitializeMachineProfiles();
  if (DiskMenuCount == 0) {
    snprintf(DiskLoadError, sizeof(DiskLoadError), "Missing/invalid apple2-index.txt");
    DEBUG_PRINTF("[SD] ERROR: %s\n", DiskLoadError);
    fabgl::FileBrowser::unmountSDCard();
    return false;
  }

  // Bootable games must run their own startup/loader from drive 1. The runtime
  // selector is for attaching their data or second disk to drive 2.
  int selected = SelectDiskImage();

  DrawDiskLoadingStatus(0, DiskEntryPath(selected));
  DEBUG_PRINTF("[SD] Selected %s\n", DiskEntryPath(selected));
  const char * bootImagePath = ConfigureDriveSavePath(0, DiskEntryPath(selected));
  FILE * imageFile = fopen(bootImagePath, "rb");
  if (!imageFile) {
    snprintf(DiskLoadError, sizeof(DiskLoadError), "Cannot open selected disk");
    DEBUG_PRINTF("[SD] ERROR: %s\n", DiskLoadError);
    fabgl::FileBrowser::unmountSDCard();
    return false;
  }

  fseek(imageFile, 0, SEEK_END);
  long imageSize = ftell(imageFile);
  rewind(imageFile);
  DEBUG_PRINTF("[SD] Image size: %ld bytes\n", imageSize);
  if (imageSize != (long) DISK_IMAGE_SIZE) {
    snprintf(DiskLoadError, sizeof(DiskLoadError), "Invalid image size: %ld (need %u)", imageSize, (unsigned) DISK_IMAGE_SIZE);
    DEBUG_PRINTF("[SD] ERROR: %s\n", DiskLoadError);
    fclose(imageFile);
    fabgl::FileBrowser::unmountSDCard();
    return false;
  }

  if (DiskPSRAMReady) {
    // Olimex FabGL initializes PSRAM at runtime with the IDE PSRAM option
    // disabled, then uses its address directly instead of the heap allocator.
    DiskImage = (unsigned char *) SOC_EXTRAM_DATA_LOW;
    diskImageInPSRAM = true;
    DEBUG_PRINTLN("[MEM] Disk buffer assigned to onboard PSRAM");
  }
  if (!DiskImage) {
    DEBUG_PRINTF("[MEM] Trying internal RAM: %u bytes free, largest block %u bytes\n",
                 ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    DiskImage = (unsigned char *) malloc(DISK_IMAGE_SIZE);
  }
  if (!DiskImage) {
    snprintf(DiskLoadError, sizeof(DiskLoadError), "Cannot allocate %u bytes for disk", (unsigned) DISK_IMAGE_SIZE);
    DEBUG_PRINTF("[SD] ERROR: %s\n", DiskLoadError);
    fclose(imageFile);
    fabgl::FileBrowser::unmountSDCard();
    return false;
  }

  uint32_t hostTransaction = BeginDiskHostIO("boot-image-load", 0, 0,
                                             DISK_IMAGE_SIZE);
  size_t bytesRead = ReadFileThroughInternalBuffer(imageFile, DiskImage,
                                                   DISK_IMAGE_SIZE, "boot disk");
  fclose(imageFile);
  EndDiskHostIO(hostTransaction, bytesRead == DISK_IMAGE_SIZE);
  if (bytesRead != DISK_IMAGE_SIZE) {
    snprintf(DiskLoadError, sizeof(DiskLoadError), "Short read: %u of %u bytes", (unsigned) bytesRead, (unsigned) DISK_IMAGE_SIZE);
    DEBUG_PRINTF("[SD] ERROR: %s\n", DiskLoadError);
    if (!diskImageInPSRAM)
      free(DiskImage);
    DiskImage = NULL;
    return false;
  }

  // A size check alone cannot prove that the expected image reached RAM.
  // Print a compact fingerprint and a few useful filesystem locations so a
  // failing image can be compared with the original file on a computer.
  uint32_t fingerprint = 2166136261UL; // FNV-1a 32-bit
  for (size_t index = 0; index < DISK_IMAGE_SIZE; index++) {
    fingerprint ^= DiskImage[index];
    fingerprint *= 16777619UL;
  }
  DEBUG_PRINTF("[SD] Image fingerprint FNV1a=%08lX\n", (unsigned long) fingerprint);
  DEBUG_PRINT("[SD] Track 0 sector 0:");
  for (int index = 0; index < 16; index++)
    DEBUG_PRINTF(" %02X", DiskImage[index]);
  DEBUG_PRINTLN();
  const size_t dosVTOCOffset = 17UL * 4096UL;
  DEBUG_PRINT("[SD] Track 17 physical sector 0:");
  for (int index = 0; index < 16; index++)
    DEBUG_PRINTF(" %02X", DiskImage[dosVTOCOffset + index]);
  DEBUG_PRINTLN();

  DiskLoadError[0] = '\0';
  snprintf(LoadedDiskName, sizeof(LoadedDiskName), "%s", DiskEntryName(selected));
  DriveDiskImage[0] = DiskImage;
  DiskMountedBuffer[0] = DiskImage;
  DriveDiskImageHeapAllocated[0] = !diskImageInPSRAM;
  DEBUG_PRINTF("[SD] Loaded %s into PSRAM (%u bytes)\n", LoadedDiskName, (unsigned) DISK_IMAGE_SIZE);
  return true;
}

/* applemu incorrectly uses 0x1a00 length tracks. A real
   Apple disk drive spinning at ~300RPM, or 1 rev every
   200ms and writing a byte every 32us has a capacity
   of 6250 bytes/track not 6656. Setting TrackBufLen to
   0x1a00 allows my disk emulation to read applemu disks,
   and the extra bytes that are available on every
   track can confuse formatting programs. The ProDOS FILER
   is one of these. TrackBufLen must remain 6250 for correct
   300 RPM timing and ProDOS FILER compatibility. */
int TrackBufLen = 6250;

int DiskSlot;

enum DiskTypes {
  UnknownType = 0,
    RawType = 1,
    DOSType = 2,
    ProDOSType = 3,
    SimsysType = 4,
    XgsType = 5
};

struct DriveState {
  /* variables for each disk */
  char DiskFN[80]; /* MS-DOS filename of disk image */
  int DiskFH; /* MS-DOS file handle of disk image */
  long DiskSize; /* length of disk image file in bytes */
  enum DiskTypes DiskType; /* Type of disk image */
  int WritePro; /* 0:write-enabled, !0:write-protected */
  int TrkBufChanged; /* Track buffer has changed */
  int TrkBufOld; /* Data in track buffer needs updating before I/O */
  int ShouldRecal;
  /* variables used during emulation */
  int Track; /* half-track position, 0-68 for a 35-track .dsk */
  int Phase; /* 0- 3 */
  int ReadWP; /* 0/1  */
  int Active; /* 0/1  */
  int Writing; /* 0/1  */
  unsigned char *DiskBuffer; /* host-visible disk buffer for this drive */
}
DrvSt[2];
/* I'll keep what Gregory-kun uses.  So I don't like using low-level file
   access, but it'll be so much easier to tweak the code. -uso. */

int CurDrv;
byte DataLatch;

int WriteAccess = 0;
/* Flag indicates that the hardware I/O memory location
						was written to. Use during writing data. */

unsigned char TrackBuffer[0x1a00];
unsigned int TrkBufIdx;
unsigned int SeekPos;

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
long lseekDisk(int handle, int newPos, int flags) {
  long retPos = 0;
  switch (flags) {
  case SEEK_SET:
    retPos = newPos;
    break; /* seek to an absolute position */
  case SEEK_CUR:
    retPos = SeekPos + newPos;
    break; /* seek relative to current position */
  case SEEK_END:
    retPos = DISK_IMAGE_SIZE + newPos;
    break; /* seek relative to end of file */
  }
  SeekPos = retPos;
  return retPos;
}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
int openDisk(const char * Path, int flags) {
  return 1;
}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
bool LoadDiskImageForDrive(int drive, const char * path) {
  if (drive < 0 || drive > 1 || !path || !*path) {
    snprintf(DiskLoadError, sizeof(DiskLoadError), "Invalid drive or image path");
    DEBUG_PRINTF("[SD] ERROR: %s\n", DiskLoadError);
    return false;
  }

  const char * imagePath = ConfigureDriveSavePath(drive, path);
  FILE * imageFile = fopen(imagePath, "rb");
  if (!imageFile) {
    snprintf(DiskLoadError, sizeof(DiskLoadError), "Cannot open drive %d image", drive + 1);
    DEBUG_PRINTF("[SD] ERROR: %s: %s\n", DiskLoadError, path);
    return false;
  }

  fseek(imageFile, 0, SEEK_END);
  long imageSize = ftell(imageFile);
  rewind(imageFile);
  if (imageSize != (long) DISK_IMAGE_SIZE) {
    snprintf(DiskLoadError, sizeof(DiskLoadError), "Invalid image size: %ld", imageSize);
    DEBUG_PRINTF("[SD] ERROR: %s (need %u)\n", DiskLoadError, (unsigned) DISK_IMAGE_SIZE);
    fclose(imageFile);
    return false;
  }

  unsigned char * payload = NULL;
  bool payloadHeapAllocated = false;
  if (DiskPSRAMReady) {
    // The board initializes PSRAM manually, so it is not part of the Arduino
    // heap. Reserve one non-overlapping image-sized region per drive.
    payload = (unsigned char *) SOC_EXTRAM_DATA_LOW + drive * DISK_IMAGE_SIZE;
    DEBUG_PRINTF("[MEM] Drive %d disk buffer assigned to PSRAM at %p\n", drive + 1, payload);
  } else {
    DEBUG_PRINTF("[MEM] Drive %d trying internal RAM: %u bytes free, largest block %u bytes\n",
                 drive + 1, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    payload = (unsigned char *) malloc(DISK_IMAGE_SIZE);
    payloadHeapAllocated = payload != NULL;
  }
  if (!payload) {
    snprintf(DiskLoadError, sizeof(DiskLoadError), "Cannot allocate drive %d disk buffer", drive + 1);
    DEBUG_PRINTF("[SD] ERROR: %s\n", DiskLoadError);
    fclose(imageFile);
    return false;
  }

  uint32_t hostTransaction = BeginDiskHostIO("image-load", drive, 0,
                                             DISK_IMAGE_SIZE);
  size_t bytesRead = ReadFileThroughInternalBuffer(imageFile, payload,
                                                   DISK_IMAGE_SIZE, "swapped disk");
  fclose(imageFile);
  EndDiskHostIO(hostTransaction, bytesRead == DISK_IMAGE_SIZE);

  if (bytesRead != DISK_IMAGE_SIZE) {
    if (payloadHeapAllocated)
      free(payload);
    snprintf(DiskLoadError, sizeof(DiskLoadError), "Drive %d short read: %u of %u",
             drive + 1, (unsigned) bytesRead, (unsigned) DISK_IMAGE_SIZE);
    DEBUG_PRINTF("[SD] ERROR: %s\n", DiskLoadError);
    return false;
  }

  if (DriveDiskImage[drive] && DriveDiskImageHeapAllocated[drive]) {
    free(DriveDiskImage[drive]);
  }

  DriveDiskImage[drive] = payload;
  DriveDiskImageHeapAllocated[drive] = payloadHeapAllocated;
  DrvSt[drive].DiskBuffer = payload;
  DiskMountedBuffer[drive] = payload;
  DrvSt[drive].DiskSize = DISK_IMAGE_SIZE;
  DrvSt[drive].DiskType = UnknownType;
  DiskAutoID(&DrvSt[drive]);
  DrvSt[drive].WritePro = DriveSavePath[drive][0] ? 0 : 1;
  if (DrvSt[drive].Track > MAX_DISK_HALF_TRACK)
    DrvSt[drive].Track = MAX_DISK_HALF_TRACK;
  // Force the emulated drive to rebuild its nibblized track from the new
  // image.  This matters when the swapped drive was already selected: the
  // controller's shared TrackBuffer may still contain bytes from the old disk.
  DrvSt[drive].TrkBufOld = 1;
  DrvSt[drive].TrkBufChanged = 0;
  DrvSt[drive].ShouldRecal = 1;
  DiskLoadError[0] = '\0';
  return true;
}

void readSector(int drvAtivo, void * buf, size_t size) {
  unsigned char *driveBuffer = NULL;
  if (drvAtivo >= 0 && drvAtivo < 2) {
    driveBuffer = DrvSt[drvAtivo].DiskBuffer;
  } else {
    // Retain the legacy fallback only for callers that do not identify a
    // drive.  An empty D2 must not silently expose D1's image.
    driveBuffer = DiskImage;
  }

  bool intentionallyEmpty = drvAtivo >= 0 && drvAtivo < 2 &&
                            !DrvSt[drvAtivo].DiskBuffer &&
                            !DiskMountedBuffer[drvAtivo];
  if (intentionallyEmpty) {
    // No media produces no address/data prologues. Keep the raw track at its
    // erased $FF state rather than synthesizing a formatted zero-filled disk.
    memset(buf, 0xFF, size);
  } else if (driveBuffer && SeekPos <= DISK_IMAGE_SIZE && size <= DISK_IMAGE_SIZE - SeekPos) {
    memcpy(buf, driveBuffer + SeekPos, size);
  } else {
    memset(buf, 0, size);
    if (!DiskBufferCorruptionReported) {
      DiskBufferCorruptionReported = true;
      DEBUG_PRINTF("[DISK-CORRUPTION] FIRST invalid read driveArg=%d curDrive=%d "
                   "offset=%u size=%u PC=%04X opcode=%02X heartbeat=%lu\n",
                   drvAtivo + 1, CurDrv + 1, SeekPos, (unsigned) size,
                   CPUInstructionStartPC, CPUInstructionOpcode,
                   (unsigned long) CPUInstructionHeartbeat);
      DEBUG_PRINTF("[DISK-CORRUPTION] pointers selected=%p mounted=%p "
                   "drv0=%p drv1=%p owner0=%p owner1=%p legacy=%p\n",
                   driveBuffer,
                   (drvAtivo >= 0 && drvAtivo < 2) ? DiskMountedBuffer[drvAtivo] : NULL,
                   DrvSt[0].DiskBuffer, DrvSt[1].DiskBuffer,
                   DriveDiskImage[0], DriveDiskImage[1], DiskImage);
      DEBUG_PRINTF("[DISK-CORRUPTION] state drive0 type=%d size=%ld wp=%d "
                   "track=%d phase=%d active=%d old=%d dirty=%d; "
                   "drive1 type=%d size=%ld wp=%d track=%d\n",
                   (int) DrvSt[0].DiskType, DrvSt[0].DiskSize,
                   DrvSt[0].WritePro, DrvSt[0].Track, DrvSt[0].Phase,
                   DrvSt[0].Active, DrvSt[0].TrkBufOld,
                   DrvSt[0].TrkBufChanged, (int) DrvSt[1].DiskType,
                   DrvSt[1].DiskSize, DrvSt[1].WritePro, DrvSt[1].Track);
      DEBUG_PRINT("[DISK-CORRUPTION] recent CPU:");
      for (int historyOffset = 0; historyOffset < 16; historyOffset++) {
        unsigned char historyIndex = (CPURecentIndex + historyOffset) & 0x0F;
        DEBUG_PRINTF(" %04X:%02X@%04X", CPURecentPC[historyIndex],
                     CPURecentOpcode[historyIndex],
                     CPURecentArgument[historyIndex]);
      }
      DEBUG_PRINTLN();
    }
    DEBUG_PRINTF("[DISK] ERROR: invalid read drive=%d at %u (%u bytes) "
                 "buffer=%p expected=%p\n", drvAtivo + 1, SeekPos,
                 (unsigned) size, driveBuffer,
                 (drvAtivo >= 0 && drvAtivo < 2) ? DiskMountedBuffer[drvAtivo] : NULL);
  }

}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
void writeSector(int drvAtivo, void * buf, size_t size) {
  if (drvAtivo < 0 || drvAtivo > 1 || DrvSt[drvAtivo].WritePro ||
      !DrvSt[drvAtivo].DiskBuffer || !DriveSavePath[drvAtivo][0] ||
      SeekPos > DISK_IMAGE_SIZE || size > DISK_IMAGE_SIZE - SeekPos)
    return;

  unsigned char * driveBuffer = DrvSt[drvAtivo].DiskBuffer;
  memcpy(driveBuffer + SeekPos, buf, size);
}

static bool PersistDriveRange(int drive, size_t offset, size_t size) {
  if (drive < 0 || drive > 1 || DrvSt[drive].WritePro ||
      !DrvSt[drive].DiskBuffer || !DriveSavePath[drive][0] ||
      offset > DISK_IMAGE_SIZE || size > DISK_IMAGE_SIZE - offset)
    return false;

  uint32_t hostTransaction = BeginDiskHostIO("sector-persist", drive,
                                             offset, size);
  FILE * save = fopen(DriveSavePath[drive], "r+b");
  if (!save) {
    DrvSt[drive].WritePro = 1;
    DEBUG_PRINTF("[DISK] ERROR opening save image; drive %d is now write-protected: %s\n",
                 drive + 1, DriveSavePath[drive]);
    EndDiskHostIO(hostTransaction, false);
    return false;
  }

  bool persisted = fseek(save, offset, SEEK_SET) == 0 &&
                   fwrite(DrvSt[drive].DiskBuffer + offset, 1, size, save) == size;
  fflush(save);
  fclose(save);
  if (!persisted) {
    DrvSt[drive].WritePro = 1;
    DEBUG_PRINTF("[DISK] ERROR persisting save; drive %d is now write-protected: %s\n",
                 drive + 1, DriveSavePath[drive]);
  }
  DEBUG_PRINTF("[DISK-SAVE] transaction=%lu drive=%d track=%u offset=%u "
               "size=%u success=%d\n",
               (unsigned long) hostTransaction, drive + 1,
               (unsigned) (offset / 4096U), (unsigned) offset,
               (unsigned) size, persisted);
  EndDiskHostIO(hostTransaction, persisted);
  return persisted;
}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
long FileSize(int filehandle) {
  long filelen;
  filelen = DISK_IMAGE_SIZE;
  //printf("file, filelen:%d\n",filelen);
  lseekDisk(filehandle, 0L, SEEK_SET);
  return filelen;
}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
#ifdef SAFE
#else
unsigned int GetAttrib(char * filename) {
  /*    int ds; ds=_DS;
  	_DS = FP_SEG( filename );
      _DX = FP_OFF( filename );
      _AX = 0x4300;
      geninterrupt(0x21);
      _DS=ds;
      if (_FLAGS&1) return 0xffff;
  	return _CX;
  	*/
  return 0;
}
#endif

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
/* Make sure the track buffer index (given by idx) is within the correct
   range. This function should be called before every time you access the
   TrackBuffer. */
void RangeCheckTBI(unsigned int * idx) {
  while ( * idx >= TrackBufLen) {
    * idx -= TrackBufLen;
  }
}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
/* 6&2 data translation table
   from Peter Koch's emulator
   as in Andrew Gregory's emulator */
unsigned char translate[ 256 ] =
{
	  0x96, 0x97, 0x9a, 0x9b, 0x9d, 0x9e, 0x9f, 0xa6,
	  0xa7, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb2, 0xb3,
	  0xb4, 0xb5, 0xb6, 0xb7, 0xb9, 0xba, 0xbb, 0xbc,
	  0xbd, 0xbe, 0xbf, 0xcb, 0xcd, 0xce, 0xcf, 0xd3,
	  0xd6, 0xd7, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde,
	  0xdf, 0xe5, 0xe6, 0xe7, 0xe9, 0xea, 0xeb, 0xec,
	  0xed, 0xee, 0xef, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6,
	  0xf7, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
	  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
	  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
	  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
	  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
	  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
	  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
	  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00, 0x01,
    0x80, 0x80, 0x02, 0x03, 0x80, 0x04, 0x05, 0x06,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x07, 0x08,
    0x80, 0x80, 0x80, 0x09, 0x0a, 0x0b, 0x0c, 0x0d,
    0x80, 0x80, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13,
    0x80, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x1b, 0x80, 0x1c, 0x1d, 0x1e,
    0x80, 0x80, 0x80, 0x1f, 0x80, 0x80, 0x20, 0x21,
    0x80, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x29, 0x2a, 0x2b,
    0x80, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32,
	  0x80, 0x80, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
    0x80, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f
};

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
void GotoHardSector(struct DriveState * ds, int sector) {
  /* from DOS 3.3 Sector column in above table */
	/* from DOS 3.3 Sector column in above table */
	int DOS33Skew[16]={0, 7, 14, 6, 13, 5, 12, 4, 11, 3, 10, 2, 9, 1, 8, 15};
	int ProDOSSkew[ 16 ] = { 0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15 };

  lseekDisk(ds -> DiskFH, (long)(ds -> Track >> 1) * 4096L, SEEK_SET);
  if (ds -> DiskType == DOSType) {
    lseekDisk(ds -> DiskFH, 256L * (long) DOS33Skew[sector], SEEK_CUR);
  }
  if (ds -> DiskType == ProDOSType) {
    lseekDisk(ds -> DiskFH, 256L * (long) ProDOSSkew[sector], SEEK_CUR);
  }
  if (ds -> DiskType == SimsysType) {
    lseekDisk(ds -> DiskFH, 256L * (long) ProDOSSkew[sector] + 30, SEEK_CUR);
  }
  if (ds -> DiskType == XgsType) /* currently only PO 2MG supported */ {
    lseekDisk(ds -> DiskFH, 256L * (long) ProDOSSkew[sector] + 64, SEEK_CUR);
  }
#if ENABLE_DISK_SECTOR_TRACE
  if (DiskReadTraceCount < 24) {
    DEBUG_PRINTF("[DISK] sector read #%u time=%lums drive=%d track=%d logical=%d offset=%u type=%d\n",
                 DiskReadTraceCount + 1, millis(),
                 CurDrv + 1, ds -> Track >> 1,
                 sector, SeekPos, (int) ds -> DiskType);
    DiskReadTraceCount++;
  }
#endif
}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
/* find and read a given hard sector given the disk format type */
/* only for .DO type files */
void ReadHardSector(struct DriveState * ds, int sector, unsigned char * buf) {
  GotoHardSector(ds, sector);
  readSector((int) (ds - DrvSt), buf, 256);
}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
/* find and write a given hard sector given the disk format type */
/* only for .DO type files */
void WriteHardSector(struct DriveState * ds, int sector, char * buf) {
  GotoHardSector(ds, sector);
  writeSector((int) (ds - DrvSt), buf, 256);
}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
/* Sector=0..15 */
void NibbliseSector(unsigned char * data, unsigned char ** trackbufptr,
  int volume, int track, int sector) {
  /* Define a template for all disk sectors */
	static unsigned char disktemplate[ 40 ] =
    {
        // Gap 3 / Gap 1 between sectors. A realistic gap distribution is
        // important to loaders that use rotational sector timing.
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, /*  0-15 */
        0xd5, 0xaa, 0x96,                                  /* 16-18 address prologue */
        0, 0, 0, 0, 0, 0, 0, 0,                            /* 19-26 address */
        0xde, 0xaa, 0xeb,                                  /* 27-29 address epilogue */
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,          /* 30-36 Gap 2 */
        0xd5, 0xaa, 0xad                                   /* 37-39 data prologue */
    };

  int diskbyte, checksum, v;
  unsigned char * trackbuf;

  trackbuf = * trackbufptr;

  /* fill in address in template */
  checksum = volume ^ track ^ sector;
  disktemplate[19] = (volume >> 1) | 0xaa;
  disktemplate[20] = volume | 0xaa;
  disktemplate[21] = (track >> 1) | 0xaa;
  disktemplate[22] = track | 0xaa;
  disktemplate[23] = (sector >> 1) | 0xaa;
  disktemplate[24] = sector | 0xaa;
  disktemplate[25] = (checksum >> 1) | 0xaa;
  disktemplate[26] = checksum | 0xaa;

  /* template */
  for (diskbyte = 0; diskbyte < 40; diskbyte++) {
    * trackbuf++ = disktemplate[diskbyte];
  }
  /* data */
  checksum = 0;
  for (diskbyte = 0; diskbyte < 0x156; diskbyte++) {
    v = (diskbyte >= 0x56) ?
      /* get 6-bit byte */
      data[diskbyte - 0x56] >> 2 :
      /* build 6-bit byte from 3 x 2 bits */
      ((data[diskbyte] & 0x02) >> 1) |
      ((data[diskbyte] & 0x01) << 1) |
      ((data[diskbyte + 0x56] & 0x02) << 1) |
      ((data[diskbyte + 0x56] & 0x01) << 3) |
      ((data[diskbyte + 0xac] & 0x02) << 3) |
      ((data[diskbyte + 0xac] & 0x01) << 5);
    * trackbuf++ = translate[(checksum ^ v) & 0x3f];
    checksum = v;
  }

  /* data checksum */
  * trackbuf++ = translate[checksum & 0x3f];
  /* data trailer */
  * trackbuf++ = 0xde;
  * trackbuf++ = 0xaa;
  * trackbuf++ = 0xeb;

  * trackbufptr = trackbuf;
}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
/* Nibblise Track: Convert from DOS 3.3 sector-level data into a raw Disk ][
   track-level byte stream */
void NibbliseTrack(struct DriveState * ds) {
  unsigned char SectorBuffer[258];
  unsigned char * TrackBufPtr = TrackBuffer;
  int idx;

  /* zero-out unused buffer space */
  SectorBuffer[256] = 0;
  SectorBuffer[257] = 0;
  /* encode each hard sector */
  for (idx = 0; idx < 16; idx++) {
    ReadHardSector(ds, idx, SectorBuffer);
    NibbliseSector(SectorBuffer, & TrackBufPtr, 254, ds -> Track >> 1, idx);
  }
}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
void ReadTrack(struct DriveState * ds) {
  int idx;
  unsigned long trackBuildStartedAt = micros();
  bool traceTrackBuild = DiskTrackTraceCount < 12;

  if (traceTrackBuild) {
    DEBUG_PRINTF("[DISK] building track #%u time=%lums drive=%d track=%d type=%d\n",
                 DiskTrackTraceCount + 1, millis(), CurDrv + 1, ds -> Track >> 1,
                 (int) ds -> DiskType);
    DiskTrackTraceCount++;
  }

  /* Make sure that any unused part of the buffer has 0xff's in it */
  for (idx = 0; idx < 0x1a00; idx++) {
    TrackBuffer[idx] = 0xff;
  }
  if (!ds -> DiskBuffer) {
    // An empty drive has no sector headers to nibblize. Leaving the raw track
    // as $FF lets software time out/probe write protection as it would with
    // no inserted disk, without fabricating readable sectors.
    ds -> TrkBufChanged = 0;
    ds -> TrkBufOld = 0;
    ds -> ShouldRecal = 0;
    if (traceTrackBuild) {
      DEBUG_PRINTF("[DISK] empty drive=%d track=%d ready as no-media stream\n",
                   (int) (ds - DrvSt) + 1, ds -> Track >> 1);
    }
    return;
  }
  if (ds -> DiskType == RawType) {
    /* Disk ][ track/byte format (0x1a00 bytes/track) - just read the bytes */
    lseekDisk(ds -> DiskFH, (long)(ds -> Track >> 1) * 0x1a00L, SEEK_SET);
    readSector((int) (ds - DrvSt), TrackBuffer, 0x1a00);
  }
  if (ds -> DiskType == DOSType || ds -> DiskType == ProDOSType ||
    ds -> DiskType == SimsysType || ds -> DiskType == XgsType) {
    /* Track/sector format (4096 bytes/track) - translate to Disk ][ format */
    NibbliseTrack(ds);
  }
  /* new track - so clear changed flag */
  ds -> TrkBufChanged = 0;
  ds -> TrkBufOld = 0;
  ds -> ShouldRecal = 0;
  if (traceTrackBuild) {
    DEBUG_PRINTF("[DISK] track ready drive=%d track=%d build=%luus\n",
                 CurDrv + 1, ds -> Track >> 1, micros() - trackBuildStartedAt);
  }
}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
/* return character offset bytes from idx. Wrap around the end of the buffer
   if required */
unsigned char GetByte(unsigned int idx, unsigned int offset) {
  unsigned int i = idx + offset;
  RangeCheckTBI( & i);
  return TrackBuffer[i];
}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
void DeNibbliseData(int idx, char * SectorBuffer) {
  unsigned char data, v;
  int diskbyte;

  /* idx is the offset of the start of the disk data */
  v = 0;
  for (diskbyte = 0; diskbyte < 0x156; diskbyte++) {
    data = translate[GetByte(idx, diskbyte)];
    if (diskbyte < 0x56) {
      /* turn 6 bits into 3 lots of 2 bits */
      data ^= v;
      SectorBuffer[diskbyte] = (data & 0x01) << 1;
      SectorBuffer[diskbyte] |= (data & 0x02) >> 1;
      SectorBuffer[diskbyte + 0x56] = (data & 0x04) >> 1;
      SectorBuffer[diskbyte + 0x56] |= (data & 0x08) >> 3;
      SectorBuffer[diskbyte + 0xac] = (data & 0x10) >> 3;
      SectorBuffer[diskbyte + 0xac] |= (data & 0x20) >> 5;
      v = data;
    } else {
      /* get 6 more bits */
      data ^= v;
      SectorBuffer[diskbyte - 0x56] |= (data << 2) & 0xfc;
      v = data;
    }
  }
}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
/* De-Nibblise Track: Convert from raw Disk ][ disk byte stream into DOS 3.3
   compatible track/sector-level data */
void DeNibbliseTrack(struct DriveState * ds) {
  unsigned int idx; /* index into track buffer */
  unsigned int start; /* index to start of track data */
  unsigned int rotations; /* # times the disk has 'rotated' */
  int volume, track, sector, chksum, Done;
  char SectorBuffer[258];

  start = 0xffff;
  idx = 0;
  /* keep searching until idx == start */
  Done = 0;
  rotations = 0;
  while (!Done) {
    if (idx >= TrackBufLen) {
      rotations++;
    }
    if (GetByte(idx, 0) == 0xd5 &&
      GetByte(idx, 1) == 0xaa &&
      GetByte(idx, 2) == 0x96) {
      /* Found address header */
      volume = (((GetByte(idx, 3) << 1) & 0xff) | 0x55) & GetByte(idx, 4);
      track = (((GetByte(idx, 5) << 1) & 0xff) | 0x55) & GetByte(idx, 6);
      sector = (((GetByte(idx, 7) << 1) & 0xff) | 0x55) & GetByte(idx, 8);
      chksum = (((GetByte(idx, 9) << 1) & 0xff) | 0x55) & GetByte(idx, 10);
      if (chksum == (volume ^ track ^ sector)) {
        /* checksum is valid */
        /* NOTE: DOS 3.3 and ProDOS chop off the last byte (0xeb)
           before it is written. A proper trailer should be DE AA EB
           instead only DE AA is written. */
        if (GetByte(idx, 11) == 0xde &&
          GetByte(idx, 12) == 0xaa) {
          /* valid address trailer */
          /* skip over self-sync bytes */
          idx += 14;
          if (idx >= TrackBufLen) {
            rotations++;
          }
          RangeCheckTBI( & idx);
          while (GetByte(idx, 0) != 0xd5 && idx < TrackBufLen) {
            idx++;
          }
          /* check for data header */
          if (GetByte(idx, 0) == 0xd5 &&
            GetByte(idx, 1) == 0xaa &&
            GetByte(idx, 2) == 0xad) {
            /* valid data header */
            idx += 3;
            if (idx >= TrackBufLen) {
              rotations++;
            }
            RangeCheckTBI( & idx);
            if (idx == start) {
              Done = 1;
              continue;
            }
            if (start == 0xffff) {
              start = idx;
            }
            /* get, decode and write data */
            DeNibbliseData(idx, SectorBuffer);
            WriteHardSector(ds, sector, SectorBuffer);
          }
        }
      }
    }
    idx++;
    /* Check timeout */
    if (rotations > 32) {
      Done = 1;
    }
  }
}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
void WriteTrack(struct DriveState * ds) {
  int idx;
  int drive = (int) (ds - DrvSt);
  uint32_t hostTransaction = BeginDiskHostIO("track-flush", drive,
    (size_t) (ds -> Track >> 1) *
      (ds -> DiskType == RawType ? 0x1a00U : 4096U),
    ds -> DiskType == RawType ? 0x1a00U : 4096U);
  unsigned long flushStartedAt = millis();
  bool persisted = true;

  /* fill any unused space in buffer with 0xff's */
  if (TrackBufLen < 0x1a00) {
    for (idx = TrackBufLen; idx < 0x1a00; idx++) {
      TrackBuffer[idx] = 0xff;
    }
  }

  if (ds -> DiskType == RawType) {
    /* Disk ][ track/byte format (0x1a00 bytes/track) - just write the bytes */
    lseekDisk(ds -> DiskFH, (long)(ds -> Track >> 1) * 0x1a00L, SEEK_SET);
    writeSector((int) (ds - DrvSt), TrackBuffer, 0x1a00);
    persisted = PersistDriveRange(drive,
      (size_t) (ds -> Track >> 1) * 0x1a00, 0x1a00);
  }
  if (ds -> DiskType == DOSType || ds -> DiskType == ProDOSType ||
    ds -> DiskType == SimsysType || ds -> DiskType == XgsType) {
    /* Track/sector format (4096 bytes/track) - translate from Disk ][ format */
    DeNibbliseTrack(ds);
    persisted = PersistDriveRange(drive,
      (size_t) (ds -> Track >> 1) * 4096, 4096);
  }
  ds -> DiskSize = FileSize(ds -> DiskFH);
  if (ds -> DiskSize < 143360) ds -> DiskSize = 143360; // uso. 2002.1109
  /* track has been saved - so clear changed flag */
  ds -> TrkBufChanged = 0;
  unsigned long flushElapsed = EndDiskHostIO(hostTransaction, persisted);
  DEBUG_PRINTF("[DISK-FLUSH] transaction=%lu writeSession=%lu writeBytes=%u "
               "drive=%d track=%d type=%d success=%d elapsed=%lums "
               "dirtyAfter=%d\n",
               (unsigned long) hostTransaction,
               (unsigned long) DiskWriteSession, DiskWriteSessionBytes,
               drive + 1, ds -> Track >> 1, (int) ds -> DiskType, persisted,
               millis() - flushStartedAt, ds -> TrkBufChanged);
  if (Task1 && xTaskGetCurrentTaskHandle() == Task1) {
    DiskFlushResumeTransaction = hostTransaction;
    DiskFlushResumeElapsed = flushElapsed;
    DiskFlushResumePending = true;
  }
}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
unsigned long LastIO = 0;
unsigned long Diff, LeftOverCycles;
static unsigned long LastRotationCycle = 0;
static unsigned long RotationCycleRemainder = 0;

void PrintDiskRuntimeState() {
  struct DriveState * ds = &DrvSt[CurDrv];
  DEBUG_PRINTF(" disk=D%d motor=%d track=%d idx=%u old=%d q7write=%d",
               CurDrv + 1, ds->Active, ds->Track, TrkBufIdx,
               ds->TrkBufOld, ds->Writing);
}

#if ENABLE_DISK_DIAGNOSTICS
static void RecordDiskControllerAccess(word address, const DriveState * ds) {
  unsigned int slot = DiskLoaderTraceIndex++ % DISK_LOADER_TRACE_SIZE;
  DiskLoaderTraceEvent * event = &DiskLoaderTrace[slot];
  uint32_t accessCycle = cycle;
  uint32_t delta = accessCycle - DiskLoaderLastAccessCycle;
  DiskLoaderLastAccessCycle = accessCycle;
  event->cycle = accessCycle;
  event->pc = CPUInstructionStartPC;
  event->index = TrkBufIdx;
  event->delta = delta > 0xFFFFUL ? 0xFFFF : (uint16_t) delta;
  event->address = address & 0x0F;
  event->latch = DataLatch;
  event->track = ds->Track;
  event->flags = (ds->ReadWP ? 0x01 : 0) |
                 (ds->Writing ? 0x02 : 0) |
                 (ds->Active ? 0x04 : 0) |
                 (WriteAccess ? 0x08 : 0) |
                 (DataLatch & 0x80 ? 0x10 : 0);

  // C0EC is the normal Q6-low data-latch read used by custom loaders.
  if (!WriteAccess && (address & 0x0F) == 0x0C &&
      !ds->Writing && !ds->ReadWP)
    DiskLoaderReadCount++;
  if (WriteAccess || ds->Writing)
    DiskLoaderWriteCount++;
}

static void DumpDiskLoaderSnapshot(uint32_t readsSinceCheck) {
  DriveState * ds = &DrvSt[CurDrv];
  DEBUG_PRINTF("[DISK-SEARCH] PROLONGED loader search drive=%d track=%d "
               "readsWindow=%lu readsTotal=%lu writesTotal=%lu PC=%04X "
               "idx=%u latch=%02X q6=%d q7=%d motor=%d cycle=%lu\n",
               CurDrv + 1, ds->Track, (unsigned long) readsSinceCheck,
               (unsigned long) DiskLoaderReadCount,
               (unsigned long) DiskLoaderWriteCount, CPUInstructionStartPC,
               TrkBufIdx, DataLatch, ds->ReadWP, ds->Writing, ds->Active,
               cycle);
  DEBUG_PRINTLN("[DISK-SEARCH] trace format: cycle:PC addr delta idx latch track flags(q6,q7,motor,write,high)");
  unsigned int available = DiskLoaderTraceIndex < DISK_LOADER_TRACE_SIZE
                         ? DiskLoaderTraceIndex : DISK_LOADER_TRACE_SIZE;
  unsigned int first = DiskLoaderTraceIndex - available;
  for (unsigned int offset = 0; offset < available; offset++) {
    DiskLoaderTraceEvent * event =
      &DiskLoaderTrace[(first + offset) % DISK_LOADER_TRACE_SIZE];
    DEBUG_PRINTF("[DISK-TRACE] %lu:%04X C0E%X +%u idx=%u data=%02X "
                 "trk=%u flags=%02X\n",
                 (unsigned long) event->cycle, event->pc, event->address,
                 event->delta, event->index, event->latch, event->track,
                 event->flags);
  }
}
#endif

void ResetDiskLoaderDiagnostics() {
#if ENABLE_DISK_DIAGNOSTICS
  memset(DiskLoaderTrace, 0, sizeof(DiskLoaderTrace));
  DiskLoaderTraceIndex = 0;
  DiskLoaderLastAccessCycle = cycle;
  DiskLoaderReadCount = 0;
  DiskLoaderWriteCount = 0;
  DiskLoaderCheckReadCount = 0;
  DiskLoaderCheckWriteCount = 0;
  DiskLoaderCheckDrive = -1;
  DiskLoaderCheckTrack = -1;
  DiskLoaderStagnantIntervals = 0;
  DiskLoaderSnapshotDumped = false;
#endif
}

void CheckDiskLoaderSearch() {
#if ENABLE_DISK_DIAGNOSTICS
  DriveState * ds = &DrvSt[CurDrv];
  uint32_t readsSinceCheck = DiskLoaderReadCount - DiskLoaderCheckReadCount;
  uint32_t writesSinceCheck = DiskLoaderWriteCount - DiskLoaderCheckWriteCount;
  bool samePosition = DiskLoaderCheckDrive == CurDrv &&
                      DiskLoaderCheckTrack == ds->Track;
  bool searchHeavy = ds->Active && !ds->Writing && !ds->ReadWP &&
                     samePosition && writesSinceCheck == 0 &&
                     readsSinceCheck >= 10000;

  if (searchHeavy) {
    if (DiskLoaderStagnantIntervals < 0xFF)
      DiskLoaderStagnantIntervals++;
  } else {
    DiskLoaderStagnantIntervals = 0;
    if (!ds->Active || writesSinceCheck || !samePosition)
      DiskLoaderSnapshotDumped = false;
  }

  // Task telemetry invokes this every ten seconds. Three consecutive
  // read-heavy intervals on one track is a loader search, not normal latency.
  if (DiskLoaderStagnantIntervals >= 3 && !DiskLoaderSnapshotDumped) {
    DiskLoaderSnapshotDumped = true;
    DumpDiskLoaderSnapshot(readsSinceCheck);
  }

  DiskLoaderCheckReadCount = DiskLoaderReadCount;
  DiskLoaderCheckWriteCount = DiskLoaderWriteCount;
  DiskLoaderCheckDrive = CurDrv;
  DiskLoaderCheckTrack = ds->Track;
#endif
}

// A media selector can leave the Disk II motor logically active while the CPU
// is paused for several seconds.  Start the replacement disk at a clean
// rotational timestamp so its first controller access never inherits timing
// from the previous image.
void ResetDiskRotationTiming() {
  LastRotationCycle = cycle;
  LastIO = cycle;
  RotationCycleRemainder = 0;
  TrkBufIdx = 0;
  ResetDiskLoaderDiagnostics();
}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
byte ReadDiskIO(word Address) {
  struct DriveState * ds;
  int newtrack;
  int writezero;

  ds = & DrvSt[CurDrv];
  newtrack = ds -> Track;

  /* Update track buffer */
  if (ds -> Active && ds -> Track % 2 == 0) {
    /* Only update when the disk drive is active AND the track is valid
       AND we're emulating correct disk drive speed */
    unsigned long rotationElapsed = cycle - LastRotationCycle;
    LastRotationCycle = cycle;
    RotationCycleRemainder += rotationElapsed;
    unsigned long rotationBytes = RotationCycleRemainder / 32L;
    RotationCycleRemainder %= 32L;
    writezero = 0;
    if (rotationBytes) {
      if (ds -> Writing && !ds -> WritePro) {
        // A long write delay fills the intervening magnetic media with zeroes.
        // No more than one complete revolution needs to be touched because any
        // additional revolutions overwrite the same circular track.
        unsigned long bytesToClear = rotationBytes > (unsigned long) TrackBufLen
          ? (unsigned long) TrackBufLen : rotationBytes;
        for (unsigned long byteIndex = 1; byteIndex < bytesToClear; byteIndex++) {
          int zeroIndex = (TrkBufIdx + byteIndex) % TrackBufLen;
          TrackBuffer[zeroIndex] = 0;
        }
        writezero = rotationBytes > 1;
      }

      // Advancing the rotating track is arithmetic rather than a loop. Games
      // commonly leave the motor on for many seconds between disk accesses;
      // replaying every elapsed byte here used to stall the CPU task.
      TrkBufIdx = (TrkBufIdx + (rotationBytes % TrackBufLen)) % TrackBufLen;
    }
    Diff = cycle - LastIO;
    LeftOverCycles = Diff & 31UL;
  }

  /* Handle I/O access */
  switch (Address & 0x0f) {
  case 0x00:
    /* Q0 - Phase 0 off */
  case 0x02:
    /* Q1 - Phase 1 off */
  case 0x04:
    /* Q2 - Phase 2 off */
  case 0x06:
    /* Q3 - Phase 3 off */
    break;
  case 0x01:
    /* Q0 - Phase 0 on */
    if (ds -> Active) {
      if (ds -> Phase == 1) {
        newtrack--; /* move head out */
      }
      if (ds -> Phase == 3) {
        newtrack++; /* move head in  */
      }
      ds -> Phase = 0;
    }
    break;
  case 0x03:
    /* Q1 - Phase 1 on */
    if (ds -> Active) {
      if (ds -> Phase == 2) {
        newtrack--; /* move head out */
      }
      if (ds -> Phase == 0) {
        newtrack++; /* move head in  */
      }
      ds -> Phase = 1;
    }
    break;
  case 0x05:
    /* Q2 - Phase 2 on */
    if (ds -> Active) {
      if (ds -> Phase == 3) {
        newtrack--; /* move head out */
      }
      if (ds -> Phase == 1) {
        newtrack++; /* move head in  */
      }
      ds -> Phase = 2;
    }
    break;
  case 0x07:
    /* Q3 - Phase 3 on */
    if (ds -> Active) {
      if (ds -> Phase == 0) {
        newtrack--; /* move head out */
      }
      if (ds -> Phase == 2) {
        newtrack++; /* move head in  */
      }
      ds -> Phase = 3;
    }
    break;
  case 0x08:
    /* Q4 - Drive off */
    ds -> Active = 0;
    LastRotationCycle = cycle;
    LastIO = cycle;
    RotationCycleRemainder = 0;
    break;
  case 0x09:
    /* Q4 - Drive on */
    if (!ds -> Active) {
      LastRotationCycle = cycle;
      LastIO = cycle;
      RotationCycleRemainder = 0;
    }
    ds -> Active = 1;
    break;
  case 0x0a:
    /* Q5 - Drive 1 select */
    if (CurDrv != 0) {
      if (ds -> TrkBufChanged && ds -> Track % 2 == 0) {
        WriteTrack(ds);
      }
      CurDrv = 0;
      ds = & DrvSt[CurDrv];
      ReadTrack(ds);
      TrkBufIdx = 0;
      newtrack = ds -> Track;
      if (DrvSt[1 - CurDrv].Active) {
        ds -> Active = 1;
      }
      DrvSt[1 - CurDrv].Active = 0; /* force other disk drive to be inactive */
    }
    break;
  case 0x0b:
    /* Q5- Drive 2 select */
    if (CurDrv != 1) {
      if (ds -> TrkBufChanged && ds -> Track % 2 == 0) {
        WriteTrack(ds);
      }
      CurDrv = 1;
      ds = & DrvSt[CurDrv];
      ReadTrack(ds);
      TrkBufIdx = 0;
      newtrack = ds -> Track;
      if (DrvSt[1 - CurDrv].Active) {
        ds -> Active = 1;
      }
      DrvSt[1 - CurDrv].Active = 0; /* force other disk drive to be inactive */
    }
    break;
  case 0x0c:
  case 0x0d:
  case 0x0e:
  case 0x0f:
    if (ds -> TrkBufOld && ds -> Track % 2 == 0) {
      ReadTrack(ds);
      // The disk keeps spinning while the head moves. Rebuilding the new
      // track must not snap rotational phase back to its first nibble.
      RangeCheckTBI(&TrkBufIdx);
    }
    /* handle switch changes first */
    switch (Address & 0x0f) {
    case 0x0c:
      /* Q6 off */
      ds -> ReadWP = 0;
      break;
    case 0x0d:
      /* Q6 on */
      ds -> ReadWP = 1;
      break;
    case 0x0e:
      /* Q7 off */
      if (ds -> Writing) {
        /* If <32us have passed since writing a byte, the byte just
           written will not be complete written. Place an 0xff there
           and move on. */
        if (Diff < 32L) {
          RangeCheckTBI( & TrkBufIdx);
          TrackBuffer[TrkBufIdx] = 0xff;
          ds -> TrkBufChanged = 1;
          Diff = 32L;
          LeftOverCycles = 0L;
        }
#if ENABLE_DISK_DIAGNOSTICS
        if (DiskWriteTraceCount < 64) {
          DEBUG_PRINTF("[DISK-WRITE] END session=%lu drive=%d track=%d "
                       "bytes=%u dirty=%d idx=%u cycle=%lu PC=%04X\n",
                       (unsigned long) DiskWriteSession, CurDrv + 1,
                       ds -> Track >> 1, DiskWriteSessionBytes,
                       ds -> TrkBufChanged, TrkBufIdx, cycle,
                       CPUInstructionStartPC);
          DiskWriteTraceCount++;
        }
#endif
      }
      ds -> Writing = 0;
      break;
    case 0x0f:
      /* Q7 on */
      if (!ds -> Writing) {
        /* If <32us have passed since reading a byte, the byte just
           read will be (partially) overwritten. Overwrite the whole
           lot. */
        LeftOverCycles = 0L;
        DiskWriteSession++;
        DiskWriteSessionBytes = 0;
#if ENABLE_DISK_DIAGNOSTICS
        if (DiskWriteTraceCount < 64) {
          DEBUG_PRINTF("[DISK-WRITE] BEGIN session=%lu drive=%d track=%d "
                       "wp=%d idx=%u cycle=%lu PC=%04X\n",
                       (unsigned long) DiskWriteSession, CurDrv + 1,
                       ds -> Track >> 1, ds -> WritePro, TrkBufIdx,
                       cycle, CPUInstructionStartPC);
          DiskWriteTraceCount++;
        }
#endif
      }
      ds -> Writing = 1;
      break;
    }
    /* Make sure the disk drive is active (motor on) before doing anything */
    if (!ds -> Active) {
      /* ProDOS appears to always want some changing data appear in the
         data latch, so generate some garbage to feed it! */
      if (!WriteAccess) {
        DataLatch = cycle & 0xff;
      }
      break;
    }
    /* then handle the mode */
    if (ds -> Writing) {
      if (!ds -> WritePro && WriteAccess && ds -> Track % 2 == 0) {
        /* write disk byte */
        if (Diff >= 32L) {
          if (Diff <= 40L) {
            LeftOverCycles = 0L;
          }
          LastIO = cycle - LeftOverCycles;
        }
        RangeCheckTBI( & TrkBufIdx);
        TrackBuffer[TrkBufIdx] = DataLatch;
        ds -> TrkBufChanged = 1;
        DiskWriteSessionBytes++;
#if ENABLE_DISK_DIAGNOSTICS
        if (DiskWriteSessionBytes == 1 && DiskWriteTraceCount < 64) {
          DEBUG_PRINTF("[DISK-WRITE] DATA session=%lu drive=%d track=%d "
                       "idx=%u latch=%02X cycle=%lu PC=%04X\n",
                       (unsigned long) DiskWriteSession, CurDrv + 1,
                       ds -> Track >> 1, TrkBufIdx, DataLatch, cycle,
                       CPUInstructionStartPC);
          DiskWriteTraceCount++;
        }
#endif
      }
    } else {
      /* read */
      if (ds -> ReadWP) {
        /* get write-protect status: bit 7 set=write-protected */
        DataLatch = ds -> WritePro ? 0xff : 0x00;
      } else {
        /* read disk byte */
        // A complete encoded nibble is available every 32 emulated cycles.
        // Returning guessed partial bits here caused custom loaders to accept
        // corrupt bytes and later execute invalid language-card code. Until a
        // bit-accurate sequencer exists, retain the earlier compatible model:
        // the latch reads as zero between complete high-bit nibbles.
        if (Diff < 32L) {
          DataLatch = 0;
        } else {
          RangeCheckTBI( & TrkBufIdx);
          DataLatch = TrackBuffer[TrkBufIdx];
          LastIO = cycle - LeftOverCycles;
        }
      }
    }
    break;
  }

  if (ds -> Track != newtrack) {
    /* Disk tracks have changed */
    /* Make sure current track is in range */
    if (newtrack < 0) {
      newtrack = 0;
      ds -> ShouldRecal = 1;
    }
    if (newtrack > MAX_DISK_HALF_TRACK) {
      newtrack = MAX_DISK_HALF_TRACK;
      ds -> ShouldRecal = 1;
    }

    // The boot ROM deliberately bangs the head against the track-zero stop
    // while recalibrating. Once clamped, this is not a real track change and
    // must not invalidate/rebuild the current track or reset I/O telemetry.
    if (ds -> Track == newtrack) {
      return DataLatch;
    }

#if ENABLE_DISK_DIAGNOSTICS
    if (DiskIOTraceCount < 32) {
      unsigned long now = millis();
      DEBUG_PRINTF("[DISKIO] head %d->%d elapsed=%lums idx=%u motor=%d\n",
                   ds -> Track, newtrack, now - DiskHeadPositionChangedAt,
                   TrkBufIdx, ds -> Active);
      DiskIOTraceCount++;
      DiskHeadPositionChangedAt = now;
    }
#endif
    /* NOTE: only even Disk ][ tracks are valid */
    if (ds -> TrkBufChanged && ds -> Track % 2 == 0) {
      WriteTrack(ds);
    }
    ds -> Track = newtrack;
    ds -> TrkBufOld = 1;
  }

#if ENABLE_DISK_DIAGNOSTICS
  if ((Address & 0x0F) >= 0x0C)
    RecordDiskControllerAccess(Address, ds);
#endif
  return DataLatch;
}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
void WriteDiskIO(word Address, byte Data) {
  /* Address = 0xe0-0xef */
  Address &= 0x0f; /* just keep offset */

  WriteAccess = 1;
  if ((Address & 0x0c) == 0x0c) {
    DataLatch = Data;
  }
  ReadDiskIO(Address);
  WriteAccess = 0;
}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
void DiskAutoID(struct DriveState * ds) {
  /* if the type is unknown, try to auto-identify */
  if (ds -> DiskType == UnknownType) {
    //      if ( ds->DiskSize % 0x1a00L == 0L )   /* Raw 0x1a00 bytes/track */
    if (ds -> DiskSize == 143390)
      ds -> DiskType = SimsysType;
    else if (ds -> DiskSize == 143424)
      ds -> DiskType = XgsType;
    else if (ds -> DiskSize >= 200000)
      ds -> DiskType = RawType;
    else
      ds -> DiskType = DOSType;
  }
}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
void MountDisk(int disk) {
  //printf("PRING GOD DAMN YOU !\n");
  struct DriveState * ds;
  unsigned int attr; /* MS-DOS file attributes */

  ds = & DrvSt[disk];

  if (!ds -> DiskBuffer) {
    ds -> DiskFH = -1;
    ds -> DiskSize = 0;
    ds -> DiskType = UnknownType;
    ds -> WritePro = 1;
    ds -> TrkBufChanged = 0;
    ds -> TrkBufOld = 1;
    ds -> ShouldRecal = 1;
    ds -> Track = 0;
    ds -> Phase = 0;
    ds -> ReadWP = 0;
    ds -> Active = 0;
    ds -> Writing = 0;
    DEBUG_PRINTF("[DISK] mounted drive=%d empty writeProtected=1\n", disk + 1);
    return;
  }

  /* open disk file and set disk parameters */
  attr = GetAttrib(ds -> DiskFN);
  if (attr == 0xffff) {
    attr = 0;
  }
  //    ds->WritePro = ( attr & FA_RDONLY ) ? 1 : 0;
  ds -> WritePro = DriveSavePath[disk][0] ? 0 : 1;
  ds -> DiskFH = openDisk(ds -> DiskFN, (ds -> WritePro ? O_RDONLY : O_RDWR));
  ds -> DiskSize = 0L;
  if (ds -> DiskFH >= 0) {
    ds -> DiskSize = FileSize(ds -> DiskFH);
    if (ds -> DiskSize < 143360) ds -> DiskSize = 143360; // uso. 2002.1109
  }
  DiskAutoID(ds);
  DEBUG_PRINTF("[DISK] mounted drive=%d size=%ld type=%d writeProtected=%d\n",
               disk + 1, ds -> DiskSize, (int) ds -> DiskType, ds -> WritePro);
  ds -> TrkBufChanged = 0;
  ds -> TrkBufOld = 1;
  ds -> ShouldRecal = 1;
  /* set disk emulation parameters */
  ds -> Track = 0;
  ds -> Phase = 0;
  ds -> ReadWP = 0;
  ds -> Active = 0;
  ds -> Writing = 0;
}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
void InitDisk(int slot) {
  DiskSlot = slot;

  // drive 1 is the OS/boot image host slot
  DrvSt[0].DiskBuffer = DriveDiskImage[0] ? DriveDiskImage[0] : DiskImage;
  DiskMountedBuffer[0] = DrvSt[0].DiskBuffer;
  DrvSt[0].DiskType = UnknownType;
  MountDisk(0);

  // drive 2 remains an attachable runtime slot for host-level image swaps
  DrvSt[1].DiskBuffer = DriveDiskImage[1] ? DriveDiskImage[1] : NULL;
  DiskMountedBuffer[1] = DrvSt[1].DiskBuffer;
  DrvSt[1].DiskType = UnknownType;
  MountDisk(1);

  CurDrv = 0;
  DataLatch = 0;
  TrkBufIdx = 0;
#if ENABLE_DISK_DIAGNOSTICS
  DiskHeadPositionChangedAt = millis();
#endif
  ResetDiskLoaderDiagnostics();
}
