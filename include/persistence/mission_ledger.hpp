// ============================================================================
// OCAS Module 5b: Bidirectional Mission Ledger
// ============================================================================
// Doubly linked list of LedgerRecord nodes carved from a fixed node pool
// (std::array member), so append never touches the heap. When the pool is
// exhausted the oldest node is evicted and recycled; evictions are counted.
//   append                      O(1)
//   set_status                  O(1), via a generation-checked handle
//   traverse_forward/backward   O(n), chronological / reverse
//   committed_dv_since_cycle    backward walk, stops at the first older record
// The ledger is not copyable or movable: nodes point into its own pool.
// ============================================================================
#ifndef OCAS_PERSISTENCE_MISSION_LEDGER_HPP
#define OCAS_PERSISTENCE_MISSION_LEDGER_HPP

#include "core/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ocas {

class MissionLedger {
    struct Node {
        LedgerRecord record{};
        Node* prev = nullptr;
        Node* next = nullptr;
        std::uint32_t generation = 0;
    };

public:
    static constexpr std::size_t kCapacity = 1024;

    struct Handle {
        Node* node = nullptr;
        std::uint32_t generation = 0;
    };

    MissionLedger() noexcept;
    MissionLedger(const MissionLedger&) = delete;
    MissionLedger& operator=(const MissionLedger&) = delete;

    Handle append(const LedgerRecord& r) noexcept;
    /// False if the handle is stale (its node was evicted and recycled).
    bool set_status(Handle h, AdvisoryStatus s) noexcept;
    [[nodiscard]] const LedgerRecord* get(Handle h) const noexcept;

    template <typename Fn>
    void traverse_forward(Fn&& fn) const {
        for (const Node* n = head_; n != nullptr; n = n->next) {
            fn(n->record);
        }
    }

    template <typename Fn>
    void traverse_backward(Fn&& fn) const {
        for (const Node* n = tail_; n != nullptr; n = n->prev) {
            fn(n->record);
        }
    }

    /// Committed delta-v from the newest record back to `cycle` (inclusive).
    [[nodiscard]] double committed_dv_since_cycle(std::uint64_t cycle) const noexcept;
    /// Committed delta-v across every retained record (backward traversal).
    [[nodiscard]] double committed_dv_total() const noexcept;

    void clear() noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] std::uint64_t evicted_count() const noexcept { return evicted_; }

private:
    Node* allocate() noexcept;
    void unlink_head() noexcept;

    std::array<Node, kCapacity> pool_{};
    Node* head_ = nullptr;
    Node* tail_ = nullptr;
    Node* free_ = nullptr;  ///< Singly linked through Node::next
    std::size_t size_ = 0;
    std::uint64_t evicted_ = 0;
};

}  // namespace ocas

#endif  // OCAS_PERSISTENCE_MISSION_LEDGER_HPP
