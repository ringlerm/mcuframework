/**
 * @file    digital_filter.h
 * @brief   Punto de entrada único de la librería de filtros digitales.
 *
 * @details Incluir únicamente este archivo para acceder a todos los filtros
 *          de la librería. No es necesario incluir los headers individuales.
 *
 *          ### Filtros disponibles
 *
 *          #### Señales continuas (int16_t, int32_t, uint16_t, uint32_t)
 *
 *          | Filtro              | Header                      | Sufijos          |
 *          |---------------------|-----------------------------|------------------|
 *          | EMA (IIR 1er orden) | digital_filter_ema.h        | i16, i32, u16, u32 |
 *          | SMA (promedio N)    | digital_filter_sma.h        | i16, i32, u16, u32 |
 *          | Mediana móvil       | digital_filter_median.h     | i16, i32, u16, u32 |
 *          | Slew rate limiter   | digital_filter_slew.h       | i16, i32, u16, u32 |
 *          | Detector de pico    | digital_filter_peak.h       | i16, i32, u16, u32 |
 *          | Histéresis          | digital_filter_hyst.h       | i16, i32, u16, u32 |
 *
 *          #### Señales binarias (uint8_t → digital_filter_binary_state_t)
 *
 *          | Filtro              | Tipo descriptor   | Función update          |
 *          |---------------------|-------------------|-------------------------|
 *          | Shift register 8b   | df_shift_t        | df_shift_update()       |
 *          | Integrador Ganssle  | df_integrator_t   | df_integrator_update()  |
 *          | Monoestable         | df_monostable_t   | df_monostable_update()  |
 *
 *          ### Convenios de nomenclatura
 *
 *          Todas las funciones siguen el patrón:
 *          @code
 *          <filtro>_filter_<sufijo>_<accion>()
 *          @endcode
 *
 *          Ejemplo: @c ema_filter_i16_update(), @c sma_filter_u32_reset().
 *
 *          Los filtros binarios no usan sufijo de tipo (operan siempre sobre
 *          uint8_t) y usan el prefijo corto @c df_:
 *          @code
 *          df_shift_update(), df_integrator_init(), df_monostable_get()
 *          @endcode
 *
 *          ### Punto fijo — resumen rápido
 *
 *          Los coeficientes (alpha del EMA, etc.) se expresan en Q0.16:
 *
 *          @code
 *          // Sin float (recomendado para M0+):
 *          uint32_t alpha = DIGITAL_FILTER_FRAC_TO_Q16(1, 10); // 0.1
 *          uint32_t alpha = DIGITAL_FILTER_FRAC_TO_Q16(1,  4); // 0.25
 *
 *          // Con float (solo si hay FPU disponible):
 *          uint32_t alpha = DIGITAL_FILTER_FLOAT_TO_Q16(0.1f);
 *          @endcode
 *
 *          ### Instancias personalizadas
 *
 *          Para usar un filtro con un tipo no incluido en las instancias
 *          predefinidas, llamar a la macro DEFINE_XXX_FILTER directamente:
 *
 *          @code
 *          // Filtro EMA para int8_t con acumulador int32_t
 *          DEFINE_EMA_FILTER(int8_t, int32_t, i8)
 *
 *          // Usar:
 *          ema_filter_i8_t my_filter;
 *          ema_filter_i8_init(&my_filter, DIGITAL_FILTER_FRAC_TO_Q16(1, 8), 0);
 *          @endcode
 *
 * @note    No requiere FPU. No usa memoria dinámica. Compatible con C99.
 *          Todos los filtros son thread-unsafe por diseño. Proteger el acceso
 *          al descriptor con CQUEUE_ENTER_CRITICAL() / CQUEUE_EXIT_CRITICAL()
 *          de target.h si se comparte entre ISR y tarea.
 *
 * @author  mcuframework contributors
 * @version 1.0.0
 */

#ifndef DIGITAL_FILTER_H
#define DIGITAL_FILTER_H

#include "digital_filter_common.h"
#include "digital_filter_ema.h"
#include "digital_filter_sma.h"
#include "digital_filter_median.h"
#include "digital_filter_slew.h"
#include "digital_filter_peak.h"
#include "digital_filter_hyst.h"
#include "digital_filter_binary.h"

#endif /* DIGITAL_FILTER_H */
