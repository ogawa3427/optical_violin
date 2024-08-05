#include <Arduino.h>
#include <M5UnitSynth.h>
#include <note.h>
#include <tone.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// hahaha
static int INSTRUMENT_ = 41;

enum states
{
  NTH0,
  NTH1,
  ON,
  OFF,
  CHG
};

const char* stateToString(states state) {
  switch (state) {
    case NTH0: return "NTH0";
    case NTH1: return "NTH1";
    case ON: return "ON  ";
    case OFF: return "OFF ";
    case CHG: return "CHG ";
    default: return "Unknown State";
  }
}

states state = NTH0;
states pastState = NTH0;
bool goSign = false;

uint32_t timeKeep = 0;
uint32_t pastTime = 0;
int SUS_BORDER = 70;

bool isNotPassed = false;
bool pastIsNotPassed = false;

// struct NowState
// {
//   int note;
//   bool valBow;
// };

// struct PastState
// {
//   int note;
//   bool valBow;
// };

int instrument = 41;

M5UnitSynth synth;

int VOLUME = 60;

uint32_t timer = 0;

int currentNote = 0;
int pastNote = 0;
int pastPastNote = 0;
int prevLoopNote = 0;

bool hold = false;
bool pastHold = false;
// 0 音を出さない
// 1 音を出す

char pastByte15 = 0x00;
char pastByte16 = 0x00;

bool pastBow = false;

void updateNote()
{
  if (currentNote != pastNote)
  {
    pastPastNote = pastNote;
    pastNote = currentNote;
  }
}

void setup()
{
  // UARTを初期化 (TX:GPIO1, RX:GPIO2を使用する例)
  Serial2.begin(115200, SERIAL_8N1, 5, 6);
  USBSerial.begin(115200);

  synth.begin(&Serial1, UNIT_SYNTH_BAUD, 1, 2);
  synth.setInstrument(0, 0, INSTRUMENT_);
  synth.setNoteOn(0, NOTE_C6, VOLUME);
  delay(1000);
  synth.setNoteOff(0, NOTE_C6, 0);
}

