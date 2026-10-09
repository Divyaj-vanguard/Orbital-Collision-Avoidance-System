// Counting replacements for the global allocation functions (see header).
#include "core/alloc_tracker.hpp"

#include <atomic>
#include <cstdlib>
#include <new>

namespace {
std::atomic<std::uint64_t> g_allocs{0};
std::atomic<std::uint64_t> g_bytes{0};

void* counted_alloc(std::size_t size) {
    g_allocs.fetch_add(1, std::memory_order_relaxed);
    g_bytes.fetch_add(size, std::memory_order_relaxed);
    if (void* p = std::malloc(size == 0 ? 1 : size)) {
        return p;
    }
    throw std::bad_alloc();
}

void* counted_aligned_alloc(std::size_t size, std::align_val_t al) {
    g_allocs.fetch_add(1, std::memory_order_relaxed);
    g_bytes.fetch_add(size, std::memory_order_relaxed);
    const auto a = static_cast<std::size_t>(al);
    const std::size_t rounded = ((size == 0 ? 1 : size) + a - 1) / a * a;
    if (void* p = std::aligned_alloc(a, rounded)) {
        return p;
    }
    throw std::bad_alloc();
}
}  // namespace

namespace ocas::alloc_tracker {
std::uint64_t allocation_count() noexcept { return g_allocs.load(std::memory_order_relaxed); }
std::uint64_t allocated_bytes() noexcept { return g_bytes.load(std::memory_order_relaxed); }
}  // namespace ocas::alloc_tracker

void* operator new(std::size_t size) { return counted_alloc(size); }
void* operator new[](std::size_t size) { return counted_alloc(size); }
void* operator new(std::size_t size, std::align_val_t al) { return counted_aligned_alloc(size, al); }
void* operator new[](std::size_t size, std::align_val_t al) { return counted_aligned_alloc(size, al); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
