#include "ConditionExpressionParser.hpp"
#include "CompositeConditions.hpp"
#include "PortCondition.hpp"
#include "ProtocolCondition.hpp"
#include "TextCondition.hpp"
#include "log/Log.hpp"
#include <cctype>
#include <charconv>
#include <string>
#include <vector>

namespace Proxy::Handler {

namespace {

    enum class TokenType {
        Identifier,
        StringLiteral,
        NumberLiteral,
        And,
        Or,
        Not,
        EqualEqual,
        LParen,
        RParen,
        Comma,
        EndOfInput,
        Invalid
    };

    struct Token {
        TokenType type { TokenType::Invalid };
        std::string text;
    };

    class Lexer {
    public:
        explicit Lexer(std::string_view input)
            : m_input(input)
        {
        }

        Token nextToken()
        {
            skipWhitespace();
            if (m_pos >= m_input.size()) {
                return { TokenType::EndOfInput, "" };
            }

            char c = m_input[m_pos];

            if (c == '(') {
                ++m_pos;
                return { TokenType::LParen, "(" };
            }
            if (c == ')') {
                ++m_pos;
                return { TokenType::RParen, ")" };
            }
            if (c == ',') {
                ++m_pos;
                return { TokenType::Comma, "," };
            }
            if (c == '!') {
                ++m_pos;
                return { TokenType::Not, "!" };
            }
            if (c == '&' && m_pos + 1 < m_input.size() && m_input[m_pos + 1] == '&') {
                m_pos += 2;
                return { TokenType::And, "&&" };
            }
            if (c == '|' && m_pos + 1 < m_input.size() && m_input[m_pos + 1] == '|') {
                m_pos += 2;
                return { TokenType::Or, "||" };
            }
            if (c == '=' && m_pos + 1 < m_input.size() && m_input[m_pos + 1] == '=') {
                m_pos += 2;
                return { TokenType::EqualEqual, "==" };
            }

            // String literal: '...' or "..."
            if (c == '"' || c == '\'') {
                return lexString(c);
            }

            // Number literal
            if (std::isdigit(static_cast<unsigned char>(c))) {
                return lexNumber();
            }

            // Identifier or keyword
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                return lexIdentifier();
            }

            ++m_pos;
            return { TokenType::Invalid, std::string(1, c) };
        }

    private:
        void skipWhitespace()
        {
            while (m_pos < m_input.size() && std::isspace(static_cast<unsigned char>(m_input[m_pos]))) {
                ++m_pos;
            }
        }

        Token lexString(char quote)
        {
            ++m_pos; // skip opening quote
            std::string result;
            while (m_pos < m_input.size()) {
                char ch = m_input[m_pos++];
                if (ch == quote) {
                    return { TokenType::StringLiteral, std::move(result) };
                }
                if (ch == '\\' && m_pos < m_input.size()) {
                    char esc = m_input[m_pos++];
                    if (esc == 'n')
                        result += '\n';
                    else if (esc == 'r')
                        result += '\r';
                    else if (esc == 't')
                        result += '\t';
                    else if (esc == '\\')
                        result += '\\';
                    else if (esc == quote)
                        result += quote;
                    else {
                        result += '\\';
                        result += esc;
                    }
                } else {
                    result += ch;
                }
            }
            return { TokenType::Invalid, "Unterminated string" };
        }

        Token lexNumber()
        {
            std::size_t start = m_pos;
            while (m_pos < m_input.size() && std::isdigit(static_cast<unsigned char>(m_input[m_pos]))) {
                ++m_pos;
            }
            return { TokenType::NumberLiteral, std::string(m_input.substr(start, m_pos - start)) };
        }

        Token lexIdentifier()
        {
            std::size_t start = m_pos;
            while (m_pos < m_input.size() && (std::isalnum(static_cast<unsigned char>(m_input[m_pos])) || m_input[m_pos] == '_')) {
                ++m_pos;
            }
            std::string_view word = m_input.substr(start, m_pos - start);
            if (word == "and")
                return { TokenType::And, "and" };
            if (word == "or")
                return { TokenType::Or, "or" };
            if (word == "not")
                return { TokenType::Not, "not" };

            return { TokenType::Identifier, std::string(word) };
        }

        std::string_view m_input;
        std::size_t m_pos { 0 };
    };

    class Parser {
    public:
        explicit Parser(std::string_view input)
            : m_lexer(input)
        {
            advance();
        }

        std::unique_ptr<IRouteCondition> parse()
        {
            auto expr = parseOr();
            if (m_current.type != TokenType::EndOfInput) {
                Proxy::Log::error("[ConditionExpressionParser] Unexpected token '{}' at end of expression", m_current.text);
                return nullptr;
            }
            return expr;
        }

    private:
        void advance()
        {
            m_current = m_lexer.nextToken();
        }

        bool match(TokenType type)
        {
            if (m_current.type == type) {
                advance();
                return true;
            }
            return false;
        }

        // Logical OR: expr || expr or expr or expr
        std::unique_ptr<IRouteCondition> parseOr()
        {
            auto left = parseAnd();
            if (!left)
                return nullptr;

            while (m_current.type == TokenType::Or) {
                advance();
                auto right = parseAnd();
                if (!right)
                    return nullptr;

                auto orCond = std::make_unique<OrCondition>();
                orCond->add(std::move(left));
                orCond->add(std::move(right));
                left = std::move(orCond);
            }

            return left;
        }

