#ifndef OCAS_CORE_ALLOC_TRACKER_HPP
#define OCAS_CORE_ALLOC_TRACKER_HPP

#include <cstdint>

namespace ocas::alloc_tracker {

// Counts C++ operator new calls only, not direct malloc calls inside the C library.
std::uint64_t allocation_count() noexcept;

}  // namespace ocas::alloc_tracker

#endif
