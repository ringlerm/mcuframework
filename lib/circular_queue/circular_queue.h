/**
 * @file        circular_queue.h
 * @brief       Generic circular (ring) queue for embedded systems.
 *
 * @details     Implements a statically-allocated, pointer-based circular queue
 *              that operates on arbitrary data types via void pointers and
 *              caller-supplied item size. The implementation uses the
 *              "count-based full/empty distinction" strategy: a separate
 *              @c count field is maintained so that no buffer slot is wasted
 *              and head == tail is unambiguous.
 *
 *              Concurrency model
 *              -----------------
 *              The core operations are NOT inherently thread-safe or
 *              ISR-safe. Callers must wrap Push/Pop in a critical section
 *              appropriate for their target (e.g. disable interrupts, RTOS
 *              mutex). Two hooks are provided for this purpose:
 *
 *              @code
 *              // Example – bare-metal AVR
 *              #define CQUEUE_ENTER_CRITICAL()  cli()
 *              #define CQUEUE_EXIT_CRITICAL()   sei()
 *              @endcode
 *
 *              If the queue is used exclusively from a single context
 *              (e.g. only main loop, never inside an ISR), define both macros
 *              as empty in @c target.h and no overhead is incurred.
 *
 * @note        This module requires @c target.h to be present and to define:
 *              - @c CQUEUE_ENTER_CRITICAL() — disable concurrent access
 *              - @c CQUEUE_EXIT_CRITICAL()  — re-enable concurrent access
 *              - Standard integer types via @c <stdint.h>
 *
 * @defgroup    circular_queue  Circular Queue
 * @ingroup     lib
 * @{
 *
 * @author      mcuframework contributors
 * @version     1.0.0
 */

#ifndef CIRCULAR_QUEUE_H
#define CIRCULAR_QUEUE_H

/*===========================================================================*
 * Includes
 *===========================================================================*/

#include "../../target.h"   /* Platform critical-section macros              */
#include <stdbool.h>        /* bool                                          */
#include <stddef.h>         /* size_t                                        */
#include <stdint.h>         /* uint16_t, uint8_t                             */

/*===========================================================================*
 * Public types
 *===========================================================================*/

/**
 * @brief   Return codes for all circular queue operations.
 *
 * @details Every API function that can fail returns one of these codes.
 *          Callers must check the return value before using output parameters.
 */
typedef enum
{
    CQUEUE_OK    = 0,   /**< Operation completed successfully.               */
    CQUEUE_FULL  = 1,   /**< Push rejected: queue has no free slots.         */
    CQUEUE_EMPTY = 2,   /**< Pop rejected: queue contains no items.          */
    CQUEUE_ERR   = 3    /**< Invalid argument (NULL pointer or zero size).   */
} cqueue_status_t;

/**
 * @brief   Circular queue control block.
 *
 * @details All fields are managed exclusively by the API. Callers must treat
 *          this structure as opaque and must not read or write its fields
 *          directly.
 *
 *          Memory layout (capacity = 4, item_size = 1):
 *          @code
 *          buffer: [ A | B | C | D ]
 *                        ^           ^
 *                       tail        head
 *          count = 2
 *          @endcode
 */
typedef struct
{
    void *      p_buffer;   /**< Caller-supplied backing store.               */
    size_t      item_size;  /**< Byte size of one element.                   */
    uint16_t    capacity;   /**< Maximum number of elements the queue holds. */
    uint16_t    head;       /**< Write index (next slot to be written).      */
    uint16_t    tail;       /**< Read index  (next slot to be read).         */
    uint16_t    count;      /**< Number of elements currently stored.        */
} cqueue_t;

/*===========================================================================*
 * Public API
 *===========================================================================*/

/**
 * @brief   Initialise a circular queue control block.
 *
 * @details Must be called exactly once before any other operation on the
 *          queue. Safe to call again to reset the queue to empty (all
 *          previously stored data is logically discarded; the backing buffer
 *          is not zeroed).
 *
 * @param[out]  p_queue     Pointer to an uninitialised @c cqueue_t.
 * @param[in]   p_buffer    Pointer to caller-allocated backing memory of at
 *                          least @p capacity * @p item_size bytes.
 * @param[in]   item_size   Byte size of one element. Must be > 0.
 * @param[in]   capacity    Maximum number of elements. Must be > 0 and
 *                          must satisfy: capacity <= UINT16_MAX.
 *
 * @retval  CQUEUE_OK       Initialisation succeeded.
 * @retval  CQUEUE_ERR      @p p_queue or @p p_buffer is NULL, or
 *                          @p item_size == 0, or @p capacity == 0.
 *
 * @note    This function is not protected by the critical-section hooks
 *          because it is expected to be called before concurrent access
 *          begins.
 */
cqueue_status_t cqueue_init(cqueue_t *  p_queue,
                             void *      p_buffer,
                             size_t      item_size,
                             uint16_t    capacity);

