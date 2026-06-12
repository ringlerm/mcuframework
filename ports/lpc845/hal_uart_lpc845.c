/**
 * @file    hal_uart_lpc845.c
 * @brief   HAL UART port implementation for NXP LPC845 (Cortex-M0+).
 *
 * @details Implements the @ref hal_uart.h interface for the five USART
 *          peripherals available on the LPC845.
 *
 *          ### Hardware resources used
 *
 *          | Instance     | Base address  | SYSAHBCLKCTRL bit | PRESETCTRL bit | NVIC IRQ |
 *          |--------------|---------------|-------------------|----------------|----------|
 *          | HAL_UART_ID_0 (USART0) | 0x40064000 | 14 | 3 | 3 |
 *          | HAL_UART_ID_1 (USART1) | 0x40068000 | 15 | 4 | 4 |
 *          | HAL_UART_ID_2 (USART2) | 0x4006C000 | 16 | 5 | 5 |
 *          | HAL_UART_ID_3 (USART3) | 0x40070000 | 17 | 6 | 6 |
 *          | HAL_UART_ID_4 (USART4) | 0x40074000 | 18 | 7 | 7 |
 *
 *          ### Clock architecture (UM11029 §13, §18)
 *
 *          @code
 *          MainClock
 *              │
 *              ▼
 *          SYSCON->UARTCLKDIV      (÷ 1..255, reset = 1)
 *              │
 *              ▼
 *          FRG (Fractional Rate Generator)
 *              FRGDIV  = 0xFF  (fixed, denominator = 256)
 *              FRGMULT = M     (0..255, nominator)
 *              Output  = input * 256 / (256 + M)
 *              │
 *              ▼
 *          USART BRG register    (÷ BRG+1)
 *              │
 *              ▼
 *          OSR register          (÷ OSR+1, default 15 → 16× oversampling)
 *              │
 *              ▼
 *          Baud clock
 *          @endcode
 *
 *          Baud rate formula:
 *          @code
 *          baud = (MainClock / UARTCLKDIV) * 256
 *                 / (256 + FRGMULT)
 *                 / (BRG + 1)
 *                 / (OSR + 1)
 *          @endcode
 *
 *          This port sets UARTCLKDIV = 1 and uses 16× oversampling (OSR = 15).
 *          It configures the FRG and BRG to minimise baud-rate error using the
 *          two-step algorithm described in UM11029 §13.3:
 *          1. Compute the ideal integer divisor  D = MainClock / (baud × 16).
 *          2. Set BRG = D - 1.
 *          3. Compute FRGMULT to absorb the fractional remainder.
 *
 *          ### ISR strategy
 *
 *          - **RX**: RXRDY interrupt is always enabled after init.  The ISR
 *            reads RXDAT and pushes into the RX queue.  Overflow (queue full)
 *            silently discards the incoming byte; no error flag is set in this
 *            version.
 *
 *          - **TX**: TXRDY interrupt is enabled only when bytes are enqueued
 *            and disabled automatically when the TX queue empties.  This avoids
 *            spurious interrupts and does not waste CPU cycles polling.
 *
 *          ### Switch matrix note
 *
 *          This file does NOT configure the switch matrix (SWM) or IOCON.
 *          Pin assignment must be done by the application before calling
 *          @ref hal_uart_init, because pin selection is board-specific and
 *          outside the scope of a peripheral driver.
 *
 * @note    This file must only be compiled when TARGET_LPC845 is defined.
 *          A compile-time guard at the top enforces this.
 *
 * @note    Includes only @c "../../target.h".  Never includes LPC845.h
 *          directly.  The device header is pulled in transitively through
 *          target.h as required by the project porting rules.
 *
 * @author  mcuframework contributors
 * @version 1.0.0
 */

/*===========================================================================*
 * Target guard — compile-time enforcement
 *===========================================================================*/

#include "../../target.h"

#ifndef TARGET_LPC845
#error "Este archivo solo debe compilarse con TARGET_LPC845 definido en target.h"
#endif

/*===========================================================================*
 * Includes
 *===========================================================================*/

#include "../../hal/hal_uart.h"
#include "../../lib/circular_queue/circular_queue.h"
#include <stddef.h>
#include <string.h>     /* memset */

/*===========================================================================*
 * Private constants — register bit definitions
 * -------------------------------------------------------------------------
 * All values taken from UM11029 Rev 1.7, Chapter 18 (USART).
 * Register offsets match the LPC845 CMSIS USART_Type struct layout.
 *===========================================================================*/

