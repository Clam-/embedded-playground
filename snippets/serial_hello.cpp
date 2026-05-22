#include <Arduino.h>

void setup() {
    Serial.begin(115200);
    while (!Serial) {
        ; // wait for serial port (needed on some boards)
    }
    Serial.println("Hello from embedded-playground!");
}

void loop() {
    Serial.print("uptime: ");
    Serial.print(millis() / 1000);
    Serial.println("s");
    delay(2000);
}