/**
 * @brief   Query whether the queue contains no elements.
 *
 * @param[in]   p_queue     Pointer to an initialised @c cqueue_t.
 *
 * @return  @c true  if the queue is empty or @p p_queue is NULL.
 * @return  @c false otherwise.
 *
 * @note    The returned value may be stale if the queue is shared between
 *          contexts. Callers in concurrent scenarios should hold a critical
 *          section across the is_empty check and the subsequent Pop.
 */
bool cqueue_is_empty(const cqueue_t * p_queue);

/**
 * @brief   Query whether the queue has no free slots.
 *
 * @param[in]   p_queue     Pointer to an initialised @c cqueue_t.
 *
 * @return  @c true  if the queue is full or @p p_queue is NULL.
 * @return  @c false otherwise.
 *
 * @note    Same concurrency caveat as @ref cqueue_is_empty.
 */
bool cqueue_is_full(const cqueue_t * p_queue);

/**
 * @brief   Return the number of elements currently stored in the queue.
 *
 * @param[in]   p_queue     Pointer to an initialised @c cqueue_t.
 *
 * @return  Number of elements (0 if empty, up to capacity if full).
 *          Returns 0 if @p p_queue is NULL.
 */
uint16_t cqueue_count(const cqueue_t * p_queue);

/**
 * @brief   Insert one element at the head of the queue.
 *
 * @details Copies @c item_size bytes from @p p_item into the next free slot.
 *          The copy is performed with @c memcpy; the caller retains ownership
 *          of @p p_item after the call returns.
 *
 *          The operation is protected by @c CQUEUE_ENTER_CRITICAL /
 *          @c CQUEUE_EXIT_CRITICAL so that it is safe to call from both
 *          thread and ISR context (provided the hooks are defined correctly
 *          in @c target.h).
 *
 * @param[in,out]   p_queue     Pointer to an initialised @c cqueue_t.
 * @param[in]       p_item      Pointer to the element to copy in.
 *                              Must point to at least @c item_size bytes.
 *
 * @retval  CQUEUE_OK           Element enqueued successfully.
 * @retval  CQUEUE_FULL         No free slot available; element not stored.
 * @retval  CQUEUE_ERR          @p p_queue or @p p_item is NULL.
 */
cqueue_status_t cqueue_push(cqueue_t *      p_queue,
                             const void *    p_item);

/**
 * @brief   Remove and return the oldest element from the tail of the queue.
 *
 * @details Copies @c item_size bytes from the tail slot into @p p_item, then
 *          advances the tail pointer. The slot is logically freed; its
 *          content in the backing buffer is not zeroed.
 *
 *          The operation is protected by @c CQUEUE_ENTER_CRITICAL /
 *          @c CQUEUE_EXIT_CRITICAL.
 *
 * @param[in,out]   p_queue     Pointer to an initialised @c cqueue_t.
 * @param[out]      p_item      Destination buffer of at least @c item_size
 *                              bytes where the element will be written.
 *
 * @retval  CQUEUE_OK           Element dequeued and written to @p p_item.
 * @retval  CQUEUE_EMPTY        Queue is empty; @p p_item is not modified.
 * @retval  CQUEUE_ERR          @p p_queue or @p p_item is NULL.
 */
cqueue_status_t cqueue_pop(cqueue_t *  p_queue,
                            void *      p_item);

/**
 * @brief   Read the oldest element without removing it from the queue.
 *
 * @details Copies the tail element into @p p_item but does not advance the
 *          tail or decrement the count. Subsequent calls to @c cqueue_peek
 *          without an intervening @c cqueue_pop return the same element.
 *
 * @param[in]   p_queue     Pointer to an initialised @c cqueue_t.
 * @param[out]  p_item      Destination buffer of at least @c item_size bytes.
 *
 * @retval  CQUEUE_OK       Element copied to @p p_item.
 * @retval  CQUEUE_EMPTY    Queue is empty; @p p_item is not modified.
 * @retval  CQUEUE_ERR      @p p_queue or @p p_item is NULL.
 */
cqueue_status_t cqueue_peek(const cqueue_t *    p_queue,
                             void *              p_item);

/**
 * @brief   Discard all elements and reset the queue to its empty state.
 *
 * @details Equivalent to calling @ref cqueue_init again with the same buffer
 *          and sizing parameters. The backing buffer is not zeroed.
 *
 * @param[in,out]   p_queue     Pointer to an initialised @c cqueue_t.
 *
 * @retval  CQUEUE_OK   Queue flushed successfully.
 * @retval  CQUEUE_ERR  @p p_queue is NULL.
 */
cqueue_status_t cqueue_flush(cqueue_t * p_queue);

/** @} */ /* end of group circular_queue */

#endif /* CIRCULAR_QUEUE_H */
