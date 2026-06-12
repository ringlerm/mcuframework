/**
 * @file    hal_gpio_lpc845.c
 * @brief   GPIO HAL port implementation for the NXP LPC845 (Cortex-M0+).
 *
 * @details Implements the five functions declared in @c hal/hal_gpio.h using
 *          the LPC845 GPIO and IOCON peripheral registers as defined in the
 *          NXP LPC84x CMSIS device header (LPC845.h, UM11029 Rev. 1.7).
 *
 *          ### Hardware overview
 *
 *          The LPC845 has a single GPIO port (PORT0) with up to 54 pins,
 *          addressed as PIO0_0 … PIO0_53.  Only port index 0 is valid.
 *
 *          **GPIO registers (base 0xA000 0000, single-cycle IOP bus)**
 *          | Register  | Description                                              |
 *          |-----------|----------------------------------------------------------|
 *          | PIN       | Read physical pin levels (input data register)           |
 *          | SET       | Write 1 → drive HIGH; write 0 → no effect; read → latch |
 *          | CLR       | Write 1 → drive LOW;  write 0 → no effect (write-only)  |
 *          | NOT       | Write 1 → toggle output latch                           |
 *          | DIRSET    | Write 1 → set pin as output (atomic)                    |
 *          | DIRCLR    | Write 1 → set pin as input  (atomic)                    |
 *          | MASK      | 0 = pin enabled for MPIN access; 1 = pin protected       |
 *          | MPIN      | Masked port read/write — only unmasked (0) pins affected |
 *
 *          **IOCON registers (base 0x4004 4000, APB bus)**
 *          Each pin has a dedicated 32-bit IOCON register, laid out as an
 *          array @c LPC_IOCON->PIO[0][n] where n is the pin index.
 *
 *          Relevant IOCON bit fields (UM11029 Ch. 12):
 *          | Bits  | Name     | Description                              |
 *          |-------|----------|------------------------------------------|
 *          | 2:0   | FUNC     | Pin function. 0 = GPIO (default)         |
 *          | 4:3   | MODE     | 00=float 01=pull-down 10=pull-up 11=rep  |
 *          | 5     | HYS      | Hysteresis enable                        |
 *          | 6     | INV      | Invert input                             |
 *          | 7     | DIGIMODE | 0=analog 1=digital (must be 1 for GPIO)  |
 *          | 8     | FILTR    | Input glitch filter disable (1=bypass)   |
 *          | 10    | OD       | Pseudo open-drain mode                   |
 *
 *          @note   Bit 7 (DIGIMODE) must always be set to 1 for GPIO use.
 *                  If left at 0 the pin register reads back 0 regardless
 *                  of actual pin state (UM11029 §12.3 Remark).
 *
 *          ### True open-drain pins (PIO0_10, PIO0_11)
 *          PIO0_10 and PIO0_11 are true open-drain pads (I²C Fast-Mode Plus
 *          capable).  Their IOCON layout differs from standard pins: there is
 *          no MODE pull field and no OD bit.  Configuring them as
 *          HAL_GPIO_MODE_INPUT_PULLDOWN or HAL_GPIO_MODE_OUTPUT_PUSHPULL
 *          returns HAL_GPIO_ERR_UNSUPPORTED_MODE.
 *
 *          ### Pull-down note
 *          The LPC845 does have an internal pull-down (MODE = 01b), unlike
 *          some older LPC800 devices.  It is a weak resistor (~50 kΩ typical)
 *          same as the pull-up.
 *
 *          ### Clock enable
 *          The GPIO clock (bit 6 of SYSAHBCLKCTRL0) and the IOCON clock
 *          (bit 18 of SYSAHBCLKCTRL0) must be enabled before any register
 *          access.  This port enables both clocks in
 *          @c hal_gpio_pin_configure() on the first call via a static init
 *          flag, so the caller does not need to do it separately.
 *
 * @note    This file must only be compiled when TARGET_LPC845 is defined.
 *
 * @author  mcuframework contributors
 * @date    2025
 */

#include "target.h"

#ifndef TARGET_LPC845
#error "Este archivo solo debe compilarse con TARGET_LPC845 definido en target.h"
#endif

