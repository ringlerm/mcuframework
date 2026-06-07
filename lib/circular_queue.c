/**
 * @file        circular_queue.c
 * @brief       Generic circular queue — implementation.
 *
 * @details     See circular_queue.h for full API documentation, usage
 *              examples, and concurrency model.
 *
 *              Implementation notes
 *              --------------------
 *              Full/empty disambiguation
 *                  A separate @c count field is maintained rather than
 *                  sacrificing one buffer slot (the common "waste-a-slot"
 *                  approach). This allows the queue to use every allocated
 *                  slot and makes full/empty detection O(1) without any
 *                  ambiguity.
 *
 *              Index arithmetic
 *                  head and tail advance with modulo wrapping:
 *                      next = (index + 1u) % capacity
 *                  Using an explicit modulo rather than a bitwise AND keeps
 *                  the implementation correct for any capacity value, not
 *                  just powers of two. The compiler will optimise to a
 *                  mask automatically when capacity is a power of two.
 *
 *              memcpy vs manual loop
 *                  Standard @c memcpy is used for byte transfers. On most
 *                  embedded toolchains this resolves to an optimised
 *                  implementation (LDM/STM, SIMD, or DMA-friendly burst).
 *                  The caller's @c item_size is trusted; no bounds check is
 *                  performed on the backing buffer beyond NULL validation.
 *
 * @ingroup     circular_queue
 *
 * @author      mcuframework contributors
 * @version     1.0.0
 */

/*===========================================================================*
 * Includes
 *===========================================================================*/

#include "circular_queue.h"
#include <string.h>     /* memcpy */

/*===========================================================================*
 * Private helpers (file-scope only)
 *===========================================================================*/

/**
 * @brief   Validate the mandatory fields of a queue control block.
 *
 * @param[in]   p_queue     Control block to validate.
 *
 * @return  @c true  if the control block appears valid.
 * @return  @c false if @p p_queue is NULL or any mandatory field is zero.
 *
 * @note    Internal use only. Not part of the public API.
 */
static bool cqueue_is_valid(const cqueue_t * p_queue)
{
    return ((p_queue            != NULL) &&
            (p_queue->p_buffer  != NULL) &&
            (p_queue->item_size  > 0u)   &&
            (p_queue->capacity   > 0u));
}

/*===========================================================================*
 * Public API — implementation
 *===========================================================================*/

cqueue_status_t cqueue_init(cqueue_t *  p_queue,
                             void *      p_buffer,
                             size_t      item_size,
                             uint16_t    capacity)
{
    if ((p_queue    == NULL) ||
        (p_buffer   == NULL) ||
        (item_size  == 0u)   ||
        (capacity   == 0u))
    {
        return CQUEUE_ERR;
    }

    p_queue->p_buffer  = p_buffer;
    p_queue->item_size = item_size;
    p_queue->capacity  = capacity;
    p_queue->head      = 0u;
    p_queue->tail      = 0u;
    p_queue->count     = 0u;

    return CQUEUE_OK;
}

/* -------------------------------------------------------------------------- */

bool cqueue_is_empty(const cqueue_t * p_queue)
{
    if (p_queue == NULL)
    {
        return true;
    }

    return (p_queue->count == 0u);
}

/* -------------------------------------------------------------------------- */

bool cqueue_is_full(const cqueue_t * p_queue)
{
    if (p_queue == NULL)
    {
        return true;
    }

    return (p_queue->count == p_queue->capacity);
}

/* -------------------------------------------------------------------------- */

uint16_t cqueue_count(const cqueue_t * p_queue)
{
    if (p_queue == NULL)
    {
        return 0u;
    }

    return p_queue->count;
}

/* -------------------------------------------------------------------------- */

cqueue_status_t cqueue_push(cqueue_t *      p_queue,
                             const void *    p_item)
{
    uint8_t *       p_dest;
    cqueue_status_t status;

    if ((p_queue == NULL) || (p_item == NULL))
    {
        return CQUEUE_ERR;
    }

    CQUEUE_ENTER_CRITICAL();

    if (!cqueue_is_valid(p_queue))
    {
        status = CQUEUE_ERR;
    }
    else if (p_queue->count == p_queue->capacity)
    {
        status = CQUEUE_FULL;
    }
    else
    {
        /*
         * Compute destination address:
         *   base + (head_index * item_size)
         *
         * Cast to uint8_t* so pointer arithmetic works in single bytes,
         * regardless of the actual element type.
         */
        p_dest = (uint8_t *)p_queue->p_buffer +
                 ((size_t)p_queue->head * p_queue->item_size);

        (void)memcpy(p_dest, p_item, p_queue->item_size);

        p_queue->head  = (uint16_t)((p_queue->head + 1u) % p_queue->capacity);
        p_queue->count = (uint16_t)(p_queue->count + 1u);

        status = CQUEUE_OK;
    }

    CQUEUE_EXIT_CRITICAL();

    return status;
}

/* -------------------------------------------------------------------------- */

cqueue_status_t cqueue_pop(cqueue_t *  p_queue,
                            void *      p_item)
{
    const uint8_t * p_src;
    cqueue_status_t status;

    if ((p_queue == NULL) || (p_item == NULL))
    {
        return CQUEUE_ERR;
    }

    CQUEUE_ENTER_CRITICAL();

    if (!cqueue_is_valid(p_queue))
    {
        status = CQUEUE_ERR;
    }
    else if (p_queue->count == 0u)
    {
        status = CQUEUE_EMPTY;
    }
    else
    {
        p_src = (const uint8_t *)p_queue->p_buffer +
                ((size_t)p_queue->tail * p_queue->item_size);

        (void)memcpy(p_item, p_src, p_queue->item_size);

        p_queue->tail  = (uint16_t)((p_queue->tail + 1u) % p_queue->capacity);
        p_queue->count = (uint16_t)(p_queue->count - 1u);

        status = CQUEUE_OK;
    }

    CQUEUE_EXIT_CRITICAL();

    return status;
}

/* -------------------------------------------------------------------------- */

cqueue_status_t cqueue_peek(const cqueue_t *    p_queue,
                             void *              p_item)
{
    const uint8_t * p_src;
    cqueue_status_t status;

    if ((p_queue == NULL) || (p_item == NULL))
    {
        return CQUEUE_ERR;
    }

    CQUEUE_ENTER_CRITICAL();

    if (!cqueue_is_valid(p_queue))
    {
        status = CQUEUE_ERR;
    }
    else if (p_queue->count == 0u)
    {
        status = CQUEUE_EMPTY;
    }
    else
    {
        p_src = (const uint8_t *)p_queue->p_buffer +
                ((size_t)p_queue->tail * p_queue->item_size);

        (void)memcpy(p_item, p_src, p_queue->item_size);

        /* tail and count are intentionally not modified */
        status = CQUEUE_OK;
    }

    CQUEUE_EXIT_CRITICAL();

    return status;
}

/* -------------------------------------------------------------------------- */

cqueue_status_t cqueue_flush(cqueue_t * p_queue)
{
    if (p_queue == NULL)
    {
        return CQUEUE_ERR;
    }

    CQUEUE_ENTER_CRITICAL();

    p_queue->head  = 0u;
    p_queue->tail  = 0u;
    p_queue->count = 0u;

    CQUEUE_EXIT_CRITICAL();

    return CQUEUE_OK;
}
