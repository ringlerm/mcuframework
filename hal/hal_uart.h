/**
 * @file    hal_uart.h
 * @brief   Hardware Abstraction Layer — UART / USART interface.
 *
 * @details Provides a target-independent UART API backed by statically-
 *          allocated circular queues for both TX and RX paths.
 *
 *          ### Architecture
 *
 *          @code
 *          Application
 *              │
 *              ▼
 *          hal_uart_write() ──► TX queue ──► ISR (TXRDY / TXE) ──► UART HW
 *          hal_uart_read()  ◄── RX queue ◄── ISR (RXRDY / RXNE) ◄── UART HW
 *          @endcode
 *
 *          - **TX path** — the application enqueues bytes via
 *            @ref hal_uart_write_byte or @ref hal_uart_write_buf.  The
 *            transmit-data-register-empty ISR drains the queue one byte at a
 *            time and disables the TXRDY interrupt automatically when the
 *            queue empties.
 *
 *          - **RX path** — the receive ISR deposits every incoming byte into
 *            the RX queue.  The application polls via @ref hal_uart_read_byte
 *            or @ref hal_uart_read_buf at its own pace (pull model).
 *
 *          ### Memory model
 *
 *          All queue storage is statically allocated inside the port
 *          implementation file.  No heap is used.  Buffer sizes are
 *          controlled by the following macros, which may be overridden from
 *          the build system:
 *
 *          @code
 *          // In your Makefile / CMakeLists.txt:
 *          -DHAL_UART_TX_QUEUE_SIZE=128
 *          -DHAL_UART_RX_QUEUE_SIZE=128
 *          @endcode
 *
 *          ### Multi-instance
 *
 *          Each physical UART peripheral is identified by a
 *          @ref hal_uart_id_t value.  Available IDs depend on the target;
 *          see the port header or @c target.h for the supported set.
 *          Calling any function with an uninitialised ID returns
 *          @c HAL_UART_ERR_NOT_INIT.
 *
 *          ### Default configuration
 *
 *          @ref hal_uart_init initialises the peripheral to **8N1** at the
 *          requested baud rate.  Call @ref hal_uart_set_config afterwards to
 *          change data bits, parity or stop bits without re-initialising.
 *
 *          ### Concurrency
 *
 *          @ref hal_uart_write_byte and @ref hal_uart_write_buf are safe to
 *          call from the main loop or a task context.  They must NOT be
 *          called from an ISR.  Read functions share the same restriction.
 *          The ISR-facing push/pop operations are protected by the
 *          @c CQUEUE_ENTER_CRITICAL / @c CQUEUE_EXIT_CRITICAL hooks defined
 *          in @c target.h.
 *
 *          ### Example — basic TX/RX
 *
 *          @code
 *          #include "hal/hal_uart.h"
 *
 *          // Initialise UART0 at 115200 baud (8N1 by default)
 *          hal_uart_init(HAL_UART_ID_0, 115200u);
 *
 *          // Send a greeting
 *          const uint8_t msg[] = "Hello\r\n";
 *          uint16_t sent = 0u;
 *          hal_uart_write_buf(HAL_UART_ID_0, msg, sizeof(msg) - 1u, &sent);
 *
 *          // Poll for an incoming byte
 *          uint8_t byte = 0u;
 *          if (hal_uart_read_byte(HAL_UART_ID_0, &byte) == HAL_UART_OK)
 *          {
 *              process(byte);
 *          }
 *          @endcode
 *
 * @note    This file is target-independent.  It must not include any device
 *          header directly.  Port implementations live under
 *          @c ports/<target>/hal_uart_<target>.c and include @c target.h as
 *          their sole platform entry point.
 *
 * @note    Requires @c lib/circular_queue/circular_queue.h to be reachable
 *          on the include path.  The port implementation file declares the
 *          static queue storage and control blocks.
 *
 * @author  mcuframework contributors
 * @version 1.0.0
 */

#ifndef HAL_UART_H
#define HAL_UART_H

