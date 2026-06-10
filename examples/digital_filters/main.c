/**
 * @file    main.c
 * @brief   Ejemplo completo de la librería digital_filter.
 *
 * @details Este archivo demuestra el uso de todos los filtros de la librería
 *          en escenarios representativos de sistemas embebidos reales.
 *          Está diseñado para compilarse con TARGET_GENERIC (host) y puede
 *          ejecutarse en cualquier PC para verificar el comportamiento.
 *
 *          ### Escenarios cubiertos
 *
 *          | Sección | Filtro       | Escenario simulado                        |
 *          |---------|--------------|-------------------------------------------|
 *          | 1       | EMA          | Suavizado de lectura de ADC (temperatura) |
 *          | 2       | SMA          | Promedio de sensor de corriente           |
 *          | 3       | Mediana      | Filtrado de picos en sensor ultrasónico   |
 *          | 4       | Histéresis   | Control de ventilador por temperatura     |
 *          | 5       | Slew rate    | Rampa suave de velocidad de motor         |
 *          | 6       | Peak HOLD    | Captura de vibración máxima               |
 *          | 7       | Peak DECAY   | Envolvente de señal de audio              |
 *          | 8       | Shift reg    | Debounce de botón con pull-up             |
 *          | 9       | Integrador   | Debounce de señal con ruido eléctrico     |
 *          | 10      | Monoestable  | Detección de flanco en encoder            |
 *          | 11      | EMA reset    | Precarga del acumulador para evitar rampa |
 *          | 12      | SMA reset    | Reinicio de ventana ante cambio de rango  |
 *          | 13      | Slew set_step| Ajuste dinámico de límite de pendiente    |
 *          | 14      | Hyst update  | Cambio de umbrales en runtime             |
 *          | 15      | Tipo custom  | Instancia EMA para int8_t                 |
 *
 *          ### Compilación standalone (host)
 *
 *          @code
 *          gcc -std=c99 -Wall -Wextra -I../../lib/digital_filter \
 *              example_digital_filter.c -o example_digital_filter
 *          ./example_digital_filter
 *          @endcode
 *
 * @note    Compatible con C99. No requiere hardware. No usa FPU.
 *
 * @author  mcuframework contributors
 * @version 1.0.0
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#include "digital_filter.h"

/*===========================================================================*
 * Instancia personalizada — EMA para int8_t
 * Debe declararse en scope de archivo, fuera de cualquier función.
 *===========================================================================*/

/**
 * @brief   Instancia EMA para int8_t con acumulador int32_t.
 * @details Demuestra que cualquier tipo puede instanciarse con la macro.
 */
DEFINE_EMA_FILTER(int8_t, int32_t, i8)

/*
 * Verificación estática de potencia de 2 para el buffer SMA del ejemplo.
 * Debe estar en scope de archivo (no dentro de una función) para que
 * el typedef sea válido en C99 sin generar warnings de tipo no usado.
 */
DIGITAL_FILTER_STATIC_ASSERT_POW2(8u);

/*===========================================================================*
 * Helpers de impresión
 *===========================================================================*/

/** @brief Convierte digital_filter_binary_state_t a string legible. */
static const char *state_to_str(digital_filter_binary_state_t state)
{
    switch (state)
    {
        case DIGITAL_FILTER_BINARY_LOW:     return "LOW    ";
        case DIGITAL_FILTER_BINARY_HIGH:    return "HIGH   ";
        case DIGITAL_FILTER_BINARY_PENDING: return "PENDING";
        default:                            return "???    ";
    }
}

/** @brief Imprime separador de sección con título. */
static void print_section(const char *title)
{
    printf("\n");
    printf("================================================================\n");
    printf("  %s\n", title);
    printf("================================================================\n");
}

/*===========================================================================*
 * Sección 1 — EMA: suavizado de lectura de ADC (temperatura)
 *
 * Escenario: ADC de temperatura en centésimas de grado (2500 = 25.00 °C).
 * La señal tiene ruido de ±50 cuentas. Se compara alpha lento vs rápido
 * y se demuestra get() sin nueva muestra.
 *===========================================================================*/
