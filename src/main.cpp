#include <Arduino.h>

struct Config {
    static constexpr uint8_t LED1_PIN = 4;
    static constexpr uint8_t LED2_PIN = 5;
    static constexpr uint8_t LED3_PIN = 6;
    static constexpr uint32_t LED1_INTERVAL_MS = 200;
    static constexpr uint32_t LED2_INTERVAL_MS = 500;
    static constexpr uint32_t LED3_INTERVAL_MS = 1000;
    static constexpr uint32_t SERIAL_BAUD = 115200;
};

enum class LedState { Off, On };

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

class BlinkTask {
public:
    constexpr BlinkTask(const char *name, uint8_t pin, uint32_t intervalMs)
        : name_(name), led_(pin), intervalMs_(intervalMs) {}

    void init(uint32_t nowMs) {
        led_.init();
        lastToggleMs_ = nowMs;
        report(nowMs);
    }

    void update(uint32_t nowMs) {
        if (nowMs - lastToggleMs_ < intervalMs_) {
            return;
        }
        // Step by the interval, not to nowMs, so a late loop() does not shift the period.
        lastToggleMs_ += intervalMs_;
        report(lastToggleMs_);
        state_ = state_ == LedState::On ? LedState::Off : LedState::On;
        led_.set(state_);
        report(lastToggleMs_);
    }

private:
    // Teleplot format with an explicit timestamp: >name:timestamp_ms:value
    void report(uint32_t timestampMs) const {
        Serial.printf(">%s:%lu:%d\n", name_, static_cast<unsigned long>(timestampMs),
                      state_ == LedState::On ? 1 : 0);
    }

    const char *const name_;
    const Led led_;
    const uint32_t intervalMs_;
    uint32_t lastToggleMs_ = 0;
    LedState state_ = LedState::Off;
};

BlinkTask tasks[] = {
    {"led1", Config::LED1_PIN, Config::LED1_INTERVAL_MS},
    {"led2", Config::LED2_PIN, Config::LED2_INTERVAL_MS},
    {"led3", Config::LED3_PIN, Config::LED3_INTERVAL_MS},
};

void setup() {
    Serial.begin(Config::SERIAL_BAUD);

    const uint32_t nowMs = millis();
    for (auto &task : tasks) {
        task.init(nowMs);
    }
}

void loop() {
    const uint32_t nowMs = millis();
    for (auto &task : tasks) {
        task.update(nowMs);
    }
}
