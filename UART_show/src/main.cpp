#include <Arduino.h>
#include <M5UnitSynth.h>
#include <note.h>
#include <tone.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// hahaha
static int INSTRUMENT_ = 41;

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

int VOLUME = 36;

uint32_t timer = 0;

int currentNote = 0;
int pastNote = 0;

int SUS_BORDER = 100;
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
    pastNote = currentNote;
  }
}

// グローバル変数
volatile bool timeoutFlag = false;
unsigned long timeoutPeriod = 10000; // 10秒

// タイマーを管理するタスク
void timerTask(void *parameter) {
  unsigned long lastTime = millis();
  while (true) {
    if (millis() - lastTime > timeoutPeriod) {
      timeoutFlag = true;
      lastTime = millis();
    }
    vTaskDelay(100 / portTICK_PERIOD_MS); // 100msごとにチェック
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

  // タイマータスクを作成
  xTaskCreate(timerTask, "Timer Task", 2048, NULL, 1, NULL);
}

void loop()
{
  bool onNow = false;
  bool offNow = false;
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
      char byte19 = receivedData[17]; // 19バイト目 (インデックスは0から始まるので18)
      USBSerial.print(byte15, HEX);   // 15バイト目を16進数で出力
      USBSerial.print(" ");           // 数字の間にスペースを入れる
      USBSerial.print(byte19, HEX);   // 19バイト目を16進数で出力
      USBSerial.print(" ");           // 数字の間にスペースを入れる
      USBSerial.print(byte5, HEX);    // 5バイト目を16進数で出力
      USBSerial.println();            // 改行を出力
      // for (int i = 0; i < receivedData.length(); i++)
      // {
      //   USBSerial.print(receivedData[i], HEX); // 受信したデータを16進数で出力
      //   USBSerial.print(" ");                  // 数字の間にスペースを入れる
      // }
      // USBSerial.println(); // 改行を出力
      hold = true;
      if (byte19 == 0xFF && byte5 == 0x02)
      {
        USBSerial.print("UpScr");
        timer = 0;
        timeoutFlag = false; // タイマーをリセット
      }
      else if (byte19 == 0x01 && byte5 == 0x02)
      {
        USBSerial.print("DownScr");
        timer = 0;
        timeoutFlag = false; // タイマーをリセット
      }
      else if (byte5 == 0x06)
      {
        if (byte15 == 0x00 && byte16 == 0x00)
        {
          USBSerial.print("OFF");
          hold = false;
          updateNote();
          currentNote = 0;
        }
        else if (byte15 != 0x00 && byte16 == 0x00)
        {
          USBSerial.print(byte15, HEX);
          updateNote();
          currentNote = tonedict[byte15];
          pastByte15 = byte15;
          pastByte16 = byte16;
        }
        else if (byte15 == 0x00 && byte16 != 0x00)
        {
          USBSerial.print(byte16, HEX);
          updateNote();
          currentNote = tonedict[byte16];
          pastByte15 = byte15;
          pastByte16 = byte16;
        }
        else if (byte15 != 0x00 && byte16 != 0x00)
        {
          if (pastByte15 == 0x00 && pastByte16 != 0x00)
          {
            USBSerial.print(byte15, HEX);
            pastNote = currentNote;
            currentNote = tonedict[byte15];
          }
          else
          {
            USBSerial.print(byte16, HEX);
            pastNote = currentNote;
            currentNote = tonedict[byte16];
          }
        }
      }
      else if (byte19 == 0x00 && byte15 != 0x00 && byte5 == 0x06)
      {
        USBSerial.print(byte15, HEX);
        currentNote = tonedict[byte15];
      }
      USBSerial.println();
      USBSerial.print("CurrentNote:");
      USBSerial.print(currentNote);
      USBSerial.print(" PastNote:");
      USBSerial.print(pastNote);
      USBSerial.print(" Hold:");
      USBSerial.print(hold);
      USBSerial.print(" pastHold:");
      USBSerial.print(pastHold);
      USBSerial.println();
    }
  }
  timer++;
  bool valBow = true;
  if ((timer > SUS_BORDER))
  {
    timer = SUS_BORDER + 1;
    valBow = false;
  }
  // USBSerial.print("Timer:");
  // USBSerial.println(timer);
  // else
  // {
  //   hold = true;
  // }

  // USBSerial.print("valBow:");
  // USBSerial.print(valBow);
  // USBSerial.print(" pastBow:");
  // USBSerial.println(pastBow);
  USBSerial.print("Timer:");
  USBSerial.println(timer);

  // タイムアウトフラグが立っている場合、音を鳴らさない
  if (timeoutFlag) {
    // 音を鳴らさない処理
    synth.setNoteOff(0, currentNote, VOLUME);
  }

  if ((currentNote != 0 && pastNote == 0 && valBow) || (currentNote != 0 && valBow && !pastBow))
  {
    synth.setNoteOff(0, pastNote, VOLUME);
    if (currentNote != 0) {
      synth.setNoteOn(0, currentNote, VOLUME);
    }
    // USBSerial.print("NoteOn");
    // USBSerial.println(currentNote);
  }
  else if (!hold && pastHold || timer > SUS_BORDER)
  {
    synth.setNoteOff(0, currentNote, VOLUME);
    // USBSerial.println("NoteOff");
  }
  else if (hold && pastHold && currentNote != pastNote)
  {
    synth.setNoteOff(0, pastNote, VOLUME);
    if (currentNote != 0) {
      synth.setNoteOn(0, currentNote, VOLUME);
    }
    // USBSerial.print("NoteOff");
    // USBSerial.print(pastNote);
    // USBSerial.print("NoteOn");
    // USBSerial.println(currentNote);
  }
  pastHold = hold;
  pastBow = valBow;

  // delay(1);
}
