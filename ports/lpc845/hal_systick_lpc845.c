/**
 * @file    hal_systick_lpc845.c
 * @brief   SysTick HAL port — NXP LPC845 (Cortex-M0+).
 *
 * @details Implements the two hardware hooks declared as weak symbols in
 *          hal_systick.h:
 *
 *          | Weak symbol              | This file provides        |
 *          |--------------------------|---------------------------|
 *          | hal_systick_hw_start()   | Strong (overrides weak)   |
 *          | hal_systick_hw_stop()    | Strong (overrides weak)   |
 *
 *          Everything else (tick counter, callback table, delay helper,
 *          hal_systick_isr_handler) is handled once in hal_systick.c and
 *          is not repeated here.
 *
 *          ### Clock source
 *          The LPC845 SysTick is clocked from the processor clock (main
 *          system clock, CLKSOURCE bit = 1 in SYST_CSR).  The CMSIS global
 *          @c SystemCoreClock holds the current frequency; it must be
 *          updated by the application's clock-initialisation code before
 *          calling hal_systick_init() — typically via @c SystemInit() or
 *          @c SystemCoreClockUpdate() from the NXP CMSIS device pack.
 *
 *          On reset the LPC845 FRO runs at 12 MHz, so without any explicit
 *          clock setup @c SystemCoreClock == 12000000 and a 1 ms tick
 *          requires a reload value of 11999.
 *
 *          ### SysTick counter limits (Cortex-M0+)
 *          The reload register is 24 bits wide, giving a maximum value of
 *          0x00FFFFFF (16 777 215).  At 12 MHz this caps the period at
 *          ~1398 ms.  At higher clock rates longer periods are possible.
 *
 *          ### ISR wiring
 *          The SysTick vector must call hal_systick_isr_handler().  Add
 *          the following to your interrupt vector source (or startup file):
 *          @code
 *          void SysTick_Handler(void)
 *          {
 *              hal_systick_isr_handler();
 *          }
 *          @endcode
 *
 *          ### Typical initialisation sequence
 *          @code
 *          // system_LPC845.c / board init already called SystemInit().
 *          SystemCoreClockUpdate();             // sync SystemCoreClock
 *
 *          hal_systick_init(SystemCoreClock, HAL_SYSTICK_DEFAULT_PERIOD_MS);
 *          soft_timers_init(hal_systick_get_tick_hz());
 *          hal_systick_register_callback(soft_timers_tick);
 *          @endcode
 *
 * @note    This port uses the CMSIS helper @c SysTick_Config() from
 *          @c core_cm0plus.h, which sets CLKSOURCE = processor clock,
 *          enables the SysTick interrupt, and assigns it the lowest
 *          available NVIC priority.
 *
 * @author  mcuframework contributors
 * @date    2025
 */

#include "hal/hal_systick.h"

/*
 * target.h is the single porting point for the framework.
 * When TARGET_LPC845 is defined there, it pulls in "LPC845.h", which
 * provides core_cm0plus.h (SysTick_Config, SysTick_LOAD_RELOAD_Msk,
 * __disable_irq/__enable_irq) and the SystemCoreClock extern.
 * No direct device header is included here; target.h is the only
 * file that names a specific MCU.
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
 * Port hook implementations  (override the weak stubs in hal_systick.c)
 *===========================================================================*/

/**
 * @brief   Configure and start the SysTick peripheral on the LPC845.
 *
 * Computes the reload value as:
 * @code
 *   reload = (cpu_hz / (1000 / period_ms)) - 1
 *          = (cpu_hz * period_ms / 1000)   - 1
 * @endcode
 *
 * Uses the CMSIS @c SysTick_Config() helper, which:
 *   - Writes the reload register (SYST_RVR).
 *   - Clears the current-value register (SYST_CVR).
 *   - Sets CLKSOURCE = processor clock, enables IRQ and counter (SYST_CSR).
 *   - Sets SysTick interrupt to the lowest NVIC priority.
 *
 * @param[in]   cpu_hz      Core clock frequency in Hz.
 * @param[in]   period_ms   Desired tick period in milliseconds.
 *
 * @return  @c HAL_SYSTICK_OK on success.
 * @return  @c HAL_SYSTICK_ERR_HW_FAULT if the computed reload value exceeds
 *          the 24-bit counter maximum (period too long for the given clock).
 */
hal_systick_status_t hal_systick_hw_start(uint32_t cpu_hz, uint32_t period_ms)
{
    /*
     * Reload = ticks per period - 1.
     * Use 64-bit intermediate to avoid overflow when cpu_hz is large.
     */
    uint64_t reload_64 = ((uint64_t)cpu_hz * (uint64_t)period_ms / 1000u) - 1u;

    if (reload_64 > (uint64_t)SYSTICK_MAX_RELOAD)
    {
        /* Period is too long for the current clock frequency. */
        return HAL_SYSTICK_ERR_HW_FAULT;
    }

    uint32_t reload = (uint32_t)reload_64;

    /*
     * SysTick_Config() returns 0 on success, 1 if the reload value is
     * out of range.  The range check above makes the failure branch
     * unreachable, but guard it anyway for defensive programming.
     */
    if (SysTick_Config(reload + 1u) != 0u)
    {
        return HAL_SYSTICK_ERR_HW_FAULT;
    }

    return HAL_SYSTICK_OK;
}

/**
 * @brief   Disable the SysTick counter and its interrupt on the LPC845.
 *
 * Clears ENABLE and TICKINT in SYST_CSR.  CLKSOURCE is left unchanged.
 * The reload and current-value registers are not touched.
 */
void hal_systick_hw_stop(void)
{
    SysTick->CTRL &= ~(SysTick_CTRL_ENABLE_Msk | SysTick_CTRL_TICKINT_Msk);
}

/*===========================================================================*
 * ISR vector entry point
 * -------------------------------------------------------------------------
 * Place this handler in your startup file or interrupt vector table.
 * It is defined here (weakly) so the linker finds it automatically when
 * this translation unit is included; the application may override it with
 * a strong definition if additional ISR work is needed.
 *===========================================================================*/

/**
 * @brief   SysTick interrupt vector for the LPC845.
 *
 * Delegates entirely to hal_systick_isr_handler(), which increments the
 * tick counter and dispatches all registered callbacks.
 *
 * @note    Declared @c __attribute__((weak)) so the application can override
 *          it with a strong definition when extra work is needed in the ISR.
 */
__attribute__((weak))
void SysTick_Handler(void)
{
    hal_systick_isr_handler();
}
