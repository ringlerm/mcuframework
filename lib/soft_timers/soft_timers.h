/**
 * @file    soft_timers.h
 * @brief   Software timer library based on a periodic tick source.
 *
 * Provides a pool of statically-allocated, non-blocking software timers driven
 * by a caller-supplied tick (e.g. SysTick ISR).  No dynamic memory is used.
 *
 * ### Typical usage
 * @code
 *   // 1. Initialise once, passing the tick frequency.
 *   soft_timers_init(1000u);          // 1 kHz tick → 1 ms resolution
 *
 *   // 2. Start a timer (timer 0, 500 ms, with a callback).
 *   soft_timer_start(0u, soft_timers_ms_to_ticks(500u), my_callback);
 *
 *   // 3. In the SysTick ISR:
 *   soft_timers_tick();
 *
 *   // 4. In the main loop:
 *   soft_timers_process();
 * @endcode
 *
 * @note    Timer callbacks are executed from the context in which
 *          soft_timers_process() is called, NOT from interrupt context.
 *
 * @author  mcuframework contributors
 * @date    2025
 */

#ifndef SOFT_TIMERS_H
#define SOFT_TIMERS_H

#include <stdint.h>
#include <stdbool.h>

/*===========================================================================*
 * Public constants
 *===========================================================================*/

/** @brief Maximum number of concurrent software timers. */
#define SOFT_TIMERS_MAX_TIMERS  (10u)

/*===========================================================================*
 * Public types
 *===========================================================================*/

/**
 * @brief   Index used to identify a software timer.
 *
 * Valid range: 0 … (SOFT_TIMERS_MAX_TIMERS - 1).
 */
typedef uint8_t soft_timer_id_t;

/**
 * @brief   Prototype for a timer-expiry callback.
 *
 * The function must be short and non-blocking; it executes in the context
 * of soft_timers_process().
 */
typedef void (*soft_timer_callback_t)(void);

/*===========================================================================*
 * Public function prototypes
 *===========================================================================*/

/**
 * @brief   Initialise the software-timer subsystem.
 *
 * Must be called once before any other soft_timer_* function.
 * Clears every timer slot and stores the tick frequency used for
 * time-unit conversions.
 *
 * @param[in]   tick_hz     Frequency (Hz) at which soft_timers_tick() will
 *                          be called.  Must be greater than zero.
 */
void soft_timers_init(uint32_t tick_hz);

/**
 * @brief   Start (or restart) a software timer.
 *
 * If the timer identified by @p id is already running it is restarted with
 * the new parameters.
 *
 * @param[in]   id          Timer index (0 … SOFT_TIMERS_MAX_TIMERS - 1).
 * @param[in]   duration    Duration expressed in ticks.  A value of 0 is
 *                          treated as an immediate expiry on the next call
 *                          to soft_timers_process().
 * @param[in]   callback    Function to invoke on expiry, or NULL.
 */
void soft_timer_start(soft_timer_id_t id,
                      uint32_t        duration,
                      soft_timer_callback_t callback);

/**
 * @brief   Stop a running software timer.
 *
 * The timer is halted and its expired flag is cleared.  If the timer is
 * already stopped this function has no effect.
 *
 * @param[in]   id  Timer index (0 … SOFT_TIMERS_MAX_TIMERS - 1).
 */
void soft_timer_stop(soft_timer_id_t id);

/**
 * @brief   Return whether a timer is currently active (counting down).
 *
 * @param[in]   id  Timer index (0 … SOFT_TIMERS_MAX_TIMERS - 1).
 * @return      @c true  if the timer is running (ticks > 0 and not expired),
 *              @c false otherwise.
 */
bool soft_timer_is_running(soft_timer_id_t id);

/**
 * @brief   Convert milliseconds to ticks.
 *
 * The result depends on the tick frequency supplied to soft_timers_init().
 *
 * @param[in]   ms  Time in milliseconds.
 * @return      Equivalent number of ticks (rounded down).
 */
uint32_t soft_timers_ms_to_ticks(uint32_t ms);

/**
 * @brief   Convert microseconds to ticks.
 *
 * @param[in]   us  Time in microseconds.
 * @return      Equivalent number of ticks (rounded down).
 */
uint32_t soft_timers_us_to_ticks(uint32_t us);

/**
 * @brief   Decrement all active timers by one tick.
 *
 * @par ISR usage
 * This function is designed to be called from a periodic interrupt handler
 * (e.g. SysTick) at the frequency given to soft_timers_init().  It is kept
 * minimal to reduce interrupt latency.
 *
 * @warning Must NOT be called from main-loop context if the tick source is
 *          an ISR; doing so introduces a race condition.
 */
void soft_timers_tick(void);

/**
 * @brief   Dispatch callbacks for all expired timers.
 *
 * Scans the timer pool and, for every timer that has expired, clears the
 * expired flag and invokes its callback (if one was registered).
 *
 * Must be called regularly from the main loop or a suitable task.  It must
 * NOT be called from interrupt context.
 */
void soft_timers_process(void);

#endif /* SOFT_TIMERS_H */
