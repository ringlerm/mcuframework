/**
 * @file    soft_timers.c
 * @brief   Software timer library — implementation.
 *
 * See soft_timers.h for the public interface and usage notes.
 *
 * ### Design notes
 * - All storage is statically allocated; no heap usage.
 * - soft_timers_tick() is ISR-safe by design: it only decrements counters and
 *   sets a boolean flag, avoiding any complex logic in interrupt context.
 * - soft_timers_process() performs callback dispatch from non-ISR context,
 *   eliminating the need for critical-section guards on most architectures
 *   where reading/writing aligned 32-bit words is atomic.  On architectures
 *   where this does not hold, the integrator should wrap the tick
 *   decrement / expired-flag read with appropriate memory barriers or
 *   critical sections.
 *
 * @author  mcuframework contributors
 * @date    2025
 */

#include "soft_timers.h"

/*===========================================================================*
 * Private types
 *===========================================================================*/

/**
 * @brief   Internal representation of a single software timer.
 */
typedef struct
{
    uint32_t              ticks;      /**< Remaining ticks until expiry.      */
    bool                  active;     /**< True while the timer is counting.  */
    bool                  expired;    /**< Set by ISR; cleared by process().  */
    soft_timer_callback_t callback;   /**< Optional expiry callback.          */
} soft_timer_t;

/*===========================================================================*
 * Private variables
 *===========================================================================*/

/** @brief Pool of software timers (statically allocated). */
static soft_timer_t s_timers[SOFT_TIMERS_MAX_TIMERS];

/** @brief Tick frequency stored at initialisation (Hz). */
static uint32_t s_tick_hz;

/*===========================================================================*
 * Private helper — bounds check
 *===========================================================================*/

/**
 * @brief   Return true if @p id is a valid timer index.
 * @param[in]   id  Timer index to validate.
 * @return  @c true if valid, @c false otherwise.
 */
static bool is_valid_id(soft_timer_id_t id)
{
    return (id < (soft_timer_id_t)SOFT_TIMERS_MAX_TIMERS);
}

/*===========================================================================*
 * Public function implementations
 *===========================================================================*/

void soft_timers_init(uint32_t tick_hz)
{
    s_tick_hz = tick_hz;

    for (uint8_t i = 0u; i < (uint8_t)SOFT_TIMERS_MAX_TIMERS; i++)
    {
        s_timers[i].ticks    = 0u;
        s_timers[i].active   = false;
        s_timers[i].expired  = false;
        s_timers[i].callback = NULL;
    }
}

/* -------------------------------------------------------------------------- */

void soft_timer_start(soft_timer_id_t       id,
                      uint32_t              duration,
                      soft_timer_callback_t callback)
{
    if (!is_valid_id(id))
    {
        return;
    }

    s_timers[id].ticks    = duration;
    s_timers[id].expired  = false;
    s_timers[id].callback = callback;
    s_timers[id].active   = true;
}

/* -------------------------------------------------------------------------- */

void soft_timer_stop(soft_timer_id_t id)
{
    if (!is_valid_id(id))
    {
        return;
    }

    s_timers[id].ticks   = 0u;
    s_timers[id].active  = false;
    s_timers[id].expired = false;
}

/* -------------------------------------------------------------------------- */

bool soft_timer_is_running(soft_timer_id_t id)
{
    if (!is_valid_id(id))
    {
        return false;
    }

    return (s_timers[id].active && !s_timers[id].expired);
}

/* -------------------------------------------------------------------------- */

uint32_t soft_timers_ms_to_ticks(uint32_t ms)
{
    return (ms * (s_tick_hz / 1000u));
}

/* -------------------------------------------------------------------------- */

uint32_t soft_timers_us_to_ticks(uint32_t us)
{
    return (us * (s_tick_hz / 1000000u));
}

/* -------------------------------------------------------------------------- */

/**
 * @brief   Decrement all active timers by one tick.
 *
 * Called from the periodic interrupt (e.g. SysTick).  Kept intentionally
 * minimal to reduce ISR latency.
 */
void soft_timers_tick(void)
{
    for (uint8_t i = 0u; i < (uint8_t)SOFT_TIMERS_MAX_TIMERS; i++)
    {
        if (s_timers[i].active && (s_timers[i].ticks > 0u))
        {
            s_timers[i].ticks--;

            if (s_timers[i].ticks == 0u)
            {
                s_timers[i].active  = false;
                s_timers[i].expired = true;
            }
        }
    }
}

/* -------------------------------------------------------------------------- */

/**
 * @brief   Dispatch callbacks for all expired timers.
 *
 * Clears each expired flag before invoking the callback so that a callback
 * may safely restart its own timer without the flag being overwritten.
 */
void soft_timers_process(void)
{
    for (uint8_t i = 0u; i < (uint8_t)SOFT_TIMERS_MAX_TIMERS; i++)
    {
        if (s_timers[i].expired)
        {
            s_timers[i].expired = false;

            if (s_timers[i].callback != NULL)
            {
                s_timers[i].callback();
            }
        }
    }
}
