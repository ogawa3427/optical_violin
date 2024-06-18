#include <Arduino.h>
#include <Wire.h>

static String toneName = "Error";
static String lasttoneName = "Error";

static char line = 'i';

void setup()
{
  Wire.begin();          // I2Cを初期化
  USBSerial.begin(9600); // シリアル通信を9600bpsで開始
}

void loop()
{
  line = 'G'; //'D', 'A', 'E'

  Wire.requestFrom(0x08, 4); // I2Cデバイスから4バイトのデータを要求
  uint8_t loopCount = 0;
  while (Wire.available())
  {                       
    char c = Wire.read(); // 1バイト読み取る
    if (loopCount == 0 && line == 'G')
    {
      if (c & 0x08) // 3ビット目をチェック
      {
        toneName = "2As";
      }
      else if (c & 0x04) // 2ビット目をチェック
      {
        toneName = "2A";
      }
      else if (c & 0x02) // 1ビット目をチェック
      {
        toneName = "2B";
      }
      else if (c & 0x01) // 0ビット目をチェック
      {
        toneName = "2H";
      }
      else if (c == 0)
      {
        toneName = "2G";
      }
    }
    else if (loopCount == 1 && line == 'G')
    {
      if (c & 0x80) // 7ビット目をチェック
      {
        toneName = "2C";
      } else if (c == 0) {
        toneName = "2G";
      }
    }
    else if (loopCount == 1 && line == 'D')
    {
      if (c & 0x40) // 6ビット目をチェック
      {
        toneName = "2Es";
      }
      else if (c & 0x20) // 5ビット目をチェック
      {
        toneName = "2E";
      }
      else if (c & 0x10) // 4ビット目をチェック
      {
        toneName = "2F";
      }
      else if (c & 0x08) // 3ビット目をチェック
      {
        toneName = "2Ges";
      }
      else if (c & 0x04) // 2ビット目をチェック
      {
        toneName = "2G";
      }
      else if (c == 0)
      {
        toneName = "2D";
      }
    }
    else if (loopCount == 1 && line == 'A')
    {
      if (c & 0x02) // 1ビット目をチェック
      {
        toneName = "1B";
      }
      else if (c & 0x01) // 0ビット目をチェック
      {
        toneName = "1H";
      }
      else if (c == 0)
      {
        toneName = "1A";
      }
    }
    else if (loopCount == 2 && line == 'A')
    {
      if (c & 0x80) // 7ビット目をチェック
      {
        toneName = "1C";
      }
      else if (c & 0x40) // 6ビット目をチェック
      {
        toneName = "1Des";
      }
      else if (c & 0x20) // 5ビット目をチェック
      {
        toneName = "1D";
      }
      else if (c == 0)
      {
        toneName = "1A";
      }
    }
    else if (loopCount == 2 && line == 'E')
    {
      if (c & 0x10) // 4ビット目をチェック
      {
        toneName = "1F";
      }
      else if (c & 0x08) // 3ビット目をチェック
      {
        toneName = "1Ges";
      }
      else if (c & 0x04) // 2ビット目をチェック
      {
        toneName = "1G";
      }
      else if (c & 0x02) // 1ビット目をチェック
      {
        toneName = "1As";
      }
      else if (c == 0)
      {
        toneName = "1E";
      }
    }
    else if (loopCount == 3 && line == 'E')
    {
      if (c & 0x20) // 5ビット目をチェック
      {
        toneName = "1A";
      }
      else if (c == 0)
      {
        toneName = "1E";
      }
    }
    loopCount++;
  }

  if (toneName != lasttoneName)
  {
    USBSerial.print(toneName); // シリアルモニタに表示
    lasttoneName = toneName;
  }
  USBSerial.println(); // 改行
  delay(100);          // 1秒間の遅延
}
