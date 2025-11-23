#include <Wire.h>
#include <U8g2lib.h>

#define CLK_PIN 15
#define DT_PIN 2
#define SW_PIN 4
#define BUZZER_PIN 12

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ----- ButtonHandler: detects short / double / long presses -----
enum BtnEvent { BTN_NONE = 0, BTN_SHORT, BTN_DOUBLE, BTN_LONG };

class ButtonHandler {
public:
  // thresholds in ms
  unsigned long debounceMs;
  unsigned long longMs;   // long press threshold
  unsigned long doubleMs; // max interval between two shorts to be a double

  ButtonHandler(unsigned long db = 30, unsigned long lg = 1500, unsigned long db2 = 450) {
    debounceMs = db; longMs = lg; doubleMs = db2;
    lastRaw = HIGH; stableState = HIGH; lastDebounceTime = 0;
    state = IDLE; pressTime = 0; waitingForDouble = false; longFired = false;
    prevPaused = false; prevCompleted = false;
    prevReleaseTime = 0;
  }

  // Call this every loop with the raw digitalRead(pin) value and current millis().
  // Returns one event (BTN_NONE if nothing)
  BtnEvent update(int raw, unsigned long now) {
    // debounce
    if (raw != lastRaw) {
      lastDebounceTime = now;
      lastRaw = raw;
    }
    if (now - lastDebounceTime >= debounceMs) {
      if (stableState != raw) {
        stableState = raw;
        // stable edge
        if (stableState == LOW) {
          // pressed
          pressTime = now;
          longFired = false;
          state = PRESSED;
        } else {
          // released
          unsigned long dur = now - pressTime;
          state = RELEASED;
          // if long already fired, treat as long (no short/double)
          if (longFired) {
            waitingForDouble = false;
            return BTN_NONE;
          } else {
            // candidate for short/double
            if (waitingForDouble && (now - prevReleaseTime <= doubleMs)) {
              waitingForDouble = false;
              return BTN_DOUBLE;
            } else {
              // start waiting for possible second press
              waitingForDouble = true;
              prevReleaseTime = now;
              // We'll emit BTN_SHORT later if no second press
              return BTN_NONE;
            }
          }
        }
      }
    }

    // If currently pressed, check long-press threshold and emit exactly once when crossed
    if (state == PRESSED && !longFired && (now - pressTime >= longMs)) {
      longFired = true;
      waitingForDouble = false; // cancel double detection
      return BTN_LONG;
    }

    // Handle single-short resolution after double window passes
    if (waitingForDouble && (now - prevReleaseTime > doubleMs)) {
      waitingForDouble = false;
      return BTN_SHORT;
    }

    return BTN_NONE;
  }

private:
  // debounce vars
  int lastRaw;
  int stableState;
  unsigned long lastDebounceTime;

  // press state machine
  enum S { IDLE = 0, PRESSED, RELEASED } state;
  unsigned long pressTime;
  bool longFired;

  // double press waiting
  bool waitingForDouble;
  unsigned long prevReleaseTime;

  // not used outside but kept for later enhancements
  bool prevPaused;
  bool prevCompleted;
};

// === Modes ===
enum Mode { MODE_VOLUME, MODE_FOCUS };
Mode currentMode = MODE_VOLUME;
Mode selectedMode = MODE_VOLUME;

// === Focus Timer ===
bool focusTimerRunning = false;
bool settingTimer = false;
bool timerPaused = false;
bool timerCompleted = false;
unsigned long focusStartTime = 0;
unsigned long focusPausedTime = 0;
unsigned long focusDuration = 0;
unsigned long lastTimerUpdate = 0;
unsigned long remainingAtPause = 0; // stored remaining when paused
int setMinutes = 25;

// === Encoder + Volume ===
int values[3] = {512, 512, 512};
int activeSlot = 0;
const char* appNames[3] = {"Master", "Chrome", "Spotify"};
int lastClkState;
const int valuePerStep = 50;

// === Standby / UI ===
bool inStandby = false;
unsigned long lastActivity = 0;
const unsigned long STANDBY_TIMEOUT = 30000; // 30 sec
int eyeSize = 38;
int centerY = 26;

// === Button / Menu ===
bool showMenu = false;

// === Overlay update tracking ===
long lastOverlaySecond = -1; // tracks last displayed seconds to avoid redundant redraws

// Button handler instance
ButtonHandler btnHandler(30, 1500, 450);

