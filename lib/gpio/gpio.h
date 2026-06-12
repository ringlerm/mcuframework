/**
 * @file    gpio.h
 * @brief   GPIO library — pin management, control, and software edge detection.
 *
 * @details Portable GPIO layer built on top of @c hal/hal_gpio.h.  Provides:
 *
 *          - **Direct control** — configure, set, clear, toggle, and read any
 *            pin without registration overhead.
 *          - **Registered pins** — optional table entry that enables automatic
 *            software edge detection via @c gpio_edge_update() /
 *            @c gpio_edge_read().
 *
 *          ### Design philosophy
 *          Pin configuration and basic I/O do not require registration; a pin
 *          can be configured and driven at any time with no table entry.
 *          Registration is reserved for pins that need edge-flag tracking,
 *          keeping the table small (only inputs of interest) and RAM usage
 *          predictable.
 *
 *          ### Edge detection model
 *          @c gpio_edge_update() must be called periodically (typically as a
 *          SysTick callback) to sample registered input pins and latch
 *          transition flags.  @c gpio_edge_read() returns the accumulated
 *          flags for a handle and atomically clears them (clear-on-read),
 *          so each transition is reported exactly once regardless of how
 *          infrequently the application polls.
 *
 *          ### Typical integration
 *          @code
 *          #include "lib/gpio/gpio.h"
 *          #include "hal/hal_systick.h"
 *
 *          int main(void)
 *          {
 *              // 1. Initialise the module.
 *              gpio_init();
 *
 *              // 2. Configure an output pin (no registration needed).
 *              gpio_pin_t led = {0u, 9u};
 *              gpio_pin_configure(led, HAL_GPIO_MODE_OUTPUT_PUSHPULL);
 *
 *              // 3. Register an input pin for edge detection.
 *              gpio_handle_t btn;
 *              gpio_pin_t btn_pin = {0u, 5u};
 *              gpio_pin_register(btn_pin,
 *                                HAL_GPIO_MODE_INPUT_PULLUP,
 *                                GPIO_EDGE_FALLING,
 *                                &btn);
 *
 *              // 4. Hook edge sampling into SysTick.
 *              hal_systick_register_callback(gpio_edge_update);
 *
 *              while (1)
 *              {
 *                  if (gpio_edge_read(btn) & GPIO_EDGE_FALLING)
 *                  {
 *                      gpio_pin_toggle(led);
 *                  }
 *              }
 *          }
 *          @endcode
 *
 * @note    @c gpio_edge_update() runs in ISR context (SysTick).
 *          @c gpio_edge_read() is protected with the framework's critical-
 *          section macros from @c target.h, making the read safe from both
 *          ISR and main-loop contexts.
 *
 * @note    The maximum number of simultaneously registered pins is set by
 *          @c GPIO_MAX_REGISTERED_PINS.  Override in the build system with
 *          @c -DGPIO_MAX_REGISTERED_PINS=N to avoid editing source files.
 *
 * @author  mcuframework contributors
 * @date    2025
 */

#ifndef GPIO_H
#define GPIO_H

#include <stdint.h>
#include <stdbool.h>
#include "hal/hal_gpio.h"

/*===========================================================================*
 * Public constants
 *===========================================================================*/

/**
 * @brief   Maximum number of pins that can be registered for edge detection.
 *
 * Each registered pin occupies one @c gpio_descriptor_t entry in a static
 * table.  Pins that only need configure / set / clear / toggle do NOT consume
 * a table slot.
 *
 * Override at build time: @c -DGPIO_MAX_REGISTERED_PINS=32
 */
#ifndef GPIO_MAX_REGISTERED_PINS
#define GPIO_MAX_REGISTERED_PINS    (16u)
#endif

/*===========================================================================*
 * Public types
 *===========================================================================*/

/**
 * @brief   Convenience alias for the HAL pin descriptor.
 *
 * Application code uses @c gpio_pin_t; the HAL uses @c hal_gpio_pin_t.
 * They are the same struct — the alias avoids leaking the HAL prefix into
 * every application file while keeping a single source of truth.
 */
typedef hal_gpio_pin_t gpio_pin_t;

/**
 * @brief   Handle returned by @c gpio_pin_register().
 *
 * An opaque index into the internal descriptor table.  Valid handles are
 * in the range @c 0 … (@c GPIO_MAX_REGISTERED_PINS - 1).
 * @c GPIO_INVALID_HANDLE marks an uninitialised or failed registration.
 */
typedef uint8_t gpio_handle_t;

/** @brief  Sentinel value indicating an invalid or unassigned handle. */
#define GPIO_INVALID_HANDLE     (0xFFu)

/**
 * @brief   Edge-detection flags.
 *
 * Used both to configure which edges a registered pin should track
 * (@c gpio_pin_register()) and to report which edges have occurred
 * (@c gpio_edge_read()).
 *
 * Values are bit-flags so they can be OR-combined:
 * @code
 *   gpio_edge_t cfg = GPIO_EDGE_RISING | GPIO_EDGE_FALLING; // == GPIO_EDGE_BOTH
 * @endcode
 */
