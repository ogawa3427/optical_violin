#include <Arduino.h>
#include <Wire.h>
#include <Encoder.h>
#include "driver/pcnt.h"
#include <array>
#include <vector>
bool sustain = false;

// diffの値を格納するキュー
std::vector<int> diffValues;
int aveDiff = 0;

#define ECD_PIN_A 5
#define ECD_PIN_B 6
Encoder encoder(ECD_PIN_A, ECD_PIN_B);

// エンコーダの値を格納する変数
volatile long encoderValue = 0;

void readEncoderTask(void *parameter) {
  long newPosition;
  int diff = 0;
  for (;;) {
    diff = 0;  // newPositionを読み取る前にdiffを0にリセット
    newPosition = encoder.read();
    if (newPosition != encoderValue) {
      encoderValue = newPosition;
      diff = 10;
      // USBSerial.println("diff");
    }
    // キューにdiffの値を保存
    diffValues.push_back(diff);
    if (diffValues.size() > 6) {
      diffValues.erase(diffValues.begin()); // キューが最大サイズを超えたら、最初の値を削除
    }
    // USBSerial.println(encoderValue); // シリアルモニタに表示
    if (diffValues.size() > 0) {
      int sum = 0;
      for (int i = 0; i < diffValues.size(); i++) {
        sum += diffValues[i];
      }
      aveDiff = sum / static_cast<float>(diffValues.size());
    }
    
    vTaskDelay(5 / portTICK_PERIOD_MS); // 10ミリ秒ごとに更新
  }
}

static int toneNum = 0;
static int lasttoneNum = 0;

// static char line = 'i';
// bool sw_R = false;
// bool sw_L = false;


#include <M5UnitSynth.h>
#include <note.h>

M5UnitSynth synth;

int instrument = 41;

void setup()
{
  Wire.begin();          // I2Cを初期化
  USBSerial.begin(9600); // シリアル通信を9600bpsで開始
  synth.begin(&Serial1, UNIT_SYNTH_BAUD, 1, 2);
  synth.setInstrument(0, 0, instrument);
  synth.setNoteOn(0, NOTE_C6, 127);
  delay(1000);
  synth.setNoteOff(0, NOTE_C6, 0);

  xTaskCreatePinnedToCore(
    readEncoderTask,   // タスク関数
    "ReadEncoderTask", // タスク名
    10000,             // スタックサイズ
    NULL,              // タスクパラメータ
    1,                 // 優先度
    NULL,              // タスクハンドル
    0                  // 実行するコア（0または1）
  );
}

