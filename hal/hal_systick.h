/**
 * @file    hal_systick.h
 * @brief   Hardware Abstraction Layer — SysTick periodic tick source.
 *
 * @details This module provides a platform-independent interface for a
 *          periodic system tick used as the time base for the rest of the
 *          framework (primarily soft_timers).
 *
 *          The HAL layer declares the contract (this header).  The concrete
 *          register-level implementation lives in the corresponding port file
 *          under @c ports/<target>/hal_systick_<target>.c.
 *
 *          ### Default tick period
 *          Unless overridden at compile time, the tick period defaults to
 *          1 ms (1 kHz), which matches the default resolution expected by
 *          soft_timers.  Override by defining @c HAL_SYSTICK_DEFAULT_PERIOD_MS
 *          in your build system or before including this header:
 *          @code
 *          #define HAL_SYSTICK_DEFAULT_PERIOD_MS  (5u)   // 5 ms tick
 *          #include "hal/hal_systick.h"
 *          @endcode
 *
 *          ### Typical integration sequence
 *          @code
 *          // main.c
 *          #include "hal/hal_systick.h"
 *          #include "lib/soft_timers/soft_timers.h"
 *
 *          int main(void)
 *          {
 *              // 1. Start SysTick at 1 ms (default), 72 MHz core clock.
 *              hal_systick_init(72000000u, HAL_SYSTICK_DEFAULT_PERIOD_MS);
 *
 *              // 2. Hand the same tick rate to soft_timers.
 *              soft_timers_init(hal_systick_get_tick_hz());
 *
 *              // 3. Register the soft-timer tick as a SysTick subscriber.
 *              hal_systick_register_callback(soft_timers_tick);
 *
 *              while (1)
 *              {
 *                  soft_timers_process();
 *              }
 *          }
 *          @endcode
 *
 *          ### ISR wiring
 *          The port implementation must call @c hal_systick_isr_handler()
 *          from the hardware interrupt vector.  On Cortex-M devices this is
 *          typically:
 *          @code
 *          void SysTick_Handler(void)
 *          {
 *              hal_systick_isr_handler();
 *          }
 *          @endcode
 *
 * @note    All callback functions registered via
 *          @c hal_systick_register_callback() execute in ISR context and
 *          therefore must be short, non-blocking, and ISR-safe.
 *
 * @author  mcuframework contributors
 * @date    2025
 */

#ifndef HAL_SYSTICK_H
#define HAL_SYSTICK_H

#include <stdint.h>
#include <stdbool.h>
#include "target.h"

/*===========================================================================*
 * Public constants
 *===========================================================================*/

/**
 * @brief   Default SysTick period in milliseconds.
 *
 * Produces a 1 kHz tick, which gives 1 ms resolution to soft_timers.
 * Override before including this header if a different period is required.
 */
#ifndef HAL_SYSTICK_DEFAULT_PERIOD_MS
#define HAL_SYSTICK_DEFAULT_PERIOD_MS   (1u)
#endif

/**
 * @brief   Maximum number of callbacks that can be registered with the tick.
 *
 * Increase this value if more subscribers are needed.  Each slot occupies
 * one function-pointer (typically 4 bytes on 32-bit targets).
 */
#ifndef HAL_SYSTICK_MAX_CALLBACKS
#define HAL_SYSTICK_MAX_CALLBACKS       (4u)
#endif

/*===========================================================================*
 * Public types
 *===========================================================================*/

/**
 * @brief   Prototype for a SysTick subscriber callback.
 *
 * Functions registered via hal_systick_register_callback() must match this
 * signature.  They execute in ISR context and must be short and non-blocking.
 */
typedef void (*hal_systick_callback_t)(void);

/**
 * @brief   Return-status codes used by the HAL SysTick API.
 */
typedef enum
{
    HAL_SYSTICK_OK              = 0,    /**< Operation succeeded.             */
    HAL_SYSTICK_ERR_INVALID_ARG = 1,    /**< A parameter was out of range.    */
    HAL_SYSTICK_ERR_FULL        = 2,    /**< Callback table is full.          */
    HAL_SYSTICK_ERR_NOT_FOUND   = 3,    /**< Callback was not registered.     */
    HAL_SYSTICK_ERR_HW_FAULT    = 4,    /**< Hardware configuration failed.   */
} hal_systick_status_t;

