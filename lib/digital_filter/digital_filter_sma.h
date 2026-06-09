/**
 * @file    digital_filter_sma.h
 * @brief   Filtro de promedio móvil simple (SMA) genérico con buffer circular.
 *
 * @details Implementa el promedio de las últimas N muestras:
 *
 *          @code
 *          y[n] = (x[n] + x[n-1] + ... + x[n-N+1]) / N
 *          @endcode
 *
 *          El buffer circular usa una máscara de bits en lugar de módulo
 *          para el avance del índice, por lo que N debe ser potencia de 2.
 *          Esto se verifica en tiempo de compilación mediante
 *          @c DIGITAL_FILTER_STATIC_ASSERT_POW2.
 *
 *          La suma acumulada se mantiene en un acumulador de tipo más ancho
 *          que la muestra para evitar desbordamiento. Con N=16 y muestras
 *          int16_t, el acumulador máximo es 16 * 32767 = 524272, que cabe
 *          holgadamente en int32_t.
 *
 *          ### Tamaños de ventana recomendados
 *
 *          | N  | Latencia | Atenuación a Nyquist |
 *          |----|----------|----------------------|
 *          | 4  | 4 muestras | -12 dB             |
 *          | 8  | 8 muestras | -18 dB             |
 *          | 16 | 16 muestras| -24 dB             |
 *          | 32 | 32 muestras| -30 dB             |
 *
 *          ### Instancias predefinidas
 *
 *          | Sufijo | Tipo de muestra | Acumulador | N máximo seguro |
 *          |--------|-----------------|------------|-----------------|
 *          | i16    | int16_t         | int32_t    | 65536           |
 *          | i32    | int32_t         | int64_t    | 65536           |
 *          | u16    | uint16_t        | uint32_t   | 65536           |
 *          | u32    | uint32_t        | uint64_t   | 65536           |
 *
 *          ### Ejemplo de uso
 *
 *          @code
 *          #include "digital_filter_sma.h"
 *
 *          #define ADC_WINDOW  8u
 *
 *          int16_t             adc_buf[ADC_WINDOW];
 *          sma_filter_i16_t    adc_filter;
 *
 *          sma_filter_i16_init(&adc_filter, adc_buf, ADC_WINDOW);
 *
 *          // En cada tick periódico:
 *          int16_t filtered = sma_filter_i16_update(&adc_filter, adc_read());
 *          @endcode
 *
 * @warning El buffer debe ser provisto por el llamador y debe tener exactamente
 *          @c window_size elementos. El módulo no verifica límites en runtime.
 *
 * @note    No requiere FPU. No usa memoria dinámica. Compatible con C99.
 *
 * @author  mcuframework contributors
 * @version 1.0.0
 */

#ifndef DIGITAL_FILTER_SMA_H
#define DIGITAL_FILTER_SMA_H

#include "digital_filter_common.h"

/*===========================================================================*
 * Macro de generación de instancias SMA
 *===========================================================================*/

/**
 * @brief   Genera el descriptor y las funciones del filtro SMA para un
 *          tipo de dato y acumulador específicos.
 *
 * @param   SAMPLE_TYPE   Tipo de las muestras (ej: int16_t).
 * @param   ACCUM_TYPE    Tipo del acumulador, más ancho que SAMPLE_TYPE.
 * @param   SUFFIX        Sufijo de nombre (ej: i16, u32).
 *
 * @details Genera:
 *          - @c sma_filter_SUFFIX_t        : descriptor del filtro.
 *          - @c sma_filter_SUFFIX_init()   : inicialización con buffer externo.
 *          - @c sma_filter_SUFFIX_reset()  : vacía el buffer y reinicia suma.
 *          - @c sma_filter_SUFFIX_update() : procesa una muestra.
 *          - @c sma_filter_SUFFIX_get()    : lee la salida sin nueva muestra.
 */
