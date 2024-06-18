#include <Arduino.h>
#include <Wire.h>

void setup() {
  Wire.begin(); // I2Cを初期化
  USBSerial.begin(9600); // シリアル通信を9600bpsで開始
}

void loop() {
  Wire.requestFrom(0x08, 32); // I2Cデバイスから32バイトのデータを要求
  while (Wire.available()) { // 受信データがある間続ける
    char c = Wire.read(); // 1バイト読み取る
    USBSerial.print(c); // 読み取ったバイトをシリアルに出力
  }
  USBSerial.println(); // 改行
  delay(100); // 1秒間の遅延
}