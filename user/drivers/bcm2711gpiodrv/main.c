#include <zuzu/msg.h>
#include <zuzu/umem.h>
#include <zuzu/task.h>
#include <zuzu/service.h>
#include <zuzu/protocols/devm.h>
#include <zuzu/log.h>
#include <zuzu/memprot.h>
#include "bcm2711gpiodrv.h"

#define LOG_TAG "pl181drv"

#define GPIO_MAX_PIN 57
#define PIN_IS_UART(p) ((p) == 14 || (p) == 15)

static Bcm2711GpioMMIO *gpio;

/* Helper to validate pin and permissions */
static int CheckPin(uint32_t pin) {
    if (pin > GPIO_MAX_PIN) return ERR_BADARG;
    if (PIN_IS_UART(pin)) return ERR_NOPERM;
    return ZUZU_OK;
}

static int GpioSetMode(uint32_t pin, uint32_t mode) {
    int err = CheckPin(pin);
    if (err != ZUZU_OK) return err;

    uint32_t reg_idx = pin / 10;
    uint32_t shift = (pin % 10) * 3;
    
    uint32_t val = gpio->gpfsel[reg_idx];
    val &= ~(0b111 << shift);           // Clear the 3 bits
    val |= ((mode & 0b111) << shift);   // Set the new mode
    gpio->gpfsel[reg_idx] = val;
    
    return ZUZU_OK;
}

static int GpioSetPull(uint32_t pin, uint32_t pull) {
    int err = CheckPin(pin);
    if (err != ZUZU_OK) return err;

    uint32_t reg_idx = pin / 16;
    uint32_t shift = (pin % 16) * 2;
    
    uint32_t val = gpio->pup_pdn_cntrl[reg_idx];
    val &= ~(0b11 << shift);             // Clear the 2 bits
    val |= ((pull & 0b11) << shift);     // Set the new pull state
    gpio->pup_pdn_cntrl[reg_idx] = val;
    
    return ZUZU_OK;
}

static int GpioWrite(uint32_t pin, uint32_t value) {
    int err = CheckPin(pin);
    if (err != ZUZU_OK) return err;

    uint32_t reg_idx = pin / 32;
    uint32_t bit_mask = (1U << (pin % 32));

    if (value) {
        gpio->gpset[reg_idx] = bit_mask;
    } else {
        gpio->gpclr[reg_idx] = bit_mask;
    }
    
    return ZUZU_OK;
}

static int GpioRead(uint32_t pin, uint32_t *out_value) {
    int err = CheckPin(pin);
    if (err != ZUZU_OK) return err;

    uint32_t reg_idx = pin / 32;
    uint32_t bit_mask = (1U << (pin % 32));

    *out_value = (gpio->gplev[reg_idx] & bit_mask) ? 1 : 0;
    
    return ZUZU_OK;
}

static void WaitForButton(uint32_t button) {
    while (1) {
        uint32_t btn_state = 1;
        GpioRead(button, &btn_state);

        if (btn_state) {
            /* Button is pressed (shorted to ground) */
            break;
        }

        /* Sleep briefly to prevent 100% CPU usage during the polling test */
        ZuzuSleep(10);
    }
}

static void LedDisplayColor(bool blue, bool green, bool red) {
    GpioWrite(ZP_PIN_LED_GREEN, (green) ? 1 : 0);
    GpioWrite(ZP_PIN_LED_RED, (red) ? 1 : 0);
    GpioWrite(ZP_PIN_LED_BLUE, (blue) ? 1 : 0);
}

static void Test1(void) {
    LOG_INFO(LOG_TAG, "Test 1: Will cycle between various colours for 10s");
    LOG_INFO(LOG_TAG, "Blue");
    LedDisplayColor(true, false, false);
    ZuzuSleep(2000);
    LOG_INFO(LOG_TAG, "Red");
    LedDisplayColor(false, false, true);
    ZuzuSleep(2000);
    LOG_INFO(LOG_TAG, "Green");
    LedDisplayColor(false, true, false);
    ZuzuSleep(2000);
    LOG_INFO(LOG_TAG, "Magenta");
    LedDisplayColor(true, false, true);
    ZuzuSleep(2000);
    LOG_INFO(LOG_TAG, "Cyan");
    LedDisplayColor(true, true, false);
    ZuzuSleep(2000);
    LOG_INFO(LOG_TAG, "Yellow");
    LedDisplayColor(false, true, true);
    ZuzuSleep(2000);
    LOG_INFO(LOG_TAG, "White");
    LedDisplayColor(true, true, true);
    ZuzuSleep(2000);
    LOG_INFO(LOG_TAG, "None");
    LedDisplayColor(false, false, false);
}

