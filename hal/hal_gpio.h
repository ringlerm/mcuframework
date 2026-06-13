/**
 * @file    hal_gpio.h
 * @brief   Hardware Abstraction Layer — GPIO pin control.
 *
 * @details Declares the contract that every GPIO port implementation must
 *          satisfy.  This header contains only types, constants, and
 *          function prototypes — no register access, no MCU-specific
 *          conditionals, no implementation.
 *
 *          The concrete register-level implementation lives exclusively in
 *          the corresponding port file:
 *          @code
 *          ports/<target>/hal_gpio_<target>.c
 *          @endcode
 *
 *          ### Responsibilities of this layer
 *          - Configure a pin's electrical mode (direction, drive, pull).
 *          - Read the physical level of an input pin.
 *          - Write the physical level of an output pin.
 *          - Read / write a full GPIO port word (optional optimisation).
 *
 *          ### What this layer does NOT do
 *          - Edge detection (handled by lib/gpio).
 *          - Pin registration or handle management (handled by lib/gpio).
 *          - Debouncing (handled by lib/gpio or the application).
 *          - Alternate-function / peripheral muxing (separate HAL module).
 *
 *          ### Typical integration
 *          This header is included by @c lib/gpio/gpio.h and by port files.
 *          Application code should include @c lib/gpio/gpio.h instead.
 *
 * @note    All functions in this header execute with direct register access
 *          and do not enter or exit critical sections themselves.  The
 *          caller (lib/gpio) is responsible for protecting shared state.
 *
 * @author  mcuframework contributors
 * @date    2025
 */

#ifndef HAL_GPIO_H
#define HAL_GPIO_H

#include <stdint.h>
#include <stdbool.h>

/*===========================================================================*
 * Public types
 *===========================================================================*/

/**
 * @brief   Identifies a single GPIO pin by port index and bit position.
 *
 * @note    Port and pin numbering are zero-based and follow the vendor
 *          convention for the active target (e.g. PORT0 pin 5 → {0u, 5u}).
 */
typedef struct
{
    uint8_t port;   /**< Port index (e.g. 0 for PORT0 / GPIOA). */
    uint8_t pin;    /**< Pin index within the port (0–31).       */
} hal_gpio_pin_t;

/**
 * @brief   Electrical mode of a GPIO pin.
 *
 * The port implementation translates each enumerator to the correct
 * combination of direction, pull, and drive registers for the active MCU.
 * If a requested mode is not supported by the hardware, the port must
 * return @c HAL_GPIO_ERR_UNSUPPORTED_MODE.
 */
typedef enum
{
    /** Input, no internal pull resistor. */
    HAL_GPIO_MODE_INPUT_FLOAT           = 0,

    /** Input with internal pull-up resistor enabled. */
    HAL_GPIO_MODE_INPUT_PULLUP          = 1,

    /** Input with internal pull-down resistor enabled. */
    HAL_GPIO_MODE_INPUT_PULLDOWN        = 2,

    /** Output, push-pull drive (actively drives high and low). */
    HAL_GPIO_MODE_OUTPUT_PUSHPULL       = 3,

    /** Output, open-drain drive (actively pulls low; high-Z when high). */
    HAL_GPIO_MODE_OUTPUT_OPENDRAIN      = 4,

    /**
     * Output, open-drain drive with internal pull-up resistor enabled.
     * Suitable for I²C-style wired-AND buses without an external resistor.
     */
    HAL_GPIO_MODE_OUTPUT_OPENDRAIN_PULLUP = 5,
} hal_gpio_mode_t;

/**
 * @brief   Logic level of a GPIO pin.
 */
typedef enum
{
    HAL_GPIO_LEVEL_LOW  = 0,    /**< Pin is at logical 0 (near GND).    */
    HAL_GPIO_LEVEL_HIGH = 1,    /**< Pin is at logical 1 (near VCC).    */
} hal_gpio_level_t;

/**
 * @brief   Return-status codes for HAL GPIO operations.
 */
typedef enum
{
    HAL_GPIO_OK                  = 0,   /**< Operation succeeded.                  */
    HAL_GPIO_ERR_INVALID_PIN     = 1,   /**< Port or pin index out of range.       */
    HAL_GPIO_ERR_UNSUPPORTED_MODE = 2,  /**< Mode not available on this hardware.  */
    HAL_GPIO_ERR_INVALID_ARG     = 3,   /**< A parameter value is not valid.       */
    HAL_GPIO_ERR_NOT_OUTPUT      = 4,   /**< Write attempted on an input pin.      */
    HAL_GPIO_ERR_NOT_INPUT       = 5,   /**< Read attempted on an output-only pin. */
} hal_gpio_status_t;

