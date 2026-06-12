/**
 * @file    gpio.c
 * @brief   GPIO library implementation — portable logic layer.
 *
 * @details Implements pin management, output control, and software edge
 *          detection on top of @c hal/hal_gpio.h.  Contains no register
 *          access and no MCU-specific code; all hardware interaction is
 *          delegated to the active port via the HAL interface.
 *
 *          ### Internal state
 *          A static table of @c gpio_descriptor_t entries tracks pins that
 *          have been registered for edge detection.  Pins that only need
 *          configure / set / clear / toggle are not stored here and consume
 *          no RAM beyond the function call stack.
 *
 *          ### Concurrency
 *          @c gpio_edge_update() runs in ISR context (SysTick).
 *          @c gpio_edge_read() may run in main-loop context.  The
 *          @c edge_flag field of each descriptor is the only shared datum;
 *          it is protected with @c CQUEUE_ENTER_CRITICAL() /
 *          @c CQUEUE_EXIT_CRITICAL() on both the write (update) and
 *          read-clear (read) paths.
 *
 * @author  mcuframework contributors
 * @date    2025
 */

#include "lib/gpio/gpio.h"
#include "hal/hal_gpio.h"
#include "target.h"

#include <stddef.h>

/*===========================================================================*
 * Private types
 *===========================================================================*/

/**
 * @brief   Internal descriptor for a registered GPIO pin.
 *
 * One entry per slot in @c s_descriptors[].  The @c in_use flag
 * distinguishes active entries from free slots.
 */
typedef struct
{
    gpio_pin_t  pin;            /**< Hardware port and pin index.               */
    gpio_edge_t edge_cfg;       /**< Which transitions to latch.                */
    gpio_edge_t edge_flag;      /**< Accumulated (unread) edge flags.           */
    bool        prev_level;     /**< Pin level sampled on the previous update.  */
    bool        in_use;         /**< True when this slot is occupied.           */
} gpio_descriptor_t;

/*===========================================================================*
 * Private variables
 *===========================================================================*/

/** @brief  Registration table — statically allocated, no heap. */
static gpio_descriptor_t s_descriptors[GPIO_MAX_REGISTERED_PINS];

/** @brief  True after gpio_init() has been called successfully. */
static bool s_initialized = false;

/*===========================================================================*
 * Private helper prototypes
 *===========================================================================*/

/**
 * @brief   Translate a HAL status code to a GPIO library status code.
 *
 * @param[in]   hal_status  Value returned by a hal_gpio_* function.
 * @return      Corresponding @c gpio_status_t value.
 */
static gpio_status_t translate_hal_status(hal_gpio_status_t hal_status);

/*===========================================================================*
 * Module lifecycle
 *===========================================================================*/

void gpio_init(void)
{
    uint8_t i;

    for (i = 0u; i < GPIO_MAX_REGISTERED_PINS; i++)
    {
        s_descriptors[i].pin.port   = 0u;
        s_descriptors[i].pin.pin    = 0u;
        s_descriptors[i].edge_cfg   = GPIO_EDGE_NONE;
        s_descriptors[i].edge_flag  = GPIO_EDGE_NONE;
        s_descriptors[i].prev_level = false;
        s_descriptors[i].in_use     = false;
    }

    s_initialized = true;
}

void gpio_deinit(void)
{
    uint8_t i;

    for (i = 0u; i < GPIO_MAX_REGISTERED_PINS; i++)
    {
        s_descriptors[i].in_use = false;
    }

    s_initialized = false;
}

/*===========================================================================*
 * Pin configuration (no registration)
 *===========================================================================*/

gpio_status_t gpio_pin_configure(gpio_pin_t pin, hal_gpio_mode_t mode)
{
    if (!s_initialized)
    {
        return GPIO_ERR_NOT_INITIALIZED;
    }

    return translate_hal_status(hal_gpio_pin_configure(pin, mode));
}

/*===========================================================================*
 * Pin registration
 *===========================================================================*/

