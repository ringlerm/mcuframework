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
#include "LPC845.h"
#include <stddef.h>

/*===========================================================================*
 * Private constants — LPC845 hardware limits
 *===========================================================================*/

/** @brief  PORT0 & PORT1 exists on the LPC845. */
#define LPC845_GPIO_PORT_MAX        (1u)

/** @brief  Highest valid pin index on the LPC845 (PIO0_0 … PIO0_31). */
#define LPC845_GPIO_PIN_MAX_P0         (31u)
#define LPC845_GPIO_PIN_MAX_P1         (21u)

/** @brief  First true open-drain pin index. */
#define LPC845_TRUE_OD_PIN_FIRST    (10u)

/** @brief  Last true open-drain pin index. */
#define LPC845_TRUE_OD_PIN_LAST     (11u)




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
 * @brief   Validate port and pin range.
 *
 * @param[in]   pin     Pin descriptor to validate.
 * @return      HAL_GPIO_OK if valid, HAL_GPIO_ERR_INVALID_PIN otherwise.
 */
static hal_gpio_status_t validate_pin(hal_gpio_pin_t pin);

/*===========================================================================*
 * Public function implementations
 *===========================================================================*/

hal_gpio_status_t hal_gpio_pin_configure(hal_gpio_pin_t pin, hal_gpio_mode_t mode)
{
    uint32_t iocon_val;
    uint8_t iocon_index;
    uint32_t mode_bits;
    uint32_t od_bit;
    bool     set_output;

    // 1. Validar el pin antes de operar
    hal_gpio_status_t status = validate_pin(pin);
    if (status != HAL_GPIO_OK)
    {
        return status;
    }

    // Tabla de búsqueda estática para traducir [port][pin] al índice real de IOCON
    static const uint8_t iocon_lookup[2][32] = {
        [0] = {
            [0]  = IOCON_INDEX_PIO0_0,  [1]  = IOCON_INDEX_PIO0_1,  [2]  = IOCON_INDEX_PIO0_2,
            [3]  = IOCON_INDEX_PIO0_3,  [4]  = IOCON_INDEX_PIO0_4,  [5]  = IOCON_INDEX_PIO0_5,
            [6]  = IOCON_INDEX_PIO0_6,  [7]  = IOCON_INDEX_PIO0_7,  [8]  = IOCON_INDEX_PIO0_8,
            [9]  = IOCON_INDEX_PIO0_9,  [10] = IOCON_INDEX_PIO0_10, [11] = IOCON_INDEX_PIO0_11,
            [12] = IOCON_INDEX_PIO0_12, [13] = IOCON_INDEX_PIO0_13, [14] = IOCON_INDEX_PIO0_14,
            [15] = IOCON_INDEX_PIO0_15, [16] = IOCON_INDEX_PIO0_16, [17] = IOCON_INDEX_PIO0_17,
            [18] = IOCON_INDEX_PIO0_18, [19] = IOCON_INDEX_PIO0_19, [20] = IOCON_INDEX_PIO0_20,
            [21] = IOCON_INDEX_PIO0_21, [22] = IOCON_INDEX_PIO0_22, [23] = IOCON_INDEX_PIO0_23,
            [24] = IOCON_INDEX_PIO0_24, [25] = IOCON_INDEX_PIO0_25, [26] = IOCON_INDEX_PIO0_26,
            [27] = IOCON_INDEX_PIO0_27, [28] = IOCON_INDEX_PIO0_28, [29] = IOCON_INDEX_PIO0_29,
            [30] = IOCON_INDEX_PIO0_30, [31] = IOCON_INDEX_PIO0_31
        },
        [1] = {
            [0]  = IOCON_INDEX_PIO1_0,  [1]  = IOCON_INDEX_PIO1_1,  [2]  = IOCON_INDEX_PIO1_2,
            [3]  = IOCON_INDEX_PIO1_3,  [4]  = IOCON_INDEX_PIO1_4,  [5]  = IOCON_INDEX_PIO1_5,
            [6]  = IOCON_INDEX_PIO1_6,  [7]  = IOCON_INDEX_PIO1_7,  [8]  = IOCON_INDEX_PIO1_8,
            [9]  = IOCON_INDEX_PIO1_9,  [10] = IOCON_INDEX_PIO1_10, [11] = IOCON_INDEX_PIO1_11,
            [12] = IOCON_INDEX_PIO1_12, [13] = IOCON_INDEX_PIO1_13, [14] = IOCON_INDEX_PIO1_14,
            [15] = IOCON_INDEX_PIO1_15, [16] = IOCON_INDEX_PIO1_16, [17] = IOCON_INDEX_PIO1_17,
            [18] = IOCON_INDEX_PIO1_18, [19] = IOCON_INDEX_PIO1_19, [20] = IOCON_INDEX_PIO1_20,
            [21] = IOCON_INDEX_PIO1_21, [22 ... 31] = 255
        }
    };

    // Obtener el índice físico del IOCON correspondiente al pin solicitado
    iocon_index = iocon_lookup[pin.port][pin.pin];

    enable_clocks();

    /* --- Traducir el modo HAL a bits de IOCON usando tus macros --- */
    od_bit     = IOCON_PIO_OD(0); // Por defecto deshabilitado (0b0)
    set_output = false;

    switch (mode)
    {
        case HAL_GPIO_MODE_INPUT_FLOAT:
            mode_bits = IOCON_PIO_MODE(0); // Inactive (0b00)
            break;

        case HAL_GPIO_MODE_INPUT_PULLUP:
            mode_bits = IOCON_PIO_MODE(2); // Pull-up (0b10)
            break;

        case HAL_GPIO_MODE_INPUT_PULLDOWN:
            mode_bits = IOCON_PIO_MODE(1); // Pull-down (0b01)
            break;

        case HAL_GPIO_MODE_OUTPUT_PUSHPULL:
            mode_bits  = IOCON_PIO_MODE(0); // Inactive (0b00)
            set_output = true;
            break;

        case HAL_GPIO_MODE_OUTPUT_OPENDRAIN:
            mode_bits  = IOCON_PIO_MODE(0);
            od_bit     = IOCON_PIO_OD(1);   // Open-drain enabled (0b1)
            set_output = true;
            break;

        case HAL_GPIO_MODE_OUTPUT_OPENDRAIN_PULLUP:
            mode_bits  = IOCON_PIO_MODE(2); // Pull-up (0b10)
            od_bit     = IOCON_PIO_OD(1);   // Open-drain enabled (0b1)
            set_output = true;
            break;

        default:
            return HAL_GPIO_ERR_UNSUPPORTED_MODE;
    }

    /* --- Escritura del registro IOCON corregido --- */
    iocon_val = 0u | mode_bits | od_bit;
    // Guardamos en el hardware usando el puntero corregido
    IOCON->PIO[iocon_index] = iocon_val;

    /* --- Configuración de la dirección GPIO --- */
    // Corrección de punteros para el periférico GPIO estándar (LPC_GPIO_PORT -> GPIO)
    // Nota: Ajusta "GPIO" al nombre exacto de la base de GPIO de tu SDK (ej: GPIO u o_GPIO)
    if (set_output)
    {
        GPIO->DIRSET[pin.port] = (1u << pin.pin);
    }
    else
    {
        GPIO->DIRCLR[pin.port] = (1u << pin.pin);
    }

    return HAL_GPIO_OK;
}

