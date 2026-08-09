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
#include <dirent.h>
#include <sys/stat.h>
extern "C" {
  #include "esp_spiram.h"
}

#ifdef RESCUE_RAIDERS
  #include "diskRescueRaiders.h"
#endif

#ifdef GALAXIAN
  #include "diskGalaxian.h"
#endif

#ifdef CHOPLIFTER
  #include "diskChoplifter.h"
#endif

#ifdef CANNONBALL_BLITZ
  #include "diskCannonBallBlitz.h"
#endif

#ifdef CASTLE_WOLFENSTEIN
  #include "diskCastleWolfestein.h"
#endif

#ifdef LOADRUNNER
  #include "diskLoadRunner.h"
#endif

unsigned long cycle;

static const size_t DISK_IMAGE_SIZE = 143360;
static const char * DISK_MOUNT_PATH = "/SD";
static const char * DISK_DIRECTORY = "/SD/apple2/disks";
static const char * LEGACY_DISK_PATH = "/SD/apple2/dos33.dsk";
static const int MAX_DISK_IMAGES = 32;
static const int MAX_VISIBLE_DISKS = 18;
unsigned char * DiskImage = NULL;
char DiskLoadError[96] = "Disk image has not been loaded";
char LoadedDiskName[64] = "";

struct DiskMenuEntry {
  char path[128];
  char name[64];
};

static DiskMenuEntry DiskMenu[MAX_DISK_IMAGES];
static int DiskMenuCount = 0;

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

static bool AddDiskMenuEntry(const char * path, const char * name) {
  if (DiskMenuCount >= MAX_DISK_IMAGES)
    return false;

  struct stat fileInfo;
  if (stat(path, &fileInfo) != 0 || !S_ISREG(fileInfo.st_mode))
    return false;
  if (fileInfo.st_size != (long) DISK_IMAGE_SIZE) {
    DEBUG_PRINTF("[SD] Skipping %s: %ld bytes (need %u)\n",
                 path, (long) fileInfo.st_size, (unsigned) DISK_IMAGE_SIZE);
    return false;
  }

  snprintf(DiskMenu[DiskMenuCount].path, sizeof(DiskMenu[DiskMenuCount].path), "%s", path);
  snprintf(DiskMenu[DiskMenuCount].name, sizeof(DiskMenu[DiskMenuCount].name), "%s", name);
  DiskMenuCount++;
  return true;
}

static void FindDiskImages() {
  DiskMenuCount = 0;

  // Keep the milestone-1 location as the default/fallback entry.
  AddDiskMenuEntry(LEGACY_DISK_PATH, "dos33.dsk");

  DIR * directory = opendir(DISK_DIRECTORY);
  if (!directory) {
    DEBUG_PRINTF("[SD] Directory %s not found; using legacy disk if available\n", DISK_DIRECTORY);
    return;
  }

  struct dirent * item;
  while (DiskMenuCount < MAX_DISK_IMAGES && (item = readdir(directory)) != NULL) {
    if (!HasDSKExtension(item->d_name))
      continue;
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", DISK_DIRECTORY, item->d_name);
    AddDiskMenuEntry(path, item->d_name);
  }
  closedir(directory);

  // Alphabetize the games while leaving the legacy DOS disk first.
  for (int i = 1; i < DiskMenuCount - 1; i++) {
    for (int j = i + 1; j < DiskMenuCount; j++) {
      if (strcmp(DiskMenu[i].name, DiskMenu[j].name) > 0) {
        DiskMenuEntry temporary = DiskMenu[i];
        DiskMenu[i] = DiskMenu[j];
        DiskMenu[j] = temporary;
      }
    }
  }
  DEBUG_PRINTF("[SD] Found %d valid disk image(s)\n", DiskMenuCount);
}

