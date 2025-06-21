# 大事な定数
# パルス間隔
PULSE_INTERVAL = 7
i = 0

# パケット内のバイトインデックス
PACKET_BOW_DIRECTION_INDEX = 14
PACKET_MOUSE_WHEEL_INDEX = 11  # マウスホイール押し離し検出用のインデックス

# UARTポート1の設定を追加
uart_port1 = 1 # UART1を使用
tx_pin1 = 2
rx_pin1 = 1
baud_rate1 = 31_250 # MIDI標準ボーレート

uart_port2 = 2 # UART2を使用
tx_pin2 = 6
rx_pin2 = 5
baud_rate2 = 115_200

# バイト配列を16進数で表示するヘルパー関数
def print_hex(data, start_index = 0, length = nil)
  return "nil" if data.nil?
  return "empty" if data.empty?

#   # # puts "not nil"
  
  # 文字列の場合はbytes配列に変換
  byte_data = data.is_a?(String) ? data.bytes : data
  
  length ||= byte_data.length - start_index
  return "invalid range" if start_index < 0 || start_index >= byte_data.length
  
  end_index = [start_index + length - 1, byte_data.length - 1].min
  return "invalid range" if end_index < start_index

  result = ""
  (start_index..end_index).each { |i| result += sprintf("%02X ", byte_data[i]) }
  return result.strip
end

# ディスプレイの初期化と設定
if Display.available?
  Display.clear() # ディスプレイをクリア
  Display.set_text_size(1) # テキストサイズを設定
  Display.set_text_color(Display.color565(255, 255, 255)) # 白色テキスト
  Display.println("UART Monitor & MIDI Demo") # タイトル変更
  Display.println("Port1(MIDI): Baud: #{baud_rate1}") # ポート1の情報を更新
  Display.println("TX: #{tx_pin1}, RX: #{rx_pin1}")
  Display.set_text_size(1)
  line_count = 5
else
  line_count = 0
  # # puts "ディスプレイは利用できません"
end

# UARTポート1の初期化を追加
# # puts "UART1(MIDI)を初期化します: ポート=#{uart_port1}, TX=#{tx_pin1}, RX=#{rx_pin1}, ボーレート=#{baud_rate1}, 8N1"
# sleep(0.1)
# MIDI標準の 8bit, Non-parity, 1 stop bit で初期化 (Arduinoコード参考)
# rx_buffer_size をハードウェアFIFO長(128)より大きい値(例: 256)に変更
if UART.init(uart_port1, tx_pin1, rx_pin1, baud_rate1, 256, 256, 1)
  LED.set([0x00, 0xFF, 0x00])
  Display.println("UART1(MIDI) init success (8N1)") # 設定情報を追加
  # # puts "UART1(MIDI)初期化成功 (8N1)" # 設定情報を追加
  # sleep(0.3)
  LED.set([0, 0, 0])
else
  LED.set([0xFF, 0x00, 0x00])

  # # puts "UART1(MIDI)初期化失敗" # 表示を更新
  Display.println("UART1(MIDI) init failed") # 表示を更新
  sleep(1)
  LED.set([0, 0, 0])
end

# UARTポート2の初期化を追加
# # puts "UART2(USB)を初期化します: ポート=#{uart_port2}, TX=#{tx_pin2}, RX=#{rx_pin2}, ボーレート=#{baud_rate2}, 8N1"
# sleep(0.1)
# MIDI標準の 8bit, Non-parity, 1 stop bit で初期化 (Arduinoコード参考)
# rx_buffer_size をハードウェアFIFO長(128)より大きい値(例: 256)に変更
if UART.init(uart_port2, tx_pin2, rx_pin2, baud_rate2, 4096, 4096, 1)
  LED.set([0x00, 0xFF, 0x00])
  Display.println("UART2(USB) init success (8N1)") # 設定情報を追加
  # # puts "UART2(USB)初期化成功 (8N1)" # 設定情報を追加
  # sleep(0.3)
  LED.set([0, 0, 0])
