#include "persistence/mission_ledger.hpp"

namespace ocas {

MissionLedger::MissionLedger() noexcept {
    for (std::size_t i = kCapacity; i-- > 0;) {
        pool_[i].next = free_;
        free_ = &pool_[i];
    }
}

LedgerRecord& MissionLedger::append(const LedgerRecord& r) noexcept {
    if (free_ == nullptr) {
        Node* old = head_;
        head_ = old->next;
        head_->prev = nullptr;
        old->next = free_;
        free_ = old;
        --size_;
        ++evicted_;
    }
    Node* n = free_;
    free_ = n->next;
    n->record = r;
    n->next = nullptr;
    n->prev = tail_;
    if (tail_ != nullptr) tail_->next = n;
    else head_ = n;
    tail_ = n;
    ++size_;
    return n->record;
}

}  // namespace ocas
