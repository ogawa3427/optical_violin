#include "note.h"
// #include "tone.h"
#include <map>

#define KEYBOARD_A 0x04
#define KEYBOARD_B 0x05
#define KEYBOARD_C 0x06
#define KEYBOARD_D 0x07
#define KEYBOARD_E 0x08
#define KEYBOARD_F 0x09
#define KEYBOARD_G 0x0A
#define KEYBOARD_H 0x0B
#define KEYBOARD_I 0x0C
#define KEYBOARD_J 0x0D
#define KEYBOARD_K 0x0E
#define KEYBOARD_L 0x0F
#define KEYBOARD_M 0x10
#define KEYBOARD_N 0x11
#define KEYBOARD_O 0x12
#define KEYBOARD_P 0x13
#define KEYBOARD_Q 0x14
#define KEYBOARD_R 0x15
#define KEYBOARD_S 0x16
#define KEYBOARD_T 0x17
#define KEYBOARD_U 0x18
#define KEYBOARD_V 0x19
#define KEYBOARD_W 0x1A
#define KEYBOARD_X 0x1B
#define KEYBOARD_Y 0x1C
#define KEYBOARD_Z 0x1D
#define KEYBOARD_1 0x1E
#define KEYBOARD_2 0x1F
#define KEYBOARD_3 0x20
#define KEYBOARD_4 0x21
#define KEYBOARD_5 0x22
#define KEYBOARD_6 0x23
#define KEYBOARD_7 0x24
#define KEYBOARD_8 0x25
#define KEYBOARD_9 0x26
#define KEYBOARD_0 0x27
#define KEYBOARD_RETURN 0x28
#define KEYBOARD_ESC 0x29
#define KEYBOARD_BACKSPACE 0x2A
#define KEYBOARD_TAB 0x2B
#define KEYBOARD_SPACE 0x2C
#define KEYBOARD_MINUS 0x2D
#define KEYBOARD_EQUAL 0x2E
#define KEYBOARD_LEFT_BRACKET 0x2F
#define KEYBOARD_RIGHT_BRACKET 0x30
#define KEYBOARD_BACKSLASH 0x31
#define KEYBOARD_SEMICOLON 0x33
#define KEYBOARD_APOSTROPHE 0x34
#define KEYBOARD_BACKQUOTE 0x35
#define KEYBOARD_COMMA 0x36
#define KEYBOARD_DOT 0x37
#define KEYBOARD_SLASH 0x38
#define KEYBOARD_CAPS_LOCK 0x39
#define KEYBOARD_F1 0x3A
#define KEYBOARD_F2 0x3B
#define KEYBOARD_F3 0x3C
#define KEYBOARD_F4 0x3D
#define KEYBOARD_F5 0x3E
#define KEYBOARD_F6 0x3F
#define KEYBOARD_F7 0x40
#define KEYBOARD_F8 0x41
#define KEYBOARD_F9 0x42
#define KEYBOARD_F10 0x43
#define KEYBOARD_F11 0x44
#define KEYBOARD_F12 0x45
#define KEYBOARD_PRINT_SCREEN 0x46
#define KEYBOARD_SCROLL_LOCK 0x47
#define KEYBOARD_PAUSE 0x48
#define KEYBOARD_INSERT 0x49
#define KEYBOARD_HOME 0x4A
#define KEYBOARD_PAGE_UP 0x4B
#define KEYBOARD_DELETE 0x4C
#define KEYBOARD_END 0x4D
#define KEYBOARD_PAGE_DOWN 0x4E
#define KEYBOARD_RIGHT_ARROW 0x4F
#define KEYBOARD_LEFT_ARROW 0x50
#define KEYBOARD_DOWN_ARROW 0x51
#define KEYBOARD_UP_ARROW 0x52
#define KEYBOARD_NUM_LOCK 0x53
#define KEYBOARD_KP_DIVIDE 0x54
#define KEYBOARD_KP_MULTIPLY 0x55
#define KEYBOARD_KP_MINUS 0x56
#define KEYBOARD_KP_PLUS 0x57
#define KEYBOARD_KP_ENTER 0x58
#define KEYBOARD_KP_1 0x59
#define KEYBOARD_KP_2 0x5A
#define KEYBOARD_KP_3 0x5B
#define KEYBOARD_KP_4 0x5C
#define KEYBOARD_KP_5 0x5D
#define KEYBOARD_KP_6 0x5E
#define KEYBOARD_KP_7 0x5F
#define KEYBOARD_KP_8 0x60
#define KEYBOARD_KP_9 0x61
#define KEYBOARD_KP_0 0x62
#define KEYBOARD_KP_DOT 0x63
#define KEYBOARD_KP_COMMA 0x64
#define KEYBOARD_KP_EQUAL 0x65
#define KEYBOARD_APPLIC 0x66
#define KEYBOARD_POWER 0x67
#define KEYBOARD_KP_EQUAL_SIGN 0x68
#define KEYBOARD_F13 0x69
#define KEYBOARD_F14 0x6A
#define KEYBOARD_F15 0x6B
#define KEYBOARD_F16 0x6C
#define KEYBOARD_F17 0x6D
#define KEYBOARD_F18 0x6E
#define KEYBOARD_F19 0x6F
#define KEYBOARD_F20 0x70
#define KEYBOARD_F21 0x71
#define KEYBOARD_F22 0x72
#define KEYBOARD_F23 0x73
#define KEYBOARD_F24 0x74
#define KEYBOARD_EXECUTE 0x75
#define KEYBOARD_HELP 0x76
#define KEYBOARD_MENU 0x77
#define KEYBOARD_SELECT 0x78
#define KEYBOARD_STOP 0x79
#define KEYBOARD_AGAIN 0x7A
#define KEYBOARD_UNDO 0x7B
#define KEYBOARD_CUT 0x7C
#define KEYBOARD_COPY 0x7D
#define KEYBOARD_PASTE 0x7E
#define KEYBOARD_FIND 0x7E
#define KEYBOARD_MUTE 0x7F
#define KEYBOARD_VOLUME_UP 0x80
#define KEYBOARD_VOLUME_DOWN 0x81
#define KEYBOARD_LOCKING_CAPS_LOCK 0x82
#define KEYBOARD_LOCKING_NUM_LOCK 0x83
#define KEYBOARD_LOCKING_SCROLL_LOCK 0x84
#define KEYBOARD_KP_COMMA 0x85
#define KEYBOARD_KP_EQUAL_SIGN 0x86
#define KEYBOARD_INTERNATIONAL1 0x87
#define KEYBOARD_INTERNATIONAL2 0x88
#define KEYBOARD_INTERNATIONAL3 0x89
#define KEYBOARD_INTERNATIONAL4 0x8A
#define KEYBOARD_INTERNATIONAL5 0x8B
#define KEYBOARD_INTERNATIONAL6 0x8C
#define KEYBOARD_INTERNATIONAL7 0x8D
#define KEYBOARD_INTERNATIONAL8 0x8E
#define KEYBOARD_INTERNATIONAL9 0x8F
#define KEYBOARD_LANG1 0x90
#define KEYBOARD_LANG2 0x91
#define KEYBOARD_LANG3 0x92
#define KEYBOARD_LANG4 0x93
#define KEYBOARD_LANG5 0x94
#define KEYBOARD_LANG6 0x95
#define KEYBOARD_LANG7 0x96
#define KEYBOARD_LANG8 0x97
#define KEYBOARD_LANG9 0x98
#define KEYBOARD_ALTERASE 0x99
#define KEYBOARD_SYSREQ 0x9A
#define KEYBOARD_CANCEL 0x9B
#define KEYBOARD_CLEAR 0x9C
#define KEYBOARD_PRIOR 0x9D
#define KEYBOARD_RETURN2 0x9E
#define KEYBOARD_SEPARATOR 0x9F
#define KEYBOARD_OUT 0xA0
#define KEYBOARD_OPER 0xA1
#define KEYBOARD_CLEAR_AGAIN 0xA2
#define KEYBOARD_CRSEL 0xA3
#define KEYBOARD_EXSEL 0xA4
#define KEYBOARD_LEFT_CTRL 0xE0
#define KEYBOARD_LEFT_SHIFT 0xE1
#define KEYBOARD_LEFT_ALT 0xE2
#define KEYBOARD_LEFT_GUI 0xE3
#define KEYBOARD_RIGHT_CTRL 0xE4
#define KEYBOARD_RIGHT_SHIFT 0xE5
#define KEYBOARD_RIGHT_ALT 0xE6
#define KEYBOARD_RIGHT_GUI 0xE7