else
  LED.set([0xFF, 0x00, 0x00])

  # # puts "UART2(USB)初期化失敗" # 表示を更新
  Display.println("UART2(USB) init failed") # 表示を更新
  sleep(1)
  LED.set([0, 0, 0])
end

max_lines = 15

# UART読み取りのタイムアウト設定（ミリ秒）
timeout_ms = 500

# MIDIコマンド定数
MIDI_CMD_NOTE_OFF = 0x80
MIDI_CMD_NOTE_ON = 0x90
MIDI_CMD_CONTROL_CHANGE = 0xB0
MIDI_CMD_PROGRAM_CHANGE = 0xC0

# キーコードとMIDIノート番号の対応表 (tone.h の tonedict より)
# note.h と tone.h を参照して作成
TONE_DICT = {
  0x1D => 64, # KEYBOARD_Z  -> NOTE_E4
  0x1B => 65, # KEYBOARD_X  -> NOTE_F4
  0x06 => 66, # KEYBOARD_C  -> NOTE_FS4
  0x19 => 67, # KEYBOARD_V  -> NOTE_G4
  0x05 => 68, # KEYBOARD_B  -> NOTE_GS4
  0x11 => 69, # KEYBOARD_N  -> NOTE_A4
  0x10 => 70, # KEYBOARD_M  -> NOTE_AS4
  0x36 => 71, # KEYBOARD_COMMA -> NOTE_B4
  0x37 => 72, # KEYBOARD_DOT -> NOTE_C5
  0x38 => 73, # KEYBOARD_SLASH -> NOTE_CS5
  0x87 => 74, # KEYBOARD_INTERNATIONAL1 -> NOTE_D5 # (多分「ろ」キー？)
  # 2段目 (ASDFG...)
  0x04 => 69, # KEYBOARD_A  -> NOTE_A4
  0x16 => 70, # KEYBOARD_S  -> NOTE_AS4
  0x07 => 71, # KEYBOARD_D  -> NOTE_B4
  0x09 => 72, # KEYBOARD_F  -> NOTE_C5
  0x0A => 73, # KEYBOARD_G  -> NOTE_CS5
  0x0B => 74, # KEYBOARD_H  -> NOTE_D5
  0x0D => 75, # KEYBOARD_J  -> NOTE_DS5
  0x0E => 76, # KEYBOARD_K  -> NOTE_E5
  0x0F => 77, # KEYBOARD_L  -> NOTE_F5
  0x33 => 78, # KEYBOARD_SEMICOLON -> NOTE_FS5
  0x34 => 79, # KEYBOARD_APOSTROPHE -> NOTE_G5
  # 3段目 (QWERTY...)
  0x14 => 74, # KEYBOARD_Q -> NOTE_D5
  0x1A => 75, # KEYBOARD_W -> NOTE_DS5
  0x08 => 76, # KEYBOARD_E -> NOTE_E5
  0x15 => 77, # KEYBOARD_R -> NOTE_F5
  0x17 => 78, # KEYBOARD_T -> NOTE_FS5
  0x1C => 79, # KEYBOARD_Y -> NOTE_G5
  0x18 => 80, # KEYBOARD_U -> NOTE_GS5
  0x0C => 81, # KEYBOARD_I -> NOTE_A5
  0x12 => 82, # KEYBOARD_O -> NOTE_AS5
  0x13 => 83, # KEYBOARD_P -> NOTE_B5
  0x2F => 84, # KEYBOARD_LEFT_BRACKET -> NOTE_C6
  # 4段目 (1234...) - これはCのコードにコメントアウトされてるけど、念のため
  0x1E => 79, # KEYBOARD_1 -> NOTE_G5
  0x1F => 80, # KEYBOARD_2 -> NOTE_GS5
  0x20 => 81, # KEYBOARD_3 -> NOTE_A5
  0x21 => 82, # KEYBOARD_4 -> NOTE_AS5
  0x22 => 83, # KEYBOARD_5 -> NOTE_B5
  0x23 => 84, # KEYBOARD_6 -> NOTE_C6
  0x24 => 85, # KEYBOARD_7 -> NOTE_CS6
  0x25 => 86, # KEYBOARD_8 -> NOTE_D6
  0x26 => 87, # KEYBOARD_9 -> NOTE_DS6
  0x27 => 88, # KEYBOARD_0 -> NOTE_E6
  0x2D => 89 # KEYBOARD_MINUS -> NOTE_F6
}