static void example_ema_adc_temperature(void)
{
    static const int16_t s_raw[] =
    {
        2510, 2495, 2520, 2480, 2505,   /* reposo ~2500  */
        2830, 2765, 2810, 2795, 2820,   /* escalon ~2800 */
        2800, 2815, 2795, 2808, 2802    /* estable ~2800 */
    };
    static const uint8_t s_num =
        (uint8_t)(sizeof(s_raw) / sizeof(s_raw[0]));

    ema_filter_i16_t slow_filter;
    ema_filter_i16_t fast_filter;
    uint8_t          i;

    print_section("1. EMA — suavizado ADC temperatura (centesimas de grado)");

    /*
     * Alpha lento: FRAC(1,20) ~ 0.05  tau largo, mucho suavizado.
     * Alpha rapido: FRAC(1,4) = 0.25  respuesta mas agil.
     * Ambos precargan el acumulador con el valor de reposo para
     * evitar el transitorio de arranque.
     */
    ema_filter_i16_init(&slow_filter,
                        DIGITAL_FILTER_FRAC_TO_Q16(1u, 20u),
                        s_raw[0]);
    ema_filter_i16_init(&fast_filter,
                        DIGITAL_FILTER_FRAC_TO_Q16(1u, 4u),
                        s_raw[0]);

    printf("  muestra | raw   | EMA lento | EMA rapido\n");
    printf("  --------|-------|-----------|----------\n");

    for (i = 0u; i < s_num; i++)
    {
        int16_t out_slow = ema_filter_i16_update(&slow_filter, s_raw[i]);
        int16_t out_fast = ema_filter_i16_update(&fast_filter, s_raw[i]);
        printf("  %7u | %5d | %9d | %10d\n",
               (unsigned)i, (int)s_raw[i],
               (int)out_slow, (int)out_fast);
    }

    /* get() — leer salida sin procesar nueva muestra */
    printf("  [get sin update] slow=%d  fast=%d\n",
           (int)ema_filter_i16_get(&slow_filter),
           (int)ema_filter_i16_get(&fast_filter));
}

/*===========================================================================*
 * Sección 2 — SMA: promedio de sensor de corriente
 *
 * Escenario: sensor de corriente en mA, ventana de 8 muestras.
 * Se demuestra el arranque progresivo (s_count < N), la ventana llena,
 * y el efecto de reset() ante un cambio de rango brusco.
 *===========================================================================*/
static void example_sma_current_sensor(void)
{
    static const uint16_t s_current[] =
    {
        1200u, 1180u, 1220u, 1195u,   /* arranque (count < 8) */
        1205u, 1190u, 1215u, 1200u,   /* ventana llena        */
        1800u, 1820u, 1790u, 1810u    /* cambio de carga      */
    };
    static const uint8_t s_num =
        (uint8_t)(sizeof(s_current) / sizeof(s_current[0]));

    uint16_t          buf[8];
    sma_filter_u16_t  filter;
    uint8_t           i;

    print_section("2. SMA — promedio sensor corriente (mA, ventana=8)");

    sma_filter_u16_init(&filter, buf, 8u);

    printf("  muestra | raw    | SMA\n");
    printf("  --------|--------|----\n");

    for (i = 0u; i < s_num; i++)
    {
        uint16_t out = sma_filter_u16_update(&filter, s_current[i]);
        printf("  %7u | %6u | %u\n",
               (unsigned)i, (unsigned)s_current[i], (unsigned)out);
    }

    printf("  [get antes reset] = %u mA\n",
           (unsigned)sma_filter_u16_get(&filter));

    sma_filter_u16_reset(&filter);

    printf("  [get tras  reset] = %u mA  (ventana limpia)\n",
           (unsigned)sma_filter_u16_get(&filter));
}

/*===========================================================================*
 * Sección 3 — Mediana: filtrado de picos en sensor ultrasónico
 *
 * Escenario: sensor HC-SR04 con ecos múltiples (lecturas espurias de 9999).
 * Ventana de 5 muestras (impar para mediana exacta).
 *===========================================================================*/
