/*
  Pomodoro Timer for M5Stack Cardputer ADV
  -----------------------------------------
  Hardware: ESP32-S3 (Stamp-S3A), 1.14" 240x135 ST7789V2 LCD,
            56-key TCA8418 keyboard, ES8311/NS4150B speaker.

  NOTE: This Cardputer keyboard has no dedicated arrow keys. Arrows are
  the Fn layer over the punctuation row: Fn+;=Up  Fn+,=Left
  Fn+.=Down  Fn+/=Right. That's what "arrow keys" means below.

  --- Timer screen ---
    [SPACE]      Start / Pause the current session
    [R]          Reset current session back to full time
    [S]          Skip to the next session (focus <-> break)
    [P]          Open Settings
    [`]          Mute / unmute sound

  --- Settings screen ---
    [Fn]+[;]/[.] Move selection Up / Down between rows
    [Fn]+[,]/[/] Move digit cursor Left / Right (duration rows),
                 or adjust value Left/Right (volume & cycle-count rows)
    [Fn]+[;]/[.] also increments/decrements the selected digit on
                 duration rows (Up = +1, Down = -1)
    [ENTER]      Confirm / exit Settings (saves to flash)
    [`]          Cancel — exit Settings without saving changes

  Settings (focus/break/long-break minutes, sessions-before-long-break,
  volume) persist across power-off using the ESP32's NVS flash via the
  Preferences library.

  Build/flash with PlatformIO (VS Code):
    pio run -t upload
    pio device monitor
*/

#include <M5Cardputer.h>
#include <Preferences.h>

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

struct PomodoroConfig {
  uint16_t focusMinutes      = 25;
  uint16_t shortBreakMinutes = 5;
  uint16_t longBreakMinutes  = 15;
  uint8_t  sessionsUntilLong = 4;   // focus sessions completed before a long break
  uint8_t  volume            = 150; // 0-255, passed straight to M5.Speaker.setVolume
};

// Bounds for everything editable in the Settings screen.
static constexpr uint16_t MIN_DURATION_MIN = 1;   // 1 minute
static constexpr uint16_t MAX_DURATION_MIN = 99;  // 99:59 fits MM:SS digit editor
static constexpr uint8_t  MIN_CYCLES       = 1;
static constexpr uint8_t  MAX_CYCLES       = 8;
static constexpr uint8_t  MIN_VOLUME       = 0;
static constexpr uint8_t  MAX_VOLUME       = 255;
static constexpr uint8_t  VOLUME_STEP      = 17; // ~15 steps end-to-end

static PomodoroConfig cfg;

// NVS namespace/key names (<=15 chars per the Preferences library limit)
static constexpr char NVS_NAMESPACE[] = "pomodoro";
static constexpr char NVS_KEY_FOCUS[]   = "focusMin";
static constexpr char NVS_KEY_SHORT[]   = "shortMin";
static constexpr char NVS_KEY_LONG[]    = "longMin";
static constexpr char NVS_KEY_CYCLES[]  = "cycles";
static constexpr char NVS_KEY_VOLUME[]  = "volume";
static constexpr char NVS_KEY_SOUNDON[] = "soundOn";

static Preferences prefs;

enum class Phase : uint8_t {
  Focus,
  ShortBreak,
  LongBreak
};

enum class RunState : uint8_t {
  Idle,     // configured but not started for this phase
  Running,
  Paused,
  Finished  // time hit zero, waiting for user to move on
};

enum class AppScreen : uint8_t {
  Timer,
  Settings
};

// Each row in the Settings screen. Duration rows are edited digit-by-digit;
// Cycles and Volume are edited as a single value with Left/Right.
enum class SettingRow : uint8_t {
  FocusDuration,
  ShortBreakDuration,
  LongBreakDuration,
  Cycles,
  Volume,
  Count // sentinel, must stay last
};

// ---------------------------------------------------------------------------
// App state
// ---------------------------------------------------------------------------

static Phase    currentPhase   = Phase::Focus;
static RunState runState       = RunState::Idle;

static uint32_t phaseTotalMs   = 0;     // length of current phase in ms
static uint32_t phaseRemainMs  = 0;     // remaining ms in current phase
static uint32_t lastTickMs     = 0;     // millis() at last loop update

static uint8_t  completedFocusSessions = 0; // toward long-break cadence

static bool     soundEnabled   = true;

