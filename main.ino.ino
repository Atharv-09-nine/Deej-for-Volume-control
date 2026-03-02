#define CLK_PIN 3
#define DT_PIN 4
#define SW_PIN 5

int values[3] = {512,512,512};
int activeSlot = 0;

const int stepsPerRotation = 20;
const int valuePerStep = 1023 / stepsPerRotation;

int lastClkState;
bool lastButton = HIGH;

int clamp(int v){
  if(v < 0) v = 0;
  if(v > 1023) v = 1023;
  return v;
}

void sendValues() {

  Serial.print(values[0]);
  Serial.print("|");
  Serial.print(values[1]);
  Serial.print("|");
  Serial.println(values[2]);
}

void setup() {

  Serial.begin(9600);
  delay(2000);

  Serial.println("Deej Encoder Test Starting...");

  pinMode(CLK_PIN, INPUT_PULLUP);
  pinMode(DT_PIN, INPUT_PULLUP);
  pinMode(SW_PIN, INPUT_PULLUP);

  lastClkState = digitalRead(CLK_PIN);

  sendValues();
}

void loop() {

  int clkState = digitalRead(CLK_PIN);
  int dtState = digitalRead(DT_PIN);

  if (clkState != lastClkState && clkState == LOW) {

    if (dtState == HIGH)
      values[activeSlot] += valuePerStep;
    else
      values[activeSlot] -= valuePerStep;

    values[activeSlot] = clamp(values[activeSlot]);

    Serial.print("Slot ");
    Serial.print(activeSlot);
    Serial.print(" -> ");

    sendValues();
  }

  lastClkState = clkState;

  bool btn = digitalRead(SW_PIN);

  if (btn == LOW && lastButton == HIGH) {

    activeSlot++;

    if(activeSlot >= 3)
      activeSlot = 0;

    Serial.print("Active Slot Changed -> ");
    Serial.println(activeSlot);
  }

  lastButton = btn;

  delay(1);
}
