/**
 * ============================================================================
 * OCAS: Autonomous Orbital Collision Avoidance System
 * Flight Software Architecture - Static CDM FIFO Queue Implementation (C11)
 * Team ID: DSCPP-III-2026-T018 | Graphic Era University
 * ============================================================================
 * Zero heap allocation. Bounded deterministic FIFO queue with drop tracking.
 * ============================================================================
 */

#include "c_ingestion.h"
#include <string.h>

void cdm_fifo_init(CDMFifoQueue* q) {
    if (!q) return;
    memset(q->buffer, 0, sizeof(q->buffer));
    q->head = 0;
    q->count = 0;
    q->total_enqueued = 0;
    q->total_dropped = 0;
    q->overflow_occurred = false;
}

bool cdm_fifo_push(CDMFifoQueue* q, const CDMAlert* alert) {
    if (!q || !alert) return false;

    if (q->count >= OCAS_CDM_FIFO_CAPACITY) {
        q->total_dropped++;
        q->overflow_occurred = true;
        return false; /* Bounded queue protection: drop incoming frame, maintain flight stability */
    }

    const size_t write_idx = (q->head + q->count) % OCAS_CDM_FIFO_CAPACITY;
    q->buffer[write_idx] = *alert;
    q->count++;
    q->total_enqueued++;
    return true;
}

bool cdm_fifo_pop(CDMFifoQueue* q, CDMAlert* out_alert) {
    if (!q || !out_alert || q->count == 0) return false;

    *out_alert = q->buffer[q->head];
    q->head = (q->head + 1) % OCAS_CDM_FIFO_CAPACITY;
    q->count--;
    return true;
}

bool cdm_fifo_peek(const CDMFifoQueue* q, CDMAlert* out_alert) {
    if (!q || !out_alert || q->count == 0) return false;

    *out_alert = q->buffer[q->head];
    return true;
}

size_t cdm_fifo_count(const CDMFifoQueue* q) {
    return q ? q->count : 0;
}

bool cdm_fifo_is_full(const CDMFifoQueue* q) {
    return q ? (q->count >= OCAS_CDM_FIFO_CAPACITY) : false;
}

bool cdm_fifo_is_empty(const CDMFifoQueue* q) {
    return q ? (q->count == 0) : true;
}

void cdm_fifo_reset(CDMFifoQueue* q) {
    cdm_fifo_init(q);
}