/*===========================================================================*
 * Public function prototypes
 *===========================================================================*/

/**
 * @brief   Configure the electrical mode of a GPIO pin.
 *
 * Applies direction, pull, and drive settings to the hardware registers.
 * May be called more than once on the same pin to change its configuration
 * at runtime.
 *
 * @param[in]   pin     Port and pin index to configure.
 * @param[in]   mode    Desired electrical mode.
 *
 * @return  @c HAL_GPIO_OK on success.
 * @return  @c HAL_GPIO_ERR_INVALID_PIN if the port or pin index is out of
 *          range for the active target.
 * @return  @c HAL_GPIO_ERR_UNSUPPORTED_MODE if the requested mode is not
 *          available on the active hardware.
 */
hal_gpio_status_t hal_gpio_pin_configure(hal_gpio_pin_t  pin,
                                         hal_gpio_mode_t mode);

/**
 * @brief   Write a logic level to an output pin.
 *
 * The pin must have been configured with an output mode; behaviour is
 * undefined if called on a pin configured as input.
 *
 * @param[in]   pin     Port and pin index to drive.
 * @param[in]   level   @c HAL_GPIO_LEVEL_HIGH or @c HAL_GPIO_LEVEL_LOW.
 *
 * @return  @c HAL_GPIO_OK on success.
 * @return  @c HAL_GPIO_ERR_INVALID_PIN if the port or pin index is out of
 *          range.
 * @return  @c HAL_GPIO_ERR_INVALID_ARG if @p level is not a valid enumerator.
 */
hal_gpio_status_t hal_gpio_pin_write(hal_gpio_pin_t   pin,
                                     hal_gpio_level_t level);


/**
 * @brief   Toggle a logic level to an output pin.
 *
 * The pin must have been configured with an output mode; behaviour is
 * undefined if called on a pin configured as input.
 *
 * @param[in]   pin     Port and pin index to drive.
 *
 * @return  @c HAL_GPIO_OK on success.
 * @return  @c HAL_GPIO_ERR_INVALID_PIN if the port or pin index is out of
 *          range.
 */
hal_gpio_status_t hal_gpio_pin_toggle(hal_gpio_pin_t   pin);


/**
 * @brief   Read the current logic level of a pin.
 *
 * Reads the input data register regardless of the pin's configured direction,
 * allowing the output latch to be verified when the pin is an output.
 *
 * @param[in]   pin         Port and pin index to sample.
 * @param[out]  p_level     Destination for the sampled level.  Must not be
 *                          NULL.
 *
 * @return  @c HAL_GPIO_OK on success.
 * @return  @c HAL_GPIO_ERR_INVALID_PIN if the port or pin index is out of
 *          range.
 * @return  @c HAL_GPIO_ERR_INVALID_ARG if @p p_level is NULL.
 */
hal_gpio_status_t hal_gpio_pin_read(hal_gpio_pin_t    pin,
                                    hal_gpio_level_t *p_level);

/**
 * @brief   Write a masked value to a full GPIO port word.
 *
 * Applies @p mask to isolate the bits that will be modified, then writes
 * the corresponding bits of @p value to the port output register.  Bits
 * outside the mask are left unchanged.
 *
 * Useful for driving parallel buses (LCD data, shift-register clocking)
 * without a per-pin loop.
 *
 * @param[in]   port    Port index (0-based).
 * @param[in]   mask    Bitmask of pins to modify.
 * @param[in]   value   Logic levels to apply to the masked pins.
 *
 * @return  @c HAL_GPIO_OK on success.
 * @return  @c HAL_GPIO_ERR_INVALID_PIN if @p port is out of range.
 */
hal_gpio_status_t hal_gpio_port_write(uint8_t  port,
                                      uint32_t mask,
                                      uint32_t value);

/**
 * @brief   Read the current input word of a full GPIO port.
 *
 * Samples the entire input data register for the given port and stores
 * the result in @p p_value.  Each bit corresponds to a pin index.
 *
 * @param[in]   port        Port index (0-based).
 * @param[out]  p_value     Destination for the port word.  Must not be NULL.
 *
 * @return  @c HAL_GPIO_OK on success.
 * @return  @c HAL_GPIO_ERR_INVALID_PIN if @p port is out of range.
 * @return  @c HAL_GPIO_ERR_INVALID_ARG if @p p_value is NULL.
 */
hal_gpio_status_t hal_gpio_port_read(uint8_t  port,
                                     uint32_t mask,
                                     uint32_t *p_value);

#endif /* HAL_GPIO_H */
