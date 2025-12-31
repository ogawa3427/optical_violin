#include <Arduino.h>
#include <M5Unified.h>

void printAsHEX(const String &receivedData)
{

  for (int i = 0; i < receivedData.length(); i++)
  {
    char buf[3];
    snprintf(buf, sizeof(buf), "%02X", static_cast<uint8_t>(receivedData[i]));
    Serial.print(buf); // 受信したデータを16進数文字列で出力
    Serial.print(" ");
  }
  Serial.println();
  for (int i = 0; i < receivedData.length(); i++)
  {
    if (i < 10)
    {
      Serial.print(" ");
    }
    Serial.print(i);
    Serial.print(" ");
  }
  Serial.println();
  Serial.println("--------------------------------");
}

uint32_t timeKeep = 0;
uint32_t pastTime = 0;

uint32_t timer = 0;

void setup()
{
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, 5, 6);

  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.println("Hello!");
  delay(1000);

  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setBrightness(90);
}

bool StopFE = false;
bool firstLoop = true;

void loop()
{
  M5.update();
  firstLoop = true;

  String receivedData = "";

  if (StopFE)
  {
    StopFE = false;
    receivedData += 0xEF;
  }

  if (Serial2.available())
  {
    receivedData = Serial2.readStringUntil('\n');

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
  }

  if (receivedData.length() > 0)
  {
    printAsHEX(receivedData);
  }
}