/* --- CFG register (offset 0x000) ---------------------------------------- */
/** @brief CFG bit 0: enable the USART peripheral. */
#define USART_CFG_ENABLE            (1u << 0u)
/** @brief CFG bits [3:2]: data length — 01 = 7 bit, 10 = 8 bit (no, wait) */
/*
 * UM11029 Table 318 — CFG[3:2] DATALEN:
 *   00 = 7 data bits
 *   01 = 8 data bits  ← 8N1 default
 *   10 = 9 data bits
 */
#define USART_CFG_DATALEN_7         (0x00u << 2u)
#define USART_CFG_DATALEN_8         (0x01u << 2u)
#define USART_CFG_DATALEN_9         (0x02u << 2u)
/** @brief CFG bits [5:4]: parity. UM11029 Table 318 — CFG[5:4] PARITYSEL. */
/*
 *   00 = No parity     ← 8N1 default
 *   01 = reserved
 *   10 = Even parity
 *   11 = Odd parity
 */
#define USART_CFG_PARITY_NONE       (0x00u << 4u)
#define USART_CFG_PARITY_EVEN       (0x02u << 4u)
#define USART_CFG_PARITY_ODD        (0x03u << 4u)
/** @brief CFG bit 6: stop bits. UM11029 Table 318 — CFG[6] STOPLEN. */
/*
 *   0 = 1 stop bit    ← 8N1 default
 *   1 = 2 stop bits
 */
#define USART_CFG_STOPLEN_1         (0x00u << 6u)
#define USART_CFG_STOPLEN_2         (0x01u << 6u)

/* --- STAT register (offset 0x004) --------------------------------------- */
/** @brief STAT bit 0: RXRDY — receive data available in RXDAT. */
#define USART_STAT_RXRDY            (1u << 0u)
/** @brief STAT bit 2: TXRDY — TX data register is ready to accept new data. */
#define USART_STAT_TXRDY            (1u << 2u)
/** @brief STAT bit 3: TXIDLE — transmitter and shift register both idle. */
#define USART_STAT_TXIDLE           (1u << 3u)

/* --- INTENSET register (offset 0x008) ----------------------------------- */
/** @brief INTENSET bit 0: enable RXRDY interrupt. Write 1 to set. */
#define USART_INTENSET_RXRDYEN      (1u << 0u)
/** @brief INTENSET bit 2: enable TXRDY interrupt. Write 1 to set. */
#define USART_INTENSET_TXRDYEN      (1u << 2u)

/* --- INTENCLR register (offset 0x010) ----------------------------------- */
/** @brief INTENCLR bit 0: clear RXRDY interrupt enable. Write 1 to clear. */
#define USART_INTENCLR_RXRDYCLR     (1u << 0u)
/** @brief INTENCLR bit 2: clear TXRDY interrupt enable. Write 1 to clear. */
#define USART_INTENCLR_TXRDYCLR     (1u << 2u)

/*===========================================================================*
 * Private constants — SYSCON bit definitions
 * -------------------------------------------------------------------------
 * UM11029 Rev 1.7, Chapter 8 (SYSCON).
 *===========================================================================*/

/**
 * @brief   SYSAHBCLKCTRL bit positions for each USART peripheral.
 *
 * @details UM11029 Table 54 — SYSAHBCLKCTRL bit description:
 *          Bit 14: UART0, Bit 15: UART1, Bit 16: UART2,
 *          Bit 17: UART3, Bit 18: UART4.
 */
static const uint8_t s_clk_bit[5u] =
{
    14u,    /* USART0 */
    15u,    /* USART1 */
    16u,    /* USART2 */
    17u,    /* USART3 */
    18u,    /* USART4 */
};

/**
 * @brief   PRESETCTRL bit positions for each USART peripheral.
 *
 * @details UM11029 Table 50 — PRESETCTRL bit description:
 *          Bit 3: UART0, Bit 4: UART1, Bit 5: UART2,
 *          Bit 6: UART3, Bit 7: UART4.
 */
static const uint8_t s_rst_bit[5u] =
{
    3u,     /* USART0 */
    4u,     /* USART1 */
    5u,     /* USART2 */
    6u,     /* USART3 */
    7u,     /* USART4 */
};

/**
 * @brief   NVIC IRQ numbers for each USART peripheral.
 *
 * @details UM11029 Table 108 — NVIC interrupt connections:
 *          UART0_IRQ = 3, UART1_IRQ = 4, UART2_IRQ = 5,
 *          UART3_IRQ = 6, UART4_IRQ = 7.
 */
static const IRQn_Type s_irq[5u] =
{
    USART0_IRQn,    /* HAL_UART_ID_0 */
    USART1_IRQn,    /* HAL_UART_ID_1 */
    USART2_IRQn,    /* HAL_UART_ID_2 */
    USART3_IRQn,    /* HAL_UART_ID_3 */
    USART4_IRQn,    /* HAL_UART_ID_4 */
};