static void DrawDiskMenu(int selected) {
  int first = 0;
  if (selected >= MAX_VISIBLE_DISKS)
    first = selected - MAX_VISIBLE_DISKS + 1;
  int last = first + MAX_VISIBLE_DISKS;
  if (last > DiskMenuCount)
    last = DiskMenuCount;

  canvas.setBrushColor(Color::Black);
  canvas.clear();
  canvas.setPenColor(Color::BrightYellow);
  canvas.drawText(20, 15, "APPLE II DISK SELECTOR");
  canvas.setPenColor(Color::White);
  canvas.drawText(20, 30, "UP/DOWN: SELECT   ENTER: BOOT");

  for (int index = first; index < last; index++) {
    int y = 52 + (index - first) * 12;
    if (index == selected) {
      canvas.setBrushColor(Color::BrightGreen);
      canvas.setPenColor(Color::Black);
    } else {
      canvas.setBrushColor(Color::Black);
      canvas.setPenColor(Color::White);
    }
    char line[70];
    snprintf(line, sizeof(line), "%c %s", index == selected ? '>' : ' ', DiskMenu[index].name);
    canvas.drawText(20, y, line);
  }

  canvas.setBrushColor(Color::Black);
  canvas.setPenColor(Color::BrightCyan);
  char status[48];
  snprintf(status, sizeof(status), "DISK %d OF %d", selected + 1, DiskMenuCount);
  canvas.drawText(20, 278, status);
}

static int SelectDiskImage() {
  if (DiskMenuCount == 1)
    return 0;

  auto keyboard = PS2Controller.keyboard();
  keyboard->enableVirtualKeys(true, true);
  int selected = 0;
  DrawDiskMenu(selected);

  for (;;) {
    bool keyDown = false;
    VirtualKey key = keyboard->getNextVirtualKey(&keyDown);
    if (!keyDown)
      continue;
    if (key == fabgl::VK_UP || key == fabgl::VK_KP_UP) {
      selected = selected > 0 ? selected - 1 : DiskMenuCount - 1;
      DrawDiskMenu(selected);
    } else if (key == fabgl::VK_DOWN || key == fabgl::VK_KP_DOWN) {
      selected = selected + 1 < DiskMenuCount ? selected + 1 : 0;
      DrawDiskMenu(selected);
    } else if (key == fabgl::VK_RETURN || key == fabgl::VK_KP_ENTER) {
      break;
    } else if (key == fabgl::VK_ESCAPE) {
      selected = 0;
      break;
    }
  }

  keyboard->enableVirtualKeys(false, false);
  return selected;
}

