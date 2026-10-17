#include "ladder/edit.hpp"
#include "ladder/history.hpp"
#include "ladder/notation.hpp"
#include <doctest/doctest.h>

using namespace ladder;

namespace {

// A KOP block with these networks.
Block rungs(std::initializer_list<const char*> texts) {
    Block result;
    for (const char* text : texts) {
        result.networks.push_back(parseRung(text).value());
    }
    return result;
}

std::vector<std::string> texts(const Block& rs) {
    std::vector<std::string> result;
    for (const auto& r : rs.networks) {
        result.push_back(formatRung(r));
    }
    return result;
}

// An edit of one rung as a command: change a copy, keep both.
std::unique_ptr<Command> edit(const Block& rs, std::size_t index, const char* after, const char* what) {
    return std::make_unique<ReplaceRung>(index, rs.networks[index], parseRung(after).value(), what);
}

} // namespace

TEST_CASE("Undo and redo walk back and forth through the commands") {
    auto rs = rungs({"a => x"});
    History history;
    CHECK_FALSE(history.canUndo());
    history.perform(edit(rs, 0, "a b => x", "insert contact"), rs);
    history.perform(edit(rs, 0, "(a | c) b => x", "add branch"), rs);
    CHECK(texts(rs) == std::vector<std::string>{"(a | c) b => x"});
    CHECK(history.undoDescription() == "add branch");

    history.undo(rs);
    CHECK(texts(rs) == std::vector<std::string>{"a b => x"});
    CHECK(history.redoDescription() == "add branch");
    history.undo(rs);
    CHECK(texts(rs) == std::vector<std::string>{"a => x"});
    CHECK_FALSE(history.canUndo());
    history.undo(rs); // nothing more to undo: no change
    CHECK(texts(rs) == std::vector<std::string>{"a => x"});

    history.redo(rs);
    history.redo(rs);
    CHECK(texts(rs) == std::vector<std::string>{"(a | c) b => x"});
    CHECK_FALSE(history.canRedo());
}

TEST_CASE("Doing something new after an undo forgets what was undone") {
    auto rs = rungs({"a => x"});
    History history;
    history.perform(edit(rs, 0, "a b => x", "insert"), rs);
    history.undo(rs);
    CHECK(history.canRedo());
    history.perform(edit(rs, 0, "a c => x", "insert"), rs);
    CHECK_FALSE(history.canRedo());
    history.undo(rs);
    CHECK(texts(rs) == std::vector<std::string>{"a => x"});
}

TEST_CASE("Network commands") {
    auto rs = rungs({"a => x", "b => y"});
    History history;
    history.perform(std::make_unique<InsertNetwork>(1, parseRung("=> new").value()), rs);
    CHECK(texts(rs) == std::vector<std::string>{"a => x", "=> new", "b => y"});
    history.perform(std::make_unique<SwapNetworks>(1, 2), rs);
    CHECK(texts(rs) == std::vector<std::string>{"a => x", "b => y", "=> new"});
    history.perform(std::make_unique<RemoveNetwork>(0, rs.networks[0]), rs);
    CHECK(texts(rs) == std::vector<std::string>{"b => y", "=> new"});

    history.undo(rs);
    CHECK(texts(rs) == std::vector<std::string>{"a => x", "b => y", "=> new"});
    history.undo(rs);
    CHECK(texts(rs) == std::vector<std::string>{"a => x", "=> new", "b => y"});
    history.undo(rs);
    CHECK(texts(rs) == std::vector<std::string>{"a => x", "b => y"});
}

TEST_CASE("Undo restores the selection that went with the state") {
    auto rs = rungs({"a => x"});
    History history;
    auto command = edit(rs, 0, "a b => x", "insert contact");
    command->selectionBefore = Selection{0, ElementPath{{0}}};
    command->selectionAfter = Selection{0, ElementPath{{1}}};
    history.perform(std::move(command), rs);
    CHECK(history.undo(rs) == Selection{0, ElementPath{{0}}});
    CHECK(history.redo(rs) == Selection{0, ElementPath{{1}}});
}

