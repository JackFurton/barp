/*
 * Teddy Lexer Implementation
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "lexer.h"

void lexer_init(Lexer *lexer, const char *source) {
    lexer->start = source;
    lexer->current = source;
    lexer->line = 1;
}

static int is_at_end(Lexer *lexer) {
    return *lexer->current == '\0';
}

static char advance(Lexer *lexer) {
    return *lexer->current++;
}

static char peek(Lexer *lexer) {
    return *lexer->current;
}

static char peek_next(Lexer *lexer) {
    if (is_at_end(lexer)) return '\0';
    return lexer->current[1];
}

static int match(Lexer *lexer, char expected) {
    if (is_at_end(lexer)) return 0;
    if (*lexer->current != expected) return 0;
    lexer->current++;
    return 1;
}

static Token make_token(Lexer *lexer, TokenType type) {
    Token token;
    token.type = type;
    token.start = lexer->start;
    token.length = (int)(lexer->current - lexer->start);
    token.line = lexer->line;
    return token;
}

static Token error_token(Lexer *lexer, const char *message) {
    Token token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = strlen(message);
    token.line = lexer->line;
    return token;
}

static void skip_whitespace(Lexer *lexer) {
    for (;;) {
        char c = peek(lexer);
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance(lexer);
                break;
            case '\n':
                lexer->line++;
                advance(lexer);
                break;
            case '/':
                if (peek_next(lexer) == '/') {
                    // Comment until end of line
                    while (peek(lexer) != '\n' && !is_at_end(lexer)) {
                        advance(lexer);
                    }
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

static Token number(Lexer *lexer) {
    while (isdigit(peek(lexer))) {
        advance(lexer);
    }
    return make_token(lexer, TOKEN_INT);
}

static TokenType check_keyword(Lexer *lexer, int start, int length,
                               const char *rest, TokenType type) {
    if (lexer->current - lexer->start == start + length &&
        memcmp(lexer->start + start, rest, length) == 0) {
        return type;
    }
    return TOKEN_IDENT;
}

static TokenType identifier_type(Lexer *lexer) {
    // Simple trie for keywords
    switch (lexer->start[0]) {
        case 'b': return check_keyword(lexer, 1, 4, "reak", TOKEN_BREAK);
        case 'c': return check_keyword(lexer, 1, 7, "ontinue", TOKEN_CONTINUE);
        case 'e':
            if (lexer->current - lexer->start > 1) {
                switch (lexer->start[1]) {
                    case 'l': return check_keyword(lexer, 2, 2, "se", TOKEN_ELSE);
                    case 'n': return check_keyword(lexer, 2, 2, "um", TOKEN_ENUM);
                }
            }
            break;
        case 'f':
            if (lexer->current - lexer->start > 1) {
                switch (lexer->start[1]) {
                    case 'n': return check_keyword(lexer, 2, 0, "", TOKEN_FN);
                    case 'a': return check_keyword(lexer, 2, 3, "lse", TOKEN_FALSE);
                    case 'o': return check_keyword(lexer, 2, 1, "r", TOKEN_FOR);
                }
            }
            break;
        case 'i':
            if (lexer->current - lexer->start > 1) {
                switch (lexer->start[1]) {
                    case 'f': return check_keyword(lexer, 2, 0, "", TOKEN_IF);
                    case 'n': return check_keyword(lexer, 2, 0, "", TOKEN_IN);
                    case 'm': return check_keyword(lexer, 2, 2, "pl", TOKEN_IMPL);
                }
            }
            break;
        case 'l': return check_keyword(lexer, 1, 2, "et", TOKEN_LET);
        case 'm': return check_keyword(lexer, 1, 4, "atch", TOKEN_MATCH);
        case 'p': return check_keyword(lexer, 1, 4, "rint", TOKEN_PRINT);
        case 'r': return check_keyword(lexer, 1, 5, "eturn", TOKEN_RETURN);
        case 's': return check_keyword(lexer, 1, 5, "truct", TOKEN_STRUCT);
        case 't': return check_keyword(lexer, 1, 3, "rue", TOKEN_TRUE);
        case 'w': return check_keyword(lexer, 1, 4, "hile", TOKEN_WHILE);
    }
    return TOKEN_IDENT;
}

static Token identifier(Lexer *lexer) {
    while (isalnum(peek(lexer)) || peek(lexer) == '_') {
        advance(lexer);
    }
    return make_token(lexer, identifier_type(lexer));
}

static Token string(Lexer *lexer) {
    while (peek(lexer) != '"' && !is_at_end(lexer)) {
        if (peek(lexer) == '\n') lexer->line++;
        if (peek(lexer) == '\\' && peek_next(lexer) != '\0') {
            advance(lexer);  // skip backslash
        }
        advance(lexer);
    }
    
    if (is_at_end(lexer)) {
        return error_token(lexer, "Unterminated string.");
    }
    
    advance(lexer);  // closing quote
    return make_token(lexer, TOKEN_STRING);
}

Token lexer_next_token(Lexer *lexer) {
    skip_whitespace(lexer);
    lexer->start = lexer->current;

    if (is_at_end(lexer)) {
        return make_token(lexer, TOKEN_EOF);
    }

    char c = advance(lexer);

    // Identifiers and keywords
    if (isalpha(c) || c == '_') {
        return identifier(lexer);
    }

    // Numbers
    if (isdigit(c)) {
        return number(lexer);
    }

    // Strings
    if (c == '"') {
        return string(lexer);
    }

    // Single and multi-character tokens
    switch (c) {
        case '(': return make_token(lexer, TOKEN_LPAREN);
        case ')': return make_token(lexer, TOKEN_RPAREN);
        case '{': return make_token(lexer, TOKEN_LBRACE);
        case '}': return make_token(lexer, TOKEN_RBRACE);
        case '[': return make_token(lexer, TOKEN_LBRACKET);
        case ']': return make_token(lexer, TOKEN_RBRACKET);
        case ';': return make_token(lexer, TOKEN_SEMICOLON);
        case ',': return make_token(lexer, TOKEN_COMMA);
        case '.':
            if (peek(lexer) == '.') {
                advance(lexer);
                return make_token(lexer, TOKEN_DOT_DOT);
            }
            return make_token(lexer, TOKEN_DOT);
        case ':': 
            return make_token(lexer, match(lexer, ':') ? TOKEN_COLON_COLON : TOKEN_COLON);
        case '+': return make_token(lexer, TOKEN_PLUS);
        case '-': return make_token(lexer, TOKEN_MINUS);
        case '*': return make_token(lexer, TOKEN_STAR);
        case '/': return make_token(lexer, TOKEN_SLASH);
        case '%': return make_token(lexer, TOKEN_PERCENT);

        case '=':
            if (match(lexer, '>')) return make_token(lexer, TOKEN_FAT_ARROW);
            return make_token(lexer, match(lexer, '=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
        case '!':
            return make_token(lexer, match(lexer, '=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
        case '<':
            return make_token(lexer, match(lexer, '=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
        case '>':
            return make_token(lexer, match(lexer, '=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
        case '&':
            if (match(lexer, '&')) return make_token(lexer, TOKEN_AND);
            break;
        case '|':
            if (match(lexer, '|')) return make_token(lexer, TOKEN_OR);
            break;
    }

    return error_token(lexer, "Unexpected character");
}

const char *token_type_name(TokenType type) {
    switch (type) {
        case TOKEN_LPAREN:       return "LPAREN";
        case TOKEN_RPAREN:       return "RPAREN";
        case TOKEN_LBRACE:       return "LBRACE";
        case TOKEN_RBRACE:       return "RBRACE";
        case TOKEN_LBRACKET:     return "LBRACKET";
        case TOKEN_RBRACKET:     return "RBRACKET";
        case TOKEN_SEMICOLON:    return "SEMICOLON";
        case TOKEN_COMMA:        return "COMMA";
        case TOKEN_DOT:          return "DOT";
        case TOKEN_COLON:        return "COLON";
        case TOKEN_PLUS:         return "PLUS";
        case TOKEN_MINUS:        return "MINUS";
        case TOKEN_STAR:         return "STAR";
        case TOKEN_SLASH:        return "SLASH";
        case TOKEN_PERCENT:      return "PERCENT";
        case TOKEN_EQUAL:        return "EQUAL";
        case TOKEN_EQUAL_EQUAL:  return "EQUAL_EQUAL";
        case TOKEN_BANG:         return "BANG";
        case TOKEN_BANG_EQUAL:   return "BANG_EQUAL";
        case TOKEN_LESS:         return "LESS";
        case TOKEN_LESS_EQUAL:   return "LESS_EQUAL";
        case TOKEN_GREATER:      return "GREATER";
        case TOKEN_GREATER_EQUAL:return "GREATER_EQUAL";
        case TOKEN_AND:          return "AND";
        case TOKEN_OR:           return "OR";
        case TOKEN_INT:          return "INT";
        case TOKEN_STRING:       return "STRING";
        case TOKEN_IDENT:        return "IDENT";
        case TOKEN_FN:           return "FN";
        case TOKEN_LET:          return "LET";
        case TOKEN_STRUCT:       return "STRUCT";
        case TOKEN_ENUM:         return "ENUM";
        case TOKEN_MATCH:        return "MATCH";
        case TOKEN_IF:           return "IF";
        case TOKEN_ELSE:         return "ELSE";
        case TOKEN_WHILE:        return "WHILE";
        case TOKEN_RETURN:       return "RETURN";
        case TOKEN_PRINT:        return "PRINT";
        case TOKEN_TRUE:         return "TRUE";
        case TOKEN_FALSE:        return "FALSE";
        case TOKEN_BREAK:        return "BREAK";
        case TOKEN_CONTINUE:     return "CONTINUE";
        case TOKEN_FOR:          return "FOR";
        case TOKEN_IN:           return "IN";
        case TOKEN_IMPL:         return "IMPL";
        case TOKEN_DOT_DOT:     return "DOT_DOT";
        case TOKEN_COLON_COLON:  return "COLON_COLON";
        case TOKEN_FAT_ARROW:    return "FAT_ARROW";
        case TOKEN_EOF:          return "EOF";
        case TOKEN_ERROR:        return "ERROR";
        default:                 return "UNKNOWN";
    }
}

void token_print(const Token *token) {
    printf("%4d | %-15s '", token->line, token_type_name(token->type));
    for (int i = 0; i < token->length; i++) {
        putchar(token->start[i]);
    }
    printf("'\n");
}