#include <stdint.h>
#include <stddef.h>

/*===========================================================================*
 * Queue size configuration
 * -------------------------------------------------------------------------
 * Override from the build system with -DHAL_UART_TX_QUEUE_SIZE=N.
 * Each UART instance allocates one TX buffer and one RX buffer of these
 * sizes (in bytes).  Total static RAM = instances * (TX + RX) bytes.
 *===========================================================================*/

/**
 * @brief   Byte capacity of each UART TX circular queue.
 *
 * @details One buffer is allocated per UART instance.  Increase for high-
 *          throughput channels; decrease to save RAM on resource-constrained
 *          targets.  Must be in range [2, 65535].
 */
#ifndef HAL_UART_TX_QUEUE_SIZE
#define HAL_UART_TX_QUEUE_SIZE      (64u)
#endif

/**
 * @brief   Byte capacity of each UART RX circular queue.
 *
 * @details One buffer is allocated per UART instance.  Should be large
 *          enough to buffer the maximum burst before the application drains
 *          it.  Must be in range [2, 65535].
 */
#ifndef HAL_UART_RX_QUEUE_SIZE
#define HAL_UART_RX_QUEUE_SIZE      (64u)
#endif

/*===========================================================================*
 * Public types
 *===========================================================================*/

/**
 * @brief   Return codes for all HAL UART operations.
 *
 * @details Every API function returns one of these values.  Callers must
 *          check the return value before using any output parameter.
 */
typedef enum
{
    HAL_UART_OK             = 0,    /**< Operation completed successfully.        */
    HAL_UART_ERR_NOT_INIT   = 1,    /**< UART instance has not been initialised.  */
    HAL_UART_ERR_INVALID_ID = 2,    /**< @p uart_id is out of range for target.   */
    HAL_UART_ERR_NULL_PTR   = 3,    /**< A required pointer argument is NULL.     */
    HAL_UART_ERR_TX_FULL    = 4,    /**< TX queue is full; no bytes were written. */
    HAL_UART_TX_PARTIAL     = 5,    /**< TX queue accepted fewer bytes than       */
                                    /**< requested; @p p_written holds the count. */
    HAL_UART_ERR_RX_EMPTY   = 6,    /**< RX queue is empty; no byte available.    */
    HAL_UART_ERR            = 7,    /**< Unspecified internal error.              */
} hal_uart_status_t;

/**
 * @brief   UART peripheral identifier.
 *
 * @details The available values depend on the active target.  Each value
 *          maps to one physical UART/USART peripheral.  The port
 *          implementation defines @c HAL_UART_ID_COUNT (not part of the
 *          enum) to bound the static descriptor array.
 *
 *          | Value          | LPC845      | STM32F103 | STM32F401/F411 |
 *          |----------------|-------------|-----------|----------------|
 *          | HAL_UART_ID_0  | USART0      | USART1    | USART1         |
 *          | HAL_UART_ID_1  | USART1      | USART2    | USART2         |
 *          | HAL_UART_ID_2  | USART2      | USART3    | USART6         |
 *          | HAL_UART_ID_3  | USART3      | —         | —              |
 *          | HAL_UART_ID_4  | USART4      | —         | —              |
 */
typedef enum
{
    HAL_UART_ID_0 = 0u,     /**< First UART instance on the target.  */
    HAL_UART_ID_1 = 1u,     /**< Second UART instance on the target. */
    HAL_UART_ID_2 = 2u,     /**< Third UART instance on the target.  */
    HAL_UART_ID_3 = 3u,     /**< Fourth UART instance on the target. */
    HAL_UART_ID_4 = 4u,     /**< Fifth UART instance on the target.  */
} hal_uart_id_t;

/**
 * @brief   Number of data bits per UART frame.
 */
typedef enum
{
    HAL_UART_DATABITS_7 = 7u,   /**< 7 data bits. */
    HAL_UART_DATABITS_8 = 8u,   /**< 8 data bits (default, 8N1). */
    HAL_UART_DATABITS_9 = 9u,   /**< 9 data bits (available on some targets). */
} hal_uart_databits_t;