# MIDIメッセージ送信ヘルパー
def send_midi_command(port, command_bytes)
  # # # puts "Sending MIDI: #{command_bytes.map { |b| "0x#{b.to_s(16).upcase}" }.join(', ')}" # デバッグ用
  UART.write(port, command_bytes)
end

# 楽器設定 (Control Change Bank Select + Program Change)
def set_instrument(port, channel, bank, value)
  # Control Change (Bank Select MSB - 通常は0)
  # M5UnitSynthのコードでは bank が MSB 0x00 に直接入っているように見えるが、
  # 一般的なGM音源などは LSB 0x20 も使う場合があるため、ここではMSBのみ送信
  cmd_cc = [
    (MIDI_CMD_CONTROL_CHANGE | (channel & 0x0f)),
    0x00, # Bank Select MSB (Controller number)
    bank # Bank number (MSB value)
  ]
  send_midi_command(port, cmd_cc)
  sleep(0.01) # コマンド間に少し待機

  # Program Change
  cmd_pc = [
    (MIDI_CMD_PROGRAM_CHANGE | (channel & 0x0f)),
    value # Instrument number
  ]
  send_midi_command(port, cmd_pc)
  sleep(0.01)
end

# ノートオン
def set_note_on(port, channel, pitch, velocity)
  cmd = [(MIDI_CMD_NOTE_ON | (channel & 0x0f)), pitch, velocity]
  send_midi_command(port, cmd)
end

# ノートオフ
def set_note_off(port, channel, pitch, velocity = 0) # velocityは通常0だが引数としては受け取れるようにしておく
  cmd = [
    (MIDI_CMD_NOTE_OFF | (channel & 0x0f)),
    pitch,
    0x00 # Note Offではベロシティは通常0
  ]
  send_midi_command(port, cmd)
end

# 任意のCCを送信
def set_cc(port, channel, cc_number, value)
  cmd = [(0xb0 | (channel & 0x0f)), cc_number & 0x7f, value & 0x7f]
  send_midi_command(port, cmd)
end

# Expression (CC11)
def set_expression(port, channel, value)
  set_cc(port, channel, 11, value)
end

# Channel Volume (CC7)
def set_channel_volume(port, channel, value)
  set_cc(port, channel, 7, value)
end

# Modulation Wheel (CC1)
def set_modulation(port, channel, value)
  set_cc(port, channel, 1, value)
end

# Pitch Bend （14bit）
def set_pitch_bend(port, channel, bend_value)
  # bend_value は -8192 … +8191 の範囲
  raw = bend_value + 8192
  lsb = raw & 0x7f
  msb = (raw >> 7) & 0x7f
  cmd = [(0xe0 | (channel & 0x0f)), lsb, msb]
  send_midi_command(port, cmd)
end

# Reverb Send (CC91)
def set_reverb_send(port, channel, value)
  set_cc(port, channel, 91, value)
end

# Chorus Send (CC93)
def set_chorus_send(port, channel, value)
  set_cc(port, channel, 93, value)
end

# Brightness / Filter Cutoff (CC74)
def set_brightness(port, channel, value)
  set_cc(port, channel, 74, value)
end

# Portamento On/Off (CC65) + Time (CC5)
def set_portamento(port, channel, on_off, time_value=nil)
  set_cc(port, channel, 65, on_off ? 127 : 0)
  set_cc(port, channel, 5, time_value) if time_value
end

# Sustain Pedal (CC64)
def set_sustain(port, channel, on_off)
  set_cc(port, channel, 64, on_off ? 127 : 0)
end

# Aftertouch (Channel Pressure)
def set_aftertouch(port, channel, pressure_value)
  cmd = [(0xd0 | (channel & 0x0f)), pressure_value & 0x7f]
  send_midi_command(port, cmd)
