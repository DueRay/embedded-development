#include <Arduino.h>

// ---------- Налаштування пінів ----------
#define PIN_GREEN   5
#define PIN_YELLOW  6
#define PIN_RED     7
#define PIN_BUTTON  4

// ---------- Тривалості (мілісекунди) ----------
#define T_GREEN_MS          10000UL   // зелений: 10 с
#define T_GREEN_BLINK_MS     3000UL   // зелений миготливий: 3 с
#define T_YELLOW_MS          3000UL   // жовтий: 3 с
#define T_RED_MS            10000UL   // червоний: 10 с
#define T_RED_YELLOW_MS      2000UL   // червоний + жовтий: 2 с
#define BLINK_HALF_PERIOD_MS  500UL   // півперіод миготіння: 0,5 с
#define DEBOUNCE_MS           100UL   // антидребезг кнопки

// Тактування таймера: 80 МГц (APB) / 80 = 1 МГц => 1 тік = 1 мкс
#define TIMER_DIVIDER  80
#define MS_TO_TICKS(ms) ((uint64_t)(ms) * 1000ULL)

// ---------- Стани автомата ----------
enum TrafficState {
  STATE_GREEN = 0,      // зелений
  STATE_GREEN_BLINK,    // зелений миготливий
  STATE_YELLOW,         // жовтий
  STATE_RED,            // червоний
  STATE_RED_YELLOW,     // червоний + жовтий
  STATE_YELLOW_BLINK    // особливий режим: жовтий миготливий
};

// ---------- Події ----------
enum TrafficEvent {
  EVT_STATE_TIMEOUT = 1,  // спрацював таймер тривалості стану
  EVT_BLINK_TICK,         // тік таймера миготіння
  EVT_BUTTON              // натиснута кнопка
};

// ---------- Апаратні таймери ----------
hw_timer_t *stateTimer = NULL;
hw_timer_t *blinkTimer = NULL;

// ---------- Прапорці подій (встановлюються в ISR) ----------
volatile bool stateTimeoutFlag = false;
volatile bool blinkTickFlag = false;
volatile bool buttonFlag = false;

// ---------- Стан автомата ----------
TrafficState currentState = STATE_GREEN;
bool blinkOn = true;  // фаза миготіння: true — LED увімкнений

// ---------- ISR ----------
void IRAM_ATTR onStateTimer() {
  stateTimeoutFlag = true;
}

void IRAM_ATTR onBlinkTimer() {
  blinkTickFlag = true;
}

void IRAM_ATTR onButton() {
  buttonFlag = true;
}

// ---------- Керування таймерами ----------

void startStateTimer(unsigned long ms) {
  timerAlarmDisable(stateTimer);
  stateTimeoutFlag = false;
  timerWrite(stateTimer, 0);
  timerAlarmWrite(stateTimer, MS_TO_TICKS(ms), false);
  timerAlarmEnable(stateTimer);
}

void stopStateTimer() {
  timerAlarmDisable(stateTimer);
  stateTimeoutFlag = false;
}

void restartBlinkPhase() {
  timerWrite(blinkTimer, 0);
  blinkTickFlag = false;
  blinkOn = true;
}

// ---------- Світлодіоди ----------
void setLights(bool red, bool yellow, bool green) {
  digitalWrite(PIN_RED, red ? HIGH : LOW);
  digitalWrite(PIN_YELLOW, yellow ? HIGH : LOW);
  digitalWrite(PIN_GREEN, green ? HIGH : LOW);
}

const char *stateName(TrafficState state) {
  switch (state) {
    case STATE_GREEN:        return "GREEN";
    case STATE_GREEN_BLINK:  return "GREEN_BLINK";
    case STATE_YELLOW:       return "YELLOW";
    case STATE_RED:          return "RED";
    case STATE_RED_YELLOW:   return "RED_YELLOW";
    case STATE_YELLOW_BLINK: return "YELLOW_BLINK";
  }
  return "?";
}

