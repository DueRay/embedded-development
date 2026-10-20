# Embedded Development

This repository exists for education purposes. I use it to learn embedded development and to keep my practice projects in one place.

## Task 2.6: Button Debounce With ESP-IDF

This task does parts 1 to 4 of task 2.4 again with the ESP-IDF framework. The project uses ESP-IDF v6.1 and an ESP32-S3 board. The button connects GPIO5 to GND.

### Build

The file `main/CMakeLists.txt` selects the source file with `app_main()`. To build a part, change the `MAIN_SRC` value, for example `set(MAIN_SRC "main-3.c")`. Then run `idf.py -B build flash monitor`.

The file `sdkconfig.defaults` sets the FreeRTOS tick rate to 1000 Hz. One tick is 1 ms, the same as in the Arduino core. Part 4 needs this rate to read the pin every 5 ms.

## Task 2.6 Part 1: Interrupt Counter Without Debounce

The file `main/main-1.c` attaches an interrupt to the button on the falling edge. The pin uses the internal pull-up resistor. The interrupt handler adds 1 to a counter. The loop in `app_main()` prints the counter when the value changes.

### Results

I pushed the button 10 times. The counter showed 16 interrupts.

The button contacts bounce, so one push gives more than one falling edge. The Arduino version counted 26 interrupts for 10 pushes. The number of bounce edges changes from push to push, so each test gives a different count.

## Task 2.6 Part 2: Time-Based Software Debounce

The file `main/main-2.c` uses the same button. The interrupt handler reads the time from `esp_timer_get_time()`. If 50 ms or more passed since the last accepted interrupt, the handler adds 1 to the counter. If not, the handler ignores the interrupt. The loop in `app_main()` prints the counter when the value changes.

### Results

I pushed the button 10 times. The counter showed 10 presses. The Arduino version counted 13 presses.

The 50 ms filter removes the bounce that comes right after a push. The filter measures the time from the last accepted interrupt. If a push lasts more than 50 ms, the bounce on release can still count as a press. In this test, no release bounce passed the filter.

## Task 2.6 Part 3: State-Based Debounce

The file `main/main-3.c` uses the same button. The interrupt handler only sets a flag to signal an event.

The loop in `app_main()` clears the flag and starts a 50 ms wait. A new event during the wait starts the wait again, so all bounce edges of one push give one check. After the wait, the loop reads the pin with `gpio_get_level()`. If the pin is LOW, the button is still pushed, and the loop adds 1 to the press counter. If the pin is HIGH, the event came from the release bounce, and the loop ignores it.

### Results

I pushed the button 10 times. The press counter showed 10 presses.

The level check gives exactly 1 press for 1 push. The release does not cause a press.

## Task 2.6 Part 4: Polling With a Debounce State Machine

The file `main/main-4.c` uses the same button and does not use interrupts. The loop in `app_main()` reads the pin every 5 ms. It uses `esp_timer_get_time()` for the interval.

A state machine with 4 states does the debounce:

| State | Pin LOW | Pin HIGH |
|-------|---------|----------|
| `RELEASED` | go to `MAYBE_PRESSED` | stay |
| `MAYBE_PRESSED` | after 4 LOW samples in a row, go to `PRESSED` and count 1 press | go back to `RELEASED` |
| `PRESSED` | stay | go to `MAYBE_RELEASED` |
| `MAYBE_RELEASED` | go back to `PRESSED` | after 4 HIGH samples in a row, go to `RELEASED` |

A bounce breaks the series of equal samples, and the state machine goes back to the last stable state. The counter changes only on the move to `PRESSED`, so the release does not cause a press.

### Results

I pushed the button 10 times. The press counter showed 10 presses.

The state machine needs 4 samples of 5 ms, so the reaction comes about 15 to 20 ms after the contacts become stable.

## Task 2.6 Summary

| Part | Method | Pushes | Arduino without RC (task 2.4) | ESP-IDF |
|------|--------|--------|-------------------------------|---------|
| 1 | No debounce | 10 | 26 | 16 |
| 2 | Time-based debounce | 10 | 13 | 10 |
| 3 | State-based debounce | 10 | 10 | 10 |
| 4 | Polling with a state machine | 10 | 10 | 10 |

The ESP-IDF versions gave the same pattern as the Arduino versions. Part 1 counted more interrupts than pushes. Parts 3 and 4 check the pin level and gave the correct count. Part 2 gave the correct count in this test, but a long push can still add a press on release.