static void Test2(void) {
    LOG_INFO(LOG_TAG, "Test 2: Will buzz the buzzer for 2s when the poweron button is pressed");

    WaitForButton(ZP_PIN_POWER_BTN);
    GpioWrite(ZP_PIN_BUZZER, 1);
    ZuzuSleep(2000);
    GpioWrite(ZP_PIN_BUZZER, 0);
}

static void Test3(void) {

    LOG_INFO(LOG_TAG, "Test 3: Will check volume up and volume down buttons, press the power button to exit any time");

    while (1) {
        uint32_t value, value_up = 0, value_down = 0;
        GpioRead(ZP_PIN_POWER_BTN, &value);
        if (value) {
            break;
        }

        GpioRead(ZP_PIN_VOL_UP_BTN, &value_up);
        if (value_up) {
            LedDisplayColor(true, false, true);
        }

        GpioRead(ZP_PIN_VOL_DOWN_BTN, &value_down);
        if (value_down) {
            LedDisplayColor(false, false, true);
        } 
    }
}

typedef void (*TestFn)(void);

int main(void) {

    int32_t devmgr_port = LookupService("/svc/devmgr");
    if (devmgr_port < 0)
    {
        LOG_ERROR(LOG_TAG, "devmgr lookup failed");
        return -1;
    }

    static const char *const block_compat[] = { "brcm,bcm2711-gpio" };   /* note: pl180, the PL18x primecell */
    Handle mmio_handle = DevmRequestDevice(devmgr_port, block_compat, 1, NULL);
    if (mmio_handle < 0) {
        LOG_ERROR(LOG_TAG, "GPIO not found");
        return mmio_handle;
    }

    gpio = (Bcm2711GpioMMIO *)ZuzuMemMap(mmio_handle, 0, PROT_RW, 0);

    /* 1. Configure Buttons (Inputs with Pull-Ups) */
    GpioSetMode(ZP_PIN_POWER_BTN, GPIO_FSEL_INPUT);
    GpioSetPull(ZP_PIN_POWER_BTN, GPIO_PUP_PDN_DOWN);

    GpioSetMode(ZP_PIN_VOL_UP_BTN, GPIO_FSEL_INPUT);
    GpioSetPull(ZP_PIN_VOL_UP_BTN, GPIO_PUP_PDN_DOWN);

    GpioSetMode(ZP_PIN_VOL_DOWN_BTN, GPIO_FSEL_INPUT);
    GpioSetPull(ZP_PIN_VOL_DOWN_BTN, GPIO_PUP_PDN_DOWN);

    /* 2. Configure LEDs (Outputs, No Pull) */
    GpioSetMode(ZP_PIN_LED_BLUE, GPIO_FSEL_OUTPUT);
    GpioSetPull(ZP_PIN_LED_BLUE, GPIO_PUP_PDN_NONE);

    GpioSetMode(ZP_PIN_LED_GREEN, GPIO_FSEL_OUTPUT);
    GpioSetPull(ZP_PIN_LED_GREEN, GPIO_PUP_PDN_NONE);

    GpioSetMode(ZP_PIN_LED_RED, GPIO_FSEL_OUTPUT);
    GpioSetPull(ZP_PIN_LED_RED, GPIO_PUP_PDN_NONE);

    /* 3. Configure Buzzer (Output, No Pull) */
    GpioSetMode(ZP_PIN_BUZZER, GPIO_FSEL_OUTPUT);
    GpioSetPull(ZP_PIN_BUZZER, GPIO_PUP_PDN_NONE);

    LOG_INFO(LOG_TAG, "GPIO pins initialized, now will proceed to the test");

    TestFn tests[3] = {Test1, Test2, Test3};

    for (int i = 0; i < 3; i++) {
        LOG_INFO(LOG_TAG, "Press the power on button once to continue");
        WaitForButton(ZP_PIN_POWER_BTN);
        tests[i]();
    }
}