// === Helpers ===
int clamp(int v) { return v < 0 ? 0 : (v > 1023 ? 1023 : v); }

void sendValues() {
  Serial.printf("%d|%d|%d\r\n", values[0], values[1], values[2]);
  Serial.flush();
}

// Centralized remaining-time calculator (returns milliseconds remaining)
unsigned long getRemaining() {
  if (timerPaused) {
    return remainingAtPause;
  }
  if (focusTimerRunning) {
    unsigned long elapsed = millis() - focusStartTime;
    if (elapsed >= focusDuration) return 0;
    return focusDuration - elapsed;
  }
  if (timerCompleted) {
    return 0;
  }
  return 0;
}

// === Display draw functions (do NOT call sendBuffer here) ===
void drawModeSelector() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(30, 10, "Select Mode");
  const char* options[] = {"🎚 Volume", "⏱ Focus Timer"};
  for (int i = 0; i < 2; i++) {
    if (i == selectedMode) {
      u8g2.drawRBox(8, 18 + i * 14, 112, 12, 3);
      u8g2.setDrawColor(0);
      u8g2.setCursor(20, 28 + i * 14);
      u8g2.print(options[i]);
      u8g2.setDrawColor(1);
    } else {
      u8g2.setCursor(20, 28 + i * 14);
      u8g2.print(options[i]);
    }
  }
}

void drawActiveApp() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 12, "Active App:");
  u8g2.setFont(u8g2_font_ncenB12_tr);
  u8g2.setCursor(0, 32);
  u8g2.print(appNames[activeSlot]);
  int volPercent = map(values[activeSlot], 0, 1023, 0, 100);
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.setCursor(0, 48);
  u8g2.printf("Volume: %d%%", volPercent);
  int filled = map(values[activeSlot], 0, 1023, 0, 128);
  u8g2.drawFrame(0, 54, 128, 10);
  u8g2.drawBox(0, 54, filled, 10);
}

void drawSetTimer() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 12, "Set Focus Time:");
  char buf[10];
  sprintf(buf, "%d min", setMinutes);
  u8g2.setFont(u8g2_font_ncenB14_tr);
  u8g2.drawStr(25, 40, buf);
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(10, 60, "Short press to Start");
}

void drawFocusTimerMain() {
  u8g2.clearBuffer();
  unsigned long remaining = getRemaining();
  int minutes = remaining / 60000;
  int seconds = (remaining % 60000) / 1000;
  char buf[16];
  sprintf(buf, "%02d:%02d", minutes, seconds);
  u8g2.setFont(u8g2_font_ncenB14_tr);
  u8g2.drawStr(32, 40, buf);
  u8g2.setFont(u8g2_font_ncenB08_tr);
  if (timerPaused)
    u8g2.drawStr(10, 60, "Short press: Resume / Double: Set");
  else if (focusTimerRunning)
    u8g2.drawStr(10, 60, "Short press: Pause / Double: Set");
  else if (timerCompleted)
    u8g2.drawStr(10, 60, "Double press: Set Time");
  else
    u8g2.drawStr(10, 60, "Short press to Start");
}

void drawEyesFade(int brightness) {
  u8g2.clearBuffer();
  u8g2.setContrast(brightness);
  int leftX = 32 - 19;
  int rightX = 96 - 19;
  u8g2.drawRBox(leftX, centerY - 19, 38, 38, 8);
  u8g2.drawRBox(rightX, centerY - 19, 38, 38, 8);
}

void fadeInEyes() {
  for (int i = 0; i <= 255; i += 25) {
    drawEyesFade(i);
    u8g2.sendBuffer();
    delay(8);
  }
}

void fadeOutEyes() {
  for (int i = 255; i >= 0; i -= 25) {
    drawEyesFade(i);
    u8g2.sendBuffer();
    delay(8);
  }
}

void buzzAlert() {
  for (int i = 0; i < 5; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
    delay(100);
  }
}

// Draws a small timer overlay (top-right). Call before sendBuffer.
void drawSmallTimerOverlay() {
  // Show overlay only when timer is running, paused, or completed
  if (!focusTimerRunning && !timerPaused && !timerCompleted) return;

  unsigned long remaining = getRemaining();
  int minutes = remaining / 60000;
  int seconds = (remaining % 60000) / 1000;
  char buf[8];
  sprintf(buf, "%02d:%02d", minutes, seconds);

  int boxW = 48;
  int boxH = 14;
  int x = 128 - boxW - 2;
  int y = 2;
  u8g2.drawRBox(x, y, boxW, boxH, 3);
  u8g2.setDrawColor(0); // inverted text
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.setCursor(x + 6, y + 11);
  u8g2.print(buf);
  u8g2.setDrawColor(1); // restore
}

