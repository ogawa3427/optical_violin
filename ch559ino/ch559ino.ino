#include <Arduino.h>

void setup() {
  pinMode(0, OUTPUT);
}

void loop() {
  digitalWrite(0, !digitalRead(0));
  delay(500);
}

