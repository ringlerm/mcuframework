/**
 * @file    digital_filter_peak.h
 * @brief   Detector de pico (peak detector) con decaimiento configurable.
 *
 * @details Rastrea el valor máximo o mínimo de una señal en una ventana
 *          de tiempo. Soporta dos modos de operación:
 *
 *          **Modo HOLD**: el pico se mantiene hasta que se llama a reset()
 *          manualmente. Útil para capturar valores extremos sin perderlos.
 *
 *          **Modo DECAY**: el pico decae una cantidad fija (@c decay_step)
 *          en cada llamada a update(). Cuando la señal supera el pico actual,
 *          el pico se actualiza inmediatamente. Útil para envolventes de
 *          señales de audio, vibración, etc.
 *
 *          @code
 *          Señal:   __/‾‾‾‾\___/‾‾\____
 *          Pico:    __/‾‾‾‾‾‾‾‾‾‾\____   (HOLD hasta reset)
 *          Pico:    __/‾‾‾‾\____/‾‾\__   (DECAY con paso grande)
 *          @endcode
 *
 *          ### Instancias predefinidas
 *
 *          | Sufijo | Tipo de muestra |
 *          |--------|-----------------|
 *          | i16    | int16_t         |
 *          | i32    | int32_t         |
 *          | u16    | uint16_t        |
 *          | u32    | uint32_t        |
 *
 *          ### Ejemplo de uso — detector de pico de vibración
 *
 *          @code
 *          #include "digital_filter_peak.h"
 *
 *          peak_filter_i16_t vib_peak;
 *
 *          // Decaer 10 unidades por muestra (tick 1 ms)
 *          peak_filter_i16_init(&vib_peak,
 *                               PEAK_FILTER_MODE_MAX,
 *                               PEAK_FILTER_DECAY,
 *                               0,      // inicial
 *                               10);    // decay_step
 *
 *          int16_t envelope = peak_filter_i16_update(&vib_peak, accel_read());
 *          @endcode
 *
 * @note    No requiere FPU. No usa memoria dinámica. Compatible con C99.
 *
 * @author  mcuframework contributors
 * @version 1.0.0
 */

#ifndef DIGITAL_FILTER_PEAK_H
#define DIGITAL_FILTER_PEAK_H

#include "digital_filter_common.h"

/** @brief  Modo del detector de pico. */
typedef enum
{
    PEAK_FILTER_MODE_MAX = 0u, /**< Rastrea el valor máximo (peak positivo).  */
    PEAK_FILTER_MODE_MIN = 1u, /**< Rastrea el valor mínimo (valley).         */
} peak_filter_mode_t;

/** @brief  Política de decaimiento del pico. */
typedef enum
{
    PEAK_FILTER_HOLD  = 0u, /**< Mantiene el pico hasta reset() explícito.    */
    PEAK_FILTER_DECAY = 1u, /**< Decae s_decay_step unidades por update().    */
} peak_filter_decay_t;

/*===========================================================================*
 * Macro de generación de instancias PEAK DETECTOR
 *===========================================================================*/

/**
 * @brief   Genera el descriptor y las funciones del detector de pico.
 *
 * @param   SAMPLE_TYPE   Tipo de las muestras (ej: int16_t).
 * @param   SUFFIX        Sufijo de nombre (ej: i16).
 *
 * @details Genera:
 *          - @c peak_filter_SUFFIX_t        : descriptor.
 *          - @c peak_filter_SUFFIX_init()   : inicialización.
 *          - @c peak_filter_SUFFIX_reset()  : reinicia al valor dado.
 *          - @c peak_filter_SUFFIX_update() : procesa muestra, devuelve pico.
 *          - @c peak_filter_SUFFIX_get()    : lee pico sin nueva muestra.
 */