void loop()
{
  // line = 'E'; //'G','D', 'A', 'E'
  // // 1/100秒で変ることはちょっと忘れさせてください
  // Wire.requestFrom(0x08, 4);
  uint8_t loopCount = 0;
  // while (Wire.available())
  // {
  //   uint8_t c = Wire.read();
  //   if (loopCount == 3)
  //   {
  //     if ((c & 0b1000) == 0b1000)
  //     {
  //       sw_R = true;
  //     } 
  //     else {
  //       sw_R = false;
  //     }
  //     if (c >= 0b10000 && c < 0b100000)
  //     {
  //       sw_L = true;
  //     }
  //     else
  //     {
  //       sw_L = false;
  //     }
  //     if (sw_L && !sw_R)
  //     {
  //       line = 'E';
  //     }
  //     else if (!sw_L && !sw_R)
  //     {
  //       line = 'A';
  //     }
  //     else if (!sw_L && sw_R)
  //     {
  //       line = 'D';
  //     }
  //     else if (sw_L && sw_R)
  //     {
  //       line = 'G';
  //     }
  //   }
  //   loopCount++; 
  // }

  Wire.requestFrom(0x08, 4); // I2Cデバイスから4バイトのデータを要求
  loopCount = 0;
  while (Wire.available())
  {                       
    uint8_t c = Wire.read(); // 1バイト読み取る
    if (loopCount == 0) // && line == 'G')
    {
      if (c >= 0b10000 && c < 0b100000)
      {
        toneNum = NOTE_GS6;
      }
      else if (c >= 0b100000 && c < 0b1000000)
      {
        toneNum = NOTE_A6;
      }
      else if (c >= 0b1000000 && c < 0b10000000)
      {
        toneNum = NOTE_AS6;
      }
      else if (c >= 0b10000000 && c < 0b100000000)
      {
        toneNum = NOTE_B6;
      }
      // else if (c == 0)
      // {
      //   toneNum = NOTE_G6;
      // }
    }
    else if (loopCount == 1)// && line == 'G')
    {
      if (c == 0b1)
      {
        toneNum = NOTE_C7;
      } //else if (c == 0) {
      //   toneNum = NOTE_G6;
      // }
// }
    // else if (loopCount == 1);// && line == 'D')
    // {
      if (c >= 0b10 && c < 0b100)
      {
        toneNum = NOTE_DS6;
      }
      else if (c >= 0b100 && c < 0b1000)
      {
        toneNum = NOTE_E6;
      }
      else if (c >= 0b1000 && c < 0b10000)
      {
        toneNum = NOTE_F6;
      }
      else if (c >= 0b10000 && c < 0b100000)
      {
        toneNum = NOTE_FS6;
      }
      else if (c >= 0b100000 && c < 0b1000000)
      {
        toneNum = NOTE_G6;
      }
      // else if (c == 0)
      // {
      //   toneNum = NOTE_D6;
      // }
    // }
    // else if (loopCount == 1);// && line == 'A')
    // {
      if (c >= 0b1000000 && c < 0b10000000)
      {
        toneNum = NOTE_AS5;
      }
      else if (c >= 0b10000000 && c < 0b100000000)
      {
        toneNum = NOTE_B5;
      }
      // else if (c == 0)
      // {
      //   toneNum = NOTE_A6;
      // }
    }
    else if (loopCount == 2)// && line == 'A')
    {
      if (c == 0b1)
      {
        toneNum = NOTE_C6;
      }
      else if (c >= 0b10 && c < 0b100)
      {
        toneNum = NOTE_CS6;
      }
      else if (c >= 0b100 && c < 0b1000)
      {
        toneNum = NOTE_D6;
      }
      // else if (c == 0)
      // {
      //   toneNum = NOTE_A5;
      // }
    // }
    // else if (loopCount == 2);// && line == 'E')
    // {
      if (c >= 0b1000 && c < 0b10000) // 4ビット目をチェック
      {
        toneNum = NOTE_F5;
      }
      else if (c >= 0b10000 && c < 0b100000) // 5ビット目をチェック
      {
        toneNum = NOTE_FS5;
      }
      else if (c >= 0b100000 && c < 0b1000000) // 6ビット目をチェック
      {
        toneNum = NOTE_G5;
      }
      else if (c >= 0b1000000 && c < 0b10000000) // 7ビット目をチェック
      {
        toneNum = NOTE_GS5;
      }
      // else if (c == 0)
      // {
      //   toneNum = NOTE_E5;
      // }
    }
    else if (loopCount == 3)// && line == 'E')
    {
      if (c == 0b101)
      {
        toneNum = NOTE_A5;
      }
      else if (c == 0b11101)
      {
        toneNum = NOTE_A5;
      }
      else
      {
        toneNum = toneNum;
      }
      // else if (c == 0)
      // {
      //   toneNum = NOTE_E5;
      // }
    }
    // USBSerial.print(c, BIN);
    // USBSerial.print(" ");
    // if (loopCount == 3)
    // {
    //   USBSerial.println();
    // }
    loopCount++;
  }


  // USBSerial.println(toneNum); // シリアルモニタに表示
  // USBSerial.println(line);
  // USBSerial.println(toneNum);
  // Serial.println(encoder.read());
  // USBSerial.println(aveDiff);

  if (!sustain && aveDiff != 0)
  {
    synth.setInstrument(0, 0, instrument);
    synth.setNoteOff(0, lasttoneNum, 0);
    synth.setNoteOn(0, toneNum, 127);
    lasttoneNum = toneNum;
    sustain = true;

    // MIDIメッセージを送信
    // USBSerial.write(0x90); // Note On
    // USBSerial.write(toneNum); // ノート番号
    // USBSerial.write(127); // ベロシティ
  }
  else if (sustain && toneNum != lasttoneNum)
  {
    synth.setNoteOff(0, lasttoneNum, 0);
    synth.setNoteOn(0, toneNum, 127);
    lasttoneNum = toneNum;

    // // MIDIメッセージを送信
    USBSerial.write(0x90); // Note On
    USBSerial.write(toneNum); // ノート番号
    USBSerial.write(127); // ベロシティ
  }
  else if (sustain && aveDiff == 0)
  {
    synth.setNoteOff(0, lasttoneNum, 0);
    sustain = false;

    // // MIDIメッセージを送信
    USBSerial.write(0x80); // Note Off
    USBSerial.write(lasttoneNum); // ノート番号
    USBSerial.write(0); // ベロシティ
  }

  delay(11);
}