bool LoadBootDiskFromSD() {
  bool psramReady = false;
  bool diskImageInPSRAM = false;
  DEBUG_PRINTLN("[MEM] Initializing onboard PSRAM for disk buffer...");
  if (esp_spiram_init() == ESP_OK) {
#ifndef BOARD_HAS_PSRAM
    esp_spiram_init_cache();
#endif
    psramReady = true;
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
  if (DiskMenuCount == 0) {
    snprintf(DiskLoadError, sizeof(DiskLoadError), "No valid .dsk images found");
    DEBUG_PRINTF("[SD] ERROR: %s\n", DiskLoadError);
    fabgl::FileBrowser::unmountSDCard();
    return false;
  }

  int selected = SelectDiskImage();
  DEBUG_PRINTF("[SD] Selected %s\n", DiskMenu[selected].path);
  FILE * imageFile = fopen(DiskMenu[selected].path, "rb");
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

  if (psramReady) {
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

  size_t bytesRead = fread(DiskImage, 1, DISK_IMAGE_SIZE, imageFile);
  fclose(imageFile);
  fabgl::FileBrowser::unmountSDCard();
  if (bytesRead != DISK_IMAGE_SIZE) {
    snprintf(DiskLoadError, sizeof(DiskLoadError), "Short read: %u of %u bytes", (unsigned) bytesRead, (unsigned) DISK_IMAGE_SIZE);
    DEBUG_PRINTF("[SD] ERROR: %s\n", DiskLoadError);
    if (!diskImageInPSRAM)
      free(DiskImage);
    DiskImage = NULL;
    return false;
  }

  DiskLoadError[0] = '\0';
  snprintf(LoadedDiskName, sizeof(LoadedDiskName), "%s", DiskMenu[selected].name);
  DEBUG_PRINTF("[SD] Loaded %s into PSRAM (%u bytes)\n", LoadedDiskName, (unsigned) DISK_IMAGE_SIZE);
  return true;
}

int FastMode = 1;
/* 0:correctly emulate drive rotation speed, !0:faster, but
   the drive performs one rotation in 130.5ms (instead of
   200ms). Turn fast mode off so ProDOS formatting and some
   copy-protection schemes can work. */

/* applemu incorrectly uses 0x1a00 length tracks. A real
   Apple disk drive spinning at ~300RPM, or 1 rev every
   200ms and writing a byte every 32us has a capacity
   of 6250 bytes/track not 6656. Setting TrackBufLen to
   0x1a00 allows my disk emulation to read applemu disks,
   but with FastMode off the drive 'spins' a little too
   slow, and the extra bytes that are available on every
   track can confuse formatting programs. The ProDOS FILER
   is one of these.  You MUST set FastMode to 0 and
   TrackBufLen to 6250 for FILER to format. */
int TrackBufLen = 6250;

int DiskSlot;

#define O_RDONLY 0
#define O_RDWR 1

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
  int Track; /* 0-70 */
  int Phase; /* 0- 3 */
  int ReadWP; /* 0/1  */
  int Active; /* 0/1  */
  int Writing; /* 0/1  */
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
void readSector(int drvAtivo, void * buf, size_t size) {
  if (DiskImage && SeekPos <= DISK_IMAGE_SIZE && size <= DISK_IMAGE_SIZE - SeekPos) {
    memcpy(buf, DiskImage + SeekPos, size);
  } else {
    memset(buf, 0, size);
    DEBUG_PRINTF("[DISK] ERROR: invalid read at %u (%u bytes)\n", SeekPos, (unsigned) size);
  }

#ifdef RESCUE_RAIDERS
    memcpy(buf, Rescue_Raiders + SeekPos, size);
#endif

#ifdef GALAXIAN
    memcpy(buf, Galaxian_1980 + SeekPos, size);
#endif

#ifdef CHOPLIFTER
    memcpy(buf, Choplifter_19xx + SeekPos, size);
#endif

#ifdef CANNONBALL_BLITZ
    memcpy(buf, Cannon_Ball_Blitz_19xx + SeekPos, size);
#endif

#ifdef CASTLE_WOLFENSTEIN
    memcpy(buf, Castle_Wolfenstein_1981 + SeekPos, size);
#endif

#ifdef LOADRUNNER
    memcpy(buf, Lode_Runner_1983 + SeekPos, size);
#endif

}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
void writeSector(int drvAtivo, void * buf, size_t size) {
  // Milestone 1: the SD-backed image is deliberately read-only.

#ifdef RESCUE_RAIDERS
  //memcpy (Rescue_Raiders+SeekPos, buf, size);
#endif

#ifdef GALAXIAN
  //memcpy (Galaxian_1980+SeekPos, buf, size);
#endif

#ifdef CHOPLIFTER
  //memcpy (Choplifter_19xx+SeekPos, buf, size);
#endif

#ifdef CANNONBALL_BLITZ
  //memcpy (Cannon_Ball_Blitz_19xx+SeekPos, buf, size);
#endif

#ifdef CASTLE_WOLFENSTEIN
  //memcpy (Castle_Wolfenstein_1981+SeekPos, buf, size);
#endif

#ifdef LOADRUNNER
  //memcpy (Lode_Runner_1983+SeekPos, buf, size);
#endif
}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
void closeDisk(int fd) {}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
long tell(int handle) {
  return lseekDisk(handle, 0, SEEK_END);
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
}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
/* find and read a given hard sector given the disk format type */
/* only for .DO type files */
void ReadHardSector(struct DriveState * ds, int sector, unsigned char * buf) {
  GotoHardSector(ds, sector);
  readSector(ds->Active, buf, 256);
}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
/* find and write a given hard sector given the disk format type */
/* only for .DO type files */
void WriteHardSector(struct DriveState * ds, int sector, char * buf) {
  GotoHardSector(ds, sector);
  writeSector(ds->Active, buf, 256);
}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
/* Sector=0..15 */
void NibbliseSector(unsigned char * data, unsigned char ** trackbufptr,
  int volume, int track, int sector) {
  /* Define a template for all disk sectors */
	static unsigned char disktemplate[ 28 ] =
    {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, /*  0- 5 = self-sync bytes */
        0xd5, 0xaa, 0x96,                   /*  6- 8 = address header */
        0, 0, 0, 0, 0, 0, 0, 0,             /*  9-16 = address */
        0xde, 0xaa, 0xeb,                   /* 17-19 = address trailer */
        0xff, 0xff, 0xff, 0xff, 0xff,       /* 20-24 = self-sync bytes */
        0xd5, 0xaa, 0xad                    /* 25-27 = data header */
    };

  int diskbyte, checksum, v;
  unsigned char * trackbuf;

  trackbuf = * trackbufptr;

  /* fill in address in template */
  checksum = volume ^ track ^ sector;
  disktemplate[9] = (volume >> 1) | 0xaa;
  disktemplate[10] = volume | 0xaa;
  disktemplate[11] = (track >> 1) | 0xaa;
  disktemplate[12] = track | 0xaa;
  disktemplate[13] = (sector >> 1) | 0xaa;
  disktemplate[14] = sector | 0xaa;
  disktemplate[15] = (checksum >> 1) | 0xaa;
  disktemplate[16] = checksum | 0xaa;

  /* template */
  for (diskbyte = 0; diskbyte < 28; diskbyte++) {
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

  /* Make sure that any unused part of the buffer has 0xff's in it */
  for (idx = 0; idx < 0x1a00; idx++) {
    TrackBuffer[idx] = 0xff;
  }
  if (ds -> DiskType == RawType) {
    /* Disk ][ track/byte format (0x1a00 bytes/track) - just read the bytes */
    lseekDisk(ds -> DiskFH, (long)(ds -> Track >> 1) * 0x1a00L, SEEK_SET);
    readSector(ds->Active, TrackBuffer, 0x1a00);
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
#pragma argsused
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

  /* fill any unused space in buffer with 0xff's */
  if (TrackBufLen < 0x1a00) {
    for (idx = TrackBufLen; idx < 0x1a00; idx++) {
      TrackBuffer[idx] = 0xff;
    }
  }

  if (ds -> DiskType == RawType) {
    /* Disk ][ track/byte format (0x1a00 bytes/track) - just write the bytes */
    lseekDisk(ds -> DiskFH, (long)(ds -> Track >> 1) * 0x1a00L, SEEK_SET);
    writeSector(ds->Active, TrackBuffer, 0x1a00);
  }
  if (ds -> DiskType == DOSType || ds -> DiskType == ProDOSType ||
    ds -> DiskType == SimsysType || ds -> DiskType == XgsType) {
    /* Track/sector format (4096 bytes/track) - translate from Disk ][ format */
    DeNibbliseTrack(ds);
  }
  ds -> DiskSize = FileSize(ds -> DiskFH);
  if (ds -> DiskSize < 143360) ds -> DiskSize = 143360; // uso. 2002.1109
  /* track has been saved - so clear changed flag */
  ds -> TrkBufChanged = 0;
}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
void UpdateDiskStatus(void) {
  //extern int dlight1, dlight2;
  //dlight1=(DrvSt[0].Active); dlight2=(DrvSt[1].Active);
  //virtplot(634,4,dlight1?COL_DRV_ON:COL_DRV_OFF);
  //virtplot(636,4,dlight2?COL_DRV_ON:COL_DRV_OFF);
  //virtcopy = 1;
}

long LastIO = 0;
long Diff, LeftOverCycles;

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
byte ReadDiskIO(word Address) {
  struct DriveState * ds;
  int newtrack;
  int writezero;
  char t[20];

  ds = & DrvSt[CurDrv];
  newtrack = ds -> Track;

  /* Update track buffer */
  if ( /*ds->Active &&*/ ds -> Track % 2 == 0 && !FastMode) {
    /* Only update when the disk drive is active AND the track is valid
       AND we're emulating correct disk drive speed */
    Diff = cycle - LastIO;
    if (Diff == 31L) {
      Diff = 32L; /* handle faster 65C02 execution */
    }
    LeftOverCycles = Diff;
    writezero = 0;
    while (LeftOverCycles >= 32L) {
      if (ds -> Writing && writezero) {
        RangeCheckTBI( & TrkBufIdx);
        TrackBuffer[TrkBufIdx] = 0;
      }
      TrkBufIdx++;
      LeftOverCycles -= 32L;
      writezero = 1; /* delays >32us cause 0's to be written */
    }
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
    break;
  case 0x09:
    /* Q4 - Drive on */
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
      TrkBufIdx = 0;
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
        if (FastMode) {
          RangeCheckTBI( & TrkBufIdx);
          TrackBuffer[TrkBufIdx++] = DataLatch;
          ds -> TrkBufChanged = 1;
        } else {
          if (Diff >= 32L) {
            if (Diff <= 40L) {
              LeftOverCycles = 0L;
            }
            LastIO = cycle - LeftOverCycles;
          }
          RangeCheckTBI( & TrkBufIdx);
          TrackBuffer[TrkBufIdx] = DataLatch;
          ds -> TrkBufChanged = 1;
        }
      }
    } else {
      /* read */
      if (ds -> ReadWP) {
        /* get write-protect status: bit 7 set=write-protected */
        DataLatch = ds -> WritePro ? 0xff : 0x00;
      } else {
        /* read disk byte */
        if (FastMode) {
          RangeCheckTBI( & TrkBufIdx);
          DataLatch = TrackBuffer[TrkBufIdx++];
        } else {
          if (Diff >= 32L) {
            RangeCheckTBI( & TrkBufIdx);
            DataLatch = TrackBuffer[TrkBufIdx];
            LastIO = cycle - LeftOverCycles;
          } else {
            DataLatch = 0;
          }
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
    if (newtrack > 70) {
      newtrack = 70;
      ds -> ShouldRecal = 1;
    }
    /* NOTE: only even Disk ][ tracks are valid */
    if (ds -> TrkBufChanged && ds -> Track % 2 == 0) {
      WriteTrack(ds);
    }
    ds -> Track = newtrack;
    ds -> TrkBufOld = 1;
  }

  UpdateDiskStatus();

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
void DiskReset(void) {
  DrvSt[0].Active = 0;
  DrvSt[0].ReadWP = 0;
  DrvSt[0].Writing = 0;
  DrvSt[1].Active = 0;
  DrvSt[1].ReadWP = 0;
  DrvSt[1].Writing = 0;
  UpdateDiskStatus();
}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
void UnmountDisk(int disk) {
  struct DriveState * ds;
  ds = & DrvSt[disk];
  if (ds -> TrkBufChanged) {
    WriteTrack(ds);
  }
  closeDisk(ds -> DiskFH);
  if (ds -> DiskSize == 0L) {
    /* nothing in the file - might as well delete it */
    //remove ( ds->DiskFN );
  }
  ds -> DiskFH = 0;
}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
void DiskAutoID(struct DriveState * ds) {
  unsigned char t1, t2; /* temporary data during identification */
  int idcount; /* count of ProDOS identifying characteristics */

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

  /* open disk file and set disk parameters */
  attr = GetAttrib(ds -> DiskFN);
  if (attr == 0xffff) {
    attr = 0;
  }
  //    ds->WritePro = ( attr & FA_RDONLY ) ? 1 : 0;
  ds -> WritePro = 1;
  ds -> DiskFH = openDisk(ds -> DiskFN, (ds -> WritePro ? O_RDONLY : O_RDWR));
  ds -> DiskSize = 0L;
  if (ds -> DiskFH >= 0) {
    ds -> DiskSize = FileSize(ds -> DiskFH);
    if (ds -> DiskSize < 143360) ds -> DiskSize = 143360; // uso. 2002.1109
  }
  DiskAutoID(ds);
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

  //strcpy( DrvSt[ 0 ].DiskFN, "disk6a.dsk" );
  DrvSt[0].DiskType = UnknownType;
  MountDisk(0);
  //strcpy( DrvSt[ 1 ].DiskFN, "disk6b.dsk" );
  DrvSt[ 1 ].DiskType = UnknownType;
  MountDisk( 1 );

  UpdateDiskStatus();

  CurDrv = 0;
  DataLatch = 0;
  TrkBufIdx = 0;
}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
void ShutdownDisk(void) {
  UnmountDisk(0);
  UnmountDisk(1);
}
