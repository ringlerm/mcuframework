/**
 * @file    target.h
 * @brief   Platform-specific configuration — single point of MCU porting.
 *
 * @details This is the ONLY file that needs to be modified when retargeting
 *          the framework to a new MCU or RTOS.  All library and HAL modules
 *          include this header; no other file contains platform-conditional
 *          code.
 *
 *          ### How to retarget
 *          1. Define exactly ONE @c TARGET_xxx symbol in the section below.
 *          2. Rebuild.  Every module picks up the correct device header,
 *             critical-section implementation, and compiler hints
 *             automatically.
 *
 *          ### Currently supported targets
 *
 *          | Symbol                | Board / MCU                       | Core        |
 *          |-----------------------|-----------------------------------|-------------|
 *          | TARGET_LPC845         | NXP LPC845 (LPC84x)               | Cortex-M0+  |
 *          | TARGET_STM32F103C8T6  | Blue Pill  — STM32F103C8T6        | Cortex-M3   |
 *          | TARGET_STM32F401CCU6  | Black Pill v1 — STM32F401CCU6     | Cortex-M4F  |
 *          | TARGET_STM32F411CEU6  | Black Pill v2 — STM32F411CEU6     | Cortex-M4F  |
 *          | TARGET_AVR328P        | Microchip ATmega328P               | AVR8        |
 *          | TARGET_GENERIC        | No hardware (host tests)           | —           |
 *
 *          ### Adding a new target
 *          1. Add a row to the table above.
 *          2. Add a corresponding @c \#elif block in each guarded section
 *             below (device header, critical sections).
 *          3. Create the port implementation files under
 *             @c ports/<target>/.
 *
 * @author  mcuframework contributors
 * @version 2.0.0
 */

#ifndef TARGET_H
#define TARGET_H

/*===========================================================================*
 * Step 1 — Select exactly ONE target
 * -------------------------------------------------------------------------
 * Uncomment the line that matches your MCU.  Comment out all others.
 *===========================================================================*/

#define TARGET_LPC845
/* #define TARGET_STM32F103C8T6 */   /* Blue Pill                      */
/* #define TARGET_STM32F401CCU6 */   /* Black Pill v1                  */
/* #define TARGET_STM32F411CEU6 */   /* Black Pill v2                  */
/* #define TARGET_AVR328P       */
/* #define TARGET_GENERIC       */

/*===========================================================================*
 * Step 2 — (Automatic) Device header
 * -------------------------------------------------------------------------
 * Each target pulls in its CMSIS or vendor device header.  This gives every
 * module access to the correct peripheral structs, core intrinsics, and the
 * SystemCoreClock variable without needing to know which MCU is active.
 *===========================================================================*/

#if defined(TARGET_LPC845)
    /**
     * @brief   NXP LPC845 CMSIS device header.
     *
     * Provided by the NXP LPC845 device pack (MCUXpresso SDK or standalone
     * CMSIS pack).  Includes core_cm0plus.h, giving access to:
     *   - SysTick_Config(), __disable_irq(), __enable_irq()
     *   - SysTick, NVIC peripheral structs
     *   - SystemCoreClock extern
     */
   #include "LPC845.h"
   #define CLK_HZ 120000000u /* 12 MHz FRO default clock; update if you change the clock setup */

#elif defined(TARGET_STM32F103C8T6)
    /**
     * @brief   ST STM32F1xx family header — Blue Pill (STM32F103C8T6).
     *
     * Cortex-M3, 72 MHz max, 64 kB Flash, 20 kB SRAM.
     * Provided by the STM32CubeF1 CMSIS pack or the STM32F1xx device pack.
     * Includes core_cm3.h, giving access to:
     *   - SysTick_Config(), __disable_irq(), __enable_irq()
     *   - SysTick, NVIC peripheral structs
     *   - SystemCoreClock extern
     */
    #include "stm32f1xx.h"