/**
 * @brief   Parity mode.
 */
typedef enum
{
    HAL_UART_PARITY_NONE = 0u,  /**< No parity bit (default, 8N1). */
    HAL_UART_PARITY_ODD  = 1u,  /**< Odd parity.                   */
    HAL_UART_PARITY_EVEN = 2u,  /**< Even parity.                  */
} hal_uart_parity_t;

/**
 * @brief   Number of stop bits per UART frame.
 */
typedef enum
{
    HAL_UART_STOPBITS_1 = 1u,   /**< 1 stop bit (default, 8N1). */
    HAL_UART_STOPBITS_2 = 2u,   /**< 2 stop bits.               */
} hal_uart_stopbits_t;

/**
 * @brief   Extended UART frame configuration.
 *
 * @details Passed to @ref hal_uart_set_config to change frame format after
 *          @ref hal_uart_init.  The baud rate is intentionally excluded; use
 *          @ref hal_uart_init (which resets the queues) to change it, or
 *          extend this struct in a future version if in-flight baud changes
 *          are required.
 *
 * @note    Not all combinations are supported by all targets.  The port
 *          implementation returns @c HAL_UART_ERR if the combination is
 *          invalid for the active MCU.
 */
typedef struct
{
    hal_uart_databits_t data_bits;  /**< Number of data bits.  */
    hal_uart_parity_t   parity;     /**< Parity mode.          */
    hal_uart_stopbits_t stop_bits;  /**< Number of stop bits.  */
} hal_uart_config_t;

/*===========================================================================*
 * Public API
 *===========================================================================*/

/**
 * @brief   Initialise a UART peripheral and its associated queues.
 *
 * @details Configures the peripheral at the requested baud rate with the
 *          default 8N1 frame format, resets both TX and RX circular queues
 *          to empty, and enables the RX interrupt.  The TX interrupt is left
 *          disabled until the first byte is enqueued by
 *          @ref hal_uart_write_byte or @ref hal_uart_write_buf.
 *
 *          Safe to call more than once on the same @p uart_id; doing so
 *          reconfigures the peripheral and discards any data still in the
 *          queues.
 *
 * @param[in]   uart_id     Peripheral to initialise.
 * @param[in]   baud_rate   Desired baud rate in bits per second
 *                          (e.g. 9600u, 115200u).  The port calculates the
 *                          reload value from @c SystemCoreClock or the
 *                          appropriate peripheral clock.
 *
 * @retval  HAL_UART_OK             Peripheral ready.
 * @retval  HAL_UART_ERR_INVALID_ID @p uart_id is not available on this target.
 * @retval  HAL_UART_ERR            Baud rate could not be achieved within the
 *                                  tolerance of the peripheral (e.g. clock
 *                                  too slow for the requested baud rate).
 */
hal_uart_status_t hal_uart_init(hal_uart_id_t   uart_id,
                                 uint32_t        baud_rate);

/**
 * @brief   Change the frame format of an already-initialised UART.
 *
 * @details Updates data bits, parity, and stop bits without reinitialising
 *          the peripheral or flushing the queues.  The baud rate is
 *          unchanged.
 *
 *          Behaviour is undefined if called while a transmission is in
 *          progress (TX queue non-empty or shift register active).  The
 *          caller is responsible for draining or flushing TX before calling
 *          this function if frame coherence is required.
 *
 * @param[in]   uart_id     Peripheral to reconfigure.
 * @param[in]   p_config    Pointer to the desired frame configuration.
 *                          Must not be NULL.
 *
 * @retval  HAL_UART_OK             Configuration applied.
 * @retval  HAL_UART_ERR_INVALID_ID @p uart_id is not available on this target.
 * @retval  HAL_UART_ERR_NOT_INIT   @ref hal_uart_init has not been called for
 *                                  this ID.
 * @retval  HAL_UART_ERR_NULL_PTR   @p p_config is NULL.
 * @retval  HAL_UART_ERR            Configuration is not supported by the
 *                                  target hardware.
 */
