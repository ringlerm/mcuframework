/**
 * @file    digital_filter_slew.h
 * @brief   Limitador de tasa de cambio (slew rate limiter) genérico.
 *
 * @details Limita la velocidad máxima de cambio de una señal por muestra:
 *
 *          @code
 *          delta   = sample - output_prev
 *          delta   = clamp(delta, -max_step, +max_step)
 *          output  = output_prev + delta
 *          @endcode
 *
 *          Aplicaciones típicas:
 *          - Rampas suaves de velocidad en motores (evitar pico de corriente).
 *          - Suavizado de señales de referencia para servos.
 *          - Filtrado de consignas en lazos de control.
 *          - Protección ante cambios bruscos de setpoint.
 *
 *          A diferencia del EMA o SMA, el slew limiter preserva la forma
 *          de la señal pero limita únicamente la derivada. No añade latencia
 *          sostenida si la señal cambia lentamente.
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
 *          ### Ejemplo de uso
 *
 *          @code
 *          #include "digital_filter_slew.h"
 *
 *          slew_filter_i16_t   motor_ramp;
 *
 *          // Máximo cambio de 50 unidades por muestra (tick de 10 ms)
 *          slew_filter_i16_init(&motor_ramp, 50, 0);
 *
 *          // En cada tick de control:
 *          int16_t cmd = slew_filter_i16_update(&motor_ramp, setpoint);
 *          motor_set_speed(cmd);
 *          @endcode
 *
 * @note    No requiere FPU. No usa memoria dinámica. Compatible con C99.
 *
 * @author  mcuframework contributors
 * @version 1.0.0
 */

#ifndef DIGITAL_FILTER_SLEW_H
#define DIGITAL_FILTER_SLEW_H

#include "digital_filter_common.h"

/*===========================================================================*
 * Macro de generación de instancias SLEW RATE LIMITER
 *===========================================================================*/

/**
 * @brief   Genera el descriptor y las funciones del slew rate limiter.
 *
 * @param   SAMPLE_TYPE   Tipo de las muestras. Debe ser un tipo con signo para
 *                        que el delta negativo funcione correctamente.
 *                        Para tipos sin signo el límite inferior del delta se
 *                        satura en 0 automáticamente por aritmética sin signo.
 * @param   SUFFIX        Sufijo de nombre (ej: i16).
 *
 * @details Genera:
 *          - @c slew_filter_SUFFIX_t        : descriptor.
 *          - @c slew_filter_SUFFIX_init()   : inicialización con max_step.
 *          - @c slew_filter_SUFFIX_reset()  : actualiza valor actual sin límite.
 *          - @c slew_filter_SUFFIX_update() : aplica límite de pendiente.
 *          - @c slew_filter_SUFFIX_get()    : lee salida sin nueva muestra.
 *          - @c slew_filter_SUFFIX_set_step(): actualiza max_step en runtime.
 */
#define DEFINE_SLEW_FILTER(SAMPLE_TYPE, SUFFIX)                               \
                                                                               \
    /** @brief Descriptor del slew rate limiter para el tipo SUFFIX. */        \
    typedef struct                                                             \
    {                                                                          \
        SAMPLE_TYPE s_output;       /**< Salida actual (valor previo).      */ \
        SAMPLE_TYPE s_max_step;     /**< Máximo cambio permitido por muestra.*/\
    } slew_filter_##SUFFIX##_t;                                                \
                                                                               \
    /**                                                                        \
     * @brief   Inicializa el slew rate limiter.                               \
     * @param[out] filter     Puntero al descriptor. No debe ser NULL.         \
     * @param[in]  max_step   Máxima variación absoluta permitida por muestra. \
     *                        Debe ser > 0.                                    \
     * @param[in]  initial    Valor inicial de la salida.                      \
     */                                                                        \
    static inline void slew_filter_##SUFFIX##_init(                           \
        slew_filter_##SUFFIX##_t * const filter,                              \
        SAMPLE_TYPE                      max_step,                            \
        SAMPLE_TYPE                      initial)                             \
    {                                                                          \
        filter->s_max_step = max_step;                                         \
        filter->s_output   = initial;                                          \
    }                                                                          \
                                                                               \
    /**                                                                        \
     * @brief   Fuerza la salida a un valor sin aplicar el límite de pendiente.\
     * @param[in,out] filter   Puntero al descriptor. No debe ser NULL.        \
     * @param[in]     value    Nuevo valor de la salida.                       \
     * @note    Útil para sincronizar el filtro con la señal tras un reset     \
     *          del sistema sin producir una rampa de arranque.                \
     */                                                                        \
    static inline void slew_filter_##SUFFIX##_reset(                          \
        slew_filter_##SUFFIX##_t * const filter,                              \
        SAMPLE_TYPE                      value)                               \
    {                                                                          \
        filter->s_output = value;                                              \
    }                                                                          \
                                                                               \
    /**                                                                        \
     * @brief   Aplica el límite de pendiente a una nueva muestra.             \
     * @param[in,out] filter   Puntero al descriptor. No debe ser NULL.        \
     * @param[in]     sample   Valor deseado (setpoint).                       \
     * @return  Salida limitada en pendiente.                                  \
     */                                                                        \
    static inline SAMPLE_TYPE slew_filter_##SUFFIX##_update(                  \
        slew_filter_##SUFFIX##_t * const filter,                              \
        SAMPLE_TYPE                      sample)                              \
    {                                                                          \
        SAMPLE_TYPE delta = (SAMPLE_TYPE)(sample - filter->s_output);         \
        delta             = (SAMPLE_TYPE)DIGITAL_FILTER_CLAMP(                \
                                delta,                                         \
                                (SAMPLE_TYPE)(-(filter->s_max_step)),          \
                                filter->s_max_step);                           \
        filter->s_output  = (SAMPLE_TYPE)(filter->s_output + delta);          \
        return filter->s_output;                                               \
    }                                                                          \
                                                                               \
    /**                                                                        \
     * @brief   Devuelve la salida actual sin procesar nueva muestra.          \
     * @param[in] filter   Puntero al descriptor. No debe ser NULL.            \
     * @return   Salida actual.                                                \
     */                                                                        \
    static inline SAMPLE_TYPE slew_filter_##SUFFIX##_get(                     \
        const slew_filter_##SUFFIX##_t * const filter)                        \
    {                                                                          \
        return filter->s_output;                                               \
    }                                                                          \
                                                                               \
    /**                                                                        \
     * @brief   Actualiza el paso máximo en runtime.                           \
     * @param[in,out] filter     Puntero al descriptor. No debe ser NULL.      \
     * @param[in]     max_step   Nuevo paso máximo. Debe ser > 0.              \
     */                                                                        \
    static inline void slew_filter_##SUFFIX##_set_step(                       \
        slew_filter_##SUFFIX##_t * const filter,                              \
        SAMPLE_TYPE                      max_step)                            \
    {                                                                          \
        filter->s_max_step = max_step;                                         \
    }

/*===========================================================================*
 * Instancias predefinidas
 *===========================================================================*/

DEFINE_SLEW_FILTER(int16_t,  i16)
DEFINE_SLEW_FILTER(int32_t,  i32)
DEFINE_SLEW_FILTER(uint16_t, u16)
DEFINE_SLEW_FILTER(uint32_t, u32)

#endif /* DIGITAL_FILTER_SLEW_H */
