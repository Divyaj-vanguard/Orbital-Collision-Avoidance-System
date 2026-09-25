/**
 * ============================================================================
 * OCAS: Autonomous Orbital Collision Avoidance System
 * Flight Software Architecture - Circular Ring Buffer Implementation (C11)
 * Team ID: DSCPP-III-2026-T018 | Graphic Era University
 * ============================================================================
 * Zero heap allocation. Deterministic overwrite-oldest policy.
 * ============================================================================
 */

#include "c_ingestion.h"
#include <string.h>

void ring_buffer_init(TelemetryRingBuffer* rb) {
    if (!rb) return;
    memset(rb->buffer, 0, sizeof(rb->buffer));
    rb->head = 0;
    rb->count = 0;
    rb->total_pushed = 0;
    rb->total_overwritten = 0;
    rb->overflow_occurred = false;
}

bool ring_buffer_push(TelemetryRingBuffer* rb, const SatelliteState* state) {
    if (!rb || !state) return false;

    const bool will_overwrite = (rb->count == OCAS_RING_BUFFER_CAPACITY);

    rb->buffer[rb->head] = *state;
    rb->head = (rb->head + 1) % OCAS_RING_BUFFER_CAPACITY;
    rb->total_pushed++;

    if (will_overwrite) {
        rb->total_overwritten++;
        rb->overflow_occurred = true;
        return false; /* Notified caller that oldest slot was overwritten */
    } else {
        rb->count++;
        return true;  /* Clean append */
    }
}

bool ring_buffer_pop(TelemetryRingBuffer* rb, SatelliteState* out_state) {
    if (!rb || !out_state || rb->count == 0) return false;

    /* Oldest frame starts at (head + CAP - count) % CAP */
    const size_t oldest_idx = (rb->head + OCAS_RING_BUFFER_CAPACITY - rb->count) % OCAS_RING_BUFFER_CAPACITY;
    *out_state = rb->buffer[oldest_idx];
    rb->count--;
    return true;
}

bool ring_buffer_peek_latest(const TelemetryRingBuffer* rb, SatelliteState* out_state) {
    if (!rb || !out_state || rb->count == 0) return false;

    const size_t latest_idx = (rb->head + OCAS_RING_BUFFER_CAPACITY - 1) % OCAS_RING_BUFFER_CAPACITY;
    *out_state = rb->buffer[latest_idx];
    return true;
}

bool ring_buffer_peek_at(const TelemetryRingBuffer* rb, size_t logical_idx, SatelliteState* out_state) {
    if (!rb || !out_state || logical_idx >= rb->count) return false;

    const size_t physical_idx = (rb->head + OCAS_RING_BUFFER_CAPACITY - rb->count + logical_idx) % OCAS_RING_BUFFER_CAPACITY;
    *out_state = rb->buffer[physical_idx];
    return true;
}

size_t ring_buffer_count(const TelemetryRingBuffer* rb) {
    return rb ? rb->count : 0;
}

bool ring_buffer_is_full(const TelemetryRingBuffer* rb) {
    return rb ? (rb->count == OCAS_RING_BUFFER_CAPACITY) : false;
}

void ring_buffer_reset(TelemetryRingBuffer* rb) {
    ring_buffer_init(rb);
}
