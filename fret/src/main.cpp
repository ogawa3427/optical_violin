#include <Arduino.h>
#include <Wire.h>
uint8_t goioNums[] = {2, 3, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26};

uint32_t dataX = 0; // ビット列として初期化
uint32_t dataY = 0; // ビット列として初期化
bool dataReadyX = false;  // データが準備完了したかどうかを示すフラグ
bool dataReadyY = true;  // データが準備完了したかどうかを示すフラグ

void requestEvent() {
  Serial.println(dataX, BIN);
  Serial.println(dataY, BIN);
  char bufferX[33]; // 32ビット + NULL終端
  char bufferY[33]; // 32ビット + NULL終端

  // dataXとdataYを文字列に変換
  for (int i = 0; i < 32; i++) {
    bufferX[31 - i] = (dataX & (1 << i)) ? '1' : '0';
    bufferY[31 - i] = (dataY & (1 << i)) ? '1' : '0';
  }
  bufferX[32] = '\0';
  bufferY[32] = '\0';

  // 文字列として送信
  if (dataReadyX) {
    Wire.write(bufferX);
  } else if (dataReadyY) {
    Wire.write(bufferY);
  }
}

void setup() {
  Wire.begin(0x08);
  Wire.onRequest(requestEvent);
  Serial.begin(9600);
  pinMode(25, OUTPUT);
  for (int i = 0; i < sizeof(goioNums)/sizeof(goioNums[0]); i++) {
    pinMode(goioNums[i], INPUT_PULLUP);
  }
}

void loop() {
  if (dataReadyX) {
      dataY = 0; // dataYをリセット
      for (int i = 0; i < sizeof(goioNums)/sizeof(goioNums[0]); i++) {
        dataY |= digitalRead(goioNums[i]) == 0 ? (1 << i) : 0; // ビットをセット
      }
      dataY <<= 4; // 上位4ビットを0で埋める
      dataReadyY = true;
      dataReadyX = false;
  } else if (dataReadyY) {
    dataX = 0; // dataXをリセット
    for (int i = 0; i < sizeof(goioNums)/sizeof(goioNums[0]); i++) {
      dataX |= digitalRead(goioNums[i]) == 0 ? (1 << i) : 0; // ビットをセット
    }
    dataX <<= 4; // 上位4ビットを0で埋める
    dataReadyX = true;
    dataReadyY = false;
  }
  delay(10);
}
