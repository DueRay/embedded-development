# Embedded Development

This repository exists for education purposes. I use it to learn embedded development and to keep my practice projects in one place.

## Task 2.4 Part 1: Interrupt Counter Without Debounce

The sketch `src/main-1.cpp` attaches an interrupt to a button on GPIO5 on the FALLING edge. The pin uses the internal pull-up resistor. The interrupt handler adds 1 to a counter. The `loop()` function prints the counter to the Serial Monitor when the value changes.

### Results

I pushed the button 10 times. The counter showed 26 interrupts.

The button contacts bounce, so one push gives more than one falling edge. The contacts also bounce when I release the button. A release can cause an interrupt too.

## Task 2.4 Part 2: Time-Based Software Debounce

The sketch `src/main-2.cpp` uses the same button on GPIO5. The interrupt handler does not filter the interrupts. It saves the time from `millis()` and adds 1 to the interrupt counter.

The `loop()` function does the debounce. When the interrupt counter changes, `loop()` compares the interrupt time with the time of the last accepted press. If less than 50 ms passed, `loop()` ignores the interrupt. If not, `loop()` adds 1 to the press counter and prints both counters to the Serial Monitor.

### Results

I pushed the button 10 times. The press counter showed 13 presses. Without debounce, the counter showed 26 interrupts for 10 pushes.

The 50 ms filter removes the bounce that comes right after a push. A long push lasts more than 50 ms, so the bounce on release can still count as a press.

## Task 2.4 Part 3: State-Based Debounce

The sketch `src/main-3.cpp` uses the same button on GPIO5. The interrupt handler only sets a flag to signal an event.

The `loop()` function clears the flag and starts a 50 ms wait. A new event during the wait starts the wait again, so all bounce edges of one push give one check. After the wait, `loop()` reads the pin level. If the pin is LOW, the button is still pushed, and `loop()` adds 1 to the press counter. If the pin is HIGH, the event came from the release bounce, and `loop()` ignores it.

### Results

I pushed the button 10 times. The press counter showed 10 presses.

The level check gives exactly 1 press for 1 push. The release does not cause a press.

## Task 2.4 Part 4: Polling With a Debounce State Machine

The sketch `src/main-4.cpp` uses the same button on GPIO5 and does not use interrupts. The `loop()` function reads the pin every 5 ms. It uses `millis()` for the interval, not `delay()`.

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

## Task 2.4 Part 5: Hardware Debounce

I added an RC filter to the button circuit. The filter has a 100 nF capacitor and a 100 Ω resistor. The 10 kΩ pull-up resistor did not change.

The capacitor cannot change its voltage at once. The short pulses from the contact bounce do not get to the GPIO pin, so the pin sees one clean edge for each push and each release.

### Results

I did parts 1 to 4 again with the RC filter. For each part, I pushed the button 10 times. Each sketch counted 10 presses.

## Task 2.4 Summary

| Part | Method | Pushes | Counter without RC | Counter with RC |
|------|--------|--------|--------------------|-----------------|
| 1 | No debounce | 10 | 26 | 10 |
| 2 | Time-based debounce | 10 | 13 | 10 |
| 3 | State-based debounce | 10 | 10 | 10 |
| 4 | Polling with a state machine | 10 | 10 | 10 |

Without the RC filter, only the state-based debounce and the polling state machine gave the correct count. With the RC filter, all 4 sketches gave the correct count. Even the sketch without software debounce counted correctly.