// 変数の初期化部分に過去の値を保存するための変数を追加
uint32_t pastTimeKeep = 0;
int pastCurrentNote = 0;
bool pastGoSign = false;
int num = 0;
void loop()
{
  timeKeep = millis();
  // UARTでデータを受信
  if (Serial2.available())
  {
    String receivedData = Serial2.readStringUntil('\n');
    // 受信したデータの15バイト目と19バイト目を表示
    if (receivedData.length() >= 19)
    {
      char byte5 = receivedData[4];
      char byte15 = receivedData[13]; // 15バイト目 (インデックスは0から始まるので14)
      char byte16 = receivedData[14];
      char byte17 = receivedData[15];
      char byte18 = receivedData[16];
      char byte19 = receivedData[17]; // 19バイト目 (インデックスは0から始まるので18)
      // USBSerial.print(byte15, HEX);   // 15バイト目を16進数で出力
      // USBSerial.print(" ");           // 数字の間にスペースを入れる
      // USBSerial.print(byte19, HEX);   // 19バイト目を16進数で出力
      // USBSerial.print(" ");           // 数字の間にスペースを入れる
      // USBSerial.print(byte5, HEX);    // 5バイト目を16進数で出力
      // USBSerial.println();            // 改行を出力
      // for (int i = 0; i < receivedData.length(); i++)
      // {
      //   USBSerial.print(receivedData[i], HEX); // 受信したデータを16進数で出力
      //   USBSerial.print(" ");                  // 数字の間にスペースを入れる
      // }
      // USBSerial.println(); // 改行を出力
      hold = true;
      if (byte19 == 0xFF && byte5 == 0x02)
      {
        // USBSerial.print("UpScr");
        pastTime = timeKeep;
      }
      else if (byte19 == 0x01 && byte5 == 0x02)
      {
        // USBSerial.print("DownScr");
        pastTime = timeKeep;
      }
      else if (byte5 == 0x06)
      {
        if (byte15 == 0x00 && byte16 == 0x00 && byte17 == 0x00 && byte18 == 0x00)
        {
          // USBSerial.print("OFF");
          hold = false;
          updateNote();
          currentNote = 0;
        }
        else if (byte15 != 0x00 && byte16 == 0x00 && byte17 == 0x00 && byte18 == 0x00)
        {
          // USBSerial.print(byte15, HEX);
          updateNote();
          currentNote = tonedict[byte15];
          pastByte15 = byte15;
          pastByte16 = byte16;
        }
        else if (byte15 == 0x00 && byte16 != 0x00 && byte17 == 0x00 && byte18 == 0x00)
        {
          // USBSerial.print(byte16, HEX);
          updateNote();
          currentNote = tonedict[byte16];
          pastByte15 = byte15;
          pastByte16 = byte16;
        }
        else if (byte15 != 0x00 && byte16 != 0x00 && byte17 == 0x00 && byte18 == 0x00)
        {
          if (pastByte15 == 0x00 && pastByte16 != 0x00)
          {
            // USBSerial.print(byte15, HEX);
            pastNote = currentNote;
            currentNote = tonedict[byte15];
          }
          else
          {
            // USBSerial.print(byte16, HEX);
            pastNote = currentNote;
            currentNote = tonedict[byte16];
          }
        }
      }
      else if (byte19 == 0x00 && byte15 != 0x00 && byte5 == 0x06 && byte17 == 0x00 && byte18 == 0x00)
      {
        // USBSerial.print(byte15, HEX);
        currentNote = tonedict[byte15];
      }
      // USBSerial.println();
      // USBSerial.print("CurrentNote:");
      // USBSerial.print(currentNote);
      // USBSerial.print(" PastNote:");
      // USBSerial.print(pastNote);
      // USBSerial.print(" Hold:");
      // USBSerial.print(hold);
      // USBSerial.print(" pastHold:");
      // USBSerial.print(pastHold);
      // USBSerial.println();
    }
  }
  isNotPassed = timeKeep - pastTime < SUS_BORDER;
  goSign = isNotPassed && currentNote != 0;

  switch (state)
  {
  case NTH0:
    if (goSign)
    {
      state = ON;
    }
    else
    {
      state = NTH0;
    }
    break;
  case NTH1:
    if (goSign)
    {
      if (currentNote != prevLoopNote)
      {
        state = CHG;
      }
      else
      {
        state = NTH1;
      }
    }
    else
    {
      state = OFF;
    }
    break;
  case ON:
    synth.setNoteOff(0, pastNote, 0);
    synth.setNoteOff(0, prevLoopNote, 0);
    synth.setNoteOff(0, pastPastNote, 0);
    synth.setNoteOn(0, currentNote, VOLUME);
    if (goSign)
    {
      if (currentNote != prevLoopNote)
      {
        state = CHG;
      }
      else
      {
        state = NTH1;
      }
    }
    else
    {
      state = OFF;
    }
    break;

  case OFF:
    synth.setNoteOff(0, currentNote, 0);
    synth.setNoteOff(0, pastPastNote, 0);
    synth.setNoteOff(0, prevLoopNote, 0);
    synth.setNoteOff(0, pastNote, 0);
    if (goSign)
    {
      state = ON;
    }
    else
    {
      state = NTH0;
    }
    break;
  case CHG:
    synth.setNoteOff(0, prevLoopNote, 0);
    synth.setNoteOff(0, pastNote, 0);
    synth.setNoteOff(0, pastPastNote, 0);
    synth.setNoteOn(0, currentNote, VOLUME);
    if (goSign)
    {
      if (currentNote != prevLoopNote)
      {
        state = CHG;
      }
      else
      {
        state = NTH1;
      }
    }
    else
    {
      state = OFF;
    }
    break;
  }

  // 変化があったかどうかをチェック
  bool hasChanged = (currentNote != pastCurrentNote) ||
                    (goSign != pastGoSign) ||
                    (isNotPassed != pastIsNotPassed) ||
                    (state != pastState);

  // 変化があった場合のみ出力
  if (hasChanged) {
    USBSerial.print(num++);
    USBSerial.print(" ");
    USBSerial.print("CurrentState:");
    USBSerial.print(" ");
    USBSerial.print(stateToString(state));
    USBSerial.print(" ");
    USBSerial.print(timeKeep - pastTime);
    USBSerial.print(" ");
    USBSerial.print("CurrentNote:");
    USBSerial.print(currentNote);
    USBSerial.print(" ");
    USBSerial.print("PastNote:");
    USBSerial.print(pastNote);
    USBSerial.print(" ");
    USBSerial.print("isNotPassed");
    USBSerial.print(" ");
    USBSerial.print(isNotPassed);
    USBSerial.print(" ");
    USBSerial.print("GoSign:");
    USBSerial.print(" ");
    USBSerial.println(goSign);
  }

  // 現在の値を「過去の値」として保存
  pastTimeKeep = timeKeep - pastTime;
  pastCurrentNote = currentNote;
  pastIsNotPassed = isNotPassed;
  pastGoSign = goSign;
  pastState = state;

  pastHold = hold;
  prevLoopNote = currentNote;
  // pastBow = valBow;
  // delay(1);
}
