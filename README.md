# Embedded Development

This repository exists for education purposes. I use it to learn embedded development and to keep my practice projects in one place.

## Task 4.2: I²C and the SSD1306 OLED Display

This task starts an SSD1306 OLED display (128x64) with I²C and shows a frame on it. The project uses ESP-IDF v6.1, the `i2c_master` driver, and an ESP32-S3 board.

### Wiring

| SSD1306 | ESP32-S3 |
|---|---|
| GND | GND |
| VCC | 3V3 |
| SDA | GPIO6 |
| SCL | GPIO7 |

The display address is `0x3C`. The bus speed is 400 kHz. The program also turns on the internal pull-up resistors on SDA and SCL.

Connect VCC to 3V3, not 5V. The module has pull-up resistors to VCC, and the ESP32-S3 pins accept a maximum of 3.6 V.

### I²C protocol

Each I²C transaction starts with the device address. Then the program sends a control byte:

- `0x00` tells the display that the next bytes are commands.
- `0x40` tells the display that the next bytes are pixel data for the display RAM (GDDRAM).

### Initialization plan

The program sends these commands in this sequence. Each command and its arguments go in one I²C transaction.

| Step | Command | Bytes | Description |
|---|---|---|---|
| 1 | Display OFF | `0xAE` | Turns off the display during the setup. |
| 2 | Clock divide ratio | `0xD5, 0x80` | Sets the default oscillator frequency. |
| 3 | Multiplex ratio | `0xA8, 0x3F` | Sets 64 rows (`0x1F` for a 32-row display). |
| 4 | Display offset | `0xD3, 0x00` | Sets no vertical offset. |
| 5 | Start line | `0x40` | Starts the display at RAM row 0. |
| 6 | Charge pump | `0x8D, 0x14` | Turns on the internal voltage converter for the OLED. |
| 7 | Addressing mode | `0x20, 0x00` | Sets the horizontal addressing mode. |
| 8 | Segment remap | `0xA1` | Connects column 127 to SEG0. |
| 9 | COM scan direction | `0xC8` | Scans from COM63 to COM0. |
| 10 | COM pins configuration | `0xDA, 0x12` | Sets the pin layout for 128x64 (`0x02` for 128x32). |
| 11 | Contrast | `0x81, 0xCF` | Sets the brightness. |
| 12 | Pre-charge period | `0xD9, 0xF1` | Sets the pixel pre-charge time. |
| 13 | VCOMH level | `0xDB, 0x40` | Sets the pixel deselect voltage. |
| 14 | Entire display ON | `0xA4` | Shows the contents of the display RAM. |
| 15 | Normal display | `0xA6` | Sets bit 1 to a lit pixel. |
| 16 | Display ON | `0xAF` | Turns on the display. |

### Frame output

The program keeps a frame buffer of 1024 bytes in RAM. Each byte holds a vertical column of 8 pixels (one page). The program draws a border and the text `SSD1306 I2C OK Z` with a 5x7 font.

To send the frame, the program does these steps:

1. It sets the column range from 0 to 127 (`0x21, 0, 127`).
2. It sets the page range from 0 to 7 (`0x22, 0, 7`).
3. It sends the control byte `0x40` and the 1024 bytes of the frame in one transaction.

### Extra command: rotation by 180°

My extra command turns the image by 180°. It uses two commands from the initialization plan with different values:

| Orientation | Segment remap | COM scan direction |
|---|---|---|
| Normal (0°) | `0xA1` | `0xC8` |
| Rotated (180°) | `0xA0` | `0xC0` |

The segment remap command changes only new data in the display RAM (datasheet, section 10.1.12). Thus, after the two commands, the program sends the frame again.

The `oled_set_rotation()` function does this work. The main loop changes the orientation every 2 seconds and writes the orientation to the console.

### Demo

The video shows the display at start and the 180° rotation every 2 seconds.

<video src="home-task-4-2.mp4" controls width="480"></video>

If the video player does not show, open [home-task-4-2.mp4](home-task-4-2.mp4).
