# Embedded Development

This repository exists for education purposes. I use it to learn embedded development and to keep my practice projects in one place.

## Task 3.1: LDR Night Light

This task reads a light-dependent resistor (LDR) with the ADC and controls an LED. The project uses ESP-IDF v6.1 and an ESP32-S3 board.

The LDR and a 10 kΩ resistor make a voltage divider between 3.3 V and GND. The middle point of the divider connects to GPIO4 (ADC1, channel 3). The LED connects to GPIO16 through a 220 Ω resistor.

The program reads the ADC in oneshot mode every 50 ms. A moving average filter smooths the values with the formula `filtered = 0.1 * raw + 0.9 * filtered_prev`.

The program uses two thresholds (hysteresis) to prevent LED flicker:

- If the filtered value goes below 1100, the LED turns on.
- If the filtered value goes above 1900, the LED turns off.
- Between the two thresholds, the LED keeps its last state.
