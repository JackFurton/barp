/*
 * Teddy AST Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"

// ============ HELPER ============

static char *str_dup(const char *s) {
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

static void print_indent(int indent) {
    for (int i = 0; i < indent; i++) {
        printf("  ");
    }
}

// ============ EXPRESSION CONSTRUCTORS ============

Expr *expr_int_literal(long value, int line) {
    Expr *e = malloc(sizeof(Expr));
    e->type = EXPR_INT_LITERAL;
    e->line = line;
    e->as.int_literal.value = value;
    return e;
}

Expr *expr_bool_literal(int value, int line) {
    Expr *e = malloc(sizeof(Expr));
    e->type = EXPR_BOOL_LITERAL;
    e->line = line;
    e->as.bool_literal.value = value;
    return e;
}

Expr *expr_string_literal(char *value, int length, int line) {
    Expr *e = malloc(sizeof(Expr));
    e->type = EXPR_STRING_LITERAL;
    e->line = line;
    e->as.string_literal.value = str_dup(value);
    e->as.string_literal.length = length;
    return e;
}

Expr *expr_variable(char *name, int line) {
    Expr *e = malloc(sizeof(Expr));
    e->type = EXPR_VARIABLE;
    e->line = line;
    e->as.variable.name = str_dup(name);
    return e;
}

Expr *expr_binary(OpType op, Expr *left, Expr *right, int line) {
    Expr *e = malloc(sizeof(Expr));
    e->type = EXPR_BINARY;
    e->line = line;
    e->as.binary.op = op;
    e->as.binary.left = left;
    e->as.binary.right = right;
    return e;
}

Expr *expr_unary(OpType op, Expr *operand, int line) {
    Expr *e = malloc(sizeof(Expr));
    e->type = EXPR_UNARY;
    e->line = line;
    e->as.unary.op = op;
    e->as.unary.operand = operand;
    return e;
}

Expr *expr_call(char *name, Expr **args, int arg_count, int line) {
    Expr *e = malloc(sizeof(Expr));
    e->type = EXPR_CALL;
    e->line = line;
    e->as.call.name = str_dup(name);
    e->as.call.args = args;
    e->as.call.arg_count = arg_count;
    return e;
}

Expr *expr_array_literal(Expr **elements, int count, int line) {
    Expr *e = malloc(sizeof(Expr));
    e->type = EXPR_ARRAY_LITERAL;
    e->line = line;
    e->as.array_literal.elements = elements;
    e->as.array_literal.count = count;
    return e;
}

Expr *expr_index(Expr *array, Expr *index, int line) {
    Expr *e = malloc(sizeof(Expr));
    e->type = EXPR_INDEX;
    e->line = line;
    e->as.index.array = array;
    e->as.index.index = index;
    return e;
}

Expr *expr_struct_literal(char *struct_name, char **field_names, Expr **field_values, int field_count, int line) {
    Expr *e = malloc(sizeof(Expr));
    e->type = EXPR_STRUCT_LITERAL;
    e->line = line;
    e->as.struct_literal.struct_name = str_dup(struct_name);
    e->as.struct_literal.field_names = field_names;
    e->as.struct_literal.field_values = field_values;
    e->as.struct_literal.field_count = field_count;
    return e;
}

Expr *expr_field_access(Expr *object, char *field_name, int line) {
    Expr *e = malloc(sizeof(Expr));
    e->type = EXPR_FIELD_ACCESS;
    e->line = line;
    e->as.field_access.object = object;
    e->as.field_access.field_name = str_dup(field_name);
    return e;
}

Expr *expr_enum_variant(char *enum_name, char *variant_name, Expr *payload, int line) {
    Expr *e = malloc(sizeof(Expr));
    e->type = EXPR_ENUM_VARIANT;
    e->line = line;
    e->as.enum_variant.enum_name = str_dup(enum_name);
    e->as.enum_variant.variant_name = str_dup(variant_name);
    e->as.enum_variant.payload = payload;
    return e;
}

MatchArm *match_arm_new(char *enum_name, char *variant_name, char *binding_name, Expr *body) {
    MatchArm *arm = malloc(sizeof(MatchArm));
    arm->enum_name = enum_name ? str_dup(enum_name) : NULL;
    arm->variant_name = str_dup(variant_name);
    arm->binding_name = binding_name ? str_dup(binding_name) : NULL;
    arm->body = body;
    return arm;
}

Expr *expr_match(Expr *value, MatchArm **arms, int arm_count, int line) {
    Expr *e = malloc(sizeof(Expr));
    e->type = EXPR_MATCH;
    e->line = line;
    e->as.match.value = value;
    e->as.match.arms = arms;
    e->as.match.arm_count = arm_count;
    return e;
}

Expr *expr_method_call(Expr *object, char *method_name, Expr **args, int arg_count, int line) {
    Expr *e = malloc(sizeof(Expr));
    e->type = EXPR_METHOD_CALL;
    e->line = line;
    e->as.method_call.object = object;
    e->as.method_call.method_name = str_dup(method_name);
    e->as.method_call.args = args;
    e->as.method_call.arg_count = arg_count;
    return e;
}

// ============ STATEMENT CONSTRUCTORS ============

Stmt *stmt_expr(Expr *expr, int line) {
    Stmt *s = malloc(sizeof(Stmt));
    s->type = STMT_EXPR;
    s->line = line;
    s->as.expr.expr = expr;
    return s;
}

Stmt *stmt_print(Expr *expr, int line) {
    Stmt *s = malloc(sizeof(Stmt));
    s->type = STMT_PRINT;
    s->line = line;
    s->as.print.expr = expr;
    return s;
}

Stmt *stmt_let(char *name, Expr *initializer, int line) {
    Stmt *s = malloc(sizeof(Stmt));
    s->type = STMT_LET;
    s->line = line;
    s->as.let.name = str_dup(name);
    s->as.let.initializer = initializer;
    return s;
}

Stmt *stmt_assign(char *name, Expr *value, int line) {
    Stmt *s = malloc(sizeof(Stmt));
    s->type = STMT_ASSIGN;
    s->line = line;
    s->as.assign.name = str_dup(name);
    s->as.assign.value = value;
    return s;
}

Stmt *stmt_index_assign(Expr *array, Expr *index, Expr *value, int line) {
    Stmt *s = malloc(sizeof(Stmt));
    s->type = STMT_INDEX_ASSIGN;
    s->line = line;
    s->as.index_assign.array = array;
    s->as.index_assign.index = index;
    s->as.index_assign.value = value;
    return s;
}

Stmt *stmt_field_assign(Expr *object, char *field_name, Expr *value, int line) {
    Stmt *s = malloc(sizeof(Stmt));
    s->type = STMT_FIELD_ASSIGN;
    s->line = line;
    s->as.field_assign.object = object;
    s->as.field_assign.field_name = str_dup(field_name);
    s->as.field_assign.value = value;
    return s;
}

Stmt *stmt_block(Stmt **statements, int count, int line) {
    Stmt *s = malloc(sizeof(Stmt));
    s->type = STMT_BLOCK;
    s->line = line;
    s->as.block.statements = statements;
    s->as.block.count = count;
    return s;
}

Stmt *stmt_if(Expr *condition, Stmt *then_branch, Stmt *else_branch, int line) {
    Stmt *s = malloc(sizeof(Stmt));
    s->type = STMT_IF;
    s->line = line;
    s->as.if_stmt.condition = condition;
    s->as.if_stmt.then_branch = then_branch;
    s->as.if_stmt.else_branch = else_branch;
    return s;
}

Stmt *stmt_while(Expr *condition, Stmt *body, int line) {
    Stmt *s = malloc(sizeof(Stmt));
    s->type = STMT_WHILE;
    s->line = line;
    s->as.while_stmt.condition = condition;
    s->as.while_stmt.body = body;
    return s;
}

Stmt *stmt_for_range(char *var_name, Expr *start, Expr *end, Stmt *body, int line) {
    Stmt *s = malloc(sizeof(Stmt));
    s->type = STMT_FOR;
    s->line = line;
    s->as.for_stmt.var_name = str_dup(var_name);
    s->as.for_stmt.start = start;
    s->as.for_stmt.end = end;
    s->as.for_stmt.iterable = NULL;
    s->as.for_stmt.body = body;
    return s;
}

Stmt *stmt_for_array(char *var_name, Expr *iterable, Stmt *body, int line) {
    Stmt *s = malloc(sizeof(Stmt));
    s->type = STMT_FOR;
    s->line = line;
    s->as.for_stmt.var_name = str_dup(var_name);
    s->as.for_stmt.start = NULL;
    s->as.for_stmt.end = NULL;
    s->as.for_stmt.iterable = iterable;
    s->as.for_stmt.body = body;
    return s;
}

Stmt *stmt_return(Expr *value, int line) {
    Stmt *s = malloc(sizeof(Stmt));
    s->type = STMT_RETURN;
    s->line = line;
    s->as.return_stmt.value = value;
    return s;
}

Stmt *stmt_break(int line) {
    Stmt *s = malloc(sizeof(Stmt));
    s->type = STMT_BREAK;
    s->line = line;
    return s;
}

Stmt *stmt_continue(int line) {
    Stmt *s = malloc(sizeof(Stmt));
    s->type = STMT_CONTINUE;
    s->line = line;
    return s;
}

// ============ PROGRAM ============

StructDef *struct_def_new(char *name, char **field_names, int field_count) {
    StructDef *sd = malloc(sizeof(StructDef));
    sd->name = str_dup(name);
    sd->field_names = field_names;
    sd->field_count = field_count;
    return sd;
}

EnumDef *enum_def_new(char *name, char **variant_names, int *variant_has_data, int variant_count) {
    EnumDef *ed = malloc(sizeof(EnumDef));
    ed->name = str_dup(name);
    ed->variant_names = variant_names;
    ed->variant_has_data = variant_has_data;
    ed->variant_count = variant_count;
    return ed;
}

Function *function_new(char *name, char **params, int param_count, Stmt *body) {
    Function *fn = malloc(sizeof(Function));
    fn->name = str_dup(name);
    fn->params = params;
    fn->param_count = param_count;
    fn->body = body;
    return fn;
}

Program *program_new(void) {
    Program *prog = malloc(sizeof(Program));
    prog->structs = NULL;
    prog->struct_count = 0;
    prog->enums = NULL;
    prog->enum_count = 0;
    prog->functions = NULL;
    prog->function_count = 0;
    return prog;
}

void program_add_struct(Program *prog, StructDef *sd) {
    prog->struct_count++;
    prog->structs = realloc(prog->structs, sizeof(StructDef*) * prog->struct_count);
    prog->structs[prog->struct_count - 1] = sd;
}

void program_add_enum(Program *prog, EnumDef *ed) {
    prog->enum_count++;
    prog->enums = realloc(prog->enums, sizeof(EnumDef*) * prog->enum_count);
    prog->enums[prog->enum_count - 1] = ed;
}

void program_add_function(Program *prog, Function *fn) {
    prog->function_count++;
    prog->functions = realloc(prog->functions, 
                              sizeof(Function*) * prog->function_count);
    prog->functions[prog->function_count - 1] = fn;
}

// ============ MEMORY CLEANUP ============

void expr_free(Expr *expr) {
    if (!expr) return;
    
    switch (expr->type) {
        case EXPR_INT_LITERAL:
        case EXPR_BOOL_LITERAL:
            break;
        case EXPR_STRING_LITERAL:
            free(expr->as.string_literal.value);
            break;
        case EXPR_VARIABLE:
            free(expr->as.variable.name);
            break;
        case EXPR_BINARY:
            expr_free(expr->as.binary.left);
            expr_free(expr->as.binary.right);
            break;
        case EXPR_UNARY:
            expr_free(expr->as.unary.operand);
            break;
        case EXPR_CALL:
            free(expr->as.call.name);
            for (int i = 0; i < expr->as.call.arg_count; i++) {
                expr_free(expr->as.call.args[i]);
            }
            free(expr->as.call.args);
            break;
        case EXPR_ARRAY_LITERAL:
            for (int i = 0; i < expr->as.array_literal.count; i++) {
                expr_free(expr->as.array_literal.elements[i]);
            }
            free(expr->as.array_literal.elements);
            break;
        case EXPR_INDEX:
            expr_free(expr->as.index.array);
            expr_free(expr->as.index.index);
            break;
        case EXPR_STRUCT_LITERAL:
            free(expr->as.struct_literal.struct_name);
            for (int i = 0; i < expr->as.struct_literal.field_count; i++) {
                free(expr->as.struct_literal.field_names[i]);
                expr_free(expr->as.struct_literal.field_values[i]);
            }
            free(expr->as.struct_literal.field_names);
            free(expr->as.struct_literal.field_values);
            break;
        case EXPR_FIELD_ACCESS:
            expr_free(expr->as.field_access.object);
            free(expr->as.field_access.field_name);
            break;
        case EXPR_ENUM_VARIANT:
            free(expr->as.enum_variant.enum_name);
            free(expr->as.enum_variant.variant_name);
            if (expr->as.enum_variant.payload) {
                expr_free(expr->as.enum_variant.payload);
            }
            break;
        case EXPR_MATCH:
            expr_free(expr->as.match.value);
            for (int i = 0; i < expr->as.match.arm_count; i++) {
                MatchArm *arm = expr->as.match.arms[i];
                if (arm->enum_name) free(arm->enum_name);
                free(arm->variant_name);
                if (arm->binding_name) free(arm->binding_name);
                expr_free(arm->body);
                free(arm);
            }
            free(expr->as.match.arms);
            break;
        case EXPR_METHOD_CALL:
            expr_free(expr->as.method_call.object);
            free(expr->as.method_call.method_name);
            for (int i = 0; i < expr->as.method_call.arg_count; i++) {
                expr_free(expr->as.method_call.args[i]);
            }
            free(expr->as.method_call.args);
            break;
    }
    free(expr);
}

void stmt_free(Stmt *stmt) {
    if (!stmt) return;
    
    switch (stmt->type) {
        case STMT_EXPR:
            expr_free(stmt->as.expr.expr);
            break;
        case STMT_PRINT:
            expr_free(stmt->as.print.expr);
            break;
        case STMT_LET:
            free(stmt->as.let.name);
            expr_free(stmt->as.let.initializer);
            break;
        case STMT_ASSIGN:
            free(stmt->as.assign.name);
            expr_free(stmt->as.assign.value);
            break;
        case STMT_INDEX_ASSIGN:
            expr_free(stmt->as.index_assign.array);
            expr_free(stmt->as.index_assign.index);
            expr_free(stmt->as.index_assign.value);
            break;
        case STMT_FIELD_ASSIGN:
            expr_free(stmt->as.field_assign.object);
            free(stmt->as.field_assign.field_name);
            expr_free(stmt->as.field_assign.value);
            break;
        case STMT_BLOCK:
            for (int i = 0; i < stmt->as.block.count; i++) {
                stmt_free(stmt->as.block.statements[i]);
            }
            free(stmt->as.block.statements);
            break;
        case STMT_IF:
            expr_free(stmt->as.if_stmt.condition);
            stmt_free(stmt->as.if_stmt.then_branch);
            stmt_free(stmt->as.if_stmt.else_branch);
            break;
        case STMT_WHILE:
            expr_free(stmt->as.while_stmt.condition);
            stmt_free(stmt->as.while_stmt.body);
            break;
        case STMT_FOR:
            free(stmt->as.for_stmt.var_name);
            if (stmt->as.for_stmt.start) expr_free(stmt->as.for_stmt.start);
            if (stmt->as.for_stmt.end) expr_free(stmt->as.for_stmt.end);
            if (stmt->as.for_stmt.iterable) expr_free(stmt->as.for_stmt.iterable);
            stmt_free(stmt->as.for_stmt.body);
            break;
        case STMT_RETURN:
            expr_free(stmt->as.return_stmt.value);
            break;
        case STMT_BREAK:
        case STMT_CONTINUE:
            break;
    }
    free(stmt);
}

void struct_def_free(StructDef *sd) {
    if (!sd) return;
    free(sd->name);
    for (int i = 0; i < sd->field_count; i++) {
        free(sd->field_names[i]);
    }
    free(sd->field_names);
    free(sd);
}

void enum_def_free(EnumDef *ed) {
    if (!ed) return;
    free(ed->name);
    for (int i = 0; i < ed->variant_count; i++) {
        free(ed->variant_names[i]);
    }
    free(ed->variant_names);
    free(ed->variant_has_data);
    free(ed);
}

void function_free(Function *fn) {
    if (!fn) return;
    free(fn->name);
    for (int i = 0; i < fn->param_count; i++) {
        free(fn->params[i]);
    }
    free(fn->params);
    stmt_free(fn->body);
    free(fn);
}

void program_free(Program *prog) {
    if (!prog) return;
    for (int i = 0; i < prog->struct_count; i++) {
        struct_def_free(prog->structs[i]);
    }
    free(prog->structs);
    for (int i = 0; i < prog->enum_count; i++) {
        enum_def_free(prog->enums[i]);
    }
    free(prog->enums);
    for (int i = 0; i < prog->function_count; i++) {
        function_free(prog->functions[i]);
    }
    free(prog->functions);
    free(prog);
}

// ============ DEBUG PRINTING ============

static const char *op_name(OpType op) {
    switch (op) {
        case OP_ADD: return "+";
        case OP_SUB: return "-";
        case OP_MUL: return "*";
        case OP_DIV: return "/";
        case OP_MOD: return "%";
        case OP_EQ:  return "==";
        case OP_NE:  return "!=";
        case OP_LT:  return "<";
        case OP_LE:  return "<=";
        case OP_GT:  return ">";
        case OP_GE:  return ">=";
        case OP_AND: return "&&";
        case OP_OR:  return "||";
        case OP_NEG: return "-";
        case OP_NOT: return "!";
        default:     return "?";
    }
}

void ast_print_expr(Expr *expr, int indent) {
    print_indent(indent);
    
    switch (expr->type) {
        case EXPR_INT_LITERAL:
            printf("Int(%ld)\n", expr->as.int_literal.value);
            break;
        case EXPR_BOOL_LITERAL:
            printf("Bool(%s)\n", expr->as.bool_literal.value ? "true" : "false");
            break;
        case EXPR_STRING_LITERAL:
            printf("String(\"%s\")\n", expr->as.string_literal.value);
            break;
        case EXPR_VARIABLE:
            printf("Var(%s)\n", expr->as.variable.name);
            break;
        case EXPR_BINARY:
            printf("Binary(%s)\n", op_name(expr->as.binary.op));
            ast_print_expr(expr->as.binary.left, indent + 1);
            ast_print_expr(expr->as.binary.right, indent + 1);
            break;
        case EXPR_UNARY:
            printf("Unary(%s)\n", op_name(expr->as.unary.op));
            ast_print_expr(expr->as.unary.operand, indent + 1);
            break;
        case EXPR_CALL:
            printf("Call(%s, %d args)\n", expr->as.call.name, expr->as.call.arg_count);
            for (int i = 0; i < expr->as.call.arg_count; i++) {
                ast_print_expr(expr->as.call.args[i], indent + 1);
            }
            break;
        case EXPR_ARRAY_LITERAL:
            printf("Array(%d elements)\n", expr->as.array_literal.count);
            for (int i = 0; i < expr->as.array_literal.count; i++) {
                ast_print_expr(expr->as.array_literal.elements[i], indent + 1);
            }
            break;
        case EXPR_INDEX:
            printf("Index\n");
            ast_print_expr(expr->as.index.array, indent + 1);
            ast_print_expr(expr->as.index.index, indent + 1);
            break;
        case EXPR_STRUCT_LITERAL:
            printf("StructLiteral(%s, %d fields)\n", 
                   expr->as.struct_literal.struct_name,
                   expr->as.struct_literal.field_count);
            for (int i = 0; i < expr->as.struct_literal.field_count; i++) {
                print_indent(indent + 1);
                printf("%s:\n", expr->as.struct_literal.field_names[i]);
                ast_print_expr(expr->as.struct_literal.field_values[i], indent + 2);
            }
            break;
        case EXPR_FIELD_ACCESS:
            printf("FieldAccess(.%s)\n", expr->as.field_access.field_name);
            ast_print_expr(expr->as.field_access.object, indent + 1);
            break;
        case EXPR_ENUM_VARIANT:
            printf("EnumVariant(%s::%s", 
                   expr->as.enum_variant.enum_name,
                   expr->as.enum_variant.variant_name);
            if (expr->as.enum_variant.payload) {
                printf("(...)");
            }
            printf(")\n");
            if (expr->as.enum_variant.payload) {
                ast_print_expr(expr->as.enum_variant.payload, indent + 1);
            }
            break;
        case EXPR_MATCH:
            printf("Match(%d arms)\n", expr->as.match.arm_count);
            print_indent(indent + 1); printf("value:\n");
            ast_print_expr(expr->as.match.value, indent + 2);
            for (int i = 0; i < expr->as.match.arm_count; i++) {
                MatchArm *arm = expr->as.match.arms[i];
                print_indent(indent + 1);
                if (arm->enum_name) {
                    printf("arm %s::%s", arm->enum_name, arm->variant_name);
                } else {
                    printf("arm %s", arm->variant_name);
                }
                if (arm->binding_name) {
                    printf("(%s)", arm->binding_name);
                }
                printf(" =>\n");
                ast_print_expr(arm->body, indent + 2);
            }
            break;
        case EXPR_METHOD_CALL:
            printf("MethodCall(.%s, %d args)\n", 
                   expr->as.method_call.method_name,
                   expr->as.method_call.arg_count);
            print_indent(indent + 1); printf("object:\n");
            ast_print_expr(expr->as.method_call.object, indent + 2);
            for (int i = 0; i < expr->as.method_call.arg_count; i++) {
                ast_print_expr(expr->as.method_call.args[i], indent + 1);
            }
            break;
    }
}

void ast_print_stmt(Stmt *stmt, int indent) {
    print_indent(indent);
    
    switch (stmt->type) {
        case STMT_EXPR:
            printf("ExprStmt\n");
            ast_print_expr(stmt->as.expr.expr, indent + 1);
            break;
        case STMT_PRINT:
            printf("Print\n");
            ast_print_expr(stmt->as.print.expr, indent + 1);
            break;
        case STMT_LET:
            printf("Let(%s)\n", stmt->as.let.name);
            ast_print_expr(stmt->as.let.initializer, indent + 1);
            break;
        case STMT_ASSIGN:
            printf("Assign(%s)\n", stmt->as.assign.name);
            ast_print_expr(stmt->as.assign.value, indent + 1);
            break;
        case STMT_INDEX_ASSIGN:
            printf("IndexAssign\n");
            print_indent(indent + 1); printf("array:\n");
            ast_print_expr(stmt->as.index_assign.array, indent + 2);
            print_indent(indent + 1); printf("index:\n");
            ast_print_expr(stmt->as.index_assign.index, indent + 2);
            print_indent(indent + 1); printf("value:\n");
            ast_print_expr(stmt->as.index_assign.value, indent + 2);
            break;
        case STMT_FIELD_ASSIGN:
            printf("FieldAssign(.%s)\n", stmt->as.field_assign.field_name);
            print_indent(indent + 1); printf("object:\n");
            ast_print_expr(stmt->as.field_assign.object, indent + 2);
            print_indent(indent + 1); printf("value:\n");
            ast_print_expr(stmt->as.field_assign.value, indent + 2);
            break;
        case STMT_BLOCK:
            printf("Block(%d stmts)\n", stmt->as.block.count);
            for (int i = 0; i < stmt->as.block.count; i++) {
                ast_print_stmt(stmt->as.block.statements[i], indent + 1);
            }
            break;
        case STMT_IF:
            printf("If\n");
            print_indent(indent + 1); printf("condition:\n");
            ast_print_expr(stmt->as.if_stmt.condition, indent + 2);
            print_indent(indent + 1); printf("then:\n");
            ast_print_stmt(stmt->as.if_stmt.then_branch, indent + 2);
            if (stmt->as.if_stmt.else_branch) {
                print_indent(indent + 1); printf("else:\n");
                ast_print_stmt(stmt->as.if_stmt.else_branch, indent + 2);
            }
            break;
        case STMT_WHILE:
            printf("While\n");
            print_indent(indent + 1); printf("condition:\n");
            ast_print_expr(stmt->as.while_stmt.condition, indent + 2);
            print_indent(indent + 1); printf("body:\n");
            ast_print_stmt(stmt->as.while_stmt.body, indent + 2);
            break;
        case STMT_FOR:
            printf("For(%s)\n", stmt->as.for_stmt.var_name);
            if (stmt->as.for_stmt.start) {
                print_indent(indent + 1); printf("range:\n");
                ast_print_expr(stmt->as.for_stmt.start, indent + 2);
                ast_print_expr(stmt->as.for_stmt.end, indent + 2);
            } else {
                print_indent(indent + 1); printf("iterable:\n");
                ast_print_expr(stmt->as.for_stmt.iterable, indent + 2);
            }
            print_indent(indent + 1); printf("body:\n");
            ast_print_stmt(stmt->as.for_stmt.body, indent + 2);
            break;
        case STMT_RETURN:
            printf("Return\n");
            if (stmt->as.return_stmt.value) {
                ast_print_expr(stmt->as.return_stmt.value, indent + 1);
            }
            break;
        case STMT_BREAK:
            printf("Break\n");
            break;
        case STMT_CONTINUE:
            printf("Continue\n");
            break;
    }
}

void ast_print_program(Program *prog) {
    printf("=== AST ===\n");
    for (int i = 0; i < prog->struct_count; i++) {
        StructDef *sd = prog->structs[i];
        printf("Struct: %s { ", sd->name);
        for (int j = 0; j < sd->field_count; j++) {
            printf("%s", sd->field_names[j]);
            if (j < sd->field_count - 1) printf(", ");
        }
        printf(" }\n");
    }
    for (int i = 0; i < prog->enum_count; i++) {
        EnumDef *ed = prog->enums[i];
        printf("Enum: %s { ", ed->name);
        for (int j = 0; j < ed->variant_count; j++) {
            printf("%s", ed->variant_names[j]);
            if (j < ed->variant_count - 1) printf(", ");
        }
        printf(" }\n");
    }
    for (int i = 0; i < prog->function_count; i++) {
        Function *fn = prog->functions[i];
        printf("Function: %s(", fn->name);
        for (int j = 0; j < fn->param_count; j++) {
            printf("%s", fn->params[j]);
            if (j < fn->param_count - 1) printf(", ");
        }
        printf(")\n");
        ast_print_stmt(fn->body, 1);
    }
}
