🍅 Pomodoro Timer for M5Stack Cardputer ADV

A fully-featured, portable Pomodoro timer built specifically for the M5Stack
Cardputer ADV (ESP32-S3). This app helps you manage your productivity using the
Pomodoro Technique, featuring customizable intervals, sound alerts, visual
progress rings, and settings that persist across reboots.

✨ Features

  - Customizable Timers: Set your own durations for Focus, Short Breaks, and
    Long Breaks (1 to 99 minutes).
  - Cycle Tracking: Automatically tracks your focus sessions and triggers a Long
    Break after a set number of cycles.
  - Auto-Start Mode: Optionally transition seamlessly between Focus and Break
    phases without needing to press start.
  - Countdown Alerts: Emits a ticking sound during the final 10 seconds of any
    phase to warn you it's ending.
  - Persistent Settings: Your durations, volume, and preferences are saved to
    the ESP32's NVS flash memory—no need to configure them every time you turn
    it on.
  - Visual UI: Smooth circular progress ring and intuitive menus.

🛠️ Hardware Requirements

  - M5Stack Cardputer (Stamp-S3A, 1.14" ST7789V2 LCD, 56-key TCA8418 keyboard,
    Speaker).

🚀 Installation & Flashing

This project is configured for PlatformIO (VS Code).

1.  Create a new PlatformIO project for your Cardputer.
2.  Ensure you have the M5Cardputer library installed in your platformio.ini.
3.  Paste the code into your main.cpp.
4.  Build and upload using the PlatformIO interface or terminal:
    pio run -t upload

🎮 Controls & Usage

⚠️ IMPORTANT NOTE ON THE ESC KEY: The Cardputer keyboard does not have a
dedicated ESC key. To trigger ESC, press Fn + ` (the top-left key on the
keyboard).

⏱️ Timer Screen (Main)

| Key                  | Action                                        |
| :------------------- | :-------------------------------------------- |
| **`[SPACE]`**        | Start / Pause / Resume the timer              |
| **`[R]`**            | Reset the current timer back to full duration |
| **`[S]`**            | Skip to the next phase (Focus ↔ Break)        |
| **`[M]`**            | Mute / Unmute the speaker temporarily         |
| **`[Fn]` + `` ` ``** | Open the **Settings Menu**                    |

⚙️ Settings Screen (List Navigation)

| Key                  | Action                                     |
| :------------------- | :----------------------------------------- |
| **`[;]` / `[.]`**    | Move selection cursor Up / Down            |
| **`[ENTER]`**        | Edit the currently selected row            |
| **`[Fn]` + `` ` ``** | **Save and Exit** back to the Timer screen |

✏️ Settings Screen (Editing a Row)

Once you press [ENTER] on a row, you enter Edit Mode:

| Key                  | Action                                                                                                                               |
| :------------------- | :----------------------------------------------------------------------------------------------------------------------------------- |
| **`[,]` / `[/]`**    | **Durations:** Move digit cursor Left/Right.<br>**Other Settings:** Decrease/Increase value (Volume, Cycles) or Toggle (Auto-Start). |
| **`[;]` / `[.]`**    | **Durations Only:** Increment/Decrement the selected digit (+1, -1, +10, -10).                                                       |
| **`[ENTER]`**        | Confirm changes and return to the Row List.                                                                                          |
| **`[Fn]` + `` ` ``** | Save everything immediately and exit to Timer.                                                                                       |

📋 Settings Menu Options

1.  Focus: Duration of your main work session (MM).
2.  Short break: Duration of your standard break (MM).
3.  Long break: Duration of your extended break (MM).
4.  Cycles/long break: How many Focus sessions must be completed before you earn
    a Long Break.
5.  Volume: Adjust the speaker volume visually via a progress bar.
6.  Auto Start: Toggle ON or OFF. When ON, the next timer automatically begins
    when the current one ends.

🧠 Code Architecture & Function Breakdown

Below is an overview of the internal functions powering the app, useful if you
want to modify or extend the code.

Core Lifecycle

  - setup(): Initializes the M5Cardputer, configures the display
    (rotation/brightness), loads saved settings from flash, prepares the canvas
    sprite, and starts the first Focus phase.
  - loop(): The main application loop. Handles time delta calculations to
    decrease the timer, triggers the 10-second countdown tick, processes
    keyboard inputs, and calls renderFrame().

Phase Management

  - startPhase(Phase p, bool autoStart): Sets up the specified phase (Focus,
    Short Break, or Long Break). Calculates total milliseconds based on user
    configuration and determines if the timer should begin ticking immediately
    (autoStart).
  - advancePhase(bool autoStart): Determines the next logical phase. If a Focus
    phase ends, it increments the completed cycle count and decides whether to
    trigger a Short or Long Break. If a Break ends, it loops back to Focus.

Input Handling

  - handleTimerInput(KeysState): Processes keystrokes while on the main Timer
    screen (Space, R, S, M).
  - handleSettingsInput(KeysState): Complex state machine handling input for the
    Settings screen. It routes inputs differently depending on whether the user
    is browsing the list (SettingsMode::List) or actively changing a value
    (SettingsMode::Editing).

Settings & Persistence

  - enterSettings(): Pauses the timer state, creates a temporary draftCfg of the
    user's settings, and switches the UI to the Settings screen.
  - confirmSettings(): Triggered by pressing ESC. Copies the draftCfg into the
    active cfg, writes changes to NVS flash, applies the new volume, and returns
    to the Timer screen.
  - adjustSelectedDigit(int delta): Helper function for the settings UI that
    allows users to edit time durations digit-by-digit (tens place vs ones
    place) while clamping values between 1 and 99.
  - loadSettings() / saveSettings(): Interfaces with the ESP32's Preferences
    library to read/write settings to the non-volatile storage (NVS) pomodoro
    namespace.

Sound (Audio Cues)

  - applyVolumeToSpeaker(): Pushes the configured volume to the hardware
    speaker.
  - playStartTone() / playPauseTone(): Brief functional boops to confirm
    play/pause actions.
  - playTickTone(): A short high-pitched click played every second during the
    last 10 seconds of a phase.
  - playNavTone(): UI feedback sound when moving through the settings menu.
  - playPhaseCompleteTone(): A rewarding 3-note ascending chime played when a
    timer hits zero.

UI & Rendering

  - renderFrame(): The main drawing orchestrator. Clears the background, calls
    either the timer or settings render function, and pushes the completed
    sprite to the display to prevent flickering.
  - renderTimerScreen(): Draws the circular progress UI, the centered countdown
    text (MM:SS), the side info panel (phase name, state, cycle dots), and the
    bottom keyboard shortcut helper.
  - drawProgressRing(float fraction, uint16_t color): Calculates and draws the
    filled wedge of the timer circle using M5GFX arc drawing features.
  - renderSettingsScreen(): Iterates through the SettingRow enum to draw the
    menu. Handles highlighting the currently selected row and drawing the cursor
    block when actively editing a value.
  - drawEditableMinutes(...): A specialized drawing function for the MM duration
    fields. It draws a block behind the specific digit (tens or ones) the user
    is currently editing to provide clear visual feedback.
