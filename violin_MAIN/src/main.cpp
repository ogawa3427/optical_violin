#include <Arduino.h>
#include <M5Unified.h>
#include <M5UnitSynth.h>
#include <note.h>
#include <tone.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "keys.h"

#define USB_SERIAL (1)

int lastNote = 0;
int lastLoopNote = 0;
bool rightClick = false;
bool leftClick = false;
bool pastRightClick = false;
bool pastLeftClick = false;


void printAsHEX(const String &receivedData)
{
#ifdef USB_SERIAL
  for (int i = 0; i < receivedData.length(); i++)
  {
    Serial.print(receivedData[i], HEX); // 受信したデータを16進数で出力
    Serial.print(" ");                  // 数字の間にスペースを入れる
  }
  Serial.println(); // 改行を出力
#endif
}

// パケット設定
// byte[1]でパケットタイプを判別：0x04=マウス、0x08=キーボード
//
// マウス（15バイト）：
// FE 04 00 04 02 00 01 EF 17 19 60 00 00 00 FF  - 上スクロール (byte[14]=0xFF)
// FE 04 00 04 02 00 01 EF 17 19 60 00 00 00 01  - 下スクロール (byte[14]=0x01)
// FE 04 00 04 02 00 01 EF 17 19 60 04 00 00 00  - 押し込み (byte[11]=0x04)
//
// キーボード（19バイト）：
// FE 08 00 04 06 01 01 3C 41 11 21 00 00 0D 0B 00 00 00 00  - 2キー押し
// FE 08 00 04 06 01 01 3C 41 11 21 00 00 0D 00 00 00 00 00  - 1キー押し

struct PacketConfig {
  // パケットタイプ判定
  int typeByteIndex;          // パケットタイプ判定用のバイト位置
  char mouseTypeValue;        // マウスパケットの識別値
  char keyboardTypeValue;     // キーボードパケットの識別値
  
  // マウス設定
  int mousePacketLength;      // マウスパケット長
  int scrollByteIndex;        // スクロール判定用のバイト位置
  char scrollUpValue;         // 上スクロール時の値
  char scrollDownValue;       // 下スクロール時の値
  int clickByteIndex;         // 押し込み判定用のバイト位置
  char clickValue;            // 押し込み時の値
  
  // キーボード設定
  int keyboardPacketLength;   // キーボードパケット長
  int key1ByteIndex;          // キー1のバイト位置
  int key2ByteIndex;          // キー2のバイト位置
};

PacketConfig packetConfig;

// 現在押されているキー（先着2キー）
char currentKey1 = 0x00;
char currentKey2 = 0x00;

// パケットタイプ判定
bool isMousePacket(const String &receivedData)
{
  if (receivedData.length() <= packetConfig.typeByteIndex) return false;
  return receivedData[packetConfig.typeByteIndex] == packetConfig.mouseTypeValue;
}

bool isKeyboardPacket(const String &receivedData)
{
  if (receivedData.length() <= packetConfig.typeByteIndex) return false;
  return receivedData[packetConfig.typeByteIndex] == packetConfig.keyboardTypeValue;
}

// マウス：スクロール上を判定
bool isScrollUp(const String &receivedData)
{
  if (!isMousePacket(receivedData)) return false;
  if (receivedData.length() <= packetConfig.scrollByteIndex) return false;
  return receivedData[packetConfig.scrollByteIndex] == packetConfig.scrollUpValue;
}

// マウス：スクロール下を判定
bool isScrollDown(const String &receivedData)
{
  if (!isMousePacket(receivedData)) return false;
  if (receivedData.length() <= packetConfig.scrollByteIndex) return false;
  return receivedData[packetConfig.scrollByteIndex] == packetConfig.scrollDownValue;
}

// マウス：押し込みを判定
bool isClick(const String &receivedData)
{
  if (!isMousePacket(receivedData)) return false;
  if (receivedData.length() <= packetConfig.clickByteIndex) return false;
  return receivedData[packetConfig.clickByteIndex] == packetConfig.clickValue;
}

// キーボード：キーコード取得（先着2キー）
void updateKeys(const String &receivedData)
{
  if (!isKeyboardPacket(receivedData)) return;
  if (receivedData.length() <= packetConfig.key2ByteIndex) return;
  
  currentKey1 = receivedData[packetConfig.key1ByteIndex];
  currentKey2 = receivedData[packetConfig.key2ByteIndex];
}

enum OuterStates
{
  MAIN,
  VOLUME
};

OuterStates outerState = MAIN;

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