gpio_status_t gpio_pin_register(gpio_pin_t      pin,
                                hal_gpio_mode_t mode,
                                gpio_edge_t     edge_cfg,
                                gpio_handle_t  *p_handle)
{
    hal_gpio_status_t hal_st;
    hal_gpio_level_t  initial_level;
    uint8_t           free_slot;
    uint8_t           i;

    if (!s_initialized)
    {
        return GPIO_ERR_NOT_INITIALIZED;
    }

    if (p_handle == NULL)
    {
        return GPIO_ERR_INVALID_ARG;
    }

    /* Validate edge_cfg — only bits 0 and 1 are defined. */
    if (((uint8_t)edge_cfg & ~(uint8_t)GPIO_EDGE_BOTH) != 0u)
    {
        *p_handle = GPIO_INVALID_HANDLE;
        return GPIO_ERR_INVALID_ARG;
    }

    /* Find the first free descriptor slot. */
    free_slot = GPIO_INVALID_HANDLE;
    for (i = 0u; i < GPIO_MAX_REGISTERED_PINS; i++)
    {
        if (!s_descriptors[i].in_use)
        {
            free_slot = i;
            break;
        }
    }

    if (free_slot == GPIO_INVALID_HANDLE)
    {
        *p_handle = GPIO_INVALID_HANDLE;
        return GPIO_ERR_TABLE_FULL;
    }

    /* Configure the hardware pin. */
    hal_st = hal_gpio_pin_configure(pin, mode);
    if (hal_st != HAL_GPIO_OK)
    {
        *p_handle = GPIO_INVALID_HANDLE;
        return translate_hal_status(hal_st);
    }

    /*
     * Sample the current level as the initial previous state so that no
     * spurious edge is reported on the first call to gpio_edge_update().
     * If the read fails, default to LOW to avoid an indeterminate state.
     */
    hal_st = hal_gpio_pin_read(pin, &initial_level);
    if (hal_st != HAL_GPIO_OK)
    {
        initial_level = HAL_GPIO_LEVEL_LOW;
    }

    /* Populate the descriptor and mark it in use. */
    s_descriptors[free_slot].pin        = pin;
    s_descriptors[free_slot].edge_cfg   = edge_cfg;
    s_descriptors[free_slot].edge_flag  = GPIO_EDGE_NONE;
    s_descriptors[free_slot].prev_level = (initial_level == HAL_GPIO_LEVEL_HIGH);
    s_descriptors[free_slot].in_use     = true;

    *p_handle = free_slot;
    return GPIO_OK;
}

gpio_status_t gpio_pin_unregister(gpio_handle_t handle)
{
    if (handle >= GPIO_MAX_REGISTERED_PINS)
    {
        return GPIO_ERR_INVALID_HANDLE;
    }

    if (!s_descriptors[handle].in_use)
    {
        return GPIO_ERR_INVALID_HANDLE;
    }

    s_descriptors[handle].in_use = false;
    return GPIO_OK;
}

/*===========================================================================*
 * Output control
 *===========================================================================*/

gpio_status_t gpio_pin_set(gpio_pin_t pin)
{
    if (!s_initialized)
    {
        return GPIO_ERR_NOT_INITIALIZED;
    }

    return translate_hal_status(hal_gpio_pin_write(pin, HAL_GPIO_LEVEL_HIGH));
}

gpio_status_t gpio_pin_clear(gpio_pin_t pin)
{
    if (!s_initialized)
    {
        return GPIO_ERR_NOT_INITIALIZED;
    }

    return translate_hal_status(hal_gpio_pin_write(pin, HAL_GPIO_LEVEL_LOW));
}

gpio_status_t gpio_pin_toggle(gpio_pin_t pin)
{
    hal_gpio_level_t  current_level;
    hal_gpio_status_t hal_st;

    if (!s_initialized)
    {
        return GPIO_ERR_NOT_INITIALIZED;
    }

    hal_st = hal_gpio_pin_read(pin, &current_level);
    if (hal_st != HAL_GPIO_OK)
    {
        return translate_hal_status(hal_st);
    }

    if (current_level == HAL_GPIO_LEVEL_HIGH)
    {
        return translate_hal_status(hal_gpio_pin_write(pin, HAL_GPIO_LEVEL_LOW));
    }
    else
    {
        return translate_hal_status(hal_gpio_pin_write(pin, HAL_GPIO_LEVEL_HIGH));
    }
}

