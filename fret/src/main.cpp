#include <Arduino.h>
#include <Wire.h>
uint8_t goioNums[] = {2, 3, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26};//, 27, 28};

uint32_t dataX = 0; // ビット列として初期化
uint32_t dataY = 0; // ビット列として初期化
bool dataReadyX = false;  // データが準備完了したかどうかを示すフラグ
bool dataReadyY = true;  // データが準備完了したかどうかを示すフラグ
int anay = 0;
void requestEvent() {
  if (dataReadyX) {
    Wire.write((uint8_t*)&dataX, sizeof(dataX));
  } else if (dataReadyY) {
    Wire.write((uint8_t*)&dataY, sizeof(dataY));
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
  pinMode(27, INPUT);
  pinMode(28, INPUT);
}

void loop() {
  if (dataReadyX) {
      dataY = 0; // dataYをリセット
      for (int i = 0; i < sizeof(goioNums)/sizeof(goioNums[0]); i++) {
        dataY |= digitalRead(goioNums[i]) == 0 ? (1 << i) : 0; // ビットをセット
      }
      dataY |= (analogRead(27) > 0 && analogRead(27) < 10) ? (1 << 23) : 0; // ビットをセット
      dataY |= (analogRead(28) > 0 && analogRead(28) < 10) ? (1 << 24) : 0; // ビットをセット
      // anay = analogRead(1);
      // if (anay > 0 && anay < 10) {
      //   dataY |= (1 << 24);
      // }
      dataY <<= 4; // 上位4ビットを0で埋める
      dataReadyY = true;
      dataReadyX = false;
  } else if (dataReadyY) {
    dataX = 0; // dataXをリセット
    for (int i = 0; i < sizeof(goioNums)/sizeof(goioNums[0]); i++) {
      dataX |= digitalRead(goioNums[i]) == 0 ? (1 << i) : 0; // ビットをセット
    }
    dataX |= (analogRead(27) > 0 && analogRead(27) < 10) ? (1 << 23) : 0; // ビットをセット
    dataX |= (analogRead(28) > 0 && analogRead(28) < 10) ? (1 << 24) : 0; // ビットをセット
    dataX <<= 4; // 上位4ビットを0で埋める
    dataReadyX = true;
    dataReadyY = false;
  }
  delay(10);
  // delay(100);
  // for (int i = 0; i < 4; i++) {
  //   uint8_t byte = (dataX >> (i * 8)) & 0xFF;
  //   Serial.print(byte, BIN);
  //   Serial.print(" ");
  // }
  // Serial.println();
  // Serial.println(anay);
}
