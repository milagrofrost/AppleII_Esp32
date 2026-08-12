/*****************************************************************************

    File: "keyboard.ino"
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

const unsigned char scancode_to_apple[] PROGMEM = {
 //$0    $1    $2    $3    $4    $5    $6    $7    $8    $9    $A    $B    $C    $D    $E    $F
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //$00
  0x00, 0x00, 0x00, 0x00, 0x00, 0xD1, 0xB1, 0x00, 0x00, 0x00, 0xDA, 0xD3, 0xC1, 0xD7, 0xB2, 0x00, //$10
  0x00, 0xC3, 0xD8, 0xC4, 0xC5, 0xB4, 0xB3, 0x00, 0x00, 0xA0, 0xD6, 0xC6, 0xD4, 0xD2, 0xB5, 0x00, //$20
  0x00, 0xCE, 0xC2, 0xC8, 0xC7, 0xD9, 0xB6, 0x00, 0x00, 0x00, 0xCD, 0xCA, 0xD5, 0xB7, 0xB8, 0x00, //$30
  0x00, 0xAC, 0xCB, 0xC9, 0xCF, 0xB0, 0xB9, 0x00, 0x00, 0xAE, 0xAF, 0xCC, 0xBB, 0xD0, 0xAD, 0x00, //$40
  0x00, 0x00, 0xA7, 0x00, 0x00, 0xBD, 0x00, 0x00, 0x00, 0x00, 0x8D, 0x00, 0x00, 0x00, 0x00, 0x00, //$50
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x88, 0x00, 0x00, 0xB1, 0x00, 0xB4, 0xB7, 0x00, 0x00, 0x00, //$60
  0xB0, 0xAE, 0xB2, 0xB5, 0xB6, 0xB8, 0x9B, 0x00, 0x00, 0xAB, 0xB3, 0xAD, 0xAA, 0xB9, 0x00, 0x00,  //$70
  // High mirror, shift modified keys
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //$80 0
  0x00, 0x00, 0x00, 0x00, 0x00, 0xD1, 0xA1, 0x00, 0x00, 0x00, 0xDA, 0xD3, 0xC1, 0xD7, 0xC0, 0x00, //$90 1
  0x00, 0xC3, 0xD8, 0xC4, 0xC5, 0xA4, 0xA3, 0x00, 0x00, 0xA0, 0xD6, 0xC6, 0xD4, 0xD2, 0xA5, 0x00, //$A0 2
  0x00, 0xCE, 0xC2, 0xC8, 0xC7, 0xD9, 0xDE, 0x00, 0x00, 0x00, 0xCD, 0xCA, 0xD5, 0xA6, 0xAA, 0x00, //$B0 3
  0x00, 0xBC, 0xCB, 0xC9, 0xCF, 0xA9, 0xA8, 0x00, 0x00, 0xBE, 0xBF, 0xCC, 0xBA, 0xD0, 0xAD, 0x00, //$C0 4 
  0x00, 0x00, 0xA2, 0x00, 0x00, 0xAB, 0x00, 0x00, 0x00, 0x00, 0x8D, 0x00, 0x00, 0x00, 0x00, 0x00, //$D0 5
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x88, 0x00, 0x00, 0xB1, 0x00, 0xB4, 0xB7, 0x00, 0x00, 0x00, //$E0 6
  0xB0, 0xAE, 0xB2, 0xB5, 0xB6, 0xB8, 0x9B, 0x00, 0x00, 0xAB, 0xB3, 0xAD, 0xAA, 0xB9, 0x00, 0x00  //$F0 7
};

// keyboard scan buffer
unsigned short keyboard_data[3] = {0, 0, 0};
unsigned char keyboard_mbyte = 0;
boolean shift_enabled = false;

// Host-side hot-key bookkeeping for disk manager hand-off.
static bool HostCtrlDown = false;
static bool HostAltDown = false;
static bool HostDeleteDown = false;
static bool HostBreakPending = false;

// In apple II scancode format
volatile unsigned char keymem = 0;

/***************************************************************************************************************************************/
//
/***************************************************************************************************************************************/
unsigned char keyboard_read() {
  return keymem;
}

/***************************************************************************************************************************************/
//
/***************************************************************************************************************************************/
void keyboard_strobe() {
  keyboard_mbyte = 0;       
  keymem&=0x7F;
}

