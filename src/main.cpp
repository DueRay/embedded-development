#include <Arduino.h>

const int FAN_PIN = 7;

const uint64_t TIME_UNIT_US = 60ULL * 1000000ULL;
const uint64_t FAN_PERIOD = 60;
const uint64_t FAN_RUN_TIME = 15;

const int OFF_EVENTS = FAN_PERIOD / FAN_RUN_TIME - 1;

const uint16_t TIMER_DIVIDER = 80;

enum FanState {
  FAN_OFF,
  FAN_ON
};

FanState state = FAN_OFF;
int offEvents = OFF_EVENTS;

hw_timer_t *timer = nullptr;

volatile bool timerEvent = true;

void IRAM_ATTR onTimer() {
  timerEvent = true;
}

const char *stateName(FanState fanState) {
  return fanState == FAN_ON ? "FAN_ON" : "FAN_OFF";
}

void changeState(FanState newState) {
  Serial.printf("state %s -> %s\n", stateName(state), stateName(newState));
  state = newState;
  digitalWrite(FAN_PIN, state == FAN_ON ? HIGH : LOW);
}

void handleEvent() {
  switch (state) {
    case FAN_ON:
      changeState(FAN_OFF);
      offEvents = 1;
      break;

    case FAN_OFF:
      if (offEvents >= OFF_EVENTS) {
        changeState(FAN_ON);
      } else {
        offEvents = offEvents + 1;
      }
      break;
  }
}

void setup() {
  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, LOW);

  Serial.begin(115200);

  timer = timerBegin(0, TIMER_DIVIDER, true);
  timerAttachInterrupt(timer, onTimer, true);
  timerAlarmWrite(timer, FAN_RUN_TIME * TIME_UNIT_US, true);
  timerAlarmEnable(timer);
}

void loop() {
  if (timerEvent) {
    timerEvent = false;
    handleEvent();
  }
}