static void example_median_ultrasonic(void)
{
    static const uint16_t s_dist[] =
    {
        350u, 348u, 9999u, 352u, 349u,
        351u, 9999u, 347u, 350u, 353u
    };
    static const uint8_t s_num =
        (uint8_t)(sizeof(s_dist) / sizeof(s_dist[0]));

    uint16_t            buf[5];
    median_filter_u16_t filter;
    uint8_t             i;

    print_section("3. Mediana — sensor ultrasonico con spikes (mm)");

    median_filter_u16_init(&filter, buf, 5u);

    printf("  muestra | raw    | Mediana\n");
    printf("  --------|--------|--------\n");

    for (i = 0u; i < s_num; i++)
    {
        uint16_t out = median_filter_u16_update(&filter, s_dist[i]);
        printf("  %7u | %6u | %u%s\n",
               (unsigned)i, (unsigned)s_dist[i], (unsigned)out,
               (s_dist[i] == 9999u) ? "  <- spike eliminado" : "");
    }

    median_filter_u16_reset(&filter);
    printf("  [tras reset get] = %u\n",
           (unsigned)median_filter_u16_get(&filter));
}

/*===========================================================================*
 * Sección 4 — Histéresis: control de ventilador por temperatura
 *
 * Escenario: encender si temp > 60 °C, apagar si temp < 50 °C.
 * La señal oscila alrededor del umbral sin producir chatter.
 *===========================================================================*/
static void example_hyst_fan_control(void)
{
    static const int16_t s_temp[] =
    {
        480,  495,  510,  530,
        555,  580,  610,  625,
        605,  590,  575,
        545,  520,  495,  470
    };
    static const uint8_t s_num =
        (uint8_t)(sizeof(s_temp) / sizeof(s_temp[0]));

    hyst_filter_i16_t filter;
    uint8_t           i;

    print_section("4. Histeresis — control ventilador (decimas de grado C)");
    printf("  Umbral ON  > 600  (60.0 C)\n");
    printf("  Umbral OFF < 500  (50.0 C)\n\n");

    hyst_filter_i16_init(&filter, 500, 600, DIGITAL_FILTER_BINARY_LOW);

    printf("  muestra | temp  | ventilador\n");
    printf("  --------|-------|----------\n");

    for (i = 0u; i < s_num; i++)
    {
        digital_filter_binary_state_t state =
            hyst_filter_i16_update(&filter, s_temp[i]);
        printf("  %7u | %5d | %s\n",
               (unsigned)i, (int)s_temp[i],
               (state == DIGITAL_FILTER_BINARY_HIGH) ? "ON " : "OFF");
    }

    /* set_thresholds — ajuste en runtime */
    hyst_filter_i16_set_thresholds(&filter, 450, 550);
    printf("  [umbrales ajustados a 450/550 en runtime]\n");
    printf("  [get estado conservado] = %s\n",
           state_to_str(hyst_filter_i16_get(&filter)));
}

/*===========================================================================*
 * Sección 5 — Slew rate: rampa suave de velocidad de motor
 *
 * Escenario: consigna que salta de 0 a 1000 rpm.
 * Se limita la pendiente a 100 rpm/tick para proteger el accionamiento.
 *===========================================================================*/
