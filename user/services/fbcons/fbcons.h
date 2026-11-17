#ifndef FBCONS_H
#define FBCONS_H

#include <stdbool.h>
#include <stdint.h>
#include <types.h>

#define MAX_COLS 128
#define MAX_ROWS 64
#define DEFAULT_FG 0xC618U
#define DEFAULT_BG 0x0000U

Err DisplayInit(void);
uint32_t DisplayCols(void);
uint32_t DisplayRows(void);
bool DisplayPrintable(uint8_t b);
void DisplayDrawCell(uint32_t col, uint32_t row, char ch, uint16_t fg, uint16_t bg);
void DisplayClearRow(uint32_t row, uint16_t color);
void DisplayScrollUp(uint16_t color);

uint16_t ColorFromRgb(uint8_t r, uint8_t g, uint8_t b);
uint16_t ColorFromIndex(uint8_t index);

void TermInit(void);
void TermFeed(uint8_t b);
void TermShowCursor(bool on);

#endif /* FBCONS_H */
