/*
 * Teddy AST (Abstract Syntax Tree)
 * Defines the structure of our parsed program.
 */

#ifndef TEDDY_AST_H
#define TEDDY_AST_H

#include <stddef.h>

// Forward declarations
typedef struct Expr Expr;
typedef struct Stmt Stmt;

// ============ EXPRESSIONS ============

typedef enum {
    EXPR_INT_LITERAL,
    EXPR_BOOL_LITERAL,
    EXPR_STRING_LITERAL,
    EXPR_ARRAY_LITERAL,
    EXPR_VARIABLE,
    EXPR_BINARY,
    EXPR_UNARY,
    EXPR_CALL,
    EXPR_INDEX,
    EXPR_STRUCT_LITERAL,
    EXPR_FIELD_ACCESS,
    EXPR_ENUM_VARIANT,    // Color::Red
    EXPR_MATCH,           // match expr { ... }
    EXPR_METHOD_CALL,     // obj.method(args)
    EXPR_CLOSURE,         // |x, y| expr or |x, y| { stmts }
    EXPR_STRING_INTERP,   // "hello {name}, age {age}"
} ExprType;

typedef enum {
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_EQ,
    OP_NE,
    OP_LT,
    OP_LE,
    OP_GT,
    OP_GE,
    OP_AND,
    OP_OR,
    OP_NEG,     // Unary -
    OP_NOT,     // Unary !
} OpType;

typedef struct {
    long value;
} IntLiteralExpr;

typedef struct {
    int value;  // 0 or 1
} BoolLiteralExpr;

typedef struct {
    char *value;
    int length;
} StringLiteralExpr;

typedef struct {
    char *name;
} VariableExpr;

typedef struct {
    OpType op;
    Expr *left;
    Expr *right;
} BinaryExpr;

typedef struct {
    OpType op;
    Expr *operand;
} UnaryExpr;

typedef struct {
    char *name;
    Expr **args;
    char **arg_names;    // Named argument labels (NULL entries for positional args)
    int arg_count;
} CallExpr;

typedef struct {
    Expr **elements;
    int count;
} ArrayLiteralExpr;

typedef struct {
    Expr *array;   // The array being indexed
    Expr *index;   // The index expression
} IndexExpr;

typedef struct {
    char *struct_name;     // Name of the struct type (e.g., "Point")
    char **field_names;    // Array of field names
    Expr **field_values;   // Array of field value expressions
    int field_count;
} StructLiteralExpr;

typedef struct {
    Expr *object;          // The struct being accessed
    char *field_name;      // The field name
} FieldAccessExpr;

typedef struct {
    char *enum_name;       // e.g., "Color" or "Option"
    char *variant_name;    // e.g., "Red" or "Some"
    Expr *payload;         // e.g., the 42 in Option::Some(42), NULL if no data
} EnumVariantExpr;

// Forward declaration for match arms
typedef struct MatchArm MatchArm;

typedef struct {
    Expr *value;           // The expression being matched
    MatchArm **arms;       // Array of match arms
    int arm_count;
} MatchExpr;

typedef struct {
    Expr *object;          // The receiver (e.g., `p` in `p.distance(q)`)
    char *method_name;     // The method name (e.g., "distance")
    Expr **args;           // Arguments (not including self)
    int arg_count;
} MethodCallExpr;

typedef struct {
    char **params;         // Parameter names
    int param_count;
    Expr *body_expr;       // Body as expression (for |x| x + 1), or NULL
    Stmt *body_block;      // Body as block (for |x| { ... }), or NULL
} ClosureExpr;

typedef struct {
    Expr **parts;          // Array of parts: EXPR_STRING_LITERAL or expression
    int part_count;
} StringInterpExpr;

struct MatchArm {
    char *enum_name;       // e.g., "Color" (can be NULL if inferred)
    char *variant_name;    // e.g., "Red"
    char *binding_name;    // e.g., "x" in Some(x) => ... (can be NULL)
    Expr *body;            // The body expression
};

struct Expr {
    ExprType type;
    int line;
    union {
        IntLiteralExpr int_literal;
        BoolLiteralExpr bool_literal;
        StringLiteralExpr string_literal;
        ArrayLiteralExpr array_literal;
        VariableExpr variable;
        BinaryExpr binary;
        UnaryExpr unary;
        CallExpr call;
        IndexExpr index;
        StructLiteralExpr struct_literal;
        FieldAccessExpr field_access;
        EnumVariantExpr enum_variant;
        MatchExpr match;
        MethodCallExpr method_call;
        ClosureExpr closure;
        StringInterpExpr string_interp;
    } as;
};

// ============ STATEMENTS ============

typedef enum {
    STMT_EXPR,
    STMT_PRINT,
    STMT_LET,
    STMT_ASSIGN,
    STMT_INDEX_ASSIGN,
    STMT_FIELD_ASSIGN,
    STMT_BLOCK,
    STMT_IF,
    STMT_WHILE,
    STMT_FOR,
    STMT_RETURN,
    STMT_BREAK,
    STMT_CONTINUE,
    STMT_DESTRUCTURE,
} StmtType;

typedef struct {
    Expr *expr;
} ExprStmt;

typedef struct {
    Expr *expr;
} PrintStmt;

typedef struct {
    char *name;
    char *type_name;  // Optional type annotation, NULL if omitted
    Expr *initializer;
} LetStmt;

typedef struct {
    char *name;
    Expr *value;
} AssignStmt;

typedef struct {
    Expr *array;    // The array expression (usually a variable)
    Expr *index;    // The index
    Expr *value;    // The value to assign
} IndexAssignStmt;

typedef struct {
    Expr *object;      // The struct expression
    char *field_name;  // The field name
    Expr *value;       // The value to assign
} FieldAssignStmt;

