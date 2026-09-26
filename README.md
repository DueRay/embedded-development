# Module 1 Mini Project: Twilight Switch on ESP32-S3

This branch holds the Module 1 mini project: a light-controlled relay (twilight switch).

## Goal

Read an analog signal from a voltage divider with the ADC. Control an external load through a transistor switch on a GPIO pin.

## Hardware

- ESP32-S3-DevKitC-1
- Photoresistor (LDR) R1 with a 10 kΩ resistor R2 as a voltage divider from +3.3V to GPIO 5 (ADC input)
- BC547B transistor (VT1) as a level shifter from 3.3V to 5V
  - GPIO 7 drives the base through a 10 kΩ resistor R3
  - A 10 kΩ resistor R4 pulls the collector to +5V and feeds the relay module IN pin
- Relay module: connect a safe low-voltage load (LED, 5V/12V LED strip) to the OUT NO contact

## Firmware

The main loop does three steps every 100 ms:

1. Read the ADC value from the photoresistor divider (range 0...4095, 12-bit).
2. Smooth the value with an exponential moving average (coefficient 0.2). This filter is an addition on top of the assignment requirements.
3. Compare the smoothed value with two thresholds (hysteresis):
   - Below `THRESHOLD_DARK` (2200): set the GPIO HIGH and turn the relay on.
   - Above `THRESHOLD_LIGHT` (2900): set the GPIO LOW and turn the relay off.
   - Between the thresholds: keep the current state. This protects the relay from chatter.

The firmware prints the raw and smoothed values to the serial port at 115200 baud in Teleplot format (`>name:value`).
