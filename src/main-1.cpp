#include <Arduino.h>

const int BUTTON_PIN = 5;

volatile int counter = 0;

int loopCounter = 0;

void IRAM_ATTR onBootPress() {
  counter = counter + 1;
}

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onBootPress, FALLING);

  Serial.begin(115200);
}

void loop() {
  int current = counter;

  if (current != loopCounter) {
    Serial.printf("interrupts: %d (+%d)\n", current, current - loopCounter);
    loopCounter = current;
  }
}