static void example_slew_motor_ramp(void)
{
    slew_filter_i16_t filter;
    int16_t           setpoint;
    uint8_t           tick;

    print_section("5. Slew rate — rampa velocidad motor (rpm, step=100)");

    slew_filter_i16_init(&filter, 100, 0);

    printf("  tick | setpoint | salida\n");
    printf("  -----|----------|------\n");

    setpoint = 1000;
    for (tick = 0u; tick < 12u; tick++)
    {
        int16_t out = slew_filter_i16_update(&filter, setpoint);
        printf("  %4u | %8d | %d\n",
               (unsigned)tick, (int)setpoint, (int)out);
    }

    printf("  --- freno: setpoint=0 ---\n");
    setpoint = 0;
    for (; tick < 24u; tick++)
    {
        int16_t out = slew_filter_i16_update(&filter, setpoint);
        printf("  %4u | %8d | %d\n",
               (unsigned)tick, (int)setpoint, (int)out);
    }

    /* set_step — reducir pendiente en caliente */
    slew_filter_i16_set_step(&filter, 50);
    printf("  [set_step a 50 rpm/tick]\n");

    /* reset — sincronizar sin rampa de arranque */
    slew_filter_i16_reset(&filter, 500);
    printf("  [reset a 500 rpm] get=%d\n",
           (int)slew_filter_i16_get(&filter));
}

/*===========================================================================*
 * Sección 6 — Peak HOLD: captura de vibración máxima por ciclo
 *
 * Escenario: acelerómetro industrial. El pico se mantiene hasta que el
 * usuario llama reset() al final del ciclo de medición.
 *===========================================================================*/
static void example_peak_hold_vibration(void)
{
    static const int16_t s_vibr[] =
    {
        10, 25, 80, 120, 340, 280,
        195, 150, 90,  40,  15, 8
    };
    static const uint8_t s_num =
        (uint8_t)(sizeof(s_vibr) / sizeof(s_vibr[0]));

    peak_filter_i16_t filter;
    uint8_t           i;

    print_section("6. Peak HOLD — vibracion maxima por ciclo (cuentas ADC)");

    peak_filter_i16_init(&filter,
                         PEAK_FILTER_MODE_MAX,
                         PEAK_FILTER_HOLD,
                         0, 0);   /* initial=0, decay_step ignorado */

    printf("  muestra | accel | pico\n");
    printf("  --------|-------|-----\n");

    for (i = 0u; i < s_num; i++)
    {
        int16_t peak = peak_filter_i16_update(&filter, s_vibr[i]);
        printf("  %7u | %5d | %d\n",
               (unsigned)i, (int)s_vibr[i], (int)peak);
    }

    printf("  [fin ciclo] pico maximo = %d\n",
           (int)peak_filter_i16_get(&filter));

    peak_filter_i16_reset(&filter, 0);
    printf("  [reset] pico = %d  (listo para siguiente ciclo)\n",
           (int)peak_filter_i16_get(&filter));
}

/*===========================================================================*
 * Sección 7 — Peak DECAY: envolvente de señal de audio (VU meter)
 *
 * Escenario: el pico sigue la señal hacia arriba al instante y decae
 * 20 unidades/muestra cuando la señal baja.
 *===========================================================================*/
static void example_peak_decay_envelope(void)
{
    static const int16_t s_audio[] =
    {
        100, 250, 480, 720, 900, 850,
        600, 400, 200, 100,  50,  30
    };
    static const uint8_t s_num =
        (uint8_t)(sizeof(s_audio) / sizeof(s_audio[0]));

    peak_filter_i16_t filter;
    uint8_t           i;

    print_section("7. Peak DECAY — envolvente VU meter (decay=20/muestra)");

    peak_filter_i16_init(&filter,
                         PEAK_FILTER_MODE_MAX,
                         PEAK_FILTER_DECAY,
                         0, 20);

    printf("  muestra | señal | envolvente\n");
    printf("  --------|-------|----------\n");

    for (i = 0u; i < s_num; i++)
    {
        int16_t env = peak_filter_i16_update(&filter, s_audio[i]);
        printf("  %7u | %5d | %d\n",
               (unsigned)i, (int)s_audio[i], (int)env);
    }
}

/*===========================================================================*
 * Sección 8 — Shift register: debounce de botón con pull-up
 *
 * Escenario: pin con pull-up, botón normalmente HIGH. Al presionar hay
 * bounce de 2-3 pulsos. Se muestrea cada 5 ms. Se necesitan 8 muestras
 * consecutivas iguales para confirmar cambio.
 *===========================================================================*/