// ---------------------------------------------------------------------------
// Settings screen state
// ---------------------------------------------------------------------------

static AppScreen currentScreen = AppScreen::Timer;
static SettingRow selectedRow  = SettingRow::FocusDuration;

// Digit cursor for the duration rows: 0 = tens-of-minutes digit,
// 1 = ones-of-minutes digit. Durations are whole minutes in this app
// (the timer always displays/runs MM:SS, but the *settable* unit is
// minutes), so the editor only has these two digits to move between.
static uint8_t  digitCursor = 0;
static constexpr uint8_t DURATION_DIGIT_COUNT = 2;

// Working copy of the config that Settings edits live-on; only written
// back to `cfg` (and flash) when the user confirms with ENTER. This is
// what lets `` ` `` cancel out of Settings without side effects.
static PomodoroConfig draftCfg;

// ---------------------------------------------------------------------------
// Display layout constants (240x135, landscape via setRotation(1))
// ---------------------------------------------------------------------------

static constexpr int SCREEN_W = 240;
static constexpr int SCREEN_H = 135;

static constexpr int RING_CX = 70;
static constexpr int RING_CY = 67;
static constexpr int RING_R  = 52;
static constexpr int RING_THICK = 10;

static constexpr uint16_t COL_BG       = 0x0000; // black
static constexpr uint16_t COL_FOCUS    = 0xF800; // red-ish
static constexpr uint16_t COL_SHORT    = 0x07E0; // green
static constexpr uint16_t COL_LONG     = 0x001F; // blue
static constexpr uint16_t COL_TRACK    = 0x39C7; // dim grey
static constexpr uint16_t COL_TEXT     = 0xFFFF; // white
static constexpr uint16_t COL_DIM      = 0x8410; // grey text

static M5Canvas canvas(&M5Cardputer.Display);

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

static void startPhase(Phase p, bool autoStart);
static void advancePhase();
static void renderFrame();
static void renderTimerScreen();
static void renderSettingsScreen();
static void playStartTone();
static void playPauseTone();
static void playPhaseCompleteTone();
static void playTickTone();
static void playNavTone();
static uint16_t colorForPhase(Phase p);
static const char* labelForPhase(Phase p);

static void loadSettings();
static void saveSettings();
static void enterSettings();
static void confirmSettings();
static void cancelSettings();
static void handleTimerInput(bool fnHeld, const Keyboard_Class::KeysState &st);
static void handleSettingsInput(bool fnHeld, const Keyboard_Class::KeysState &st);
static uint16_t* durationFieldForRow(SettingRow row); // points into draftCfg
static void adjustSelectedDigit(int delta);
static void applyVolumeToSpeaker();

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void setup() {
  auto mcfg = M5.config();
  M5Cardputer.begin(mcfg, true /* enableKeyboard */);

  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setBrightness(80);
  M5Cardputer.Display.fillScreen(COL_BG);

  loadSettings(); // populates cfg + soundEnabled from flash, or defaults
  applyVolumeToSpeaker();

  canvas.setColorDepth(8);
  canvas.createSprite(SCREEN_W, SCREEN_H);
  canvas.setTextDatum(top_left);

  startPhase(Phase::Focus, /*autoStart=*/false);
  lastTickMs = millis();
}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------

void loop() {
  M5Cardputer.update();

  uint32_t now = millis();
  uint32_t dt  = now - lastTickMs;
  lastTickMs   = now;

  // ---- timer tick (only while the Timer screen is showing & running) ----
  // Opening Settings effectively pauses the countdown — your remaining
  // time is exactly where you left it when you come back. This avoids
  // a focus session silently burning down while you're adjusting volume.
  if (currentScreen == AppScreen::Timer && runState == RunState::Running) {
    if (dt >= phaseRemainMs) {
      phaseRemainMs = 0;
      runState = RunState::Finished;
      playPhaseCompleteTone();
    } else {
      uint32_t prevSec = (phaseRemainMs + 999) / 1000;
      phaseRemainMs -= dt;
      uint32_t nowSec = (phaseRemainMs + 999) / 1000;
      // tick sound on the last 3 seconds of the phase
      if (nowSec != prevSec && nowSec <= 3 && nowSec > 0) {
        playTickTone();
      }
    }
  }

  // ---- input ----
  if (M5Cardputer.Keyboard.isChange()) {
    if (M5Cardputer.Keyboard.isPressed()) {
      Keyboard_Class::KeysState st = M5Cardputer.Keyboard.keysState();
      bool fnHeld = st.fn;

      if (currentScreen == AppScreen::Timer) {
        handleTimerInput(fnHeld, st);
      } else {
        handleSettingsInput(fnHeld, st);
      }
    }
  }

  renderFrame();

  delay(5); // keep keyboard FIFO from overflowing; matches TCA8418 polling guidance
}

