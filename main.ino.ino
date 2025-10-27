#include <Wire.h>
#include <U8g2lib.h>

#define CLK_PIN 15
#define DT_PIN 2
#define SW_PIN 4

// --- OLED Display (0x3C) ---
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// --- Volume Slots ---
int values[3] = {512, 512, 512};  // initial mid values
int activeSlot = 0;
const char* appNames[3] = {"Master", "Chrome", "Spotify"};

bool lastCLK, lastDT;
bool lastButton = HIGH;

// --- Timing for standby ---
unsigned long lastActivityTime = 0;
const unsigned long STANDBY_TIMEOUT = 5000;  // 5 seconds
bool inStandby = false;

// --- Setup ---
void setup() {
  Serial.begin(230400);
  Wire.begin(21, 22);  // SDA, SCL

  u8g2.begin();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.clearBuffer();
  u8g2.drawStr(0, 20, "PCBCupid Deck Ready!");
  u8g2.sendBuffer();

  pinMode(CLK_PIN, INPUT_PULLUP);
  pinMode(DT_PIN, INPUT_PULLUP);
  pinMode(SW_PIN, INPUT_PULLUP);

  lastCLK = digitalRead(CLK_PIN);
  lastDT = digitalRead(DT_PIN);

  lastActivityTime = millis();
  delay(800);
  displayActiveApp();
}

int clampValue(int val) {
  if (val < 0) val = 0;
  if (val > 1023) val = 1023;
  return val;
}

void sendValues(bool buttonPressed) {
  for (int i = 0; i < 3; i++) {
    Serial.print(values[i]);
    Serial.print("|");
  }
  Serial.println(buttonPressed ? "1" : "0");
}

void displayActiveApp() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);

  // --- Title ---
  u8g2.drawStr(0, 12, "Active App:");

  // --- App Name ---
  u8g2.setFont(u8g2_font_ncenB12_tr);
  u8g2.setCursor(0, 32);
  u8g2.print(appNames[activeSlot]);

  // --- Inverted Volume Percentage ---
  int volPercent = map(values[activeSlot], 0, 1023, 100, 0);
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.setCursor(0, 48);
  u8g2.print("Volume: ");
  u8g2.print(volPercent);
  u8g2.print("%");

  // --- White Empty Bar ---
  int filledWidth = map(values[activeSlot], 0, 1023, 128, 0);  // inverted fill
  u8g2.drawFrame(0, 54, 128, 10);

  // Fill white
  u8g2.setDrawColor(1);
  u8g2.drawBox(0, 54, 128, 10);

  // Black portion shows volume
  u8g2.setDrawColor(0);
  u8g2.drawBox(filledWidth, 54, 128 - filledWidth, 10);

  u8g2.setDrawColor(1);
  u8g2.sendBuffer();
}

// --- Draw cute Emo Eyes ---
void drawEmoEyes(bool blink) {
  u8g2.clearBuffer();

  int eyeY = 25;
  int eyeRadius = 10;

  if (blink) {
    // Draw eyes as thin lines (blinking)
    u8g2.drawHLine(34, eyeY, 18);
    u8g2.drawHLine(78, eyeY, 18);
  } else {
    // Normal open eyes
    u8g2.drawCircle(43, eyeY, eyeRadius, U8G2_DRAW_ALL);
    u8g2.drawCircle(87, eyeY, eyeRadius, U8G2_DRAW_ALL);
    // Pupils
    u8g2.drawDisc(43, eyeY, 4);
    u8g2.drawDisc(87, eyeY, 4);
  }

  u8g2.sendBuffer();
}

void showStandby() {
  static unsigned long lastBlink = 0;
  static bool blinkState = false;

  if (millis() - lastBlink > 700) {
    blinkState = !blinkState;
    drawEmoEyes(blinkState);
    lastBlink = millis();
  }
}

void loop() {
  bool clk = digitalRead(CLK_PIN);
  bool dt = digitalRead(DT_PIN);
  bool btn = digitalRead(SW_PIN);

  static int lastState = 0;
  int state = (clk << 1) | dt;

  bool activity = false;

  // --- Rotation Detection ---
  if (state != lastState) {
    if ((lastState == 0b00 && state == 0b01) ||
        (lastState == 0b01 && state == 0b11) ||
        (lastState == 0b11 && state == 0b10) ||
        (lastState == 0b10 && state == 0b00)) {
      values[activeSlot] += 4;  // CW
    } else {
      values[activeSlot] -= 4;  // CCW
    }

    values[activeSlot] = clampValue(values[activeSlot]);
    sendValues(false);
    displayActiveApp();
    activity = true;
  }
  lastState = state;

  // --- Button Press Detection ---
  if (btn == LOW && lastButton == HIGH) {
    activeSlot++;
    if (activeSlot >= 3) activeSlot = 0;
    sendValues(true);
    displayActiveApp();
    activity = true;
  }

  lastButton = btn;

  // --- Handle activity and standby ---
  if (activity) {
    lastActivityTime = millis();
    if (inStandby) {
      inStandby = false;
      displayActiveApp();
    }
  }

  if (!inStandby && millis() - lastActivityTime > STANDBY_TIMEOUT) {
    inStandby = true;
    u8g2.clearBuffer();
    drawEmoEyes(false);
  }

  if (inStandby) {
    showStandby();  // animate eyes
  }

  delay(1);
}
