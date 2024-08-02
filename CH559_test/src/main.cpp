#include <mcs51/ch559.h>
#include <stdint.h>

void delay(uint16_t ms) {
    uint16_t i, j;
    for (i = 0; i < ms; i++)
        for (j = 0; j < 1000; j++);
}

void main() {
    P0_DIR_PU &= ~(1<<0);  // P0.0をプッシュプル出力モードに設定
    
    while (1) {
        P0 ^= 0x01;  // P0.0の状態を反転
        delay(500);  // 500ms待機
    }
}