#include "ladder/history.hpp"
#include <utility>

namespace ladder {

// ---------------------------------------------------------------- Commands

ReplaceRung::ReplaceRung(std::size_t index, Rung before, Rung after, std::string what)
    : index_(index), before_(std::move(before)), after_(std::move(after)), what_(std::move(what)) {}

void ReplaceRung::apply(std::vector<Rung>& rungs) const {
    rungs.at(index_) = after_;
}

void ReplaceRung::revert(std::vector<Rung>& rungs) const {
    rungs.at(index_) = before_;
}

bool ReplaceRung::absorb(const Command& next) {
    const auto* edit = dynamic_cast<const ReplaceRung*>(&next);
    if (edit == nullptr || edit->index_ != index_) {
        return false;
    }
    after_ = edit->after_; // from our "before" straight to its "after"
    selectionAfter = next.selectionAfter;
    return true;
}

InsertNetwork::InsertNetwork(std::size_t index, Rung rung) : index_(index), rung_(std::move(rung)) {}

void InsertNetwork::apply(std::vector<Rung>& rungs) const {
    rungs.insert(rungs.begin() + static_cast<std::ptrdiff_t>(index_), rung_);
}

void InsertNetwork::revert(std::vector<Rung>& rungs) const {
    rungs.erase(rungs.begin() + static_cast<std::ptrdiff_t>(index_));
}

bool InsertNetwork::absorb(const Command& next) {
    const auto* edit = dynamic_cast<const ReplaceRung*>(&next);
    if (edit == nullptr || edit->index() != index_) {
        return false;
    }
    rung_ = edit->after(); // insert the network as it is after the edit
    selectionAfter = next.selectionAfter;
    return true;
}

RemoveNetwork::RemoveNetwork(std::size_t index, Rung rung) : index_(index), rung_(std::move(rung)) {}

void RemoveNetwork::apply(std::vector<Rung>& rungs) const {
    rungs.erase(rungs.begin() + static_cast<std::ptrdiff_t>(index_));
}

void RemoveNetwork::revert(std::vector<Rung>& rungs) const {
    rungs.insert(rungs.begin() + static_cast<std::ptrdiff_t>(index_), rung_);
}

SwapNetworks::SwapNetworks(std::size_t first, std::size_t second) : first_(first), second_(second) {}

void SwapNetworks::apply(std::vector<Rung>& rungs) const {
    std::swap(rungs.at(first_), rungs.at(second_));
}

// ---------------------------------------------------------------- History

void History::perform(std::unique_ptr<Command> command, std::vector<Rung>& rungs) {
    command->apply(rungs);
    push(std::move(command));
}

void History::record(std::unique_ptr<Command> command, bool mergeWithLast) {
    if (mergeWithLast && !done_.empty() && undone_.empty() && done_.back()->absorb(*command)) {
        if (saved_ && *saved_ == done_.size()) {
            saved_.reset(); // the saved state was the one before the merge: it's gone now
        }
        return;
    }
    push(std::move(command));
}

void History::push(std::unique_ptr<Command> command) {
    if (saved_ && *saved_ > done_.size()) {
        saved_.reset(); // the saved state was among the undone commands, which are now forgotten
    }
    undone_.clear();
    done_.push_back(std::move(command));
    if (done_.size() > limit_) {
        done_.erase(done_.begin()); // forget the oldest step
        if (saved_) {
            saved_ = *saved_ == 0 ? std::optional<std::size_t>() : std::optional(*saved_ - 1);
        }
    }
}

std::string History::undoDescription() const {
    return done_.empty() ? std::string() : done_.back()->description();
}

std::string History::redoDescription() const {
    return undone_.empty() ? std::string() : undone_.back()->description();
}

std::optional<Selection> History::undo(std::vector<Rung>& rungs) {
    if (done_.empty()) {
        return std::nullopt;
    }
    auto command = std::move(done_.back());
    done_.pop_back();
    command->revert(rungs);
    auto selection = command->selectionBefore;
    undone_.push_back(std::move(command));
    return selection;
}

std::optional<Selection> History::redo(std::vector<Rung>& rungs) {
    if (undone_.empty()) {
        return std::nullopt;
    }
    auto command = std::move(undone_.back());
    undone_.pop_back();
    command->apply(rungs);
    auto selection = command->selectionAfter;
    done_.push_back(std::move(command));
    return selection;
}

void History::markSaved() {
    saved_ = done_.size();
}

bool History::modified() const {
    return !saved_ || *saved_ != done_.size();
}

} // namespace ladder
