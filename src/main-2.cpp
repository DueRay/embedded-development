#include <Arduino.h>

const int BUTTON_PIN = 5;

const unsigned long DEBOUNCE_TIME = 50;

volatile unsigned long interruptMillis = 0;

volatile int counter = 0;
int loopCounter = 0;

void IRAM_ATTR onButtonPress() {
  unsigned long currentMillis = millis();
  if (currentMillis - interruptMillis >= DEBOUNCE_TIME) {
    interruptMillis = currentMillis;
    counter = counter + 1;
  }
}

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onButtonPress, FALLING);

  Serial.begin(115200);
}

void loop() {
  int current = counter;

  if (current != loopCounter) {
    Serial.printf("interrupts: %d (+%d)\n", current, current - loopCounter);
    loopCounter = current;
  }
}
