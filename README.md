# Pomodoro Timer — M5Stack Cardputer ADV

A simple Pomodoro (focus/break) timer for the M5Stack Cardputer ADV,
built with PlatformIO + Arduino framework + M5Unified/M5Cardputer.

## Features

- 25/5/15-minute focus / short-break / long-break cycle (classic Pomodoro),
  configurable in code via `PomodoroConfig` in `src/main.cpp`
- Circular countdown ring + big digital readout
- Session dots showing progress toward the long break
- Start / pause / resume / reset / skip controls from the keyboard
- Adjustable focus length on the fly (`Fn` + `,` / `.`)
- Tone feedback on start/pause/finish, plus a 3-2-1 tick near the end of
  each phase
- Mute toggle

## Hardware

- M5Stack **Cardputer ADV** (ESP32-S3 / Stamp-S3A, 1.14" 240×135 ST7789V2
  display, 56-key keyboard)

## Setup (VS Code + PlatformIO)

1. Install the [PlatformIO IDE extension](https://platformio.org/install/ide?install=vscode)
   in VS Code if you haven't already.
2. Unzip this project and open the folder in VS Code
   (`File > Open Folder...`).
3. PlatformIO will detect `platformio.ini` automatically and offer to
   install the project's dependencies (M5Unified, M5GFX, M5Cardputer) —
   accept, or run it manually from the PlatformIO sidebar.
4. Plug in the Cardputer ADV over USB-C.
5. Build & upload:
   - PlatformIO sidebar → **Upload**, or
   - Terminal: `pio run -t upload`
6. Open the serial monitor if you want logs: `pio device monitor`

If `pio run -t upload` can't find the device, set the side power switch
to **OFF**, hold the **G0** button, plug in USB, then release G0 — this
forces the board into download mode.

## Controls

| Key            | Action                                      |
|----------------|----------------------------------------------|
| `SPACE`        | Start / pause / resume the current session   |
| `R`            | Reset the current session to its full length |
| `S`            | Skip to the next session                     |
| `Fn` + `,`     | Decrease focus length (while idle)           |
| `Fn` + `.`     | Increase focus length (while idle)           |
| `` ` ``        | Mute / unmute sound                          |

## Notes on the `platformio.ini` board choice

This project targets the Cardputer ADV using the generic
`esp32-s3-devkitc-1` board definition plus the USB-CDC build flags that
M5Stack's own docs use for the (closely related) original Cardputer.
This compiles cleanly on any standard PlatformIO `espressif32` install.

If your installed M5Stack board package already exposes a dedicated
`m5stack-stamps3` board entry (the Cardputer ADV's core module), you can
switch to it in `platformio.ini`:

```ini
board = m5stack-stamps3
```

Either should work; the devkitc-1 + flags combo is just the safer
default since it doesn't depend on having a specific extra board
package installed.

## Customizing

All the timing/behavior knobs live at the top of `src/main.cpp` in the
`PomodoroConfig` struct:

```cpp
struct PomodoroConfig {
  uint16_t focusMinutes      = 25;
  uint16_t shortBreakMinutes = 5;
  uint16_t longBreakMinutes  = 15;
  uint8_t  sessionsUntilLong = 4;   // long break after this many focus sessions
  uint8_t  minFocusMinutes   = 5;
  uint8_t  maxFocusMinutes   = 60;
};
```

Colors, ring size/position, and screen layout constants are also near
the top of the file if you want to restyle it.