const char *stateToString(states state)
{
  switch (state)
  {
  case NTH0:
    return "NTH0";
  case NTH1:
    return "NTH1";
  case ON:
    return "ON  ";
  case OFF:
    return "OFF ";
  case CHG:
    return "CHG ";
  default:
    return "Unknown State";
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

int timeStamp_LastReceive = 0;

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

int volumeInt = 127;

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


// 音名を決定するための配列
const char *noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

void setup()
{
  auto cfg = M5.config();
  M5.begin(cfg);

  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, 5, 6);

  synth.begin(&Serial1, UNIT_SYNTH_BAUD, 1, 2);
  synth.setInstrument(0, 0, INSTRUMENT_);
  synth.setNoteOn(0, NOTE_C6, volumeInt);
  delay(1000);
  synth.setNoteOff(0, NOTE_C6, 0);

  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(YELLOW);
  M5.Lcd.println("Hello!");
  M5.update();
  delay(1000);

  // パケット設定（環境に合わせて調整）
  // パケットタイプ判定
  packetConfig.typeByteIndex = 1;           // byte[1]でタイプ判別
  packetConfig.mouseTypeValue = 0x04;       // マウス = 0x04
  packetConfig.keyboardTypeValue = 0x08;    // キーボード = 0x08
  
  // マウス設定（15バイトパケット）
  packetConfig.mousePacketLength = 15;
  packetConfig.scrollByteIndex = 14;        // byte[14]がスクロール値
  packetConfig.scrollUpValue = 0xFF;        // 上スクロール = 0xFF
  packetConfig.scrollDownValue = 0x01;      // 下スクロール = 0x01
  packetConfig.clickByteIndex = 11;         // byte[11]が押し込み
  packetConfig.clickValue = 0x04;           // 押し込み = 0x04
  
  // キーボード設定（19バイトパケット）
  packetConfig.keyboardPacketLength = 19;
  packetConfig.key1ByteIndex = 13;          // byte[13]がキー1
  packetConfig.key2ByteIndex = 14;          // byte[14]がキー2

  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setBrightness(90);

  outerState = MAIN;
}

// CtrKeyCfg ctrKeyCfg;

// 変数の初期化部分に過去の値を保存するための変数を追加
uint32_t pastTimeKeep = 2;
uint32_t pastTimeKeep2 = 1;

int pastCurrentNote = 0;
bool pastGoSign = false;
int num = 0;

bool upBool = false;
bool downBool = false;

bool StopFE = false;
bool firstLoop = true;

bool volmeChanged = false;

void loop()
{
  M5.update();
  firstLoop = true;

  String receivedData = "";
  String hexString = "";

  if (StopFE)
  {
    StopFE = false;
    receivedData += 0xEF;
  }

  if (Serial2.available())
  {
    receivedData = Serial2.readStringUntil('\n');
    Serial.print("receivedData: ");
    Serial.println(receivedData);
    while (Serial2.available())
    {
      if (firstLoop)
      {
        receivedData += '\n';
        firstLoop = false;
      }

      char c = Serial2.read();
      if (c == '\n' || c == 0xFE)
      {
        if (c == 0xFE)
        {
          StopFE = true;
        }
        break;
      }
      receivedData += c;
    }
    for (int i = 0; i < receivedData.length(); i++)
    {
      char hex[3];
      sprintf(hex, "%02X", receivedData[i]);
      hexString += hex;
    }
    Serial.print("hexString: ");
    Serial.println(hexString);
  }

  if (outerState == VOLUME)
  {
    if (receivedData.length() > 0)
    {
      Serial.println("VOLUME  ing");
      if (isScrollUp(receivedData))
      {
        volmeChanged = true;
        volumeInt = volumeInt + 2;
      }
      else if (isScrollDown(receivedData))
      {
        volmeChanged = true;
        volumeInt = volumeInt - 2;
      }
    }

    if (volumeInt < 0)
    {
      volumeInt = 0;
    }
    else if (volumeInt > 127)
    {
      volumeInt = 127;
    }

    if (volmeChanged)
    {
      volmeChanged = false;

      Serial.println("VOLUME: " + String(volumeInt));

      M5.Lcd.fillScreen(BLACK);
      M5.Lcd.setCursor(0, 0);
      M5.Lcd.drawCenterString("BtnA:Save Back", 0, 0);
      M5.Lcd.drawCenterString(String(volumeInt), 0, 10);
      M5.Lcd.fillRect(0, 20, volumeInt, M5.Lcd.height() / 4, ORANGE);
    }

    // M5.update();

    if (M5.BtnA.wasReleased())
    {
      outerState = MAIN;
    }
  }
  else if (outerState == MAIN)
  {
    // M5.update();
    if (M5.BtnA.wasReleasedAfterHold())
    {
      outerState = VOLUME;
      Serial.println("VOLUME");
    }

    timeKeep = millis();
    // if (timeKeep % 1000 == 0)
    // {
    //   Serial.println(timeKeep);
    // }

    if (receivedData.length() > 0)
    {
      // パケット処理
      {
        M5.Lcd.setCursor(0, 0);
        // M5.Lcd.fillScreen(BLACK);
        M5.Lcd.setTextColor(YELLOW, BLACK);
        // M5.Lcd.println("Main mode");
        char byte5 = receivedData[4];
        char byte15 = receivedData[13]; // 15バイト目 (インデックスは0から始まるので14)
        char byte16 = receivedData[14];
        char byte17 = receivedData[15];
        char byte18 = receivedData[16];
        char byte19 = receivedData[17]; // 19バイト目 (インデックスは0から始まるので18)
        // Serial.print(byte5, HEX);    // 15バイト目を16進数で出力
        // Serial.print(" ");           // 数字の間にスペースを入れる
        // Serial.print(byte15, HEX);   // 19バイト目を16進数で出力
        // Serial.print(" ");           // 数字の間にスペースを入れる
        // Serial.print(byte16, HEX);   // 5バイト目を16進数で出力
        // Serial.print(" ");           // 数字の間にスペースを入れる
        // Serial.print(byte17, HEX);   // 5バイト目を16進数で出力
        // Serial.println();            // 改行を出力
        // 前回の受信から0.05秒以上経過しているか判定
        int delta = timeKeep - timeStamp_LastReceive;
        timeStamp_LastReceive = timeKeep;
        // if (delta > 25)
        // {
        //   // Serial.print("---------------------------------");
        //   // Serial.println(delta);
        // }
        // for (int i = 0; i < receivedData.length(); i++)
        // {
        //   Serial.print(receivedData[i], HEX); // 受信したデータを16進数で出力
        //   Serial.print(" ");                  // 数字の間にスペースを入れる
        // }
        // Serial.println(); // 改行を出力
        hold = true;
        // if (byte17 == 0xFF && byte5 == 0x03)
        // {
        //   // Serial.print("UpScr");
        //   pastTime = timeKeep;
        // }
        // else if (byte17 == 0x01 && byte5 == 0x03)
        // {
        //   // Serial.print("DownScr");
        //   pastTime = timeKeep;
        // }
        // ビットマッチングでスクロール判定
        if (isScrollUp(receivedData))
        {
          // Serial.print("UpScr");
          pastTime = timeKeep;
          upBool = true;
        }
        else
        {
          upBool = false;
        }
        if (isScrollDown(receivedData))
        {
          // Serial.print("DownScr");
          pastTime = timeKeep;
          downBool = true;
        }
        else
        {
          downBool = false;
        }

        if (byte5 == 0x06)
        {
          M5.Lcd.setCursor(0, 0);
          // M5.Lcd.fillScreen(BLACK);
          if (byte15 == 0x00 && byte16 == 0x00 && byte17 == 0x00 && byte18 == 0x00)
          {
            // Serial.print("OFF");
            hold = false;
            updateNote();
            currentNote = 0;
            // M5.Lcd.println("OFF");
          }
          else if (byte15 != 0x00 && byte16 == 0x00 && byte17 == 0x00 && byte18 == 0x00)
          {
            // Serial.print(byte15, HEX);
            updateNote();
            currentNote = tonedict[byte15];
            pastByte15 = byte15;
            pastByte16 = byte16;
            // M5.Lcd.println("ON");
          }
          else if (byte15 == 0x00 && byte16 != 0x00 && byte17 == 0x00 && byte18 == 0x00)
          {
            // Serial.print(byte16, HEX);
            updateNote();
            currentNote = tonedict[byte16];
            pastByte15 = byte15;
            pastByte16 = byte16;
            // M5.Lcd.println("ON2");
          }
          else if (byte15 != 0x00 && byte16 != 0x00 && byte17 == 0x00 && byte18 == 0x00)
          {
            if (pastByte15 == 0x00 && pastByte16 != 0x00)
            {
              // Serial.print(byte15, HEX);
              pastNote = currentNote;
              currentNote = tonedict[byte15];
            }
            else
            {
              // Serial.print(byte16, HEX);
              pastNote = currentNote;
              currentNote = tonedict[byte16];
            }
            // M5.Lcd.println("ON3");
          }
        }
        else if (byte19 == 0x00 && byte15 != 0x00 && byte5 == 0x06 && byte17 == 0x00 && byte18 == 0x00)
        {
          // Serial.print(byte15, HEX);
          currentNote = tonedict[byte15];
          // M5.Lcd.println("ON4");
        }

        // ビットマッチングでボウイング方向判定（スクロールと同じロジック）
        if (isScrollUp(receivedData))
        {
          // Serial.print("UpBow");
          pastTime = timeKeep;
        }
        else if (isScrollDown(receivedData))
        {
          // Serial.print("DownBow");
          pastTime = timeKeep;
        }

        if ((upBool || downBool) && currentNote != 0)
        {
          Serial.println(currentNote);
        }
        // Serial.println();
        // Serial.print("CurrentNote:");
        // Serial.print(currentNote);
        // Serial.print(" PastNote:");
        // Serial.print(pastNote);
        // Serial.print(" Hold:");
        // Serial.print(hold);
        // Serial.print(" pastHold:");
        // Serial.print(pastHold);
        // Serial.println();
        // M5.Lcd.println(currentNote);
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
      synth.setNoteOn(0, currentNote, volumeInt);
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
      synth.setNoteOn(0, currentNote, volumeInt);
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
    // if (hasChanged)
    if (false)
    {
      Serial.print(num++);
      Serial.print(" ");
      Serial.print("CurrentState:");
      Serial.print(" ");
      Serial.print(stateToString(state));
      Serial.print(" ");
      Serial.print(timeKeep - pastTime);
      Serial.print(" ");
      Serial.print("CurrentNote:");
      Serial.print(currentNote);
      Serial.print(" ");
      Serial.print("PastNote:");
      Serial.print(pastNote);
      Serial.print(" ");
      Serial.print("isNotPassed");
      Serial.print(" ");
      Serial.print(isNotPassed);
      Serial.print(" ");
      Serial.print("GoSign:");
      Serial.print(" ");
      Serial.println(goSign);
    }

    if (pastGoSign != goSign)
    {
      int LineWide = M5.Lcd.width() / 16;
      if (goSign)
      {
        M5.Lcd.fillRect(M5.Lcd.width() - LineWide, 0, LineWide, M5.Lcd.height(), ORANGE);
        M5.Lcd.fillRect(0, 0, LineWide, M5.Lcd.height(), ORANGE);
      }
      else
      {
        M5.Lcd.fillRect(M5.Lcd.width() - LineWide, 0, LineWide, M5.Lcd.height(), BLACK);
        M5.Lcd.fillRect(0, 0, LineWide, M5.Lcd.height(), BLACK);
      }
      // M5.Lcd.fillRect(M5.Lcd.width() * 32, M5.Lcd.height() * 32, M5.Lcd.width() * (1 - (2 / 32)), M5.Lcd.height() * (1 - (2 / 32)), BLACK);
    }

    static String lastNoteName = "NA"; // 前回の音名を保持する変数

    // 音名を取得
    String noteName = "NA";
    if (currentNote > 0)
    {
      int noteIndex = currentNote % 12;
      noteName = String(currentNote / 12) + noteNames[noteIndex];
    }

    // 音名に変化があった場合のみ表示を更新
    if (noteName != lastNoteName)
    {
      int filCN = DARKGREY;
      if (noteName != "NA")
        filCN = YELLOW;

      M5.Lcd.setCursor(0, 0);
      M5.Lcd.fillRect(M5.Lcd.width() * 1 / 16, 0, M5.Lcd.width() * 14 / 16, M5.Lcd.height() * 2 / 3, filCN); // 上2/3をクリア
      M5.Lcd.setTextSize(3);
      M5.Lcd.setTextColor(BLACK);
      M5.Lcd.drawCentreString(noteName, M5.Lcd.width() / 2, M5.Lcd.height() / 8, 2);
      lastNoteName = noteName; // 前回の音名を更新
    }

    String bowDir = "";
    static String lastBowDir = "";
    if (isNotPassed && upBool)
    {
      bowDir = "===>";
    }
    else if (isNotPassed && downBool)
    {
      bowDir = "<===";
    }

    int filCB = DARKGREY;
    if (isNotPassed)
      filCB = CYAN;

    if (bowDir != lastBowDir)
    {
      M5.Lcd.setCursor(0, M5.Lcd.height() * 2 / 3);
      M5.Lcd.fillRect(M5.Lcd.width() * 1 / 16, M5.Lcd.height() * 2 / 3, M5.Lcd.width() * 14 / 16, M5.Lcd.height() / 3, filCB); // 下1/3をクリア
      M5.Lcd.setTextSize(3);
      M5.Lcd.setTextColor(BLACK);
      M5.Lcd.drawCentreString(bowDir, M5.Lcd.width() / 2, M5.Lcd.height() * 2 / 3, 2);
      lastBowDir = bowDir;
    }

    // 現在の値を「過去の値」として保存
    pastTimeKeep = timeKeep - pastTime;
    pastCurrentNote = currentNote;
    pastIsNotPassed = isNotPassed;
    pastGoSign = goSign;
    pastState = state;

    pastHold = hold;
    prevLoopNote = currentNote;
  }

  pastState = state;
  lastLoopNote = currentNote;

  M5.update();
}