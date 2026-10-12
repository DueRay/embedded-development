#include <Arduino.h>

const int RELAY_PIN = 7;
const int CONTACT_PIN = 5;
const int BOOT_PIN = 0;

const unsigned long DEBOUNCE_TIME = 100;

volatile int counter = 0;
volatile unsigned long lastPressMillis = 0;

int loopCounter = 0;
bool relayOn = false;

bool measuring = false;
int contactBefore = HIGH;
unsigned long startingTime = 0;

void IRAM_ATTR onBootPress() {
  unsigned long now = millis();

  if (now - lastPressMillis >= DEBOUNCE_TIME) {
    lastPressMillis = now;
    counter = counter + 1;
  }
}

void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  pinMode(CONTACT_PIN, INPUT_PULLUP);

  pinMode(BOOT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BOOT_PIN), onBootPress, FALLING);

  Serial.begin(115200);
}

void loop() {
  if (!measuring && counter != loopCounter) {
    loopCounter = counter;
    relayOn = !relayOn;

    contactBefore = digitalRead(CONTACT_PIN);

    startingTime = micros();
    digitalWrite(RELAY_PIN, relayOn ? HIGH : LOW);
    measuring = true;
  }

  if (measuring && digitalRead(CONTACT_PIN) != contactBefore) {
    unsigned long finishTime = micros();
    measuring = false;

    Serial.printf("#%d %s: %lu us\n", loopCounter, relayOn ? "ON" : "OFF", finishTime - startingTime);
  }
}
