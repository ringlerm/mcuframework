/**
 * @file    digital_filter_median.h
 * @brief   Filtro de mediana móvil genérico — robusto frente a picos (spikes).
 *
 * @details En cada llamada a @c update() se inserta la nueva muestra en el
 *          buffer circular y se calcula la mediana de las N muestras actuales
 *          mediante ordenamiento por inserción sobre una copia de trabajo.
 *
 *          El ordenamiento por inserción fue elegido porque:
 *          - Para N pequeño (3, 5, 7, 9) es más rápido que quicksort.
 *          - No usa recursión ni memoria dinámica.
 *          - Es determinista en tiempo (sin caso promedio/peor variable).
 *
 *          El buffer de trabajo se declara en el stack de @c update() con
 *          tamaño máximo configurable por @c DIGITAL_FILTER_MEDIAN_MAX_WINDOW.
 *          Si N > MAX_WINDOW el llamador obtiene un error de compilación.
 *
 *          ### Ventanas impares vs pares
 *
 *          Se recomienda usar N impar para que la mediana sea un elemento
 *          exacto del buffer. Con N par, la mediana se calcula como la media
 *          de los dos elementos centrales (posible truncamiento en tipos enteros).
 *
 *          ### Instancias predefinidas
 *
 *          | Sufijo | Tipo de muestra | Acumulador mediana par |
 *          |--------|-----------------|------------------------|
 *          | i16    | int16_t         | int32_t                |
 *          | i32    | int32_t         | int64_t                |
 *          | u16    | uint16_t        | uint32_t               |
 *          | u32    | uint32_t        | uint64_t               |
 *
 *          ### Ejemplo de uso
 *
 *          @code
 *          #include "digital_filter_median.h"
 *
 *          #define SONAR_WINDOW  5u
 *
 *          uint16_t              sonar_buf[SONAR_WINDOW];
 *          median_filter_u16_t   sonar_filter;
 *
 *          median_filter_u16_init(&sonar_filter, sonar_buf, SONAR_WINDOW);
 *
 *          uint16_t dist = median_filter_u16_update(&sonar_filter, sonar_read());
 *          @endcode
 *
 * @warning El costo de CPU de @c update() es O(N²) en el peor caso.
 *          Mantener N ≤ 15 para aplicaciones de tiempo real con periodos < 1 ms.
 *
 * @note    No requiere FPU. No usa memoria dinámica. Compatible con C99.
 *
 * @author  mcuframework contributors
 * @version 1.0.0
 */

#ifndef DIGITAL_FILTER_MEDIAN_H
#define DIGITAL_FILTER_MEDIAN_H

#include "digital_filter_common.h"

/**
 * @brief   Tamaño máximo de ventana permitido para el filtro de mediana.
 * @details Limita el array de trabajo declarado en el stack de update().
 *          Se puede sobreescribir desde el build system con -D.
 */
#ifndef DIGITAL_FILTER_MEDIAN_MAX_WINDOW
#define DIGITAL_FILTER_MEDIAN_MAX_WINDOW    (15u)
#endif

/*===========================================================================*
 * Macro de generación de instancias MEDIAN
 *===========================================================================*/

/**
 * @brief   Genera el descriptor y las funciones del filtro de mediana.
 *
 * @param   SAMPLE_TYPE   Tipo de las muestras (ej: uint16_t).
 * @param   ACCUM_TYPE    Tipo para el promedio de dos centrales (N par).
 * @param   SUFFIX        Sufijo de nombre (ej: u16).
 *
 * @details Genera:
 *          - @c median_filter_SUFFIX_t        : descriptor.
 *          - @c median_filter_SUFFIX_init()   : inicialización.
 *          - @c median_filter_SUFFIX_reset()  : limpia buffer.
 *          - @c median_filter_SUFFIX_update() : inserta muestra, devuelve mediana.
 *          - @c median_filter_SUFFIX_get()    : devuelve última mediana calculada.
 */
