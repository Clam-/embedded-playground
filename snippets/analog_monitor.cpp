// Arduino sketch for sending analog readings over serial
// Reads A0, A1, A2 and sends comma-separated values
// First line is axis labels, subsequent lines are readings

#include <Arduino.h>

#define ARDUINO101

void setup() {
  Serial.begin(115200);
  while (!Serial) {
          ;
      }
  // Send axis labels on first line
  Serial.println("A0,A1,A2");
}

void loop() {
  // Read analog pins
  int a0 = analogRead(A0);
  int a1 = analogRead(A1);
  int a2 = analogRead(A2);

  // Send comma-separated values
  Serial.print(a0);
  Serial.print(",");
  Serial.print(a1);
  Serial.print(",");
  Serial.println(a2);

  // Send readings every 100ms
  delay(20);
}