typedef struct {
    Stmt **statements;
    int count;
} BlockStmt;

typedef struct {
    Expr *condition;
    Stmt *then_branch;
    Stmt *else_branch;  // Can be NULL
} IfStmt;

typedef struct {
    Expr *condition;
    Stmt *body;
} WhileStmt;

typedef struct {
    char *var_name;    // Loop variable name
    Expr *start;       // Range start (NULL for array iteration)
    Expr *end;         // Range end (NULL for array iteration)
    Expr *iterable;    // Array expression (NULL for range)
    Stmt *body;
} ForStmt;

typedef struct {
    Expr *value;  // Can be NULL
} ReturnStmt;

typedef struct {
    char **field_names;   // Field names to extract
    int field_count;
    Expr *initializer;    // The struct expression to destructure
} DestructureStmt;

struct Stmt {
    StmtType type;
    int line;
    union {
        ExprStmt expr;
        PrintStmt print;
        LetStmt let;
        AssignStmt assign;
        IndexAssignStmt index_assign;
        FieldAssignStmt field_assign;
        BlockStmt block;
        IfStmt if_stmt;
        WhileStmt while_stmt;
        ForStmt for_stmt;
        ReturnStmt return_stmt;
        DestructureStmt destructure;
    } as;
};

// ============ TOP-LEVEL ============

typedef struct {
    char *name;
    char **field_names;
    char **field_types;  // Optional per-field type annotations
    int field_count;
} StructDef;

typedef struct {
    char *name;
    char **variant_names;
    int *variant_has_data;   // 1 if variant has payload, 0 if not
    char **variant_payload_types;  // Optional per-variant payload type annotations
    int variant_count;
} EnumDef;

typedef struct {
    char *name;
    char **params;
    char **param_types;   // Optional per-parameter type annotations
    Expr **defaults;     // Default value expressions (NULL if no default)
    char *return_type;   // Optional return type annotation
    char **effects;      // Optional effect annotations after `with`
    int effect_count;
    int param_count;
    Stmt *body;  // Block statement
} Function;

typedef struct {
    StructDef **structs;
    int struct_count;
    EnumDef **enums;
    int enum_count;
    Function **functions;
    int function_count;
} Program;

// ============ CONSTRUCTORS ============

// Expressions
Expr *expr_int_literal(long value, int line);
Expr *expr_bool_literal(int value, int line);
Expr *expr_string_literal(char *value, int length, int line);
Expr *expr_variable(char *name, int line);
Expr *expr_binary(OpType op, Expr *left, Expr *right, int line);
Expr *expr_unary(OpType op, Expr *operand, int line);
Expr *expr_call(char *name, Expr **args, int arg_count, int line);
Expr *expr_array_literal(Expr **elements, int count, int line);
Expr *expr_index(Expr *array, Expr *index, int line);
Expr *expr_struct_literal(char *struct_name, char **field_names, Expr **field_values, int field_count, int line);
Expr *expr_field_access(Expr *object, char *field_name, int line);
Expr *expr_enum_variant(char *enum_name, char *variant_name, Expr *payload, int line);
MatchArm *match_arm_new(char *enum_name, char *variant_name, char *binding_name, Expr *body);
Expr *expr_match(Expr *value, MatchArm **arms, int arm_count, int line);
Expr *expr_method_call(Expr *object, char *method_name, Expr **args, int arg_count, int line);
Expr *expr_closure(char **params, int param_count, Expr *body_expr, Stmt *body_block, int line);
Expr *expr_string_interp(Expr **parts, int part_count, int line);

// Statements
Stmt *stmt_expr(Expr *expr, int line);
Stmt *stmt_print(Expr *expr, int line);
Stmt *stmt_let(char *name, char *type_name, Expr *initializer, int line);
Stmt *stmt_assign(char *name, Expr *value, int line);
Stmt *stmt_index_assign(Expr *array, Expr *index, Expr *value, int line);
Stmt *stmt_field_assign(Expr *object, char *field_name, Expr *value, int line);
Stmt *stmt_block(Stmt **statements, int count, int line);
Stmt *stmt_if(Expr *condition, Stmt *then_branch, Stmt *else_branch, int line);
Stmt *stmt_while(Expr *condition, Stmt *body, int line);
Stmt *stmt_for_range(char *var_name, Expr *start, Expr *end, Stmt *body, int line);
Stmt *stmt_for_array(char *var_name, Expr *iterable, Stmt *body, int line);
Stmt *stmt_return(Expr *value, int line);
Stmt *stmt_break(int line);
Stmt *stmt_continue(int line);
Stmt *stmt_destructure(char **field_names, int field_count, Expr *initializer, int line);

// Structs
StructDef *struct_def_new(char *name, char **field_names, char **field_types, int field_count);

// Enums
EnumDef *enum_def_new(char *name, char **variant_names, int *variant_has_data,
                      char **variant_payload_types, int variant_count);

// Program
Function *function_new(char *name, char **params, char **param_types, int param_count,
                       char *return_type, char **effects, int effect_count, Stmt *body);
Program *program_new(void);
void program_add_struct(Program *prog, StructDef *sd);
void program_add_enum(Program *prog, EnumDef *ed);
void program_add_function(Program *prog, Function *fn);

// Memory cleanup
void expr_free(Expr *expr);
void stmt_free(Stmt *stmt);
void struct_def_free(StructDef *sd);
void enum_def_free(EnumDef *ed);
void function_free(Function *fn);
void program_free(Program *prog);

// Debug printing
void ast_print_expr(Expr *expr, int indent);
void ast_print_stmt(Stmt *stmt, int indent);
void ast_print_program(Program *prog);

#endif // TEDDY_AST_H
