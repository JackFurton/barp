/*
 * Teddy Lexer
 * Converts source text into tokens.
 */

#ifndef TEDDY_LEXER_H
#define TEDDY_LEXER_H

#include <stddef.h>

typedef enum {
    // Single character tokens
    TOKEN_LPAREN,       // (
    TOKEN_RPAREN,       // )
    TOKEN_LBRACE,       // {
    TOKEN_RBRACE,       // }
    TOKEN_LBRACKET,     // [
    TOKEN_RBRACKET,     // ]
    TOKEN_SEMICOLON,    // ;
    TOKEN_COMMA,        // ,
    TOKEN_DOT,          // .
    TOKEN_COLON,        // :
    TOKEN_PLUS,         // +
    TOKEN_MINUS,        // -
    TOKEN_STAR,         // *
    TOKEN_SLASH,        // /
    TOKEN_PERCENT,      // %

    // One or two character tokens
    TOKEN_EQUAL,        // =
    TOKEN_EQUAL_EQUAL,  // ==
    TOKEN_BANG,         // !
    TOKEN_BANG_EQUAL,   // !=
    TOKEN_LESS,         // <
    TOKEN_LESS_EQUAL,   // <=
    TOKEN_GREATER,      // >
    TOKEN_GREATER_EQUAL,// >=
    TOKEN_AND,          // &&
    TOKEN_OR,           // ||

    // Literals
    TOKEN_INT,          // 123
    TOKEN_STRING,       // "hello"
    TOKEN_IDENT,        // foo

    // Keywords
    TOKEN_FN,           // fn
    TOKEN_LET,          // let
    TOKEN_STRUCT,       // struct
    TOKEN_ENUM,         // enum
    TOKEN_MATCH,        // match
    TOKEN_IF,           // if
    TOKEN_ELSE,         // else
    TOKEN_WHILE,        // while
    TOKEN_RETURN,       // return
    TOKEN_PRINT,        // print
    TOKEN_TRUE,         // true
    TOKEN_FALSE,        // false

    // Two-character tokens (new)
    TOKEN_COLON_COLON,  // ::
    TOKEN_FAT_ARROW,    // =>

    // Special
    TOKEN_EOF,
    TOKEN_ERROR,
} TokenType;

typedef struct {
    TokenType type;
    const char *start;  // Pointer into source
    int length;
    int line;
} Token;

typedef struct {
    const char *start;   // Start of current token
    const char *current; // Current position
    int line;
} Lexer;

void lexer_init(Lexer *lexer, const char *source);
Token lexer_next_token(Lexer *lexer);
void token_print(const Token *token);
const char *token_type_name(TokenType type);

#endif // TEDDY_LEXER_H
