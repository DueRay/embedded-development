#include <Arduino.h>

const int BUTTON_PIN = 5;

const unsigned long DEBOUNCE_TIME = 50;

volatile bool buttonEvent = false;

bool checking = false;
unsigned long eventMillis = 0;
int pressCounter = 0;

void IRAM_ATTR onButtonPress() {
  buttonEvent = true;
}

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onButtonPress, FALLING);

  Serial.begin(115200);
}

void loop() {
  if (buttonEvent) {
    buttonEvent = false;
    eventMillis = millis();
    checking = true;
  }

  if (checking && millis() - eventMillis >= DEBOUNCE_TIME) {
    checking = false;

    if (digitalRead(BUTTON_PIN) == LOW) {
      pressCounter = pressCounter + 1;
      Serial.printf("presses: %d\n", pressCounter);
    }
  }
}
