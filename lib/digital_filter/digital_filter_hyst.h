/**
 * @file    digital_filter_hyst.h
 * @brief   Filtro de histéresis genérico — evita oscilaciones alrededor de umbrales.
 *
 * @details Implementa un comparador con banda muerta (dead-band) de dos umbrales:
 *
 *          @code
 *          Si estado actual = LOW  y muestra >  threshold_high → nuevo estado = HIGH
 *          Si estado actual = HIGH y muestra <  threshold_low  → nuevo estado = LOW
 *          En cualquier otro caso el estado no cambia.
 *          @endcode
 *
 *          Esto previene el chatter (oscilación rápida de estado) cuando la
 *          señal flota cerca de un umbral simple. Aplicaciones típicas:
 *
 *          - Termostatos (encender calefacción a 18°C, apagar a 22°C).
 *          - Detección de nivel de batería (alarma < 10%, quitar alarma > 15%).
 *          - Comparadores de tensión digitales.
 *          - Control ON/OFF con banda muerta.
 *
 *          ### Estados de salida
 *
 *          El filtro devuelve @c digital_filter_binary_state_t:
 *          - @c DIGITAL_FILTER_BINARY_LOW  : señal por debajo del umbral bajo.
 *          - @c DIGITAL_FILTER_BINARY_HIGH : señal por encima del umbral alto.
 *          (PENDING no se usa en este filtro; el estado inicial es configurable.)
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
 *          #include "digital_filter_hyst.h"
 *
 *          hyst_filter_i16_t   temp_ctrl;
 *
 *          // Encender calefacción bajo 18°C, apagar sobre 22°C
 *          hyst_filter_i16_init(&temp_ctrl,
 *                               1800,   // threshold_low  (18.00 °C * 100)
 *                               2200,   // threshold_high (22.00 °C * 100)
 *                               DIGITAL_FILTER_BINARY_LOW);
 *
 *          digital_filter_binary_state_t state =
 *              hyst_filter_i16_update(&temp_ctrl, read_temp_centidegrees());
 *
 *          if (state == DIGITAL_FILTER_BINARY_LOW)
 *          {
 *              heater_on();
 *          }
 *          @endcode
 *
 * @note    No requiere FPU. No usa memoria dinámica. Compatible con C99.
 *
 * @author  mcuframework contributors
 * @version 1.0.0
 */

#ifndef DIGITAL_FILTER_HYST_H
#define DIGITAL_FILTER_HYST_H

#include "digital_filter_common.h"

/*===========================================================================*
 * Macro de generación de instancias HYSTÉRESIS
 *===========================================================================*/

/**
 * @brief   Genera el descriptor y las funciones del filtro de histéresis.
 *
 * @param   SAMPLE_TYPE   Tipo de las muestras (ej: int16_t).
 * @param   SUFFIX        Sufijo de nombre (ej: i16).
 *
 * @details Genera:
 *          - @c hyst_filter_SUFFIX_t        : descriptor.
 *          - @c hyst_filter_SUFFIX_init()   : inicialización con umbrales y estado.
 *          - @c hyst_filter_SUFFIX_update() : evalúa muestra, devuelve nuevo estado.
 *          - @c hyst_filter_SUFFIX_get()    : lee estado actual sin nueva muestra.
 *          - @c hyst_filter_SUFFIX_set_thresholds() : actualiza umbrales en runtime.
 */