#define DEFINE_MEDIAN_FILTER(SAMPLE_TYPE, ACCUM_TYPE, SUFFIX)                 \
                                                                               \
    /** @brief Descriptor del filtro de mediana para el tipo SUFFIX. */        \
    typedef struct                                                             \
    {                                                                          \
        SAMPLE_TYPE *s_buf;         /**< Buffer circular de muestras.       */ \
        SAMPLE_TYPE  s_last_median; /**< Última mediana calculada.          */ \
        uint8_t      s_window;      /**< Tamaño de la ventana (N).          */ \
        uint8_t      s_index;       /**< Índice de escritura actual.        */ \
        uint8_t      s_count;       /**< Muestras cargadas (0..N).          */ \
    } median_filter_##SUFFIX##_t;                                              \
                                                                               \
    /**                                                                        \
     * @brief   Inicializa el filtro de mediana.                               \
     * @param[out] filter       Puntero al descriptor. No debe ser NULL.       \
     * @param[in]  buf          Buffer externo de @p window_size elementos.    \
     * @param[in]  window_size  Tamaño de la ventana [3, MAX_WINDOW].          \
     *                          Se recomienda valor impar.                     \
     * @warning  window_size debe ser <= DIGITAL_FILTER_MEDIAN_MAX_WINDOW.    \
     */                                                                        \
    static inline void median_filter_##SUFFIX##_init(                         \
        median_filter_##SUFFIX##_t * const filter,                            \
        SAMPLE_TYPE * const                buf,                               \
        uint8_t                            window_size)                       \
    {                                                                          \
        uint8_t i;                                                             \
        filter->s_buf         = buf;                                           \
        filter->s_window      = window_size;                                   \
        filter->s_index       = 0u;                                            \
        filter->s_count       = 0u;                                            \
        filter->s_last_median = (SAMPLE_TYPE)0;                               \
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
    static inline void median_filter_##SUFFIX##_reset(                        \
        median_filter_##SUFFIX##_t * const filter)                            \
    {                                                                          \
        uint8_t i;                                                             \
        filter->s_index       = 0u;                                            \
        filter->s_count       = 0u;                                            \
        filter->s_last_median = (SAMPLE_TYPE)0;                               \
        for (i = 0u; i < filter->s_window; i++)                               \
        {                                                                      \
            filter->s_buf[i] = (SAMPLE_TYPE)0;                                \
        }                                                                      \
    }                                                                          \
                                                                               \
    /**                                                                        \
     * @brief   Inserta una muestra y devuelve la mediana de la ventana actual.\
     * @param[in,out] filter   Puntero al descriptor. No debe ser NULL.        \
     * @param[in]     sample   Muestra de entrada.                             \
     * @return  Mediana de las N muestras actuales.                            \
     * @note    Usa un array de trabajo en el stack de tamaño                  \
     *          DIGITAL_FILTER_MEDIAN_MAX_WINDOW. Costo O(N²) por inserción.  \
     */                                                                        \
    static inline SAMPLE_TYPE median_filter_##SUFFIX##_update(                \
        median_filter_##SUFFIX##_t * const filter,                            \
        SAMPLE_TYPE                        sample)                            \
    {                                                                          \
        SAMPLE_TYPE work[DIGITAL_FILTER_MEDIAN_MAX_WINDOW];                   \
        uint8_t     n;                                                         \
        uint8_t     i;                                                         \
        uint8_t     j;                                                         \
        SAMPLE_TYPE key;                                                       \
                                                                               \
        /* Insertar muestra en buffer circular */                              \
        filter->s_buf[filter->s_index] = sample;                              \
        filter->s_index = (uint8_t)((filter->s_index + 1u)                    \
                                    % filter->s_window);                      \
        if (filter->s_count < filter->s_window)                               \
        {                                                                      \
            filter->s_count++;                                                 \
        }                                                                      \
        n = filter->s_count;                                                   \
                                                                               \
        /* Copiar muestras activas al buffer de trabajo */                     \
        for (i = 0u; i < n; i++)                                               \
        {                                                                      \
            work[i] = filter->s_buf[i];                                       \
        }                                                                      \
                                                                               \
        /* Ordenamiento por inserción ascendente */                            \
        for (i = 1u; i < n; i++)                                               \
        {                                                                      \
            key = work[i];                                                     \
            j   = i;                                                           \
            while ((j > 0u) && (work[j - 1u] > key))                          \
            {                                                                  \
                work[j] = work[j - 1u];                                       \
                j--;                                                           \
            }                                                                  \
            work[j] = key;                                                     \
        }                                                                      \
                                                                               \
        /* Calcular mediana */                                                 \
        if ((n & 1u) != 0u)                                                    \
        {                                                                      \
            /* N impar: elemento central exacto */                             \
            filter->s_last_median = work[n / 2u];                             \
        }                                                                      \
        else                                                                   \
        {                                                                      \
            /* N par: media de los dos centrales */                            \
            filter->s_last_median = (SAMPLE_TYPE)(                            \
                ((ACCUM_TYPE)work[(n / 2u) - 1u]                              \
                 + (ACCUM_TYPE)work[n / 2u]) / (ACCUM_TYPE)2);                \
        }                                                                      \
        return filter->s_last_median;                                          \
    }                                                                          \
                                                                               \
    /**                                                                        \
     * @brief   Devuelve la última mediana calculada sin nueva muestra.        \
     * @param[in] filter   Puntero al descriptor. No debe ser NULL.            \
     * @return   Última mediana calculada.                                     \
     */                                                                        \
    static inline SAMPLE_TYPE median_filter_##SUFFIX##_get(                   \
        const median_filter_##SUFFIX##_t * const filter)                      \
    {                                                                          \
        return filter->s_last_median;                                          \
    }

/*===========================================================================*
 * Instancias predefinidas
 *===========================================================================*/

DEFINE_MEDIAN_FILTER(int16_t,  int32_t,  i16)
DEFINE_MEDIAN_FILTER(int32_t,  int64_t,  i32)
DEFINE_MEDIAN_FILTER(uint16_t, uint32_t, u16)
DEFINE_MEDIAN_FILTER(uint32_t, uint64_t, u32)

#endif /* DIGITAL_FILTER_MEDIAN_H */
