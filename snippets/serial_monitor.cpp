#include <Arduino.h>

void setup() {
    Serial.begin(115200);
    while (!Serial) { ; }
    Serial.println("A0,A1,A2");
}

void loop() {
    Serial.print(analogRead(A0));
    Serial.print(",");
    Serial.print(analogRead(A1));
    Serial.print(",");
    Serial.println(analogRead(A2));
    delay(50);
}
