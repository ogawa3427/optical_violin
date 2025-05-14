# 色の辞書定義  // [色, 文字色]
colors = {
  "red" => [0xf800, 0xFFFF],       # 赤色, 白
  "green" => [0x07e0, 0x0000],     # 緑色, 黒
  "blue" => [0x001f, 0xFFFF],      # 青色, 白
  "white" => [0xffff, 0x0000],     # 白色, 黒
  "black" => [0x0000, 0xFFFF],     # 黒色, 白
  "yellow" => [0xffe0, 0x0000],    # 黄色, 黒
  "cyan" => [0x07ff, 0x0000],      # シアン, 黒
  "magenta" => [0xf81f, 0xFFFF],   # マゼンタ, 白
  "orange" => [0xfd20, 0x0000],    # オレンジ, 黒
  "purple" => [0x780f, 0xFFFF],    # 紫色, 白
  "pink" => [0xf81f, 0x0000],      # ピンク, 黒
  "brown" => [0x8a22, 0xFFFF],     # 茶色, 白
  "lightgray" => [0xc618, 0x0000], # 明るい灰色, 黒
  "darkgray" => [0x4208, 0xFFFF]   # 暗い灰色, 白
}

isFirst = true
current_color = "white"  # 初期色

# テキスト色を白に設定

while true do
    if isFirst then
        isFirst = false
        # ディスプレイが利用可能か確認
        if Display.available?
        # ディスプレイをクリアして現在の色で塗りつぶす
        Display.clear(colors[current_color][0])
        
        # テキストサイズを設定（適切なサイズに調整）
        Display.set_text_size(4)
        
        # テキストを中央に配置するための座標計算
        width, height = Display.dimension
        text = current_color
        x = (width - text.length * 4 * 6) / 2  # 文字幅の概算: サイズ(4) * 文字幅(約6px)
        y = (height - 4 * 8) / 2               # 文字高さの概算: サイズ(4) * 文字高さ(約8px)
        Display.set_text_color(colors[current_color][1])

        # カーソル位置設定とテキスト表示
        Display.set_cursor(x, y)
        Display.print(text)
        
        # 更新を反映
        M5.update
        
    end
    end
    break if Blink.req_reload?

end