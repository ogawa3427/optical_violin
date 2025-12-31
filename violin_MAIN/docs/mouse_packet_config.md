# パケット設定メモ

## パケット構造（生データ）

### マウスパケット（15バイト、byte[1] = 0x04）
```
FE 04 00 04 02 00 01 EF 17 19 60 00 00 00 01  - 下スクロール (byte[14]=0x01)
 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 
--------------------------------
FE 04 00 04 02 00 01 EF 17 19 60 00 00 00 FF  - 上スクロール (byte[14]=0xFF)
 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 
--------------------------------
FE 04 00 04 02 00 01 EF 17 19 60 04 00 00 00  - 押し込み (byte[11]=0x04)
 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 
--------------------------------
FE 04 00 04 02 00 01 EF 17 19 60 00 00 00 00  - 何もなし
 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 
```

### キーボードパケット（19バイト、byte[1] = 0x08）
```
FE 08 00 04 06 01 01 3C 41 11 21 00 00 0D 0B 00 00 00 00  - 2キー押し
 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 
--------------------------------
FE 08 00 04 06 01 01 3C 41 11 21 00 00 0D 00 00 00 00 00  - 1キー押し
 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 
--------------------------------
FE 08 00 04 06 01 01 3C 41 11 21 00 00 00 00 00 00 00 00  - キーなし
 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 
```

## 判定ロジック

1. **byte[1]でパケットタイプを判別**
   - 0x04 → マウスパケット
   - 0x08 → キーボードパケット

2. **マウス判定**
   - byte[14] = 0xFF → 上スクロール
   - byte[14] = 0x01 → 下スクロール
   - byte[11] = 0x04 → 押し込み

3. **キーボード判定**
   - byte[13] = キー1のコード（0x00なら押してない）
   - byte[14] = キー2のコード（先着2キー）

## 設定変数（PacketConfig構造体）

| 変数名 | 説明 | デフォルト値 |
|--------|------|--------------|
| `typeByteIndex` | パケットタイプ判定位置 | 1 |
| `mouseTypeValue` | マウスパケット識別値 | 0x04 |
| `keyboardTypeValue` | キーボードパケット識別値 | 0x08 |
| `mousePacketLength` | マウスパケット長 | 15 |
| `scrollByteIndex` | スクロール判定位置 | 14 |
| `scrollUpValue` | 上スクロール値 | 0xFF |
| `scrollDownValue` | 下スクロール値 | 0x01 |
| `clickByteIndex` | 押し込み判定位置 | 11 |
| `clickValue` | 押し込み値 | 0x04 |
| `keyboardPacketLength` | キーボードパケット長 | 19 |
| `key1ByteIndex` | キー1の位置 | 13 |
| `key2ByteIndex` | キー2の位置 | 14 |

## 変更方法

`setup()`内の以下の部分を環境に合わせて修正：

```cpp
// パケットタイプ判定
packetConfig.typeByteIndex = 1;
packetConfig.mouseTypeValue = 0x04;
packetConfig.keyboardTypeValue = 0x08;

// マウス設定
packetConfig.mousePacketLength = 15;
packetConfig.scrollByteIndex = 14;
packetConfig.scrollUpValue = 0xFF;
packetConfig.scrollDownValue = 0x01;
packetConfig.clickByteIndex = 11;
packetConfig.clickValue = 0x04;

// キーボード設定
packetConfig.keyboardPacketLength = 19;
packetConfig.key1ByteIndex = 13;
packetConfig.key2ByteIndex = 14;
```

## 利用可能な関数

```cpp
bool isMousePacket(const String &receivedData);     // マウスパケットか判定
bool isKeyboardPacket(const String &receivedData);  // キーボードパケットか判定
bool isScrollUp(const String &receivedData);        // 上スクロールか判定
bool isScrollDown(const String &receivedData);      // 下スクロールか判定
bool isClick(const String &receivedData);           // 押し込みか判定
void updateKeys(const String &receivedData);        // キーコード更新（currentKey1, currentKey2に格納）
```

## 注意点

- `receivedData`は生バイト配列（char配列）
- `hexString`は16進文字列表記（バイト位置 × 2）
- ビットマッチングなので完全一致ではなく特定バイトのみ判定