#define DEFINE_SMA_FILTER(SAMPLE_TYPE, ACCUM_TYPE, SUFFIX)                    \
                                                                               \
    /** @brief Descriptor del filtro SMA para el tipo SUFFIX. */               \
    typedef struct                                                             \
    {                                                                          \
        SAMPLE_TYPE *s_buf;         /**< Buffer circular externo.           */ \
        ACCUM_TYPE   s_sum;         /**< Suma acumulada de las N muestras.  */ \
        uint16_t     s_index;       /**< Índice de escritura actual.        */ \
        uint16_t     s_mask;        /**< Máscara de bits (N-1).             */ \
        uint16_t     s_count;       /**< Muestras cargadas (0..N).          */ \
    } sma_filter_##SUFFIX##_t;                                                 \
                                                                               \
    /**                                                                        \
     * @brief   Inicializa el filtro SMA.                                      \
     * @param[out] filter       Puntero al descriptor. No debe ser NULL.       \
     * @param[in]  buf          Buffer externo de @p window_size elementos.    \
     *                          Debe permanecer válido durante toda la vida     \
     *                          del filtro. No debe ser NULL.                  \
     * @param[in]  window_size  Tamaño de la ventana. Debe ser potencia de 2   \
     *                          y mayor que 1. El llamador debe verificar esto  \
     *                          con DIGITAL_FILTER_STATIC_ASSERT_POW2 si el    \
     *                          valor es una constante conocida en compilación. \
     * @warning  window_size NO se verifica en runtime por diseño.             \
     */                                                                        \
    static inline void sma_filter_##SUFFIX##_init(                            \
        sma_filter_##SUFFIX##_t * const filter,                               \
        SAMPLE_TYPE * const             buf,                                  \
        uint16_t                        window_size)                          \
    {                                                                          \
        uint16_t i;                                                            \
        filter->s_buf   = buf;                                                 \
        filter->s_sum   = (ACCUM_TYPE)0;                                      \
        filter->s_index = 0u;                                                  \
        filter->s_mask  = (uint16_t)(window_size - 1u);                       \
        filter->s_count = 0u;                                                  \
        for (i = 0u; i < window_size; i++)                                    \
        {                                                                      \
            buf[i] = (SAMPLE_TYPE)0;                                          \
        }                                                                      \
    }                                                                          \
                                                                               \
    /**                                                                        \
     * @brief   Reinicia el filtro sin cambiar el tamaño de ventana.           \
     * @param[in,out] filter   Puntero al descriptor. No debe ser NULL.        \
     */                                                                        \
    static inline void sma_filter_##SUFFIX##_reset(                           \
        sma_filter_##SUFFIX##_t * const filter)                               \
    {                                                                          \
        uint16_t const window = (uint16_t)(filter->s_mask + 1u);              \
        uint16_t       i;                                                      \
        filter->s_sum   = (ACCUM_TYPE)0;                                      \
        filter->s_index = 0u;                                                  \
        filter->s_count = 0u;                                                  \
        for (i = 0u; i < window; i++)                                         \
        {                                                                      \
            filter->s_buf[i] = (SAMPLE_TYPE)0;                                \
        }                                                                      \
    }                                                                          \
                                                                               \
    /**                                                                        \
     * @brief   Procesa una muestra y devuelve el promedio de la ventana.      \
     * @param[in,out] filter   Puntero al descriptor. No debe ser NULL.        \
     * @param[in]     sample   Muestra de entrada.                             \
     * @return  Promedio de las últimas N muestras (o menos durante arranque). \
     */                                                                        \
    static inline SAMPLE_TYPE sma_filter_##SUFFIX##_update(                   \
        sma_filter_##SUFFIX##_t * const filter,                               \
        SAMPLE_TYPE                     sample)                               \
    {                                                                          \
        filter->s_sum -= (ACCUM_TYPE)filter->s_buf[filter->s_index];          \
        filter->s_buf[filter->s_index] = sample;                              \
        filter->s_sum += (ACCUM_TYPE)sample;                                  \
        filter->s_index = (uint16_t)((filter->s_index + 1u) & filter->s_mask);\
        if (filter->s_count <= filter->s_mask)                                 \
        {                                                                      \
            filter->s_count++;                                                 \
        }                                                                      \
        return (SAMPLE_TYPE)(filter->s_sum / (ACCUM_TYPE)filter->s_count);    \
    }                                                                          \
                                                                               \
    /**                                                                        \
     * @brief   Devuelve el promedio actual sin procesar nueva muestra.        \
     * @param[in] filter   Puntero al descriptor. No debe ser NULL.            \
     * @return   Promedio actual.                                              \
     */                                                                        \
    static inline SAMPLE_TYPE sma_filter_##SUFFIX##_get(                      \
        const sma_filter_##SUFFIX##_t * const filter)                         \
    {                                                                          \
        if (filter->s_count == 0u)                                             \
        {                                                                      \
            return (SAMPLE_TYPE)0;                                             \
        }                                                                      \
        return (SAMPLE_TYPE)(filter->s_sum / (ACCUM_TYPE)filter->s_count);    \
    }

/*===========================================================================*
 * Instancias predefinidas
 *===========================================================================*/

DEFINE_SMA_FILTER(int16_t,  int32_t,  i16)
DEFINE_SMA_FILTER(int32_t,  int64_t,  i32)
DEFINE_SMA_FILTER(uint16_t, uint32_t, u16)
DEFINE_SMA_FILTER(uint32_t, uint64_t, u32)

#endif /* DIGITAL_FILTER_SMA_H */
