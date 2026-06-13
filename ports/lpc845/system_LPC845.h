#ifndef __SYSTEM_LPC845_H__
#define __SYSTEM_LPC845_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Variables globales obligatorias de CMSIS para la frecuencia del sistema */
extern uint32_t SystemCoreClock;     /*!< System Clock Frequency (Core Clock) */
extern uint32_t BootReg;             /*!< Boot Register Configuration */

/**
 * @brief Inicializa el sistema (relojes, watchdog si aplica, etc.)
 */
void SystemInit(void);

/**
 * @brief Actualiza la variable global SystemCoreClock leyendo los registros de hardware
 */
void SystemCoreClockUpdate(void);

#ifdef __cplusplus
}
#endif

#endif  /* __SYSTEM_LPC845_H__ */