hal_gpio_status_t hal_gpio_pin_write(hal_gpio_pin_t   pin,
                                     hal_gpio_level_t level)
{
    hal_gpio_status_t status = validate_pin(pin);
    if (status != HAL_GPIO_OK)
        return status;

    switch (level)
    {
        case HAL_GPIO_LEVEL_LOW:
            GPIO->CLR[pin.port] = (1u << pin.pin);
            break;
        case HAL_GPIO_LEVEL_HIGH:
            GPIO->SET[pin.port] = (1u << pin.pin);
            break;
        default:
            return HAL_GPIO_ERR_INVALID_ARG;
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

    *p_level = (GPIO->B[pin.port][pin.pin]) ? HAL_GPIO_LEVEL_HIGH : HAL_GPIO_LEVEL_LOW;

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
    uint32_t prev_mask = GPIO->MASK[port];
    GPIO->MASK[port] = ~mask;
    GPIO->MPIN[port] =  value;
    GPIO->MASK[port] =  prev_mask;

    return HAL_GPIO_OK;
}

hal_gpio_status_t hal_gpio_port_read(uint8_t  port,
                                     uint32_t mask,
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
    uint32_t prev_mask = GPIO->MASK[port];
    GPIO->MASK[port] = ~mask;
    *p_value = GPIO->MPIN[port];
    GPIO->MASK[port] =  prev_mask;

    return HAL_GPIO_OK;
}

/*===========================================================================*
 * Private helper implementations
 *===========================================================================*/

static void enable_clocks(void)
{
    if (!s_clocks_enabled)
    {
        SYSCON->SYSAHBCLKCTRL0 |= SYSCON_SYSAHBCLKCTRL0_GPIO0(1)
        | SYSCON_SYSAHBCLKCTRL0_GPIO1(1)
        | SYSCON_SYSAHBCLKCTRL0_IOCON(1);
        s_clocks_enabled = true;
    }
}


static hal_gpio_status_t validate_pin(hal_gpio_pin_t pin)
{
    switch (pin.port)
    {
        case 0u:
            if (pin.pin > LPC845_GPIO_PIN_MAX_P0)
            {
                return HAL_GPIO_ERR_INVALID_PIN;
            }
            break;

        case 1u:
            if (pin.pin > LPC845_GPIO_PIN_MAX_P1)
            {
                return HAL_GPIO_ERR_INVALID_PIN;
            }
            break;

        default:
            return HAL_GPIO_ERR_INVALID_PIN;
    }
    return HAL_GPIO_OK;
}