/** @brief Maximum number of UART instances available on the LPC845. */
#define HAL_UART_LPC845_INSTANCE_COUNT  (5u)

/** @brief Oversampling ratio (16× — OSR register value = ratio - 1). */
#define HAL_UART_OSR_VALUE              (15u)   /* 16× oversampling */

/** @brief UARTCLKDIV: divide main clock by 1 before the FRG. */
#define HAL_UART_UARTCLKDIV             (1u)

/*===========================================================================*
 * Private types
 *===========================================================================*/

/**
 * @brief   Internal descriptor for one UART instance.
 *
 * @details Holds the queue control blocks and backing buffers for TX and RX,
 *          a pointer to the hardware peripheral registers, and a flag
 *          indicating whether the instance has been initialised.
 */
typedef struct
{
    USART_Type *    p_regs;         /**< Pointer to hardware register block.  */
    cqueue_t        tx_queue;       /**< TX circular queue control block.     */
    cqueue_t        rx_queue;       /**< RX circular queue control block.     */
    uint8_t         tx_buf[HAL_UART_TX_QUEUE_SIZE]; /**< TX backing store.   */
    uint8_t         rx_buf[HAL_UART_RX_QUEUE_SIZE]; /**< RX backing store.   */
    bool            initialised;    /**< True after successful hal_uart_init. */
} hal_uart_instance_t;

/*===========================================================================*
 * Private storage — static pool of instances
 *===========================================================================*/

/**
 * @brief   Static pool of UART descriptors — one per physical peripheral.
 *
 * @details Zero-initialised at program startup.  Each instance is activated
 *          by @ref hal_uart_init.
 */
static hal_uart_instance_t s_instances[HAL_UART_LPC845_INSTANCE_COUNT];

/**
 * @brief   Register base addresses for each USART peripheral.
 *
 * @details Taken from the LPC845 memory map (UM11029 §2.2):
 *          USART0 = 0x40064000, USART1 = 0x40068000, ...
 *          The CMSIS header defines LPC_USART0 through LPC_USART4 which
 *          resolve to the same values.
 */
static USART_Type * const s_regs[HAL_UART_LPC845_INSTANCE_COUNT] =
{
    LPC_USART0,     /* HAL_UART_ID_0 — base 0x40064000 */
    LPC_USART1,     /* HAL_UART_ID_1 — base 0x40068000 */
    LPC_USART2,     /* HAL_UART_ID_2 — base 0x4006C000 */
    LPC_USART3,     /* HAL_UART_ID_3 — base 0x40070000 */
    LPC_USART4,     /* HAL_UART_ID_4 — base 0x40074000 */
};

/*===========================================================================*
 * Private helpers
 *===========================================================================*/

/**
 * @brief   Validate a uart_id and return true if it is in range.
 *
 * @param[in]   uart_id     ID to validate.
 * @return  @c true if valid, @c false otherwise.
 */
static bool is_valid_id(hal_uart_id_t uart_id)
{
    return ((uint32_t)uart_id < (uint32_t)HAL_UART_LPC845_INSTANCE_COUNT);
}

/**
 * @brief   Configure the FRG and BRG for a target baud rate.
 *
 * @details Uses 16× oversampling (OSR = 15) and UARTCLKDIV = 1.
 *
 *          Algorithm (UM11029 §13.3):
 *          1. UARTclk = SystemCoreClock / UARTCLKDIV  (= SystemCoreClock)
 *          2. D = UARTclk / (baud_rate × 16)  — ideal integer divisor
 *          3. BRG = D - 1
 *          4. Fractional adjustment:
 *             FRGDIV  = 0xFF (denominator fixed at 256)
 *             FRGMULT = round(UARTclk * 256 / (baud_rate * 16 * D)) - 256
 *             The FRG output clock = UARTclk * 256 / (256 + FRGMULT)
 *             which is then divided by (BRG+1)*(OSR+1) to yield the baud.
 *
 * @param[in]   baud_rate   Desired baud rate in bps.
 *
 * @retval  HAL_UART_OK   Configuration applied.
 * @retval  HAL_UART_ERR  Baud rate cannot be achieved (clock too low).
 */
