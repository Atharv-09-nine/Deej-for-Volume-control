#define CLK_PIN 15
#define DT_PIN 2
#define SW_PIN 4

int values[3] = {512, 512, 512};  // slider values
int activeSlot = 0;

bool lastCLK, lastDT;
bool lastButton = HIGH;

void setup() {
  Serial.begin(230400);

  pinMode(CLK_PIN, INPUT_PULLUP);
  pinMode(DT_PIN, INPUT_PULLUP);
  pinMode(SW_PIN, INPUT_PULLUP);

  lastCLK = digitalRead(CLK_PIN);
  lastDT = digitalRead(DT_PIN);
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

void loop() {
  bool clk = digitalRead(CLK_PIN);
  bool dt = digitalRead(DT_PIN);
  bool btn = digitalRead(SW_PIN);

  // --- Encoder movement detection (4x decoding) ---
  static int lastState = 0;
  int state = (clk << 1) | dt;

  if (state != lastState) {
    if ((lastState == 0b00 && state == 0b01) ||
        (lastState == 0b01 && state == 0b11) ||
        (lastState == 0b11 && state == 0b10) ||
        (lastState == 0b10 && state == 0b00)) {
      if (values[activeSlot] < 1023) values[activeSlot] += 4;  // clockwise
    } else {
      if (values[activeSlot] > 0) values[activeSlot] -= 4;     // counter-clockwise
    }

    values[activeSlot] = clampValue(values[activeSlot]);
    sendValues(false);
  }
  lastState = state;

  // --- Button press detection ---
  if (btn == LOW && lastButton == HIGH) {
    activeSlot++;
    if (activeSlot >= 3) activeSlot = 0;
    sendValues(true);
  }

  lastButton = btn;
  delay(1);
}