end

# NRPN送信用：MSB(99), LSB(98), Data Entry MSB(6), LSB(38)
def set_nrpn(port, channel, param_msb, param_lsb, value_msb, value_lsb=0)
  cmds = []
  cmds << [(0xb0|(channel&0x0f)), 99, param_msb&0x7f]
  cmds << [(0xb0|(channel&0x0f)), 98, param_lsb&0x7f]
  cmds << [(0xb0|(channel&0x0f)), 6,  value_msb&0x7f]
  cmds << [(0xb0|(channel&0x0f)), 38, value_lsb&0x7f]
  cmds.each { |cmd| send_midi_command(port, cmd) }
end

# Master Volume (NRPN 0x0E 0x73 → 3707h)
def set_master_volume(port, channel, value_msb, value_lsb=0)
  # NRPN #3707h → MSB=0x37, LSB=0x07
  set_nrpn(port, channel, 0x37, 0x07, value_msb, value_lsb)
end



# MIDI送信関連
midi_channel = 0 # チャンネル1 (0-15)
# midi_note = 72 # これはもう使わんな
midi_velocity = 100 # ベロシティ (0-127)
# last_midi_send_time = Time.now # Timeクラスは使えないので削除
# midi_interval = 2 # 固定sleepにするので不要
set_instrument(uart_port1, midi_channel, 0, 40) # 楽器設定 (バイオリン)

Display.set_text_size(2)

pastnote = 0

# バッファをクリアする関数を追加
def clear_uart_buffer(port)
  UART.read(port, 1) while UART.available(port) > 0
end

# パケット処理用のバッファ
uart2_buffer = ""

# 正しいパケットを抽出する関数
def extract_packets(buffer)
  packets = []

  # バッファが十分なサイズになるまで処理
  while buffer.length >= 2 # 少なくとも識別子と種類を見るために2バイト必要
    # FEで始まるパケットを探す  
    start_index = buffer.index(0xFE.chr)

    # パケットの開始マーカーが見つからない場合
    if start_index.nil?
      # バッファをクリア
      return [], ""
    end

    # 開始位置より前のデータを削除
    buffer = buffer[start_index..-1] if start_index > 0

    # パケット識別子の次のバイトをチェックするための最小サイズチェック
    break if buffer.length < 2

    # FEで始まるパケットを処理
    if buffer[0].ord == 0xFE
      # 2バイト目でパケットの種類/長さを判断
      packet_type = buffer[1].ord

      if packet_type == 0x08 && buffer.length >= 20
        # FE 08から始まる20バイトパケット
        packet = buffer[0...20]
        buffer = buffer[20..-1] || ""
        packets << packet
      elsif packet_type == 0x04 && buffer.length >= 16
        # FE 04から始まる16バイトパケット
        packet = buffer[0...16]
        buffer = buffer[16..-1] || ""
        packets << packet
      elsif buffer.length >= 19
        # 19バイトパケットの可能性をチェック（識別子不明なので長さベース）
        # 一般的に FE + 識別子 + 17バイトのデータ形式と仮定
        packet = buffer[0...19]
        buffer = buffer[19..-1] || ""
        packets << packet
      else
        # 不明なパケットタイプまたはデータ不足なので1バイト進める
        buffer = buffer[1..-1] || ""
      end
    else
      # FEで始まらない場合は1バイト進める
      buffer = buffer[1..-1] || ""
    end
  end

  # 残りのバッファと抽出されたパケットを返す
#   # # puts packets
  # # puts "buffer: #{print_hex(buffer.bytes)}"
  return packets, buffer
end

# ステートマシン
bowTimeStamp = 0
newBowTimeStamp = 0 # newIsBowingを決定するために保持
prevBowTimeStamp = Utils.millis() # 前回パケット受信時刻
bowSpeed = 0.0 # 現在の弓速度

isBowing = false
newIsBowing = false # ループ内で計算する

isUping = false
newIsUping = false

# マウスホイール押し離し状態
isWheelPressed = false
newIsWheelPressed = false
lastWheelValue = 0x00  # 前回のマウスホイール値
wheelStateChanged = false  # マウスホイール状態変化フラグ

