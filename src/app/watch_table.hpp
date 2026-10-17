#pragma once
#include "app/widget_positions.hpp"
#include "ladder/block.hpp"
#include "online/session.hpp"
#include <optional>
#include <string>
#include <vector>

namespace app {

// The project's watch table, like a watch and force table in TIA Portal:
// a list of variables chosen by hand, from any block, with their live
// values; for each one a value ready to write, and buttons to write it
// once (Set) or hold it every cycle (Force).
//
// Offline it's just the list, to prepare; online the values come from the
// session, and soft_plc's own variable list says which names it doesn't know.
class WatchTable {
public:
    // Draws the table and handles its buttons. Returns true if `entries` changed.
    bool draw(std::vector<ladder::WatchEntry>& entries, online::Session* session);

    // Online: the paths the last frame showed, to be read for the next one.
    const std::vector<std::string>& watched() const { return watched_; }
    // "path 0", "modify 0", "set 0", "force 0", "unforce 0", "delete 0", "+ line", "set all", "unforce all".
    const WidgetPositions& widgets() const { return widgets_; }

private:
    // What soft_plc calls the line's variable, if it can be read.
    std::optional<std::string> pathOf(const ladder::WatchEntry& entry, const online::Session* session) const;

    std::vector<std::string> watched_;
    WidgetPositions widgets_;
};

} // namespace app