static hal_uart_status_t configure_baud(uint32_t baud_rate)
{
    uint32_t uart_clk;
    uint32_t d;
    uint32_t frg_mult;

    if (baud_rate == 0u)
    {
        return HAL_UART_ERR;
    }

    /*
     * Step 1 — Set UARTCLKDIV = 1 (pass main clock straight to FRG).
     *          UARTCLKDIV is a global divider shared by all USART instances.
     *          Writing it here affects all instances; callers are responsible
     *          for consistency if multiple USARTs run at different rates.
     *
     *          UM11029 Table 60 — UARTCLKDIV: bits [7:0], value 0 = disabled.
     */
    LPC_SYSCON->UARTCLKDIV = HAL_UART_UARTCLKDIV;

    uart_clk = SystemCoreClock / HAL_UART_UARTCLKDIV;

    /*
     * Step 2 — Compute the integer divisor D.
     *
     *          D = uart_clk / (baud_rate * (OSR+1))
     *            = uart_clk / (baud_rate * 16)
     *
     *          D must be >= 1.
     */
    d = uart_clk / (baud_rate * (HAL_UART_OSR_VALUE + 1u));

    if (d == 0u)
    {
        return HAL_UART_ERR;    /* Clock too slow for requested baud rate. */
    }

    /*
     * Step 3 — Configure the FRG.
     *
     *          FRGDIV  = 0xFF  → denominator = 256 (fixed by design).
     *          FRGMULT = M     → FRG output = uart_clk * 256 / (256 + M).
     *
     *          We want FRG output such that:
     *              FRG_out / ((BRG+1) * (OSR+1)) = baud_rate
     *          ⟹  FRG_out = baud_rate * d * 16
     *
     *          From FRG_out = uart_clk * 256 / (256 + M):
     *              256 + M = uart_clk * 256 / FRG_out
     *              M = (uart_clk * 256 / (baud_rate * d * 16)) - 256
     *
     *          Using integer arithmetic (no FPU):
     *              numerator   = uart_clk * 256
     *              denominator = baud_rate * d * (OSR+1)
     *              M = numerator / denominator - 256
     *
     *          M must be in [0, 255]; clamp to valid range.
     *
     *          UM11029 Table 68 — UARTFRGDIV, Table 69 — UARTFRGMULT.
     */
    LPC_SYSCON->UARTFRGDIV = 0xFFu;

    frg_mult = ((uart_clk * 256u) / (baud_rate * d * (HAL_UART_OSR_VALUE + 1u)))
               - 256u;

    if (frg_mult > 255u)
    {
        frg_mult = 255u;    /* Clamp — slight baud error, still operational. */
    }

    LPC_SYSCON->UARTFRGMULT = frg_mult;

    /*
     * Step 4 — Write BRG and OSR to every instance that is being initialised.
     *          The caller writes these after calling this function because only
     *          the specific peripheral's registers need updating, not all five.
     *          This function only touches the global SYSCON FRG registers.
     *          BRG and OSR are set per-instance in hal_uart_init.
     */

    return HAL_UART_OK;
}

/**
 * @brief   Build the CFG register value for a given frame configuration.
 *
 * @param[in]   p_config    Desired frame configuration.  NULL → 8N1.
 * @return  32-bit CFG value (without ENABLE bit).
 */
static uint32_t build_cfg(const hal_uart_config_t * p_config)
{
    uint32_t cfg = 0u;

    /* Data bits */
    if (p_config == NULL)
    {
        cfg |= USART_CFG_DATALEN_8;
    }
    else
    {
        switch (p_config->data_bits)
        {
            case HAL_UART_DATABITS_7:
                cfg |= USART_CFG_DATALEN_7;
                break;
            case HAL_UART_DATABITS_9:
                cfg |= USART_CFG_DATALEN_9;
                break;
            case HAL_UART_DATABITS_8: /* fall-through */
            default:
                cfg |= USART_CFG_DATALEN_8;
                break;
        }

        /* Parity */
        switch (p_config->parity)
        {
            case HAL_UART_PARITY_EVEN:
                cfg |= USART_CFG_PARITY_EVEN;
                break;
            case HAL_UART_PARITY_ODD:
                cfg |= USART_CFG_PARITY_ODD;
                break;
            case HAL_UART_PARITY_NONE: /* fall-through */
            default:
                cfg |= USART_CFG_PARITY_NONE;
                break;
        }

        /* Stop bits */
        if (p_config->stop_bits == HAL_UART_STOPBITS_2)
        {
            cfg |= USART_CFG_STOPLEN_2;
        }
        else
        {
            cfg |= USART_CFG_STOPLEN_1;
        }
    }

    return cfg;
}

/**
 * @brief   Common ISR body — called from each USARTx_IRQHandler.
 *
 * @details Handles both RX (RXRDY) and TX (TXRDY) in a single function to
 *          avoid code duplication across the five ISRs.
 *
 *          RX path: reads RXDAT and pushes into rx_queue.
 *          TX path: pops from tx_queue and writes to TXDAT; if queue empties,
 *                   disables the TXRDY interrupt via INTENCLR.
 *
 * @param[in]   idx     Index into s_instances[] (0..4).
 */