hal_uart_status_t hal_uart_set_config(hal_uart_id_t           uart_id,
                                       const hal_uart_config_t * p_config);

/**
 * @brief   Enqueue a single byte for transmission.
 *
 * @details Copies @p byte into the TX circular queue and enables the
 *          TXRDY interrupt if it was disabled.  Returns immediately; actual
 *          transmission is handled by the ISR.
 *
 * @param[in]   uart_id     Peripheral to transmit on.
 * @param[in]   byte        Byte to send.
 *
 * @retval  HAL_UART_OK             Byte enqueued successfully.
 * @retval  HAL_UART_ERR_INVALID_ID @p uart_id is not available on this target.
 * @retval  HAL_UART_ERR_NOT_INIT   @ref hal_uart_init has not been called for
 *                                  this ID.
 * @retval  HAL_UART_ERR_TX_FULL    TX queue is full; byte was NOT enqueued.
 */
hal_uart_status_t hal_uart_write_byte(hal_uart_id_t   uart_id,
                                       uint8_t         byte);

/**
 * @brief   Enqueue a buffer of bytes for transmission.
 *
 * @details Copies as many bytes as possible from @p p_data into the TX
 *          circular queue, enables the TXRDY interrupt, and returns.  If the
 *          queue does not have enough free space for all @p length bytes, the
 *          function writes the maximum number that fits and returns
 *          @c HAL_UART_TX_PARTIAL.  The caller can inspect @p *p_written to
 *          determine how many bytes were accepted.
 *
 *          When @c HAL_UART_TX_PARTIAL is returned, the bytes that were NOT
 *          accepted have not been modified or consumed; the caller is
 *          responsible for retrying or discarding them.
 *
 * @param[in]   uart_id     Peripheral to transmit on.
 * @param[in]   p_data      Pointer to the source buffer.  Must not be NULL.
 * @param[in]   length      Number of bytes to send.  Must be > 0.
 * @param[out]  p_written   Pointer to a variable that receives the number of
 *                          bytes actually enqueued.  Must not be NULL.
 *                          Set to 0 on error.
 *
 * @retval  HAL_UART_OK             All @p length bytes enqueued successfully.
 * @retval  HAL_UART_TX_PARTIAL     Fewer than @p length bytes were enqueued;
 *                                  @p *p_written holds the accepted count.
 * @retval  HAL_UART_ERR_TX_FULL    TX queue was already full; no bytes were
 *                                  enqueued, @p *p_written is 0.
 * @retval  HAL_UART_ERR_INVALID_ID @p uart_id is not available on this target.
 * @retval  HAL_UART_ERR_NOT_INIT   @ref hal_uart_init has not been called.
 * @retval  HAL_UART_ERR_NULL_PTR   @p p_data or @p p_written is NULL.
 */
hal_uart_status_t hal_uart_write_buf(hal_uart_id_t    uart_id,
                                      const uint8_t *  p_data,
                                      uint16_t         length,
                                      uint16_t *       p_written);

/**
 * @brief   Dequeue a single received byte.
 *
 * @details Removes the oldest byte from the RX circular queue and writes it
 *          to @p *p_byte.  Returns immediately; if the queue is empty no
 *          modification is made to @p *p_byte.
 *
 * @param[in]   uart_id     Peripheral to read from.
 * @param[out]  p_byte      Destination for the received byte.
 *                          Must not be NULL.  Not modified if empty.
 *
 * @retval  HAL_UART_OK             A byte was dequeued and written to
 *                                  @p *p_byte.
 * @retval  HAL_UART_ERR_RX_EMPTY   No byte available in the RX queue.
 * @retval  HAL_UART_ERR_INVALID_ID @p uart_id is not available on this target.
 * @retval  HAL_UART_ERR_NOT_INIT   @ref hal_uart_init has not been called.
 * @retval  HAL_UART_ERR_NULL_PTR   @p p_byte is NULL.
 */