// ---------------------------------------------------------------------------
// Phase management
// ---------------------------------------------------------------------------

static uint32_t minutesToMs(uint16_t minutes) {
  return (uint32_t)minutes * 60UL * 1000UL;
}

static void startPhase(Phase p, bool autoStart) {
  currentPhase = p;
  switch (p) {
    case Phase::Focus:      phaseTotalMs = minutesToMs(cfg.focusMinutes);      break;
    case Phase::ShortBreak: phaseTotalMs = minutesToMs(cfg.shortBreakMinutes); break;
    case Phase::LongBreak:  phaseTotalMs = minutesToMs(cfg.longBreakMinutes);  break;
  }
  phaseRemainMs = phaseTotalMs;
  runState = autoStart ? RunState::Running : RunState::Idle;
}

static void advancePhase() {
  if (currentPhase == Phase::Focus) {
    completedFocusSessions++;
    if (completedFocusSessions % cfg.sessionsUntilLong == 0) {
      startPhase(Phase::LongBreak, false);
    } else {
      startPhase(Phase::ShortBreak, false);
    }
  } else {
    // any break -> back to focus
    startPhase(Phase::Focus, false);
  }
}

// ---------------------------------------------------------------------------
// Input handling — Timer screen
// ---------------------------------------------------------------------------

static void handleTimerInput(bool fnHeld, const Keyboard_Class::KeysState &st) {
  if (fnHeld) return; // no Fn-layer actions needed on the Timer screen anymore

  for (char c : st.word) {
    if (c == ' ') {
      // Start / pause / resume
      if (runState == RunState::Idle || runState == RunState::Paused) {
        runState = RunState::Running;
        playStartTone();
      } else if (runState == RunState::Running) {
        runState = RunState::Paused;
        playPauseTone();
      } else if (runState == RunState::Finished) {
        advancePhase();
        runState = RunState::Running;
        playStartTone();
      }
    } else if (c == 'r' || c == 'R') {
      // Reset current phase to full length
      startPhase(currentPhase, false);
    } else if (c == 's' || c == 'S') {
      // Skip to next phase
      advancePhase();
    } else if (c == 'p' || c == 'P') {
      enterSettings();
    } else if (c == '`') {
      soundEnabled = !soundEnabled;
      prefs.begin(NVS_NAMESPACE, /*readOnly=*/false);
      prefs.putBool(NVS_KEY_SOUNDON, soundEnabled);
      prefs.end();
    }
  }
}

// ---------------------------------------------------------------------------
// Settings screen — enter / confirm / cancel
// ---------------------------------------------------------------------------

static void enterSettings() {
  draftCfg = cfg;       // edit a copy so cancel is a true no-op
  selectedRow = SettingRow::FocusDuration;
  digitCursor = 0;
  currentScreen = AppScreen::Settings;
  playNavTone();
}

static void confirmSettings() {
  cfg = draftCfg;
  applyVolumeToSpeaker();
  saveSettings();
  // Any duration the user changed should take effect immediately if the
  // affected phase is currently idle (not mid-countdown); cheapest correct
  // way to guarantee that is to just re-derive the current phase's total
  // when we're idle, same as the old Fn+,/. behavior did.
  if (runState == RunState::Idle) {
    startPhase(currentPhase, false);
  }
  currentScreen = AppScreen::Timer;
  playStartTone();
}

static void cancelSettings() {
  // draftCfg is simply discarded; cfg was never touched.
  currentScreen = AppScreen::Timer;
  playPauseTone();
}

// Returns a pointer to the minutes field in draftCfg that the given row
// edits, or nullptr if the row isn't a duration row.
static uint16_t* durationFieldForRow(SettingRow row) {
  switch (row) {
    case SettingRow::FocusDuration:      return &draftCfg.focusMinutes;
    case SettingRow::ShortBreakDuration: return &draftCfg.shortBreakMinutes;
    case SettingRow::LongBreakDuration:  return &draftCfg.longBreakMinutes;
    default: return nullptr;
  }
}

