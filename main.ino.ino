#include <Wire.h>
#include <U8g2lib.h>

// ------------------- ROTARY -------------------
#define CLK_PIN 15
#define DT_PIN 2
#define SW_PIN 4

int values[3] = {512, 512, 512};
int activeSlot = 0;
const char* appNames[3] = {"Master", "Chrome", "Spotify"};

bool lastButton = HIGH;
unsigned long lastActivity = 0;
const unsigned long STANDBY_TIMEOUT = 5000;
bool inStandby = false;

// ------------------- OLED -------------------
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ------------------- EYE SETTINGS -------------------
int eyeSize = 38; // fixed size (no breathing)
int eyeSpacing = 6;
int centerY = 26;

// Smooth idle blink
unsigned long blinkTimer = 0;
bool blinkState = false;

// ------------------- HELPERS -------------------
int clamp(int v) {
  if (v < 0) v = 0;
  if (v > 1023) v = 1023;
  return v;
}

void sendValues(bool button) {
  for (int i = 0; i < 3; i++) {
    Serial.print(values[i]);
    Serial.print("|");
  }
  Serial.println(button ? "1" : "0");
}

// ------------------- VOLUME UI -------------------
void displayActiveApp() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);

  u8g2.drawStr(0, 12, "Active App:");
  u8g2.setFont(u8g2_font_ncenB12_tr);
  u8g2.setCursor(0, 32);
  u8g2.print(appNames[activeSlot]);

  int volPercent = map(values[activeSlot], 0, 1023, 100, 0);
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.setCursor(0, 48);
  u8g2.print("Volume: ");
  u8g2.print(volPercent);
  u8g2.print("%");

  int filled = map(values[activeSlot], 0, 1023, 128, 0);

  u8g2.drawFrame(0, 54, 128, 10);
  u8g2.setDrawColor(1);
  u8g2.drawBox(0, 54, 128, 10);
  u8g2.setDrawColor(0);
  u8g2.drawBox(filled, 54, 128 - filled, 10);
  u8g2.setDrawColor(1);

  u8g2.sendBuffer();
}

// ------------------- EYES -------------------
void drawEyes(bool blink) {
  u8g2.clearBuffer();

  int leftX = 32 - eyeSize / 2;
  int rightX = 96 - eyeSize / 2;

  if (blink) {
    u8g2.drawHLine(leftX, centerY, eyeSize);
    u8g2.drawHLine(rightX, centerY, eyeSize);
  } else {
    u8g2.drawRBox(leftX, centerY - eyeSize/2, eyeSize, eyeSize, 8);
    u8g2.drawRBox(rightX, centerY - eyeSize/2, eyeSize, eyeSize, 8);
  }

  u8g2.sendBuffer();
}

// ------------------- STANDBY ANIMATION -------------------
void standbyAnimation() {
  if (millis() - blinkTimer > 3000) {
    blinkTimer = millis();
    blinkState = true;
  }

  if (blinkState && millis() - blinkTimer > 120) {
    blinkState = false;
  }

  drawEyes(blinkState);
}

// ------------------- SETUP -------------------
void setup() {
  Serial.begin(230400);
  Wire.begin(21, 22);

  u8g2.begin();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 20, "PCBCupid Deck Ready!");
  u8g2.sendBuffer();
  delay(700);

  pinMode(CLK_PIN, INPUT_PULLUP);
  pinMode(DT_PIN, INPUT_PULLUP);
  pinMode(SW_PIN, INPUT_PULLUP);

  lastActivity = millis();
  displayActiveApp();
}

// ------------------- LOOP -------------------
void loop() {
  bool activity = false;

  static int lastState = (digitalRead(CLK_PIN) << 1) | digitalRead(DT_PIN);
  int state = (digitalRead(CLK_PIN) << 1) | digitalRead(DT_PIN);

  if (state != lastState) {
    if ((lastState == 0 && state == 1) || (lastState == 1 && state == 3) ||
        (lastState == 3 && state == 2) || (lastState == 2 && state == 0)) {
      values[activeSlot] += 4;
    } else {
      values[activeSlot] -= 4;
    }

    values[activeSlot] = clamp(values[activeSlot]);
    sendValues(false);
    displayActiveApp();
    activity = true;
    lastState = state;
  }

  bool btn = digitalRead(SW_PIN);
  if (btn == LOW && lastButton == HIGH) {
    activeSlot++;
    if (activeSlot >= 3) activeSlot = 0;

    sendValues(true);
    displayActiveApp();
    activity = true;
  }
  lastButton = btn;

  if (activity) {
    lastActivity = millis();
    inStandby = false;
  }

  if (!inStandby && millis() - lastActivity > STANDBY_TIMEOUT) {
    inStandby = true;
    drawEyes(false);
  }

  if (inStandby) {
    standbyAnimation();
  }

  delay(1);
}
