#include "st_support/highlight.hpp"
#include "st/builtins.hpp"
#include "st/functions.hpp"
#include "st/lexer.hpp"
#include "util/text.hpp"
#include <algorithm>
#include <optional>

namespace st_support {

namespace {

using softplc::st::Token;
using softplc::st::TokenKind;

// Where each line starts in the text, to turn (line, column) into an offset.
std::vector<std::size_t> lineStarts(std::string_view source) {
    std::vector<std::size_t> starts{0};
    for (std::size_t i = 0; i < source.size(); ++i) {
        if (source[i] == '\n') {
            starts.push_back(i + 1);
        }
    }
    return starts;
}

bool isBuiltinName(const std::string& upper) {
    for (const auto& block : softplc::st::builtinFunctionBlocks()) {
        if (softplc::toUpper(block.name) == upper) {
            return true;
        }
    }
    return softplc::st::findBuiltinFunction(upper).has_value();
}

std::optional<Style> styleOf(const Token& token) {
    switch (token.kind) {
    case TokenKind::Identifier: {
        const auto upper = softplc::toUpper(token.text);
        if (upper == "BOOL" || upper == "INT" || upper == "DINT" || upper == "REAL" || upper == "TIME") {
            return Style::Type;
        }
        if (isBuiltinName(upper)) {
            return Style::Builtin;
        }
        return std::nullopt; // a plain name
    }
    case TokenKind::Integer:
    case TokenKind::Real:
        return Style::Number;
    case TokenKind::Time:
        return Style::Time;
    case TokenKind::DirectAddress:
        return Style::Address;
    case TokenKind::True:
    case TokenKind::False:
        return Style::Literal;
    case TokenKind::Array:
    case TokenKind::Struct:
    case TokenKind::EndStruct:
        return Style::Type;
    case TokenKind::Assign:
    case TokenKind::Semicolon:
    case TokenKind::Colon:
    case TokenKind::Comma:
    case TokenKind::DotDot:
    case TokenKind::Dot:
    case TokenKind::LeftParen:
    case TokenKind::RightParen:
    case TokenKind::LeftBracket:
    case TokenKind::RightBracket:
    case TokenKind::Plus:
    case TokenKind::Minus:
    case TokenKind::Star:
    case TokenKind::Slash:
    case TokenKind::Power:
    case TokenKind::Equal:
    case TokenKind::NotEqual:
    case TokenKind::Less:
    case TokenKind::LessEqual:
    case TokenKind::Greater:
    case TokenKind::GreaterEqual:
        return Style::Operator;
    case TokenKind::EndOfFile:
        return std::nullopt;
    default:
        return Style::Keyword; // every other kind is a keyword: IF, VAR, AND, MOD, ...
    }
}

// Every non-space stretch in [from, to) of the text is a comment: between
// tokens, the lexer skips only spaces and comments. One span per line.
void commentsBetween(std::string_view source, std::size_t from, std::size_t to, const std::vector<std::size_t>& starts,
                     std::vector<Span>& spans) {
    if (from >= to) {
        return;
    }
    const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    auto line = static_cast<std::size_t>(std::upper_bound(starts.begin(), starts.end(), from) - starts.begin()) - 1;
    for (; line < starts.size() && starts[line] < to; ++line) {
        auto begin = std::max(from, starts[line]);
        const auto lineEnd = line + 1 < starts.size() ? starts[line + 1] - 1 : source.size(); // its '\n'
        auto end = std::min(to, lineEnd);
        while (begin < end && isSpace(source[begin]))
            ++begin;
        while (end > begin && isSpace(source[end - 1]))
            --end;
        if (begin < end) {
            spans.push_back(Span{line, begin - starts[line], end - begin, Style::Comment});
        }
    }
}

} // namespace

std::vector<Span> highlight(std::string_view source) {
    const auto starts = lineStarts(source);
    const auto toLineColumn = [&](std::size_t offset) {
        const auto line =
            static_cast<std::size_t>(std::upper_bound(starts.begin(), starts.end(), offset) - starts.begin()) - 1;
        return std::pair{line, offset - starts[line]};
    };

    std::vector<Span> spans;
    std::size_t position = 0; // where the lexer starts reading this round
    std::size_t previousEnd = 0;
    while (position < source.size()) {
        // Lex from `position`. If the lexer gives up somewhere, colour what
        // it read before that, skip the character it stopped at, and go on.
        const auto rest = source.substr(position);
        const auto restStarts = lineStarts(rest);
        const auto offsetIn = [&](const softplc::st::SourceLocation& location) {
            return position + restStarts[location.line - 1] + location.column - 1;
        };
        auto tokens = softplc::st::tokenize(rest);
        std::size_t readable = source.size();
        bool unclosedComment = false;
        if (!tokens) {
            readable = offsetIn(tokens.error().location);
            unclosedComment = tokens.error().message.find("comment is never closed") != std::string::npos;
            tokens = softplc::st::tokenize(source.substr(position, readable - position));
        }
        if (tokens) {
            for (const auto& token : *tokens) {
                if (token.kind == TokenKind::EndOfFile) {
                    break;
                }
                const auto start = offsetIn(token.location);
                commentsBetween(source, previousEnd, start, starts, spans);
                if (const auto style = styleOf(token)) {
                    const auto [line, column] = toLineColumn(start);
                    spans.push_back(Span{line, column, token.text.size(), *style});
                }
                previousEnd = start + token.text.size();
            }
        }
        commentsBetween(source, previousEnd, readable, starts, spans);
        if (unclosedComment) {
            commentsBetween(source, readable, source.size(), starts, spans); // everything after it is the comment
            break;
        }
        position = readable + 1; // past the character the lexer couldn't read
        previousEnd = position;
    }
    return spans;
}

} // namespace st_support
