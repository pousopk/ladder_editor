#pragma once
#include "ladder/block.hpp"
#include "ladder/layout.hpp"
#include "ladder/rung.hpp"
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Undo and redo, with the command pattern: every change to a KOP block (its
// networks or its interface) is a Command object that can apply itself and
// revert itself, and the History keeps the commands in order. Each block has
// its own history, as in most editors with several open documents.
//
//   done:    [insert contact] [branch around start] [edit operand]   <- undo takes the last one
//   undone:  [delete network]                                          <- redo puts it back
//
// Doing something new after an undo forgets what was undone: the history is
// a line, not a tree.
namespace ladder {

// What's selected: a network, and maybe an element or coil in it.
struct Selection {
    std::size_t rung = 0;
    std::optional<ItemPath> item;
    bool operator==(const Selection&) const = default;
};

class Command {
public:
    virtual ~Command() = default;

    virtual void apply(Block& block) const = 0;  // do, and redo
    virtual void revert(Block& block) const = 0; // undo
    virtual std::string description() const = 0; // "insert contact", for the Undo button

    // Takes `next` into this command, so the two become one step, if that
    // makes sense (an operand typed right after inserting the element).
    // Returns false if it doesn't.
    virtual bool absorb(const Command& next) {
        (void)next;
        return false;
    }

    // The selection to show after doing or undoing the command.
    std::optional<Selection> selectionBefore;
    std::optional<Selection> selectionAfter;
};

// Any change inside one network: the rung as it was, and as it is after.
// Rungs are plain values, so keeping both is simple and always exact, even
// for edits that rearrange the tree.
class ReplaceRung : public Command {
public:
    ReplaceRung(std::size_t index, Rung before, Rung after, std::string what);
    void apply(Block& block) const override;
    void revert(Block& block) const override;
    std::string description() const override { return what_; }
    bool absorb(const Command& next) override;

    std::size_t index() const { return index_; }
    const Rung& after() const { return after_; }

private:
    std::size_t index_;
    Rung before_;
    Rung after_;
    std::string what_;
};

class InsertNetwork : public Command {
public:
    InsertNetwork(std::size_t index, Rung rung);
    void apply(Block& block) const override;
    void revert(Block& block) const override;
    std::string description() const override { return "add network"; }
    bool absorb(const Command& next) override; // the new network's first edits

private:
    std::size_t index_;
    Rung rung_;
};

class RemoveNetwork : public Command {
public:
    RemoveNetwork(std::size_t index, Rung rung);
    void apply(Block& block) const override;
    void revert(Block& block) const override;
    std::string description() const override { return "delete network"; }

private:
    std::size_t index_;
    Rung rung_;
};

// A block's interface: its variables, and a FUNCTION's return type.
struct Interface {
    std::vector<Variable> variables;
    std::string returnType;
    bool operator==(const Interface&) const = default;
};

Interface interfaceOf(const Block& block);

// Any change to the block's interface: as it was, and as it is after.
class ReplaceInterface : public Command {
public:
    ReplaceInterface(Interface before, Interface after, std::string what);
    void apply(Block& block) const override;
    void revert(Block& block) const override;
    std::string description() const override { return what_; }

private:
    Interface before_;
    Interface after_;
    std::string what_;
};

// Swaps two neighbouring networks.
class SwapNetworks : public Command {
public:
    SwapNetworks(std::size_t first, std::size_t second);
    void apply(Block& block) const override;
    void revert(Block& block) const override { apply(block); } // swapping again undoes it
    std::string description() const override { return "move network"; }

private:
    std::size_t first_;
    std::size_t second_;
};

class History {
public:
    explicit History(std::size_t limit = 500) : limit_(limit) {}

    // Applies `command` to `block` and records it.
    void perform(std::unique_ptr<Command> command, Block& block);

    // Records a command whose change is already made (like an operand typed
    // into a field). With `mergeWithLast`, it becomes part of the last step
    // if that step can absorb it.
    void record(std::unique_ptr<Command> command, bool mergeWithLast = false);

    bool canUndo() const { return !done_.empty(); }
    bool canRedo() const { return !undone_.empty(); }
    std::string undoDescription() const; // "" when there's nothing to undo
    std::string redoDescription() const;

    // Undo and redo return the selection that goes with the state they restore.
    std::optional<Selection> undo(Block& block);
    std::optional<Selection> redo(Block& block);

    // Saving: remember this point, so modified() is false exactly when the
    // networks are as they were saved (also after undoing back to it).
    void markSaved();
    bool modified() const;

    std::size_t size() const { return done_.size(); }

private:
    void push(std::unique_ptr<Command> command);

    std::vector<std::unique_ptr<Command>> done_;
    std::vector<std::unique_ptr<Command>> undone_;
    std::size_t limit_;
    // How many commands were done when the file was saved; none if that
    // state can't be reached any more (it was undone, then replaced).
    std::optional<std::size_t> saved_ = 0;
};

} // namespace ladder
