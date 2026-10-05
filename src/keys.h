// Solace Express - key and gamepad button codes shared by the game, the platform layer and the tests
#pragma once

// Virtual key codes (Windows VK values)
enum {
  K_BACK = 0x08, K_TAB = 0x09, K_ENTER = 0x0D, K_SHIFT = 0x10, K_CTRL = 0x11, K_ESC = 0x1B, K_SPACE = 0x20,
  K_PGUP = 0x21, K_PGDN = 0x22, K_END = 0x23, K_HOME = 0x24, K_LEFT = 0x25, K_UP = 0x26, K_RIGHT = 0x27, K_DOWN = 0x28,
  K_F1 = 0x70, K_F11 = 0x7A, K_F12 = 0x7B, K_LBRACKET = 0xDB, K_RBRACKET = 0xDD, K_PLUS = 0xBB, K_MINUS = 0xBD
};
enum { PAD_A = 1, PAD_B = 2, PAD_X = 4, PAD_Y = 8, PAD_LB = 16, PAD_RB = 32, PAD_BACK = 64, PAD_START = 128, PAD_UP = 256, PAD_DOWN = 512, PAD_LEFT = 1024, PAD_RIGHT = 2048, PAD_LS = 4096, PAD_RS = 8192 };
