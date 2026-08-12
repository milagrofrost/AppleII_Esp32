# Apple II Emulator for ESP32

![Apple II emulator comparison](Imagens/AppleII_ESP32_Compara.jpg)

This project emulates an original Apple II on an ESP32, with VGA video, PS/2 keyboard input, and sound. It is intended both as a usable nostalgic emulator and as an educational example of 6502, Apple II video, and Disk II emulation on a modern microcontroller.

The project was originally created by Francisco J. A. Souza. The 6502 and disk-emulation sources were adapted from earlier emulator projects; their original authors should be credited when they are identified.

## Current hardware target

The currently tested configuration is:

- Olimex ESP32-SBC-FabGL
- Olimex FabGL fork 1.0.9
- ESP32 Arduino core 2.0.11
- Board: **ESP32 Dev Module**
- CPU frequency: **240 MHz**
- Partition scheme: **Huge APP**
- PSRAM setting in Arduino IDE: **Disabled**
- Serial monitor: **115200 baud**

Keep the Arduino IDE PSRAM setting disabled. The firmware initializes the ESP32-SBC-FabGL's onboard PSRAM at runtime, following the approach used by Olimex's FabGL examples.

Open and upload the root sketch:

```text
EspAppleII/EspAppleII.ino
```

The nested duplicate sketch at `EspAppleII/EspAppleII/EspAppleII.ino`, if present in an older checkout, is not used.

## Software setup

### 1. Install Arduino IDE

