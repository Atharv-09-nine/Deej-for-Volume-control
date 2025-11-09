#include <Wire.h>
#include <U8g2lib.h>

#define CLK_PIN 15
#define DT_PIN 2
#define SW_PIN 4
#define BUZZER_PIN 12  // 🔔 Buzzer output pin

int values[3] = {512, 512, 512};
int activeSlot = 0;
const char* appNames[3] = {"Master", "Chrome", "Spotify"};

bool lastButton = HIGH;
unsigned long buttonPressTime = 0;
bool buttonHeld = false;

unsigned long lastActivity = 0;
const unsigned long STANDBY_TIMEOUT = 8000;
bool inStandby = false;

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

int eyeSize = 38;
int centerY = 26;
unsigned long blinkTimer = 0;
bool blinkState = false;

int lastClkState;
const int stepsPerRotation = 20;
const int valuePerStep = 1023 / stepsPerRotation;

// ===== Focus Timer =====
bool focusMode = false;
bool focusTimerRunning = false;
bool settingTimer = false;
bool timerPaused = false;
bool timerCompleted = false;
unsigned long focusStartTime = 0;
unsigned long focusPausedTime = 0;
unsigned long focusDuration = 0;
int setMinutes = 25;
bool showModePopup = false;
String modeName = "";
unsigned long popupStartTime = 0;
unsigned long lastTimerUpdate = 0;

// ===== Helper =====
int clamp(int v) {
  return v < 0 ? 0 : v > 1023 ? 1023 : v;
}

void sendValues() {
  Serial.printf("%d|%d|%d\n", values[0], values[1], values[2]);
}

// ===== OLED Displays =====
void drawModePopup(String msg) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawRBox(10, 24, 108, 20, 4);
  u8g2.setDrawColor(0);
  u8g2.setCursor(20, 38);
  u8g2.print(msg);
  u8g2.setDrawColor(1);
  u8g2.sendBuffer();
}

void displaySetTimer() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 12, "Set Focus Time:");
  u8g2.setFont(u8g2_font_ncenB14_tr);
  char buf[10];
  sprintf(buf, "%d min", setMinutes);
  u8g2.drawStr(25, 40, buf);
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(10, 60, "Press to start");
  u8g2.sendBuffer();
}

void displayFocusTimer() {
  u8g2.clearBuffer();

  if (timerCompleted) {
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(10, 30, "Session Complete!");
    u8g2.drawStr(10, 50, "Double-press to Set Time");
    u8g2.sendBuffer();
    return;
  }

  unsigned long remaining = focusDuration;
  if (focusTimerRunning) {
    unsigned long elapsed = millis() - focusStartTime;
    if (elapsed >= focusDuration) {
      focusTimerRunning = false;
      timerPaused = false;
      timerCompleted = true;
      displayFocusTimer();

      // 🔔 Trigger buzzer alert when timer ends
      for (int i = 0; i < 3; i++) {
        digitalWrite(BUZZER_PIN, HIGH);
        delay(100);
        digitalWrite(BUZZER_PIN, LOW);
        delay(100);
      }
      return;
    }
    remaining = focusDuration - elapsed;
  }

  int minutes = remaining / 60000;
  int seconds = (remaining % 60000) / 1000;
  char buf[16];
  sprintf(buf, "%02d:%02d", minutes, seconds);

  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 12, "Focus Timer:");
  u8g2.setFont(u8g2_font_ncenB14_tr);
  u8g2.drawStr(32, 40, buf);
  u8g2.setFont(u8g2_font_ncenB08_tr);
  if (timerPaused)
    u8g2.drawStr(10, 60, "Press: Resume");
  else
    u8g2.drawStr(10, 60, "Press: Pause");
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

// ===== Eye animation =====
void drawEyes(bool blink) {
  u8g2.clearBuffer();
  int leftX = 32 - eyeSize / 2;
  int rightX = 96 - eyeSize / 2;
  if (blink) {
    u8g2.drawHLine(leftX, centerY, eyeSize);
    u8g2.drawHLine(rightX, centerY, eyeSize);
  } else {
    u8g2.drawRBox(leftX, centerY - eyeSize / 2, eyeSize, eyeSize, 8);
    u8g2.drawRBox(rightX, centerY - eyeSize / 2, eyeSize, eyeSize, 8);
  }
  u8g2.sendBuffer();
}

