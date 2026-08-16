# EspAppleII Compatibility Roadmap

This document defines how to improve EspAppleII compatibility efficiently without turning development into a series of one-game patches.

The guiding question for every failure should be:

> What real Apple II/IIe hardware behavior is this software exposing that the emulator does not yet model correctly?

A fix should preferably improve an entire subsystem or class of software, not only the title that exposed the problem.

## Current direction

EspAppleII has moved beyond basic Apple II emulation and now includes IIe/Enhanced IIe behavior, auxiliary memory, 80-column/DHR support, multiple machine profiles, SD-backed disk images, writable save overlays, and hardware-specific FabGL video handling.

Recent compatibility work has shown a recurring pattern:

1. A title boots successfully.
2. A crack intro or startup loader works.
3. The title later switches to a more demanding CPU, memory, or Disk II path.
4. The emulator fails farther into execution.
5. Diagnostics expose a missing hardware behavior.

Examples include 65C02 instruction support, language-card/banked-memory behavior, and writable disk support. These are not game-specific features. They are machine-correctness gaps that individual programs happened to reveal.

The development process should therefore shift from **game-driven patching** to **subsystem correctness with games as integration tests**.

---

## Core principles

### 1. Model the machine, not the game

Machine profiles should define the hardware that exists and its behavior:

- Apple II+ 64K
- Apple IIe 128K
- Enhanced Apple IIe 128K

A profile should eventually determine, in one place:

- CPU type
- ROM set
- auxiliary RAM availability
- language-card behavior
- 80-column capability
- double-hi-res capability
- soft-switch behavior
- Disk II controller behavior

Avoid accumulating scattered title-specific workarounds or checks that create a hybrid machine that never existed.

### 2. Keep one emulated timeline

Time-sensitive hardware should derive from the same emulated CPU cycle count.

`TotalCycles` should increasingly become the machine clock for:

- CPU timing
- Disk II rotation and data availability
- speaker timing
- paddle timing
- future timing-sensitive video behavior

Host execution speed should not determine virtual hardware timing.

### 3. Prefer deterministic tests over long game boots

If a game exposes a bug, create or adopt the smallest possible test that reproduces the underlying hardware behavior.

A 200 ms memory-bank test is better than waiting 70 seconds for Carmen Sandiego to reach the same failure.

### 4. Do not optimize an inaccurate model

Correctness comes before shortcuts.

Examples:

- establish correct Disk II behavior before adding aggressive fast-disk shortcuts
- establish correct memory-bank behavior before caching assumptions about visible RAM
- keep hardware-visible behavior stable when adding performance optimizations

### 5. Preserve known-good baselines

When experimenting, keep the previous implementation selectable when practical. This was useful during DHR work and should also be used for disk/controller changes when a new implementation is risky.

---

# Compatibility workstreams

## A. CPU correctness

Goal: CPU behavior should be independently validated before software compatibility is blamed on other subsystems.

Validate separately for:

### NMOS 6502

- documented opcodes
- addressing modes
- flags
- decimal mode
- BRK/RTI
- IRQ/NMI behavior
- stack behavior
- branch behavior
- indirect JMP page-wrap behavior
- page-cross timing where externally relevant

### 65C02 / Enhanced IIe

- BRA
- STZ
- PHX/PHY/PLX/PLY
- `(zp)` addressing
- BIT variants
- INC/DEC accumulator
- JMP indexed-indirect behavior
- decimal-mode flag differences
- 65C02-specific NOP behavior as needed

### Recommended reference

**Klaus Dormann 6502/65C02 functional tests**  
https://github.com/Klaus2m5/6502_65C02_functional_tests

Use these as a dedicated CPU validation target rather than waiting for games to expose instruction bugs.

---

## B. Apple IIe memory and soft-switch correctness

This should become one of the highest-priority validation areas because IIe software depends heavily on bank switching.

Build a deterministic test path for:

- MAIN vs AUX RAM
- RAMRD
- RAMWRT
- ALTZP
- 80STORE
- PAGE2
- HIRES
- language-card bank 1 / bank 2
- language-card read enable
- language-card write enable
- ROM vs RAM visibility at `$D000-$FFFF`
- `$C000-$CFFF` I/O/ROM behavior
- alternate zero page and stack
- transitions between combinations of the above

For each switch combination:

1. write distinct sentinel values
2. change switch state
3. read values back
4. verify the expected physical bank was accessed

