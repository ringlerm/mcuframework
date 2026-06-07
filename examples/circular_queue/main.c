/**
 * @file        main.c
 * @brief       circular_queue usage examples — primitive types and structs.
 *
 * @details     Demonstrates correct initialisation, push, pop, peek, and
 *              flush for both scalar (uint8_t) and compound (sensor_sample_t)
 *              element types. Each example is self-contained and prints its
 *              result via printf; on a real MCU replace printf with your
 *              HAL UART driver.
 *
 *              Build (host machine, for validation):
 *              @code
 *              gcc -std=c99 -Wall -Wextra -pedantic \
 *                  -I../../ \
 *                  ../../lib/circular_queue/circular_queue.c \
 *                  main.c -o cqueue_example
 *              @endcode
 *
 * @author      mcuframework contributors
 * @version     1.0.0
 */

#include <stdio.h>
#include <stdint.h>
#include "../../lib/circular_queue/circular_queue.h"

/*===========================================================================*
 * Example 1 — scalar uint8_t queue
 *===========================================================================*/

/** @brief  Number of slots in the byte queue. */
#define BYTE_QUEUE_CAPACITY     (4u)

/**
 * @brief   Demonstrate push, pop, and overflow behaviour on a byte queue.
 */
static void example_byte_queue(void)
{
    cqueue_t        queue;
    uint8_t         buffer[BYTE_QUEUE_CAPACITY];
    uint8_t         value;
    cqueue_status_t status;

    printf("\n--- Example 1: uint8_t queue (capacity %u) ---\n",
           BYTE_QUEUE_CAPACITY);

    (void)cqueue_init(&queue, buffer, sizeof(uint8_t), BYTE_QUEUE_CAPACITY);

    /* Fill the queue */
    for (uint8_t i = 10u; i < 10u + BYTE_QUEUE_CAPACITY; i++)
    {
        status = cqueue_push(&queue, &i);
        printf("  push(%u) -> %s  [count=%u]\n",
               i,
               (status == CQUEUE_OK) ? "OK" : "FULL",
               cqueue_count(&queue));
    }

    /* Attempt one extra push — must return CQUEUE_FULL */
    value  = 99u;
    status = cqueue_push(&queue, &value);
    printf("  push(99) -> %s  (expected FULL)\n",
           (status == CQUEUE_FULL) ? "FULL" : "unexpected");

    /* Peek without removing */
    status = cqueue_peek(&queue, &value);
    printf("  peek()   -> value=%u, status=%s\n",
           value,
           (status == CQUEUE_OK) ? "OK" : "error");

    /* Drain the queue */
    while (!cqueue_is_empty(&queue))
    {
        (void)cqueue_pop(&queue, &value);
        printf("  pop()    -> %u  [count=%u]\n",
               value,
               cqueue_count(&queue));
    }

    /* Attempt pop on empty queue — must return CQUEUE_EMPTY */
    status = cqueue_pop(&queue, &value);
    printf("  pop() on empty -> %s  (expected EMPTY)\n",
           (status == CQUEUE_EMPTY) ? "EMPTY" : "unexpected");
}

/*===========================================================================*
 * Example 2 — struct queue
 *===========================================================================*/

/**
 * @brief   Simulated sensor reading with timestamp and channel identifier.
 */
typedef struct
{
    uint32_t    timestamp_ms;   /**< Milliseconds since system start.        */
    uint16_t    raw_adc;        /**< Raw ADC reading (0–4095 for 12-bit).    */
    uint8_t     channel;        /**< ADC channel index.                      */
} sensor_sample_t;

/** @brief  Number of slots in the sensor sample queue. */
#define SAMPLE_QUEUE_CAPACITY   (3u)

/**
 * @brief   Demonstrate that the queue works transparently with structs.
 */
static void example_struct_queue(void)
{
    cqueue_t        queue;
    sensor_sample_t buffer[SAMPLE_QUEUE_CAPACITY];
    sensor_sample_t sample;
    cqueue_status_t status;

    const sensor_sample_t samples[] =
    {
        { .timestamp_ms = 100u, .raw_adc = 1024u, .channel = 0u },
        { .timestamp_ms = 200u, .raw_adc = 2048u, .channel = 1u },
        { .timestamp_ms = 300u, .raw_adc = 3072u, .channel = 0u },
    };

    printf("\n--- Example 2: sensor_sample_t queue (capacity %u) ---\n",
           SAMPLE_QUEUE_CAPACITY);

    (void)cqueue_init(&queue,
                      buffer,
                      sizeof(sensor_sample_t),
                      SAMPLE_QUEUE_CAPACITY);

    /* Enqueue all prepared samples */
    for (size_t i = 0u; i < SAMPLE_QUEUE_CAPACITY; i++)
    {
        status = cqueue_push(&queue, &samples[i]);
        printf("  push(ch=%u, adc=%u, t=%u) -> %s\n",
               samples[i].channel,
               samples[i].raw_adc,
               samples[i].timestamp_ms,
               (status == CQUEUE_OK) ? "OK" : "FULL");
    }

    /* Flush and verify empty state */
    (void)cqueue_flush(&queue);
    printf("  flush() -> count=%u  (expected 0)\n", cqueue_count(&queue));

    /* Re-push first sample and pop it back */
    (void)cqueue_push(&queue, &samples[0u]);
    status = cqueue_pop(&queue, &sample);
    printf("  pop()   -> ch=%u, adc=%u, t=%u, status=%s\n",
           sample.channel,
           sample.raw_adc,
           sample.timestamp_ms,
           (status == CQUEUE_OK) ? "OK" : "error");
}

/*===========================================================================*
 * Entry point
 *===========================================================================*/

int main(void)
{
    printf("=== circular_queue examples ===");

    example_byte_queue();
    example_struct_queue();

    printf("\nDone.\n");
    return 0;
}
