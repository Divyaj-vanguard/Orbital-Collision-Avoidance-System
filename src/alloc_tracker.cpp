// Replaces global operator new/delete with counting versions.
#include "alloc_tracker.hpp"

#include <atomic>
#include <cstdlib>
#include <new>

namespace {

std::atomic<std::uint64_t> g_allocs{0};

void* counted_alloc(std::size_t size, std::size_t align = 0) {
    g_allocs.fetch_add(1, std::memory_order_relaxed);
    if (size == 0) size = 1;
    void* p = align == 0 ? std::malloc(size) : std::aligned_alloc(align, (size + align - 1) / align * align);
    if (p == nullptr) throw std::bad_alloc();
    return p;
}

}  // namespace

std::uint64_t ocas::allocation_count() noexcept { return g_allocs.load(std::memory_order_relaxed); }

void* operator new(std::size_t n) { return counted_alloc(n); }
void* operator new[](std::size_t n) { return counted_alloc(n); }
void* operator new(std::size_t n, std::align_val_t a) { return counted_alloc(n, static_cast<std::size_t>(a)); }
void* operator new[](std::size_t n, std::align_val_t a) { return counted_alloc(n, static_cast<std::size_t>(a)); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
