#include <Wire.h>
#include <U8g2lib.h>

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

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

int eyeSize = 38;
int centerY = 26;
unsigned long blinkTimer = 0;
bool blinkState = false;

int lastClkState;
long encoderPos = 0;

const int stepsPerRotation = 20;
const int valuePerStep = 1023 / stepsPerRotation;

int clamp(int v) {
  if (v < 0) v = 0;
  if (v > 1023) v = 1023;
  return v;
}

//  Only this simple format for Deej
void sendValues() {
  Serial.print(values[0]);
  Serial.print("|");
  Serial.print(values[1]);
  Serial.print("|");
  Serial.println(values[2]);
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
  u8g2.print("Volume: ");
  u8g2.print(volPercent);
  u8g2.print("%");

  int filled = map(values[activeSlot], 0, 1023, 0, 128);
  u8g2.drawFrame(0, 54, 128, 10);
  u8g2.drawBox(0, 54, filled, 10);

  u8g2.sendBuffer();
}

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
  if (blinkState && millis() - blinkTimer > 120) {
    blinkState = false;
  }
  drawEyes(blinkState);
}

void setup() {
  Serial.begin(9600); // ✅ Match Deej default baud rate
  Wire.begin(21, 22);
  u8g2.begin();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 20, "PCBCupid Deck Ready!");
  u8g2.sendBuffer();
  delay(700);

  pinMode(CLK_PIN, INPUT_PULLUP);
  pinMode(DT_PIN, INPUT_PULLUP);
  pinMode(SW_PIN, INPUT_PULLUP);

  lastClkState = digitalRead(CLK_PIN);
  lastActivity = millis();
  displayActiveApp();

  sendValues(); // send initial
}

void loop() {
  bool activity = false;
  int clkState = digitalRead(CLK_PIN);
  int dtState = digitalRead(DT_PIN);

  if (clkState != lastClkState && clkState == LOW) {
    if (dtState == HIGH)
      values[activeSlot] += valuePerStep;
    else
      values[activeSlot] -= valuePerStep;

    values[activeSlot] = clamp(values[activeSlot]);
    sendValues();
    displayActiveApp();
    activity = true;
  }
  lastClkState = clkState;

  bool btn = digitalRead(SW_PIN);
  if (btn == LOW && lastButton == HIGH) {
    activeSlot++;
    if (activeSlot >= 3) activeSlot = 0;
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

  if (inStandby) standbyAnimation();
  delay(1);
}
