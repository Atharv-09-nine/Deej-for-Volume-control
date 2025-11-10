#include <Wire.h>
#include <U8g2lib.h>

#define CLK_PIN 15
#define DT_PIN 2
#define SW_PIN 4
#define BUZZER_PIN 12

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// === Modes ===
enum Mode { MODE_VOLUME, MODE_FOCUS, MODE_PINGPONG };
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
int setMinutes = 25;

// === Encoder + Volume ===
int values[3] = {512, 512, 512};
int activeSlot = 0;
const char* appNames[3] = {"Master", "Chrome", "Spotify"};
int lastClkState;
const int valuePerStep = 50;

// === Ping Pong ===
int paddleX = 54;
int ballX = 64, ballY = 32;
int ballDirX = 1, ballDirY = 1;
unsigned long lastPingPongUpdate = 0;

// === Button ===
bool btn, lastButton = HIGH;
unsigned long buttonPressTime = 0;
unsigned long lastButtonReleaseTime = 0;
bool longPressHandled = false;
bool waitingForSecondPress = false;
bool showMenu = false;

// === Standby ===
bool inStandby = false;
unsigned long lastActivity = 0;
const unsigned long STANDBY_TIMEOUT = 30000; // 30 sec
int eyeSize = 38;
int centerY = 26;

// === Helpers ===
int clamp(int v) { return v < 0 ? 0 : (v > 1023 ? 1023 : v); }

void sendValues() {
  Serial.printf("%d|%d|%d\r\n", values[0], values[1], values[2]);
  Serial.flush();
}

// === Display Functions ===
void drawModeSelector() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(35, 10, "Select Mode");
  const char* options[] = {"🎚 Volume", "⏱ Focus Timer", "🏓 Ping Pong"};

  for (int i = 0; i < 3; i++) {
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
  u8g2.sendBuffer();
}

void displayActiveApp() {
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
  u8g2.sendBuffer();
}

void displaySetTimer() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 12, "Set Focus Time:");
  char buf[10];
  sprintf(buf, "%d min", setMinutes);
  u8g2.setFont(u8g2_font_ncenB14_tr);
  u8g2.drawStr(25, 40, buf);
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(10, 60, "Short press to Start");
  u8g2.sendBuffer();
}

void displayFocusTimer() {
  u8g2.clearBuffer();
  unsigned long remaining = focusDuration;
  if (focusTimerRunning && !timerPaused) {
    unsigned long elapsed = millis() - focusStartTime;
    remaining = (elapsed >= focusDuration) ? 0 : (focusDuration - elapsed);
  } else if (timerPaused) {
    unsigned long elapsed = focusPausedTime - focusStartTime;
    remaining = (focusDuration > elapsed) ? (focusDuration - elapsed) : 0;
  }
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
  u8g2.sendBuffer();
}

void drawPingPong() {
  u8g2.clearBuffer();
  u8g2.drawBox(paddleX, 60, 20, 3);
  u8g2.drawDisc(ballX, ballY, 2);
  u8g2.sendBuffer();
}

void drawEyesFade(int brightness) {
  u8g2.clearBuffer();
  // use contrast to mimic fade; keep background black
  u8g2.setContrast(brightness);
  int leftX = 32 - 19;
  int rightX = 96 - 19;
  u8g2.drawRBox(leftX, centerY - 19, 38, 38, 8);
  u8g2.drawRBox(rightX, centerY - 19, 38, 38, 8);
  u8g2.sendBuffer();
}

void fadeInEyes() {
  for (int i = 0; i <= 255; i += 25) {
    drawEyesFade(i);
    delay(8);
  }
}

