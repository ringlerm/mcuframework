/**
 * @file        target.h
 * @brief       Platform-specific configuration — single point of MCU porting.
 *
 * @details     Edit ONLY this file when retargeting the framework to a new
 *              MCU or RTOS. All library modules include this header; no other
 *              file should contain platform-conditional code.
 *
 *              Mandatory definitions
 *              ---------------------
 *              CQUEUE_ENTER_CRITICAL()
 *                  Suspend any concurrent access to shared queue state.
 *                  On bare-metal systems this typically disables interrupts.
 *                  On RTOS systems this typically acquires a mutex or enters
 *                  a scheduler-lock region.
 *
 *              CQUEUE_EXIT_CRITICAL()
 *                  Re-enable concurrent access. Must be paired 1:1 with
 *                  every CQUEUE_ENTER_CRITICAL() call.
 *
 *              Porting examples
 *              ----------------
 *
 *              Bare-metal Cortex-M (CMSIS):
 *              @code
 *              #include "cmsis_compiler.h"
 *              #define CQUEUE_ENTER_CRITICAL()  __disable_irq()
 *              #define CQUEUE_EXIT_CRITICAL()   __enable_irq()
 *              @endcode
 *
 *              Bare-metal AVR (avr-libc):
 *              @code
 *              #include <avr/interrupt.h>
 *              #define CQUEUE_ENTER_CRITICAL()  cli()
 *              #define CQUEUE_EXIT_CRITICAL()   sei()
 *              @endcode
 *
 *              FreeRTOS (task context only):
 *              @code
 *              #include "FreeRTOS.h"
 *              #include "task.h"
 *              #define CQUEUE_ENTER_CRITICAL()  taskENTER_CRITICAL()
 *              #define CQUEUE_EXIT_CRITICAL()   taskEXIT_CRITICAL()
 *              @endcode
 *
 *              Single-context (no concurrent access, zero overhead):
 *              @code
 *              #define CQUEUE_ENTER_CRITICAL()  do {} while (0)
 *              #define CQUEUE_EXIT_CRITICAL()   do {} while (0)
 *              @endcode
 *
 * @note        The macros below default to no-ops. Replace them with the
 *              correct implementation for your platform before building.
 *
 * @author      mcuframework contributors
 * @version     1.0.0
 */

#ifndef TARGET_H
#define TARGET_H

/*===========================================================================*
 * Critical-section hooks
 * -------------------------------------------------------------------------
 * Replace these no-ops with platform-appropriate implementations.
 *===========================================================================*/

#ifndef CQUEUE_ENTER_CRITICAL
/** @brief  Disable concurrent access before a queue operation. */
#define CQUEUE_ENTER_CRITICAL()     do {} while (0)
#endif

#ifndef CQUEUE_EXIT_CRITICAL
/** @brief  Re-enable concurrent access after a queue operation. */
#define CQUEUE_EXIT_CRITICAL()      do {} while (0)
#endif

#endif /* TARGET_H */