void standbyAnimation() {
  if (millis() - blinkTimer > 3000) {
    blinkTimer = millis();
    blinkState = true;
  }
  if (blinkState && millis() - blinkTimer > 120)
    blinkState = false;
  drawEyes(blinkState);
}

// ===== Setup =====
void setup() {
  Serial.begin(9600);
  Wire.begin(21, 22);
  u8g2.begin();

  pinMode(CLK_PIN, INPUT_PULLUP);
  pinMode(DT_PIN, INPUT_PULLUP);
  pinMode(SW_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);  // ✅ Buzzer pin setup
  digitalWrite(BUZZER_PIN, LOW);

  lastClkState = digitalRead(CLK_PIN);
  lastActivity = millis();
  displayActiveApp();
  sendValues();
}

// ===== Loop =====
void loop() {
  bool activity = false;
  int clkState = digitalRead(CLK_PIN);
  int dtState = digitalRead(DT_PIN);
  bool btn = digitalRead(SW_PIN);

  static unsigned long lastPressTime = 0;
  static bool waitingForSecondPress = false;

  // ===== Encoder rotation =====
  if (clkState != lastClkState && clkState == LOW) {
    if (focusMode && settingTimer) {
      if (dtState == HIGH) setMinutes++;
      else setMinutes--;
      if (setMinutes < 1) setMinutes = 1;
      if (setMinutes > 120) setMinutes = 120;
      displaySetTimer();
    } else if (!focusMode) {
      if (dtState == HIGH) values[activeSlot] += valuePerStep;
      else values[activeSlot] -= valuePerStep;
      values[activeSlot] = clamp(values[activeSlot]);
      sendValues();
      displayActiveApp();
    }
    activity = true;
  }
  lastClkState = clkState;

  // ===== Button press tracking =====
  if (btn == LOW && lastButton == HIGH) {
    buttonPressTime = millis();
    buttonHeld = false;
  }

  // ===== Long press → toggle mode =====
  if (btn == LOW && !buttonHeld && millis() - buttonPressTime > 2000) {
    buttonHeld = true;
    focusMode = !focusMode;
    settingTimer = focusMode;
    showModePopup = true;
    modeName = focusMode ? "⏱ FocusMode" : "🎚 Volume Mode";
    popupStartTime = millis();
    if (focusMode)
      displaySetTimer();
    else
      displayActiveApp();
    waitingForSecondPress = false;
  }

  // ===== Button released =====
  if (btn == HIGH && lastButton == LOW && !buttonHeld) {
    unsigned long now = millis();

    if (focusMode) {
      // Double-press → go to set timer mode
      if (waitingForSecondPress && now - lastPressTime < 600) {
        waitingForSecondPress = false;
        focusTimerRunning = false;
        timerPaused = false;
        timerCompleted = false;
        settingTimer = true;
        displaySetTimer();
      } else {
        waitingForSecondPress = true;
        lastPressTime = now;
      }
    } else {
      // Volume mode app switching
      activeSlot = (activeSlot + 1) % 3;
      displayActiveApp();
    }
    activity = true;
  }

  // ===== After 600ms with no 2nd press → handle as single press =====
  if (waitingForSecondPress && millis() - lastPressTime > 600) {
    waitingForSecondPress = false;
    if (focusMode) {
      if (settingTimer) {
        focusDuration = (unsigned long)setMinutes * 60UL * 1000UL;
        focusStartTime = millis();
        focusTimerRunning = true;
        settingTimer = false;
        displayFocusTimer();
      } else if (focusTimerRunning) {
        timerPaused = !timerPaused;
        if (timerPaused)
          focusPausedTime = millis();
        else
          focusStartTime += (millis() - focusPausedTime);
        displayFocusTimer();
      }
    }
  }

  if (btn == HIGH) buttonHeld = false;
  lastButton = btn;

  // ===== Popup display =====
  if (showModePopup && millis() - popupStartTime < 1000)
    drawModePopup(modeName);
  else
    showModePopup = false;

  // ===== Background timer update =====
  if (focusTimerRunning && !timerPaused && millis() - lastTimerUpdate >= 1000) {
    lastTimerUpdate = millis();
    if (focusMode)
      displayFocusTimer();
    else
      displayActiveApp();
  }

  // ===== Standby logic =====
  if (activity) {
    lastActivity = millis();
    inStandby = false;
  }

  if (!inStandby && millis() - lastActivity > STANDBY_TIMEOUT && !focusTimerRunning) {
    inStandby = true;
    drawEyes(false);
  }

  if (inStandby) standbyAnimation();
  delay(1);
}