typedef enum
{
    GPIO_EDGE_NONE    = 0x00u,  /**< No edge of interest / no edge detected.   */
    GPIO_EDGE_RISING  = 0x01u,  /**< Low → High transition.                    */
    GPIO_EDGE_FALLING = 0x02u,  /**< High → Low transition.                    */
    GPIO_EDGE_BOTH    = 0x03u,  /**< Either transition.                         */
} gpio_edge_t;

/**
 * @brief   Return-status codes for GPIO library operations.
 */
typedef enum
{
    GPIO_OK                   = 0,  /**< Operation succeeded.                      */
    GPIO_ERR_INVALID_PIN      = 1,  /**< Port or pin index rejected by the HAL.    */
    GPIO_ERR_UNSUPPORTED_MODE = 2,  /**< Mode not available on this hardware.      */
    GPIO_ERR_INVALID_ARG      = 3,  /**< A parameter is NULL or out of range.      */
    GPIO_ERR_TABLE_FULL       = 4,  /**< No free slot in the registration table.   */
    GPIO_ERR_INVALID_HANDLE   = 5,  /**< Handle is out of range or not in use.     */
    GPIO_ERR_NOT_INITIALIZED  = 6,  /**< gpio_init() has not been called.          */
    GPIO_ERR_HAL              = 7,  /**< Unexpected error returned by the HAL.     */
} gpio_status_t;

/*===========================================================================*
 * Public function prototypes — module lifecycle
 *===========================================================================*/

/**
 * @brief   Initialise the GPIO library.
 *
 * Clears the internal registration table and marks every slot as free.
 * Must be called once before any other @c gpio_* function.
 * Safe to call more than once (re-initialises, discards all handles).
 */
void gpio_init(void);

/**
 * @brief   De-initialise the GPIO library.
 *
 * Releases all registered handles.  Does not reconfigure hardware pins;
 * their electrical state is preserved until explicitly changed.
 */
void gpio_deinit(void);

/*===========================================================================*
 * Public function prototypes — pin configuration (no registration)
 *===========================================================================*/

/**
 * @brief   Configure the electrical mode of a GPIO pin.
 *
 * Delegates directly to the HAL.  No table slot is consumed; this function
 * is appropriate for pins that need only output control or a single-shot
 * input read.
 *
 * @param[in]   pin     Pin to configure.
 * @param[in]   mode    Desired electrical mode.
 *
 * @return  @c GPIO_OK on success.
 * @return  @c GPIO_ERR_NOT_INITIALIZED if @c gpio_init() was not called.
 * @return  @c GPIO_ERR_INVALID_PIN if the pin is out of range for the target.
 * @return  @c GPIO_ERR_UNSUPPORTED_MODE if the mode is not available.
 */
gpio_status_t gpio_pin_configure(gpio_pin_t      pin,
                                 hal_gpio_mode_t mode);

/*===========================================================================*
 * Public function prototypes — pin registration (edge detection)
 *===========================================================================*/

/**
 * @brief   Register a pin for periodic edge detection.
 *
 * Allocates a descriptor table slot, configures the pin hardware, and
 * samples the current level as the initial "previous state" so no spurious
 * edge is reported at startup.
 *
 * @param[in]   pin         Pin to monitor.
 * @param[in]   mode        Electrical mode (should be an input mode).
 * @param[in]   edge_cfg    Which transitions to track (@c GPIO_EDGE_RISING,
 *                          @c GPIO_EDGE_FALLING, or @c GPIO_EDGE_BOTH).
 * @param[out]  p_handle    Receives the handle for subsequent calls.  Must
 *                          not be NULL.  Set to @c GPIO_INVALID_HANDLE on
 *                          failure.
 *
 * @return  @c GPIO_OK on success.
 * @return  @c GPIO_ERR_NOT_INITIALIZED if @c gpio_init() was not called.
 * @return  @c GPIO_ERR_INVALID_ARG if @p p_handle is NULL or @p edge_cfg is
 *          not a valid combination of @c gpio_edge_t flags.
 * @return  @c GPIO_ERR_TABLE_FULL if @c GPIO_MAX_REGISTERED_PINS entries are
 *          already in use.
 * @return  @c GPIO_ERR_INVALID_PIN / @c GPIO_ERR_UNSUPPORTED_MODE propagated
 *          from the HAL configure call.
 */
gpio_status_t gpio_pin_register(gpio_pin_t       pin,
                                hal_gpio_mode_t  mode,
                                gpio_edge_t      edge_cfg,
                                gpio_handle_t   *p_handle);

