#ifndef OCAS_ALLOC_TRACKER_HPP
#define OCAS_ALLOC_TRACKER_HPP

#include <cstdint>

namespace ocas {

// Counts C++ operator new calls (not direct malloc). Linked into the flight executable only.
std::uint64_t allocation_count() noexcept;

}  // namespace ocas

#endif