// Adds `delta` to the digit currently under the cursor for a duration row.
// digitCursor 0 = tens-of-minutes (±10), 1 = ones-of-minutes (±1).
// Result is clamped to [MIN_DURATION_MIN, MAX_DURATION_MIN].
static void adjustSelectedDigit(int delta) {
  uint16_t* field = durationFieldForRow(selectedRow);
  if (!field) return;

  int step = (digitCursor == 0) ? 10 : 1;
  int newVal = (int)(*field) + delta * step;
  if (newVal < (int)MIN_DURATION_MIN) newVal = MIN_DURATION_MIN;
  if (newVal > (int)MAX_DURATION_MIN) newVal = MAX_DURATION_MIN;
  *field = (uint16_t)newVal;
}

static void handleSettingsInput(bool fnHeld, const Keyboard_Class::KeysState &st) {
  // Fn must be held for navigation/editing — that's how arrows work on
  // this keyboard (Fn+;,./  = Up/Left/Down/Right).
  if (fnHeld) {
    // Arrow input is "pick one direction", not text entry — only act on
    // the first arrow character seen this frame. This also sidesteps any
    // staleness issue from selectedRow changing mid-loop if the hardware
    // ever reports more than one key in st.word for a single event.
    for (char c : st.word) {
      bool isDurationRow = (durationFieldForRow(selectedRow) != nullptr);
      bool handled = true;

      switch (c) {
        case ';': // Up
          if (isDurationRow) {
            adjustSelectedDigit(+1);
          } else {
            uint8_t idx = (uint8_t)selectedRow;
            idx = (idx == 0) ? (uint8_t)SettingRow::Count - 1 : idx - 1;
            selectedRow = (SettingRow)idx;
            digitCursor = 0;
            playNavTone();
          }
          break;

        case '.': // Down
          if (isDurationRow) {
            adjustSelectedDigit(-1);
          } else {
            uint8_t idx = (uint8_t)selectedRow;
            idx = (idx + 1) % (uint8_t)SettingRow::Count;
            selectedRow = (SettingRow)idx;
            digitCursor = 0;
            playNavTone();
          }
          break;

        case ',': // Left
          if (isDurationRow) {
            if (digitCursor == 0) {
              uint8_t idx = (uint8_t)selectedRow;
              idx = (idx == 0) ? (uint8_t)SettingRow::Count - 1 : idx - 1;
              selectedRow = (SettingRow)idx;
              digitCursor = 0;
            } else {
              digitCursor--;
            }
          } else if (selectedRow == SettingRow::Cycles) {
            if (draftCfg.sessionsUntilLong > MIN_CYCLES) draftCfg.sessionsUntilLong--;
            playNavTone();
          } else if (selectedRow == SettingRow::Volume) {
            int v = (int)draftCfg.volume - VOLUME_STEP;
            draftCfg.volume = (v < MIN_VOLUME) ? MIN_VOLUME : (uint8_t)v;
            M5Cardputer.Speaker.setVolume(draftCfg.volume);
            M5Cardputer.Speaker.tone(1000, 120); // preview the new level
          }
          break;

        case '/': // Right
          if (isDurationRow) {
            if (digitCursor >= DURATION_DIGIT_COUNT - 1) {
              uint8_t idx = (uint8_t)selectedRow;
              idx = (idx + 1) % (uint8_t)SettingRow::Count;
              selectedRow = (SettingRow)idx;
              digitCursor = 0;
            } else {
              digitCursor++;
            }
          } else if (selectedRow == SettingRow::Cycles) {
            if (draftCfg.sessionsUntilLong < MAX_CYCLES) draftCfg.sessionsUntilLong++;
            playNavTone();
          } else if (selectedRow == SettingRow::Volume) {
            int v = (int)draftCfg.volume + VOLUME_STEP;
            draftCfg.volume = (v > MAX_VOLUME) ? MAX_VOLUME : (uint8_t)v;
            M5Cardputer.Speaker.setVolume(draftCfg.volume);
            M5Cardputer.Speaker.tone(1000, 120); // preview the new level
          }
          break;

        default:
          handled = false;
          break;
      }

      if (handled) break; // one arrow action per keyboard event
    }
    return; // Fn layer handled; ignore any plain-key fallthrough this frame
  }

  // Non-Fn keys on the Settings screen
  if (st.enter) {
    confirmSettings();
    return;
  }
  for (char c : st.word) {
    if (c == '`') {
      cancelSettings();
      return;
    }
  }
}