### Recommended reference/test suite

**a2audit**  
https://zellyn.com/a2audit/

This project exists specifically to identify software-visible differences between real Apple II-family hardware and emulators. It already includes language-card, main/auxiliary-memory, soft-switch, and Cxxx tests.

A major milestone for EspAppleII should be passing the relevant a2audit tests for each supported machine profile.

---

## C. Disk II architecture

Disk handling should be separated conceptually into distinct layers.

### Storage layer

Responsible for:

- SD mounting
- filesystem access
- disk browser/index
- loading and saving host files
- save-overlay handling

### Disk-image layer

Responsible for understanding image formats:

- DSK / DO
- PO
- NIB
- later: WOZ

This layer should translate a host file into a representation the virtual drive can use.

### Virtual drive layer

Responsible for physical-drive state:

- mounted image
- current half-track
- motor state
- rotational position
- write protection
- dirty state

### Disk II controller layer

Responsible for `$C0E0-$C0EF` behavior:

- stepper phases
- drive select
- motor state
- Q6/Q7 state
- data latch
- read/write behavior

The controller should not care whether the mounted host file originated as `.dsk`, `.po`, `.nib`, or eventually `.woz`.

---

## D. Disk II timing correctness

This is especially important for commercial games and custom loaders.

A boot sector or crack intro working does not prove the Disk II implementation is correct. Many titles later replace DOS/RWTS with their own low-level loader.

Disk rotation should ultimately be derived from emulated time:

```text
TotalCycles
    -> elapsed virtual time
    -> disk rotational position
    -> current nibble/bit under the head
    -> data latch
```

Avoid models where repeated reads themselves implicitly advance the disk in ways real hardware would not.

Important behaviors to verify:

- approximately 300 RPM rotational model
- persistent rotational position while stepping tracks
- half-track movement
- track transitions without resetting rotational phase
- data latch semantics
- Q6/Q7 transitions
- motor-on/motor-off behavior
- timing of repeated `$C0EC` reads
- read behavior across track wrap
- write transitions

Do not involve SD-card I/O in live virtual-disk timing. Mounted media should operate from RAM/PSRAM.

---

## E. Disk image support

Current `.dsk` success does not guarantee all sector images are interpreted correctly.

Priority:

1. identify/support DOS-order DSK/DO explicitly
2. identify/support ProDOS-order PO explicitly
3. add NIB support
4. consider WOZ when controller fidelity is ready for it

A 143,360-byte file can still be interpreted with the wrong sector ordering. Treat extension and/or format detection as part of the image layer rather than assuming every image is equivalent.

NIB support is particularly attractive because it moves EspAppleII closer to the representation consumed by the Disk II controller and preserves more track-level information than a sector image.

WOZ should be treated as a later compatibility milestone for software depending on nonstandard tracks, weak bits, timing information, or copy protection.

---

# Diagnostics architecture

## Replace continuous debug spam with a structured trace ring

Serial printing can materially perturb emulator timing. Prefer capturing compact events in memory and dumping them only on request or failure.

Suggested event classes:

```text
CPU_EXCEPTION
CPU_BRANCH_OR_JUMP
MEMORY_BANK_CHANGE
SOFTSWITCH_CHANGE
DISK_IO
DISK_HEAD_MOVE
DISK_TRACK_BUILD
DISK_MOTOR
MACHINE_PROFILE
```

Each event should contain only the minimum information required, for example:

```text
cycle
PC
event type
small event-specific payload
```

Keep the last few hundred or thousand events.

## Automatic failure snapshots

Detect obvious pathological states such as:

- repeating PC pattern
- repeated BRK loop
- execution through long runs of `$00` or `$FF`
- suspicious indirect JMP target
- stack collapse
- disk motor active with a repeating loader loop
- excessive reads with no useful forward progress

When detected, dump a single structured compatibility snapshot containing:

- machine profile
- CPU registers
- PC/SP/status
- recent instruction history
- memory-switch state
- language-card state
- recent soft-switch events
- recent Disk II events
- current drive/track/rotation state

The goal is for a Codex/debugging session to receive a useful failure report rather than several thousand lines of timing-altering serial output.

---

# Developer snapshots / save states

A developer-only machine snapshot can greatly shorten compatibility debugging.

Capture enough state to reproduce a failure:

- main RAM
- auxiliary RAM
- language-card memory/state
- CPU registers
- machine profile
- soft switches
- drive/controller state
- current track/rotational position

Workflow:

```text
boot game normally
    -> reach shortly before failure
    -> save diagnostic snapshot

future development cycle
    -> load snapshot
    -> reproduce failure almost immediately
```

This does not need to begin as an end-user save-state feature. Its first purpose is shortening emulator development feedback loops.

---

# Compatibility corpus

Maintain a small set of representative programs rather than testing random titles after every change.

Suggested categories:

| Test | Purpose |
|---|---|
| Apple/DOS 3.3 system disk | baseline boot + Disk II + DOS |
| ProDOS | CPU, disk, filesystem, memory behavior |
| a2audit | Apple II/IIe memory and soft-switch correctness |
| 6502 functional test | NMOS CPU correctness |
| 65C02 functional test | Enhanced IIe CPU correctness |
| Carmen Sandiego | IIe memory + real commercial loader + writable progress |
| Prince of Persia | demanding commercial/custom disk loader |
| Leisure Suit Larry | Sierra loader, memory, disk behavior |
| Lode Runner | mainstream game baseline |
| Oregon Trail | mainstream compatibility baseline |
| one 80-column application | aux-memory/text path |
| one DHR program/game | DHR path |

Track checkpoints rather than only PASS/FAIL:

| Program | Boot | Intro | Main load | Gameplay | Disk swap | Save | Reload |
|---|---|---|---|---|---|---|---|
| Example | PASS | PASS | FAIL | - | - | - | - |

A new subsystem change should not silently regress previously passed checkpoints.

---

# Workflow for a compatibility failure

When a title fails:

## 1. Classify the symptom

Examples:

- continuous disk activity / retry loop
- no disk activity and frozen CPU
- reboot
- BRK storm
- invalid opcode
- corrupt video only
- game runs but save fails
- crash after machine-profile transition

## 2. Capture a failure snapshot

Do not immediately patch the title.

## 3. Identify the subsystem

Candidate buckets:

- CPU
- memory/banking
- ROM/profile
- Disk II controller
- disk image representation
- video
- input
- storage/save behavior

## 4. Reproduce with the smallest available test

Prefer existing test suites or create a minimal hardware test.

## 5. Compare against a known-good implementation

Use mature emulators as behavioral references.

## 6. Fix the hardware model

Avoid filename checks, title-specific PC checks, or compatibility hacks unless there is clear evidence the real machine itself behaves differently for that condition.

## 7. Add a regression test

Every important compatibility fix should leave behind a smaller test, assertion, trace expectation, or compatibility-corpus checkpoint.

---

# Useful reference projects

These projects should be treated as implementation references and behavioral oracles. Do not blindly copy large subsystems. Identify the behavior EspAppleII needs, understand how mature projects model it, then adapt appropriately for ESP32 memory/performance constraints.

## AppleWin

https://github.com/AppleWin/AppleWin

Use for:

- mature Apple II/IIe behavior
- Disk II controller behavior
- CPU/memory edge cases
- video/artifact-color reference
- disk image handling

Apple2JS itself describes AppleWin's source as a particularly useful reference.

## Apple2JS

https://github.com/whscullin/apple2js

Use for:

- readable Apple IIe implementation
- memory/soft-switch behavior
- Disk II concepts
- DHR/video behavior
- examples of using a2audit to tighten memory emulation

Its architecture is often easier to read than older emulator code and can help explain behavior before looking at more complicated implementations.

## izapple2

https://github.com/ivanizag/izapple2

Use for:

- clean modern subsystem separation
- machine configurations
- tracing strategy
- disk/storage architecture
- Apple IIe/Enhanced IIe behavior
- automated a2audit integration

It is particularly interesting because it reports passing a2audit as II+, IIe, and Enhanced IIe and includes tracing for CPU execution and soft-switch activity.

## CLK

https://github.com/TomHarte/CLK

Use for:

- timing-oriented emulation architecture
- cycle/bus-level thinking
- Apple II video/composite behavior
- avoiding host-speed-dependent hardware behavior

This is a strong reference when deciding how accurately a timing-sensitive peripheral should be modeled.

## MiSTer Apple II

https://github.com/MiSTer-devel/Apple-II_MiSTer

Use for:

- hardware-oriented Apple II behavior
- DSK/DO/PO/NIB handling
- Disk II representation ideas
- language-card/aux-memory features

