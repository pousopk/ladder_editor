#include "ladder/notation.hpp"
#include <cctype>
#include <fmt/format.h>
#include <vector>

namespace ladder {

namespace {

// "Not filled in yet": an empty operand is written as a lone ?, and read back as empty.
constexpr const char* kMissing = "?";

std::string shown(const std::string& operand) {
    return operand.empty() ? kMissing : operand;
}
std::string read(std::string word) {
    return word == kMissing ? std::string() : word;
}

} // namespace

// ---------------------------------------------------------------- Formatting

namespace {

std::string formatSeries(const Series& series);

} // namespace

std::string formatElement(const Element& element) {
    if (const auto* contact = std::get_if<Contact>(&element)) {
        return (contact->kind == ContactKind::NormallyClosed ? "/" : "") + shown(contact->operand);
    }
    if (const auto* box = std::get_if<Box>(&element)) {
        std::string text = "{" + box->type + " " + shown(box->instance);
        for (const auto& pin : box->inputs) {
            text += ", " + pin.name + " := " + pin.operand;
        }
        for (const auto& pin : box->outputs) {
            text += ", " + pin.name + " => " + pin.operand;
        }
        return text + "}";
    }
    const auto& parallel = std::get<Parallel>(element);
    std::string text = "(";
    for (std::size_t i = 0; i < parallel.branches.size(); ++i) {
        text += (i == 0 ? "" : " | ") + formatSeries(parallel.branches[i]);
    }
    return text + ")";
}

namespace {

std::string formatSeries(const Series& series) {
    std::string text;
    for (const auto& element : series.elements) {
        if (!text.empty()) {
            text += ' ';
        }
        text += formatElement(element);
    }
    return text;
}

std::string formatCoil(const Coil& coil) {
    switch (coil.kind) {
    case CoilKind::Normal:
        return shown(coil.operand);
    case CoilKind::Negated:
        return "/" + shown(coil.operand);
    case CoilKind::Set:
        return "S:" + shown(coil.operand);
    case CoilKind::Reset:
        return "R:" + shown(coil.operand);
    }
    return shown(coil.operand);
}

} // namespace

std::string formatRung(const Rung& rung) {
    std::string text = formatSeries(rung.logic);
    text += text.empty() ? "=>" : " =>";
    for (std::size_t i = 0; i < rung.coils.size(); ++i) {
        text += (i == 0 ? " " : ", ") + formatCoil(rung.coils[i]);
    }
    return text;
}

// ---------------------------------------------------------------- Parsing

namespace {

enum class TokenKind { Word, LeftParen, RightParen, Bar, LeftBrace, RightBrace, Comma, Arrow, Assign, End };

struct Token {
    TokenKind kind;
    std::string text;
    std::size_t column; // 1-based, for error messages
};

// Thrown inside the parser; parseRung() turns it into the error.
struct NotationError {
    std::string message;
};

std::vector<Token> tokenize(std::string_view text) {
    std::vector<Token> tokens;
    std::size_t i = 0;
    const auto at = [&](std::size_t k) { return k < text.size() ? text[k] : '\0'; };
    while (i < text.size()) {
        const char c = text[i];
        const auto column = i + 1;
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            ++i;
            continue;
        }
        const auto single = [&](TokenKind kind) {
            tokens.push_back(Token{kind, std::string(1, c), column});
            ++i;
        };
        switch (c) {
        case '(':
            single(TokenKind::LeftParen);
            continue;
        case ')':
            single(TokenKind::RightParen);
            continue;
        case '|':
            single(TokenKind::Bar);
            continue;
        case '{':
            single(TokenKind::LeftBrace);
            continue;
        case '}':
            single(TokenKind::RightBrace);
            continue;
        case ',':
            single(TokenKind::Comma);
            continue;
        default:
            break;
        }
        if (c == '=' && at(i + 1) == '>') {
            tokens.push_back(Token{TokenKind::Arrow, "=>", column});
            i += 2;
            continue;
        }
        if (c == ':' && at(i + 1) == '=') {
            tokens.push_back(Token{TokenKind::Assign, ":=", column});
            i += 2;
            continue;
        }
        // A word: everything up to a space, a symbol, "=>" or ":=". Operands
        // like t1.Q, %IX0.0, T#5s, a[1] or -5 are single words.
        const auto begin = i;
        while (i < text.size()) {
            const char d = text[i];
            const bool symbol = std::isspace(static_cast<unsigned char>(d)) != 0 || d == '(' || d == ')' || d == '|' ||
                                d == '{' || d == '}' || d == ',' || (d == '=' && at(i + 1) == '>') ||
                                (d == ':' && at(i + 1) == '=');
            if (symbol) {
                break;
            }
            ++i;
        }
        tokens.push_back(Token{TokenKind::Word, std::string(text.substr(begin, i - begin)), column});
    }
    tokens.push_back(Token{TokenKind::End, "", text.size() + 1});
    return tokens;
}

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    Rung rung() {
        Rung result;
        result.logic = series();
        expect(TokenKind::Arrow, "'=>' before the coils");
        do {
            result.coils.push_back(coil());
        } while (match(TokenKind::Comma));
        if (peek().kind != TokenKind::End) {
            fail("expected ',' or the end of the rung after a coil");
        }
        return result;
    }

private:
    // Elements until something that can't start one: '=>', '|', ')' or the end.
    Series series() {
        Series result;
        for (;;) {
            switch (peek().kind) {
            case TokenKind::Word:
                result.elements.emplace_back(contact());
                break;
            case TokenKind::LeftParen:
                result.elements.emplace_back(parallel());
                break;
            case TokenKind::LeftBrace:
                result.elements.emplace_back(box());
                break;
            default:
                return result;
            }
        }
    }

