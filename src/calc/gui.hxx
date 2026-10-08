#pragma once

#ifndef COMPILE_PC

#include <cstdint>

constexpr uint8_t COLOR_TRANSPARENT = 10;

constexpr uint8_t COLOR_BACKGROUND = 0x00;
constexpr uint8_t COLOR_BLUE = 0x9F;
constexpr uint8_t COLOR_PURPLE = 0xBC;
constexpr uint8_t COLOR_TEXT = 0xFF;

constexpr int TEXT_HEIGHT = 8;

namespace gui {

void run();

} // namespace gui

#endif