// Render wrapper: draw selected screen, add overlay, then sendBuffer
void renderCurrentScreen() {
  if (showMenu) {
    drawModeSelector();
  } else {
    if (currentMode == MODE_VOLUME) {
      drawActiveApp();
    } else if (currentMode == MODE_FOCUS) {
      if (settingTimer) drawSetTimer();
      else drawFocusTimerMain();
    }
  }

  // Overlay (draw on top of whatever is drawn)
  drawSmallTimerOverlay();

  // finally present
  u8g2.sendBuffer();
}

// Update timer state in background (runs irrespective of mode)
// Now also triggers redraw when the displayed second changes or on state changes.
void updateFocusTimerBackground() {
  // compute remaining once (centralized)
  unsigned long remaining = getRemaining();
  long remainingSec = (long)(remaining / 1000);

  // If running and not paused, update per-second and detect completion
  if (focusTimerRunning && !timerPaused) {
    // ensure we only act each second to minimize draw frequency
    if (millis() - lastTimerUpdate >= 250) { // check frequently but only redraw on second change
      lastTimerUpdate = millis();

      // timer finished?
      if (remaining == 0) {
        focusTimerRunning = false;
        timerPaused = false;
        timerCompleted = true;
        buzzAlert();
        settingTimer = true; // show set screen to allow quick restart
        lastActivity = millis();
        // force immediate redraw
        lastOverlaySecond = -1;
        renderCurrentScreen();
        // if no timer active (we just completed but still show completed state) do not alter standby here
        return;
      }

      // redraw overlay only when the displayed second changed
      if (remainingSec != lastOverlaySecond) {
        lastOverlaySecond = remainingSec;
        renderCurrentScreen();
      }
    }
  } else {
    // Not actively running
    // If paused or completed, ensure the overlay reflects that immediately (state change)
    static bool prevPaused = false;
    static bool prevCompleted = false;
    if (timerPaused != prevPaused || timerCompleted != prevCompleted) {
      prevPaused = timerPaused;
      prevCompleted = timerCompleted;
      lastOverlaySecond = -1; // force redraw
      renderCurrentScreen();
    }
  }
}