#elif defined(TARGET_STM32F401CCU6)
    /**
     * @brief   ST STM32F4xx family header — Black Pill v1 (STM32F401CCU6).
     *
     * Cortex-M4F, 84 MHz max, 256 kB Flash, 64 kB SRAM.
     * Provided by the STM32CubeF4 CMSIS pack or the STM32F4xx device pack.
     * Includes core_cm4.h (with FPU support), giving access to:
     *   - SysTick_Config(), __disable_irq(), __enable_irq()
     *   - SysTick, NVIC peripheral structs
     *   - SystemCoreClock extern
     */
    #include "stm32f4xx.h"

#elif defined(TARGET_STM32F411CEU6)
    /**
     * @brief   ST STM32F4xx family header — Black Pill v2 (STM32F411CEU6).
     *
     * Cortex-M4F, 100 MHz max, 512 kB Flash, 128 kB SRAM.
     * Same CMSIS family header as the F401; the device pack selects the
     * correct peripheral map via the compiler's predefined device macro.
     * Includes core_cm4.h (with FPU support).
     */
    #include "stm32f4xx.h"

#elif defined(TARGET_AVR328P)
    /**
     * @brief   avr-libc headers for ATmega328P.
     *
     * avr-libc does not follow the CMSIS convention; critical-section
     * macros use cli()/sei() instead of CMSIS intrinsics (see below).
     */
    #include <avr/io.h>
    #include <avr/interrupt.h>

#elif defined(TARGET_GENERIC)
    /**
     * @brief   Generic / host target — no hardware headers.
     *
     * Used for unit tests compiled on a host machine (Linux, Windows, macOS).
     * Critical sections are no-ops; no peripheral structs are available.
     */
    #include <stdint.h>

#else
    #error "target.h: no TARGET_xxx defined. Edit target.h and select a target."
#endif

/*===========================================================================*
 * Step 3 — (Automatic) Critical-section hooks
 * -------------------------------------------------------------------------
 * CQUEUE_ENTER_CRITICAL() / CQUEUE_EXIT_CRITICAL() protect shared state
 * (queue descriptors, callback tables) against concurrent access.
 *
 * On bare-metal Cortex-M targets the CMSIS intrinsics __disable_irq() /
 * __enable_irq() are the correct implementation; they compile to single
 * CPSID / CPSIE instructions with no overhead.
 *
 * On AVR, cli() / sei() serve the same role.
 *
 * On an RTOS, replace these with the appropriate scheduler-lock or mutex
 * primitives for your target.
 *===========================================================================*/

#if defined(TARGET_LPC845)         || \
    defined(TARGET_STM32F103C8T6)  || \
    defined(TARGET_STM32F401CCU6)  || \
    defined(TARGET_STM32F411CEU6)
    /**
     * @brief   Disable all maskable interrupts (Cortex-M CPSID i).
     *
     * Valid for Cortex-M0+, M3, and M4F cores.  Compiles to a single
     * CPSID instruction with zero overhead.  Paired 1:1 with
     * CQUEUE_EXIT_CRITICAL().
     */
    #define CQUEUE_ENTER_CRITICAL()     __disable_irq()

    /** @brief  Re-enable maskable interrupts (Cortex-M CPSIE i). */
    #define CQUEUE_EXIT_CRITICAL()      __enable_irq()

#elif defined(TARGET_AVR328P)
    /** @brief  Disable interrupts on AVR (CLI instruction). */
    #define CQUEUE_ENTER_CRITICAL()     cli()

    /** @brief  Enable interrupts on AVR (SEI instruction). */
    #define CQUEUE_EXIT_CRITICAL()      sei()

#elif defined(TARGET_GENERIC)
    /** @brief  No-op — single-context host environment. */
    #define CQUEUE_ENTER_CRITICAL()     do {} while (0)

    /** @brief  No-op — single-context host environment. */
    #define CQUEUE_EXIT_CRITICAL()      do {} while (0)

#endif

#endif /* TARGET_H */
