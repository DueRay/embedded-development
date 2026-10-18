#include <Arduino.h>

const int RELAY_PIN = 7;
const int CONTACT_PIN = 5;
const int BOOT_PIN = 0;

const unsigned long DEBOUNCE_TIME = 100;
const int MEASUREMENT_COUNT = 10;
const unsigned long PAUSE_TIME = 500;
const unsigned long TIMEOUT_TIME = 1000000;

volatile int counter = 0;
volatile unsigned long lastPressMillis = 0;

volatile bool waitingContact = false;
volatile unsigned long contactTime = 0;

int loopCounter = 0;
bool relayOn = false;

bool seriesRunning = false;
int measurementNumber = 0;
unsigned long lastSwitchMillis = 0;

bool measuring = false;
unsigned long startingTime = 0;

unsigned long onSum = 0;
unsigned long offSum = 0;
int onCount = 0;
int offCount = 0;

void IRAM_ATTR onBootPress() {
  unsigned long now = millis();

  if (now - lastPressMillis >= DEBOUNCE_TIME) {
    lastPressMillis = now;
    counter = counter + 1;
  }
}

void IRAM_ATTR onContactChange() {
  if (waitingContact) {
    contactTime = micros();
    waitingContact = false;
  }
}

void startSeries() {
  seriesRunning = true;
  measurementNumber = 0;
  onSum = 0;
  offSum = 0;
  onCount = 0;
  offCount = 0;
  lastSwitchMillis = millis() - PAUSE_TIME;

  Serial.printf("Start of %d measurements\n", MEASUREMENT_COUNT);
}

void printAverages() {
  if (onCount > 0) {
    Serial.printf("Average ON: %lu us\n", onSum / onCount);
  }

  if (offCount > 0) {
    Serial.printf("Average OFF: %lu us\n", offSum / offCount);
  }

  if (onCount + offCount > 0) {
    Serial.printf("Average total: %lu us\n", (onSum + offSum) / (onCount + offCount));
  }
}

void finishMeasurement() {
  measuring = false;
  measurementNumber = measurementNumber + 1;
  lastSwitchMillis = millis();

  if (measurementNumber >= MEASUREMENT_COUNT) {
    seriesRunning = false;
    printAverages();
  }
}

void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  pinMode(CONTACT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(CONTACT_PIN), onContactChange, CHANGE);

  pinMode(BOOT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BOOT_PIN), onBootPress, FALLING);

  Serial.begin(115200);
}

void loop() {
  if (counter != loopCounter) {
    loopCounter = counter;

    if (!seriesRunning) {
      startSeries();
    }
  }

  if (seriesRunning && !measuring && millis() - lastSwitchMillis >= PAUSE_TIME) {
    relayOn = !relayOn;

    waitingContact = true;
    startingTime = micros();
    digitalWrite(RELAY_PIN, relayOn ? HIGH : LOW);
    measuring = true;
  }

  if (measuring && !waitingContact) {
    unsigned long switchingTime = contactTime - startingTime;

    if (relayOn) {
      onSum = onSum + switchingTime;
      onCount = onCount + 1;
    } else {
      offSum = offSum + switchingTime;
      offCount = offCount + 1;
    }

    Serial.printf("#%d %s: %lu us\n", measurementNumber + 1, relayOn ? "ON" : "OFF", switchingTime);
    finishMeasurement();
  }

  if (measuring && micros() - startingTime >= TIMEOUT_TIME) {
    waitingContact = false;

    Serial.printf("#%d %s: timeout\n", measurementNumber + 1, relayOn ? "ON" : "OFF");
    finishMeasurement();
  }
}
