/**
* @file bcm2711gpiodrv.h
* @brief Header file for the zuzu BCM2711 board GPIO driver
*/

#ifndef BCM2711_GPIODRV_H
#define BCM2711_GPIODRV_H

#include <stdint.h>

typedef struct __attribute__((packed)) {
    volatile uint32_t gpfsel[6];        /* 0x00 - 0x14: function select */
    volatile uint32_t reserved1;        /* 0x18 */
    volatile uint32_t gpset[2];         /* 0x1C - 0x20: output set */
    volatile uint32_t reserved2;        /* 0x24 */
    volatile uint32_t gpclr[2];         /* 0x28 - 0x2C: output clear */
    volatile uint32_t reserved3;        /* 0x30 */
    volatile uint32_t gplev[2];         /* 0x34 - 0x38: pin level (read) */
    volatile uint32_t reserved4;        /* 0x3C */
    volatile uint32_t gpeds[2];         /* 0x40 - 0x44: event detect status */
    volatile uint32_t reserved5;        /* 0x48 */
    volatile uint32_t gpren[2];         /* 0x4C - 0x50: rising edge detect enable */
    volatile uint32_t reserved6;        /* 0x54 */
    volatile uint32_t gpfen[2];         /* 0x58 - 0x5C: falling edge detect enable */
    volatile uint32_t reserved7;        /* 0x60 */
    volatile uint32_t gphen[2];         /* 0x64 - 0x68: high detect enable */
    volatile uint32_t reserved8;        /* 0x6C */
    volatile uint32_t gplen[2];         /* 0x70 - 0x74: low detect enable */
    volatile uint32_t reserved9;        /* 0x78 */
    volatile uint32_t gparen[2];        /* 0x7C - 0x80: async rising edge detect enable */
    volatile uint32_t reserved10;       /* 0x84 */
    volatile uint32_t gpafen[2];        /* 0x88 - 0x8C: async falling edge detect enable */
    volatile uint32_t reserved11[21];   /* padding to reach 0xE4 */
    volatile uint32_t pup_pdn_cntrl[4]; /* 0xE4 - 0xF0: pull-up / pull-down cntrl */
} Bcm2711GpioMMIO;

/* Function select modes */
#define GPIO_FSEL_INPUT   0b000
#define GPIO_FSEL_OUTPUT  0b001
#define GPIO_FSEL_ALT0    0b100

/* pull up/pull down modes */
#define GPIO_PUP_PDN_NONE 0b00
#define GPIO_PUP_PDN_UP   0b01
#define GPIO_PUP_PDN_DOWN 0b10

/* zuzuphone pin mapping (temporary) */
#define ZP_PIN_POWER_BTN   4
#define ZP_PIN_VOL_UP_BTN  23
#define ZP_PIN_VOL_DOWN_BTN 24
#define ZP_PIN_LED_BLUE    17
#define ZP_PIN_LED_GREEN   27
#define ZP_PIN_LED_RED     22
#define ZP_PIN_BUZZER      5

/* ipc cmds */
enum GpioIpcCommand {
    GPIO_CMD_SET_MODE = 1,   /* args: pin, mode (e.g., input/output/alt). Denied for UART */
    GPIO_CMD_SET_PULL,       /* args: pin, pull_type (e.g., up/down/none) */
    GPIO_CMD_WRITE,          /* args: pin, value (1 or 0) */
    GPIO_CMD_READ            /* args: pin. returns: value (1 or 0) */
};


#endif /* BCM2711_GPIODRV_H */