void fadeOutEyes() {
  for (int i = 255; i >= 0; i -= 25) {
    drawEyesFade(i);
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
  displayActiveApp();
  sendValues();
}

// === Loop ===
void loop() {
  int clk = digitalRead(CLK_PIN);
  int dt = digitalRead(DT_PIN);
  btn = digitalRead(SW_PIN);
  unsigned long now = millis();

  // ===== Long Press -> open menu =====
  if (btn == LOW && lastButton == HIGH) {
    buttonPressTime = now;
    longPressHandled = false;
  }
  if (btn == LOW && !longPressHandled && (now - buttonPressTime > 1500)) {
    longPressHandled = true;
    showMenu = true;
    inStandby = false;          // disable standby while in menu
    selectedMode = currentMode;
    drawModeSelector();
    lastActivity = now;
  }

  // ===== Button Released =====
  if (btn == HIGH && lastButton == LOW) {
    unsigned long pressDuration = now - buttonPressTime;

    // If we're in Focus mode and not in settingTimer, handle double-press timing
    if (currentMode == MODE_FOCUS && !settingTimer && !showMenu) {
      if (waitingForSecondPress && (now - lastButtonReleaseTime <= 450)) {
        // Double press detected -> go to Set Time immediately
        waitingForSecondPress = false;
        focusTimerRunning = false;
        timerPaused = false;
        timerCompleted = false;
        settingTimer = true;
        displaySetTimer();
        lastActivity = now;
        lastButton = btn;
        return;
      } else {
        // Start waiting for a potential second press
        waitingForSecondPress = true;
        lastButtonReleaseTime = now;
        // DO NOT execute single-press action yet; wait in main loop for timeout
      }
    } else {
      // Not in double-press candidate state (either settingTimer or other mode)
      if (!longPressHandled && pressDuration < 500) {
        // Immediate actions (no double-press delay) when in settingTimer or other modes
        if (showMenu) {
          // confirm mode selection
          currentMode = selectedMode;
          showMenu = false;
          if (currentMode == MODE_VOLUME) displayActiveApp();
          else if (currentMode == MODE_FOCUS) {
            settingTimer = true;
            focusTimerRunning = false;
            timerPaused = false;
            displaySetTimer();
          } else {
            // pingpong
            drawPingPong();
          }
          lastActivity = now;
        } else if (currentMode == MODE_VOLUME) {
          // change active slot
          activeSlot = (activeSlot + 1) % 3;
          displayActiveApp();
          lastActivity = now;
        } else if (currentMode == MODE_FOCUS && settingTimer) {
          // Start timer immediately from Set screen (no waiting for double)
          focusDuration = (unsigned long)setMinutes * 60000UL;
          focusStartTime = now;
          focusTimerRunning = true;
          timerPaused = false;
          settingTimer = false;
          timerCompleted = false;
          displayFocusTimer();
          lastActivity = now;
        } else if (currentMode == MODE_PINGPONG) {
          // Could be used for UI feedback; nothing here
        }
      }
    }
  }

  // ===== If waiting for a possible double-press, check timeout to perform single action =====
  if (waitingForSecondPress) {
    if (millis() - lastButtonReleaseTime > 450) { // no second press - treat as single
      waitingForSecondPress = false;
      // Perform single-press action for focus mode (toggle pause/resume)
      if (currentMode == MODE_FOCUS && !settingTimer) {
        if (focusTimerRunning) {
          // toggle pause/resume
          timerPaused = !timerPaused;
          if (timerPaused) {
            focusPausedTime = millis();
          } else {
            // resume: shift start time forward by paused duration
            focusStartTime += (millis() - focusPausedTime);
          }
          displayFocusTimer();
          lastActivity = millis();
        } else {
          // If timer was not running and not in settingTimer, do nothing
        }
      }
    }
  }

  lastButton = btn;

  // ===== Menu navigation =====
  if (showMenu) {
    if (clk != lastClkState && clk == LOW) {
      if (dt == HIGH)
        selectedMode = (Mode)((selectedMode + 1) % 3);
      else
        selectedMode = (Mode)((selectedMode + 2) % 3);
      drawModeSelector();
      delay(120);
    }
    lastClkState = clk;
    return; // skip other mode logic while menu active
  }

  // ===== Mode Logic =====
  if (currentMode == MODE_VOLUME) {
    // encoder adjusts current slot volume
    if (clk != lastClkState && clk == LOW) {
      if (dt == HIGH) values[activeSlot] = clamp(values[activeSlot] + valuePerStep);
      else values[activeSlot] = clamp(values[activeSlot] - valuePerStep);
      sendValues();
      displayActiveApp();
      lastActivity = millis();
      delay(8); // small debounce
    }
    lastClkState = clk;
  }

  else if (currentMode == MODE_FOCUS) {
    // when setting timer, encoder changes setMinutes
    if (settingTimer && clk != lastClkState && clk == LOW) {
      if (dt == HIGH) setMinutes++;
      else setMinutes--;
      setMinutes = constrain(setMinutes, 1, 120);
      displaySetTimer();
      lastActivity = millis();
      delay(8);
    }
    lastClkState = clk;

    // timer countdown when running and not paused
    if (focusTimerRunning && !timerPaused && millis() - lastTimerUpdate >= 1000) {
      lastTimerUpdate = millis();
      unsigned long elapsed = millis() - focusStartTime;
      if (elapsed >= focusDuration) {
        // timer finished
        focusTimerRunning = false;
        timerPaused = false;
        timerCompleted = true;
        buzzAlert();
        settingTimer = true;       // return to set screen
        displaySetTimer();
        lastActivity = millis();
      } else {
        displayFocusTimer();
      }
    }
  }

  else if (currentMode == MODE_PINGPONG) {
    // map values[0] as encoder-controlled position for smooth movement (one rotation -> full travel)
    paddleX = map(values[0], 0, 1023, 0, 108);

    // optionally allow fine adjustments with encoder clicks
    if (clk != lastClkState && clk == LOW) {
      if (dt == HIGH) values[0] = clamp(values[0] + 40);
      else values[0] = clamp(values[0] - 40);
      lastActivity = millis();
      delay(8);
    }
    lastClkState = clk;

    if (millis() - lastPingPongUpdate > 30) {
      lastPingPongUpdate = millis();
      ballX += ballDirX * 2;
      ballY += ballDirY * 2;
      if (ballX <= 2 || ballX >= 126) ballDirX = -ballDirX;
      if (ballY <= 2) ballDirY = -ballDirY;
      if (ballY >= 58 && ballX > paddleX && ballX < paddleX + 20) ballDirY = -ballDirY;
      if (ballY > 63) {
        ballX = 64; ballY = 32; ballDirY = -1;
        buzzAlert();
      }
      drawPingPong();
    }
  }

  // ===== Standby (only in Volume Mode) =====
  if (currentMode == MODE_VOLUME) {
    if (!inStandby && millis() - lastActivity > STANDBY_TIMEOUT) {
      inStandby = true;
      fadeInEyes();
    }
    if (inStandby && btn == LOW) {
      inStandby = false;
      fadeOutEyes();
      displayActiveApp();
      lastActivity = millis();
    }
  }

  delay(2);
}