#include "hal/hal_gpio.h"
#include <stddef.h>

/*===========================================================================*
 * Private constants — LPC845 hardware limits
 *===========================================================================*/

/** @brief  Only PORT0 exists on the LPC845. */
#define LPC845_GPIO_PORT_MAX        (0u)

/** @brief  Highest valid pin index on the LPC845 (PIO0_0 … PIO0_53). */
#define LPC845_GPIO_PIN_MAX         (53u)

/** @brief  First true open-drain pin index. */
#define LPC845_TRUE_OD_PIN_FIRST    (10u)

/** @brief  Last true open-drain pin index. */
#define LPC845_TRUE_OD_PIN_LAST     (11u)

/*===========================================================================*
 * Private constants — IOCON bit field positions and masks
 *===========================================================================*/

/** @brief  FUNC field — bits [2:0].  Value 0 selects GPIO. */
#define IOCON_FUNC_SHIFT            (0u)
#define IOCON_FUNC_MASK             (0x07u)
#define IOCON_FUNC_GPIO             (0x00u)

/** @brief  MODE field — bits [4:3].  Pull resistor selection. */
#define IOCON_MODE_SHIFT            (3u)
#define IOCON_MODE_MASK             (0x18u)
#define IOCON_MODE_FLOAT            (0x00u)     /**< No pull (inactive).     */
#define IOCON_MODE_PULLDOWN         (0x08u)     /**< Pull-down enabled.      */
#define IOCON_MODE_PULLUP           (0x10u)     /**< Pull-up enabled.        */
#define IOCON_MODE_REPEATER         (0x18u)     /**< Repeater mode.          */

/** @brief  DIGIMODE bit [7].  Must be 1 for all digital/GPIO use. */
#define IOCON_DIGIMODE_BIT          (0x80u)

/** @brief  OD bit [10].  Enables pseudo open-drain mode. */
#define IOCON_OD_BIT                (0x400u)

/**
 * @brief   Mask covering all fields this port touches in a single write.
 *
 * Bits cleared: FUNC[2:0], MODE[4:3], OD[10].
 * Bits preserved: HYS[5], INV[6], FILTR[8], reserved[9], and upper bits.
 * DIGIMODE[7] is always set to 1 by this port.
 */
#define IOCON_WRITE_MASK  (IOCON_FUNC_MASK | IOCON_MODE_MASK | IOCON_OD_BIT)

/*===========================================================================*
 * Private constants — SYSCON clock enable bits (SYSAHBCLKCTRL0)
 *===========================================================================*/

/** @brief  Bit 6 of SYSAHBCLKCTRL0 — GPIO clock enable. */
#define SYSCON_GPIO_CLK_BIT         (1u << 6u)

/** @brief  Bit 18 of SYSAHBCLKCTRL0 — IOCON clock enable. */
#define SYSCON_IOCON_CLK_BIT        (1u << 18u)

/*===========================================================================*
 * Private variables
 *===========================================================================*/

/** @brief  True after clocks have been enabled on the first configure call. */
static bool s_clocks_enabled = false;

/*===========================================================================*
 * Private helper prototypes
 *===========================================================================*/

/**
 * @brief   Enable GPIO and IOCON clocks once if not already done.
 */
static void enable_clocks(void);

/**
 * @brief   Return true if the given pin is a true open-drain pad.
 *
 * @param[in]   pin_index   Zero-based pin index within PORT0.
 * @return      true for PIO0_10 and PIO0_11; false otherwise.
 */
static bool is_true_od_pin(uint8_t pin_index);

/**
 * @brief   Validate port and pin range.
 *
 * @param[in]   pin     Pin descriptor to validate.
 * @return      HAL_GPIO_OK if valid, HAL_GPIO_ERR_INVALID_PIN otherwise.
 */
static hal_gpio_status_t validate_pin(hal_gpio_pin_t pin);

/*===========================================================================*
 * Public function implementations
 *===========================================================================*/