// === Setup ===
void setup() {
  Serial.begin(9600);
  Wire.begin(21, 22);
  u8g2.begin();
  pinMode(CLK_PIN, INPUT_PULLUP);
  pinMode(DT_PIN, INPUT_PULLUP);
  pinMode(SW_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  lastClkState = digitalRead(CLK_PIN);
  lastActivity = millis();
  renderCurrentScreen();
  sendValues();
}

// === Loop ===
void loop() {
  int clk = digitalRead(CLK_PIN);
  int dt = digitalRead(DT_PIN);
  int rawBtn = digitalRead(SW_PIN); // raw button for handler
  unsigned long now = millis();

  // Always update timer in background (this now triggers redraws correctly)
  updateFocusTimerBackground();

  // --- Ensure eyes are off when there is no active timer ---
  // Only allow standby (eyes) if timer is active (running, paused, or completed).
  // If a timer is not active and eyes are currently blinking, turn them off.
  bool timerIsActive = (focusTimerRunning || timerPaused || timerCompleted);
  if (!timerIsActive && inStandby) {
    // timer turned off while in standby -> stop eyes
    inStandby = false;
    fadeOutEyes();
    lastOverlaySecond = -1;
    renderCurrentScreen();
  }

  // === Button handling via ButtonHandler ===
  BtnEvent ev = btnHandler.update(rawBtn, now);
  if (ev != BTN_NONE) {
    // Wake up from standby on any button event (but standby will only exist if timer was active)
    if (inStandby) {
      inStandby = false;
      fadeOutEyes();
      lastOverlaySecond = -1;
      renderCurrentScreen();
      lastActivity = now;
    }

    if (ev == BTN_LONG) {
      // Long press: open menu (fires once while button held)
      showMenu = true;
      inStandby = false;
      selectedMode = currentMode;
      lastOverlaySecond = -1;
      renderCurrentScreen();
    } else if (ev == BTN_DOUBLE) {
      // Double press
      if (showMenu) {
        // confirm selection
        currentMode = selectedMode;
        showMenu = false;
        lastOverlaySecond = -1;
        renderCurrentScreen();
      } else if (currentMode == MODE_FOCUS && !settingTimer) {
        // go to set time screen (timer continues running in background)
        settingTimer = true;
        lastOverlaySecond = -1;
        renderCurrentScreen();
      } else {
        // other double-press uses can be added here
      }
    } else if (ev == BTN_SHORT) {
      // Short press (single)
      if (showMenu) {
        // confirm mode selection
        currentMode = selectedMode;
        showMenu = false;
        lastOverlaySecond = -1;
        renderCurrentScreen();
      } else if (currentMode == MODE_VOLUME) {
        // change active slot
        activeSlot = (activeSlot + 1) % 3;
        lastOverlaySecond = -1;
        renderCurrentScreen();
        lastActivity = now;
      } else if (currentMode == MODE_FOCUS && settingTimer) {
        // Start timer immediately from Set screen
        focusDuration = (unsigned long)setMinutes * 60000UL;
        focusStartTime = millis();
        focusTimerRunning = true;
        timerPaused = false;
        timerCompleted = false;
        remainingAtPause = focusDuration; // full duration available now
        lastTimerUpdate = millis();
        // initialize overlay tracking
        lastOverlaySecond = (long)(focusDuration / 1000);
        settingTimer = false;
        lastOverlaySecond = -1;
        renderCurrentScreen();
        lastActivity = now;
      } else if (currentMode == MODE_FOCUS && !settingTimer) {
        // toggle pause/resume
        if (focusTimerRunning && !timerPaused) {
          // pause: capture remaining once
          remainingAtPause = getRemaining();
          timerPaused = true;
          focusPausedTime = millis();
          lastOverlaySecond = -1;
          renderCurrentScreen();
          lastActivity = now;
        } else if (timerPaused) {
          // resume: compute new start so remaining stays same
          timerPaused = false;
          unsigned long adjustedStart = millis();
          if (focusDuration > remainingAtPause)
            adjustedStart = millis() - (focusDuration - remainingAtPause);
          focusStartTime = adjustedStart;
          remainingAtPause = 0;
          lastTimerUpdate = millis();
          lastOverlaySecond = -1;
          renderCurrentScreen();
          lastActivity = now;
        } else {
          // If timer was not running and not in settingTimer, do nothing
        }
      }
    }
  }

  // ===== Menu navigation (encoder) =====
  if (showMenu) {
    if (clk != lastClkState && clk == LOW) {
      if (dt == HIGH)
        selectedMode = (Mode)((selectedMode + 1) % 2);
      else
        selectedMode = (Mode)((selectedMode + 1) % 2); // same since only 2 items
      lastOverlaySecond = -1;
      renderCurrentScreen();
      delay(120);
    }
    lastClkState = clk;
    // skip other mode logic while menu active
    return;
  }

  // ===== Mode Logic =====
  if (currentMode == MODE_VOLUME) {
    // encoder adjusts current slot volume
    if (clk != lastClkState && clk == LOW) {
      if (dt == HIGH) values[activeSlot] = clamp(values[activeSlot] + valuePerStep);
      else values[activeSlot] = clamp(values[activeSlot] - valuePerStep);
      sendValues();
      lastOverlaySecond = -1;
      renderCurrentScreen();
      lastActivity = millis();
      delay(8); // small debounce
    }
    lastClkState = clk;
  } else if (currentMode == MODE_FOCUS) {
    // when setting timer, encoder changes setMinutes
    if (settingTimer && clk != lastClkState && clk == LOW) {
      if (dt == HIGH) setMinutes++;
      else setMinutes--;
      setMinutes = constrain(setMinutes, 1, 120);
      lastOverlaySecond = -1;
      renderCurrentScreen();
      lastActivity = millis();
      delay(8);
    }
    lastClkState = clk;

    // Redraw occasionally while in focus to keep UI responsive (overlay handled separately)
    if (!settingTimer && (millis() - lastTimerUpdate >= 500)) {
      renderCurrentScreen();
    }
  }

  // ===== Standby (only in Volume Mode and only if timer is active) =====
  // Eyes will only blink if a timer is active (running/paused/completed)
  bool timerActiveNow = (focusTimerRunning || timerPaused || timerCompleted);
  if (currentMode == MODE_VOLUME) {
    if (!inStandby && millis() - lastActivity > STANDBY_TIMEOUT && timerActiveNow) {
      inStandby = true;
      fadeInEyes();
    }
    // waking handled at button-event top (so press will wake)
  }

  delay(2);
}
