#include <Arduino.h>
#include <Wire.h>
#include <Encoder.h>

// エンコーダのピン番号を指定
Encoder myEnc(5, 6);

static int toneNum = 0;
static int lasttoneNum = 0;

static char line = 'i';
bool sw_R = false;
bool sw_L = false;


#include <M5UnitSynth.h>
#include <note.h>

M5UnitSynth synth;

int instrument = 41;

void updateEncoder() {
  static long lastPosition = -999;
  long newPosition = myEnc.read();
  if (newPosition != lastPosition) {
    lastPosition = newPosition;
    USBSerial.println(newPosition);
  }
}


void setup()
{
  Wire.begin();          // I2Cを初期化
  USBSerial.begin(9600); // シリアル通信を9600bpsで開始
  synth.begin(&Serial1, UNIT_SYNTH_BAUD, 1, 2);
  synth.setInstrument(0, 0, instrument);
  synth.setNoteOn(0, NOTE_C6, 127);
  delay(1000);
  synth.setNoteOff(0, NOTE_C6, 0);
  // 割り込みの設定
  attachInterrupt(digitalPinToInterrupt(5), updateEncoder, CHANGE);
  // attachInterrupt(digitalPinToInterrupt(6), updateEncoder, CHANGE);
}

void loop()
{
  // line = 'E'; //'G','D', 'A', 'E'
  // // 1/100秒で変わることはちょっと忘れさせてください
  // Wire.requestFrom(0x08, 4);
  // uint8_t loopCount = 0;
  // while (Wire.available())
  // {
  //   uint8_t c = Wire.read();
  //   if (loopCount == 3)
  //   {
  //     USBSerial.println(c, BIN);
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
}