benchMark = 0
benchMarkTime = 0

# 20バイトパケットの13-16バイト目を保存するスタック
packet_stack = []
MAX_STACK_SIZE = 100 # スタックの最大サイズ

# スタックが更新されたかどうかを示すフラグ
stack_updated = false

# 前回のパケット値を保存する変数を初期化
last_packet_values = []

# 現在鳴っている音を追跡する変数
current_playing_note = nil

Display.clear()
Display.println("L287")
# メインループ
while true
  # Display.println(i)

  # # # puts i
  benchMarkTime = Utils.millis()
  # # # puts benchMarkTime - benchMark
  benchMark = benchMarkTime
  currentTimeStamp = Utils.millis()
  # Display.println("L294")

  # 弓が動いているかどうかの判定 (最後にパケットを受信してから PULSE_INTERVAL 以内か)
  # bowTimeStamp は FE 04 または FE 08 受信時に更新される
  newIsBowing = (currentTimeStamp - bowTimeStamp <= PULSE_INTERVAL)

  changeDisp = false # 表示更新フラグ初期化

  # UART2からデータ受信チェック
  available_bytes = UART.available(uart_port2)
  # available_bytes = 0
  if available_bytes > 0
    # # puts "available_bytes: #{available_bytes}"
    # 新しいデータを読み取り、バッファに追加
    new_data = UART.read(uart_port2, available_bytes)
    # # puts "Raw received data: #{print_hex(new_data.bytes)}"
    uart2_buffer += new_data

    # バッファからパケットを抽出
    packets, uart2_buffer = extract_packets(uart2_buffer)

    # 抽出したパケットを処理
    packets.each do |packet|
        # # puts "pct"