static void example_binary_shift_button(void)
{
    static const uint8_t s_pin[] =
    {
        1u, 1u, 1u,
        0u, 1u, 0u, 1u, 0u, 0u, 0u, 0u, 0u,   /* bounce + confirm LOW  */
        1u, 0u, 1u, 1u, 1u, 1u, 1u, 1u, 1u    /* bounce + confirm HIGH */
    };
    static const uint8_t s_num =
        (uint8_t)(sizeof(s_pin) / sizeof(s_pin[0]));

    df_shift_t btn;
    uint8_t    i;

    print_section("8. Shift register — debounce boton pull-up (5 ms/muestra)");

    df_shift_init(&btn, DIGITAL_FILTER_BINARY_HIGH);

    printf("  muestra | pin | estado\n");
    printf("  --------|-----|-------\n");

    for (i = 0u; i < s_num; i++)
    {
        digital_filter_binary_state_t state = df_shift_update(&btn, s_pin[i]);
        printf("  %7u |  %u  | %s\n",
               (unsigned)i, (unsigned)s_pin[i], state_to_str(state));
    }

    printf("  [get estado actual] = %s\n",
           state_to_str(df_shift_get(&btn)));
}

/*===========================================================================*
 * Sección 9 — Integrador Ganssle: señal con ruido eléctrico
 *
 * Escenario: señal digital con ráfagas de interferencia. El integrador
 * acumula confianza antes de confirmar el cambio de estado.
 *===========================================================================*/
static void example_binary_integrator_noisy(void)
{
    static const uint8_t s_sig[] =
    {
        1u, 1u, 0u, 1u, 1u, 1u, 0u, 1u,   /* mayoria HIGH con ruido */
        0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,   /* transicion a LOW       */
        1u, 0u, 1u, 0u, 1u, 1u, 1u, 1u    /* recuperacion con ruido */
    };
    static const uint8_t s_num =
        (uint8_t)(sizeof(s_sig) / sizeof(s_sig[0]));

    df_integrator_t filter;
    uint8_t         i;

    print_section("9. Integrador Ganssle — señal con ruido electrico");
    printf("  limit_low=0  limit_high=8\n\n");

    df_integrator_init(&filter, 8u, 0u, DIGITAL_FILTER_BINARY_HIGH);

    printf("  muestra | señal | estado\n");
    printf("  --------|-------|-------\n");

    for (i = 0u; i < s_num; i++)
    {
        digital_filter_binary_state_t state =
            df_integrator_update(&filter, s_sig[i]);
        printf("  %7u |   %u   | %s\n",
               (unsigned)i, (unsigned)s_sig[i], state_to_str(state));
    }

    printf("  [get estado actual] = %s\n",
           state_to_str(df_integrator_get(&filter)));
}

/*===========================================================================*
 * Sección 10 — Monoestable: detección de flanco en encoder óptico
 *
 * Escenario: señal de encoder con bounce corto al cambiar de estado.
 * El monoestable requiere 5 muestras estables consecutivas.
 *===========================================================================*/
static void example_binary_monostable_encoder(void)
{
    static const uint8_t s_enc[] =
    {
        1u, 1u, 1u,
        0u, 1u, 0u,                   /* bounce al bajar   */
        0u, 0u, 0u, 0u, 0u,           /* 5 muestras LOW    */
        1u, 0u, 1u,                   /* bounce al subir   */
        1u, 1u, 1u, 1u, 1u            /* 5 muestras HIGH   */
    };
    static const uint8_t s_num =
        (uint8_t)(sizeof(s_enc) / sizeof(s_enc[0]));

    df_monostable_t filter;
    uint8_t         i;

    print_section("10. Monoestable — encoder optico (hold=5 muestras)");

    df_monostable_init(&filter, 5u, DIGITAL_FILTER_BINARY_HIGH);

    printf("  muestra | enc | estado\n");
    printf("  --------|-----|-------\n");

    for (i = 0u; i < s_num; i++)
    {
        digital_filter_binary_state_t state =
            df_monostable_update(&filter, s_enc[i]);
        printf("  %7u |  %u  | %s\n",
               (unsigned)i, (unsigned)s_enc[i], state_to_str(state));
    }

    printf("  [get estado actual] = %s\n",
           state_to_str(df_monostable_get(&filter)));
}