/**
 * @brief   Unregister a previously registered pin.
 *
 * Frees the descriptor table slot.  The pin's hardware configuration is not
 * changed.  After this call the handle is invalid and must not be used.
 *
 * @param[in]   handle  Handle returned by @c gpio_pin_register().
 *
 * @return  @c GPIO_OK on success.
 * @return  @c GPIO_ERR_INVALID_HANDLE if the handle is out of range or the
 *          slot is not currently in use.
 */
gpio_status_t gpio_pin_unregister(gpio_handle_t handle);

/*===========================================================================*
 * Public function prototypes — output control
 *===========================================================================*/

/**
 * @brief   Drive an output pin to the logical high level.
 *
 * @param[in]   pin     Output pin to assert.
 *
 * @return  @c GPIO_OK on success.
 * @return  @c GPIO_ERR_NOT_INITIALIZED if @c gpio_init() was not called.
 * @return  @c GPIO_ERR_INVALID_PIN if the pin is out of range.
 */
gpio_status_t gpio_pin_set(gpio_pin_t pin);

/**
 * @brief   Drive an output pin to the logical low level.
 *
 * @param[in]   pin     Output pin to de-assert.
 *
 * @return  @c GPIO_OK on success.
 * @return  @c GPIO_ERR_NOT_INITIALIZED if @c gpio_init() was not called.
 * @return  @c GPIO_ERR_INVALID_PIN if the pin is out of range.
 */
gpio_status_t gpio_pin_clear(gpio_pin_t pin);

/**
 * @brief   Toggle an output pin (high → low or low → high).
 *
 * Reads the current output latch value and writes the complement.  The
 * read-modify-write sequence is not atomic; the caller must ensure no
 * concurrent access when toggling from multiple contexts.
 *
 * @param[in]   pin     Output pin to toggle.
 *
 * @return  @c GPIO_OK on success.
 * @return  @c GPIO_ERR_NOT_INITIALIZED if @c gpio_init() was not called.
 * @return  @c GPIO_ERR_INVALID_PIN if the pin is out of range.
 */
gpio_status_t gpio_pin_toggle(gpio_pin_t pin);

/*===========================================================================*
 * Public function prototypes — input reading
 *===========================================================================*/

/**
 * @brief   Read the instantaneous logic level of a pin.
 *
 * Samples the input data register directly via the HAL.  May be called on
 * both registered and unregistered pins.
 *
 * @param[in]   pin         Pin to sample.
 * @param[out]  p_level     Receives @c HAL_GPIO_LEVEL_HIGH or
 *                          @c HAL_GPIO_LEVEL_LOW.  Must not be NULL.
 *
 * @return  @c GPIO_OK on success.
 * @return  @c GPIO_ERR_NOT_INITIALIZED if @c gpio_init() was not called.
 * @return  @c GPIO_ERR_INVALID_PIN if the pin is out of range.
 * @return  @c GPIO_ERR_INVALID_ARG if @p p_level is NULL.
 */
gpio_status_t gpio_pin_read(gpio_pin_t        pin,
                             hal_gpio_level_t *p_level);

/*===========================================================================*
 * Public function prototypes — software edge detection
 *===========================================================================*/

/**
 * @brief   Sample all registered pins and latch edge flags.
 *
 * For each active descriptor, reads the current pin level and compares it
 * against the previous sample.  If a transition matching the configured
 * @c edge_cfg is detected, the corresponding flag bit is OR-ed into the
 * descriptor's @c edge_flag field.
 *
 * @par ISR usage
 * Designed to be registered as a SysTick callback via
 * @c hal_systick_register_callback(gpio_edge_update).  Executes in ISR
 * context; kept minimal to reduce interrupt latency.
 *
 * @note    Because this function writes @c edge_flag from ISR context while
 *          @c gpio_edge_read() may read it from main-loop context, the write
 *          is protected with @c CQUEUE_ENTER_CRITICAL() / @c CQUEUE_EXIT_CRITICAL().
 */
void gpio_edge_update(void);

/**
 * @brief   Read and clear the accumulated edge flags for a registered pin.
 *
 * Returns the edge flags that have been latched since the last call and
 * atomically clears them (clear-on-read).  Each transition is therefore
 * reported exactly once, regardless of polling rate.
 *
 * @par Example
 * @code
 *   gpio_edge_t edges = gpio_edge_read(btn_handle);
 *   if (edges & GPIO_EDGE_FALLING)
 *   {
 *       // button pressed
 *   }
 * @endcode
 *
 * @param[in]   handle  Handle returned by @c gpio_pin_register().
 *
 * @return  Accumulated @c gpio_edge_t flags since the last read.
 * @return  @c GPIO_EDGE_NONE if no edge occurred, the handle is invalid, or
 *          the module is uninitialised.
 *
 * @note    The clear operation is protected with the framework critical-section
 *          macros to prevent a race with @c gpio_edge_update() running from
 *          ISR context.
 */
gpio_edge_t gpio_edge_read(gpio_handle_t handle);

#endif /* GPIO_H */
