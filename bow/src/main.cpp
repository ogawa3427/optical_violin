#include <Arduino.h>
#include <Wire.h>

static int toneNum = 0;
static int lasttoneNum = 0;

static char line = 'i';
bool sw_R
bool sw_L


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
}

void loop()
{
  line = 'E'; //'G','D', 'A', 'E'
  // 1/100秒で変わることはちょっと忘れさせてください
  Wire.requestFrom(0x08, 4);
  uint8_t loopCount = 0;
  while (Wire.available())
  {
    uint8_t c = Wire.read();
    if (loopCount == 3)
    {
      if (c >= 0b1000 && c < 0b10000)
      {
        sw_R = true;
      }
      if (c >= 0b10000 && c < 0b100000)
      {
        sw_L = true;
      }
      
      if (sw_L && !sw_R)
      {
        line = 'E';
      }
      else if (!sw_L && !sw_R)
      {
        line = 'A';
      }
      else if (!sw_L && sw_R)
      {
        line = 'D';
      }
      else if (sw_L && sw_R)
      {
        line = 'G';
      }
    }
    loopCount++; 
  }

  Wire.requestFrom(0x08, 4); // I2Cデバイスから4バイトのデータを要求
  loopCount = 0;
  while (Wire.available())
  {                       
    uint8_t c = Wire.read(); // 1バイト読み取る
    if (loopCount == 0 && line == 'G')
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
      else if (c == 0)
      {
        toneNum = NOTE_G6;
      }
    }
    else if (loopCount == 1 && line == 'G')
    {
      if (c == 0b1)
      {
        toneNum = NOTE_C7;
      } //else if (c == 0) {
      //   toneNum = NOTE_G6;
      // }
    }
    else if (loopCount == 1 && line == 'D')
    {
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
      else if (c == 0)
      {
        toneNum = NOTE_D6;
      }
    }
    else if (loopCount == 1 && line == 'A')
    {
      if (c >= 0b1000000 && c < 0b10000000)
      {
        toneNum = NOTE_AS6;
      }
      else if (c >= 0b10000000 && c < 0b100000000)
      {
        toneNum = NOTE_B6;
      }
      else if (c == 0)
      {
        toneNum = NOTE_A6;
      }
    }
    else if (loopCount == 2 && line == 'A')
    {
      if (c == 0b1)
      {
        toneNum = NOTE_C5;
      }
      else if (c >= 0b10 && c < 0b100)
      {
        toneNum = NOTE_CS5;
      }
      else if (c >= 0b100 && c < 0b1000)
      {
        toneNum = NOTE_D5;
      }
      // else if (c == 0)
      // {
      //   toneNum = NOTE_A5;
      // }
    }
    else if (loopCount == 2 && line == 'E')
    {
      USBSerial.println(c, BIN); // シリアルモニタに表示
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
      else if (c == 0)
      {
        toneNum = NOTE_E5;
      }
    }
    else if (loopCount == 3 && line == 'E')
    {
      USBSerial.println(c, BIN); // シリアルモニタに表示

      if (c == 0b100)
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
    loopCount++;
  }

  if (toneNum != lasttoneNum)
  {
    USBSerial.println(toneNum); // シリアルモニタに表示
    synth.setInstrument(0, 0, instrument);
    synth.setNoteOff(0, lasttoneNum, 0);

    if (toneNum != 0)
    {
      synth.setNoteOn(0, toneNum, 127);
    }
    lasttoneNum = toneNum;
  }
  // USBSerial.println(toneNum); // シリアルモニタに表示

  delay(10);          // 1秒間の遅延
}