/*===========================================================================*
 * Sección 11 — EMA reset(): precarga para evitar transitorio de arranque
 *
 * Escenario: se compara un filtro precargado con la primera lectura real
 * contra uno que arranca desde cero.
 *===========================================================================*/
static void example_ema_reset_preload(void)
{
    ema_filter_i32_t filter_pre;
    ema_filter_i32_t filter_zero;
    uint8_t          i;

    print_section("11. EMA reset() — precarga vs arranque desde cero");

    ema_filter_i32_init(&filter_zero,
                        DIGITAL_FILTER_FRAC_TO_Q16(1u, 8u), 0);

    ema_filter_i32_init(&filter_pre,
                        DIGITAL_FILTER_FRAC_TO_Q16(1u, 8u), 0);
    ema_filter_i32_reset(&filter_pre, 5000);   /* precarga con valor real */

    printf("  muestra | raw   | desde_0 | precargado\n");
    printf("  --------|-------|---------|----------\n");

    for (i = 0u; i < 10u; i++)
    {
        int32_t out_z = ema_filter_i32_update(&filter_zero, 5000);
        int32_t out_p = ema_filter_i32_update(&filter_pre,  5000);
        printf("  %7u | %5d | %7d | %d\n",
               (unsigned)i, 5000, (int)out_z, (int)out_p);
    }
}

/*===========================================================================*
 * Sección 12 — SMA reset(): cambio de rango de señal
 *
 * Escenario: el ADC cambia de escala. Sin reset, el buffer antiguo
 * contamina el promedio por N ticks.
 *===========================================================================*/
static void example_sma_reset_range_change(void)
{
    uint32_t         buf[4];
    sma_filter_u32_t filter;
    uint8_t          i;

    print_section("12. SMA reset() — cambio de rango de señal");

    sma_filter_u32_init(&filter, buf, 4u);

    printf("  [rango A: ~1000]\n");
    for (i = 0u; i < 4u; i++)
    {
        uint32_t out = sma_filter_u32_update(&filter, 1000u + (uint32_t)i);
        printf("  tick %u  raw=%u  sma=%u\n",
               (unsigned)i, (unsigned)(1000u + i), (unsigned)out);
    }

    printf("  [reset antes de cambiar a rango B: ~50000]\n");
    sma_filter_u32_reset(&filter);

    for (i = 0u; i < 4u; i++)
    {
        uint32_t out = sma_filter_u32_update(&filter, 50000u + (uint32_t)i);
        printf("  tick %u  raw=%u  sma=%u\n",
               (unsigned)i, (unsigned)(50000u + i), (unsigned)out);
    }
}

/*===========================================================================*
 * Sección 13 — Slew set_step(): cambio dinámico de pendiente
 *
 * Escenario: motor con modo normal (step=50) y modo emergencia (step=200).
 *===========================================================================*/
static void example_slew_dynamic_step(void)
{
    slew_filter_i16_t filter;
    uint8_t           tick;

    print_section("13. Slew set_step() — pendiente dinamica");

    slew_filter_i16_init(&filter, 50, 0);

    printf("  [modo normal: step=50]\n");
    for (tick = 0u; tick < 6u; tick++)
    {
        int16_t out = slew_filter_i16_update(&filter, 1000);
        printf("  tick %u: out=%d\n", (unsigned)tick, (int)out);
    }

    slew_filter_i16_set_step(&filter, 200);
    printf("  [modo emergencia: step=200]\n");
    for (; tick < 12u; tick++)
    {
        int16_t out = slew_filter_i16_update(&filter, 1000);
        printf("  tick %u: out=%d\n", (unsigned)tick, (int)out);
    }
}

/*===========================================================================*
 * Sección 14 — Hyst set_thresholds(): cambio de umbrales en runtime
 *
 * Escenario: termostato con dos modos operativos. Los umbrales cambian
 * sin reiniciar el estado actual del controlador.
 *===========================================================================*/