        // Logical AND: expr && expr or expr and expr
        std::unique_ptr<IRouteCondition> parseAnd()
        {
            auto left = parseUnary();
            if (!left)
                return nullptr;

            while (m_current.type == TokenType::And) {
                advance();
                auto right = parseUnary();
                if (!right)
                    return nullptr;

                auto andCond = std::make_unique<AndCondition>();
                andCond->add(std::move(left));
                andCond->add(std::move(right));
                left = std::move(andCond);
            }

            return left;
        }

        // Logical NOT: not expr or !expr
        std::unique_ptr<IRouteCondition> parseUnary()
        {
            if (m_current.type == TokenType::Not) {
                advance();
                auto inner = parseUnary();
                if (!inner)
                    return nullptr;
                return std::make_unique<NotCondition>(std::move(inner));
            }
            return parsePrimary();
        }

        // Primary: ( expr ) or leaf functions
        std::unique_ptr<IRouteCondition> parsePrimary()
        {
            if (m_current.type == TokenType::LParen) {
                advance();
                auto inner = parseOr();
                if (!inner || !match(TokenType::RParen)) {
                    Proxy::Log::error("[ConditionExpressionParser] Expected closing ')'");
                    return nullptr;
                }
                return inner;
            }

            if (m_current.type == TokenType::Identifier) {
                std::string id = std::move(m_current.text);
                advance();
                return parseFunctionOrComparison(id);
            }

            Proxy::Log::error("[ConditionExpressionParser] Unexpected token in primary expression: '{}'", m_current.text);
            return nullptr;
        }

        std::unique_ptr<IRouteCondition> parseFunctionOrComparison(const std::string& name)
        {
            // port == 8080 or port(8080)
            if (name == "port") {
                if (match(TokenType::EqualEqual)) {
                    if (m_current.type != TokenType::NumberLiteral) {
                        Proxy::Log::error("[ConditionExpressionParser] Expected port number after 'port =='");
                        return nullptr;
                    }
                    int p = std::stoi(m_current.text);
                    advance();
                    return std::make_unique<PortCondition>(static_cast<std::uint16_t>(p));
                }
                if (match(TokenType::LParen)) {
                    if (m_current.type != TokenType::NumberLiteral) {
                        return nullptr;
                    }
                    int p = std::stoi(m_current.text);
                    advance();
                    if (!match(TokenType::RParen))
                        return nullptr;
                    return std::make_unique<PortCondition>(static_cast<std::uint16_t>(p));
                }
            }

            // protocol == 1 or protocol(1)
            if (name == "protocol") {
                if (match(TokenType::EqualEqual)) {
                    if (m_current.type != TokenType::NumberLiteral)
                        return nullptr;
                    int p = std::stoi(m_current.text);
                    advance();
                    return std::make_unique<ProtocolCondition>(Backend::ProtocolId { static_cast<std::uint8_t>(p) });
                }
                if (match(TokenType::LParen)) {
                    if (m_current.type != TokenType::NumberLiteral)
                        return nullptr;
                    int p = std::stoi(m_current.text);
                    advance();
                    if (!match(TokenType::RParen))
                        return nullptr;
                    return std::make_unique<ProtocolCondition>(Backend::ProtocolId { static_cast<std::uint8_t>(p) });
                }
            }

            // prefix("pattern")
            if (name == "prefix") {
                if (!match(TokenType::LParen))
                    return nullptr;
                if (m_current.type != TokenType::StringLiteral)
                    return nullptr;
                std::string str = std::move(m_current.text);
                advance();
                if (!match(TokenType::RParen))
                    return nullptr;
                return std::make_unique<TextCondition>(std::move(str), TextMatchMode::Prefix, 1);
            }

            // exact("pattern")
            if (name == "exact") {
                if (!match(TokenType::LParen))
                    return nullptr;
                if (m_current.type != TokenType::StringLiteral)
                    return nullptr;
                std::string str = std::move(m_current.text);
                advance();
                if (!match(TokenType::RParen))
                    return nullptr;
                return std::make_unique<TextCondition>(std::move(str), TextMatchMode::Exact, 1);
            }

            // contains("pattern" [, times])
            if (name == "contains") {
                if (!match(TokenType::LParen))
                    return nullptr;
                if (m_current.type != TokenType::StringLiteral)
                    return nullptr;
                std::string str = std::move(m_current.text);
                advance();
                std::size_t times = 1;
                if (match(TokenType::Comma)) {
                    if (m_current.type != TokenType::NumberLiteral)
                        return nullptr;
                    times = static_cast<std::size_t>(std::stoul(m_current.text));
                    advance();
                }
                if (!match(TokenType::RParen))
                    return nullptr;
                return std::make_unique<TextCondition>(std::move(str), TextMatchMode::Contains, times);
            }

            // fallback or default
            if (name == "fallback" || name == "default") {
                return std::make_unique<TextCondition>("", TextMatchMode::Contains, 0);
            }

            Proxy::Log::error("[ConditionExpressionParser] Unknown condition function/keyword: '{}'", name);
            return nullptr;
        }

        Lexer m_lexer;
        Token m_current;
    };

} // namespace

std::unique_ptr<IRouteCondition> ConditionExpressionParser::parse(std::string_view expr)
{
    Parser parser(expr);
    return parser.parse();
}

} // namespace Proxy::Handler
