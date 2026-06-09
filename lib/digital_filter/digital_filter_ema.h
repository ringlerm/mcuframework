/**
 * @file    digital_filter_ema.h
 * @brief   Filtro de promedio móvil exponencial (EMA) genérico con punto fijo.
 *
 * @details Implementa el filtro IIR de primer orden:
 *
 *          @code
 *          y[n] = alpha * x[n] + (1 - alpha) * y[n-1]
 *          @endcode
 *
 *          El coeficiente @c alpha se representa en punto fijo Q0.16
 *          (uint32_t, escala 65536) para evitar el uso de FPU en Cortex-M0+.
 *          La multiplicación intermedia usa int64_t/uint64_t para prevenir
 *          desbordamiento.
 *
 *          ### Elección de alpha
 *
 *          Un alpha alto → respuesta rápida, poco suavizado.
 *          Un alpha bajo → respuesta lenta, mucho suavizado.
 *
 *          | alpha real | alpha_Q16 | ~Tiempo establecimiento (5τ) a 5 ms/muestra |
 *          |------------|-----------|---------------------------------------------|
 *          | 0.5        | 32768     | ~50 ms                                      |
 *          | 0.25       | 16384     | ~100 ms                                     |
 *          | 0.1        | 6554      | ~250 ms                                     |
 *          | 0.05       | 3277      | ~500 ms                                     |
 *
 *          Para calcular alpha_Q16 sin float:
 *          @code
 *          DIGITAL_FILTER_FRAC_TO_Q16(1, 10)  // alpha ≈ 0.1
 *          DIGITAL_FILTER_FRAC_TO_Q16(1,  4)  // alpha = 0.25
 *          @endcode
 *
 *          ### Instancias predefinidas
 *
 *          | Sufijo | Tipo de muestra | Acumulador interno |
 *          |--------|-----------------|--------------------|
 *          | i16    | int16_t         | int64_t            |
 *          | i32    | int32_t         | int64_t            |
 *          | u16    | uint16_t        | uint64_t           |
 *          | u32    | uint32_t        | uint64_t           |
 *
 *          ### Ejemplo de uso
 *
 *          @code
 *          #include "digital_filter_ema.h"
 *
 *          ema_filter_i16_t adc_filter;
 *          ema_filter_i16_init(&adc_filter,
 *                              DIGITAL_FILTER_FRAC_TO_Q16(1, 10),
 *                              0);
 *
 *          // En cada tick periódico:
 *          int16_t filtered = ema_filter_i16_update(&adc_filter, adc_read());
 *          @endcode
 *
 * @note    No requiere FPU. No usa memoria dinámica. Compatible con C99.
 *          Thread-safety: ninguna. Proteger con secciones críticas si se
 *          comparte entre ISR y tarea.
 *
 * @author  mcuframework contributors
 * @version 1.0.0
 */

#ifndef DIGITAL_FILTER_EMA_H
#define DIGITAL_FILTER_EMA_H

#include "digital_filter_common.h"

/*===========================================================================*
 * Macro de generación de instancias EMA
 *===========================================================================*/

/**
 * @brief   Genera el descriptor y las funciones del filtro EMA para un
 *          tipo de dato y acumulador específicos.
 *
 * @param   SAMPLE_TYPE   Tipo de las muestras de entrada/salida (ej: int16_t).
 * @param   ACCUM_TYPE    Tipo del acumulador interno, debe ser más ancho que
 *                        SAMPLE_TYPE (ej: int64_t para int16_t/int32_t).
 * @param   SUFFIX        Sufijo de nombre (ej: i16, u32).
 *
 * @details Genera:
 *          - @c ema_filter_SUFFIX_t        : descriptor del filtro.
 *          - @c ema_filter_SUFFIX_init()   : inicialización con valor inicial.
 *          - @c ema_filter_SUFFIX_reset()  : recarga del acumulador en runtime.
 *          - @c ema_filter_SUFFIX_update() : procesa una muestra.
 *          - @c ema_filter_SUFFIX_get()    : lee la salida sin nueva muestra.
 */
