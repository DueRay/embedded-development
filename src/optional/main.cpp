#include <Arduino.h>

// Виводи (Піни)
const int POT_PIN = 5;   // Аналоговий вхід (потенціометр)
const int MOTOR_PIN = 7; // Цифровий вихід (база транзистора 2N2222A)

// Параметри ШІМ
const unsigned long PWM_PERIOD = 10000; // Період 10 000 мкс (~100 Гц)

// Змінні для таймінгу ШІМ
unsigned long previousMicros = 0;
unsigned long highTime = 0;
bool pinState = false;

// Змінні для таймера виведення в консоль
unsigned long previousMillis = 0;
const unsigned long SERIAL_INTERVAL = 200; // Інтервал виведення (200 мс)

void setup() {
  pinMode(MOTOR_PIN, OUTPUT);
  digitalWrite(MOTOR_PIN, LOW);

  // Ініціалізуємо послідовний порт для виведення в консоль
  Serial.begin(115200);
}

void loop() {
  // 1. Читання потенціометра та розрахунок тривалості імпульсу
  int adcValue = analogRead(POT_PIN);

  // Переводимо значення 0...4095 у тривалість 0...10000 мкс
  highTime = map(adcValue, 0, 4095, 0, PWM_PERIOD);

  // 2. Асинхронна генерація ШІМ
  unsigned long currentMicros = micros();
  unsigned long timePassed = currentMicros - previousMicros;

  if (timePassed >= PWM_PERIOD) {
    // Початок нового періоду ШІМ
    previousMicros = currentMicros;

    if (highTime > 0) {
      digitalWrite(MOTOR_PIN, HIGH);
      pinState = true;
    } else {
      digitalWrite(MOTOR_PIN, LOW);
      pinState = false;
    }
  }
  else if (pinState && (timePassed >= highTime)) {
    // Час високого рівня вичерпано, перемикаємо в LOW
    digitalWrite(MOTOR_PIN, LOW);
    pinState = false;
  }

  // 3. Періодичне виведення adcValue в консоль без затримок (delay)
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= SERIAL_INTERVAL) {
    previousMillis = currentMillis;

    Serial.print("ADC Value: ");
    Serial.println(adcValue);
  }
}