hal_uart_status_t hal_uart_read_byte(hal_uart_id_t   uart_id,
                                      uint8_t *       p_byte);

/**
 * @brief   Dequeue multiple received bytes.
 *
 * @details Removes up to @p max_length bytes from the RX circular queue and
 *          writes them to @p p_buf in FIFO order.  Stops early if the queue
 *          empties before @p max_length bytes are read.  The actual number
 *          of bytes written is reported via @p *p_read.
 *
 * @param[in]   uart_id     Peripheral to read from.
 * @param[out]  p_buf       Destination buffer.  Must point to at least
 *                          @p max_length bytes.  Must not be NULL.
 * @param[in]   max_length  Maximum number of bytes to read.  Must be > 0.
 * @param[out]  p_read      Receives the number of bytes actually written to
 *                          @p p_buf.  May be 0 if the queue is empty.
 *                          Must not be NULL.  Set to 0 on error.
 *
 * @retval  HAL_UART_OK             At least one byte was read.
 * @retval  HAL_UART_ERR_RX_EMPTY   RX queue was empty; @p *p_read is 0 and
 *                                  @p p_buf is not modified.
 * @retval  HAL_UART_ERR_INVALID_ID @p uart_id is not available on this target.
 * @retval  HAL_UART_ERR_NOT_INIT   @ref hal_uart_init has not been called.
 * @retval  HAL_UART_ERR_NULL_PTR   @p p_buf or @p p_read is NULL.
 */
hal_uart_status_t hal_uart_read_buf(hal_uart_id_t   uart_id,
                                     uint8_t *       p_buf,
                                     uint16_t        max_length,
                                     uint16_t *      p_read);

/**
 * @brief   Return the number of bytes currently available in the RX queue.
 *
 * @details Useful for deciding how many bytes to pass to
 *          @ref hal_uart_read_buf without risk of getting
 *          @c HAL_UART_ERR_RX_EMPTY mid-read.
 *
 * @param[in]   uart_id     Peripheral to query.
 *
 * @return  Number of bytes available (0 if empty, uninitialised, or invalid).
 */
uint16_t hal_uart_rx_available(hal_uart_id_t uart_id);

/**
 * @brief   Return the number of free bytes remaining in the TX queue.
 *
 * @details Useful for checking headroom before calling
 *          @ref hal_uart_write_buf.
 *
 * @param[in]   uart_id     Peripheral to query.
 *
 * @return  Number of free bytes (0 if full, uninitialised, or invalid).
 */
uint16_t hal_uart_tx_free(hal_uart_id_t uart_id);

/**
 * @brief   Discard all bytes in the RX queue.
 *
 * @details Resets the RX circular queue to empty.  Bytes received after
 *          this call are unaffected.
 *
 * @param[in]   uart_id     Peripheral whose RX queue should be flushed.
 *
 * @retval  HAL_UART_OK             Queue flushed.
 * @retval  HAL_UART_ERR_INVALID_ID @p uart_id is not available on this target.
 * @retval  HAL_UART_ERR_NOT_INIT   @ref hal_uart_init has not been called.
 */
hal_uart_status_t hal_uart_flush_rx(hal_uart_id_t uart_id);

/**
 * @brief   Discard all bytes pending in the TX queue.
 *
 * @details Resets the TX circular queue to empty and disables the TXRDY
 *          interrupt.  Any byte currently loaded into the hardware shift
 *          register will still be transmitted.
 *
 * @param[in]   uart_id     Peripheral whose TX queue should be flushed.
 *
 * @retval  HAL_UART_OK             Queue flushed.
 * @retval  HAL_UART_ERR_INVALID_ID @p uart_id is not available on this target.
 * @retval  HAL_UART_ERR_NOT_INIT   @ref hal_uart_init has not been called.
 */
hal_uart_status_t hal_uart_flush_tx(hal_uart_id_t uart_id);

#endif /* HAL_UART_H */