static void example_hyst_dynamic_thresholds(void)
{
    hyst_filter_i16_t filter;

    print_section("14. Hyst set_thresholds() — umbrales dinamicos termostato");

    /* Modo confort: ON < 200 (20.0 C), OFF > 220 (22.0 C) */
    hyst_filter_i16_init(&filter, 200, 220, DIGITAL_FILTER_BINARY_LOW);

    printf("  [modo confort: ON<200, OFF>220]\n");
    printf("  temp=190: %s\n",
           state_to_str(hyst_filter_i16_update(&filter, 190)));
    printf("  temp=215: %s  (dentro de banda, no cambia)\n",
           state_to_str(hyst_filter_i16_update(&filter, 215)));
    printf("  temp=225: %s\n",
           state_to_str(hyst_filter_i16_update(&filter, 225)));
    printf("  temp=210: %s  (dentro de banda, no cambia)\n",
           state_to_str(hyst_filter_i16_update(&filter, 210)));

    /* Cambiar a modo ahorro sin reiniciar el estado */
    hyst_filter_i16_set_thresholds(&filter, 160, 200);
    printf("  [modo ahorro: ON<160, OFF>200 — estado conservado]\n");
    printf("  [get] = %s\n",
           state_to_str(hyst_filter_i16_get(&filter)));
    printf("  temp=155: %s\n",
           state_to_str(hyst_filter_i16_update(&filter, 155)));
    printf("  temp=205: %s\n",
           state_to_str(hyst_filter_i16_update(&filter, 205)));
}

/*===========================================================================*
 * Sección 15 — Instancia custom: EMA para int8_t
 *
 * Escenario: sensor compacto que entrega int8_t. Demuestra que la macro
 * DEFINE_EMA_FILTER instancia correctamente para cualquier tipo primitivo.
 *===========================================================================*/
static void example_ema_custom_type_i8(void)
{
    static const int8_t s_temp[] =
    {
        25, 26, 24, 27, 25, 26, 80, 25, 26, 25   /* spike en muestra 6 */
    };
    static const uint8_t s_num =
        (uint8_t)(sizeof(s_temp) / sizeof(s_temp[0]));

    ema_filter_i8_t filter;
    uint8_t         i;

    print_section("15. EMA tipo custom (int8_t) — sensor temperatura compacto");

    ema_filter_i8_init(&filter,
                       DIGITAL_FILTER_FRAC_TO_Q16(1u, 5u),
                       s_temp[0]);

    printf("  muestra | raw | EMA_i8\n");
    printf("  --------|-----|-------\n");

    for (i = 0u; i < s_num; i++)
    {
        int8_t out = ema_filter_i8_update(&filter, s_temp[i]);
        printf("  %7u | %3d | %d%s\n",
               (unsigned)i, (int)s_temp[i], (int)out,
               (s_temp[i] == 80) ? "  <- spike atenuado" : "");
    }
}

/*===========================================================================*
 * main
 *===========================================================================*/

/**
 * @brief   Punto de entrada del ejemplo.
 * @return  0 siempre (TARGET_GENERIC / host).
 */
int main(void)
{
    printf("================================================================\n");
    printf("  mcuframework — digital_filter — ejemplo completo\n");
    printf("================================================================\n");

    example_ema_adc_temperature();
    example_sma_current_sensor();
    example_median_ultrasonic();
    example_hyst_fan_control();
    example_slew_motor_ramp();
    example_peak_hold_vibration();
    example_peak_decay_envelope();
    example_binary_shift_button();
    example_binary_integrator_noisy();
    example_binary_monostable_encoder();
    example_ema_reset_preload();
    example_sma_reset_range_change();
    example_slew_dynamic_step();
    example_hyst_dynamic_thresholds();
    example_ema_custom_type_i8();

    printf("\n================================================================\n");
    printf("  Fin del ejemplo. Todos los filtros ejecutados sin errores.\n");
    printf("================================================================\n");

    return 0;
}