// ---------------------------------------------------------------------------
// Persistence (NVS flash via Preferences)
// ---------------------------------------------------------------------------

static void loadSettings() {
  prefs.begin(NVS_NAMESPACE, /*readOnly=*/true);
  cfg.focusMinutes      = (uint16_t)prefs.getUInt(NVS_KEY_FOCUS,   cfg.focusMinutes);
  cfg.shortBreakMinutes = (uint16_t)prefs.getUInt(NVS_KEY_SHORT,   cfg.shortBreakMinutes);
  cfg.longBreakMinutes  = (uint16_t)prefs.getUInt(NVS_KEY_LONG,    cfg.longBreakMinutes);
  cfg.sessionsUntilLong = (uint8_t)prefs.getUInt(NVS_KEY_CYCLES,  cfg.sessionsUntilLong);
  cfg.volume             = (uint8_t)prefs.getUInt(NVS_KEY_VOLUME,  cfg.volume);
  soundEnabled           = prefs.getBool(NVS_KEY_SOUNDON, soundEnabled);
  prefs.end();

  // Defensive clamps in case flash holds something stale/out-of-range
  // from an older firmware version with different limits.
  if (cfg.focusMinutes < MIN_DURATION_MIN || cfg.focusMinutes > MAX_DURATION_MIN) cfg.focusMinutes = 25;
  if (cfg.shortBreakMinutes < MIN_DURATION_MIN || cfg.shortBreakMinutes > MAX_DURATION_MIN) cfg.shortBreakMinutes = 5;
  if (cfg.longBreakMinutes < MIN_DURATION_MIN || cfg.longBreakMinutes > MAX_DURATION_MIN) cfg.longBreakMinutes = 15;
  if (cfg.sessionsUntilLong < MIN_CYCLES || cfg.sessionsUntilLong > MAX_CYCLES) cfg.sessionsUntilLong = 4;
}

static void saveSettings() {
  prefs.begin(NVS_NAMESPACE, /*readOnly=*/false);
  prefs.putUInt(NVS_KEY_FOCUS,  cfg.focusMinutes);
  prefs.putUInt(NVS_KEY_SHORT,  cfg.shortBreakMinutes);
  prefs.putUInt(NVS_KEY_LONG,   cfg.longBreakMinutes);
  prefs.putUInt(NVS_KEY_CYCLES, cfg.sessionsUntilLong);
  prefs.putUInt(NVS_KEY_VOLUME, cfg.volume);
  prefs.putBool(NVS_KEY_SOUNDON, soundEnabled);
  prefs.end();
}

static void applyVolumeToSpeaker() {
  M5Cardputer.Speaker.setVolume(cfg.volume);
}

// ---------------------------------------------------------------------------
// Sound
// ---------------------------------------------------------------------------

static void playStartTone() {
  if (!soundEnabled) return;
  M5Cardputer.Speaker.tone(1200, 90);
}

static void playPauseTone() {
  if (!soundEnabled) return;
  M5Cardputer.Speaker.tone(600, 90);
}

static void playTickTone() {
  if (!soundEnabled) return;
  M5Cardputer.Speaker.tone(900, 40);
}

static void playNavTone() {
  if (!soundEnabled) return;
  M5Cardputer.Speaker.tone(1500, 25);
}