/*===========================================================================*
 * Input reading
 *===========================================================================*/

gpio_status_t gpio_pin_read(gpio_pin_t pin, hal_gpio_level_t *p_level)
{
    if (!s_initialized)
    {
        return GPIO_ERR_NOT_INITIALIZED;
    }

    if (p_level == NULL)
    {
        return GPIO_ERR_INVALID_ARG;
    }

    return translate_hal_status(hal_gpio_pin_read(pin, p_level));
}

/*===========================================================================*
 * Software edge detection
 *===========================================================================*/

void gpio_edge_update(void)
{
    hal_gpio_level_t  current_level;
    hal_gpio_status_t hal_st;
    bool              current_bool;
    gpio_edge_t       detected;
    uint8_t           i;

    for (i = 0u; i < GPIO_MAX_REGISTERED_PINS; i++)
    {
        if (!s_descriptors[i].in_use)
        {
            continue;
        }

        hal_st = hal_gpio_pin_read(s_descriptors[i].pin, &current_level);
        if (hal_st != HAL_GPIO_OK)
        {
            /* Skip this pin on a read error; do not corrupt prev_level. */
            continue;
        }

        current_bool = (current_level == HAL_GPIO_LEVEL_HIGH);
        detected     = GPIO_EDGE_NONE;

        if (current_bool && !s_descriptors[i].prev_level)
        {
            detected = GPIO_EDGE_RISING;
        }
        else if (!current_bool && s_descriptors[i].prev_level)
        {
            detected = GPIO_EDGE_FALLING;
        }
        else
        {
            /* No transition — nothing to latch. */
        }

        if (((uint8_t)detected & (uint8_t)s_descriptors[i].edge_cfg) != 0u)
        {
            /*
             * Latch the edge flag.  The OR-assignment preserves any flag
             * that has not yet been consumed by gpio_edge_read().
             * Critical section protects against concurrent read in
             * main-loop context.
             */
            CQUEUE_ENTER_CRITICAL();
            s_descriptors[i].edge_flag = (gpio_edge_t)
                ((uint8_t)s_descriptors[i].edge_flag | (uint8_t)detected);
            CQUEUE_EXIT_CRITICAL();
        }

        s_descriptors[i].prev_level = current_bool;
    }
}

gpio_edge_t gpio_edge_read(gpio_handle_t handle)
{
    gpio_edge_t flags;

    if (!s_initialized)
    {
        return GPIO_EDGE_NONE;
    }

    if (handle >= GPIO_MAX_REGISTERED_PINS)
    {
        return GPIO_EDGE_NONE;
    }

    if (!s_descriptors[handle].in_use)
    {
        return GPIO_EDGE_NONE;
    }

    /* Atomic read-and-clear. */
    CQUEUE_ENTER_CRITICAL();
    flags = s_descriptors[handle].edge_flag;
    s_descriptors[handle].edge_flag = GPIO_EDGE_NONE;
    CQUEUE_EXIT_CRITICAL();

    return flags;
}

/*===========================================================================*
 * Private helper implementations
 *===========================================================================*/

/**
 * @brief   Translate a HAL status code to a GPIO library status code.
 */
static gpio_status_t translate_hal_status(hal_gpio_status_t hal_status)
{
    gpio_status_t result;

    switch (hal_status)
    {
        case HAL_GPIO_OK:
            result = GPIO_OK;
            break;

        case HAL_GPIO_ERR_INVALID_PIN:
            result = GPIO_ERR_INVALID_PIN;
            break;

        case HAL_GPIO_ERR_UNSUPPORTED_MODE:
            result = GPIO_ERR_UNSUPPORTED_MODE;
            break;

        case HAL_GPIO_ERR_INVALID_ARG:
            result = GPIO_ERR_INVALID_ARG;
            break;

        case HAL_GPIO_ERR_NOT_OUTPUT:
        case HAL_GPIO_ERR_NOT_INPUT:
        default:
            result = GPIO_ERR_HAL;
            break;
    }

    return result;
}
