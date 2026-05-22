# embedded-playground

A PlatformIO project for quickly testing code snippets on various microcontroller boards.

## Setup

Install PlatformIO CLI (if not already installed):

```bash
pip install platformio
# or
brew install platformio
```

## Usage

### 1. Choose a snippet

```bash
./use.sh              # list available snippets
./use.sh blink        # activate the blink snippet
./use.sh serial_hello # activate the serial hello snippet
```

This symlinks the chosen snippet into `src/main.cpp`.

### 2. Build and upload

```bash
pio run -e uno              # build for Arduino Uno
pio run -e esp32 -t upload  # build & upload to ESP32
pio run -e due -t upload    # build & upload to Arduino Due
pio run -e arduino101       # build for Arduino 101
```

### 3. Monitor serial output

```bash
pio device monitor          # default 9600 baud
pio device monitor -b 115200
```

## Adding snippets

Drop a `.cpp` file into `snippets/` — it just needs `#include <Arduino.h>`, `setup()`, and `loop()`. Then activate it with `./use.sh <name>`.

## Supported boards

| Environment   | Board          | Platform       |
|---------------|----------------|----------------|
| `uno`         | Arduino Uno    | Atmel AVR      |
| `due`         | Arduino Due    | Atmel SAM      |
| `esp32`       | ESP32 DevKit   | Espressif 32   |
| `arduino101`  | Arduino 101    | Intel ARC32    |

Add more boards by adding `[env:name]` sections to `platformio.ini`.
