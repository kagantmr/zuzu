#ifndef FBCONS_H
#define FBCONS_H

#include <stdbool.h>
#include <stdint.h>
#include <types.h>
#include <dev/protocols/input.h>
#include <util/vterm.h>

#define MAX_COLS VTERM_MAX_COLS
#define MAX_ROWS VTERM_MAX_ROWS
#define DEFAULT_FG 0xC618U
#define DEFAULT_BG 0x0000U

Err DisplayInit(Handle event, uint32_t shift);
bool DisplaySetActive(bool active);
Err DisplayAckDeactivate(void);
InputRing *DisplayInputRing(void);
void DisplayClear(uint16_t color);
uint32_t DisplayCols(void);
uint32_t DisplayRows(void);
bool DisplayPrintable(uint8_t b);
void DisplayDrawCell(uint32_t col, uint32_t row, char ch, uint16_t fg, uint16_t bg);
void DisplayClearRow(uint32_t row, uint16_t color);
void DisplayScrollUp(uint16_t color);


#endif /* FBCONS_H */
