/**
 * @file    digital_filter_binary.h
 * @brief   Filtros para señales binarias: shift register, integrador y monoestable.
 *
 * @details Este archivo agrupa los tres algoritmos de filtrado para señales
 *          digitales (0/1), diseñados para debouncing de botones, detección
 *          de flancos robusta y filtrado de señales con ruido eléctrico.
 *
 *          Todos operan sobre muestras de tipo @c uint8_t (0 o 1) y devuelven
 *          @c digital_filter_binary_state_t (LOW, HIGH o PENDING).
 *
 *          ---
 *
 *          ### 1. Shift Register (recomendado para botones mecánicos)
 *
 *          Mantiene un registro de las últimas 8 muestras. El estado cambia
 *          solo cuando todas las muestras son iguales (0x00 o 0xFF).
 *
 *          - 8 muestras a 5 ms = 40 ms de ventana de debounce.
 *          - Cubre el 99% de los switches mecánicos.
 *          - Costo: 1 byte de estado, O(1) por muestra.
 *
 *          ---
 *
 *          ### 2. Integrador con contador (Ganssle)
 *
 *          Incrementa un contador cuando el pin está en HIGH, lo decrementa
 *          en LOW. El estado cambia al alcanzar un límite superior o inferior.
 *          Más suave frente a ruido variable que el shift register.
 *
 *          - Límites configurables en init().
 *          - Costo: 2 bytes de estado, O(1) por muestra.
 *
 *          ---
 *
 *          ### 3. Monoestable con retrigger
 *
 *          Cualquier cambio de muestra reinicia un contador descendente.
 *          El estado se acepta cuando el contador llega a 0. Minimiza
 *          la latencia en señales limpias.
 *
 *          - Tiempo de hold configurable en muestras.
 *          - Costo: 2 bytes de estado, O(1) por muestra.
 *
 *          ---
 *
 *          ### Ejemplo de uso — shift register para botón
 *
 *          @code
 *          #include "digital_filter_binary.h"
 *
 *          df_shift_t btn_filter;
 *          df_shift_init(&btn_filter, DIGITAL_FILTER_BINARY_HIGH);
 *
 *          // Llamar cada 5 ms desde soft_timer callback:
 *          uint8_t pin_level = (uint8_t)gpio_read_pin(BTN_PORT, BTN_PIN);
 *          digital_filter_binary_state_t state =
 *              df_shift_update(&btn_filter, pin_level);
 *
 *          if (state == DIGITAL_FILTER_BINARY_LOW)
 *          {
 *              // Botón presionado (pull-up activo)
 *          }
 *          @endcode
 *
 * @note    No requiere FPU. No usa memoria dinámica. Compatible con C99.
 *
 * @author  mcuframework contributors
 * @version 1.0.0
 */

#ifndef DIGITAL_FILTER_BINARY_H
#define DIGITAL_FILTER_BINARY_H

#include "digital_filter_common.h"

/*===========================================================================*
 * 1. Filtro Shift Register de 8 bits
 *===========================================================================*/

/** @brief Descriptor del filtro shift register binario. */
typedef struct
{
    uint8_t                       s_shift_reg;  /**< Registro de 8 muestras. */
    digital_filter_binary_state_t s_state;      /**< Estado confirmado actual.*/
} df_shift_t;

/**
 * @brief   Inicializa el filtro shift register.
 * @param[out] filter         Puntero al descriptor. No debe ser NULL.
 * @param[in]  initial_state  Estado inicial asumido (LOW o HIGH).
 *                            Determina el valor de precarga del shift register.
 */
static inline void df_shift_init(
    df_shift_t * const            filter,
    digital_filter_binary_state_t initial_state)
{
    filter->s_state     = initial_state;
    filter->s_shift_reg = (initial_state == DIGITAL_FILTER_BINARY_HIGH)
                          ? 0xFFu
                          : 0x00u;
}

/**
 * @brief   Procesa una muestra y devuelve el estado confirmado.
 * @param[in,out] filter   Puntero al descriptor. No debe ser NULL.
 * @param[in]     sample   Muestra binaria: 0 o 1.
 * @return  Estado confirmado o PENDING si las 8 muestras no son uniformes.
 */
static inline digital_filter_binary_state_t df_shift_update(
    df_shift_t * const filter,
    uint8_t            sample)
{
    filter->s_shift_reg = (uint8_t)((filter->s_shift_reg << 1u)
                                    | (sample & 0x01u));
    if (filter->s_shift_reg == 0xFFu)
    {
        filter->s_state = DIGITAL_FILTER_BINARY_HIGH;
    }
    else if (filter->s_shift_reg == 0x00u)
    {
        filter->s_state = DIGITAL_FILTER_BINARY_LOW;
    }
    else
    {
        return DIGITAL_FILTER_BINARY_PENDING;
    }
    return filter->s_state;
}

/**
 * @brief   Devuelve el estado confirmado actual sin nueva muestra.
 * @param[in] filter   Puntero al descriptor. No debe ser NULL.
 * @return   Estado confirmado actual (nunca PENDING).
 */
static inline digital_filter_binary_state_t df_shift_get(
    const df_shift_t * const filter)
{
    return filter->s_state;
}

/*===========================================================================*
 * 2. Filtro Integrador con contador (Ganssle)
 *===========================================================================*/

