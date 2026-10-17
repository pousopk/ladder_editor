#pragma once
#include <optional>
#include <string>

// What to ask soft_plc for: the monitor path of an operand, as the block
// sees it from one of its instances.
namespace online {

// `instance` is the path of the instance being watched ("p1", "line.p1"),
// empty for the PROGRAM. Gives nothing for a literal (TRUE, 30, T#5s),
// which has no value to read. An address (%IX0.0) reads the variable the
// translation generated for it (only the PROGRAM can use addresses).
std::optional<std::string> monitorPath(const std::string& operand, const std::string& instance);

// TRUE or FALSE, written as a literal: its value is known without asking.
std::optional<bool> boolLiteral(const std::string& operand);

} // namespace online