static void HostDiskManagerTrigger() {
  DEBUG_PRINTLN("[HOST] Ctrl+Alt+Del requested: disk manager hot-key intercepted");

  if (Task1 != NULL) {
    vTaskSuspend(Task1);
  }

  FindDiskImages();
  if (DiskMenuCount == 0) {
    DEBUG_PRINTLN("[HOST] No disk images are available for the manager screen");
    if (Task1 != NULL) {
      vTaskResume(Task1);
    }
    return;
  }

  int selected = SelectDiskImage();
  DEBUG_PRINTF("[HOST] Cold boot drive 1 from %s\n", DiskEntryPath(selected));

  if (LoadDiskImageForDrive(0, DiskEntryPath(selected))) {
    // A different game needs a power-on-style boot, not merely a disk attach.
    // Clear the emulated machine while this video/keyboard task owns the menu
    // and the CPU task is suspended.
    memset(RAM, 0, sizeof(RAM));
    memset(RAMEXT, 0, sizeof(RAMEXT));
    if (AUXRAM)
      memset(AUXRAM, 0, 0x10000);
    if (AUXRAMEXT)
      memset(AUXRAMEXT, 0, 0x4000);
    memset(RAM_TXT_BACK, 0xFF, sizeof(RAM_TXT_BACK));
    memset(RAM_HGR_BACK, 0xFF, sizeof(RAM_HGR_BACK));
    ResetMemorySoftSwitches();
    A = X = Y = 0;
    SR = 0x20;
    STP = 0xFD;
    keymem = 0;
    snprintf(LoadedDiskName, sizeof(LoadedDiskName), "%s", DiskEntryName(selected));
    initCode();
    canvas.setBrushColor(Color::Black);
    canvas.clear();
    DrawVGAAlignmentMarkers();
    DEBUG_PRINTF("[HOST] drive 1 cold boot initialized: %s PC=%04X\n",
                 DiskEntryName(selected), PC);
  } else {
    DEBUG_PRINTF("[HOST] failed to load selection into drive 1: %s (%s)\n",
                 DiskEntryPath(selected), DiskLoadError);
  }

  if (Task1 != NULL) {
    vTaskResume(Task1);
  }
}

/***************************************************************************************************************************************/
//
/***************************************************************************************************************************************/
void keyboard_In(int keyPush) {

  // Host-level Ctrl+Alt+Del hot-key intercept. This sequence must never be
  // translated into an Apple II key. Track PS/2 make and break sequences
  // without swallowing their bytes, so the normal raw-scancode parser remains
  // synchronized after returning from the disk menu.
  if (keyPush == 0xF0) {
    HostBreakPending = true;
  } else if (keyPush != 0xE0) {
    bool keyDown = !HostBreakPending;
    HostBreakPending = false;

    if (keyPush == 0x14)
      HostCtrlDown = keyDown;
    else if (keyPush == 0x11)
      HostAltDown = keyDown;
    else if (keyPush == 0x71) {
      HostDeleteDown = keyDown;
      if (keyDown && HostCtrlDown && HostAltDown) {
        HostDeleteDown = false;
        HostCtrlDown = false;
        HostAltDown = false;
        HostDiskManagerTrigger();
      }
    }

    if (keyPush == 0x14 || keyPush == 0x11 || keyPush == 0x71) {
      // These host-only keys are not Apple II input. Clear any E0/F0 prefix
      // retained by the legacy parser so the next ordinary key starts cleanly.
      keyboard_mbyte = 0;
      keyboard_data[0] = 0;
      keyboard_data[1] = 0;
      keyboard_data[2] = 0;
      return;
    }
  }

  keyboard_data[2] = keyPush;
    
  // extended keys
  if(keyboard_data[2] == 0xF0 || keyboard_data[2] == 0xE0) keyboard_mbyte = 1;
  else {
    //decrement counter for multibyte commands
    //if(keyboard_mbyte) keyboard_mbyte--;
    // multibyte command is finished / normal command, process it
    if(keyboard_mbyte) {
      //DEBUG_PRINTF("2-%02X \n", keyPush);

      if(keyboard_data[1] != 0xF0 && keyboard_data[1] != 0xE0) {
        //Standard keys
        if(keyboard_data[2] == 0x12 || keyboard_data[2] == 0x59) shift_enabled = true; //shift modifiers
        else 
        keymem = pgm_read_byte_near(scancode_to_apple+keyboard_data[2]+((shift_enabled)?0x80:0x00));
      } else 
      if(keyboard_data[0] != 0xF0 && keyboard_data[1] == 0xE0) {
        //Extended keys
        if(keyboard_data[2] == 0x6B) keymem = 0x95; //back key
        if(keyboard_data[2] == 0x74) keymem = 0x88; //forward key
        // Power management keys, hardware reset
        if(keyboard_data[2] == 0x37) {
          // enable watchdog with min timeout
          // wait until reset
          //wdt_enable(WDTO_15MS);
          //for(;;);            
        }
      } else if(keyboard_data[1] == 0xF0 && (keyboard_data[2] == 0x12 || keyboard_data[2] == 0x59)) shift_enabled = false;  
    } else {
      //keymem = pgm_read_byte_near(scancode_to_apple+keyboard_data[2]+((shift_enabled)?0x80:0x00));
    }

    //shuffle buffer
    keyboard_data[0] = keyboard_data[1];
    keyboard_data[1] = keyboard_data[2];
  }
}