hal_gpio_status_t hal_gpio_pin_configure(hal_gpio_pin_t  pin,
                                         hal_gpio_mode_t mode)
{
    uint32_t iocon_val;
    uint32_t mode_bits;
    uint32_t od_bit;
    bool     set_output;

    hal_gpio_status_t status = validate_pin(pin);
    if (status != HAL_GPIO_OK)
    {
        return status;
    }

    /* True open-drain pins do not support push-pull or pull-down. */
    if (is_true_od_pin(pin.pin))
    {
        if ((mode == HAL_GPIO_MODE_OUTPUT_PUSHPULL)  ||
            (mode == HAL_GPIO_MODE_INPUT_PULLDOWN))
        {
            return HAL_GPIO_ERR_UNSUPPORTED_MODE;
        }
    }

    enable_clocks();

    /* --- Translate HAL mode to IOCON bits and direction --- */
    od_bit     = 0u;
    set_output = false;

    switch (mode)
    {
        case HAL_GPIO_MODE_INPUT_FLOAT:
            mode_bits = IOCON_MODE_FLOAT;
            break;

        case HAL_GPIO_MODE_INPUT_PULLUP:
            mode_bits = IOCON_MODE_PULLUP;
            break;

        case HAL_GPIO_MODE_INPUT_PULLDOWN:
            mode_bits = IOCON_MODE_PULLDOWN;
            break;

        case HAL_GPIO_MODE_OUTPUT_PUSHPULL:
            mode_bits  = IOCON_MODE_FLOAT;
            set_output = true;
            break;

        case HAL_GPIO_MODE_OUTPUT_OPENDRAIN:
            mode_bits  = IOCON_MODE_FLOAT;
            od_bit     = IOCON_OD_BIT;
            set_output = true;
            break;

        case HAL_GPIO_MODE_OUTPUT_OPENDRAIN_PULLUP:
            mode_bits  = IOCON_MODE_PULLUP;
            od_bit     = IOCON_OD_BIT;
            set_output = true;
            break;

        default:
            return HAL_GPIO_ERR_UNSUPPORTED_MODE;
    }

    /* --- Write IOCON register ---
     *
     * Read-modify-write: preserve HYS, INV, FILTR, and reserved bits.
     * Always set DIGIMODE (bit 7) to 1; without it the PIN register reads
     * back 0 regardless of the actual pin state (UM11029 §12.3 Remark).
     */
    iocon_val  =  LPC_IOCON->PIO[pin.port][pin.pin];
    iocon_val &= ~IOCON_WRITE_MASK;
    iocon_val |=  IOCON_FUNC_GPIO;
    iocon_val |=  mode_bits;
    iocon_val |=  od_bit;
    iocon_val |=  IOCON_DIGIMODE_BIT;
    LPC_IOCON->PIO[pin.port][pin.pin] = iocon_val;

    /* --- Set GPIO direction ---
     *
     * DIRSET0 and DIRCLR0 are atomic set/clear registers; no
     * read-modify-write needed, no race condition.
     */
    if (set_output)
    {
        LPC_GPIO_PORT->DIRSET[pin.port] = (1u << pin.pin);
    }
    else
    {
        LPC_GPIO_PORT->DIRCLR[pin.port] = (1u << pin.pin);
    }

    return HAL_GPIO_OK;
}

hal_gpio_status_t hal_gpio_pin_write(hal_gpio_pin_t   pin,
                                     hal_gpio_level_t level)
{
    hal_gpio_status_t status = validate_pin(pin);
    if (status != HAL_GPIO_OK)
    {
        return status;
    }

    if ((level != HAL_GPIO_LEVEL_LOW) && (level != HAL_GPIO_LEVEL_HIGH))
    {
        return HAL_GPIO_ERR_INVALID_ARG;
    }

    /*
     * SET0 and CLR0 are dedicated set/clear registers.  Writing a 1 to a
     * bit drives the corresponding pin HIGH (SET0) or LOW (CLR0).  These
     * are single-cycle atomic operations on the IOP bus — no
     * read-modify-write and no critical section required.
     */
    if (level == HAL_GPIO_LEVEL_HIGH)
    {
        LPC_GPIO_PORT->SET[pin.port] = (1u << pin.pin);
    }
    else
    {
        LPC_GPIO_PORT->CLR[pin.port] = (1u << pin.pin);
    }

    return HAL_GPIO_OK;
}

