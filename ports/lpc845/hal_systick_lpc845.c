/**
 * @file    hal_systick_lpc845.c
 * @brief   SysTick HAL Port and Core Logic — NXP LPC845 (Cortex-M0+).
 *
 * @details Implements the full SysTick API declared in hal_systick.h. This file
 * encapsulates the hardware register manipulation, internal tick accounting,
 * subscribers management (callbacks), and the ISR vector execution.
 *
 * ### Clock Source and Range Extension
 * The driver interacts directly with the standard SysTick registers (SYST_CSR, 
 * SYST_RVR, SYST_CVR). It supports automatic clock division to achieve longer 
 * tick periods:
 * - **Full Clock:** Processor clock (main system clock, CLKSOURCE bit = 1).
 * - **Half Clock:** External clock source reference (system clock / 2, CLKSOURCE bit = 0).
 *
 * On reset the LPC845 FRO runs at 12 MHz, so without an explicit clock setup, 
 * a 1 ms tick requires a reload value of 11999 under full clock.
 *
 * ### SysTick counter limits (Cortex-M0+)
 * The reload register is 24 bits wide, giving a maximum value of 0x00FFFFFF.
 * If the requested period cannot be fulfilled with the main clock, the driver 
 * dynamically switches to the half-clock configuration to extend the maximum range.
 *
 * ### ISR wiring
 * The standard CMSIS interrupt vector @c SysTick_Handler() is implemented here,
 * meaning it intercepts the timer event directly to update the global count
 * and safely dispatch subscribers.
 *
 * @author  mcuframework contributors
 * @date    2025
 */

#include "hal/hal_systick.h"

/*
 * target.h is the single porting point for the framework.
 * When TARGET_LPC845 is defined there, it pulls in "LPC845.h", which
 * provides core_cm0plus.h (SysTick_LOAD_RELOAD_Msk, __disable_irq/__enable_irq)
 * and the SystemCoreClock extern.
 */
#include "target.h"

#ifndef TARGET_LPC845
#error "hal_systick_lpc845.c must only be compiled when TARGET_LPC845 is defined in target.h"
#endif

/*===========================================================================*
 * Private constants
 *===========================================================================*/

/**
 * @brief   Maximum reload value for the 24-bit Cortex-M0+ SysTick counter.
 */
#define SYSTICK_MAX_RELOAD  (SysTick_LOAD_RELOAD_Msk)   /* 0x00FFFFFF */

/*===========================================================================*
 * Private variables
 *===========================================================================*/

/** @brief Array of subscriber function pointers. */
static hal_systick_callback_t callback_table[HAL_SYSTICK_MAX_CALLBACKS] = { NULL };

/** @brief Active frequency derived from the running register metrics. */
static uint32_t tick_hz = 0;

/** @brief Monotonically increasing tick counter updated inside the ISR. */
static uint32_t tick_count = 0;

/*===========================================================================*
 * Public function implementations
 *===========================================================================*/

/**
 * @brief   Initialise and start the SysTick peripheral.
 *
 * @details Computes the required reload value based on CPU clock and target period.
 * If the value fits within the 24-bit SysTick limits, it configures the hardware
 * using the core processor clock. If it overflows, it automatically attempts
 * to scale down the timing logic using the internal system half-clock divider.
 *
 * @param[in]   cpu_hz      Core clock frequency in Hz (typically SystemCoreClock).
 * @param[in]   period_ms   Desired tick rate period expressed in milliseconds.
 *
 * @return  @c HAL_SYSTICK_OK on configuration success.
 * @return  @c HAL_SYSTICK_ERR_HW_FAULT if the requested period remains too long 
 * to be captured even after applying the half-clock hardware option.
 */
hal_systick_status_t hal_systick_init(uint32_t cpu_hz, uint32_t period_ms)
{
    /*
     * Reload = ticks per period - 1.
     * Use 64-bit intermediate to avoid overflow when cpu_hz is large.
     */
    uint64_t reload_64 = ((uint64_t)cpu_hz * (uint64_t)period_ms / 1000u) - 1u;
    bool full_clk = 1; /* SysTick clocked from processor clock if 1, half clock if 0 */

    if (reload_64 > (uint64_t)SYSTICK_MAX_RELOAD)
    {
        /* Period is too long for the current clock frequency, try half clock */
        full_clk = 0;
        reload_64 = ((uint64_t)cpu_hz * (uint64_t)period_ms / 1000u) / 2u - 1u;
        if (reload_64 > (uint64_t)SYSTICK_MAX_RELOAD)
        {
            return HAL_SYSTICK_ERR_HW_FAULT;
        }
    }

    uint32_t reload = (uint32_t)reload_64;

    SysTick->LOAD = reload;    /* Set reload value. */
    SysTick->VAL = 0;           /* Clear current value. */
    SysTick->CTRL = (full_clk<<2) | (1<<1) | (1<<0);  /* CLKSOURCE, enable IRQ and counter. */
    tick_hz = full_clk ? (cpu_hz / (reload + 1u)) : (cpu_hz / 2u / (reload + 1u));

    return HAL_SYSTICK_OK;
}

/**
 * @brief   Stop the SysTick peripheral and disable its interrupt.
 *
 * @details Turns off the ENABLE and TICKINT control flags in the register block. 
 * The active configurations, current counter flags, and registered 
 * callbacks are preserved.
 */