#define DEFINE_PEAK_FILTER(SAMPLE_TYPE, SUFFIX)                               \
                                                                               \
    /** @brief Descriptor del detector de pico para el tipo SUFFIX. */         \
    typedef struct                                                             \
    {                                                                          \
        SAMPLE_TYPE          s_peak;        /**< Valor de pico actual.      */ \
        SAMPLE_TYPE          s_decay_step;  /**< Paso de decaimiento.       */ \
        peak_filter_mode_t   s_mode;        /**< MAX o MIN.                 */ \
        peak_filter_decay_t  s_decay_mode;  /**< HOLD o DECAY.              */ \
    } peak_filter_##SUFFIX##_t;                                                \
                                                                               \
    /**                                                                        \
     * @brief   Inicializa el detector de pico.                                \
     * @param[out] filter       Puntero al descriptor. No debe ser NULL.       \
     * @param[in]  mode         PEAK_FILTER_MODE_MAX o PEAK_FILTER_MODE_MIN.   \
     * @param[in]  decay_mode   PEAK_FILTER_HOLD o PEAK_FILTER_DECAY.          \
     * @param[in]  initial      Valor inicial del pico.                        \
     * @param[in]  decay_step   Decaimiento por llamada a update().             \
     *                          Ignorado en modo HOLD.                         \
     */                                                                        \
    static inline void peak_filter_##SUFFIX##_init(                           \
        peak_filter_##SUFFIX##_t * const filter,                              \
        peak_filter_mode_t               mode,                                \
        peak_filter_decay_t              decay_mode,                          \
        SAMPLE_TYPE                      initial,                             \
        SAMPLE_TYPE                      decay_step)                          \
    {                                                                          \
        filter->s_peak        = initial;                                       \
        filter->s_mode        = mode;                                          \
        filter->s_decay_mode  = decay_mode;                                    \
        filter->s_decay_step  = decay_step;                                    \
    }                                                                          \
                                                                               \
    /**                                                                        \
     * @brief   Reinicia el pico a un valor dado.                              \
     * @param[in,out] filter   Puntero al descriptor. No debe ser NULL.        \
     * @param[in]     value    Nuevo valor del pico.                           \
     */                                                                        \
    static inline void peak_filter_##SUFFIX##_reset(                          \
        peak_filter_##SUFFIX##_t * const filter,                              \
        SAMPLE_TYPE                      value)                               \
    {                                                                          \
        filter->s_peak = value;                                                \
    }                                                                          \
                                                                               \
    /**                                                                        \
     * @brief   Procesa una muestra y actualiza el pico.                       \
     * @param[in,out] filter   Puntero al descriptor. No debe ser NULL.        \
     * @param[in]     sample   Muestra de entrada.                             \
     * @return  Valor del pico actualizado.                                    \
     */                                                                        \
    static inline SAMPLE_TYPE peak_filter_##SUFFIX##_update(                  \
        peak_filter_##SUFFIX##_t * const filter,                              \
        SAMPLE_TYPE                      sample)                              \
    {                                                                          \
        if (filter->s_mode == PEAK_FILTER_MODE_MAX)                            \
        {                                                                      \
            if (sample >= filter->s_peak)                                      \
            {                                                                  \
                filter->s_peak = sample;                                       \
            }                                                                  \
            else if (filter->s_decay_mode == PEAK_FILTER_DECAY)               \
            {                                                                  \
                if (filter->s_peak > filter->s_decay_step)                    \
                {                                                              \
                    filter->s_peak = (SAMPLE_TYPE)(filter->s_peak             \
                                                   - filter->s_decay_step);   \
                }                                                              \
                else                                                           \
                {                                                              \
                    filter->s_peak = (SAMPLE_TYPE)0;                          \
                }                                                              \
                if (filter->s_peak < sample)                                   \
                {                                                              \
                    filter->s_peak = sample;                                   \
                }                                                              \
            }                                                                  \
            else                                                               \
            {                                                                  \
                /* HOLD: no decae */                                           \
            }                                                                  \
        }                                                                      \
        else /* PEAK_FILTER_MODE_MIN */                                        \
        {                                                                      \
            if (sample <= filter->s_peak)                                      \
            {                                                                  \
                filter->s_peak = sample;                                       \
            }                                                                  \
            else if (filter->s_decay_mode == PEAK_FILTER_DECAY)               \
            {                                                                  \
                filter->s_peak = (SAMPLE_TYPE)(filter->s_peak                 \
                                               + filter->s_decay_step);       \
                if (filter->s_peak > sample)                                   \
                {                                                              \
                    filter->s_peak = sample;                                   \
                }                                                              \
            }                                                                  \
            else                                                               \
            {                                                                  \
                /* HOLD: no decae */                                           \
            }                                                                  \
        }                                                                      \
        return filter->s_peak;                                                 \
    }                                                                          \
                                                                               \
    /**                                                                        \
     * @brief   Devuelve el pico actual sin procesar nueva muestra.            \
     * @param[in] filter   Puntero al descriptor. No debe ser NULL.            \
     * @return   Pico actual.                                                  \
     */                                                                        \
    static inline SAMPLE_TYPE peak_filter_##SUFFIX##_get(                     \
        const peak_filter_##SUFFIX##_t * const filter)                        \
    {                                                                          \
        return filter->s_peak;                                                 \
    }

/*===========================================================================*
 * Instancias predefinidas
 *===========================================================================*/

DEFINE_PEAK_FILTER(int16_t,  i16)
DEFINE_PEAK_FILTER(int32_t,  i32)
DEFINE_PEAK_FILTER(uint16_t, u16)
DEFINE_PEAK_FILTER(uint32_t, u32)

#endif /* DIGITAL_FILTER_PEAK_H */