hal_gpio_status_t hal_gpio_pin_read(hal_gpio_pin_t    pin,
                                    hal_gpio_level_t *p_level)
{
    hal_gpio_status_t status = validate_pin(pin);
    if (status != HAL_GPIO_OK)
    {
        return status;
    }

    if (p_level == NULL)
    {
        return HAL_GPIO_ERR_INVALID_ARG;
    }

    /*
     * PIN0 samples the physical pin state regardless of direction.
     * For output pins this reflects the driven level.
     * For gpio_pin_toggle(), gpio.c reads via this function; the value
     * corresponds to what the pin is actually driving since SET0/CLR0 and
     * PIN0 stay in sync on the LPC845 output path.
     *
     * @note  If the caller needs to read the output latch specifically
     *        (e.g. to toggle without a glitch if the pin is loaded),
     *        SET0 can be read instead: it returns the output latch value.
     *        For this framework's toggle use-case, PIN0 is sufficient
     *        because the pin drives the load directly.
     */
    if ((LPC_GPIO_PORT->PIN[pin.port] & (1u << pin.pin)) != 0u)
    {
        *p_level = HAL_GPIO_LEVEL_HIGH;
    }
    else
    {
        *p_level = HAL_GPIO_LEVEL_LOW;
    }

    return HAL_GPIO_OK;
}

hal_gpio_status_t hal_gpio_port_write(uint8_t  port,
                                      uint32_t mask,
                                      uint32_t value)
{
    if (port > LPC845_GPIO_PORT_MAX)
    {
        return HAL_GPIO_ERR_INVALID_PIN;
    }

    /*
     * The LPC845 provides a MASK/MPIN register pair for atomic masked
     * port writes (UM11029 §9.5.3.6).
     *
     * MASK convention: a 0 bit ENABLES the corresponding pin for
     * modification; a 1 bit PROTECTS it.  This is the inverse of the
     * mask parameter passed to this function, where 1 means "modify".
     *
     * Sequence:
     *   1. Write ~mask to MASK — only the pins of interest have 0.
     *   2. Write value to MPIN — only unmasked pins are affected,
     *      atomically and without glitch.
     *   3. Restore MASK to 0 so that subsequent PIN register accesses
     *      (reads via hal_gpio_port_read) are not affected by a stale
     *      mask.
     *
     * This is a true single-write atomic operation; no read-modify-write
     * and no intermediate glitch on any pin.
     */
    LPC_GPIO_PORT->MASK[port] = ~mask;
    LPC_GPIO_PORT->MPIN[port] =  value;
    LPC_GPIO_PORT->MASK[port] =  0u;

    return HAL_GPIO_OK;
}

hal_gpio_status_t hal_gpio_port_read(uint8_t   port,
                                     uint32_t *p_value)
{
    if (port > LPC845_GPIO_PORT_MAX)
    {
        return HAL_GPIO_ERR_INVALID_PIN;
    }

    if (p_value == NULL)
    {
        return HAL_GPIO_ERR_INVALID_ARG;
    }

    *p_value = LPC_GPIO_PORT->PIN[port];

    return HAL_GPIO_OK;
}

/*===========================================================================*
 * Private helper implementations
 *===========================================================================*/

static void enable_clocks(void)
{
    if (!s_clocks_enabled)
    {
        LPC_SYSCON->SYSAHBCLKCTRL0 |= SYSCON_GPIO_CLK_BIT;
        LPC_SYSCON->SYSAHBCLKCTRL0 |= SYSCON_IOCON_CLK_BIT;
        s_clocks_enabled = true;
    }
}

static bool is_true_od_pin(uint8_t pin_index)
{
    return ((pin_index >= LPC845_TRUE_OD_PIN_FIRST) &&
            (pin_index <= LPC845_TRUE_OD_PIN_LAST));
}

static hal_gpio_status_t validate_pin(hal_gpio_pin_t pin)
{
    if (pin.port > LPC845_GPIO_PORT_MAX)
    {
        return HAL_GPIO_ERR_INVALID_PIN;
    }

    if (pin.pin > LPC845_GPIO_PIN_MAX)
    {
        return HAL_GPIO_ERR_INVALID_PIN;
    }

    return HAL_GPIO_OK;
}