void hal_systick_deinit(void)
{
    SysTick->CTRL &= ~((1<<0) | (1<<1));
    tick_hz = 0; /* Clear the tick frequency since the timer is stopped */
}

/*===========================================================================*
 * ISR vector entry point
 *===========================================================================*/

/**
 * @brief   SysTick hardware interrupt vector handler.
 *
 * @details Increments the internal runtime tracking tick count on each execution.
 * Then, it loops sequentially through the subscriber matrix to invoke 
 * any non-NULL registered callback functions in an isolated ISR context.
 */
void SysTick_Handler(void)
{
    tick_count++; /* Increment the global tick counter */
    
    /* Dispatch all registered callback functions sequentially */
    for (uint32_t i = 0; i < HAL_SYSTICK_MAX_CALLBACKS; i++)
    {
        /* Store in local pointer for safety if an interruption occurs (though we are in ISR context) */
        hal_systick_callback_t cb = callback_table[i];
        if (cb != NULL)
        {
            cb(); /* Execute the subscribed callback */
        }
    }
}

/**
 * @brief   Blocking delay using the SysTick counter.
 *
 * @details Computes the total required ticks needed for the timeout based on the 
 * configured driver frequency, then polls the counter status actively until 
 * the window expires.
 *
 * @param[in]   ms  Number of milliseconds to wait.
 */
void hal_systick_delay_ms(uint32_t ms)
{
    uint32_t start_tick = hal_systick_get_tick_count();
    uint32_t delay_ticks = (ms * hal_systick_get_tick_hz()) / 1000u;

    while ((hal_systick_get_tick_count() - start_tick) < delay_ticks);
}

/**
 * @brief   Return the configured tick frequency in Hz.
 *
 * @return  Calculated operational frequency value.
 */
uint32_t hal_systick_get_tick_hz(void)
{
    return tick_hz;
}

/**
 * @brief   Return the running tick counter value.
 *
 * @return  Current global operational tick count register state.
 */
uint32_t hal_systick_get_tick_count(void)
{
    return tick_count;
}

/**
 * @brief   Register a callback to be invoked on every SysTick interrupt.
 *
 * @details Checks for argument validation and verifies that the subscriber does 
 * not already exist within the array tracker. To prevent race conditions 
 * with the asynchronous execution of the ISR, the lookup and assignment 
 * sequence is protected via global interrupt masking locks.
 *
 * @param[in]   callback    Non-NULL pointer to the function matching the signature.
 *
 * @return  @c HAL_SYSTICK_OK on enrollment success or if already tracked.
 * @return  @c HAL_SYSTICK_ERR_INVALID_ARG if the passed pointer evaluates to NULL.
 * @return  @c HAL_SYSTICK_ERR_FULL if no available storage indexing slots remain.
 */
hal_systick_status_t hal_systick_register_callback(hal_systick_callback_t callback)
{
    /* 1. Validate argument */
    if (callback == NULL)
    {
        return HAL_SYSTICK_ERR_INVALID_ARG;
    }

    /* 2. Protect critical section to prevent race conditions with the ISR */
    __disable_irq();

    /* 3. Check if already registered (avoid duplicates) or find a free slot */
    int32_t free_slot = -1;
    for (uint32_t i = 0; i < HAL_SYSTICK_MAX_CALLBACKS; i++)
    {
        if (callback_table[i] == callback)
        {
            /* Already registered, exit with success (idempotency) */
            __enable_irq();
            return HAL_SYSTICK_OK;
        }
        
        if ((callback_table[i] == NULL) && (free_slot == -1))
        {
            free_slot = (int32_t)i; /* Save the first free slot found */
        }
    }

    /* 4. If no free slots are available, the table is full */
    if (free_slot == -1)
    {
        __enable_irq();
        return HAL_SYSTICK_ERR_FULL;
    }

    /* 5. Register the callback in the free slot */
    callback_table[free_slot] = callback;

    /* 6. Exit critical section */
    __enable_irq();

    return HAL_SYSTICK_OK;
}

/**
 * @brief   Unregister a previously registered callback.
 *
 * @details Scans the internal storage table to locate the specific matching pointer.
 * If found, clears its slot safely under an internal atomic lock mask wrapper.
 *
 * @param[in]   callback    Active tracked pointer previously submitted.
 *
 * @return  @c HAL_SYSTICK_OK on removal execution.
 * @return  @c HAL_SYSTICK_ERR_INVALID_ARG if the target evaluates to NULL.
 * @return  @c HAL_SYSTICK_ERR_NOT_FOUND if the function pointer is missing.
 */
hal_systick_status_t hal_systick_unregister_callback(hal_systick_callback_t callback)
{
    /* 1. Validate argument */
    if (callback == NULL)
    {
        return HAL_SYSTICK_ERR_INVALID_ARG;
    }

    /* 2. Enter critical section */
    __disable_irq();

    /* 3. Search for the callback to remove it */
    for (uint32_t i = 0; i < HAL_SYSTICK_MAX_CALLBACKS; i++)
    {
        if (callback_table[i] == callback)
        {
            /* Found: clear the slot by setting it to NULL */
            callback_table[i] = NULL;
            
            __enable_irq();
            return HAL_SYSTICK_OK;
        }
    }

    /* 4. If the loop completes and the callback was not found */
    __enable_irq();
    return HAL_SYSTICK_ERR_NOT_FOUND;
}