static void uart_isr_common(uint32_t idx)
{
    hal_uart_instance_t * const p_inst = &s_instances[idx];
    USART_Type * const          p_regs = p_inst->p_regs;
    uint8_t                     byte;

    /* --- RX path --------------------------------------------------------- */
    if ((p_regs->STAT & USART_STAT_RXRDY) != 0u)
    {
        /*
         * Read RXDAT to clear the RXRDY flag.
         * Bits [8:0] hold the received data; bit8 is valid only for 9-bit
         * frames.  Masking to uint8_t is correct for 7/8-bit frames.
         *
         * If the RX queue is full, the incoming byte is discarded.  This
         * prevents the queue from blocking the ISR; a future version may
         * add an overrun flag.
         */
        byte = (uint8_t)(p_regs->RXDAT & 0xFFu);
        (void)cqueue_push(&p_inst->rx_queue, &byte);
    }

    /* --- TX path --------------------------------------------------------- */
    if ((p_regs->STAT & USART_STAT_TXRDY) != 0u)
    {
        /*
         * Check whether TXRDY interrupt is actually enabled before acting.
         * INTSTAT reflects enabled interrupts only, so reading it avoids
         * a spurious pop when the TXRDY flag fires but the interrupt was
         * already disabled by flush_tx.
         *
         * UM11029 §18.6.7 — INTSTAT: mirrors INTENSET masked with STAT.
         */
        if ((p_regs->INTENSET & USART_INTENSET_TXRDYEN) != 0u)
        {
            if (cqueue_pop(&p_inst->tx_queue, &byte) == CQUEUE_OK)
            {
                /* Write byte to transmit data register. */
                p_regs->TXDAT = (uint32_t)byte;
            }
            else
            {
                /*
                 * Queue is empty — disable TXRDY interrupt.
                 * Writing 1 to INTENCLR bit 2 clears the TXRDYEN bit in
                 * INTENSET without affecting any other interrupt enables.
                 * UM11029 §18.6.4 — INTENCLR.
                 */
                p_regs->INTENCLR = USART_INTENCLR_TXRDYCLR;
            }
        }
    }
}

/*===========================================================================*
 * ISR definitions — one per USART peripheral
 *===========================================================================*/

/**
 * @brief   USART0 interrupt service routine.
 * @note    IRQ number 3 (UM11029 Table 108).
 */
void USART0_IRQHandler(void)
{
    uart_isr_common(0u);
}

/**
 * @brief   USART1 interrupt service routine.
 * @note    IRQ number 4.
 */
void USART1_IRQHandler(void)
{
    uart_isr_common(1u);
}

/**
 * @brief   USART2 interrupt service routine.
 * @note    IRQ number 5.
 */
void USART2_IRQHandler(void)
{
    uart_isr_common(2u);
}

/**
 * @brief   USART3 interrupt service routine.
 * @note    IRQ number 6.
 */
void USART3_IRQHandler(void)
{
    uart_isr_common(3u);
}

/**
 * @brief   USART4 interrupt service routine.
 * @note    IRQ number 7.
 */
void USART4_IRQHandler(void)
{
    uart_isr_common(4u);
}

/*===========================================================================*
 * Public API — implementation
 *===========================================================================*/