std::map<int, String> keyNameDict = {
    {0x04, "A"},
    {0x05, "B"},
    {0x06, "C"},
    {0x07, "D"},
    {0x08, "E"},
    {0x09, "F"},
    {0x0A, "G"},
    {0x0B, "H"},
    {0x0C, "I"},
    {0x0D, "J"},
    {0x0E, "K"},
    {0x0F, "L"},
    {0x10, "M"},
    {0x11, "N"},
    {0x12, "O"},
    {0x13, "P"},
    {0x14, "Q"},
    {0x15, "R"},
    {0x16, "S"},
    {0x17, "T"},
    {0x18, "U"},
    {0x19, "V"},
    {0x1A, "W"},
    {0x1B, "X"},
    {0x1C, "Y"},
    {0x1D, "Z"},
    {0x1E, "1"},
    {0x1F, "2"},
    {0x20, "3"},
    {0x21, "4"},
    {0x22, "5"},
    {0x23, "6"},
    {0x24, "7"},
    {0x25, "8"},
    {0x26, "9"},
    {0x27, "0"},
    {0x28, "RETURN"},
    {0x29, "ESC"},
    {0x2A, "BACKSPACE"},
    {0x2B, "TAB"},
    {0x2C, "SPACE"},
    {0x2D, "MINUS"},
    {0x2E, "EQUAL"},
    {0x2F, "LEFT_BRACKET"},
    {0x30, "RIGHT_BRACKET"},
    {0x31, "BACKSLASH"},
    {0x32, "..."},
    {0x33, "SEMICOLON"},
    {0x34, "APOSTROPHE"},
    {0x35, "BACKQUOTE"},
    {0x36, "COMMA"},
    {0x37, "DOT"},
    {0x38, "SLASH"},
    {0x39, "CAPS_LOCK"},
    {0x3A, "F1"},
    {0x3B, "F2"},
    {0x3C, "F3"},
    {0x3D, "F4"},
    {0x3E, "F5"},
    {0x3F, "F6"},
    {0x40, "F7"},
    {0x41, "F8"},
    {0x42, "F9"},
    {0x43, "F10"},
    {0x44, "F11"},
    {0x45, "F12"},
    {0x46, "PRINTSCREEN"},
    {0x47, "SCROLL_LOCK"},
    {0x48, "PAUSE"},
    {0x49, "INSERT"},
    {0x4A, "HOME"},
    {0x4B, "PAGE_UP"},
    {0x4C, "DELETE"},
    {0x4D, "END"},
    {0x4E, "PAGE_DOWN"},
    {0x4F, "RIGHT_ARROW"},
    {0x50, "LEFT_ARROW"},
    {0x51, "DOWN_ARROW"},
    {0x52, "UP_ARROW"},
    {0x53, "NUM_LOCK"},
    {0x54, "KP_DIVIDE"},
    {0x55, "KP_MULTIPLY"},
    {0x56, "KP_MINUS"},
    {0x57, "KP_PLUS"},
    {0x58, "KP_ENTER"},
    {0x59, "KP_1"},
    {0x5A, "KP_2"},
    {0x5B, "KP_3"},
    {0x5C, "KP_4"},
    {0x5D, "KP_5"},
    {0x5E, "KP_6"},
    {0x5F, "KP_7"},
    {0x60, "KP_8"},
    {0x61, "KP_9"},
    {0x62, "KP_0"},
    {0x63, "KP_DOT"},
    {0x64, "..."},
    {0x65, "APPLIC"},
    {0x66, "POWER"},
    {0x67, "KP_EQUAL"},
    {0x68, "F13"},
    {0x69, "F14"},
    {0x6A, "F15"},
    {0x6B, "F16"},
    {0x6C, "F17"},
    {0x6D, "F18"},
    {0x6E, "F19"},
    {0x6F, "F20"},
    {0x70, "F21"},
    {0x71, "F22"},
    {0x72, "F23"},
    {0x73, "F24"},
    {0x74, "EXECUTE"},
    {0x75, "HELP"},
    {0x76, "MENU"},
    {0x77, "SELECT"},
    {0x78, "STOP"},
    {0x79, "AGAIN"},
    {0x7A, "UNDO"},
    {0x7B, "CUT"},
    {0x7C, "COPY"},
    {0x7D, "PASTE"},
    {0x7E, "FIND"},
    {0x7F, "MUTE"},
    {0x80, "VOLUME_UP"},
    {0x81, "VOLUME_DOWN"},
    {0x82, "LOCKING_CAPS_LOCK"},
    {0x83, "LOCKING_NUM_LOCK"},
    {0x84, "LOCKING_SCROLL_LOCK"},
    {0x85, "KP_COMMA"},
    {0x86, "KP_EQUAL_SIGN"},
    {0x87, "INTERNATIONAL1"},
    {0x88, "INTERNATIONAL2"},
    {0x89, "INTERNATIONAL3"},
    {0x8A, "INTERNATIONAL4"},
    {0x8B, "INTERNATIONAL5"},
    {0x8C, "INTERNATIONAL6"},
    {0x8D, "INTERNATIONAL7"},
    {0x8E, "INTERNATIONAL8"},
    {0x8F, "INTERNATIONAL9"},
    {0x90, "LANG1"},
    {0x91, "LANG2"},
    {0x92, "LANG3"},
    {0x93, "LANG4"},
    {0x94, "LANG5"},
    {0x95, "LANG6"},
    {0x96, "LANG7"},
    {0x97, "LANG8"},
    {0x98, "LANG9"},
    {0x99, "ALTERASE"},
    {0x9A, "SYSREQ"},
    {0x9B, "CANCEL"},
    {0x9C, "CLEAR"},
    {0x9D, "PRIOR"},
    {0x9E, "RETURN2"},
    {0x9F, "SEPARATOR"},
    {0xA0, "OUT"},
    {0xA1, "OPER"},
    {0xA2, "CLEAR_AGAIN"},
    {0xA3, "CRSEL"},
    {0xA4, "EXSEL"},
    {0xE0, "LEFT_CTRL"},
    {0xE1, "LEFT_SHIFT"},
    {0xE2, "LEFT_ALT"},
    {0xE3, "LEFT_GUI"},
    {0xE4, "RIGHT_CTRL"},
    {0xE5, "RIGHT_SHIFT"},
    {0xE6, "RIGHT_ALT"},
    {0xE7, "RIGHT_GUI"}
};
