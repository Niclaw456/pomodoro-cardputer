# Pomodoro Timer — M5Stack Cardputer ADV

A simple Pomodoro (focus/break) timer for the M5Stack Cardputer ADV,
built with PlatformIO + Arduino framework + M5Unified/M5Cardputer.

## Features

- 25/5/15-minute focus / short-break / long-break cycle (classic Pomodoro)
  by default, fully customizable in a built-in **Settings** screen
- Circular countdown ring + big digital readout
- Session dots showing progress toward the long break
- Start / pause / resume / reset / skip controls from the keyboard
- **Settings screen** (press `ESC` from the Timer screen) to adjust:
  - Focus / short-break / long-break duration, edited digit-by-digit
  - How many focus sessions happen before a long break (1–8)
  - Speaker volume, with an audible preview tone as you adjust it
- **Settings persist across power-off** — saved to the ESP32's internal
  flash (NVS) via the `Preferences` library, so your durations, cycle
  count, and volume are still there next time you turn the Cardputer on
- Tone feedback on start/pause/finish, plus a 3-2-1 tick near the end of
  each phase
- Mute toggle (also persisted)

## Hardware

- M5Stack **Cardputer ADV** (ESP32-S3 / Stamp-S3A, 1.14" 240×135 ST7789V2
  display, 56-key keyboard)

> **Note on this keyboard's ESC key:** the Cardputer has no dedicated ESC
> key. ESC is `Fn` + `` ` `` (the top-left backtick key). That's the only
> place `Fn` is used anywhere in this app — every other control below is
> a plain key press.

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

### Timer screen

| Key            | Action                                      |
|----------------|----------------------------------------------|
| `SPACE`        | Start / pause / resume the current session   |
| `R`            | Reset the current session to its full length |
| `S`            | Skip to the next session                     |
| `M`            | Mute / unmute sound                          |
| `ESC` (`Fn`+`` ` ``) | Open Settings                          |

### Settings screen — row list

Opening Settings pauses the countdown — your remaining time is exactly
where you left it when you come back. You land here first; pick a row,
then press `ENTER` to actually edit it.

| Key      | Action                                                  |
|----------|------------------------------------------------------------|
| `;`      | Move the selection up                                      |
| `.`      | Move the selection down                                     |
| `ENTER`  | Drill into the selected row to edit it                      |
| `ESC` (`Fn`+`` ` ``) | Save everything to flash and return to the Timer screen, from anywhere |

### Settings screen — editing a row (after `ENTER`)

| Key      | Duration rows (Focus / Short break / Long break) | Cycles / Volume rows |
|----------|----------------------------------------------------|------------------------|
| `,`      | Move the digit cursor left                          | Decrease the value     |
| `/`      | Move the digit cursor right                         | Increase the value     |
| `;`      | Increment the selected digit                        | —                       |
| `.`      | Decrement the selected digit                        | —                       |
| `ENTER`  | Done with this row — back to the row list (stays in Settings) | same |
| `ESC` (`Fn`+`` ` ``) | Save everything and exit to the Timer screen — works even mid-edit | same |

Each duration row shows two boxed digits, `MM`. Use `,`/`/` to move
between the tens-digit and the ones-digit, and `;`/`.` to bump whichever
digit is currently boxed up or down.

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

Most of what you'd want to tweak — focus/break/long-break length, how
many focus sessions happen before a long break, and volume — is now
editable right on the device via the **Settings** screen, and survives
power-off.

The defaults (used the very first time the app runs, before anything's
been saved to flash) and the editable ranges live near the top of
`src/main.cpp`:

```cpp
struct PomodoroConfig {
  uint16_t focusMinutes      = 25;
  uint16_t shortBreakMinutes = 5;
  uint16_t longBreakMinutes  = 15;
  uint8_t  sessionsUntilLong = 4;   // focus sessions before a long break
  uint8_t  volume            = 150; // 0-255
};

static constexpr uint16_t MIN_DURATION_MIN = 1;
static constexpr uint16_t MAX_DURATION_MIN = 99;
static constexpr uint8_t  MIN_CYCLES       = 1;
static constexpr uint8_t  MAX_CYCLES       = 8;
static constexpr uint8_t  MIN_VOLUME       = 0;
static constexpr uint8_t  MAX_VOLUME       = 255;
static constexpr uint8_t  VOLUME_STEP      = 17; // how much ,/ moves volume per press
```

Colors, ring size/position, and screen layout constants (including the
Settings screen's row positions) are also near the top of the file if
you want to restyle it.

### Resetting saved settings back to defaults

Settings live in NVS flash under the namespace `"pomodoro"`, completely
separate from the firmware itself — re-flashing won't clear them. If
you ever want to wipe them and start over from the defaults above, the
simplest way is to temporarily add this to the top of `setup()` and
flash once:

```cpp
Preferences p;
p.begin("pomodoro", false);
p.clear();
p.end();
```

Then remove those three lines and re-flash again so it doesn't wipe
your settings every boot.