hal_uart_status_t hal_uart_init(hal_uart_id_t uart_id, uint32_t baud_rate)
{
    hal_uart_instance_t *   p_inst;
    USART_Type *            p_regs;
    uint32_t                idx;
    hal_uart_status_t       status;
    uint32_t                d;

    if (!is_valid_id(uart_id))
    {
        return HAL_UART_ERR_INVALID_ID;
    }

    idx    = (uint32_t)uart_id;
    p_inst = &s_instances[idx];
    p_regs = s_regs[idx];

    /*
     * Step 1 — Disable NVIC for this USART while reconfiguring.
     *          Prevents spurious interrupts during the reconfiguration window.
     */
    NVIC_DisableIRQ(s_irq[idx]);

    /*
     * Step 2 — Disable the peripheral and clear its interrupt enables.
     *          Writing zero to CFG disables the USART and allows register
     *          writes to BRG, OSR, etc.
     *          UM11029 §18.6.1 — CFG: registers can only be written while
     *          ENABLE = 0.
     */
    p_regs->CFG      = 0u;
    p_regs->INTENCLR = USART_INTENCLR_RXRDYCLR | USART_INTENCLR_TXRDYCLR;

    /*
     * Step 3 — Enable the USART peripheral clock via SYSAHBCLKCTRL.
     *          UM11029 Table 54.
     */
    LPC_SYSCON->SYSAHBCLKCTRL |= (1u << s_clk_bit[idx]);

    /*
     * Step 4 — Assert then deassert reset for the USART peripheral.
     *          UM11029 Table 50 — PRESETCTRL: 0 = reset asserted,
     *                                         1 = reset de-asserted.
     *          Sequence: clear bit (assert reset) → set bit (release reset).
     */
    LPC_SYSCON->PRESETCTRL &= ~(1u << s_rst_bit[idx]);
    LPC_SYSCON->PRESETCTRL |=  (1u << s_rst_bit[idx]);

    /*
     * Step 5 — Configure the global FRG and UARTCLKDIV for the baud rate.
     *          The FRG is shared by all USART instances; this limits mixed
     *          baud rate operation.  An application needing different baud
     *          rates on multiple USARTs must manage the FRG externally.
     */
    status = configure_baud(baud_rate);

    if (status != HAL_UART_OK)
    {
        return status;
    }

    /*
     * Step 6 — Set OSR (oversampling) and BRG (baud rate generator).
     *
     *          OSR register: bits [3:0] = oversampling ratio - 1.
     *          Default reset value is 0xF (16×), which is what we use.
     *          UM11029 §18.6.9 — OSR.
     *
     *          BRG register: bits [15:0] = divisor - 1.
     *          UM11029 §18.6.8 — BRG.
     *
     *          d = MainClock / (baud_rate * (OSR+1)) — recomputed locally
     *          because configure_baud() does not return it.
     */
    p_regs->OSR = HAL_UART_OSR_VALUE;

    d = (SystemCoreClock / HAL_UART_UARTCLKDIV)
        / (baud_rate * (HAL_UART_OSR_VALUE + 1u));

    if (d == 0u)
    {
        d = 1u;     /* Minimum divisor: d=1 → BRG=0. */
    }

    p_regs->BRG = (uint32_t)(d - 1u);

    /*
     * Step 7 — Configure frame format: 8N1 (default).
     *          Build CFG without ENABLE bit; set ENABLE separately after
     *          queues are ready to avoid missing an early RX byte.
     */
    p_regs->CFG = build_cfg(NULL);  /* 8N1, ENABLE=0 */

    /*
     * Step 8 — Initialise (or re-initialise) TX and RX queues.
     */
    p_inst->p_regs = p_regs;

    (void)cqueue_init(&p_inst->tx_queue,
                      p_inst->tx_buf,
                      sizeof(uint8_t),
                      (uint16_t)HAL_UART_TX_QUEUE_SIZE);

    (void)cqueue_init(&p_inst->rx_queue,
                      p_inst->rx_buf,
                      sizeof(uint8_t),
                      (uint16_t)HAL_UART_RX_QUEUE_SIZE);

    /*
     * Step 9 — Enable the peripheral (set ENABLE bit in CFG).
     *          Once ENABLE = 1, the USART begins sampling the RX pin.
     *          UM11029 §18.6.1 — CFG bit 0.
     */
    p_regs->CFG |= USART_CFG_ENABLE;

    /*
     * Step 10 — Enable RXRDY interrupt and set initialised flag.
     *           TXRDY interrupt is left disabled until bytes are pushed.
     */
    p_regs->INTENSET = USART_INTENSET_RXRDYEN;

    p_inst->initialised = true;

    /*
     * Step 11 — Configure and enable NVIC.
     *           Priority 3 (lower than SysTick at 0, giving room for
     *           time-critical ISRs).  Application may override.
     *           Cortex-M0+ supports 4 priority levels (bits [7:6] only).
     */
    NVIC_SetPriority(s_irq[idx], 3u);
    NVIC_EnableIRQ(s_irq[idx]);

    return HAL_UART_OK;
}

/* -------------------------------------------------------------------------- */