    Contact contact() {
        auto word = advance().text;
        Contact result;
        if (word.front() == '/') {
            result.kind = ContactKind::NormallyClosed;
            word.erase(0, 1);
        }
        if (word.empty()) {
            fail("expected an operand after '/'", -1);
        }
        result.operand = read(std::move(word));
        return result;
    }

    Parallel parallel() {
        advance(); // '('
        Parallel result;
        do {
            result.branches.push_back(series());
        } while (match(TokenKind::Bar));
        expect(TokenKind::RightParen, "')' to close the parallel branch");
        return result;
    }

    Box box() {
        advance(); // '{'
        Box result;
        result.type = expectWord("the box type, as in {TON t1}");
        result.instance = read(expectWord("the instance name after the box type, as in {TON t1}"));
        while (match(TokenKind::Comma)) {
            Pin pin;
            pin.name = expectWord("a pin name, as in PT := T#5s");
            if (match(TokenKind::Assign)) {
                pin.operand = expectWord(fmt::format("a value after '{} :='", pin.name));
                result.inputs.push_back(std::move(pin));
            } else if (match(TokenKind::Arrow)) {
                pin.operand = expectWord(fmt::format("a variable after '{} =>'", pin.name));
                result.outputs.push_back(std::move(pin));
            } else {
                fail(fmt::format("expected ':=' (an input) or '=>' (an output) after '{}'", pin.name));
            }
        }
        expect(TokenKind::RightBrace, "'}' to close the box");
        return result;
    }

    Coil coil() {
        auto word = expectWord("a coil after '=>'");
        Coil result;
        if (word.rfind("S:", 0) == 0 || word.rfind("R:", 0) == 0) {
            result.kind = word[0] == 'S' ? CoilKind::Set : CoilKind::Reset;
            word.erase(0, 2);
        } else if (word.front() == '/') {
            result.kind = CoilKind::Negated;
            word.erase(0, 1);
        }
        if (word.empty()) {
            fail("expected an operand for the coil", -1);
        }
        result.operand = read(std::move(word));
        return result;
    }

    const Token& peek() const { return tokens_[pos_]; }

    const Token& advance() {
        const Token& token = tokens_[pos_];
        if (token.kind != TokenKind::End) {
            ++pos_;
        }
        return token;
    }

    bool match(TokenKind kind) {
        if (peek().kind != kind) {
            return false;
        }
        advance();
        return true;
    }

    void expect(TokenKind kind, const std::string& what) {
        if (!match(kind)) {
            fail("expected " + what);
        }
    }

    std::string expectWord(const std::string& what) {
        if (peek().kind != TokenKind::Word) {
            fail("expected " + what);
        }
        return advance().text;
    }

    // `back` = -1 reports the token just read instead of the next one.
    [[noreturn]] void fail(const std::string& message, int back = 0) const {
        const auto& token = tokens_[back == 0 ? pos_ : pos_ - 1];
        const auto found = token.kind == TokenKind::End ? std::string("the end") : "'" + token.text + "'";
        throw NotationError{
            fmt::format("column {}: {}{}", token.column, message, back == 0 ? ", found " + found : std::string())};
    }

    std::vector<Token> tokens_;
    std::size_t pos_ = 0;
};

} // namespace

tl::expected<Rung, std::string> parseRung(std::string_view text) {
    try {
        return Parser(tokenize(text)).rung();
    } catch (const NotationError& error) {
        return tl::unexpected(error.message);
    }
}

} // namespace ladder