/*===========================================================================*
 * Public function prototypes
 *===========================================================================*/

/**
 * @brief   Initialise and start the SysTick peripheral.
 *
 * Configures the hardware to generate a periodic interrupt at the requested
 * period and enables the interrupt.  If this function is called while the
 * peripheral is already running, it is reconfigured with the new parameters.
 *
 * @param[in]   cpu_hz      Core clock frequency in Hz (e.g. 72000000u for
 *                          72 MHz).  Must be greater than zero.
 * @param[in]   period_ms   Interrupt period in milliseconds.
 *                          Use @c HAL_SYSTICK_DEFAULT_PERIOD_MS for the
 *                          framework default (1 ms).  Must be > 0.
 *
 * @return  @c HAL_SYSTICK_OK on success.
 * @return  @c HAL_SYSTICK_ERR_INVALID_ARG if any parameter is zero.
 * @return  @c HAL_SYSTICK_ERR_HW_FAULT if the reload value exceeds the
 *          counter's maximum (hardware cannot achieve the requested period
 *          at the given clock frequency).
 */
hal_systick_status_t hal_systick_init(uint32_t cpu_hz, uint32_t period_ms);

/**
 * @brief   Stop the SysTick peripheral and disable its interrupt.
 *
 * All registered callbacks are preserved; the tick can be resumed by
 * calling hal_systick_init() again.
 */
void hal_systick_deinit(void);

/**
 * @brief   Register a callback to be invoked on every SysTick interrupt.
 *
 * The callback executes in ISR context.  Up to @c HAL_SYSTICK_MAX_CALLBACKS
 * callbacks may be registered simultaneously.
 *
 * @param[in]   callback    Non-NULL pointer to the function to register.
 *
 * @return  @c HAL_SYSTICK_OK if the callback was registered.
 * @return  @c HAL_SYSTICK_ERR_INVALID_ARG if @p callback is NULL.
 * @return  @c HAL_SYSTICK_ERR_FULL if no callback slots remain.
 */
hal_systick_status_t hal_systick_register_callback(hal_systick_callback_t callback);

/**
 * @brief   Unregister a previously registered callback.
 *
 * @param[in]   callback    Pointer that was previously passed to
 *                          hal_systick_register_callback().
 *
 * @return  @c HAL_SYSTICK_OK if the callback was removed.
 * @return  @c HAL_SYSTICK_ERR_INVALID_ARG if @p callback is NULL.
 * @return  @c HAL_SYSTICK_ERR_NOT_FOUND if the callback was not registered.
 */
hal_systick_status_t hal_systick_unregister_callback(hal_systick_callback_t callback);

/**
 * @brief   Return the configured tick frequency in Hz.
 *
 * The value is derived from the parameters passed to hal_systick_init().
 * Returns 0 if hal_systick_init() has not been called successfully.
 *
 * Use this to initialise soft_timers with the exact same frequency:
 * @code
 *   soft_timers_init(hal_systick_get_tick_hz());
 * @endcode
 *
 * @return  Tick frequency in Hz, or 0 if uninitialised.
 */
uint32_t hal_systick_get_tick_hz(void);

/**
 * @brief   Return the running tick counter value.
 *
 * Increments by one on every SysTick interrupt.  Wraps around at
 * @c UINT32_MAX (approximately 49.7 days at 1 kHz).
 *
 * @return  Current tick count.
 */
uint32_t hal_systick_get_tick_count(void);

/**
 * @brief   Blocking delay using the SysTick counter.
 *
 * Spins until @p ms milliseconds have elapsed.  Intended only for
 * initialisation sequences; use soft_timers for non-blocking delays.
 *
 * @warning Calling this function with interrupts disabled will hang forever.
 *
 * @param[in]   ms  Number of milliseconds to wait.  A value of 0 returns
 *                  immediately.
 */
void hal_systick_delay_ms(uint32_t ms);

/**
 * @brief   SysTick ISR body — must be called from the hardware vector.
 *
 * Increments the internal tick counter and dispatches all registered
 * callbacks.  The port implementation wires this into the hardware vector:
 * @code
 *   void SysTick_Handler(void) { hal_systick_isr_handler(); }
 * @endcode
 *
 * @note    This function executes in interrupt context.
 */
void hal_systick_isr_handler(void);

#endif /* HAL_SYSTICK_H */
