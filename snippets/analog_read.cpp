#include <Arduino.h>

const int SENSOR_PIN = A0;

void setup() {
    Serial.begin(115200);
    while (!Serial) {
        ;
    }
    Serial.println("Analog read demo — reading A0");
}

void loop() {
    int value = analogRead(SENSOR_PIN);
    Serial.print("A0 = ");
    Serial.println(value);
    delay(500);
}
