# Embedded Development

This repository exists for education purposes. I use it to learn embedded development and to keep my practice projects in one place.

## Task 3.5: Servo Control with a Potentiometer

This task turns a servo shaft with a potentiometer at a 1:1 ratio. The project uses ESP-IDF v6.1 and an ESP32-S3 board.

The servo is an SG90 with a 180° range. Its signal wire connects to GPIO4. The potentiometer is a B10K (linear, 10 kΩ) with a 270° range. Its outer pins connect to 3.3 V and GND. Its middle pin connects to GPIO6 (ADC1, channel 5).

The program reads the potentiometer every 20 ms:

1. It reads the ADC 32 times and calculates the average value.
2. It converts the average value to millivolts with the ADC calibration (curve fitting).
3. It converts the voltage to an angle: `angle = mV * 270 / 3300`.
4. It limits the angle to the common range of the two parts.
5. It sets the servo to the same angle with a 50 Hz PWM signal (LEDC, 14-bit). A 500 µs pulse gives 0°, and a 2400 µs pulse gives 180°.

The servo range (180°) is smaller than the potentiometer range (270°). Thus, the program uses only the range from 0° to 180°. If you turn the potentiometer past 180°, the servo stays at 180°.

The program updates the servo only if the angle changes by more than 0.2°. This stops servo jitter from ADC noise. After each update, the program writes the angle from the far-left position to the console.
