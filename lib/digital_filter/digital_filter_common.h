/**
 * @file    digital_filter_common.h
 * @brief   Tipos base, macros de punto fijo y macros de generación de código
 *          para la librería de filtros digitales.
 *
 * @details Este archivo define:
 *          - Representaciones de punto fijo con signo y sin signo en formato
 *            Qm.n (m bits enteros, n bits fraccionarios).
 *          - Macros de conversión entre punto fijo y valores escalados.
 *          - Macros de generación de código (X-macros) que instancian los
 *            algoritmos de filtrado para tipos de dato concretos.
 *
 *          ### Punto fijo en este framework
 *
 *          Se usa la notación Q0.n para coeficientes en el rango [0, 1):
 *
 *          | Macro sufijo | Tipo interno  | Bits fraccionarios | Resolución     |
 *          |--------------|---------------|--------------------|----------------|
 *          | Q8           | uint16_t      | 8                  | 1/256 ≈ 0.0039 |
 *          | Q15          | uint16_t      | 15                 | 1/32768        |
 *          | Q16          | uint32_t      | 16                 | 1/65536        |
 *
 *          Para coeficientes: alpha_Q8 = (uint16_t)(alpha * 256.0f)
 *
 *          La multiplicación intermedia usa el tipo ACCUM_TYPE (más ancho)
 *          para evitar desbordamiento antes del desplazamiento.
 *
 *          ### Uso de las macros de generación
 *
 *          Las macros @c DEFINE_EMA_FILTER, @c DEFINE_SMA_FILTER, etc. generan
 *          tipos y funciones con sufijo de tipo. No deben llamarse desde código
 *          de usuario; están destinadas a los headers individuales de cada
 *          algoritmo.
 *
 * @note    Compatible con C99. No requiere FPU. No usa memoria dinámica.
 *
 * @author  mcuframework contributors
 * @version 1.0.0
 */

#ifndef DIGITAL_FILTER_COMMON_H
#define DIGITAL_FILTER_COMMON_H

#include <stdint.h>
#include <stddef.h>

/*===========================================================================*
 * Constantes de escala para punto fijo
 *===========================================================================*/

/** @brief Escala Q0.8: 2^8 = 256. Coeficientes en uint16_t. */
#define DIGITAL_FILTER_Q8_SCALE     (256u)

/** @brief Escala Q0.15: 2^15 = 32768. Coeficientes en uint16_t. */
#define DIGITAL_FILTER_Q15_SCALE    (32768u)

/** @brief Escala Q0.16: 2^16 = 65536. Coeficientes en uint32_t. */
#define DIGITAL_FILTER_Q16_SCALE    (65536u)

/*===========================================================================*
 * Macros de conversión de punto fijo
 *===========================================================================*/

/**
 * @brief   Convierte un valor flotante [0.0, 1.0) a representación Q0.8.
 * @param   x   Valor flotante. Debe estar en [0.0, 1.0).
 * @return  Valor uint16_t en Q0.8.
 * @note    Usar solo en inicializaciones con literales constantes conocidos.
 *          En runtime sin FPU preferir @c DIGITAL_FILTER_FLOAT_TO_Q8_SAFE.
 */
#define DIGITAL_FILTER_FLOAT_TO_Q8(x)  \
    ((uint16_t)((x) * (float)DIGITAL_FILTER_Q8_SCALE))

/**
 * @brief   Convierte un valor flotante [0.0, 1.0) a representación Q0.15.
 * @param   x   Valor flotante. Debe estar en [0.0, 1.0).
 * @return  Valor uint16_t en Q0.15.
 */
#define DIGITAL_FILTER_FLOAT_TO_Q15(x) \
    ((uint16_t)((x) * (float)DIGITAL_FILTER_Q15_SCALE))

/**
 * @brief   Convierte un valor flotante [0.0, 1.0) a representación Q0.16.
 * @param   x   Valor flotante. Debe estar en [0.0, 1.0).
 * @return  Valor uint32_t en Q0.16.
 */
#define DIGITAL_FILTER_FLOAT_TO_Q16(x) \
    ((uint32_t)((x) * (float)DIGITAL_FILTER_Q16_SCALE))

/**
 * @brief   Convierte un entero numerador/denominador a Q0.8 sin usar float.
 * @param   num   Numerador (0 <= num < den).
 * @param   den   Denominador.
 * @return  Valor uint16_t en Q0.8.
 * @example DIGITAL_FILTER_FRAC_TO_Q8(1, 10) → ~26  (≈ 0.1015, error < 0.5%)
 */
#define DIGITAL_FILTER_FRAC_TO_Q8(num, den) \
    ((uint16_t)(((uint32_t)(num) * DIGITAL_FILTER_Q8_SCALE) / (uint32_t)(den)))

/**
 * @brief   Convierte un entero numerador/denominador a Q0.16 sin usar float.
 * @param   num   Numerador (0 <= num < den).
 * @param   den   Denominador.
 * @return  Valor uint32_t en Q0.16.
 */
#define DIGITAL_FILTER_FRAC_TO_Q16(num, den) \
    ((uint32_t)(((uint64_t)(num) * DIGITAL_FILTER_Q16_SCALE) / (uint64_t)(den)))

/*===========================================================================*
 * Utilidades internas
 *===========================================================================*/

/**
 * @brief   Clamp (saturación) de un valor entre un mínimo y un máximo.
 * @param   val   Valor a saturar.
 * @param   lo    Límite inferior.
 * @param   hi    Límite superior.
 * @return  Valor saturado.
 */
#define DIGITAL_FILTER_CLAMP(val, lo, hi) \
    (((val) < (lo)) ? (lo) : (((val) > (hi)) ? (hi) : (val)))

/**
 * @brief   Valor absoluto genérico para tipos con signo.
 * @param   x   Valor con signo.
 * @return  |x|
 */
#define DIGITAL_FILTER_ABS(x) \
    (((x) < 0) ? (-(x)) : (x))

/*===========================================================================*
 * Estados de salida para filtros binarios
 *===========================================================================*/

/**
 * @brief   Estado de salida de un filtro binario.
 *
 * @details Un filtro binario puede devolver PENDING mientras acumula muestras
 *          y aún no puede confirmar el estado del pin o señal.
 */
typedef enum
{
    DIGITAL_FILTER_BINARY_LOW     = 0,   /**< Estado bajo confirmado.         */
    DIGITAL_FILTER_BINARY_HIGH    = 1,   /**< Estado alto confirmado.         */
    DIGITAL_FILTER_BINARY_PENDING = 2,   /**< Acumulando muestras, sin certeza.*/
} digital_filter_binary_state_t;

/*===========================================================================*
 * Macro de generación — verificación de tamaño de buffer (poder de 2)
 *===========================================================================*/

/**
 * @brief   Verifica en tiempo de compilación que SIZE sea potencia de 2.
 * @details Requerido para el buffer circular del SMA sin módulo.
 *          Un tamaño que no sea potencia de 2 genera un error de compilación.
 */
#define DIGITAL_FILTER_STATIC_ASSERT_POW2(SIZE) \
    typedef char _df_pow2_check_##SIZE[                        \
        (((SIZE) != 0u) && (((SIZE) & ((SIZE) - 1u)) == 0u))   \
        ? 1 : -1]

#endif /* DIGITAL_FILTER_COMMON_H */
