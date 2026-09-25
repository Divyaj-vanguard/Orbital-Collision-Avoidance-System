/**
 * ============================================================================
 * OCAS: Autonomous Orbital Collision Avoidance System
 * Flight Software Architecture - Core Ingestion Layer (Pure C11)
 * Team ID: DSCPP-III-2026-T018 | Graphic Era University
 * ============================================================================
 * Safety-Critical Constraint: Deterministic static memory layout. Zero heap
 * allocations (malloc/free) after startup initialization. Cache-aligned
 * structures for optimal hardware memory-subsystem performance.
 * ============================================================================
 */

#ifndef OCAS_C_INGESTION_H
#define OCAS_C_INGESTION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
#include <cstdalign>
extern "C" {
#else
#include <stdalign.h>
#endif

#define OCAS_ALIGNMENT 64
#define OCAS_RING_BUFFER_CAPACITY 64
#define OCAS_CDM_FIFO_CAPACITY    32
#define OCAS_NAME_LEN             32

/**
 * @brief Telemetry status flag bitmasks.
 */
#define OCAS_STATUS_NOMINAL      (1u << 0)
#define OCAS_STATUS_MANEUVERING  (1u << 1)
#define OCAS_STATUS_ANOMALY      (1u << 2)
#define OCAS_STATUS_DEGRADED     (1u << 3)
#define OCAS_STATUS_ROLLBACK     (1u << 4)

/**
 * @brief Spacecraft kinematic and physical state structure.
 * 64-byte cache line aligned for deterministic memory access.
 */
typedef struct {
    alignas(OCAS_ALIGNMENT) double epoch_s; /**< Telemetry timestamp (seconds since epoch) */
    double r[3];             /**< Position vector (x, y, z) in ECI frame [km] */
    double v[3];             /**< Velocity vector (vx, vy, vz) in ECI frame [km/s] */
    double dry_mass_kg;      /**< Spacecraft dry structural mass [kg] */
    double fuel_mass_kg;     /**< Usable propellant mass remaining [kg] */
    uint32_t status_flags;   /**< Bitwise operational status flags */
    uint32_t sequence_id;    /**< Monotonically increasing packet sequence ID */
    uint8_t  _reserved[16];  /**< Padding to maintain 64-byte alignment cache bounds */
} SatelliteState;

/**
 * @brief Conjunction Data Message (CDM) alert packet.
 * 64-byte cache line aligned. Conforms to CCSDS CDM standard features.
 */
typedef struct {
    alignas(OCAS_ALIGNMENT) uint32_t object_id; /**< Catalog ID of the secondary conjunction object */
    char     object_name[OCAS_NAME_LEN]; /**< Human-readable designator */
    double   d_miss_m;            /**< Estimated miss distance at TCA [meters] */
    double   v_rel_kms;           /**< Relative velocity magnitude [km/s] */
    double   t_tca_s;             /**< Time remaining until Closest Approach [seconds] */
    double   covariance[3][3];    /**< 3x3 Positional error covariance matrix [m^2] */
    double   epoch_tca_s;         /**< Absolute UTC epoch timestamp of TCA [seconds] */
    uint32_t alert_id;            /**< Ingestion sequence alert ID */
    uint8_t  _reserved[20];       /**< Alignment padding */
} CDMAlert;

/**
 * @brief Fixed-size Circular Ring Buffer for high-rate satellite telemetry frames.
 * Deterministic overwrite-oldest policy ensures recent telemetry is never dropped.
 */
typedef struct {
    SatelliteState buffer[OCAS_RING_BUFFER_CAPACITY];
    size_t head;                     /**< Write cursor index */
    size_t count;                    /**< Current active item count */
    uint64_t total_pushed;           /**< Lifetime pushed frames */
    uint64_t total_overwritten;      /**< Total frames evicted due to buffer full */
    bool overflow_occurred;          /**< Flag set upon first overwrite event */
} TelemetryRingBuffer;

/**
 * @brief Fixed-size FIFO Queue for incoming Conjunction Data Messages (CDMs).
 * Deterministic drop-newest-on-full policy to prevent queue memory exhaustion.
 */
typedef struct {
    CDMAlert buffer[OCAS_CDM_FIFO_CAPACITY];
    size_t head;                     /**< Read cursor index */
    size_t count;                    /**< Current item count in queue */
    uint64_t total_enqueued;         /**< Lifetime enqueued CDMs */
    uint64_t total_dropped;          /**< Lifetime dropped CDMs due to full queue */
    bool overflow_occurred;          /**< Flag set if drop occurs */
} CDMFifoQueue;

/* ============================================================================
 * Telemetry Ring Buffer API (Static Allocation, Zero Heap)
 * ============================================================================ */

void ring_buffer_init(TelemetryRingBuffer* rb);
bool ring_buffer_push(TelemetryRingBuffer* rb, const SatelliteState* state);
bool ring_buffer_pop(TelemetryRingBuffer* rb, SatelliteState* out_state);
bool ring_buffer_peek_latest(const TelemetryRingBuffer* rb, SatelliteState* out_state);
bool ring_buffer_peek_at(const TelemetryRingBuffer* rb, size_t index, SatelliteState* out_state);
size_t ring_buffer_count(const TelemetryRingBuffer* rb);
bool ring_buffer_is_full(const TelemetryRingBuffer* rb);
void ring_buffer_reset(TelemetryRingBuffer* rb);

/* ============================================================================
 * CDM FIFO Queue API (Static Allocation, Zero Heap)
 * ============================================================================ */

void cdm_fifo_init(CDMFifoQueue* q);
bool cdm_fifo_push(CDMFifoQueue* q, const CDMAlert* alert);
bool cdm_fifo_pop(CDMFifoQueue* q, CDMAlert* out_alert);
bool cdm_fifo_peek(const CDMFifoQueue* q, CDMAlert* out_alert);
size_t cdm_fifo_count(const CDMFifoQueue* q);
bool cdm_fifo_is_full(const CDMFifoQueue* q);
bool cdm_fifo_is_empty(const CDMFifoQueue* q);
void cdm_fifo_reset(CDMFifoQueue* q);

#if defined(__cplusplus)
}
#endif

#endif /* OCAS_C_INGESTION_H */
