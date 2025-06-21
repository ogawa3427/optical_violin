# USB UART データビューア
# UART2からのUSBデータを受信して表示する

# UART2の設定
uart_port2 = 2 # UART2を使用
tx_pin2 = 6
rx_pin2 = 5
baud_rate2 = 115_200

# ディスプレイの初期化と設定
if Display.available?
  Display.clear() # ディスプレイをクリア
  Display.set_text_size(1) # テキストサイズを設定
  Display.set_text_color(Display.color565(255, 255, 255)) # 白色テキスト
  Display.println("USB UART Monitor") # タイトル
  Display.println("Port2(USB): Baud: #{baud_rate2}") # ポート2の情報
  Display.println("TX: #{tx_pin2}, RX: #{rx_pin2}")
  Display.set_text_size(1)
  line_count = 4
else
  line_count = 0
  puts "ディスプレイは利用できません"
end

# UARTポート2の初期化
puts "UART2(USB)を初期化します: ポート=#{uart_port2}, TX=#{tx_pin2}, RX=#{rx_pin2}, ボーレート=#{baud_rate2}, 8N1"

if UART.init(uart_port2, tx_pin2, rx_pin2, baud_rate2, 4096, 4096, 1)
  LED.set([0x00, 0xFF, 0x00])
  Display.println("UART2(USB) init success (8N1)")
  puts "UART2(USB)初期化成功 (8N1)"
  LED.set([0, 0, 0])
else
  LED.set([0xFF, 0x00, 0x00])
  puts "UART2(USB)初期化失敗"
  Display.println("UART2(USB) init failed")
  sleep(1)
  LED.set([0, 0, 0])
end

i = 0

Display.clear()
Display.println("USB Monitor Ready")

# メインループ
while true
  puts i if i % 1000 == 0  # 1000回に一度カウンタ表示

  # UART2から改行まで読み取り
  line = UART.readuntil(uart_port2, "\n")
  
  if line && line.length > 0
    puts "受信: #{line}"
    Display.clear()
    Display.println("USB UART Monitor")
    Display.println("受信データ:")
    Display.println(line)
  end

  i += 1

  # Blinkリロード要求をチェック
  if Blink.req_reload?
    puts "リロード要求を検出"
    UART.deinit(uart_port2)
    break
  end
end

# 終了処理
puts "UARTを終了します"
UART.deinit(uart_port2)
LED.set([0x00, 0xFF, 0x00])
sleep(0.4)
LED.set([0, 0, 0])