# パケットをHEXで# # putsする
        # # puts "packet: #{print_hex(packet.bytes)}"
      # パケットの処理
      if packet.bytes[0] == 0xFE
        # # puts "=== Complete Packet ==="
        # # puts "Raw: #{print_hex(packet.bytes)}"
        # # puts "Length: #{packet.length} bytes"
        # # puts "Type: 0x#{packet.bytes[1].to_s(16).upcase}" if packet.length > 1

        packet_type = packet.bytes[1]
        if packet_type == 0x04 && packet.length == 16
          # 16バイトパケットの処理 (弓の方向 + マウスホイール)
          # # puts "--- FE 04 Packet (16 bytes) ---"
          # # puts "Bow direction (byte 14): 0x#{packet.bytes[PACKET_BOW_DIRECTION_INDEX].to_s(16).upcase}"
          # # puts "Mouse wheel (byte 11): 0x#{packet.bytes[PACKET_MOUSE_WHEEL_INDEX].to_s(16).upcase}"
          
          # マウスホイール押し離し判定（11バイト目）
          current_wheel_value = packet.bytes[PACKET_MOUSE_WHEEL_INDEX]
          if current_wheel_value != lastWheelValue
            # マウスホイール状態が変化した
            # 4ビット目（0x08）が1なら押されている、0なら離されている
            newIsWheelPressed = ((current_wheel_value & 0x04) != 0)
            # # # puts "Mouse wheel: #{current_wheel_value.to_s(16)} -> #{newIsWheelPressed ? 'Pressed' : 'Released'}"
            lastWheelValue = current_wheel_value
            wheelStateChanged = true
          end
          
          # 弓の方向判定（14バイト目）
          # Display.println("FE 04 パケット (16バイト)")
          # 14バイト目だけ表示
          # Display.println(packet.bytes[14].to_s)
          if packet.bytes[PACKET_BOW_DIRECTION_INDEX] == 0x00
            # Display.println("00")
          elsif packet.bytes[PACKET_BOW_DIRECTION_INDEX] == 0x01
            # Display.println("01")
            newBowTimeStamp = Utils.millis()
            # Δt を計算（ミリ秒）
            delta = newBowTimeStamp - prevBowTimeStamp
            prevBowTimeStamp = newBowTimeStamp
            # 速度をスケール（たとえば 1000ms/Δt で正規化）
            bowSpeed = delta > 0 ? (1000.0 / delta) : 0.0
            newIsUping = true
            bowTimeStamp = newBowTimeStamp # FE 04 受信時も更新
          elsif packet.bytes[PACKET_BOW_DIRECTION_INDEX] == 0xFF
            # Display.println("02")
            newBowTimeStamp = Utils.millis()
            # 同様に速度計算
            delta = newBowTimeStamp - prevBowTimeStamp
            prevBowTimeStamp = newBowTimeStamp
            bowSpeed = delta > 0 ? (1000.0 / delta) : 0.0
            newIsUping = false
            bowTimeStamp = newBowTimeStamp # FE 04 受信時も更新
          end
          changeDisp = true if newIsUping != isUping
          changeDisp = true if wheelStateChanged
        elsif packet_type == 0x08 && packet.length == 20
          # 20バイトパケットの処理 (キー入力)
          bowTimeStamp = newBowTimeStamp

          # # puts "--- FE 08 Packet (20 bytes) ---"
          # # puts "Bytes 0-7:   #{print_hex(packet.bytes, 0, 8)}"
          # # puts "Bytes 8-15:  #{print_hex(packet.bytes, 8, 8)}"
          # # puts "Bytes 16-19: #{print_hex(packet.bytes, 16, 4)}"
          # # puts "Key area (12-15): #{print_hex(packet.bytes, 12, 4)}"

          # 13-16バイト目（インデックス12-15）を取得
          current_values = []
          (12..15).each do |i|
            current_values << packet.bytes[i] if i < packet.bytes.length
          end

          # 前回の値と比較して新しく増えた値（押されたキー）を見つける
          new_values = []
          current_values.each do |v|
            next if v == 0x00 # 0x00 は無視
            found = false
            last_packet_values.each { |lv| found = true if v == lv }
            new_values << v unless found
          end

          # 前回あって今回ない値（離されたキー）を見つける
          removed_values = []
          last_packet_values.each do |lv|
            next if lv == 0x00 # 0x00 は無視
            found = false
            current_values.each { |v| found = true if v == lv }
            removed_values << lv unless found
          end

          # キューの更新フラグをリセット (MIDI送信ロジックで使う)
          stack_updated = false

          # 新しい値があればフラグを立て、スタックに追加
          unless new_values.empty?
            stack_updated = true
            new_values.each do |v|
              packet_stack.push(v)
              packet_stack.shift if packet_stack.size > MAX_STACK_SIZE
            end
          end

          # 削除された値があればフラグを立て、スタックから削除
          unless removed_values.empty?
            stack_updated = true
            removed_values.each { |v| packet_stack.delete_if { |x| x == v } }
          end

          # 現在の値を保存
          last_packet_values = current_values.dup

          # === MIDIノート送信処理 ===
          # このブロックは削除し、ループの最後に移動
          # if stack_updated
          #   # 現在鳴っている音をオフにする
          #   if current_playing_note
          #     set_note_off(uart_port1, midi_channel, current_playing_note)
          #     current_playing_note = nil
          #   end
          #
          #   # スタックの末尾（最新のキー）の音だけを鳴らす
          #   if !packet_stack.empty?
          #     key_code = packet_stack.last
          #     note = TONE_DICT[key_code]
          #     if note
          #       set_note_on(uart_port1, midi_channel, note, midi_velocity)
          #       current_playing_note = note
          #       # # puts "単音演奏: Key=0x#{key_code.to_s(16)} Note=#{note}"
          #     end
          #   end
          # end
          # =======================

          # デバッグ用のスタック表示
          if !packet_stack.empty?
            # # # puts "Stack size: #{packet_stack.size}"
            # puts "内容: #{packet_stack.map { |v| sprintf("0x%02X", v) }.join(" ")}"
          end
        elsif packet.length == 19
          # 19バイトパケットの処理（弓の方向判定 + マウスホイール押し込み判定）
          # puts "--- 19 Byte Packet ---"
          # puts "Type: 0x#{packet_type.to_s(16).upcase}"
          # puts "Bytes 0-7:   #{print_hex(packet.bytes, 0, 8)}"
          # puts "Bytes 8-15:  #{print_hex(packet.bytes, 8, 8)}"
          # puts "Bytes 16-18: #{print_hex(packet.bytes, 16, 3)}"
          # puts "Full packet: #{print_hex(packet.bytes)}"
          
          # マウスホイール押し込み判定（13バイト目、後ろから6番目）
          wheel_value_19 = packet.bytes[12]
          # puts "Wheel value (byte 13): 0x#{wheel_value_19.to_s(16).upcase}"
          if wheel_value_19 == 0x04
            if !newIsWheelPressed  # 状態が変化した時のみ更新
              newIsWheelPressed = true
              wheelStateChanged = true
              # puts "Wheel pressed (19-byte packet)"
            end
          else
            if newIsWheelPressed  # 状態が変化した時のみ更新
              newIsWheelPressed = false
              wheelStateChanged = true
              # puts "Wheel released (19-byte packet)"
            end
          end
          
          # 14バイト目で弓の方向判定
          bow_direction = packet.bytes[16]
          # puts "Bow direction (byte 16): 0x#{bow_direction.to_s(16).upcase}"
          
          if bow_direction == 0xFF
            # FFの時はupbow
            newBowTimeStamp = Utils.millis()
            delta = newBowTimeStamp - prevBowTimeStamp
            prevBowTimeStamp = newBowTimeStamp
            bowSpeed = delta > 0 ? (1000.0 / delta) : 0.0
            newIsUping = true
            bowTimeStamp = newBowTimeStamp
            # puts "Upbow detected (FF)"
          elsif bow_direction == 0x01
            # 01の時はdownbow
            newBowTimeStamp = Utils.millis()
            delta = newBowTimeStamp - prevBowTimeStamp
            prevBowTimeStamp = newBowTimeStamp
            bowSpeed = delta > 0 ? (1000.0 / delta) : 0.0
            newIsUping = false
            bowTimeStamp = newBowTimeStamp
            # puts "Downbow detected (01)"
          end
          changeDisp = true if newIsUping != isUping
          changeDisp = true if wheelStateChanged
        else
          # # puts "--- Unknown Packet ---"
          # # puts "Type: 0x#{packet_type.to_s(16).upcase}, Length: #{packet.length}"
          # # puts "Data: #{print_hex(packet.bytes, 0, [packet.length, 16].min)}" # 最大16バイトまで表示
        end
      else
        # 無視する（何も処理しない）
      end
    end

    # パケット受信時にはbowing状態をtrueに -> これはキー入力とは別に管理すべきか？一旦コメントアウトしておく
    # newIsBowing = true

    # バッファが大きくなりすぎたらクリア（異常状態防止）
    if uart2_buffer.length > 1024
      # puts "Buffer overflow, clearing"
      uart2_buffer = ""
    end
  end

  # 現在時刻と最後のパルス時刻の差が一定以上ならbowing終了 -> ループ先頭の判定に移動

  # 状態に変化があるか確認 (弓の方向のみ)
  # changeDisp = false # ループ先頭で初期化するように変更
  changeDisp = true if newIsBowing != isBowing # isBowing の変化も表示更新トリガーに
  changeDisp = true if newIsUping != isUping

  # 状態を更新（表示の前に更新）
  isBowing = newIsBowing # isBowing を更新
  isUping = newIsUping
  isWheelPressed = newIsWheelPressed  # マウスホイール状態も更新
  # bowTimeStamp = newBowTimeStamp # パケット受信時に更新するように変更

  # 弓の状態が変化した場合のみ表示を更新
  if changeDisp
    Display.set_text_size(1)
    Display.clear()
    Display.println("Bowing: #{isBowing}") # Bowing状態も表示
    Display.println("Uping: #{isUping}") # 弓の方向だけ表示
    Display.println("Wheel: #{isWheelPressed}") # マウスホイール状態も表示
  end

  # === 新しいMIDIノート送信ロジック ===
  target_note = nil
  if !packet_stack.empty? && isBowing
    # スタックが空でなく、弓が動いている場合
    key_code = packet_stack.last
    target_note = TONE_DICT[key_code] # 鳴らすべきノート
  end

  if target_note != current_playing_note
    # 鳴らすべき音と現在鳴っている音が違う場合

    # まず現在鳴っている音を止める
    if current_playing_note
      set_note_off(uart_port1, midi_channel, current_playing_note)
      # # # puts "Note OFF: #{current_playing_note}" if current_playing_note # デバッグ用
    end

    # 新しい音を鳴らす (target_noteがnilでなければ)
    if target_note && isBowing
      set_note_on(uart_port1, midi_channel, target_note, midi_velocity)
      # 新しいノートオンと同時に、現在のisWheelPressed状態に基づいてモジュレーションを設定
      if isWheelPressed
        set_modulation(uart_port1, midi_channel, 127)
        # # # puts "Vibrato ON (new note, wheel IS pressed)"
      else
        # ホイールが押されていなければ、新しいノートではビブラートOFF
        set_modulation(uart_port1, midi_channel, 0)
        # # # puts "Vibrato OFF (new note, wheel NOT pressed)"
      end
      # # # puts "Note ON: #{target_note} (Key: 0x#{packet_stack.last.to_s(16)})" # デバッグ用
    end

    # 現在鳴っている音を更新
    current_playing_note = target_note
  end

  # === マウスホイールビブラート制御 ===
  # マウスホイール状態が変化した時のみビブラート制御
  # if wheelStateChanged && current_playing_note
  #   if isWheelPressed
  #     # 強めのビブラート（モジュレーション最大値）
  #     set_modulation(uart_port1, midi_channel, 127)
  #     # # # puts "Vibrato ON (強め)" # デバッグ用
  #   else
  #     # ビブラートオフ
  #     set_modulation(uart_port1, midi_channel, 0)
  #     # # # puts "Vibrato OFF" # デバッグ用
  #   end
  # end
  
  # 弓速度 bowSpeed に合わせてビブラート深度を動的制御
  # min_speed, max_speed はあらかじめ適当に決めておくこと
  min_speed = 1.0    # 例：最小速度（1ms間隔→1000Hz相当）
  max_speed = 50.0   # 例：最大速度
  # 線形マッピング
  depth = 0 # 初期化
  if max_speed > min_speed # ゼロ除算を避ける
    depth = ((bowSpeed - min_speed) / (max_speed - min_speed)) * 127.0
  elsif bowSpeed >= max_speed # max_speed と min_speed が同じ場合、bowSpeed がそれを超えていれば最大深度
    depth = 127.0
  end
  depth = [[depth, 0].max, 127].min.to_i

  # 弓が動いているときだけモジュレーションを送信
  # マウスホイールが押されている場合のみビブラートを有効にする
  if isBowing && current_playing_note && isWheelPressed
    set_modulation(uart_port1, midi_channel, depth)
  elsif current_playing_note
    # それ以外（弓停止時やホイールが押されていない場合）はモジュレーションをオフ
    set_modulation(uart_port1, midi_channel, 0)
  end

  # フラグをリセット
  wheelStateChanged = false
  
  # sleep 1
  i = i + 1

  # Blinkリロード要求をチェック
  if Blink.req_reload?
    # # puts "リロード要求を検出"
    # 終了前に全てのノートをオフにする (音が鳴りっぱなしになるのを防ぐ)
    TONE_DICT.each do |key_code, note| 
      set_note_off(uart_port1, midi_channel, note)
    end
    UART.deinit(uart_port1)
    UART.deinit(uart_port2)
    break   
  end
end

# 終了処理
# puts "UARTを終了します"
# 念のためここでも全ノートオフ
TONE_DICT.each do |key_code, note|
  set_note_off(uart_port1, midi_channel, note)
end
UART.deinit(uart_port1)
UART.deinit(uart_port2)
LED.set([0x00, 0xFF, 0x00])
sleep(0.4)
LED.set([0, 0, 0])