Install [Arduino IDE 2](https://www.arduino.cc/en/software). This project is an Arduino sketch, so PlatformIO or ESP-IDF is not required.

### 2. Install ESP32 Arduino core 2.0.11

In Arduino IDE:

1. Open **Tools → Board → Boards Manager**.
2. Search for `esp32` by Espressif Systems.
3. Select version **2.0.11** and install it.

Do not use the ESP32 3.x core for this tested configuration. FabGL and its runtime PSRAM setup depend on behavior provided by the 2.0.11 toolchain.

### 3. Install the Olimex FabGL fork

Use the [Olimex FabGL fork](https://github.com/OLIMEX/FabGL), not the standard FabGL Library Manager release.

1. Download the Olimex repository as a ZIP file.
2. In Arduino IDE, choose **Sketch → Include Library → Add .ZIP Library**.
3. Select the downloaded ZIP.
4. Restart Arduino IDE if the FabGL examples or headers are not detected.

If another FabGL version is already installed, remove or rename it first. Multiple FabGL installations can cause Arduino to compile against the wrong library. The tested Olimex fork reports FabGL version 1.0.9.

### 4. Install the Timer library

The sketch requires Jack Christensen's [Arduino Timer library](https://github.com/JChristensen/Timer), which supplies `Timer.h`, `Timer.cpp`, `Event.h`, and `Event.cpp`.

1. Download the Timer repository as a ZIP file.
2. Choose **Sketch → Include Library → Add .ZIP Library**.
3. Select the Timer ZIP.

This project was tested with the classic Timer 1.x API. It is not the ESP32 hardware-timer API and not an unrelated library with a similar `Timer` name. After installation, a typical library directory contains:

```text
Arduino/libraries/Timer-master/
├── Timer.h
├── Timer.cpp
├── Event.h
└── Event.cpp
```

SPI, Wi-Fi support used by FabGL, PSRAM support, FAT filesystem support, and the standard C file/directory APIs are supplied by ESP32 Arduino core 2.0.11. No separate libraries are required for them.

### 5. Configure the board

Select **Tools → Board → esp32 → ESP32 Dev Module**, then configure:

```text
CPU Frequency:     240MHz (WiFi/BT)
Partition Scheme:  Huge APP (3MB No OTA/1MB SPIFFS)
PSRAM:             Disabled
Upload Speed:      921600 (use a lower speed if uploads are unreliable)
```

Select the serial port belonging to the ESP32-SBC-FabGL. PSRAM must remain disabled in the IDE because the firmware initializes it at runtime without enabling the compiler's PSRAM cache workaround.

### 6. Prepare the SD card

Format a microSD card as FAT32 and copy the `apple2` directory described below to its root. Insert the card into the ESP32-SBC-FabGL before powering or resetting the board.

### 7. Compile and upload

Open the root `EspAppleII.ino`, click **Verify**, and then click **Upload**. After upload, open **Tools → Serial Monitor** and select **115200 baud**.

If Arduino reports a missing header:

- `Timer.h`: install the Jack Christensen Timer library.
- `fabgl.h`: install the Olimex FabGL fork and remove competing FabGL copies.
- ESP32-specific headers: verify that ESP32 Arduino core **2.0.11** and **ESP32 Dev Module** are selected.

## SD-card disk images

Apple II disk images are loaded from the ESP32-SBC-FabGL's microSD card into PSRAM before emulation starts. The emulator does not perform SD-card I/O for every emulated disk access.

Format the card as FAT32 and use this layout:

```text
SD card root/
└── apple2/
    ├── dos33.dsk
    └── disks/
        ├── choplifter.dsk
        ├── galaxian.dsk
        ├── rescue-raiders.dsk
        └── another-game.dsk
```

`/apple2/dos33.dsk` is the default and backward-compatible boot image. Additional images belong in `/apple2/disks/`, which may contain subdirectories.

For the current milestone, every image must be:

- A standard DOS-order `.dsk` image
- Exactly **143,360 bytes**
- Read-only while running

Images with other sizes are skipped and reported over serial. Formats such as `.po`, `.nib`, `.woz`, and 2MG are not currently supported.

## Apple IIe 128K mode

The emulator keeps the Apple II+ 64K profile for older software and can also run as a 6502-based Apple IIe with 128K. Apple ROM code is not distributed with this repository. To enable the IIe profile, provide a legally obtained, unmodified 16K Apple IIe ROM image at:

```text
/apple2/roms/apple2e.rom
```

At the disk selector, press **Tab** to switch between `APPLE II+ 64K` and `APPLE IIE 128K`, then choose the disk with **Enter**. If the ROM is missing or is not exactly 16,384 bytes, IIe mode remains unavailable. The selected machine profile is preserved when reopening the disk selector with **Ctrl+Alt+Delete**.

IIe mode implements the 64K auxiliary bank, `80STORE`, `RAMRD`, `RAMWRT`, `ALTZP`, internal-ROM selection, 80-column text, double low resolution, and double high resolution. The original IIe profile deliberately retains the NMOS 6502 CPU; an Enhanced IIe ROM requiring 65C02 instructions is not currently supported.

For a large archive, generate the index on the computer before copying the archive to the SD card:

```bash
python3 tools/build_disk_index.py "/path/to/apple2/disks"
```

This creates `apple2-index.txt` inside that directory. Copy the complete `apple2` tree to the SD card. The firmware loads this index directly; if it is absent or invalid, it falls back to recursively scanning the directory.

The firmware stores compact catalog records and a shared path-string pool in PSRAM. Up to 4,096 indexed images are supported.

## Debug and release builds

Normal Arduino IDE builds use the debug profile. It enables serial diagnostics, limited CPU and Disk II traces, and stack high-water reporting every ten seconds.

For a quiet release build, define this compiler symbol:

```text
ESPAPPLEII_RELEASE=1
```

With `arduino-cli`, add:

```bash
--build-property compiler.cpp.extra_flags=-DESPAPPLEII_RELEASE=1
```

Release mode removes diagnostic code from hot paths. It does not alter CPU pacing, Disk II rotation timing, video behavior, or disk-index capacity.

Debug builds report lines such as:

```text
[TASK] CPU minimum free stack=...
[TASK] Video minimum free stack=...
```

These are the lowest remaining stack-space values observed since each task started. Both tasks currently reserve 8,192 bytes; test several games and disk-menu operations before reducing them further.

## Startup disk selector

When more than one valid image is available, a VGA disk-selection screen appears at startup.

- **Up/Down:** change selection
- **Enter:** load and boot the selected disk
- **Escape:** select the first entry, normally `dos33.dsk`

With PSRAM, the selector supports up to 4,096 images. Without PSRAM it retains a 32-image fallback catalog. The number of visible rows is calculated from the active VGA canvas height. Type a partial name for substring search; looser fuzzy subsequence matches are shown afterward. Long selected names scroll horizontally.

Press **Ctrl+Alt+Delete** while emulation is running to reopen the disk selector and cold-boot the newly selected drive-1 image.

If only `/apple2/dos33.dsk` is present, it boots immediately without showing the selector.

## Serial diagnostics

Open the serial monitor at 115200 baud. A normal startup resembles:

```text
EspApple II Emulator (ESP32)
[MEM] Initializing onboard PSRAM for disk buffer...
[MEM] PSRAM initialized
[MEM] Disk buffer assigned to onboard PSRAM
[SD] Initializing ESP32-SBC-FabGL SD card...
[SD] Found 4 valid disk image(s)
[SD] Selected /SD/apple2/disks/choplifter.dsk
[SD] Image size: 143360 bytes
[SD] Loaded choplifter.dsk into PSRAM (143360 bytes)
```

Errors are reported for SD initialization, missing images, incorrect image sizes, allocation failures, file-open failures, and short reads. Startup stops before creating the emulator tasks if no disk can be loaded, and an error is displayed over VGA where practical.

## Emulator status and limitations

The project currently:

- Emulates the original Apple II, not later Apple II models
- Emulates two Disk II drives with independently loaded memory-backed images
- Supports cold-boot disk changes from the runtime disk selector
- Treats disk images as read-only, so saved games and disk writes do not persist
- Implements page 1 and page 2 for text, low-resolution, and high-resolution graphics
- Implements mixed graphics/text display mode
- Uses a PS/2 keyboard; joystick emulation is not implemented
- Preserves the existing 6502, video, keyboard, sound, and Disk II behavior

Some copy-protected games or software that relies on unsupported memory/video behavior may not work. Lode Runner was known not to work in the original project and remains an interesting debugging challenge.

## Hardware

The ESP32, introduced in 2016 as a successor to the ESP8266, contains two CPU cores running at up to 240 MHz. This project uses one core for 6502 execution and the other for video refresh and keyboard handling.

The Olimex ESP32-SBC-FabGL provides the ESP32, VGA output, PS/2 keyboard and mouse connectors, audio output, microSD slot, and USB programming/debug connection on one board.

![ESP32-SBC-FabGL front](Imagens/Esp32_Front.jpeg)

![ESP32-SBC-FabGL back](Imagens/Esp32_Back.jpeg)

Graphics and peripheral support use the open-source [FabGL library](http://www.fabglib.org) and the Olimex FabGL fork for ESP32-SBC-FabGL compatibility.

## Videos

- [DOS 3.3](https://www.youtube.com/watch?v=gkJJiDuz0lA)
- [Rescue Raiders](https://youtu.be/1CllMtIGst4)
- [Galaxian](https://youtu.be/dFom_zQjH2I)
- [Choplifter](https://youtu.be/LiarlgUO_FE)
- [CannonBall Blitz](https://youtu.be/a9vT981Lyd8)

![Original startup screen](Imagens/TelaInicial.png)

## License

See [LICENSE](LICENSE) for the project's license terms.

## Supporting the original author

The original author is an independent developer. If you would like to buy him a beer or coffee, his Pix and PayPal address is `fj_souza@hotmail.com`. Otherwise, please enjoy the project, learn from it, and share the knowledge.
