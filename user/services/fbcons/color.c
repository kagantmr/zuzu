#include "fbcons.h"

static const uint8_t k_ansi16[16][3] = {
    {0, 0, 0},       {170, 0, 0},     {0, 170, 0},     {170, 85, 0},
    {0, 0, 170},     {170, 0, 170},   {0, 170, 170},   {170, 170, 170},
    {85, 85, 85},    {255, 85, 85},   {85, 255, 85},   {255, 255, 85},
    {85, 85, 255},   {255, 85, 255},  {85, 255, 255},  {255, 255, 255},
};

static const uint8_t k_cube[6] = {0, 95, 135, 175, 215, 255};

uint16_t ColorFromRgb(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

uint16_t ColorFromIndex(uint8_t index)
{
    if (index < 16)
        return ColorFromRgb(k_ansi16[index][0], k_ansi16[index][1], k_ansi16[index][2]);
    if (index < 232) {
        uint8_t v = index - 16;
        return ColorFromRgb(k_cube[v / 36], k_cube[(v / 6) % 6], k_cube[v % 6]);
    }
    uint8_t level = 8 + ((index - 232) * 10);
    return ColorFromRgb(level, level, level);
}
