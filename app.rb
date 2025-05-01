# UARTポート1の設定を追加
uart_port1 = 1 # UART1を使用
tx_pin1 = 2
rx_pin1 = 1
baud_rate1 = 31_250 # MIDI標準ボーレート

uart_port2 = 2 # UART2を使用
tx_pin2 = 6
rx_pin2 = 5
baud_rate2 = 115_200

# ディスプレイの初期化と設定
if Display.available?
  Display.clear() # ディスプレイをクリア
  Display.set_text_size(1) # テキストサイズを設定
  Display.set_text_color(Display.color565(255, 255, 255)) # 白色テキスト
  Display.println("UART Monitor & MIDI Demo") # タイトル変更
  Display.println("Port1(MIDI): Baud: #{baud_rate1}") # ポート1の情報を更新
  Display.println("TX: #{tx_pin1}, RX: #{rx_pin1}")
  Display.set_text_size(2)
  line_count = 5
else
  line_count = 0
  puts "ディスプレイは利用できません"
end

# UARTポート1の初期化を追加
puts "UART1(MIDI)を初期化します: ポート=#{uart_port1}, TX=#{tx_pin1}, RX=#{rx_pin1}, ボーレート=#{baud_rate1}, 8N1"
sleep(0.1)
# MIDI標準の 8bit, Non-parity, 1 stop bit で初期化 (Arduinoコード参考)
# rx_buffer_size をハードウェアFIFO長(128)より大きい値(例: 256)に変更
if UART.init(uart_port1, tx_pin1, rx_pin1, baud_rate1, 256, 256, 1)
  LED.set([0x00, 0xFF, 0x00])
  Display.println("UART1(MIDI) init success (8N1)") # 設定情報を追加
  puts "UART1(MIDI)初期化成功 (8N1)" # 設定情報を追加
  sleep(0.3)
  LED.set([0, 0, 0])
else
  LED.set([0xFF, 0x00, 0x00])

  puts "UART1(MIDI)初期化失敗" # 表示を更新
  Display.println("UART1(MIDI) init failed") # 表示を更新
  sleep(1)
  LED.set([0, 0, 0])
end

# UARTポート2の初期化を追加
puts "UART2(USB)を初期化します: ポート=#{uart_port2}, TX=#{tx_pin2}, RX=#{rx_pin2}, ボーレート=#{baud_rate2}, 8N1"
sleep(0.1)
# MIDI標準の 8bit, Non-parity, 1 stop bit で初期化 (Arduinoコード参考)
# rx_buffer_size をハードウェアFIFO長(128)より大きい値(例: 256)に変更
if UART.init(uart_port2, tx_pin2, rx_pin2, baud_rate2, 4096, 4096, 1)
  LED.set([0x00, 0xFF, 0x00])
  Display.println("UART2(USB) init success (8N1)") # 設定情報を追加
  puts "UART2(USB)初期化成功 (8N1)" # 設定情報を追加
  sleep(0.3)
  LED.set([0, 0, 0])
else
  LED.set([0xFF, 0x00, 0x00])

  puts "UART2(USB)初期化失敗" # 表示を更新
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

# MIDIメッセージ送信ヘルパー
def send_midi_command(port, command_bytes)
  # puts "Sending MIDI: #{command_bytes.map { |b| "0x#{b.to_s(16).upcase}" }.join(', ')}" # デバッグ用
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

# MIDI送信関連
midi_channel = 0 # チャンネル1 (0-15)
midi_note = 72 # C4 (中央ド)
midi_velocity = 100 # ベロシティ (0-127)
# last_midi_send_time = Time.now # Timeクラスは使えないので削除
# midi_interval = 2 # 固定sleepにするので不要
set_instrument(uart_port1, midi_channel, 0, 40)

Display.set_text_size(4)
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
  while buffer.length >= 16
    # FEで始まるパケットを探す
    start_index = buffer.index(0xFE.chr)

    # パケットの開始マーカーが見つからない場合
    if start_index.nil?
      # バッファをクリア
      return [], ""
    end

    # 開始位置より前のデータを削除
    buffer = buffer[start_index..-1] if start_index > 0

    # 16バイトパケットの処理
    if buffer.length >= 16 && buffer[0].ord == 0xFE
      # 16バイトパケットとして抽出
      packet = buffer[0...16]
      buffer = buffer[16..-1] || ""
      packets << packet
      next
    end

    # 20バイトパケットの処理
    if buffer.length >= 20 && buffer[0].ord == 0xFE
      # 20バイトパケットとして抽出
      packet = buffer[0...20]
      buffer = buffer[20..-1] || ""
      packets << packet
      next
    end

    # どちらのパケットサイズにも合わない場合は1バイト進める
    buffer = buffer[1..-1] || ""
  end

  # 残りのバッファと抽出されたパケットを返す
  return packets, buffer
end

# メインループ
while true
  # UART2からデータ受信チェック
  available_bytes = UART.available(uart_port2)
  if available_bytes > 0
    # 新しいデータを読み取り、バッファに追加
    new_data = UART.read(uart_port2, available_bytes)
    uart2_buffer += new_data

    # バッファからパケットを抽出
    packets, uart2_buffer = extract_packets(uart2_buffer)

    # 抽出したパケットを処理
    packets.each do |packet|
      hex_string = packet.bytes.map { |b| sprintf("%02X", b) }.join(" ")

      # パケットの長さが16または20の場合のみ処理
      if (packet.length == 16 || packet.length == 20) && packet.bytes[0] == 0xFE
        puts "Complete Packet: #{hex_string}"

        # ここでパケットの内容に応じた処理を行う
        # 例: キーボード入力の場合は音を出すなど
      else
        # 無視する（何も処理しない）
      end
    end

    # バッファが大きくなりすぎたらクリア（異常状態防止）
    if uart2_buffer.length > 1024
      puts "Buffer overflow, clearing"
      uart2_buffer = ""
    end
  end

  # Blinkリロード要求をチェック
  if Blink.req_reload?
    puts "リロード要求を検出"
    UART.deinit(uart_port1)
    UART.deinit(uart_port2)
    break
  end

  # 少し待機してCPU使用率を下げる
  sleep(0.001)
end

# 終了処理
puts "UARTを終了します"
UART.deinit(uart_port1)
UART.deinit(uart_port2)
LED.set([0x00, 0xFF, 0x00])
sleep(0.4)
LED.set([0, 0, 0])
