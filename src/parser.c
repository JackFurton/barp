/*
 * Teddy Parser Implementation
 * 
 * Grammar:
 *   program     → function* EOF
 *   function    → "fn" IDENT "(" params? ")" block
 *   params      → IDENT ("," IDENT)*
 *   block       → "{" statement* "}"
 *   statement   → letStmt | printStmt | ifStmt | whileStmt | returnStmt | exprStmt
 *   letStmt     → "let" IDENT "=" expression ";"
 *   printStmt   → "print" expression ";"
 *   ifStmt      → "if" expression block ("else" block)?
 *   whileStmt   → "while" expression block
 *   returnStmt  → "return" expression? ";"
 *   exprStmt    → expression ";"
 *   
 *   expression  → or
 *   or          → and ("||" and)*
 *   and         → equality ("&&" equality)*
 *   equality    → comparison (("==" | "!=") comparison)*
 *   comparison  → term (("<" | "<=" | ">" | ">=") term)*
 *   term        → factor (("+" | "-") factor)*
 *   factor      → unary (("*" | "/" | "%") unary)*
 *   unary       → ("!" | "-") unary | call
 *   call        → primary ("(" arguments? ")")?
 *   arguments   → expression ("," expression)*
 *   primary     → INT | "true" | "false" | IDENT | "(" expression ")"
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"

// ============ HELPERS ============

static void error_at(Parser *parser, Token *token, const char *message) {
    if (parser->panic_mode) return;
    parser->panic_mode = 1;
    parser->had_error = 1;

    fprintf(stderr, "[line %d] Error", token->line);

    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type != TOKEN_ERROR) {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
}

static void error(Parser *parser, const char *message) {
    error_at(parser, &parser->previous, message);
}

static void error_current(Parser *parser, const char *message) {
    error_at(parser, &parser->current, message);
}

static void advance(Parser *parser) {
    parser->previous = parser->current;

    for (;;) {
        parser->current = lexer_next_token(parser->lexer);
        if (parser->current.type != TOKEN_ERROR) break;
        error_current(parser, parser->current.start);
    }
}

static int check(Parser *parser, TokenType type) {
    return parser->current.type == type;
}

static int match(Parser *parser, TokenType type) {
    if (!check(parser, type)) return 0;
    advance(parser);
    return 1;
}

static void consume(Parser *parser, TokenType type, const char *message) {
    if (parser->current.type == type) {
        advance(parser);
        return;
    }
    error_current(parser, message);
}

static char *copy_token_string(Token *token) {
    char *str = malloc(token->length + 1);
    memcpy(str, token->start, token->length);
    str[token->length] = '\0';
    return str;
}

// ============ FORWARD DECLARATIONS ============

static Expr *expression(Parser *parser);
static Stmt *statement(Parser *parser);
static Stmt *block(Parser *parser);

// ============ EXPRESSION PARSING ============

static Expr *primary(Parser *parser) {
    int line = parser->current.line;

    if (match(parser, TOKEN_INT)) {
        char *str = copy_token_string(&parser->previous);
        long value = strtol(str, NULL, 10);
        free(str);
        return expr_int_literal(value, line);
    }

    if (match(parser, TOKEN_TRUE)) {
        return expr_bool_literal(1, line);
    }

    if (match(parser, TOKEN_FALSE)) {
        return expr_bool_literal(0, line);
    }

    if (match(parser, TOKEN_STRING)) {
        // Token includes the quotes, so skip them
        const char *start = parser->previous.start + 1;  // skip opening "
        int length = parser->previous.length - 2;         // skip both quotes
        
        // Check if string contains interpolation markers {..}
        int has_interp = 0;
        for (int i = 0; i < length; i++) {
            if (start[i] == '\\') { i++; continue; }
            if (start[i] == '{') { has_interp = 1; break; }
        }
        
        if (has_interp) {
            // Parse as string interpolation
            // Split into parts: string fragments and variable/expression references
            Expr **parts = NULL;
            int part_count = 0;
            int part_cap = 0;
            
            int seg_start = 0;  // start of current string segment
            int i = 0;
            while (i < length) {
                if (start[i] == '\\' && i + 1 < length) {
                    i += 2;  // skip escape sequence
                    continue;
                }
                if (start[i] == '{') {
                    // Emit string fragment before this {
                    if (i > seg_start) {
                        // Process escape sequences in fragment
                        char *frag = malloc(i - seg_start + 1);
                        int fj = 0;
                        for (int k = seg_start; k < i; k++) {
                            if (start[k] == '\\' && k + 1 < i) {
                                k++;
                                switch (start[k]) {
                                    case 'n': frag[fj++] = '\n'; break;
                                    case 't': frag[fj++] = '\t'; break;
                                    case 'r': frag[fj++] = '\r'; break;
                                    case '0': frag[fj++] = '\0'; break;
                                    case '\\': frag[fj++] = '\\'; break;
                                    case '"': frag[fj++] = '"'; break;
                                    case '{': frag[fj++] = '{'; break;
                                    default: frag[fj++] = start[k]; break;
                                }
                            } else {
                                frag[fj++] = start[k];
                            }
                        }
                        frag[fj] = '\0';
                        Expr *str_part = expr_string_literal(frag, fj, line);
                        free(frag);
                        if (part_count >= part_cap) {
                            part_cap = part_cap == 0 ? 8 : part_cap * 2;
                            parts = realloc(parts, sizeof(Expr*) * part_cap);
                        }
                        parts[part_count++] = str_part;
                    }
                    
                    // Find matching }
                    i++;  // skip {
                    int expr_start = i;
                    while (i < length && start[i] != '}') i++;
                    if (i >= length) {
                        fprintf(stderr, "[line %d] Error: Unterminated { in string interpolation\n", line);
                        exit(1);
                    }
                    
                    // Extract the expression text (variable name)
                    int expr_len = i - expr_start;
                    char *expr_text = malloc(expr_len + 1);
                    memcpy(expr_text, start + expr_start, expr_len);
                    expr_text[expr_len] = '\0';
                    
                    // Create a variable reference expression
                    Expr *var_part = expr_variable(expr_text, line);
                    // expr_text ownership transferred to expr_variable (it calls strdup)
                    free(expr_text);
                    
                    if (part_count >= part_cap) {
                        part_cap = part_cap == 0 ? 8 : part_cap * 2;
                        parts = realloc(parts, sizeof(Expr*) * part_cap);
                    }
                    parts[part_count++] = var_part;
                    
                    i++;  // skip }
                    seg_start = i;
                } else {
                    i++;
                }
            }
            
            // Emit trailing string fragment
            if (i > seg_start) {
                char *frag = malloc(i - seg_start + 1);
                int fj = 0;
                for (int k = seg_start; k < i; k++) {
                    if (start[k] == '\\' && k + 1 < i) {
                        k++;
                        switch (start[k]) {
                            case 'n': frag[fj++] = '\n'; break;
                            case 't': frag[fj++] = '\t'; break;
                            case 'r': frag[fj++] = '\r'; break;
                            case '0': frag[fj++] = '\0'; break;
                            case '\\': frag[fj++] = '\\'; break;
                            case '"': frag[fj++] = '"'; break;
                            case '{': frag[fj++] = '{'; break;
                            default: frag[fj++] = start[k]; break;
                        }
                    } else {
                        frag[fj++] = start[k];
                    }
                }
                frag[fj] = '\0';
                Expr *str_part = expr_string_literal(frag, fj, line);
                free(frag);
                if (part_count >= part_cap) {
                    part_cap = part_cap == 0 ? 8 : part_cap * 2;
                    parts = realloc(parts, sizeof(Expr*) * part_cap);
                }
                parts[part_count++] = str_part;
            }
            
            return expr_string_interp(parts, part_count, line);
        }
        
        // Regular string literal (no interpolation)
        // Process escape sequences
        char *value = malloc(length + 1);
        int j = 0;
        for (int i = 0; i < length; i++) {
            if (start[i] == '\\' && i + 1 < length) {
                i++;
                switch (start[i]) {
                    case 'n': value[j++] = '\n'; break;
                    case 't': value[j++] = '\t'; break;
                    case 'r': value[j++] = '\r'; break;
                    case '0': value[j++] = '\0'; break;
                    case '\\': value[j++] = '\\'; break;
                    case '"': value[j++] = '"'; break;
                    default: value[j++] = start[i]; break;
                }
            } else {
                value[j++] = start[i];
            }
        }
        value[j] = '\0';
        
        Expr *e = expr_string_literal(value, j, line);
        free(value);
        return e;
    }

    if (match(parser, TOKEN_IDENT)) {
        char *name = copy_token_string(&parser->previous);
        
        // Check for enum variant: EnumName::Variant or EnumName::Variant(payload)
        char first = name[0];
        int is_uppercase = (first >= 'A' && first <= 'Z');
        if (is_uppercase && check(parser, TOKEN_COLON_COLON)) {
            advance(parser);  // consume '::'
            consume(parser, TOKEN_IDENT, "Expected variant name after '::'.");
            char *variant = copy_token_string(&parser->previous);
            
            // Check for payload: Some(42)
            Expr *payload = NULL;
            if (match(parser, TOKEN_LPAREN)) {
                payload = expression(parser);
                consume(parser, TOKEN_RPAREN, "Expected ')' after variant payload.");
            }
            
            Expr *result = expr_enum_variant(name, variant, payload, line);
            free(name);
            free(variant);
            return result;
        }
        
        // Check for struct literal: Name { field: value, ... }
        // Only treat as struct literal if identifier starts with uppercase (A-Z)
        if (is_uppercase && check(parser, TOKEN_LBRACE)) {
            advance(parser);  // consume '{'
            
            char **field_names = NULL;
            Expr **field_values = NULL;
            int field_count = 0;
            int capacity = 0;
            
            if (!check(parser, TOKEN_RBRACE)) {
                do {
                    consume(parser, TOKEN_IDENT, "Expected field name.");
                    char *field_name = copy_token_string(&parser->previous);
                    consume(parser, TOKEN_COLON, "Expected ':' after field name.");
                    Expr *field_value = expression(parser);
                    
                    if (field_count >= capacity) {
                        capacity = capacity == 0 ? 4 : capacity * 2;
                        field_names = realloc(field_names, sizeof(char*) * capacity);
                        field_values = realloc(field_values, sizeof(Expr*) * capacity);
                    }
                    field_names[field_count] = field_name;
                    field_values[field_count] = field_value;
                    field_count++;
                } while (match(parser, TOKEN_COMMA) && !check(parser, TOKEN_RBRACE));
            }
            
            consume(parser, TOKEN_RBRACE, "Expected '}' after struct fields.");
            Expr *result = expr_struct_literal(name, field_names, field_values, field_count, line);
            free(name);
            return result;
        }
        
        return expr_variable(name, line);
    }

    if (match(parser, TOKEN_LPAREN)) {
        Expr *expr = expression(parser);
        consume(parser, TOKEN_RPAREN, "Expected ')' after expression.");
        return expr;
    }

    // Match expression: match expr { Pattern => body, ... }
    if (match(parser, TOKEN_MATCH)) {
        Expr *value = expression(parser);
        consume(parser, TOKEN_LBRACE, "Expected '{' after match value.");
        
        MatchArm **arms = NULL;
        int arm_count = 0;
        int capacity = 0;
        
        while (!check(parser, TOKEN_RBRACE) && !check(parser, TOKEN_EOF)) {
            // Parse pattern: Variant, Variant(binding), EnumName::Variant, EnumName::Variant(binding)
            consume(parser, TOKEN_IDENT, "Expected pattern in match arm.");
            char *first_ident = copy_token_string(&parser->previous);
            char *enum_name = NULL;
            char *variant_name = NULL;
            char *binding_name = NULL;
            
            if (check(parser, TOKEN_COLON_COLON)) {
                advance(parser);  // consume '::'
                consume(parser, TOKEN_IDENT, "Expected variant name after '::'.");
                enum_name = first_ident;
                variant_name = copy_token_string(&parser->previous);
            } else {
                // Just variant name (enum type inferred from match value)
                variant_name = first_ident;
            }
            
            // Check for binding: Some(x) or None
            if (match(parser, TOKEN_LPAREN)) {
                consume(parser, TOKEN_IDENT, "Expected binding name in pattern.");
                binding_name = copy_token_string(&parser->previous);
                consume(parser, TOKEN_RPAREN, "Expected ')' after binding name.");
            }
            
            consume(parser, TOKEN_FAT_ARROW, "Expected '=>' after pattern.");
            
            Expr *body = expression(parser);
            
            MatchArm *arm = match_arm_new(enum_name, variant_name, binding_name, body);
            if (enum_name) free(enum_name);
            free(variant_name);
            if (binding_name) free(binding_name);
            
            if (arm_count >= capacity) {
                capacity = capacity == 0 ? 4 : capacity * 2;
                arms = realloc(arms, sizeof(MatchArm*) * capacity);
            }
            arms[arm_count++] = arm;
            
            // Optional comma between arms
            match(parser, TOKEN_COMMA);
        }
        
        consume(parser, TOKEN_RBRACE, "Expected '}' after match arms.");
        return expr_match(value, arms, arm_count, line);
    }

    // Array literal: [1, 2, 3]
    if (match(parser, TOKEN_LBRACKET)) {
        Expr **elements = NULL;
        int count = 0;
        int capacity = 0;
        
        if (!check(parser, TOKEN_RBRACKET)) {
            do {
                if (count >= capacity) {
                    capacity = capacity == 0 ? 4 : capacity * 2;
                    elements = realloc(elements, sizeof(Expr*) * capacity);
                }
                elements[count++] = expression(parser);
            } while (match(parser, TOKEN_COMMA));
        }
        
        consume(parser, TOKEN_RBRACKET, "Expected ']' after array elements.");
        return expr_array_literal(elements, count, line);
    }

    // Closure: |params| expr  or  |params| { block }
    if (match(parser, TOKEN_PIPE)) {
        char **params = NULL;
        int param_count = 0;
        int capacity = 0;
        
        // Parse parameters (can be empty: || expr)
        if (!check(parser, TOKEN_PIPE)) {
            do {
                consume(parser, TOKEN_IDENT, "Expected parameter name in closure.");
                if (param_count >= capacity) {
                    capacity = capacity == 0 ? 4 : capacity * 2;
                    params = realloc(params, sizeof(char*) * capacity);
                }
                params[param_count++] = copy_token_string(&parser->previous);
            } while (match(parser, TOKEN_COMMA));
        }
        consume(parser, TOKEN_PIPE, "Expected '|' after closure parameters.");
        
        // Body: either a block or a single expression
        if (check(parser, TOKEN_LBRACE)) {
            Stmt *body_block = block(parser);
            return expr_closure(params, param_count, NULL, body_block, line);
        } else {
            Expr *body_expr = expression(parser);
            return expr_closure(params, param_count, body_expr, NULL, line);
        }
    }

    error_current(parser, "Expected expression.");
    return expr_int_literal(0, line);  // Dummy to continue
}

static Expr *call(Parser *parser) {
    Expr *expr = primary(parser);

    // Handle postfix operators: function calls, array indexing, and field access
    for (;;) {
        if (expr->type == EXPR_VARIABLE && match(parser, TOKEN_LPAREN)) {
            char *name = expr->as.variable.name;
            int line = expr->line;
            
            // Parse arguments
            Expr **args = NULL;
            int arg_count = 0;
            int capacity = 0;

            if (!check(parser, TOKEN_RPAREN)) {
                do {
                    if (arg_count >= capacity) {
                        capacity = capacity == 0 ? 4 : capacity * 2;
                        args = realloc(args, sizeof(Expr*) * capacity);
                    }
                    args[arg_count++] = expression(parser);
                } while (match(parser, TOKEN_COMMA));
            }

            consume(parser, TOKEN_RPAREN, "Expected ')' after arguments.");
            
            // Convert variable expr to call expr
            char *name_copy = malloc(strlen(name) + 1);
            strcpy(name_copy, name);
            expr_free(expr);
            
            expr = expr_call(name_copy, args, arg_count, line);
        } else if (match(parser, TOKEN_LBRACKET)) {
            // Array indexing: expr[index]
            int line = parser->previous.line;
            Expr *index = expression(parser);
            consume(parser, TOKEN_RBRACKET, "Expected ']' after index.");
            expr = expr_index(expr, index, line);
        } else if (match(parser, TOKEN_DOT)) {
            // Field access or method call: expr.field or expr.method(args)
            int line = parser->previous.line;
            consume(parser, TOKEN_IDENT, "Expected field name after '.'.");
            char *field_name = copy_token_string(&parser->previous);
            
            if (match(parser, TOKEN_LPAREN)) {
                // Method call: expr.method(args)
                Expr **args = NULL;
                int arg_count = 0;
                int capacity = 0;
                
                if (!check(parser, TOKEN_RPAREN)) {
                    do {
                        if (arg_count >= capacity) {
                            capacity = capacity == 0 ? 4 : capacity * 2;
                            args = realloc(args, sizeof(Expr*) * capacity);
                        }
                        args[arg_count++] = expression(parser);
                    } while (match(parser, TOKEN_COMMA));
                }
                consume(parser, TOKEN_RPAREN, "Expected ')' after method arguments.");
                
                expr = expr_method_call(expr, field_name, args, arg_count, line);
            } else {
                // Plain field access
                expr = expr_field_access(expr, field_name, line);
            }
            free(field_name);
        } else {
            break;
        }
    }

    return expr;
}

static Expr *unary(Parser *parser) {
    if (match(parser, TOKEN_BANG)) {
        int line = parser->previous.line;
        Expr *operand = unary(parser);
        return expr_unary(OP_NOT, operand, line);
    }

    if (match(parser, TOKEN_MINUS)) {
        int line = parser->previous.line;
        Expr *operand = unary(parser);
        return expr_unary(OP_NEG, operand, line);
    }

    return call(parser);
}

static Expr *factor(Parser *parser) {
    Expr *left = unary(parser);

    while (match(parser, TOKEN_STAR) || match(parser, TOKEN_SLASH) || 
           match(parser, TOKEN_PERCENT)) {
        int line = parser->previous.line;
        OpType op;
        switch (parser->previous.type) {
            case TOKEN_STAR:    op = OP_MUL; break;
            case TOKEN_SLASH:   op = OP_DIV; break;
            case TOKEN_PERCENT: op = OP_MOD; break;
            default:            op = OP_MUL; break;  // Unreachable
        }
        Expr *right = unary(parser);
        left = expr_binary(op, left, right, line);
    }

    return left;
}

static Expr *term(Parser *parser) {
    Expr *left = factor(parser);

    while (match(parser, TOKEN_PLUS) || match(parser, TOKEN_MINUS)) {
        int line = parser->previous.line;
        OpType op = parser->previous.type == TOKEN_PLUS ? OP_ADD : OP_SUB;
        Expr *right = factor(parser);
        left = expr_binary(op, left, right, line);
    }

    return left;
}

static Expr *comparison(Parser *parser) {
    Expr *left = term(parser);

    while (match(parser, TOKEN_LESS) || match(parser, TOKEN_LESS_EQUAL) ||
           match(parser, TOKEN_GREATER) || match(parser, TOKEN_GREATER_EQUAL)) {
        int line = parser->previous.line;
        OpType op;
        switch (parser->previous.type) {
            case TOKEN_LESS:          op = OP_LT; break;
            case TOKEN_LESS_EQUAL:    op = OP_LE; break;
            case TOKEN_GREATER:       op = OP_GT; break;
            case TOKEN_GREATER_EQUAL: op = OP_GE; break;
            default:                  op = OP_LT; break;
        }
        Expr *right = term(parser);
        left = expr_binary(op, left, right, line);
    }

    return left;
}

static Expr *equality(Parser *parser) {
    Expr *left = comparison(parser);

    while (match(parser, TOKEN_EQUAL_EQUAL) || match(parser, TOKEN_BANG_EQUAL)) {
        int line = parser->previous.line;
        OpType op = parser->previous.type == TOKEN_EQUAL_EQUAL ? OP_EQ : OP_NE;
        Expr *right = comparison(parser);
        left = expr_binary(op, left, right, line);
    }

    return left;
}

static Expr *and_expr(Parser *parser) {
    Expr *left = equality(parser);

    while (match(parser, TOKEN_AND)) {
        int line = parser->previous.line;
        Expr *right = equality(parser);
        left = expr_binary(OP_AND, left, right, line);
    }

    return left;
}

static Expr *or_expr(Parser *parser) {
    Expr *left = and_expr(parser);

    while (match(parser, TOKEN_OR)) {
        int line = parser->previous.line;
        Expr *right = and_expr(parser);
        left = expr_binary(OP_OR, left, right, line);
    }

    return left;
}

static Expr *expression(Parser *parser) {
    return or_expr(parser);
}

// ============ STATEMENT PARSING ============

static Stmt *let_statement(Parser *parser) {
    int line = parser->previous.line;
    
    // Check for destructuring: let { x, y } = expr;
    if (check(parser, TOKEN_LBRACE)) {
        advance(parser);  // consume {
        
        char **fields = NULL;
        int field_count = 0;
        int field_cap = 0;
        
        while (!check(parser, TOKEN_RBRACE) && !check(parser, TOKEN_EOF)) {
            consume(parser, TOKEN_IDENT, "Expected field name in destructuring pattern.");
            char *fname = copy_token_string(&parser->previous);
            
            if (field_count >= field_cap) {
                field_cap = field_cap == 0 ? 4 : field_cap * 2;
                fields = realloc(fields, sizeof(char*) * field_cap);
            }
            fields[field_count++] = fname;
            
            if (!check(parser, TOKEN_RBRACE)) {
                consume(parser, TOKEN_COMMA, "Expected ',' or '}' in destructuring pattern.");
            }
        }
        
        consume(parser, TOKEN_RBRACE, "Expected '}' after destructuring pattern.");
        consume(parser, TOKEN_EQUAL, "Expected '=' after destructuring pattern.");
        Expr *initializer = expression(parser);
        consume(parser, TOKEN_SEMICOLON, "Expected ';' after destructuring statement.");
        
        return stmt_destructure(fields, field_count, initializer, line);
    }
    
    consume(parser, TOKEN_IDENT, "Expected variable name.");
    char *name = copy_token_string(&parser->previous);
    
    consume(parser, TOKEN_EQUAL, "Expected '=' after variable name.");
    Expr *initializer = expression(parser);
    
    consume(parser, TOKEN_SEMICOLON, "Expected ';' after variable declaration.");
    
    return stmt_let(name, initializer, line);
}

static Stmt *print_statement(Parser *parser) {
    int line = parser->previous.line;
    Expr *value = expression(parser);
    consume(parser, TOKEN_SEMICOLON, "Expected ';' after print statement.");
    return stmt_print(value, line);
}

static Stmt *if_statement(Parser *parser) {
    int line = parser->previous.line;
    
    Expr *condition = expression(parser);
    Stmt *then_branch = block(parser);
    Stmt *else_branch = NULL;
    
    if (match(parser, TOKEN_ELSE)) {
        if (check(parser, TOKEN_IF)) {
            // else if: parse as nested if statement
            advance(parser);  // consume 'if'
            else_branch = if_statement(parser);
        } else {
            else_branch = block(parser);
        }
    }
    
    return stmt_if(condition, then_branch, else_branch, line);
}

static Stmt *while_statement(Parser *parser) {
    int line = parser->previous.line;
    
    Expr *condition = expression(parser);
    Stmt *body = block(parser);
    
    return stmt_while(condition, body, line);
}

static Stmt *return_statement(Parser *parser) {
    int line = parser->previous.line;
    
    Expr *value = NULL;
    if (!check(parser, TOKEN_SEMICOLON)) {
        value = expression(parser);
    }
    
    consume(parser, TOKEN_SEMICOLON, "Expected ';' after return value.");
    return stmt_return(value, line);
}

static Stmt *expression_statement(Parser *parser) {
    int line = parser->current.line;
    
    // Check for assignment: IDENT = expr; or IDENT[idx] = expr; or IDENT.field = expr;
    if (check(parser, TOKEN_IDENT)) {
        Token ident = parser->current;
        advance(parser);
        
        // Check for array index assignment: arr[idx] = value;
        if (match(parser, TOKEN_LBRACKET)) {
            char *name = malloc(ident.length + 1);
            memcpy(name, ident.start, ident.length);
            name[ident.length] = '\0';
            
            Expr *array = expr_variable(name, ident.line);
            Expr *index = expression(parser);
            consume(parser, TOKEN_RBRACKET, "Expected ']' after index.");
            consume(parser, TOKEN_EQUAL, "Expected '=' for array assignment.");
            Expr *value = expression(parser);
            consume(parser, TOKEN_SEMICOLON, "Expected ';' after assignment.");
            return stmt_index_assign(array, index, value, line);
        }
        
        // Check for field assignment: obj.field = value;
        if (match(parser, TOKEN_DOT)) {
            char *var_name = malloc(ident.length + 1);
            memcpy(var_name, ident.start, ident.length);
            var_name[ident.length] = '\0';
            
            Expr *object = expr_variable(var_name, ident.line);
            
            // Parse chained field access: obj.field1.field2 = value;
            consume(parser, TOKEN_IDENT, "Expected field name after '.'.");
            char *field_name = copy_token_string(&parser->previous);
            
            while (match(parser, TOKEN_DOT)) {
                object = expr_field_access(object, field_name, parser->previous.line);
                free(field_name);
                consume(parser, TOKEN_IDENT, "Expected field name after '.'.");
                field_name = copy_token_string(&parser->previous);
            }
            
            consume(parser, TOKEN_EQUAL, "Expected '=' for field assignment.");
            Expr *value = expression(parser);
            consume(parser, TOKEN_SEMICOLON, "Expected ';' after assignment.");
            
            Stmt *result = stmt_field_assign(object, field_name, value, line);
            free(field_name);
            return result;
        }
        
        if (match(parser, TOKEN_EQUAL)) {
            // It's an assignment
            char *name = malloc(ident.length + 1);
            memcpy(name, ident.start, ident.length);
            name[ident.length] = '\0';
            
            Expr *value = expression(parser);
            consume(parser, TOKEN_SEMICOLON, "Expected ';' after assignment.");
            return stmt_assign(name, value, line);
        }
        
        // Not assignment - we consumed the ident, need to handle it
        // Reconstruct as variable expression and continue parsing
        char *name = malloc(ident.length + 1);
        memcpy(name, ident.start, ident.length);
        name[ident.length] = '\0';
        
        Expr *left = expr_variable(name, ident.line);
        
        // Continue parsing the rest of the expression if there's more
        // For now, simple case - just the variable or it could be a call
        if (match(parser, TOKEN_LPAREN)) {
            // Function call
            Expr **args = NULL;
            int arg_count = 0;
            int capacity = 0;
            
            if (!check(parser, TOKEN_RPAREN)) {
                do {
                    if (arg_count >= capacity) {
                        capacity = capacity == 0 ? 4 : capacity * 2;
                        args = realloc(args, sizeof(Expr*) * capacity);
                    }
                    args[arg_count++] = expression(parser);
                } while (match(parser, TOKEN_COMMA));
            }
            consume(parser, TOKEN_RPAREN, "Expected ')' after arguments.");
            
            char *call_name = malloc(strlen(name) + 1);
            strcpy(call_name, name);
            free(left->as.variable.name);
            free(left);
            left = expr_call(call_name, args, arg_count, ident.line);
        }
        
        consume(parser, TOKEN_SEMICOLON, "Expected ';' after expression.");
        return stmt_expr(left, line);
    }
    
    Expr *expr = expression(parser);
    consume(parser, TOKEN_SEMICOLON, "Expected ';' after expression.");
    return stmt_expr(expr, line);
}

static Stmt *block(Parser *parser) {
    int line = parser->current.line;
    consume(parser, TOKEN_LBRACE, "Expected '{'.");

    Stmt **statements = NULL;
    int count = 0;
    int capacity = 0;

    while (!check(parser, TOKEN_RBRACE) && !check(parser, TOKEN_EOF)) {
        if (count >= capacity) {
            capacity = capacity == 0 ? 8 : capacity * 2;
            statements = realloc(statements, sizeof(Stmt*) * capacity);
        }
        statements[count++] = statement(parser);
    }

    consume(parser, TOKEN_RBRACE, "Expected '}'.");
    return stmt_block(statements, count, line);
}

static Stmt *for_statement(Parser *parser) {
    int line = parser->previous.line;
    
    // for VAR in EXPR { ... }
    consume(parser, TOKEN_IDENT, "Expected variable name after 'for'.");
    char *var_name = copy_token_string(&parser->previous);
    
    consume(parser, TOKEN_IN, "Expected 'in' after for variable.");
    
    // Parse the iterable expression
    Expr *first = expression(parser);
    
    if (match(parser, TOKEN_DOT_DOT)) {
        // Range: for i in START..END { ... }
        Expr *end = expression(parser);
        Stmt *body = block(parser);
        Stmt *result = stmt_for_range(var_name, first, end, body, line);
        free(var_name);
        return result;
    } else {
        // Array iteration: for x in arr { ... }
        Stmt *body = block(parser);
        Stmt *result = stmt_for_array(var_name, first, body, line);
        free(var_name);
        return result;
    }
}

static Stmt *statement(Parser *parser) {
    if (match(parser, TOKEN_LET)) return let_statement(parser);
    if (match(parser, TOKEN_PRINT)) return print_statement(parser);
    if (match(parser, TOKEN_IF)) return if_statement(parser);
    if (match(parser, TOKEN_WHILE)) return while_statement(parser);
    if (match(parser, TOKEN_FOR)) return for_statement(parser);
    if (match(parser, TOKEN_RETURN)) return return_statement(parser);
    if (match(parser, TOKEN_BREAK)) {
        int line = parser->previous.line;
        consume(parser, TOKEN_SEMICOLON, "Expected ';' after break.");
        return stmt_break(line);
    }
    if (match(parser, TOKEN_CONTINUE)) {
        int line = parser->previous.line;
        consume(parser, TOKEN_SEMICOLON, "Expected ';' after continue.");
        return stmt_continue(line);
    }
    if (check(parser, TOKEN_LBRACE)) return block(parser);
    
    return expression_statement(parser);
}

// ============ ENUM PARSING ============

static EnumDef *enum_definition(Parser *parser) {
    // 'enum' already consumed
    consume(parser, TOKEN_IDENT, "Expected enum name.");
    char *name = copy_token_string(&parser->previous);
    
    consume(parser, TOKEN_LBRACE, "Expected '{' after enum name.");
    
    char **variant_names = NULL;
    int *variant_has_data = NULL;
    int variant_count = 0;
    int capacity = 0;
    
    while (!check(parser, TOKEN_RBRACE) && !check(parser, TOKEN_EOF)) {
        consume(parser, TOKEN_IDENT, "Expected variant name.");
        if (variant_count >= capacity) {
            capacity = capacity == 0 ? 4 : capacity * 2;
            variant_names = realloc(variant_names, sizeof(char*) * capacity);
            variant_has_data = realloc(variant_has_data, sizeof(int) * capacity);
        }
        variant_names[variant_count] = copy_token_string(&parser->previous);
        
        // Check for variant with data: Some(value)
        if (match(parser, TOKEN_LPAREN)) {
            // For now, just skip the parameter name - we only support single-value variants
            consume(parser, TOKEN_IDENT, "Expected parameter name in variant.");
            consume(parser, TOKEN_RPAREN, "Expected ')' after variant parameter.");
            variant_has_data[variant_count] = 1;
        } else {
            variant_has_data[variant_count] = 0;
        }
        variant_count++;
        
        // Optional comma between variants
        match(parser, TOKEN_COMMA);
    }
    
    consume(parser, TOKEN_RBRACE, "Expected '}' after enum variants.");
    
    return enum_def_new(name, variant_names, variant_has_data, variant_count);
}

// ============ STRUCT PARSING ============

static StructDef *struct_definition(Parser *parser) {
    // 'struct' already consumed
    consume(parser, TOKEN_IDENT, "Expected struct name.");
    char *name = copy_token_string(&parser->previous);
    
    consume(parser, TOKEN_LBRACE, "Expected '{' after struct name.");
    
    char **field_names = NULL;
    int field_count = 0;
    int capacity = 0;
    
    while (!check(parser, TOKEN_RBRACE) && !check(parser, TOKEN_EOF)) {
        consume(parser, TOKEN_IDENT, "Expected field name.");
        if (field_count >= capacity) {
            capacity = capacity == 0 ? 4 : capacity * 2;
            field_names = realloc(field_names, sizeof(char*) * capacity);
        }
        field_names[field_count++] = copy_token_string(&parser->previous);
        
        // Optional comma between fields
        match(parser, TOKEN_COMMA);
    }
    
    consume(parser, TOKEN_RBRACE, "Expected '}' after struct fields.");
    
    return struct_def_new(name, field_names, field_count);
}

// ============ FUNCTION PARSING ============

static Function *function(Parser *parser) {
    consume(parser, TOKEN_FN, "Expected 'fn'.");
    consume(parser, TOKEN_IDENT, "Expected function name.");
    char *name = copy_token_string(&parser->previous);

    consume(parser, TOKEN_LPAREN, "Expected '(' after function name.");

    // Parse parameters
    char **params = NULL;
    int param_count = 0;
    int capacity = 0;

    if (!check(parser, TOKEN_RPAREN)) {
        do {
            consume(parser, TOKEN_IDENT, "Expected parameter name.");
            if (param_count >= capacity) {
                capacity = capacity == 0 ? 4 : capacity * 2;
                params = realloc(params, sizeof(char*) * capacity);
            }
            params[param_count++] = copy_token_string(&parser->previous);
        } while (match(parser, TOKEN_COMMA));
    }

    consume(parser, TOKEN_RPAREN, "Expected ')' after parameters.");

    Stmt *body = block(parser);

    return function_new(name, params, param_count, body);
}

// ============ IMPL BLOCK PARSING ============

// Parse impl block: impl StructName { fn method(self, ...) { ... } ... }
// Methods get name-mangled to StructName_method
static void impl_block(Parser *parser, Program *prog) {
    // 'impl' already consumed
    consume(parser, TOKEN_IDENT, "Expected struct name after 'impl'.");
    char *struct_name = copy_token_string(&parser->previous);
    
    consume(parser, TOKEN_LBRACE, "Expected '{' after impl struct name.");
    
    while (!check(parser, TOKEN_RBRACE) && !check(parser, TOKEN_EOF)) {
        // Each item in impl block must be a function
        Function *fn = function(parser);
        
        // Mangle name: "method" -> "StructName_method"
        int slen = strlen(struct_name);
        int mlen = strlen(fn->name);
        char *mangled = malloc(slen + 1 + mlen + 1);
        memcpy(mangled, struct_name, slen);
        mangled[slen] = '_';
        memcpy(mangled + slen + 1, fn->name, mlen);
        mangled[slen + 1 + mlen] = '\0';
        
        free(fn->name);
        fn->name = mangled;
        
        program_add_function(prog, fn);
    }
    
    consume(parser, TOKEN_RBRACE, "Expected '}' after impl block.");
    free(struct_name);
}

// ============ PUBLIC API ============

void parser_init(Parser *parser, Lexer *lexer) {
    parser->lexer = lexer;
    parser->had_error = 0;
    parser->panic_mode = 0;
    advance(parser);  // Prime the pump
}

Program *parser_parse(Parser *parser) {
    Program *prog = program_new();

    while (!check(parser, TOKEN_EOF)) {
        if (match(parser, TOKEN_STRUCT)) {
            StructDef *sd = struct_definition(parser);
            program_add_struct(prog, sd);
        } else if (match(parser, TOKEN_ENUM)) {
            EnumDef *ed = enum_definition(parser);
            program_add_enum(prog, ed);
        } else if (match(parser, TOKEN_IMPL)) {
            impl_block(parser, prog);
        } else {
            Function *fn = function(parser);
            program_add_function(prog, fn);
        }
    }

    return prog;
}
