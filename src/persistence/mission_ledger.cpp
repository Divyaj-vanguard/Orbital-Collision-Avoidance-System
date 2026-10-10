// OCAS Module 5b implementation: pooled doubly linked mission ledger.
#include "persistence/mission_ledger.hpp"

namespace ocas {

MissionLedger::MissionLedger() noexcept { clear(); }

void MissionLedger::clear() noexcept {
    head_ = tail_ = nullptr;
    size_ = 0;
    free_ = nullptr;
    for (std::size_t i = kCapacity; i-- > 0;) {
        pool_[i].prev = nullptr;
        pool_[i].next = free_;
        ++pool_[i].generation;  // invalidates every outstanding handle
        free_ = &pool_[i];
    }
}

void MissionLedger::unlink_head() noexcept {
    Node* old = head_;
    head_ = old->next;
    if (head_ != nullptr) {
        head_->prev = nullptr;
    } else {
        tail_ = nullptr;
    }
    ++old->generation;
    old->next = free_;
    old->prev = nullptr;
    free_ = old;
    --size_;
}

MissionLedger::Node* MissionLedger::allocate() noexcept {
    if (free_ == nullptr) {
        unlink_head();  // pool exhausted: evict the oldest record
        ++evicted_;
    }
    Node* n = free_;
    free_ = n->next;
    n->next = nullptr;
    n->prev = nullptr;
    return n;
}

MissionLedger::Handle MissionLedger::append(const LedgerRecord& r) noexcept {
    Node* n = allocate();
    n->record = r;
    n->prev = tail_;
    if (tail_ != nullptr) {
        tail_->next = n;
    } else {
        head_ = n;
    }
    tail_ = n;
    ++size_;
    return Handle{n, n->generation};
}

bool MissionLedger::set_status(Handle h, AdvisoryStatus s) noexcept {
    if (h.node == nullptr || h.node->generation != h.generation) {
        return false;
    }
    h.node->record.status = s;
    return true;
}

const LedgerRecord* MissionLedger::get(Handle h) const noexcept {
    if (h.node == nullptr || h.node->generation != h.generation) {
        return nullptr;
    }
    return &h.node->record;
}

double MissionLedger::committed_dv_since_cycle(std::uint64_t cycle) const noexcept {
    double sum = 0.0;
    for (const Node* n = tail_; n != nullptr && n->record.cycle >= cycle; n = n->prev) {
        if (n->record.status == AdvisoryStatus::Committed) {
            sum += n->record.directive.delta_v_mps;
        }
    }
    return sum;
}

double MissionLedger::committed_dv_total() const noexcept { return committed_dv_since_cycle(0); }

}  // namespace ocas
