// ============================================================================
// OCAS memory-determinism hook. src/core/alloc_tracker.cpp replaces the global
// operator new/delete family with counting versions. Linked only into the
// flight executable, which reads the counter before and after the cyclic loop.
// Limitation: counts C++ operator new only, not direct malloc calls made
// inside the C library.
// ============================================================================
#ifndef OCAS_CORE_ALLOC_TRACKER_HPP
#define OCAS_CORE_ALLOC_TRACKER_HPP

#include <cstdint>

namespace ocas::alloc_tracker {

std::uint64_t allocation_count() noexcept;
std::uint64_t allocated_bytes() noexcept;

}  // namespace ocas::alloc_tracker

#endif  // OCAS_CORE_ALLOC_TRACKER_HPP
