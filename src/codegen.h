/*
 * Teddy Code Generator
 * Generates x86-64 assembly from AST.
 */

#ifndef TEDDY_CODEGEN_H
#define TEDDY_CODEGEN_H

#include <stdio.h>
#include "ast.h"

typedef struct {
    FILE *out;           // Output file
    Program *prog;       // Reference to the program AST (for function lookup)
    int label_count;     // For generating unique labels
    int stack_offset;    // Current stack offset for locals
    
    // Simple symbol table for local variables
    struct {
        char *name;
        int offset;        // Offset from rbp
        char *struct_type; // Struct type name (NULL if not a struct)
    } locals[256];
    int local_count;
    
    // Scope stack - stores local_count at each scope entry
    int scope_stack[64];
    int scope_depth;
    
    // Loop label stack for break/continue
    struct {
        int start_label;   // label at top of loop (continue target)
        int end_label;     // label after loop (break target)
    } loop_stack[32];
    int loop_depth;
    
    // String literals table
    struct {
        char *value;
        int length;
        int label;       // .str0, .str1, etc.
    } strings[256];
    int string_count;
    
    // Struct definitions for field offset lookup
    struct {
        char *name;
        char **field_names;
        int field_count;
    } structs[64];
    int struct_count;
    
    // Deferred closure functions to emit after current function
    struct {
        char *name;            // Generated function name
        char **params;         // env + user params
        int param_count;
        Expr *body_expr;       // One of these is set
        Stmt *body_block;
        char **captures;       // Captured variable names (for env layout)
        int capture_count;
    } closures[64];
    int closure_count;
    
    // Enum definitions for variant value lookup
    struct {
        char *name;
        char **variant_names;
        int *variant_has_data;  // 1 if variant carries data, 0 if not
        int variant_count;
    } enums[64];
    int enum_count;
} CodeGen;

void codegen_init(CodeGen *gen, FILE *out);
void codegen_program(CodeGen *gen, Program *prog);

#endif // TEDDY_CODEGEN_H
