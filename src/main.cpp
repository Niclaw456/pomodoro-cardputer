/*
  Pomodoro Timer for M5Stack Cardputer ADV
  -----------------------------------------
  Hardware: ESP32-S3 (Stamp-S3A), 1.14" 240x135 ST7789V2 LCD,
            56-key TCA8418 keyboard, ES8311/NS4150B speaker.

  Controls:
    [SPACE]      Start / Pause the current session
    [R]          Reset current session back to full time
    [S]          Skip to the next session (focus <-> break)
    [Fn] + [,]   Decrease focus length  (when idle)
    [Fn] + [.]   Increase focus length  (when idle)
    [`] (ESC)    Mute / unmute sound

  Build/flash with PlatformIO (VS Code):
    pio run -t upload
    pio device monitor
*/

#include <M5Cardputer.h>

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

struct PomodoroConfig {
  uint16_t focusMinutes      = 25;
  uint16_t shortBreakMinutes = 5;
  uint16_t longBreakMinutes  = 15;
  uint8_t  sessionsUntilLong = 4;   // long break after this many focus sessions
  uint8_t  minFocusMinutes   = 5;
  uint8_t  maxFocusMinutes   = 60;
};

static PomodoroConfig cfg;

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
static void playStartTone();
static void playPauseTone();
static void playPhaseCompleteTone();
static void playTickTone();
static uint16_t colorForPhase(Phase p);
static const char* labelForPhase(Phase p);

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void setup() {
  auto mcfg = M5.config();
  M5Cardputer.begin(mcfg, true /* enableKeyboard */);

  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setBrightness(80);
  M5Cardputer.Display.fillScreen(COL_BG);

  M5Cardputer.Speaker.setVolume(150);

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

  // ---- timer tick ----
  if (runState == RunState::Running) {
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

      // Fn-layer adjustments only while idle (so we don't resize mid-run)
      if (fnHeld && runState == RunState::Idle && currentPhase == Phase::Focus) {
        for (char c : st.word) {
          if (c == ',') { // Fn+,  -> decrease focus length
            if (cfg.focusMinutes > cfg.minFocusMinutes) {
              cfg.focusMinutes--;
              startPhase(Phase::Focus, false);
            }
          } else if (c == '.') { // Fn+. -> increase focus length
            if (cfg.focusMinutes < cfg.maxFocusMinutes) {
              cfg.focusMinutes++;
              startPhase(Phase::Focus, false);
            }
          }
        }
      }

      // Plain (non-Fn) key handling
      if (!fnHeld) {
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
          }
        }

        // The grave/backtick key doubles as ESC on the Cardputer layout;
        // use it here to toggle sound on/off.
        for (char c : st.word) {
          if (c == '`') {
            soundEnabled = !soundEnabled;
          }
        }
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
    help = "SPACE start  Fn+,/. length  S skip";
  } else if (runState == RunState::Running) {
    help = "SPACE pause  R reset  S skip";
  } else if (runState == RunState::Paused) {
    help = "SPACE resume  R reset  S skip";
  } else {
    help = "SPACE continue  ` mute";
  }
  canvas.drawString(help, 4, SCREEN_H - 14);

  canvas.pushSprite(0, 0);
}