Its documentation also explicitly notes the ambiguity and messiness of Apple II disk-image sector ordering.

## a2audit

https://zellyn.com/a2audit/

Use as a primary machine-correctness target for:

- language card
- main/aux memory switching
- soft switches
- Cxxx behavior
- future Apple IIe hardware edge cases

## Klaus Dormann CPU tests

https://github.com/Klaus2m5/6502_65C02_functional_tests

Use as the primary standalone CPU correctness suite for NMOS 6502 and 65C02 behavior.

---

# Suggested implementation order

## Current validation baseline

- Built-in isolated CPU smoke harness: **29/29 PASS on ESP32 hardware**
- NMOS 6502 coverage: basic loads/flags, binary ADC, relative branch,
  JSR/RTS, indirect-JMP page wrap, decimal ADC/SBC
- 65C02 coverage: BRA, STZ, PHX/PLX, `(zp)`, corrected indirect JMP,
  decimal ADC/SBC
- Klaus Dormann flat-memory SD runner: **2/2 PASS on ESP32 hardware**
- NMOS 6502 functional test: **PASS**, 30,646,184 instructions and
  96,241,388 cycles in 48,635 ms
- 65C02 extended-opcode test: **PASS**, 21,986,993 instructions and
  66,907,153 cycles in 36,059 ms
- CPU baseline established on hardware. Keep the smoke and Klaus runners as
  opt-in regression targets while normal emulation remains the default build.
- Built-in isolated IIe banking harness: **8/8 PASS on ESP32 hardware**
- IIe coverage: MAIN/AUX `RAMRD`/`RAMWRT`, `ALTZP` zero page/stack,
  `80STORE` text/HGR routing, language-card banks/ROM/write locking,
  auxiliary language-card selection, and soft-switch status reads
- Next IIe memory milestone: run the relevant a2audit suites and add Cxxx
  internal/slot-ROM latch coverage
- Disk II controller/write-path validation harness: **7/7 controller checks
  PASS on ESP32 hardware**. Initial coverage is motor state, write-protect sensing,
  cycle-derived rotation, stepper movement, Q6/Q7 write gating, protected-media
  writes, and 6-and-2 nibble encode/decode round trips. The first harness run
  also exposed and corrected a diagnostic-only 256-versus-258-byte padding
  allocation around the legacy codec. SD persistence is kept separate so
  controller failures cannot be confused with filesystem latency.
- Isolated SD save-overlay harness: **9/9 PASS on ESP32 hardware**.
  It creates a dedicated deterministic image under `/SD/apple2/tests`, runs
  the production copy-on-write creation/load path, updates and flushes one
  sector, reopens it for byte-for-byte verification, checks both neighboring
  bytes, and removes its own base/overlay files. It never opens a game image.
  Measured hardware latency was 3,249 ms to create the base test image,
  10,506 ms for production overlay creation plus buffered loading, 116 ms for
  one targeted 256-byte memory update/flush, and 93 ms to reopen/read/verify.
  Data integrity is established; the remaining Carmen save investigation
  should distinguish long synchronous host I/O from an actual CPU/controller
  deadlock and should avoid treating an intentional host pause as a CPU stall.
- Host-I/O correlation diagnostics are now integrated for normal-emulation
  testing. Transaction IDs and elapsed times cover overlay creation, image
  loading, track flushes, and sector persistence. Q7 write sessions report
  their first data byte, byte count, track, rotational index, cycle, and PC;
  subsequent flushes include the most recent write-session identity. The task
  monitor reports active filesystem work as `CPU HOST-IO-WAIT`, and a flush
  performed by the CPU task emits an explicit `CPU resumed` event after the
  triggering 6502 instruction completes.
- Carmen Sandiego v2.0 (4am crack) exposes an NMOS-profile dependency: its
  loaded language-card code uses `$80 operand` as a two-byte undocumented NOP.
  Enhanced IIe/65C02 hardware interprets `$80` as `BRA`, producing the observed
  backward branch into a BRK trap. NMOS undefined-NOP operand lengths are now
  modeled and regression-tested; this particular image should be run with the
  non-enhanced `APPLE IIE 128K` profile rather than title-specific 65C02 hacks.
  The regular Apple IIe 128K profile is therefore the normal startup default;
  Enhanced IIe and Apple II+ remain selectable from the disk menu.