#define DEFINE_HYST_FILTER(SAMPLE_TYPE, SUFFIX)                               \
                                                                               \
    /** @brief Descriptor del filtro de histéresis para el tipo SUFFIX. */     \
    typedef struct                                                             \
    {                                                                          \
        SAMPLE_TYPE                   s_threshold_low;  /**< Umbral inferior.*/\
        SAMPLE_TYPE                   s_threshold_high; /**< Umbral superior.*/\
        digital_filter_binary_state_t s_state;          /**< Estado actual.  */\
    } hyst_filter_##SUFFIX##_t;                                                \
                                                                               \
    /**                                                                        \
     * @brief   Inicializa el filtro de histéresis.                            \
     * @param[out] filter           Puntero al descriptor. No debe ser NULL.   \
     * @param[in]  threshold_low    Umbral inferior. Al caer por debajo de     \
     *                              este valor el estado pasa a LOW.           \
     * @param[in]  threshold_high   Umbral superior. Al superar este valor     \
     *                              el estado pasa a HIGH.                     \
     * @param[in]  initial_state    Estado inicial.                            \
     * @note   threshold_low debe ser < threshold_high.                        \
     */                                                                        \
    static inline void hyst_filter_##SUFFIX##_init(                           \
        hyst_filter_##SUFFIX##_t * const  filter,                             \
        SAMPLE_TYPE                       threshold_low,                      \
        SAMPLE_TYPE                       threshold_high,                     \
        digital_filter_binary_state_t     initial_state)                      \
    {                                                                          \
        filter->s_threshold_low  = threshold_low;                             \
        filter->s_threshold_high = threshold_high;                            \
        filter->s_state          = initial_state;                             \
    }                                                                          \
                                                                               \
    /**                                                                        \
     * @brief   Evalúa una muestra y actualiza el estado con histéresis.       \
     * @param[in,out] filter   Puntero al descriptor. No debe ser NULL.        \
     * @param[in]     sample   Muestra de entrada.                             \
     * @return  Estado resultante (LOW o HIGH).                                \
     */                                                                        \
    static inline digital_filter_binary_state_t hyst_filter_##SUFFIX##_update(\
        hyst_filter_##SUFFIX##_t * const filter,                              \
        SAMPLE_TYPE                      sample)                              \
    {                                                                          \
        if (filter->s_state == DIGITAL_FILTER_BINARY_LOW)                     \
        {                                                                      \
            if (sample > filter->s_threshold_high)                            \
            {                                                                  \
                filter->s_state = DIGITAL_FILTER_BINARY_HIGH;                 \
            }                                                                  \
        }                                                                      \
        else                                                                   \
        {                                                                      \
            if (sample < filter->s_threshold_low)                             \
            {                                                                  \
                filter->s_state = DIGITAL_FILTER_BINARY_LOW;                  \
            }                                                                  \
        }                                                                      \
        return filter->s_state;                                                \
    }                                                                          \
                                                                               \
    /**                                                                        \
     * @brief   Devuelve el estado actual sin procesar nueva muestra.          \
     * @param[in] filter   Puntero al descriptor. No debe ser NULL.            \
     * @return   Estado actual.                                                \
     */                                                                        \
    static inline digital_filter_binary_state_t hyst_filter_##SUFFIX##_get(  \
        const hyst_filter_##SUFFIX##_t * const filter)                        \
    {                                                                          \
        return filter->s_state;                                                \
    }                                                                          \
                                                                               \
    /**                                                                        \
     * @brief   Actualiza los umbrales en runtime sin reiniciar el estado.     \
     * @param[in,out] filter          Puntero al descriptor. No debe ser NULL. \
     * @param[in]     threshold_low   Nuevo umbral inferior.                   \
     * @param[in]     threshold_high  Nuevo umbral superior.                   \
     */                                                                        \
    static inline void hyst_filter_##SUFFIX##_set_thresholds(                 \
        hyst_filter_##SUFFIX##_t * const filter,                              \
        SAMPLE_TYPE                      threshold_low,                       \
        SAMPLE_TYPE                      threshold_high)                      \
    {                                                                          \
        filter->s_threshold_low  = threshold_low;                             \
        filter->s_threshold_high = threshold_high;                            \
    }

/*===========================================================================*
 * Instancias predefinidas
 *===========================================================================*/

DEFINE_HYST_FILTER(int16_t,  i16)
DEFINE_HYST_FILTER(int32_t,  i32)
DEFINE_HYST_FILTER(uint16_t, u16)
DEFINE_HYST_FILTER(uint32_t, u32)

#endif /* DIGITAL_FILTER_HYST_H */
