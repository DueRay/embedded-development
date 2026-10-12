#include <Arduino.h>

const int BUTTON_PIN = 5;

const unsigned long POLL_INTERVAL = 5;
const int STABLE_SAMPLES = 4;

enum ButtonState {
  RELEASED,
  MAYBE_PRESSED,
  PRESSED,
  MAYBE_RELEASED
};

ButtonState state = RELEASED;
int stableSamples = 0;
unsigned long lastPollMillis = 0;
int pressCounter = 0;

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Serial.begin(115200);
}

void loop() {
  unsigned long now = millis();

  if (now - lastPollMillis >= POLL_INTERVAL) {
    lastPollMillis = now;
    bool low = digitalRead(BUTTON_PIN) == LOW;

    switch (state) {
      case RELEASED:
        if (low) {
          state = MAYBE_PRESSED;
          stableSamples = 1;
        }
        break;

      case MAYBE_PRESSED:
        if (!low) {
          state = RELEASED;
        } else {
          stableSamples = stableSamples + 1;

          if (stableSamples >= STABLE_SAMPLES) {
            state = PRESSED;
            pressCounter = pressCounter + 1;
            Serial.printf("presses: %d\n", pressCounter);
          }
        }
        break;

      case PRESSED:
        if (!low) {
          state = MAYBE_RELEASED;
          stableSamples = 1;
        }
        break;

      case MAYBE_RELEASED:
        if (low) {
          state = PRESSED;
        } else {
          stableSamples = stableSamples + 1;

          if (stableSamples >= STABLE_SAMPLES) {
            state = RELEASED;
          }
        }
        break;
    }
  }
}