- A clean Carmen load also executes the stable undocumented NMOS `$07` SLO
  instruction. Treating it as a one-byte undefined opcode consumed its
  operand as the next instruction and sent execution into data. The NMOS core
  now implements the stable SLO, RLA, SRE, RRA, SAX, LAX, DCP, and ISC opcode
  families across their documented addressing forms, plus ANC, ALR, ARR,
  AXS/SBX, and unofficial immediate SBC. The CPU smoke harness contains an
  isolated semantic regression for every family. Enhanced IIe retains its
  distinct 65C02 interpretations; unstable bus-dependent opcodes and JAM/KIL
  remain deliberately unsupported rather than being assigned invented
  behavior.
  The expanded smoke suite, including all of these stable undocumented NMOS
  families and the existing 65C02 checks, passed **29/29 on ESP32 hardware**.
- Carmen also probes/selects an empty drive 2 during detection. This probe is
  not evidence that its later insertion prompt expects media in drive 2; a
  hardware trace with Side B mounted there showed subsequent loading remained
  on drive 1. Empty media was incorrectly
  mounted as a 143,360-byte DOS image and failed reads were nibblized into a
  synthetic zero-filled disk. Empty drives now remain size-zero, unknown-type,
  write-protected media and expose an all-`$FF` no-prologue track stream so
  normal drive-detection code can time out cleanly. A null active pointer is
  no longer reported as corruption when the mounted-media guard is also null.
- Runtime drive-2 insertion is available through `Ctrl+Alt+D`. It reuses the
  indexed disk selector and production copy-on-write loader, assigns the
  second PSRAM disk buffer, identifies the inserted image format, and resumes
  without resetting CPU, RAM, video, machine profile, or drive-1 media.
- Custom-loader diagnostics now retain the latest 48 Disk II controller
  accesses in an internal-RAM ring without serial output on the live path.
  Three consecutive ten-second, motor-on, read-heavy intervals on one track
  emit one `[DISK-SEARCH]` snapshot with Q6/Q7, latch, rotational index,
  access-cycle delta, PC, and returned-nibble history. This targets loaders
  such as Carmen Sandiego without filename or PC-specific behavior.
- Disk II latch/rotation behavior was compared against AppleWin and Apple2JS.
  Track rebuilds after stepping preserve rotational index instead of snapping
  to nibble zero. A first-pass approximation exposed guessed partial bits
  between 32-cycle nibble boundaries; Carmen subsequently loaded corrupt code
  into the language card and trapped after `JSR $D20C`. That approximation was
  removed. Until a bit-accurate sequencer is implemented, intermediate reads
  retain the earlier zero-until-complete-nibble behavior. The isolated Disk II
  regression now records this compatibility behavior explicitly.

## Phase 1: Correctness harness

1. build and run an isolated CPU smoke-test harness
2. integrate/run 6502 functional tests
3. integrate/run 65C02 functional tests
4. build a deterministic IIe memory/soft-switch test
5. integrate/run a2audit
6. structured trace ring
7. automatic failure snapshot
8. record baseline compatibility matrix

## Phase 2: Machine model cleanup

1. centralize machine-profile capabilities
2. audit memory/soft-switch implementation
3. eliminate contradictory or duplicated mode logic
4. add targeted regression tests for every discovered banking bug

## Phase 3: Disk II cleanup

1. separate storage, image, drive, and controller responsibilities
2. instrument controller timing without serial spam
3. tie rotational behavior to `TotalCycles`
4. verify latch/Q6/Q7/head behavior
5. add DO/PO handling
6. add NIB support
7. consider WOZ later

## Phase 4: Performance

Only after correctness baselines are stable:

- pre-nibblized track cache in PSRAM
- lower-cost disk-image mounting
- faster catalog/index startup
- targeted rendering and memory-access optimizations
- optional fast-disk mode that preserves normal accurate mode

---

# Definition of progress

Do not measure compatibility progress only by the number of games that boot.

Prefer measurable milestones such as:

- CPU functional suite passes
- Enhanced IIe CPU suite passes
- a2audit relevant tests pass
- Disk II controller trace matches known-good behavior for representative loaders
- compatibility corpus checkpoints improve without regressions
- failures produce deterministic snapshots instead of requiring manual serial archaeology
- fixes are covered by subsystem regression tests

A game getting farther is useful evidence. A hardware behavior becoming correct and tested is the real improvement.