#define DEFINE_EMA_FILTER(SAMPLE_TYPE, ACCUM_TYPE, SUFFIX)                    \
                                                                               \
    /** @brief Descriptor del filtro EMA para el tipo SUFFIX. */               \
    typedef struct                                                             \
    {                                                                          \
        ACCUM_TYPE  s_accum;    /**< Acumulador interno en Q0.16 escalado.  */ \
        uint32_t    s_alpha_q16;/**< Coeficiente alpha en Q0.16 [1,65536].  */ \
    } ema_filter_##SUFFIX##_t;                                                 \
                                                                               \
    /**                                                                        \
     * @brief   Inicializa el filtro EMA con un valor de arranque.             \
     * @param[out] filter     Puntero al descriptor. No debe ser NULL.         \
     * @param[in]  alpha_q16  Coeficiente alpha en Q0.16.                      \
     *                        Usar DIGITAL_FILTER_FRAC_TO_Q16(num, den).       \
     *                        Rango válido: [1, 65536].                        \
     * @param[in]  initial    Valor inicial del acumulador. Permite evitar el  \
     *                        transitorio de arranque si se conoce el reposo.  \
     */                                                                        \
    static inline void ema_filter_##SUFFIX##_init(                            \
        ema_filter_##SUFFIX##_t * const filter,                               \
        uint32_t                        alpha_q16,                            \
        SAMPLE_TYPE                     initial)                              \
    {                                                                          \
        filter->s_alpha_q16 = DIGITAL_FILTER_CLAMP(alpha_q16, 1u, 65536u);   \
        filter->s_accum     = (ACCUM_TYPE)initial                             \
                              * (ACCUM_TYPE)DIGITAL_FILTER_Q16_SCALE;         \
    }                                                                          \
                                                                               \
    /**                                                                        \
     * @brief   Recarga el acumulador con un nuevo valor sin cambiar alpha.    \
     * @param[in,out] filter   Puntero al descriptor. No debe ser NULL.        \
     * @param[in]     value    Nuevo valor del acumulador.                     \
     */                                                                        \
    static inline void ema_filter_##SUFFIX##_reset(                           \
        ema_filter_##SUFFIX##_t * const filter,                               \
        SAMPLE_TYPE                     value)                                \
    {                                                                          \
        filter->s_accum = (ACCUM_TYPE)value                                   \
                          * (ACCUM_TYPE)DIGITAL_FILTER_Q16_SCALE;             \
    }                                                                          \
                                                                               \
    /**                                                                        \
     * @brief   Procesa una muestra y devuelve la salida filtrada.             \
     * @param[in,out] filter   Puntero al descriptor. No debe ser NULL.        \
     * @param[in]     sample   Muestra de entrada.                             \
     * @return  Valor filtrado del mismo tipo que la muestra.                  \
     */                                                                        \
    static inline SAMPLE_TYPE ema_filter_##SUFFIX##_update(                   \
        ema_filter_##SUFFIX##_t * const filter,                               \
        SAMPLE_TYPE                     sample)                               \
    {                                                                          \
        ACCUM_TYPE const alpha     = (ACCUM_TYPE)filter->s_alpha_q16;         \
        ACCUM_TYPE const one_minus = (ACCUM_TYPE)DIGITAL_FILTER_Q16_SCALE     \
                                    - alpha;                                  \
        filter->s_accum =                                                      \
            (alpha * (ACCUM_TYPE)sample)                                      \
            + (one_minus * (filter->s_accum                                   \
                            / (ACCUM_TYPE)DIGITAL_FILTER_Q16_SCALE));         \
        return (SAMPLE_TYPE)(filter->s_accum                                  \
                             / (ACCUM_TYPE)DIGITAL_FILTER_Q16_SCALE);         \
    }                                                                          \
                                                                               \
    /**                                                                        \
     * @brief   Devuelve la salida actual sin procesar una nueva muestra.      \
     * @param[in] filter   Puntero al descriptor. No debe ser NULL.            \
     * @return   Valor filtrado actual.                                        \
     */                                                                        \
    static inline SAMPLE_TYPE ema_filter_##SUFFIX##_get(                      \
        const ema_filter_##SUFFIX##_t * const filter)                         \
    {                                                                          \
        return (SAMPLE_TYPE)(filter->s_accum                                  \
                             / (ACCUM_TYPE)DIGITAL_FILTER_Q16_SCALE);         \
    }

/*===========================================================================*
 * Instancias predefinidas
 *===========================================================================*/

DEFINE_EMA_FILTER(int16_t,  int64_t,  i16)
DEFINE_EMA_FILTER(int32_t,  int64_t,  i32)
DEFINE_EMA_FILTER(uint16_t, uint64_t, u16)
DEFINE_EMA_FILTER(uint32_t, uint64_t, u32)

#endif /* DIGITAL_FILTER_EMA_H */
