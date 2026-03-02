#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define SDA_PIN 8
#define SCL_PIN 9

#define CLK_PIN 3
#define DT_PIN 4
#define SW_PIN 5

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

int values[5] = {512,512,512,512,512};
int activeSlot = 0;

const char* appNames[5] = {
  "Master",
  "Chrome",
  "Spotify",
  "Games",
  "Discord"
};

bool lastButton = HIGH;
int lastClkState;

unsigned long lastActivity = 0;
const unsigned long STANDBY_TIMEOUT = 5000;
bool inStandby = false;

int eyeSize = 38;
int centerY = 26;

unsigned long blinkTimer = 0;
bool blinkState = false;

const int stepsPerRotation = 20;
const int valuePerStep = 1023 / stepsPerRotation;

int clamp(int v){
  if(v < 0) v = 0;
  if(v > 1023) v = 1023;
  return v;
}

void sendValues(){
  Serial.print(values[0]);
  Serial.print("|");
  Serial.print(values[1]);
  Serial.print("|");
  Serial.println(values[2]);
}

void drawVolumeUI(){

  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0,0);
  display.print("Active App:");

  display.setTextSize(2);
  display.setCursor(0,14);
  display.print(appNames[activeSlot]);

  int percent = map(values[activeSlot],0,1023,0,100);

  display.setTextSize(1);
  display.setCursor(0,38);
  display.print("Volume: ");
  display.print(percent);
  display.print("%");

  int filled = map(values[activeSlot],0,1023,0,128);

  display.drawRect(0,50,128,10,SSD1306_WHITE);
  display.fillRect(0,50,filled,10,SSD1306_WHITE);

  display.display();
}

void drawEyes(bool blink){

  display.clearDisplay();

  int leftX = 32 - eyeSize/2;
  int rightX = 96 - eyeSize/2;

  if(blink){

    display.drawLine(leftX,centerY,leftX+eyeSize,centerY,SSD1306_WHITE);
    display.drawLine(rightX,centerY,rightX+eyeSize,centerY,SSD1306_WHITE);

  } else {

    display.fillRoundRect(leftX,centerY-eyeSize/2,eyeSize,eyeSize,8,SSD1306_WHITE);
    display.fillRoundRect(rightX,centerY-eyeSize/2,eyeSize,eyeSize,8,SSD1306_WHITE);

  }

  display.display();
}

void standbyAnimation(){

  if(millis() - blinkTimer > 3000){
    blinkTimer = millis();
    blinkState = true;
  }

  if(blinkState && millis() - blinkTimer > 120){
    blinkState = false;
  }

  drawEyes(blinkState);
}

void setup(){

  Serial.begin(9600);

  Wire.begin(SDA_PIN,SCL_PIN);
  Wire.setClock(100000);

  display.begin(SSD1306_SWITCHCAPVCC,0x3C);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(10,25);
  display.println("check:1 Million,2 Million,3 Million,...");

  display.display();
  delay(800);

  pinMode(CLK_PIN,INPUT_PULLUP);
  pinMode(DT_PIN,INPUT_PULLUP);
  pinMode(SW_PIN,INPUT_PULLUP);

  lastClkState = digitalRead(CLK_PIN);

  lastActivity = millis();

  drawVolumeUI();

  sendValues();
}

void loop(){

  bool activity = false;

  int clkState = digitalRead(CLK_PIN);
  int dtState = digitalRead(DT_PIN);

  if(clkState != lastClkState && clkState == LOW){

    if(dtState == HIGH)
      values[activeSlot] += valuePerStep;
    else
      values[activeSlot] -= valuePerStep;

    values[activeSlot] = clamp(values[activeSlot]);

    sendValues();
    drawVolumeUI();

    activity = true;
  }

  lastClkState = clkState;

  bool btn = digitalRead(SW_PIN);

  if(btn == LOW && lastButton == HIGH){

    activeSlot++;

    if(activeSlot >= 3)
      activeSlot = 0;

    drawVolumeUI();

    activity = true;
  }

  lastButton = btn;

  if(activity){
    lastActivity = millis();
    inStandby = false;
  }

  if(!inStandby && millis() - lastActivity > STANDBY_TIMEOUT){
    inStandby = true;
    drawEyes(false);
  }

  if(inStandby)
    standbyAnimation();

  delay(1);
}