// ---------- Автомат ----------

void enterState(TrafficState state) {
  currentState = state;
  Serial.printf("state: %s\n", stateName(state));

  switch (state) {
    case STATE_GREEN:
      setLights(false, false, true);
      startStateTimer(T_GREEN_MS);
      break;
    case STATE_GREEN_BLINK:
      restartBlinkPhase();
      setLights(false, false, blinkOn);
      startStateTimer(T_GREEN_BLINK_MS);
      break;
    case STATE_YELLOW:
      setLights(false, true, false);
      startStateTimer(T_YELLOW_MS);
      break;
    case STATE_RED:
      setLights(true, false, false);
      startStateTimer(T_RED_MS);
      break;
    case STATE_RED_YELLOW:
      setLights(true, true, false);
      startStateTimer(T_RED_YELLOW_MS);
      break;
    case STATE_YELLOW_BLINK:
      restartBlinkPhase();
      setLights(false, blinkOn, false);
      stopStateTimer();
      break;
  }
}

void handleEvent(TrafficEvent event) {
  if (event == EVT_BUTTON) {
    if (currentState == STATE_YELLOW_BLINK) {
      enterState(STATE_RED);
    } else {
      enterState(STATE_YELLOW_BLINK);
    }
    return;
  }

  switch (currentState) {
    case STATE_GREEN:
      if (event == EVT_STATE_TIMEOUT) enterState(STATE_GREEN_BLINK);
      break;
    case STATE_GREEN_BLINK:
      if (event == EVT_STATE_TIMEOUT) {
        enterState(STATE_YELLOW);
      } else if (event == EVT_BLINK_TICK) {
        blinkOn = !blinkOn;
        setLights(false, false, blinkOn);
      }
      break;
    case STATE_YELLOW:
      if (event == EVT_STATE_TIMEOUT) enterState(STATE_RED);
      break;
    case STATE_RED:
      if (event == EVT_STATE_TIMEOUT) enterState(STATE_RED_YELLOW);
      break;
    case STATE_RED_YELLOW:
      if (event == EVT_STATE_TIMEOUT) enterState(STATE_GREEN);
      break;
    case STATE_YELLOW_BLINK:
      if (event == EVT_BLINK_TICK) {
        blinkOn = !blinkOn;
        setLights(false, blinkOn, false);
      }
      break;
  }
}

// ---------- Кнопка ----------

bool pollButton() {
  static bool pending = false;
  static unsigned long lastEdgeMs = 0;

  if (buttonFlag) {
    buttonFlag = false;
    pending = true;
    lastEdgeMs = millis();
  }

  if (pending && millis() - lastEdgeMs >= DEBOUNCE_MS) {
    pending = false;
    return digitalRead(PIN_BUTTON) == LOW;
  }

  return false;
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_GREEN, OUTPUT);
  pinMode(PIN_YELLOW, OUTPUT);
  pinMode(PIN_RED, OUTPUT);
  setLights(false, false, false);

  pinMode(PIN_BUTTON, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), onButton, FALLING);

  stateTimer = timerBegin(0, TIMER_DIVIDER, true);
  timerAttachInterrupt(stateTimer, onStateTimer, false);

  blinkTimer = timerBegin(1, TIMER_DIVIDER, true);
  timerAttachInterrupt(blinkTimer, onBlinkTimer, false);
  timerAlarmWrite(blinkTimer, MS_TO_TICKS(BLINK_HALF_PERIOD_MS), true);
  timerAlarmEnable(blinkTimer);

  enterState(STATE_GREEN);
}

void loop() {
  if (stateTimeoutFlag) {
    stateTimeoutFlag = false;
    handleEvent(EVT_STATE_TIMEOUT);
  }

  if (blinkTickFlag) {
    blinkTickFlag = false;
    handleEvent(EVT_BLINK_TICK);
  }

  if (pollButton()) {
    handleEvent(EVT_BUTTON);
  }
}