static void playPhaseCompleteTone() {
  if (!soundEnabled) return;
  // little three-note rising chime
  M5Cardputer.Speaker.tone(880, 120);
  delay(130);
  M5Cardputer.Speaker.tone(1100, 120);
  delay(130);
  M5Cardputer.Speaker.tone(1320, 200);
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

static uint16_t colorForPhase(Phase p) {
  switch (p) {
    case Phase::Focus:      return COL_FOCUS;
    case Phase::ShortBreak: return COL_SHORT;
    case Phase::LongBreak:  return COL_LONG;
  }
  return COL_TEXT;
}

static const char* labelForPhase(Phase p) {
  switch (p) {
    case Phase::Focus:      return "FOCUS";
    case Phase::ShortBreak: return "SHORT BREAK";
    case Phase::LongBreak:  return "LONG BREAK";
  }
  return "";
}

static const char* labelForState(RunState s) {
  switch (s) {
    case RunState::Idle:     return "READY";
    case RunState::Running:  return "RUNNING";
    case RunState::Paused:   return "PAUSED";
    case RunState::Finished: return "DONE!";
  }
  return "";
}

// Draws a circular progress ring showing remaining time as a filled wedge.
static void drawProgressRing(float fraction /* 0..1 remaining */, uint16_t color) {
  // M5GFX::drawArc(x, y, r0, r1, angle0, angle1, color) where r0 is the
  // inner radius and r1 is the outer radius of the ring.
  const int32_t rInner = RING_R - RING_THICK;
  const int32_t rOuter = RING_R;

  // background track (full circle)
  canvas.fillArc(RING_CX, RING_CY, rInner, rOuter, 0.0f, 360.0f, COL_TRACK);

  if (fraction > 0.0f) {
    float deg = fraction * 360.0f;
    // Start at top (-90deg) and sweep clockwise
    canvas.fillArc(RING_CX, RING_CY, rInner, rOuter,
                    -90.0f, -90.0f + deg, color);
  }
}

static void renderFrame() {
  canvas.fillSprite(COL_BG);

  if (currentScreen == AppScreen::Timer) {
    renderTimerScreen();
  } else {
    renderSettingsScreen();
  }

  canvas.pushSprite(0, 0);
}

static void renderTimerScreen() {
  uint16_t phaseColor = colorForPhase(currentPhase);
  float remainFrac = (phaseTotalMs == 0) ? 0.0f
                      : (float)phaseRemainMs / (float)phaseTotalMs;

  drawProgressRing(remainFrac, phaseColor);

  // Time remaining, centered in the ring
  uint32_t totalSeconds = (phaseRemainMs + 999) / 1000;
  uint32_t mm = totalSeconds / 60;
  uint32_t ss = totalSeconds % 60;
  char timeBuf[8];
  snprintf(timeBuf, sizeof(timeBuf), "%02lu:%02lu", (unsigned long)mm, (unsigned long)ss);

  canvas.setTextDatum(middle_center);
  canvas.setTextColor(COL_TEXT, COL_BG);
  canvas.setFont(&fonts::DejaVu24); // built into M5GFX, safe on every build
  canvas.setTextSize(1);
  canvas.drawString(timeBuf, RING_CX, RING_CY);
  canvas.setFont(nullptr); // back to default font for the rest of the UI

  // ---- right-hand info panel ----
  int infoX = 150;
  canvas.setTextDatum(top_left);
  canvas.setTextSize(1);

  canvas.setTextColor(phaseColor, COL_BG);
  canvas.drawString(labelForPhase(currentPhase), infoX, 8);

  canvas.setTextColor(COL_TEXT, COL_BG);
  canvas.drawString(labelForState(runState), infoX, 26);

  // session dots: filled = completed focus session in this long-break cycle
  uint8_t dotsFilled = completedFocusSessions % cfg.sessionsUntilLong;
  int dotY = 48;
  for (uint8_t i = 0; i < cfg.sessionsUntilLong; i++) {
    int dotX = infoX + i * 14;
    bool filled = (i < dotsFilled);
    if (filled) {
      canvas.fillCircle(dotX, dotY, 4, COL_FOCUS);
    } else {
      canvas.drawCircle(dotX, dotY, 4, COL_DIM);
    }
  }

  canvas.setTextColor(COL_DIM, COL_BG);
  char focusLenBuf[24];
  snprintf(focusLenBuf, sizeof(focusLenBuf), "Focus: %u min", cfg.focusMinutes);
  canvas.drawString(focusLenBuf, infoX, 64);

  canvas.drawString(soundEnabled ? "Sound: on" : "Sound: muted", infoX, 78);

  // ---- bottom help bar ----
  canvas.setTextColor(COL_DIM, COL_BG);
  const char* help;
  if (runState == RunState::Idle) {
    help = "SPACE start  P settings  S skip";
  } else if (runState == RunState::Running) {
    help = "SPACE pause  R reset  S skip";
  } else if (runState == RunState::Paused) {
    help = "SPACE resume  R reset  S skip  P settings";
  } else {
    help = "SPACE continue  P settings  ` mute";
  }
  canvas.drawString(help, 4, SCREEN_H - 14);
}

// ---------------------------------------------------------------------------
// Rendering — Settings screen
// ---------------------------------------------------------------------------

static const char* nameForRow(SettingRow row) {
  switch (row) {
    case SettingRow::FocusDuration:      return "Focus";
    case SettingRow::ShortBreakDuration: return "Short break";
    case SettingRow::LongBreakDuration:  return "Long break";
    case SettingRow::Cycles:             return "Cycles/long break";
    case SettingRow::Volume:             return "Volume";
    default: return "";
  }
}

static constexpr int SETTINGS_ROW_H   = 20;
static constexpr int SETTINGS_TOP_Y   = 12;
static constexpr int SETTINGS_LABEL_X = 8;
static constexpr int SETTINGS_VALUE_X = 178;

// Draws a "MM" value where the digit under digitCursor is boxed, so the
// user can see exactly which digit Up/Down/Left/Right will affect.
static void drawEditableMinutes(uint16_t minutes, int x, int y, bool selected) {
  char buf[3];
  snprintf(buf, sizeof(buf), "%02u", minutes);

  // Each digit glyph is ~6px wide at textSize(1) with the default font;
  // we bump to size(1) deliberately and measure via textWidth so this
  // still lines up if the default font metrics differ slightly.
  canvas.setTextSize(1);
  int charW = canvas.textWidth("0");

  for (int i = 0; i < 2; i++) {
    char digitStr[2] = { buf[i], '\0' };
    int dx = x + i * charW;
    bool isCursorHere = selected && (digitCursor == (uint8_t)i);

    if (isCursorHere) {
      canvas.fillRect(dx - 1, y - 2, charW + 1, 14, COL_FOCUS);
      canvas.setTextColor(COL_BG, COL_FOCUS);
    } else {
      canvas.setTextColor(COL_TEXT, COL_BG);
    }
    canvas.drawString(digitStr, dx, y);
  }
  canvas.setTextColor(COL_TEXT, COL_BG);
  canvas.drawString("m", x + 2 * charW + 2, y);
}

static void renderSettingsScreen() {
  canvas.setTextDatum(top_left);
  canvas.setTextSize(1);
  canvas.setTextColor(COL_TEXT, COL_BG);
  canvas.drawString("SETTINGS", SETTINGS_LABEL_X, 2);

  for (uint8_t i = 0; i < (uint8_t)SettingRow::Count; i++) {
    SettingRow row = (SettingRow)i;
    bool selected = (row == selectedRow);
    int rowY = SETTINGS_TOP_Y + i * SETTINGS_ROW_H + 14;

    canvas.setTextColor(selected ? COL_FOCUS : COL_DIM, COL_BG);
    canvas.drawString(selected ? ">" : " ", SETTINGS_LABEL_X, rowY);
    canvas.drawString(nameForRow(row), SETTINGS_LABEL_X + 10, rowY);

    canvas.setTextColor(COL_TEXT, COL_BG);
    uint16_t* durField = durationFieldForRow(row);
    if (durField) {
      drawEditableMinutes(*durField, SETTINGS_VALUE_X, rowY, selected);
    } else if (row == SettingRow::Cycles) {
      char buf[4];
      snprintf(buf, sizeof(buf), "%u", draftCfg.sessionsUntilLong);
      if (selected) {
        canvas.fillRect(SETTINGS_VALUE_X - 1, rowY - 2, 16, 14, COL_FOCUS);
        canvas.setTextColor(COL_BG, COL_FOCUS);
      }
      canvas.drawString(buf, SETTINGS_VALUE_X, rowY);
    } else if (row == SettingRow::Volume) {
      // small horizontal bar instead of a number, easier to read at a glance
      int barX = SETTINGS_VALUE_X;
      int barW = 50, barH = 8;
      int barY = rowY + 2;
      canvas.drawRect(barX, barY, barW, barH, COL_DIM);
      int fillW = (int)((float)draftCfg.volume / (float)MAX_VOLUME * (barW - 2));
      if (fillW > 0) {
        canvas.fillRect(barX + 1, barY + 1, fillW, barH - 2,
                         selected ? COL_FOCUS : COL_TEXT);
      }
    }
  }

  canvas.setTextColor(COL_DIM, COL_BG);
  canvas.drawString("Fn+arrow move/edit ENTER save ` cancel",
                     SETTINGS_LABEL_X, SCREEN_H - 14);
}
