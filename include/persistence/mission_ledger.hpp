#ifndef OCAS_PERSISTENCE_MISSION_LEDGER_HPP
#define OCAS_PERSISTENCE_MISSION_LEDGER_HPP

#include "core/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ocas {

// Doubly linked list on a fixed node pool. When the pool is full the oldest record is
// evicted and counted. A returned record stays valid until kCapacity more appends.
class MissionLedger {
public:
    static constexpr std::size_t kCapacity = 1024;

    MissionLedger() noexcept;
    MissionLedger(const MissionLedger&) = delete;
    MissionLedger& operator=(const MissionLedger&) = delete;

    LedgerRecord& append(const LedgerRecord& r) noexcept;

    template <typename Fn>
    void traverse_forward(Fn&& fn) const {
        for (const Node* n = head_; n != nullptr; n = n->next) fn(n->record);
    }

    template <typename Fn>
    void traverse_backward(Fn&& fn) const {
        for (const Node* n = tail_; n != nullptr; n = n->prev) fn(n->record);
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::uint64_t evicted_count() const noexcept { return evicted_; }

private:
    struct Node {
        LedgerRecord record{};
        Node* prev = nullptr;
        Node* next = nullptr;
    };

    std::array<Node, kCapacity> pool_{};
    Node* head_ = nullptr;
    Node* tail_ = nullptr;
    Node* free_ = nullptr;
    std::size_t size_ = 0;
    std::uint64_t evicted_ = 0;
};

}  // namespace ocas

#endif