TEST_CASE("An operand typed right after inserting merges into one step") {
    auto rs = rungs({"a => x"});
    History history;
    history.perform(edit(rs, 0, "a b => x", "insert contact"), rs);
    // The user types into the new contact: the change is made directly...
    rs.networks[0] = parseRung("a guard => x").value();
    // ...and recorded afterwards, as part of the insertion.
    history.record(std::make_unique<ReplaceRung>(0, parseRung("a b => x").value(), rs.networks[0], "edit operand"),
                   true);
    CHECK(history.size() == 1);
    CHECK(history.undoDescription() == "insert contact");
    history.undo(rs);
    CHECK(texts(rs) == std::vector<std::string>{"a => x"}); // one undo takes both away
    history.redo(rs);
    CHECK(texts(rs) == std::vector<std::string>{"a guard => x"});
}

TEST_CASE("A new network merges its first edit too, but edits of other networks stay separate") {
    auto rs = rungs({"a => x"});
    History history;
    history.perform(std::make_unique<InsertNetwork>(1, parseRung("=> ").value_or(Rung{})), rs);
    rs.networks[1] = parseRung("=> ready").value();
    history.record(std::make_unique<ReplaceRung>(1, Rung{}, rs.networks[1], "edit operand"), true);
    CHECK(history.size() == 1);

    rs.networks[0] = parseRung("b => x").value();
    history.record(std::make_unique<ReplaceRung>(0, parseRung("a => x").value(), rs.networks[0], "edit operand"), true);
    CHECK(history.size() == 2); // another network: its own step

    history.undo(rs);
    history.undo(rs);
    CHECK(texts(rs) == std::vector<std::string>{"a => x"});
    history.redo(rs);
    CHECK(texts(rs) == std::vector<std::string>{"a => x", "=> ready"});
}

TEST_CASE("Modified means: not as it was saved, also after undoing back to it") {
    auto rs = rungs({"a => x"});
    History history;
    CHECK_FALSE(history.modified()); // just opened
    history.perform(edit(rs, 0, "a b => x", "insert"), rs);
    CHECK(history.modified());
    history.undo(rs);
    CHECK_FALSE(history.modified()); // back to the saved state
    history.redo(rs);
    history.markSaved();
    CHECK_FALSE(history.modified());
    history.undo(rs);
    CHECK(history.modified()); // the saved state has b
    history.perform(edit(rs, 0, "a c => x", "insert"), rs);
    history.undo(rs);
    CHECK(history.modified()); // the saved state (with b) can't be reached any more
}

TEST_CASE("The history keeps only its newest steps") {
    auto rs = rungs({"a => x"});
    History history(3);
    for (const char* text : {"a => x1", "a => x2", "a => x3", "a => x4", "a => x5"}) {
        history.perform(edit(rs, 0, text, "edit"), rs);
    }
    CHECK(history.size() == 3);
    while (history.canUndo()) {
        history.undo(rs);
    }
    CHECK(texts(rs) == std::vector<std::string>{"a => x2"}); // the two oldest steps are gone
    CHECK(history.modified());
}

TEST_CASE("Interface changes are commands too") {
    Block block = rungs({"a => x"});
    History history;
    auto after = interfaceOf(block);
    after.variables.push_back(Variable{"start", "BOOL", Section::Local, "", "%IX0.0", "start button"});
    history.perform(std::make_unique<ReplaceInterface>(interfaceOf(block), after, "add variable"), block);
    REQUIRE(block.variables.size() == 1);
    CHECK(history.undoDescription() == "add variable");
    history.undo(block);
    CHECK(block.variables.empty());
    history.redo(block);
    CHECK(block.variables.at(0).address == "%IX0.0");
}
