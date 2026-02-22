/*
 * Teddy Parser
 * Recursive descent parser - turns tokens into AST.
 */

#ifndef TEDDY_PARSER_H
#define TEDDY_PARSER_H

#include "lexer.h"
#include "ast.h"

typedef struct {
    Lexer *lexer;
    Token current;
    Token previous;
    int had_error;
    int panic_mode;
} Parser;

void parser_init(Parser *parser, Lexer *lexer);
Program *parser_parse(Parser *parser);

#endif // TEDDY_PARSER_H