hal_uart_status_t hal_uart_set_config(hal_uart_id_t           uart_id,
                                       const hal_uart_config_t * p_config)
{
    hal_uart_instance_t *   p_inst;
    USART_Type *            p_regs;
    uint32_t                idx;
    uint32_t                cfg;

    if (!is_valid_id(uart_id))
    {
        return HAL_UART_ERR_INVALID_ID;
    }

    idx    = (uint32_t)uart_id;
    p_inst = &s_instances[idx];

    if (!p_inst->initialised)
    {
        return HAL_UART_ERR_NOT_INIT;
    }

    if (p_config == NULL)
    {
        return HAL_UART_ERR_NULL_PTR;
    }

    p_regs = p_inst->p_regs;

    /*
     * To change frame format, the peripheral must be disabled (ENABLE = 0).
     * UM11029 §18.6.1: registers other than STAT, INTENSET, INTENCLR, TXDAT
     * must not be written while ENABLE = 1.
     *
     * Sequence:
     *   1. Wait for TX to be idle (shift register empty).
     *   2. Disable NVIC to prevent re-entrant ISR during reconfiguration.
     *   3. Clear ENABLE.
     *   4. Write new CFG (without ENABLE).
     *   5. Re-enable peripheral.
     *   6. Re-enable NVIC.
     */

    /* Wait for TX idle (TXIDLE bit in STAT). */
    while ((p_regs->STAT & USART_STAT_TXIDLE) == 0u)
    {
        /* spin */
    }

    NVIC_DisableIRQ(s_irq[idx]);

    /* Preserve ENABLE=0, rewrite CFG with new format. */
    cfg = build_cfg(p_config);
    p_regs->CFG = cfg;              /* ENABLE=0, new format bits. */
    p_regs->CFG = cfg | USART_CFG_ENABLE;

    NVIC_EnableIRQ(s_irq[idx]);

    return HAL_UART_OK;
}

/* -------------------------------------------------------------------------- */

hal_uart_status_t hal_uart_write_byte(hal_uart_id_t uart_id, uint8_t byte)
{
    hal_uart_instance_t *   p_inst;
    cqueue_status_t         q_status;
    uint32_t                idx;

    if (!is_valid_id(uart_id))
    {
        return HAL_UART_ERR_INVALID_ID;
    }

    idx    = (uint32_t)uart_id;
    p_inst = &s_instances[idx];

    if (!p_inst->initialised)
    {
        return HAL_UART_ERR_NOT_INIT;
    }

    /*
     * Push byte into TX queue.
     * cqueue_push is protected by CQUEUE_ENTER_CRITICAL / EXIT_CRITICAL
     * internally, so it is safe to call from main-loop context while the
     * TXRDY ISR may also be accessing the queue.
     */
    q_status = cqueue_push(&p_inst->tx_queue, &byte);

    if (q_status == CQUEUE_FULL)
    {
        return HAL_UART_ERR_TX_FULL;
    }

    /*
     * Enable TXRDY interrupt so the ISR can drain the queue.
     * Writing 1 to INTENSET bit 2 is idempotent if already enabled.
     * UM11029 §18.6.3 — INTENSET.
     */
    p_inst->p_regs->INTENSET = USART_INTENSET_TXRDYEN;

    return HAL_UART_OK;
}

/* -------------------------------------------------------------------------- */

hal_uart_status_t hal_uart_write_buf(hal_uart_id_t    uart_id,
                                      const uint8_t *  p_data,
                                      uint16_t         length,
                                      uint16_t *       p_written)
{
    hal_uart_instance_t *   p_inst;
    uint32_t                idx;
    uint16_t                i;
    cqueue_status_t         q_status;
    hal_uart_status_t       ret;

    /* Initialise output parameter conservatively. */
    if (p_written != NULL)
    {
        *p_written = 0u;
    }

    if (!is_valid_id(uart_id))
    {
        return HAL_UART_ERR_INVALID_ID;
    }

    idx    = (uint32_t)uart_id;
    p_inst = &s_instances[idx];

    if (!p_inst->initialised)
    {
        return HAL_UART_ERR_NOT_INIT;
    }

    if ((p_data == NULL) || (p_written == NULL))
    {
        return HAL_UART_ERR_NULL_PTR;
    }

    if (length == 0u)
    {
        return HAL_UART_OK;
    }

    ret = HAL_UART_OK;

    for (i = 0u; i < length; i++)
    {
        q_status = cqueue_push(&p_inst->tx_queue, &p_data[i]);

        if (q_status == CQUEUE_FULL)
        {
            if (i == 0u)
            {
                ret = HAL_UART_ERR_TX_FULL;
            }
            else
            {
                ret = HAL_UART_TX_PARTIAL;
            }
            break;
        }
    }

    *p_written = i;

    if (i > 0u)
    {
        /*
         * At least one byte was enqueued — enable TXRDY interrupt.
         */
        p_inst->p_regs->INTENSET = USART_INTENSET_TXRDYEN;
    }

    return ret;
}

/* -------------------------------------------------------------------------- */

