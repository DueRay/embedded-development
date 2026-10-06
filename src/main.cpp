#include <Arduino.h>

struct Config {
    static constexpr uint8_t LED_PIN = 4;
    static constexpr uint8_t BUTTON_PIN = 6;
    static constexpr uint32_t BLINK_INTERVAL_MS = 1000;
    static constexpr uint32_t DEBOUNCE_MS = 200;
    static constexpr uint32_t SERIAL_BAUD = 115200;
    static constexpr uint32_t PROFILE_ITERATIONS = 1000;
};

enum class LedState { Off, On };
enum class LedMode { Blink, AlwaysOn, AlwaysOff };

class Led {
public:
    explicit constexpr Led(uint8_t pin) : pin_(pin) {}

    void init() const {
        digitalWrite(pin_, LOW);
        pinMode(pin_, OUTPUT);
    }

    void set(LedState state) const {
        digitalWrite(pin_, state == LedState::On ? HIGH : LOW);
    }

private:
    const uint8_t pin_;
};

constexpr Led led(Config::LED_PIN);

volatile bool buttonPressed = false;

void IRAM_ATTR onButtonPress() {
    buttonPressed = true;
}

LedMode nextMode(LedMode mode) {
    switch (mode) {
        case LedMode::Blink:     return LedMode::AlwaysOn;
        case LedMode::AlwaysOn:  return LedMode::AlwaysOff;
        case LedMode::AlwaysOff: return LedMode::Blink;
    }
    return LedMode::Blink;
}

void setup() {
    Serial.begin(Config::SERIAL_BAUD);
    led.init();

    pinMode(Config::BUTTON_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(Config::BUTTON_PIN), onButtonPress, FALLING);
}

void loop() {
    static LedMode mode = LedMode::Blink;
    static LedState state = LedState::Off;
    static uint32_t lastToggleMs = 0;
    static uint32_t lastPressMs = 0;
    static uint32_t totalUs = 0;
    static uint32_t iterations = 0;

    const uint32_t startUs = micros();
    const uint32_t nowMs = millis();

    if (buttonPressed) {
        buttonPressed = false;
        if (nowMs - lastPressMs >= Config::DEBOUNCE_MS && digitalRead(Config::BUTTON_PIN) == LOW) {
            lastPressMs = nowMs;
            mode = nextMode(mode);
        }
    }

    switch (mode) {
        case LedMode::Blink:
            if (nowMs - lastToggleMs >= Config::BLINK_INTERVAL_MS) {
                lastToggleMs = nowMs;
                state = state == LedState::On ? LedState::Off : LedState::On;
            }
            break;
        case LedMode::AlwaysOn:
            state = LedState::On;
            break;
        case LedMode::AlwaysOff:
            state = LedState::Off;
            break;
    }
    led.set(state);

    totalUs += micros() - startUs;
    if (++iterations == Config::PROFILE_ITERATIONS) {
        Serial.printf(">loopAvgUs:%.2f\n", static_cast<float>(totalUs) / iterations);
        totalUs = 0;
        iterations = 0;
    }
}
