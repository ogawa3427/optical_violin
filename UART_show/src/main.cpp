#include <M5Unified.h>

void setup() {
  M5.begin();
  
  // UARTを初期化 (TX:GPIO1, RX:GPIO2を使用する例)
  Serial1.begin(115200, SERIAL_8N1, 5, 6);
  USBSerial.begin(115200);
}

void loop() {
  // UARTでデータを受信
  if (Serial1.available()) {
    String receivedData = Serial1.readStringUntil('\n');
    // 受信したデータを16進数で表示
    for (int i = 0; i < receivedData.length(); i++) {
      char c = receivedData[i];
      USBSerial.print(c, HEX); // 16進数で出力
      USBSerial.print(" "); // 数字の間にスペースを入れる
    }
    USBSerial.println(); // 改行を出力
  }

  delay(1);
}