hal_uart_status_t hal_uart_read_byte(hal_uart_id_t uart_id, uint8_t * p_byte)
{
    hal_uart_instance_t *   p_inst;
    uint32_t                idx;
    cqueue_status_t         q_status;

    if (!is_valid_id(uart_id))
    {
        return HAL_UART_ERR_INVALID_ID;
    }

    idx    = (uint32_t)uart_id;
    p_inst = &s_instances[idx];

    if (!p_inst->initialised)
    {
        return HAL_UART_ERR_NOT_INIT;
    }

    if (p_byte == NULL)
    {
        return HAL_UART_ERR_NULL_PTR;
    }

    q_status = cqueue_pop(&p_inst->rx_queue, p_byte);

    if (q_status == CQUEUE_EMPTY)
    {
        return HAL_UART_ERR_RX_EMPTY;
    }

    return HAL_UART_OK;
}

/* -------------------------------------------------------------------------- */

hal_uart_status_t hal_uart_read_buf(hal_uart_id_t   uart_id,
                                     uint8_t *       p_buf,
                                     uint16_t        max_length,
                                     uint16_t *      p_read)
{
    hal_uart_instance_t *   p_inst;
    uint32_t                idx;
    uint16_t                i;
    cqueue_status_t         q_status;

    if (p_read != NULL)
    {
        *p_read = 0u;
    }

    if (!is_valid_id(uart_id))
    {
        return HAL_UART_ERR_INVALID_ID;
    }

    idx    = (uint32_t)uart_id;
    p_inst = &s_instances[idx];

    if (!p_inst->initialised)
    {
        return HAL_UART_ERR_NOT_INIT;
    }

    if ((p_buf == NULL) || (p_read == NULL))
    {
        return HAL_UART_ERR_NULL_PTR;
    }

    if (max_length == 0u)
    {
        return HAL_UART_OK;
    }

    for (i = 0u; i < max_length; i++)
    {
        q_status = cqueue_pop(&p_inst->rx_queue, &p_buf[i]);

        if (q_status == CQUEUE_EMPTY)
        {
            break;
        }
    }

    *p_read = i;

    if (i == 0u)
    {
        return HAL_UART_ERR_RX_EMPTY;
    }

    return HAL_UART_OK;
}

/* -------------------------------------------------------------------------- */

uint16_t hal_uart_rx_available(hal_uart_id_t uart_id)
{
    uint32_t idx;

    if (!is_valid_id(uart_id))
    {
        return 0u;
    }

    idx = (uint32_t)uart_id;

    if (!s_instances[idx].initialised)
    {
        return 0u;
    }

    return cqueue_count(&s_instances[idx].rx_queue);
}

/* -------------------------------------------------------------------------- */

uint16_t hal_uart_tx_free(hal_uart_id_t uart_id)
{
    uint32_t    idx;
    uint16_t    count;

    if (!is_valid_id(uart_id))
    {
        return 0u;
    }

    idx = (uint32_t)uart_id;

    if (!s_instances[idx].initialised)
    {
        return 0u;
    }

    count = cqueue_count(&s_instances[idx].tx_queue);

    return (uint16_t)(HAL_UART_TX_QUEUE_SIZE - (uint32_t)count);
}

/* -------------------------------------------------------------------------- */

hal_uart_status_t hal_uart_flush_rx(hal_uart_id_t uart_id)
{
    uint32_t idx;

    if (!is_valid_id(uart_id))
    {
        return HAL_UART_ERR_INVALID_ID;
    }

    idx = (uint32_t)uart_id;

    if (!s_instances[idx].initialised)
    {
        return HAL_UART_ERR_NOT_INIT;
    }

    (void)cqueue_flush(&s_instances[idx].rx_queue);

    return HAL_UART_OK;
}

/* -------------------------------------------------------------------------- */

hal_uart_status_t hal_uart_flush_tx(hal_uart_id_t uart_id)
{
    uint32_t    idx;
    USART_Type * p_regs;

    if (!is_valid_id(uart_id))
    {
        return HAL_UART_ERR_INVALID_ID;
    }

    idx = (uint32_t)uart_id;

    if (!s_instances[idx].initialised)
    {
        return HAL_UART_ERR_NOT_INIT;
    }

    p_regs = s_instances[idx].p_regs;

    /*
     * Disable TXRDY interrupt before flushing the queue to avoid a race
     * condition where the ISR pops a byte from the queue simultaneously
     * with cqueue_flush resetting it.
     *
     * Sequence:
     *   1. Disable TXRDY interrupt (INTENCLR bit 2).
     *   2. Flush TX queue.
     *
     * Note: any byte already in the hardware shift register will still be
     * transmitted completely.  This is by design and documented in
     * hal_uart.h.
     */
    p_regs->INTENCLR = USART_INTENCLR_TXRDYCLR;

    (void)cqueue_flush(&s_instances[idx].tx_queue);

    return HAL_UART_OK;
}