/** @brief Descriptor del filtro integrador binario (Ganssle). */
typedef struct
{
    digital_filter_binary_state_t s_state;      /**< Estado confirmado actual.*/
    uint8_t                       s_counter;    /**< Contador de integración. */
    uint8_t                       s_limit_high; /**< Umbral para confirmar HIGH.*/
    uint8_t                       s_limit_low;  /**< Umbral para confirmar LOW. */
} df_integrator_t;

/**
 * @brief   Inicializa el filtro integrador.
 * @param[out] filter         Puntero al descriptor. No debe ser NULL.
 * @param[in]  limit_high     Valor del contador para confirmar HIGH. Ej: 8.
 * @param[in]  limit_low      Valor del contador para confirmar LOW.  Ej: 0.
 * @param[in]  initial_state  Estado inicial.
 * @note   El contador arranca en (limit_high + limit_low) / 2 para que
 *         se necesiten el mismo número de muestras para confirmar cualquier estado.
 */
static inline void df_integrator_init(
    df_integrator_t * const       filter,
    uint8_t                       limit_high,
    uint8_t                       limit_low,
    digital_filter_binary_state_t initial_state)
{
    filter->s_limit_high = limit_high;
    filter->s_limit_low  = limit_low;
    filter->s_state      = initial_state;
    filter->s_counter    = (uint8_t)((limit_high + limit_low) / 2u);
}

/**
 * @brief   Procesa una muestra y actualiza el contador integrador.
 * @param[in,out] filter   Puntero al descriptor. No debe ser NULL.
 * @param[in]     sample   Muestra binaria: 0 o 1.
 * @return  Estado confirmado, o PENDING si el contador está entre los límites.
 */
static inline digital_filter_binary_state_t df_integrator_update(
    df_integrator_t * const filter,
    uint8_t                 sample)
{
    if (sample != 0u)
    {
        if (filter->s_counter < filter->s_limit_high)
        {
            filter->s_counter++;
        }
    }
    else
    {
        if (filter->s_counter > filter->s_limit_low)
        {
            filter->s_counter--;
        }
    }

    if (filter->s_counter >= filter->s_limit_high)
    {
        filter->s_state = DIGITAL_FILTER_BINARY_HIGH;
        return DIGITAL_FILTER_BINARY_HIGH;
    }

    if (filter->s_counter <= filter->s_limit_low)
    {
        filter->s_state = DIGITAL_FILTER_BINARY_LOW;
        return DIGITAL_FILTER_BINARY_LOW;
    }

    return DIGITAL_FILTER_BINARY_PENDING;
}

/**
 * @brief   Devuelve el estado confirmado actual sin nueva muestra.
 * @param[in] filter   Puntero al descriptor. No debe ser NULL.
 * @return   Estado confirmado actual.
 */
static inline digital_filter_binary_state_t df_integrator_get(
    const df_integrator_t * const filter)
{
    return filter->s_state;
}

/*===========================================================================*
 * 3. Filtro Monoestable con retrigger
 *===========================================================================*/

/** @brief Descriptor del filtro monoestable. */
typedef struct
{
    digital_filter_binary_state_t s_state;       /**< Estado confirmado actual.*/
    uint8_t                       s_last_sample; /**< Última muestra vista.    */
    uint8_t                       s_counter;     /**< Contador descendente.    */
    uint8_t                       s_hold_time;   /**< Tiempo de hold en muestras.*/
} df_monostable_t;

/**
 * @brief   Inicializa el filtro monoestable.
 * @param[out] filter         Puntero al descriptor. No debe ser NULL.
 * @param[in]  hold_samples   Número de muestras consecutivas estables
 *                            requeridas para confirmar un estado. Ej: 8.
 * @param[in]  initial_state  Estado inicial.
 */
static inline void df_monostable_init(
    df_monostable_t * const       filter,
    uint8_t                       hold_samples,
    digital_filter_binary_state_t initial_state)
{
    filter->s_hold_time   = hold_samples;
    filter->s_counter     = 0u;
    filter->s_state       = initial_state;
    filter->s_last_sample = (initial_state == DIGITAL_FILTER_BINARY_HIGH)
                            ? 1u : 0u;
}

/**
 * @brief   Procesa una muestra con lógica monoestable.
 * @param[in,out] filter   Puntero al descriptor. No debe ser NULL.
 * @param[in]     sample   Muestra binaria: 0 o 1.
 * @return  Estado confirmado, o PENDING mientras el contador no llega a 0.
 */
static inline digital_filter_binary_state_t df_monostable_update(
    df_monostable_t * const filter,
    uint8_t                 sample)
{
    if (sample != filter->s_last_sample)
    {
        /* Cualquier cambio reinicia el contador */
        filter->s_last_sample = sample;
        filter->s_counter     = filter->s_hold_time;
        return DIGITAL_FILTER_BINARY_PENDING;
    }

    if (filter->s_counter > 0u)
    {
        filter->s_counter--;
        if (filter->s_counter > 0u)
        {
            return DIGITAL_FILTER_BINARY_PENDING;
        }
    }

    /* Contador llegó a 0: confirmar estado */
    filter->s_state = (sample != 0u)
                      ? DIGITAL_FILTER_BINARY_HIGH
                      : DIGITAL_FILTER_BINARY_LOW;
    return filter->s_state;
}

/**
 * @brief   Devuelve el estado confirmado actual sin nueva muestra.
 * @param[in] filter   Puntero al descriptor. No debe ser NULL.
 * @return   Estado confirmado actual.
 */
static inline digital_filter_binary_state_t df_monostable_get(
    const df_monostable_t * const filter)
{
    return filter->s_state;
}

#endif /* DIGITAL_FILTER_BINARY_H */
