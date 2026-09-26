#include <Arduino.h>

const int PHOTO_PIN = 5;
const int RELAY_PIN = 7;

int lightValue = 0;
float calculatedValue = 0;
float previousCalculatedValue = 0;
float coefficient = 0.2;

const int THRESHOLD_DARK = 2200;
const int THRESHOLD_LIGHT = 2900;

void setup() {
    Serial.begin(115200);

    digitalWrite(RELAY_PIN, LOW);
    pinMode(RELAY_PIN, OUTPUT);

    analogReadResolution(12);

    previousCalculatedValue = analogRead(PHOTO_PIN);
}

void loop()
{
    lightValue = analogRead(PHOTO_PIN);
    calculatedValue = lightValue * coefficient + (1 - coefficient) * previousCalculatedValue;
    previousCalculatedValue = calculatedValue;

    Serial.printf(">rawValue:%d\n", lightValue);
    Serial.printf(">calculatedValue:%.1f\n", calculatedValue);

    if (calculatedValue < THRESHOLD_DARK) {
        digitalWrite(RELAY_PIN, HIGH);
    } else if (calculatedValue > THRESHOLD_LIGHT) {
        digitalWrite(RELAY_PIN, LOW);
    }

    delay(100);
}