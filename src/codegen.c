/*
 * Teddy Code Generator Implementation
 * 
 * Generates ARM64 (AArch64) assembly.
 * 
 * Calling convention (AAPCS64):
 *   - Args: x0-x7 (then stack)
 *   - Return value: x0
 *   - Callee-saved: x19-x28, x29 (fp), x30 (lr)
 *   - Caller-saved: x0-x18
 * 
 * Our approach:
 *   - Expressions evaluate to x0
 *   - Local variables live on stack (negative offsets from fp/x29)
 *   - We use the stack for intermediate values in binary ops
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "codegen.h"

// ============ HELPERS ============

static void emit(CodeGen *gen, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(gen->out, "    ");
    vfprintf(gen->out, fmt, args);
    fprintf(gen->out, "\n");
    va_end(args);
}

static void emit_label(CodeGen *gen, const char *label) {
    fprintf(gen->out, "%s:\n", label);
    (void)emit_label; // suppress unused warning
}

static void emit_raw(CodeGen *gen, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(gen->out, fmt, args);
    fprintf(gen->out, "\n");
    va_end(args);
}

static int new_label(CodeGen *gen) {
    return gen->label_count++;
}

static int find_local(CodeGen *gen, const char *name) {
    // Search backwards to find most recent (innermost scope) variable first
    for (int i = gen->local_count - 1; i >= 0; i--) {
        if (strcmp(gen->locals[i].name, name) == 0) {
            return gen->locals[i].offset;
        }
    }
    return 0; // Not found
}

static char *my_strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

static int add_local(CodeGen *gen, const char *name) {
    if (gen->local_count >= 256) {
        fprintf(stderr, "Error: too many local variables (max 256)\n");
        exit(1);
    }
    gen->stack_offset -= 8;  // Each local takes 8 bytes
    gen->locals[gen->local_count].name = my_strdup(name);
    gen->locals[gen->local_count].offset = gen->stack_offset;
    gen->locals[gen->local_count].struct_type = NULL;
    gen->local_count++;
    return gen->stack_offset;
}

// Set the struct type for the most recently added local variable
static void set_local_type(CodeGen *gen, const char *struct_type) {
    if (gen->local_count > 0 && struct_type) {
        free(gen->locals[gen->local_count - 1].struct_type);
        gen->locals[gen->local_count - 1].struct_type = my_strdup(struct_type);
    }
}

// Find the struct type of a local variable (returns NULL if not a struct)
static const char *find_local_type(CodeGen *gen, const char *name) {
    for (int i = gen->local_count - 1; i >= 0; i--) {
        if (strcmp(gen->locals[i].name, name) == 0) {
            return gen->locals[i].struct_type;
        }
    }
    return NULL;
}

// Emit load from [x29, #offset] with large offset support
// Uses x9 as scratch register
static void emit_ldr_fp(CodeGen *gen, const char *reg, int offset) {
    if (offset >= -255 && offset <= 255) {
        // Use simple ldur for small offsets
        emit(gen, "ldur %s, [x29, #%d]", reg, offset);
    } else {
        // For large offsets, use: mov x9, #offset; ldr reg, [x29, x9]
        emit(gen, "mov x9, #%d", offset);
        emit(gen, "ldr %s, [x29, x9]", reg);
    }
}

// Emit store to [x29, #offset] with large offset support
// Uses x9 as scratch register
static void emit_str_fp(CodeGen *gen, const char *reg, int offset) {
    if (offset >= -255 && offset <= 255) {
        // Use simple stur for small offsets
        emit(gen, "stur %s, [x29, #%d]", reg, offset);
    } else {
        // For large offsets, use: mov x9, #offset; str reg, [x29, x9]
        emit(gen, "mov x9, #%d", offset);
        emit(gen, "str %s, [x29, x9]", reg);
    }
}

// Emit sub x0, x29, #offset (for getting address of stack-allocated struct)
static void emit_sub_fp(CodeGen *gen, int neg_offset) {
    // neg_offset is the negative offset, so we sub by -neg_offset to get address
    int amount = -neg_offset;
    if (amount <= 4095) {
        emit(gen, "sub x0, x29, #%d", amount);
    } else {
        emit(gen, "mov x9, #%d", neg_offset);
        emit(gen, "add x0, x29, x9");
    }
}

static void clear_locals(CodeGen *gen) {
    for (int i = 0; i < gen->local_count; i++) {
        free(gen->locals[i].name);
        free(gen->locals[i].struct_type);
    }
    gen->local_count = 0;
    gen->stack_offset = 0;
    gen->scope_depth = 0;
}

static void push_scope(CodeGen *gen) {
    if (gen->scope_depth >= 64) {
        fprintf(stderr, "Error: too many nested scopes (max 64)\n");
        exit(1);
    }
    gen->scope_stack[gen->scope_depth] = gen->local_count;
    gen->scope_depth++;
}

static void pop_scope(CodeGen *gen) {
    gen->scope_depth--;
    int old_count = gen->scope_stack[gen->scope_depth];
    // Free variable names that are going out of scope
    for (int i = old_count; i < gen->local_count; i++) {
        free(gen->locals[i].name);
        free(gen->locals[i].struct_type);
    }
    gen->local_count = old_count;
    // Note: we don't restore stack_offset because the stack space is still allocated
}

// Find field offset within a struct (returns byte offset, or -1 if not found)
static int find_field_offset(CodeGen *gen, const char *struct_name, const char *field_name) {
    for (int i = 0; i < gen->struct_count; i++) {
        if (strcmp(gen->structs[i].name, struct_name) == 0) {
            for (int j = 0; j < gen->structs[i].field_count; j++) {
                if (strcmp(gen->structs[i].field_names[j], field_name) == 0) {
                    return j * 8;  // Each field is 8 bytes
                }
            }
        }
    }
    return -1;
}

// Get struct info by name
static int find_struct(CodeGen *gen, const char *name) {
    for (int i = 0; i < gen->struct_count; i++) {
        if (strcmp(gen->structs[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

// Get field index within struct
static int get_field_index(CodeGen *gen, const char *struct_name, const char *field_name) {
    int si = find_struct(gen, struct_name);
    if (si < 0) return -1;
    for (int j = 0; j < gen->structs[si].field_count; j++) {
        if (strcmp(gen->structs[si].field_names[j], field_name) == 0) {
            return j;
        }
    }
    return -1;
}

// Find enum by name, return index or -1
static int find_enum(CodeGen *gen, const char *name) {
    for (int i = 0; i < gen->enum_count; i++) {
        if (strcmp(gen->enums[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

// Get variant value (index) within an enum
static int get_variant_value(CodeGen *gen, const char *enum_name, const char *variant_name) {
    int ei = find_enum(gen, enum_name);
    if (ei < 0) return -1;
    for (int j = 0; j < gen->enums[ei].variant_count; j++) {
        if (strcmp(gen->enums[ei].variant_names[j], variant_name) == 0) {
            return j;  // Variant value is its index (0, 1, 2, ...)
        }
    }
    return -1;
}

// Check if a variant has associated data
static int variant_has_data(CodeGen *gen, const char *enum_name, const char *variant_name) {
    int ei = find_enum(gen, enum_name);
    if (ei < 0) return 0;
    for (int j = 0; j < gen->enums[ei].variant_count; j++) {
        if (strcmp(gen->enums[ei].variant_names[j], variant_name) == 0) {
            return gen->enums[ei].variant_has_data[j];
        }
    }
    return 0;
}

// Check if any variant in an enum has data (making it a "fat" enum)
static int enum_has_any_data(CodeGen *gen, const char *enum_name) {
    int ei = find_enum(gen, enum_name);
    if (ei < 0) return 0;
    for (int j = 0; j < gen->enums[ei].variant_count; j++) {
        if (gen->enums[ei].variant_has_data[j]) {
            return 1;
        }
    }
    return 0;
}

// Find enum name for a variant (searches all enums)
static const char* find_enum_for_variant(CodeGen *gen, const char *variant_name) {
    for (int e = 0; e < gen->enum_count; e++) {
        for (int v = 0; v < gen->enums[e].variant_count; v++) {
            if (strcmp(gen->enums[e].variant_names[v], variant_name) == 0) {
                return gen->enums[e].name;
            }
        }
    }
    return NULL;
}

// ============ TYPE INFERENCE ============

// Infer the struct type of an expression (returns NULL if not a struct)
static const char *infer_struct_type(CodeGen *gen, Expr *expr) {
    switch (expr->type) {
        case EXPR_STRUCT_LITERAL:
            return expr->as.struct_literal.struct_name;
        case EXPR_VARIABLE:
            return find_local_type(gen, expr->as.variable.name);
        case EXPR_METHOD_CALL:
            // Method call on a struct - the receiver type is the struct type
            return infer_struct_type(gen, expr->as.method_call.object);
        case EXPR_CALL:
            // Function return types aren't tracked yet - return NULL
            return NULL;
        default:
            return NULL;
    }
}

// ============ CAPTURE ANALYSIS ============

// Collect free variables in an expression that aren't in the given param list
static void collect_captures_expr(Expr *expr, char **params, int param_count,
                                   char ***captures, int *capture_count, int *capture_cap);
static void collect_captures_stmt(Stmt *stmt, char **params, int param_count,
                                   char ***captures, int *capture_count, int *capture_cap);

static int is_param(const char *name, char **params, int param_count) {
    for (int i = 0; i < param_count; i++) {
        if (strcmp(name, params[i]) == 0) return 1;
    }
    return 0;
}

static int is_captured(const char *name, char **captures, int count) {
    for (int i = 0; i < count; i++) {
        if (strcmp(name, captures[i]) == 0) return 1;
    }
    return 0;
}

static void maybe_add_capture(const char *name, char **params, int param_count,
                               char ***captures, int *capture_count, int *capture_cap) {
    if (is_param(name, params, param_count)) return;
    if (is_captured(name, *captures, *capture_count)) return;
    // Skip builtins and known globals
    if (strcmp(name, "len") == 0 || strcmp(name, "char_at") == 0 ||
        strcmp(name, "alloc") == 0 || strcmp(name, "free") == 0 ||
        strcmp(name, "peek") == 0 || strcmp(name, "poke") == 0 ||
        strcmp(name, "str_copy") == 0 || strcmp(name, "int_to_str") == 0 ||
        strcmp(name, "read_file") == 0 || strcmp(name, "write_file") == 0 ||
        strcmp(name, "exit") == 0 || strcmp(name, "str_len") == 0 ||
        strcmp(name, "put_char") == 0 || strcmp(name, "put_str") == 0 ||
        strcmp(name, "put_int") == 0 || strcmp(name, "print") == 0 ||
        strcmp(name, "map") == 0 || strcmp(name, "filter") == 0 ||
        strcmp(name, "reduce") == 0) return;
    
    if (*capture_count >= *capture_cap) {
        *capture_cap = *capture_cap == 0 ? 4 : *capture_cap * 2;
        *captures = realloc(*captures, sizeof(char*) * *capture_cap);
    }
    (*captures)[*capture_count] = my_strdup(name);
    (*capture_count)++;
}

static void collect_captures_expr(Expr *expr, char **params, int param_count,
                                   char ***captures, int *capture_count, int *capture_cap) {
    if (!expr) return;
    switch (expr->type) {
        case EXPR_VARIABLE:
            maybe_add_capture(expr->as.variable.name, params, param_count,
                             captures, capture_count, capture_cap);
            break;
        case EXPR_BINARY:
            collect_captures_expr(expr->as.binary.left, params, param_count, captures, capture_count, capture_cap);
            collect_captures_expr(expr->as.binary.right, params, param_count, captures, capture_count, capture_cap);
            break;
        case EXPR_UNARY:
            collect_captures_expr(expr->as.unary.operand, params, param_count, captures, capture_count, capture_cap);
            break;
        case EXPR_CALL:
            // Don't capture the function name itself (it's a global function)
            for (int i = 0; i < expr->as.call.arg_count; i++) {
                collect_captures_expr(expr->as.call.args[i], params, param_count, captures, capture_count, capture_cap);
            }
            break;
        case EXPR_INDEX:
            collect_captures_expr(expr->as.index.array, params, param_count, captures, capture_count, capture_cap);
            collect_captures_expr(expr->as.index.index, params, param_count, captures, capture_count, capture_cap);
            break;
        case EXPR_FIELD_ACCESS:
            collect_captures_expr(expr->as.field_access.object, params, param_count, captures, capture_count, capture_cap);
            break;
        case EXPR_METHOD_CALL:
            collect_captures_expr(expr->as.method_call.object, params, param_count, captures, capture_count, capture_cap);
            for (int i = 0; i < expr->as.method_call.arg_count; i++) {
                collect_captures_expr(expr->as.method_call.args[i], params, param_count, captures, capture_count, capture_cap);
            }
            break;
        case EXPR_STRUCT_LITERAL:
            for (int i = 0; i < expr->as.struct_literal.field_count; i++) {
                collect_captures_expr(expr->as.struct_literal.field_values[i], params, param_count, captures, capture_count, capture_cap);
            }
            break;
        case EXPR_ARRAY_LITERAL:
            for (int i = 0; i < expr->as.array_literal.count; i++) {
                collect_captures_expr(expr->as.array_literal.elements[i], params, param_count, captures, capture_count, capture_cap);
            }
            break;
        case EXPR_MATCH:
            collect_captures_expr(expr->as.match.value, params, param_count, captures, capture_count, capture_cap);
            for (int i = 0; i < expr->as.match.arm_count; i++) {
                collect_captures_expr(expr->as.match.arms[i]->body, params, param_count, captures, capture_count, capture_cap);
            }
            break;
        case EXPR_ENUM_VARIANT:
            if (expr->as.enum_variant.payload)
                collect_captures_expr(expr->as.enum_variant.payload, params, param_count, captures, capture_count, capture_cap);
            break;
        case EXPR_CLOSURE:
            // Nested closure - its own params shadow, but it may capture from our scope
            // For now, don't recurse into nested closures (they'll do their own capture)
            break;
        default:
            break;
    }
}

static void collect_captures_stmt(Stmt *stmt, char **params, int param_count,
                                   char ***captures, int *capture_count, int *capture_cap) {
    if (!stmt) return;
    switch (stmt->type) {
        case STMT_EXPR:
            collect_captures_expr(stmt->as.expr.expr, params, param_count, captures, capture_count, capture_cap);
            break;
        case STMT_PRINT:
            collect_captures_expr(stmt->as.print.expr, params, param_count, captures, capture_count, capture_cap);
            break;
        case STMT_LET:
            collect_captures_expr(stmt->as.let.initializer, params, param_count, captures, capture_count, capture_cap);
            // The new variable is now a local, add to params so it's not captured
            // We need to extend our param list temporarily... but that's complex.
            // For simplicity: let-bound vars inside the closure body won't be captured
            // (they're defined inside, not outside)
            break;
        case STMT_ASSIGN:
            maybe_add_capture(stmt->as.assign.name, params, param_count, captures, capture_count, capture_cap);
            collect_captures_expr(stmt->as.assign.value, params, param_count, captures, capture_count, capture_cap);
            break;
        case STMT_IF:
            collect_captures_expr(stmt->as.if_stmt.condition, params, param_count, captures, capture_count, capture_cap);
            collect_captures_stmt(stmt->as.if_stmt.then_branch, params, param_count, captures, capture_count, capture_cap);
            if (stmt->as.if_stmt.else_branch)
                collect_captures_stmt(stmt->as.if_stmt.else_branch, params, param_count, captures, capture_count, capture_cap);
            break;
        case STMT_WHILE:
            collect_captures_expr(stmt->as.while_stmt.condition, params, param_count, captures, capture_count, capture_cap);
            collect_captures_stmt(stmt->as.while_stmt.body, params, param_count, captures, capture_count, capture_cap);
            break;
        case STMT_FOR:
            if (stmt->as.for_stmt.start) {
                collect_captures_expr(stmt->as.for_stmt.start, params, param_count, captures, capture_count, capture_cap);
                collect_captures_expr(stmt->as.for_stmt.end, params, param_count, captures, capture_count, capture_cap);
            } else {
                collect_captures_expr(stmt->as.for_stmt.iterable, params, param_count, captures, capture_count, capture_cap);
            }
            collect_captures_stmt(stmt->as.for_stmt.body, params, param_count, captures, capture_count, capture_cap);
            break;
        case STMT_RETURN:
            if (stmt->as.return_stmt.value)
                collect_captures_expr(stmt->as.return_stmt.value, params, param_count, captures, capture_count, capture_cap);
            break;
        case STMT_BLOCK:
            for (int i = 0; i < stmt->as.block.count; i++) {
                collect_captures_stmt(stmt->as.block.statements[i], params, param_count, captures, capture_count, capture_cap);
            }
            break;
        case STMT_INDEX_ASSIGN:
            collect_captures_expr(stmt->as.index_assign.array, params, param_count, captures, capture_count, capture_cap);
            collect_captures_expr(stmt->as.index_assign.index, params, param_count, captures, capture_count, capture_cap);
            collect_captures_expr(stmt->as.index_assign.value, params, param_count, captures, capture_count, capture_cap);
            break;
        case STMT_FIELD_ASSIGN:
            collect_captures_expr(stmt->as.field_assign.object, params, param_count, captures, capture_count, capture_cap);
            collect_captures_expr(stmt->as.field_assign.value, params, param_count, captures, capture_count, capture_cap);
            break;
        default:
            break;
    }
}

// ============ EXPRESSION CODEGEN ============

static void codegen_expr(CodeGen *gen, Expr *expr);

static void codegen_int_literal(CodeGen *gen, Expr *expr) {
    long val = expr->as.int_literal.value;
    if (val >= 0 && val < 65536) {
        emit(gen, "mov x0, #%ld", val);
    } else if (val >= -65536 && val < 0) {
        emit(gen, "mov x0, #%ld", val);
    } else {
        // Load large constant in parts
        emit(gen, "mov x0, #%ld", val & 0xFFFF);
        if ((val >> 16) & 0xFFFF) {
            emit(gen, "movk x0, #%ld, lsl #16", (val >> 16) & 0xFFFF);
        }
        if ((val >> 32) & 0xFFFF) {
            emit(gen, "movk x0, #%ld, lsl #32", (val >> 32) & 0xFFFF);
        }
        if ((val >> 48) & 0xFFFF) {
            emit(gen, "movk x0, #%ld, lsl #48", (val >> 48) & 0xFFFF);
        }
    }
}

static void codegen_bool_literal(CodeGen *gen, Expr *expr) {
    emit(gen, "mov x0, #%d", expr->as.bool_literal.value);
}

static int add_string(CodeGen *gen, const char *value, int length) {
    // Check if string already exists
    for (int i = 0; i < gen->string_count; i++) {
        if (gen->strings[i].length == length &&
            memcmp(gen->strings[i].value, value, length) == 0) {
            return gen->strings[i].label;
        }
    }
    
    // Add new string
    if (gen->string_count >= 256) {
        fprintf(stderr, "Error: too many string literals (max 256)\n");
        exit(1);
    }
    int label = gen->string_count;
    gen->strings[gen->string_count].value = my_strdup(value);
    gen->strings[gen->string_count].length = length;
    gen->strings[gen->string_count].label = label;
    gen->string_count++;
    return label;
}

static void codegen_string_literal(CodeGen *gen, Expr *expr) {
    int label = add_string(gen, expr->as.string_literal.value, 
                           expr->as.string_literal.length);
    emit(gen, "adrp x0, .str%d", label);
    emit(gen, "add x0, x0, :lo12:.str%d", label);
}

static void codegen_array_literal(CodeGen *gen, Expr *expr) {
    ArrayLiteralExpr *arr = &expr->as.array_literal;
    
    // Layout: [length][elem0][elem1]...
    // Allocate space: 8 bytes for length + 8 bytes per element
    int size = (arr->count + 1) * 8;
    emit(gen, "sub sp, sp, #%d", (size + 15) & ~15);  // 16-byte align
    
    // Store length at offset 0
    emit(gen, "mov x0, #%d", arr->count);
    emit(gen, "str x0, [sp]");
    
    // Store each element starting at offset 8
    for (int i = 0; i < arr->count; i++) {
        codegen_expr(gen, arr->elements[i]);
        emit(gen, "str x0, [sp, #%d]", (i + 1) * 8);
    }
    
    // Return pointer to array start (at length field)
    emit(gen, "mov x0, sp");
}

static void codegen_index(CodeGen *gen, Expr *expr) {
    IndexExpr *idx = &expr->as.index;
    
    // Evaluate array (get base pointer)
    codegen_expr(gen, idx->array);
    emit(gen, "str x0, [sp, #-16]!");  // push array ptr
    
    // Evaluate index
    codegen_expr(gen, idx->index);
    
    // Bounds check: x0 = index, load length from array
    emit(gen, "ldr x1, [sp]");         // peek array ptr (don't pop yet)
    emit(gen, "ldr x1, [x1]");         // x1 = array length
    emit(gen, "bl _bounds_check");     // check 0 <= x0 < x1
    
    // Load: base + 8 + index * 8 (skip length field)
    emit(gen, "ldr x1, [sp], #16");    // pop array ptr into x1
    emit(gen, "add x1, x1, #8");       // skip length field
    emit(gen, "lsl x0, x0, #3");       // x0 = index * 8
    emit(gen, "add x0, x1, x0");       // x0 = base + offset
    emit(gen, "ldr x0, [x0]");         // load value
}

static void codegen_struct_literal(CodeGen *gen, Expr *expr) {
    StructLiteralExpr *sl = &expr->as.struct_literal;
    
    // Find struct definition to know the field order
    int si = find_struct(gen, sl->struct_name);
    if (si < 0) {
        fprintf(stderr, "Unknown struct: %s\n", sl->struct_name);
        return;
    }
    
    int field_count = gen->structs[si].field_count;
    int size = field_count * 8;
    
    // Allocate space in the local variable area (negative offsets from x29)
    // This ensures the struct won't be clobbered by function calls
    int struct_offset = gen->stack_offset - size;
    // Align to 8 bytes
    struct_offset = struct_offset & ~7;
    gen->stack_offset = struct_offset;
    
    // Store each field value at the correct offset
    // Fields in struct literal may be in different order than definition
    for (int i = 0; i < sl->field_count; i++) {
        // Find the offset for this field name
        int field_idx = get_field_index(gen, sl->struct_name, sl->field_names[i]);
        if (field_idx < 0) {
            fprintf(stderr, "Unknown field '%s' in struct '%s'\n", 
                    sl->field_names[i], sl->struct_name);
            continue;
        }
        
        codegen_expr(gen, sl->field_values[i]);
        emit_str_fp(gen, "x0", struct_offset + field_idx * 8);
    }
    
    // Return pointer to struct (address = x29 + offset)
    // Since struct_offset is negative, use sub with positive value
    emit_sub_fp(gen, struct_offset);
}

static void codegen_field_access(CodeGen *gen, Expr *expr) {
    FieldAccessExpr *fa = &expr->as.field_access;
    
    // Try to infer the struct type from the object expression
    const char *stype = infer_struct_type(gen, fa->object);
    
    // Evaluate object (get struct pointer)
    codegen_expr(gen, fa->object);
    
    int offset = -1;
    
    if (stype) {
        // We know the struct type - look up the field in that specific struct
        offset = find_field_offset(gen, stype, fa->field_name);
    }
    
    if (offset < 0) {
        // Fallback: search all structs (old behavior for untyped cases)
        for (int i = 0; i < gen->struct_count && offset < 0; i++) {
            for (int j = 0; j < gen->structs[i].field_count; j++) {
                if (strcmp(gen->structs[i].field_names[j], fa->field_name) == 0) {
                    offset = j * 8;
                    break;
                }
            }
        }
    }
    
    if (offset < 0) {
        fprintf(stderr, "Unknown field: %s\n", fa->field_name);
        offset = 0;
    }
    
    // Load field value: x0 = base ptr, load at offset
    emit(gen, "ldr x0, [x0, #%d]", offset);
}

static void codegen_variable(CodeGen *gen, Expr *expr) {
    int offset = find_local(gen, expr->as.variable.name);
    emit_ldr_fp(gen, "x0", offset);
}

static void codegen_enum_variant(CodeGen *gen, Expr *expr) {
    EnumVariantExpr *ev = &expr->as.enum_variant;
    int tag = get_variant_value(gen, ev->enum_name, ev->variant_name);
    if (tag < 0) {
        fprintf(stderr, "Unknown enum variant: %s::%s\n", ev->enum_name, ev->variant_name);
        tag = 0;
    }
    
    // Check if this enum type has any variants with data
    int is_fat_enum = enum_has_any_data(gen, ev->enum_name);
    
    if (is_fat_enum) {
        // "Fat" enum: allocate [tag][payload] in local area
        // Even if this specific variant has no payload, we use consistent layout
        int enum_offset = gen->stack_offset - 16;
        enum_offset = enum_offset & ~7;  // align
        gen->stack_offset = enum_offset;
        
        if (ev->payload) {
            // Evaluate and store payload first (so it doesn't clobber our tag)
            codegen_expr(gen, ev->payload);
            emit_str_fp(gen, "x0", enum_offset + 8);  // store payload at offset 8
        } else {
            // No payload - store 0 as placeholder
            emit_str_fp(gen, "xzr", enum_offset + 8);
        }
        
        // Store tag
        emit(gen, "mov x0, #%d", tag);
        emit_str_fp(gen, "x0", enum_offset);  // store tag at offset 0
        
        // Return pointer to the enum value
        emit_sub_fp(gen, enum_offset);
    } else {
        // Simple enum (no variants have data): just the tag value
        emit(gen, "mov x0, #%d", tag);
    }
}

static void codegen_match(CodeGen *gen, Expr *expr) {
    MatchExpr *m = &expr->as.match;
    
    // Evaluate the value being matched
    codegen_expr(gen, m->value);
    emit(gen, "str x0, [sp, #-16]!");  // push match value (either int or pointer)
    
    int end_label = new_label(gen);
    
    // Determine if this is a fat enum (any variant has data)
    // We need to find the enum name first
    const char *enum_name = NULL;
    for (int i = 0; i < m->arm_count && !enum_name; i++) {
        if (m->arms[i]->enum_name) {
            enum_name = m->arms[i]->enum_name;
        } else {
            enum_name = find_enum_for_variant(gen, m->arms[i]->variant_name);
        }
    }
    int is_fat_enum = enum_name ? enum_has_any_data(gen, enum_name) : 0;
    
    for (int i = 0; i < m->arm_count; i++) {
        MatchArm *arm = m->arms[i];
        int next_arm_label = new_label(gen);
        
        // Get the variant tag value to compare against
        int variant_tag = -1;
        const char *arm_enum = arm->enum_name ? arm->enum_name : enum_name;
        if (arm_enum) {
            variant_tag = get_variant_value(gen, arm_enum, arm->variant_name);
        }
        
        if (variant_tag < 0) {
            fprintf(stderr, "Unknown variant in match: %s\n", arm->variant_name);
            variant_tag = 0;
        }
        
        // Load match value and compare
        emit(gen, "ldr x0, [sp]");
        if (is_fat_enum) {
            // Fat enum: x0 is pointer, load tag from [x0]
            emit(gen, "ldr x1, [x0]");  // x1 = tag
            emit(gen, "cmp x1, #%d", variant_tag);
        } else {
            // Simple enum: x0 is the tag directly
            emit(gen, "cmp x0, #%d", variant_tag);
        }
        emit(gen, "b.ne .L%d", next_arm_label);
        
        // If there's a binding, extract the payload and create a local variable
        if (arm->binding_name && is_fat_enum) {
            // x0 still has the enum pointer from above
            emit(gen, "ldr x0, [sp]");        // reload enum pointer
            emit(gen, "ldr x0, [x0, #8]");    // load payload from offset 8
            int offset = add_local(gen, arm->binding_name);
            emit_str_fp(gen, "x0", offset);
        }
        
        // Execute body
        codegen_expr(gen, arm->body);
        emit(gen, "b .L%d", end_label);
        
        emit_raw(gen, ".L%d:", next_arm_label);
    }
    
    // If no arm matched (shouldn't happen with exhaustive match), return 0
    emit(gen, "mov x0, #0");
    
    emit_raw(gen, ".L%d:", end_label);
    emit(gen, "add sp, sp, #16");  // pop match value
}

static void codegen_binary(CodeGen *gen, Expr *expr) {
    BinaryExpr *bin = &expr->as.binary;
    
    // Evaluate left, push result
    codegen_expr(gen, bin->left);
    emit(gen, "str x0, [sp, #-16]!");  // push x0
    
    // Evaluate right (result in x0)
    codegen_expr(gen, bin->right);
    
    // Pop left into x1
    emit(gen, "ldr x1, [sp], #16");  // pop into x1
    
    // Now: x1 = left, x0 = right
    
    switch (bin->op) {
        case OP_ADD:
            emit(gen, "add x0, x1, x0");
            break;
        case OP_SUB:
            emit(gen, "sub x0, x1, x0");
            break;
        case OP_MUL:
            emit(gen, "mul x0, x1, x0");
            break;
        case OP_DIV:
            emit(gen, "sdiv x0, x1, x0");
            break;
        case OP_MOD:
            emit(gen, "sdiv x2, x1, x0");  // x2 = x1 / x0
            emit(gen, "msub x0, x2, x0, x1");  // x0 = x1 - x2*x0
            break;
        case OP_EQ:
            emit(gen, "cmp x1, x0");
            emit(gen, "cset x0, eq");
            break;
        case OP_NE:
            emit(gen, "cmp x1, x0");
            emit(gen, "cset x0, ne");
            break;
        case OP_LT:
            emit(gen, "cmp x1, x0");
            emit(gen, "cset x0, lt");
            break;
        case OP_LE:
            emit(gen, "cmp x1, x0");
            emit(gen, "cset x0, le");
            break;
        case OP_GT:
            emit(gen, "cmp x1, x0");
            emit(gen, "cset x0, gt");
            break;
        case OP_GE:
            emit(gen, "cmp x1, x0");
            emit(gen, "cset x0, ge");
            break;
        case OP_AND:
            emit(gen, "cmp x1, #0");
            emit(gen, "cset x1, ne");
            emit(gen, "cmp x0, #0");
            emit(gen, "cset x0, ne");
            emit(gen, "and x0, x1, x0");
            break;
        case OP_OR:
            emit(gen, "orr x0, x1, x0");
            emit(gen, "cmp x0, #0");
            emit(gen, "cset x0, ne");
            break;
        default:
            fprintf(stderr, "Unknown binary op\n");
            break;
    }
}

static void codegen_unary(CodeGen *gen, Expr *expr) {
    UnaryExpr *un = &expr->as.unary;
    
    codegen_expr(gen, un->operand);
    
    switch (un->op) {
        case OP_NEG:
            emit(gen, "neg x0, x0");
            break;
        case OP_NOT:
            emit(gen, "cmp x0, #0");
            emit(gen, "cset x0, eq");
            break;
        default:
            break;
    }
}

static void codegen_call(CodeGen *gen, Expr *expr) {
    CallExpr *call = &expr->as.call;
    
    // Builtin: len(array) - return length stored at offset 0
    if (strcmp(call->name, "len") == 0 && call->arg_count == 1) {
        codegen_expr(gen, call->args[0]);  // array ptr in x0
        emit(gen, "ldr x0, [x0]");         // load length from offset 0
        return;
    }
    
    // Builtin: char_at(string, index) - return character at index
    if (strcmp(call->name, "char_at") == 0 && call->arg_count == 2) {
        codegen_expr(gen, call->args[0]);  // string ptr in x0
        emit(gen, "str x0, [sp, #-16]!");  // push string ptr
        codegen_expr(gen, call->args[1]);  // index in x0
        emit(gen, "ldr x1, [sp], #16");    // pop string ptr into x1
        emit(gen, "ldrb w0, [x1, x0]");    // load byte at string[index]
        return;
    }
    
    // Builtin: str_len(string) - return string length (counts until null byte)
    if (strcmp(call->name, "str_len") == 0 && call->arg_count == 1) {
        codegen_expr(gen, call->args[0]);  // string ptr in x0
        emit(gen, "mov x1, x0");           // x1 = start ptr
        int loop_label = new_label(gen);
        int end_label = new_label(gen);
        emit_raw(gen, ".L%d:", loop_label);
        emit(gen, "ldrb w2, [x1], #1");    // load byte, increment ptr
        emit(gen, "cbnz w2, .L%d", loop_label);  // if not null, continue
        emit(gen, "sub x0, x1, x0");       // length = end - start
        emit(gen, "sub x0, x0, #1");       // subtract 1 (we counted the null)
        return;
    }
    
    // Builtin: put_char(c) - write single character to stdout
    if (strcmp(call->name, "put_char") == 0 && call->arg_count == 1) {
        codegen_expr(gen, call->args[0]);  // char in x0
        emit(gen, "strb w0, [sp, #-16]!"); // store byte on stack
        emit(gen, "mov x0, #1");           // stdout
        emit(gen, "mov x1, sp");           // buffer
        emit(gen, "mov x2, #1");           // length
        emit(gen, "mov x8, #64");          // sys_write
        emit(gen, "svc #0");
        emit(gen, "add sp, sp, #16");
        return;
    }
    
    // Builtin: put_str(ptr, len) - write string to stdout (no newline)
    if (strcmp(call->name, "put_str") == 0 && call->arg_count == 2) {
        codegen_expr(gen, call->args[0]);  // string ptr
        emit(gen, "str x0, [sp, #-16]!");
        codegen_expr(gen, call->args[1]);  // length
        emit(gen, "mov x2, x0");           // length in x2
        emit(gen, "ldr x1, [sp], #16");    // ptr in x1
        emit(gen, "mov x0, #1");           // stdout
        emit(gen, "mov x8, #64");          // sys_write
        emit(gen, "svc #0");
        return;
    }
    
    // Builtin: put_int(n) - write integer to stdout (no newline)
    if (strcmp(call->name, "put_int") == 0 && call->arg_count == 1) {
        codegen_expr(gen, call->args[0]);
        emit(gen, "bl _put_int_no_newline");
        return;
    }
    
    // Builtin: read_file(filename) - returns pointer to file content (with length prefix)
    if (strcmp(call->name, "read_file") == 0 && call->arg_count == 1) {
        codegen_expr(gen, call->args[0]);  // filename ptr in x0
        emit(gen, "bl _builtin_read_file");
        return;
    }
    
    // Builtin: write_file(filename, content, length)
    if (strcmp(call->name, "write_file") == 0 && call->arg_count == 3) {
        codegen_expr(gen, call->args[0]);
        emit(gen, "str x0, [sp, #-16]!");
        codegen_expr(gen, call->args[1]);
        emit(gen, "str x0, [sp, #-16]!");
        codegen_expr(gen, call->args[2]);
        emit(gen, "mov x2, x0");           // length
        emit(gen, "ldr x1, [sp], #16");    // content
        emit(gen, "ldr x0, [sp], #16");    // filename
        emit(gen, "bl _builtin_write_file");
        return;
    }
    
    // Builtin: exit(code)
    if (strcmp(call->name, "exit") == 0 && call->arg_count == 1) {
        codegen_expr(gen, call->args[0]);
        emit(gen, "mov x8, #93");
        emit(gen, "svc #0");
        return;
    }
    
    // Builtin: alloc(size) - allocate memory using mmap
    if (strcmp(call->name, "alloc") == 0 && call->arg_count == 1) {
        codegen_expr(gen, call->args[0]);  // size in x0
        emit(gen, "bl _builtin_alloc");
        return;
    }
    
    // Builtin: free(ptr, size) - free memory using munmap
    if (strcmp(call->name, "free") == 0 && call->arg_count == 2) {
        codegen_expr(gen, call->args[0]);  // ptr
        emit(gen, "str x0, [sp, #-16]!");
        codegen_expr(gen, call->args[1]);  // size
        emit(gen, "mov x1, x0");           // x1 = size
        emit(gen, "ldr x0, [sp], #16");    // x0 = ptr
        emit(gen, "bl _builtin_free");
        return;
    }
    
    // Builtin: str_copy(dest, src, len) - copy len bytes from src to dest
    if (strcmp(call->name, "str_copy") == 0 && call->arg_count == 3) {
        codegen_expr(gen, call->args[0]);  // dest
        emit(gen, "str x0, [sp, #-16]!");
        codegen_expr(gen, call->args[1]);  // src
        emit(gen, "str x0, [sp, #-16]!");
        codegen_expr(gen, call->args[2]);  // len
        emit(gen, "mov x2, x0");           // x2 = len
        emit(gen, "ldr x1, [sp], #16");    // x1 = src
        emit(gen, "ldr x0, [sp], #16");    // x0 = dest
        // Simple byte-by-byte copy loop
        int loop_label = new_label(gen);
        int done_label = new_label(gen);
        emit(gen, "cbz x2, .L%d", done_label);
        emit_raw(gen, ".L%d:", loop_label);
        emit(gen, "ldrb w3, [x1], #1");
        emit(gen, "strb w3, [x0], #1");
        emit(gen, "subs x2, x2, #1");
        emit(gen, "b.ne .L%d", loop_label);
        emit_raw(gen, ".L%d:", done_label);
        return;
    }
    
    // Builtin: int_to_str(dest, n) - convert int to string, return length
    if (strcmp(call->name, "int_to_str") == 0 && call->arg_count == 2) {
        codegen_expr(gen, call->args[0]);  // dest
        emit(gen, "str x0, [sp, #-16]!");
        codegen_expr(gen, call->args[1]);  // n
        emit(gen, "mov x1, x0");           // x1 = n
        emit(gen, "ldr x0, [sp], #16");    // x0 = dest
        emit(gen, "bl _builtin_int_to_str");
        return;
    }
    
    // Builtin: poke(addr, value) - write 8-byte value to memory address
    if (strcmp(call->name, "poke") == 0 && call->arg_count == 2) {
        codegen_expr(gen, call->args[0]);  // addr
        emit(gen, "str x0, [sp, #-16]!");
        codegen_expr(gen, call->args[1]);  // value
        emit(gen, "mov x1, x0");           // x1 = value
        emit(gen, "ldr x0, [sp], #16");    // x0 = addr
        emit(gen, "str x1, [x0]");         // store value at addr
        return;
    }
    
    // Builtin: peek(addr) - read 8-byte value from memory address
    if (strcmp(call->name, "peek") == 0 && call->arg_count == 1) {
        codegen_expr(gen, call->args[0]);  // addr
        emit(gen, "ldr x0, [x0]");         // load value from addr
        return;
    }
    
    // Builtin: map(arr, f) - apply f to each element, return new heap-allocated array
    if (strcmp(call->name, "map") == 0 && call->arg_count == 2) {
        codegen_expr(gen, call->args[0]);  // arr ptr in x0
        emit(gen, "str x0, [sp, #-16]!");
        codegen_expr(gen, call->args[1]);  // closure ptr in x0
        emit(gen, "mov x1, x0");           // x1 = closure ptr
        emit(gen, "ldr x0, [sp], #16");    // x0 = arr ptr
        emit(gen, "bl _builtin_map");
        return;
    }
    
    // Builtin: filter(arr, f) - keep elements where f returns truthy
    if (strcmp(call->name, "filter") == 0 && call->arg_count == 2) {
        codegen_expr(gen, call->args[0]);  // arr ptr in x0
        emit(gen, "str x0, [sp, #-16]!");
        codegen_expr(gen, call->args[1]);  // closure ptr in x0
        emit(gen, "mov x1, x0");           // x1 = closure ptr
        emit(gen, "ldr x0, [sp], #16");    // x0 = arr ptr
        emit(gen, "bl _builtin_filter");
        return;
    }
    
    // Builtin: reduce(arr, init, f) - fold array with accumulator
    if (strcmp(call->name, "reduce") == 0 && call->arg_count == 3) {
        codegen_expr(gen, call->args[0]);  // arr ptr in x0
        emit(gen, "str x0, [sp, #-16]!");
        codegen_expr(gen, call->args[1]);  // init in x0
        emit(gen, "str x0, [sp, #-16]!");
        codegen_expr(gen, call->args[2]);  // closure ptr in x0
        emit(gen, "mov x2, x0");           // x2 = closure ptr
        emit(gen, "ldr x1, [sp], #16");    // x1 = init
        emit(gen, "ldr x0, [sp], #16");    // x0 = arr ptr
        emit(gen, "bl _builtin_reduce");
        return;
    }
    
    // Check if this is a closure call (name is a local variable)
    int is_closure_call = 0;
    int local_offset = 0;
    for (int i = gen->local_count - 1; i >= 0; i--) {
        if (strcmp(gen->locals[i].name, call->name) == 0) {
            local_offset = gen->locals[i].offset;
            is_closure_call = 1;
            break;
        }
    }
    
    // AAPCS64: first 8 args in x0-x7
    const char *arg_regs[] = {"x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"};
    
    if (is_closure_call) {
        // Closure call: variable holds pointer to [fn_ptr, env_ptr]
        // Evaluate user args and push (in reverse order)
        for (int i = call->arg_count - 1; i >= 0; i--) {
            codegen_expr(gen, call->args[i]);
            emit(gen, "str x0, [sp, #-16]!");
        }
        
        // Load closure struct pointer
        emit_ldr_fp(gen, "x9", local_offset);
        // x9 = closure ptr -> [fn_ptr, env_ptr]
        emit(gen, "ldr x10, [x9]");         // x10 = fn_ptr
        emit(gen, "ldr x11, [x9, #8]");     // x11 = env_ptr
        
        // Push env as first arg (before user args)
        emit(gen, "str x11, [sp, #-16]!");
        
        // Pop all args into registers: env first, then user args
        int total_args = 1 + call->arg_count;
        for (int i = 0; i < total_args && i < 8; i++) {
            emit(gen, "ldr %s, [sp], #16", arg_regs[i]);
        }
        
        // Indirect call through fn_ptr
        emit(gen, "blr x10");
    } else {
        // Regular function call
        // Evaluate args and push to stack (in reverse order)
        for (int i = call->arg_count - 1; i >= 0; i--) {
            codegen_expr(gen, call->args[i]);
            emit(gen, "str x0, [sp, #-16]!");
        }
        
        // Pop into argument registers
        for (int i = 0; i < call->arg_count && i < 8; i++) {
            emit(gen, "ldr %s, [sp], #16", arg_regs[i]);
        }
        
        // Call the function
        emit(gen, "bl %s", call->name);
    }
}

static void codegen_closure(CodeGen *gen, Expr *expr) {
    ClosureExpr *cl = &expr->as.closure;
    
    // Analyze captures
    char **captures = NULL;
    int capture_count = 0;
    int capture_cap = 0;
    
    if (cl->body_expr) {
        collect_captures_expr(cl->body_expr, cl->params, cl->param_count,
                             &captures, &capture_count, &capture_cap);
    } else {
        collect_captures_stmt(cl->body_block, cl->params, cl->param_count,
                             &captures, &capture_count, &capture_cap);
    }
    
    // Filter captures to only include actual local variables in scope
    char **real_captures = NULL;
    int real_count = 0;
    for (int i = 0; i < capture_count; i++) {
        // Check if this name is actually a local variable
        int found = 0;
        for (int j = gen->local_count - 1; j >= 0; j--) {
            if (strcmp(gen->locals[j].name, captures[i]) == 0) {
                found = 1;
                break;
            }
        }
        if (found) {
            real_captures = realloc(real_captures, sizeof(char*) * (real_count + 1));
            real_captures[real_count++] = captures[i];
        } else {
            free(captures[i]);
        }
    }
    free(captures);
    captures = real_captures;
    capture_count = real_count;
    
    // Generate unique function name for the closure body
    int closure_id = gen->closure_count;
    char fn_name[64];
    snprintf(fn_name, sizeof(fn_name), "__closure_%d", closure_id);
    
    // Defer the closure function for later emission
    // The closure function takes (env, user_params...)
    gen->closures[closure_id].name = my_strdup(fn_name);
    
    // Build param list: __env + user params
    int total_params = 1 + cl->param_count;
    char **fn_params = malloc(sizeof(char*) * total_params);
    fn_params[0] = my_strdup("__env");
    for (int i = 0; i < cl->param_count; i++) {
        fn_params[i + 1] = my_strdup(cl->params[i]);
    }
    gen->closures[closure_id].params = fn_params;
    gen->closures[closure_id].param_count = total_params;
    gen->closures[closure_id].body_expr = cl->body_expr;
    gen->closures[closure_id].body_block = cl->body_block;
    gen->closures[closure_id].captures = captures;
    gen->closures[closure_id].capture_count = capture_count;
    gen->closure_count++;
    
    // Now emit code to create the closure at runtime:
    // 1. Allocate closure struct on stack: [fn_ptr, env_ptr] = 16 bytes
    int closure_offset = gen->stack_offset - 16;
    closure_offset = closure_offset & ~7;
    gen->stack_offset = closure_offset;
    
    // Store function pointer
    emit(gen, "adrp x0, %s", fn_name);
    emit(gen, "add x0, x0, :lo12:%s", fn_name);
    emit_str_fp(gen, "x0", closure_offset);
    
    // 2. Allocate and fill environment if there are captures
    if (capture_count > 0) {
        // Env is an array of captured values: capture_count * 8 bytes
        int env_size = capture_count * 8;
        int env_offset = gen->stack_offset - env_size;
        env_offset = env_offset & ~7;
        gen->stack_offset = env_offset;
        
        // Copy captured values into env
        for (int i = 0; i < capture_count; i++) {
            int src_offset = find_local(gen, captures[i]);
            emit_ldr_fp(gen, "x0", src_offset);
            emit_str_fp(gen, "x0", env_offset + i * 8);
        }
        
        // Store env pointer in closure struct
        // env_offset is negative (below x29)
        if (-env_offset <= 4095) {
            emit(gen, "sub x0, x29, #%d", -env_offset);
        } else {
            emit(gen, "mov x0, #%d", -env_offset);
            emit(gen, "sub x0, x29, x0");
        }
        emit_str_fp(gen, "x0", closure_offset + 8);
    } else {
        // No captures - env is NULL
        emit(gen, "mov x0, #0");
        emit_str_fp(gen, "x0", closure_offset + 8);
    }
    
    // Return pointer to closure struct
    // closure_offset is negative (below x29)
    if (-closure_offset <= 4095) {
        emit(gen, "sub x0, x29, #%d", -closure_offset);
    } else {
        emit(gen, "mov x0, #%d", -closure_offset);
        emit(gen, "sub x0, x29, x0");
    }
}

static void codegen_method_call(CodeGen *gen, Expr *expr) {
    MethodCallExpr *mc = &expr->as.method_call;
    
    // Figure out the struct type of the object
    const char *stype = infer_struct_type(gen, mc->object);
    if (!stype) {
        fprintf(stderr, "[line %d] Error: cannot determine struct type for method call '.%s'\n",
                expr->line, mc->method_name);
        exit(1);
    }
    
    // Build mangled function name: StructType_method
    int slen = strlen(stype);
    int mlen = strlen(mc->method_name);
    char *mangled = malloc(slen + 1 + mlen + 1);
    memcpy(mangled, stype, slen);
    mangled[slen] = '_';
    memcpy(mangled + slen + 1, mc->method_name, mlen);
    mangled[slen + 1 + mlen] = '\0';
    
    // Total args = self + explicit args
    int total_args = 1 + mc->arg_count;
    
    // AAPCS64: first 8 args in x0-x7
    const char *arg_regs[] = {"x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"};
    
    // Evaluate all args (self first) and push to stack in reverse order
    for (int i = mc->arg_count - 1; i >= 0; i--) {
        codegen_expr(gen, mc->args[i]);
        emit(gen, "str x0, [sp, #-16]!");
    }
    // self (the object) is arg 0
    codegen_expr(gen, mc->object);
    emit(gen, "str x0, [sp, #-16]!");
    
    // Pop into argument registers
    for (int i = 0; i < total_args && i < 8; i++) {
        emit(gen, "ldr %s, [sp], #16", arg_regs[i]);
    }
    
    // Call the mangled function
    emit(gen, "bl %s", mangled);
    free(mangled);
}

static void codegen_expr(CodeGen *gen, Expr *expr) {
    switch (expr->type) {
        case EXPR_INT_LITERAL:    codegen_int_literal(gen, expr); break;
        case EXPR_BOOL_LITERAL:   codegen_bool_literal(gen, expr); break;
        case EXPR_STRING_LITERAL: codegen_string_literal(gen, expr); break;
        case EXPR_ARRAY_LITERAL:  codegen_array_literal(gen, expr); break;
        case EXPR_VARIABLE:       codegen_variable(gen, expr); break;
        case EXPR_BINARY:         codegen_binary(gen, expr); break;
        case EXPR_UNARY:          codegen_unary(gen, expr); break;
        case EXPR_CALL:           codegen_call(gen, expr); break;
        case EXPR_INDEX:          codegen_index(gen, expr); break;
        case EXPR_STRUCT_LITERAL: codegen_struct_literal(gen, expr); break;
        case EXPR_FIELD_ACCESS:   codegen_field_access(gen, expr); break;
        case EXPR_ENUM_VARIANT:   codegen_enum_variant(gen, expr); break;
        case EXPR_MATCH:          codegen_match(gen, expr); break;
        case EXPR_METHOD_CALL:    codegen_method_call(gen, expr); break;
        case EXPR_CLOSURE:        codegen_closure(gen, expr); break;
    }
}

// ============ STATEMENT CODEGEN ============

static void codegen_stmt(CodeGen *gen, Stmt *stmt);

static void codegen_block(CodeGen *gen, Stmt *stmt) {
    BlockStmt *block = &stmt->as.block;
    push_scope(gen);
    for (int i = 0; i < block->count; i++) {
        codegen_stmt(gen, block->statements[i]);
    }
    pop_scope(gen);
}

static void codegen_let(CodeGen *gen, Stmt *stmt) {
    LetStmt *let = &stmt->as.let;
    
    // Infer struct type before codegen (which may modify stack_offset)
    const char *stype = infer_struct_type(gen, let->initializer);
    
    // Evaluate initializer
    codegen_expr(gen, let->initializer);
    
    // Allocate stack slot and store
    int offset = add_local(gen, let->name);
    emit_str_fp(gen, "x0", offset);
    
    // Track the struct type of this variable
    if (stype) {
        set_local_type(gen, stype);
    }
}

static void codegen_assign(CodeGen *gen, Stmt *stmt) {
    AssignStmt *assign = &stmt->as.assign;
    
    // Infer struct type from the value being assigned
    const char *stype = infer_struct_type(gen, assign->value);
    
    // Evaluate value
    codegen_expr(gen, assign->value);
    
    // Find existing variable and store
    int offset = find_local(gen, assign->name);
    emit_str_fp(gen, "x0", offset);
    
    // Update the struct type of this variable if the new value is a struct
    if (stype) {
        // Find the local and update its type
        for (int i = gen->local_count - 1; i >= 0; i--) {
            if (strcmp(gen->locals[i].name, assign->name) == 0) {
                free(gen->locals[i].struct_type);
                gen->locals[i].struct_type = my_strdup(stype);
                break;
            }
        }
    }
}

static void codegen_index_assign(CodeGen *gen, Stmt *stmt) {
    IndexAssignStmt *ia = &stmt->as.index_assign;
    
    // Evaluate array (get base pointer)
    codegen_expr(gen, ia->array);
    emit(gen, "str x0, [sp, #-16]!");  // push array ptr
    
    // Evaluate index
    codegen_expr(gen, ia->index);
    emit(gen, "str x0, [sp, #-16]!");  // push index
    
    // Bounds check: index in x0, load length from array
    emit(gen, "ldr x1, [sp, #16]");    // peek array ptr (2 slots back)
    emit(gen, "ldr x1, [x1]");         // x1 = array length
    emit(gen, "bl _bounds_check");     // check 0 <= x0 < x1
    
    // Evaluate value
    codegen_expr(gen, ia->value);
    // x0 = value
    
    // Pop index and array ptr
    emit(gen, "ldr x1, [sp], #16");    // x1 = index
    emit(gen, "ldr x2, [sp], #16");    // x2 = array ptr
    
    // Store: array[index] = value (skip length at offset 0)
    emit(gen, "add x2, x2, #8");       // skip length field
    emit(gen, "lsl x1, x1, #3");       // x1 = index * 8
    emit(gen, "add x2, x2, x1");       // x2 = base + 8 + offset
    emit(gen, "str x0, [x2]");         // store value
}

static void codegen_field_assign(CodeGen *gen, Stmt *stmt) {
    FieldAssignStmt *fa = &stmt->as.field_assign;
    
    // Try to infer the struct type from the object expression
    const char *stype = infer_struct_type(gen, fa->object);
    
    // Evaluate object (get struct pointer)
    codegen_expr(gen, fa->object);
    emit(gen, "str x0, [sp, #-16]!");  // push struct ptr
    
    // Evaluate value
    codegen_expr(gen, fa->value);
    // x0 = value
    
    // Pop struct ptr
    emit(gen, "ldr x1, [sp], #16");    // x1 = struct ptr
    
    int offset = -1;
    
    if (stype) {
        // We know the struct type - look up the field in that specific struct
        offset = find_field_offset(gen, stype, fa->field_name);
    }
    
    if (offset < 0) {
        // Fallback: search all structs (old behavior for untyped cases)
        for (int i = 0; i < gen->struct_count && offset < 0; i++) {
            for (int j = 0; j < gen->structs[i].field_count; j++) {
                if (strcmp(gen->structs[i].field_names[j], fa->field_name) == 0) {
                    offset = j * 8;
                    break;
                }
            }
        }
    }
    
    if (offset < 0) {
        fprintf(stderr, "Unknown field: %s\n", fa->field_name);
        offset = 0;
    }
    
    // Store value at field offset
    emit(gen, "str x0, [x1, #%d]", offset);
}

static void codegen_print(CodeGen *gen, Stmt *stmt) {
    PrintStmt *print = &stmt->as.print;
    
    // Evaluate expression
    codegen_expr(gen, print->expr);
    
    // Determine if it's a string or int
    if (print->expr->type == EXPR_STRING_LITERAL) {
        // x0 = pointer to string, x1 = length
        int len = print->expr->as.string_literal.length;
        emit(gen, "mov x1, #%d", len);
        emit(gen, "bl print_str");
    } else {
        // Call print_int (x0 already has the value)
        emit(gen, "bl print_int");
    }
}

static void codegen_if(CodeGen *gen, Stmt *stmt) {
    IfStmt *if_stmt = &stmt->as.if_stmt;
    
    int else_label = new_label(gen);
    int end_label = new_label(gen);
    
    // Evaluate condition
    codegen_expr(gen, if_stmt->condition);
    emit(gen, "cmp x0, #0");
    emit(gen, "b.eq .L%d", else_label);
    
    // Then branch
    codegen_stmt(gen, if_stmt->then_branch);
    emit(gen, "b .L%d", end_label);
    
    // Else branch
    emit_raw(gen, ".L%d:", else_label);
    if (if_stmt->else_branch) {
        codegen_stmt(gen, if_stmt->else_branch);
    }
    
    emit_raw(gen, ".L%d:", end_label);
}

static void codegen_while(CodeGen *gen, Stmt *stmt) {
    WhileStmt *while_stmt = &stmt->as.while_stmt;
    
    int start_label = new_label(gen);
    int end_label = new_label(gen);
    
    // Push loop labels for break/continue
    if (gen->loop_depth >= 32) {
        fprintf(stderr, "Error: too many nested loops (max 32)\n");
        exit(1);
    }
    gen->loop_stack[gen->loop_depth].start_label = start_label;
    gen->loop_stack[gen->loop_depth].end_label = end_label;
    gen->loop_depth++;
    
    emit_raw(gen, ".L%d:", start_label);
    
    // Evaluate condition
    codegen_expr(gen, while_stmt->condition);
    emit(gen, "cmp x0, #0");
    emit(gen, "b.eq .L%d", end_label);
    
    // Body
    codegen_stmt(gen, while_stmt->body);
    emit(gen, "b .L%d", start_label);
    
    emit_raw(gen, ".L%d:", end_label);
    
    // Pop loop labels
    gen->loop_depth--;
}

static void codegen_for(CodeGen *gen, Stmt *stmt) {
    ForStmt *for_stmt = &stmt->as.for_stmt;
    
    int start_label = new_label(gen);
    int end_label = new_label(gen);
    
    // Push loop labels for break/continue
    if (gen->loop_depth >= 32) {
        fprintf(stderr, "Error: too many nested loops (max 32)\n");
        exit(1);
    }
    
    if (for_stmt->start != NULL) {
        // Range for: for VAR in START..END { body }
        // Desugars to:
        //   let VAR = START;
        //   let __end = END;
        //   while VAR < __end { body; VAR = VAR + 1; }
        
        // Evaluate start value and store in loop variable
        codegen_expr(gen, for_stmt->start);
        int var_offset = add_local(gen, for_stmt->var_name);
        emit_str_fp(gen, "x0", var_offset);
        
        // Evaluate end value and store in hidden local
        codegen_expr(gen, for_stmt->end);
        int end_offset = add_local(gen, "__for_end");
        emit_str_fp(gen, "x0", end_offset);
        
        // continue should jump to the increment, not the condition
        int inc_label = new_label(gen);
        gen->loop_stack[gen->loop_depth].start_label = inc_label;
        gen->loop_stack[gen->loop_depth].end_label = end_label;
        gen->loop_depth++;
        
        // Loop start: check VAR < END
        emit_raw(gen, ".L%d:", start_label);
        emit_ldr_fp(gen, "x0", var_offset);
        emit_ldr_fp(gen, "x1", end_offset);
        emit(gen, "cmp x0, x1");
        emit(gen, "b.ge .L%d", end_label);
        
        // Body
        codegen_stmt(gen, for_stmt->body);
        
        // Increment: VAR = VAR + 1
        emit_raw(gen, ".L%d:", inc_label);
        emit_ldr_fp(gen, "x0", var_offset);
        emit(gen, "add x0, x0, #1");
        emit_str_fp(gen, "x0", var_offset);
        emit(gen, "b .L%d", start_label);
        
        emit_raw(gen, ".L%d:", end_label);
    } else {
        // Array for: for VAR in ARR { body }
        // Desugars to:
        //   let __arr = ARR;
        //   let __idx = 0;
        //   let __len = len(__arr);
        //   while __idx < __len { let VAR = __arr[__idx]; body; __idx = __idx + 1; }
        
        // Evaluate array and store
        codegen_expr(gen, for_stmt->iterable);
        int arr_offset = add_local(gen, "__for_arr");
        emit_str_fp(gen, "x0", arr_offset);
        
        // Store length
        emit(gen, "ldr x0, [x0]");     // load length from array header
        int len_offset = add_local(gen, "__for_len");
        emit_str_fp(gen, "x0", len_offset);
        
        // Initialize index to 0
        emit(gen, "mov x0, #0");
        int idx_offset = add_local(gen, "__for_idx");
        emit_str_fp(gen, "x0", idx_offset);
        
        // Allocate the loop variable
        emit(gen, "mov x0, #0");
        int var_offset = add_local(gen, for_stmt->var_name);
        emit_str_fp(gen, "x0", var_offset);
        
        // continue should jump to the increment
        int inc_label = new_label(gen);
        gen->loop_stack[gen->loop_depth].start_label = inc_label;
        gen->loop_stack[gen->loop_depth].end_label = end_label;
        gen->loop_depth++;
        
        // Loop start: check __idx < __len
        emit_raw(gen, ".L%d:", start_label);
        emit_ldr_fp(gen, "x0", idx_offset);
        emit_ldr_fp(gen, "x1", len_offset);
        emit(gen, "cmp x0, x1");
        emit(gen, "b.ge .L%d", end_label);
        
        // Load VAR = arr[__idx]
        emit_ldr_fp(gen, "x1", arr_offset);
        emit(gen, "add x1, x1, #8");       // skip length field
        emit_ldr_fp(gen, "x0", idx_offset);
        emit(gen, "lsl x0, x0, #3");
        emit(gen, "ldr x0, [x1, x0]");
        emit_str_fp(gen, "x0", var_offset);
        
        // Body
        codegen_stmt(gen, for_stmt->body);
        
        // Increment: __idx = __idx + 1
        emit_raw(gen, ".L%d:", inc_label);
        emit_ldr_fp(gen, "x0", idx_offset);
        emit(gen, "add x0, x0, #1");
        emit_str_fp(gen, "x0", idx_offset);
        emit(gen, "b .L%d", start_label);
        
        emit_raw(gen, ".L%d:", end_label);
    }
    
    // Pop loop labels
    gen->loop_depth--;
}

static void codegen_return(CodeGen *gen, Stmt *stmt) {
    ReturnStmt *ret = &stmt->as.return_stmt;
    
    if (ret->value) {
        codegen_expr(gen, ret->value);
    } else {
        emit(gen, "mov x0, #0");
    }
    
    // Epilogue
    emit(gen, "mov sp, x29");
    emit(gen, "ldp x29, x30, [sp], #16");
    emit(gen, "ret");
}

static void codegen_expr_stmt(CodeGen *gen, Stmt *stmt) {
    codegen_expr(gen, stmt->as.expr.expr);
}

static void codegen_break(CodeGen *gen, Stmt *stmt) {
    if (gen->loop_depth == 0) {
        fprintf(stderr, "[line %d] Error: 'break' outside of loop\n", stmt->line);
        exit(1);
    }
    int end_label = gen->loop_stack[gen->loop_depth - 1].end_label;
    emit(gen, "b .L%d", end_label);
}

static void codegen_continue(CodeGen *gen, Stmt *stmt) {
    if (gen->loop_depth == 0) {
        fprintf(stderr, "[line %d] Error: 'continue' outside of loop\n", stmt->line);
        exit(1);
    }
    int start_label = gen->loop_stack[gen->loop_depth - 1].start_label;
    emit(gen, "b .L%d", start_label);
}

static void codegen_stmt(CodeGen *gen, Stmt *stmt) {
    switch (stmt->type) {
        case STMT_BLOCK:        codegen_block(gen, stmt); break;
        case STMT_LET:          codegen_let(gen, stmt); break;
        case STMT_ASSIGN:       codegen_assign(gen, stmt); break;
        case STMT_INDEX_ASSIGN: codegen_index_assign(gen, stmt); break;
        case STMT_FIELD_ASSIGN: codegen_field_assign(gen, stmt); break;
        case STMT_PRINT:        codegen_print(gen, stmt); break;
        case STMT_IF:           codegen_if(gen, stmt); break;
        case STMT_WHILE:        codegen_while(gen, stmt); break;
        case STMT_FOR:          codegen_for(gen, stmt); break;
        case STMT_RETURN:       codegen_return(gen, stmt); break;
        case STMT_BREAK:        codegen_break(gen, stmt); break;
        case STMT_CONTINUE:     codegen_continue(gen, stmt); break;
        case STMT_EXPR:         codegen_expr_stmt(gen, stmt); break;
    }
}

// ============ FUNCTION CODEGEN ============

static void codegen_function(CodeGen *gen, Function *fn) {
    const char *arg_regs[] = {"x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"};
    
    clear_locals(gen);
    
    emit_raw(gen, "");
    emit_raw(gen, "%s:", fn->name);
    
    // Prologue: save frame pointer and link register
    emit(gen, "stp x29, x30, [sp, #-16]!");
    emit(gen, "mov x29, sp");
    
    // Reserve space for locals (512 bytes - enough for large structs)
    emit(gen, "sub sp, sp, #512");
    
    // Check if this is a method (name contains '_' like StructName_method)
    // Extract struct name for 'self' parameter type tracking
    char *method_struct_name = NULL;
    char *underscore = strchr(fn->name, '_');
    if (underscore && fn->param_count > 0 && strcmp(fn->params[0], "self") == 0) {
        int prefix_len = underscore - fn->name;
        method_struct_name = malloc(prefix_len + 1);
        memcpy(method_struct_name, fn->name, prefix_len);
        method_struct_name[prefix_len] = '\0';
    }
    
    // Move arguments from registers to stack
    for (int i = 0; i < fn->param_count && i < 8; i++) {
        int offset = add_local(gen, fn->params[i]);
        emit_str_fp(gen, arg_regs[i], offset);
        
        // If this is 'self', set its struct type
        if (method_struct_name && i == 0) {
            set_local_type(gen, method_struct_name);
        }
    }
    free(method_struct_name);
    
    // Generate body
    codegen_stmt(gen, fn->body);
    
    // Default return
    emit(gen, "mov x0, #0");
    emit(gen, "mov sp, x29");
    emit(gen, "ldp x29, x30, [sp], #16");
    emit(gen, "ret");
}

static void emit_closure_functions(CodeGen *gen) {
    const char *arg_regs[] = {"x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"};
    
    // Emit all deferred closure functions
    // Note: emitting a closure may create more closures (nested), so use index loop
    for (int c = 0; c < gen->closure_count; c++) {
        clear_locals(gen);
        
        emit_raw(gen, "");
        emit_raw(gen, "%s:", gen->closures[c].name);
        
        // Prologue
        emit(gen, "stp x29, x30, [sp, #-16]!");
        emit(gen, "mov x29, sp");
        emit(gen, "sub sp, sp, #512");
        
        // Store params (env + user params) from registers to stack
        for (int i = 0; i < gen->closures[c].param_count && i < 8; i++) {
            int offset = add_local(gen, gen->closures[c].params[i]);
            emit_str_fp(gen, arg_regs[i], offset);
        }
        
        // Unpack captured values from env into local variables
        // env is __env parameter, captures are at env[0], env[1], ...
        if (gen->closures[c].capture_count > 0) {
            int env_offset = find_local(gen, "__env");
            for (int i = 0; i < gen->closures[c].capture_count; i++) {
                emit_ldr_fp(gen, "x1", env_offset);  // x1 = env ptr
                emit(gen, "ldr x0, [x1, #%d]", i * 8);
                int cap_offset = add_local(gen, gen->closures[c].captures[i]);
                emit_str_fp(gen, "x0", cap_offset);
            }
        }
        
        // Generate body
        if (gen->closures[c].body_block) {
            codegen_stmt(gen, gen->closures[c].body_block);
        } else if (gen->closures[c].body_expr) {
            codegen_expr(gen, gen->closures[c].body_expr);
            // Expression body returns the value
            emit(gen, "mov sp, x29");
            emit(gen, "ldp x29, x30, [sp], #16");
            emit(gen, "ret");
        }
        
        // Default return (for block bodies without explicit return)
        emit(gen, "mov x0, #0");
        emit(gen, "mov sp, x29");
        emit(gen, "ldp x29, x30, [sp], #16");
        emit(gen, "ret");
    }
}

// ============ PROGRAM CODEGEN ============

static void emit_runtime(CodeGen *gen) {
    // print_int: prints integer in x0, followed by newline
    emit_raw(gen, "");
    emit_raw(gen, "// Runtime: print_int");
    emit_raw(gen, "print_int:");
    emit(gen, "stp x29, x30, [sp, #-16]!");
    emit(gen, "mov x29, sp");
    emit(gen, "sub sp, sp, #32");
    
    // Handle negative numbers
    emit(gen, "mov x9, x0");           // save original
    emit(gen, "mov x10, #0");          // negative flag
    emit(gen, "cmp x9, #0");
    emit(gen, "b.ge .print_pos");
    emit(gen, "mov x10, #1");
    emit(gen, "neg x9, x9");
    
    emit_raw(gen, ".print_pos:");
    // Build string backwards from sp
    emit(gen, "mov x11, sp");          // end of buffer
    emit(gen, "mov x12, #10");         // newline
    emit(gen, "strb w12, [x11, #-1]!");
    emit(gen, "mov x13, #10");         // divisor
    
    emit_raw(gen, ".print_loop:");
    emit(gen, "udiv x14, x9, x13");    // x14 = x9 / 10
    emit(gen, "msub x15, x14, x13, x9"); // x15 = x9 - x14*10 (remainder)
    emit(gen, "add x15, x15, #48");    // convert to ASCII
    emit(gen, "strb w15, [x11, #-1]!");
    emit(gen, "mov x9, x14");
    emit(gen, "cbnz x9, .print_loop");
    
    // Add minus sign if negative
    emit(gen, "cbz x10, .print_write");
    emit(gen, "mov x12, #45");         // '-'
    emit(gen, "strb w12, [x11, #-1]!");
    
    emit_raw(gen, ".print_write:");
    // syscall: write(1, buf, len)
    emit(gen, "mov x0, #1");           // stdout
    emit(gen, "mov x1, x11");          // buffer start
    emit(gen, "mov x2, sp");
    emit(gen, "sub x2, x2, x11");      // length
    emit(gen, "mov x8, #64");          // sys_write on arm64
    emit(gen, "svc #0");
    
    emit(gen, "mov sp, x29");
    emit(gen, "ldp x29, x30, [sp], #16");
    emit(gen, "ret");
    
    // _put_int_no_newline: prints integer without newline
    emit_raw(gen, "");
    emit_raw(gen, "// Runtime: _put_int_no_newline");
    emit_raw(gen, "_put_int_no_newline:");
    emit(gen, "stp x29, x30, [sp, #-16]!");
    emit(gen, "mov x29, sp");
    emit(gen, "sub sp, sp, #32");
    
    emit(gen, "mov x9, x0");
    emit(gen, "mov x10, #0");
    emit(gen, "cmp x9, #0");
    emit(gen, "b.ge .pint_pos");
    emit(gen, "mov x10, #1");
    emit(gen, "neg x9, x9");
    
    emit_raw(gen, ".pint_pos:");
    emit(gen, "mov x11, sp");
    emit(gen, "mov x13, #10");
    
    emit_raw(gen, ".pint_loop:");
    emit(gen, "udiv x14, x9, x13");
    emit(gen, "msub x15, x14, x13, x9");
    emit(gen, "add x15, x15, #48");
    emit(gen, "strb w15, [x11, #-1]!");
    emit(gen, "mov x9, x14");
    emit(gen, "cbnz x9, .pint_loop");
    
    emit(gen, "cbz x10, .pint_write");
    emit(gen, "mov x12, #45");
    emit(gen, "strb w12, [x11, #-1]!");
    
    emit_raw(gen, ".pint_write:");
    emit(gen, "mov x0, #1");
    emit(gen, "mov x1, x11");
    emit(gen, "mov x2, sp");
    emit(gen, "sub x2, x2, x11");
    emit(gen, "mov x8, #64");
    emit(gen, "svc #0");
    
    emit(gen, "mov sp, x29");
    emit(gen, "ldp x29, x30, [sp], #16");
    emit(gen, "ret");
}

static void emit_print_char(CodeGen *gen) {
    // print_char: output single character in x0
    emit_raw(gen, "");
    emit_raw(gen, "// Runtime: print_char");
    emit_raw(gen, "print_char:");
    emit(gen, "stp x29, x30, [sp, #-16]!");
    emit(gen, "mov x29, sp");
    emit(gen, "sub sp, sp, #16");
    emit(gen, "strb w0, [sp]");
    emit(gen, "mov x0, #1");           // stdout
    emit(gen, "mov x1, sp");           // buffer
    emit(gen, "mov x2, #1");           // length
    emit(gen, "mov x8, #64");          // sys_write
    emit(gen, "svc #0");
    emit(gen, "mov sp, x29");
    emit(gen, "ldp x29, x30, [sp], #16");
    emit(gen, "ret");
}

static void emit_print_str(CodeGen *gen) {
    // print_str: prints string at x0 with length x1 (no newline!)
    emit_raw(gen, "");
    emit_raw(gen, "// Runtime: print_str");
    emit_raw(gen, "print_str:");
    emit(gen, "stp x29, x30, [sp, #-16]!");
    emit(gen, "mov x29, sp");
    emit(gen, "mov x2, x1");           // length
    emit(gen, "mov x1, x0");           // buffer
    emit(gen, "mov x0, #1");           // stdout
    emit(gen, "mov x8, #64");          // sys_write
    emit(gen, "svc #0");
    emit(gen, "ldp x29, x30, [sp], #16");
    emit(gen, "ret");
}

static void emit_print_int_raw(CodeGen *gen) {
    // print_int_raw: output integer in x0 without newline
    emit_raw(gen, "");
    emit_raw(gen, "// Runtime: print_int_raw");
    emit_raw(gen, "print_int_raw:");
    emit(gen, "stp x29, x30, [sp, #-16]!");
    emit(gen, "mov x29, sp");
    emit(gen, "sub sp, sp, #32");
    emit(gen, "mov x9, x0");
    emit(gen, "mov x10, #0");          // is_negative flag
    emit(gen, "cmp x9, #0");
    emit(gen, "b.ge .pir_pos");
    emit(gen, "mov x10, #1");
    emit(gen, "neg x9, x9");
    emit_raw(gen, ".pir_pos:");
    emit(gen, "mov x11, sp");          // buffer pointer
    emit(gen, "mov x13, #10");         // divisor
    emit_raw(gen, ".pir_loop:");
    emit(gen, "udiv x14, x9, x13");
    emit(gen, "msub x15, x14, x13, x9");
    emit(gen, "add x15, x15, #48");    // to ASCII
    emit(gen, "strb w15, [x11, #-1]!");
    emit(gen, "mov x9, x14");
    emit(gen, "cbnz x9, .pir_loop");
    emit(gen, "cbz x10, .pir_write");
    emit(gen, "mov x12, #45");         // '-'
    emit(gen, "strb w12, [x11, #-1]!");
    emit_raw(gen, ".pir_write:");
    emit(gen, "mov x0, #1");           // stdout
    emit(gen, "mov x1, x11");          // buffer
    emit(gen, "mov x2, sp");
    emit(gen, "sub x2, x2, x11");      // length
    emit(gen, "mov x8, #64");          // sys_write
    emit(gen, "svc #0");
    emit(gen, "mov sp, x29");
    emit(gen, "ldp x29, x30, [sp], #16");
    emit(gen, "ret");
}

static void emit_file_io(CodeGen *gen) {
    // _builtin_read_file: x0 = filename ptr (null-terminated)
    // Returns: x0 = ptr to buffer with [length][data...]
    // Uses mmap to allocate 128KB buffer for large files
    emit_raw(gen, "");
    emit_raw(gen, "// Builtin: read_file");
    emit_raw(gen, "_builtin_read_file:");
    emit(gen, "stp x29, x30, [sp, #-16]!");
    emit(gen, "mov x29, sp");
    emit(gen, "stp x19, x20, [sp, #-16]!");  // save callee-saved regs
    emit(gen, "stp x21, x22, [sp, #-16]!");
    
    // Save filename in callee-saved register
    emit(gen, "mov x19, x0");          // x19 = filename (callee-saved)
    
    // Allocate 128KB buffer via mmap
    emit(gen, "mov x0, #0");           // addr = NULL
    emit(gen, "mov x1, #131072");      // 128KB size
    emit(gen, "mov x2, #3");           // PROT_READ|PROT_WRITE
    emit(gen, "mov x3, #0x22");        // MAP_PRIVATE|MAP_ANONYMOUS
    emit(gen, "mov x4, #-1");          // fd
    emit(gen, "mov x5, #0");           // offset
    emit(gen, "mov x8, #222");         // sys_mmap
    emit(gen, "svc #0");
    emit(gen, "mov x20, x0");          // x20 = buffer (callee-saved)
    
    // openat(AT_FDCWD, filename, O_RDONLY, 0)
    emit(gen, "mov x0, #-100");        // AT_FDCWD
    emit(gen, "mov x1, x19");          // filename
    emit(gen, "mov x2, #0");           // O_RDONLY
    emit(gen, "mov x3, #0");           // mode
    emit(gen, "mov x8, #56");          // sys_openat
    emit(gen, "svc #0");
    emit(gen, "mov x21, x0");          // x21 = fd (callee-saved)
    
    // read(fd, buf+8, 128KB-8)
    emit(gen, "mov x0, x21");          // fd
    emit(gen, "add x1, x20, #8");      // buf + 8 (skip length field)
    emit(gen, "mov x2, #131064");      // max bytes (128KB - 8)
    emit(gen, "mov x8, #63");          // sys_read
    emit(gen, "svc #0");
    emit(gen, "str x0, [x20]");        // store bytes read as length
    
    // close(fd)
    emit(gen, "mov x0, x21");
    emit(gen, "mov x8, #57");          // sys_close
    emit(gen, "svc #0");
    
    // Return buffer pointer
    emit(gen, "mov x0, x20");
    emit(gen, "ldp x21, x22, [sp], #16");  // restore callee-saved regs
    emit(gen, "ldp x19, x20, [sp], #16");
    emit(gen, "ldp x29, x30, [sp], #16");
    emit(gen, "ret");
    
    // _builtin_write_file: x0=filename, x1=content, x2=length
    emit_raw(gen, "");
    emit_raw(gen, "// Builtin: write_file");
    emit_raw(gen, "_builtin_write_file:");
    emit(gen, "stp x29, x30, [sp, #-16]!");
    emit(gen, "mov x29, sp");
    emit(gen, "sub sp, sp, #32");
    emit(gen, "str x0, [x29, #-8]");   // save filename
    emit(gen, "str x1, [x29, #-16]");  // save content
    emit(gen, "str x2, [x29, #-24]");  // save length
    
    // openat(AT_FDCWD, filename, O_WRONLY|O_CREAT|O_TRUNC, 0644)
    emit(gen, "mov x0, #-100");        // AT_FDCWD
    emit(gen, "ldr x1, [x29, #-8]");   // filename
    emit(gen, "mov x2, #577");         // O_WRONLY|O_CREAT|O_TRUNC (1+64+512)
    emit(gen, "mov x3, #420");         // 0644
    emit(gen, "mov x8, #56");          // sys_openat
    emit(gen, "svc #0");
    emit(gen, "mov x9, x0");           // save fd
    
    // write(fd, content, length)
    emit(gen, "mov x0, x9");
    emit(gen, "ldr x1, [x29, #-16]");
    emit(gen, "ldr x2, [x29, #-24]");
    emit(gen, "mov x8, #64");          // sys_write
    emit(gen, "svc #0");
    
    // close(fd)
    emit(gen, "mov x0, x9");
    emit(gen, "mov x8, #57");          // sys_close
    emit(gen, "svc #0");
    
    emit(gen, "mov sp, x29");
    emit(gen, "ldp x29, x30, [sp], #16");
    emit(gen, "ret");
}

static void emit_alloc_and_strings(CodeGen *gen) {
    // _builtin_alloc: x0 = size, returns pointer to allocated memory
    emit_raw(gen, "");
    emit_raw(gen, "// Builtin: alloc (uses mmap)");
    emit_raw(gen, "_builtin_alloc:");
    emit(gen, "stp x29, x30, [sp, #-16]!");
    emit(gen, "mov x29, sp");
    
    // Round up size to page boundary (4096)
    emit(gen, "add x0, x0, #4095");
    emit(gen, "and x0, x0, #-4096");
    emit(gen, "mov x1, x0");           // x1 = size (for mmap)
    
    // mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)
    emit(gen, "mov x0, #0");           // addr = NULL
    // x1 already has size
    emit(gen, "mov x2, #3");           // PROT_READ | PROT_WRITE
    emit(gen, "mov x3, #0x22");        // MAP_PRIVATE | MAP_ANONYMOUS (0x02 | 0x20)
    emit(gen, "mov x4, #-1");          // fd = -1
    emit(gen, "mov x5, #0");           // offset = 0
    emit(gen, "mov x8, #222");         // sys_mmap
    emit(gen, "svc #0");
    
    emit(gen, "ldp x29, x30, [sp], #16");
    emit(gen, "ret");
    
    // _builtin_free: x0 = ptr, x1 = size - free memory using munmap
    emit_raw(gen, "");
    emit_raw(gen, "// Builtin: free (uses munmap)");
    emit_raw(gen, "_builtin_free:");
    emit(gen, "stp x29, x30, [sp, #-16]!");
    emit(gen, "mov x29, sp");
    
    // Round up size to page boundary (same as alloc)
    emit(gen, "add x1, x1, #4095");
    emit(gen, "and x1, x1, #-4096");
    
    // munmap(ptr, size)
    // x0 already has ptr
    // x1 already has rounded size
    emit(gen, "mov x8, #215");         // sys_munmap
    emit(gen, "svc #0");
    
    emit(gen, "ldp x29, x30, [sp], #16");
    emit(gen, "ret");
    
    // _builtin_int_to_str: x0 = dest buffer, x1 = number, returns length in x0
    emit_raw(gen, "");
    emit_raw(gen, "// Builtin: int_to_str");
    emit_raw(gen, "_builtin_int_to_str:");
    emit(gen, "stp x29, x30, [sp, #-16]!");
    emit(gen, "mov x29, sp");
    emit(gen, "sub sp, sp, #32");
    
    emit(gen, "mov x9, x0");           // x9 = dest
    emit(gen, "mov x10, x1");          // x10 = number
    emit(gen, "mov x11, #0");          // x11 = negative flag
    
    // Handle negative
    emit(gen, "cmp x10, #0");
    emit(gen, "b.ge .its_pos");
    emit(gen, "mov x11, #1");
    emit(gen, "neg x10, x10");
    
    emit_raw(gen, ".its_pos:");
    // Build digits in reverse on stack
    emit(gen, "mov x12, sp");          // x12 = end of temp buffer
    emit(gen, "mov x13, #10");
    
    emit_raw(gen, ".its_loop:");
    emit(gen, "udiv x14, x10, x13");   // x14 = x10 / 10
    emit(gen, "msub x15, x14, x13, x10"); // x15 = x10 % 10
    emit(gen, "add x15, x15, #48");    // ASCII digit
    emit(gen, "strb w15, [x12, #-1]!");
    emit(gen, "mov x10, x14");
    emit(gen, "cbnz x10, .its_loop");
    
    // Add minus sign if negative
    emit(gen, "cbz x11, .its_copy");
    emit(gen, "mov x14, #45");         // '-'
    emit(gen, "strb w14, [x12, #-1]!");
    
    emit_raw(gen, ".its_copy:");
    // x12 = start of string, sp = end of string
    // Copy to dest (x9), count length
    emit(gen, "mov x16, sp");          // x16 = end marker (can't compare with sp directly)
    emit(gen, "mov x0, #0");           // length counter
    emit_raw(gen, ".its_copy_loop:");
    emit(gen, "cmp x12, x16");
    emit(gen, "b.eq .its_done");
    emit(gen, "ldrb w14, [x12], #1");
    emit(gen, "strb w14, [x9], #1");
    emit(gen, "add x0, x0, #1");
    emit(gen, "b .its_copy_loop");
    
    emit_raw(gen, ".its_done:");
    emit(gen, "mov sp, x29");
    emit(gen, "ldp x29, x30, [sp], #16");
    emit(gen, "ret");
}

static void emit_higher_order_ops(CodeGen *gen) {
    // _builtin_map: x0 = arr ptr, x1 = closure ptr
    // Returns: pointer to new heap-allocated array with f applied to each element
    // Array layout: [length, elem0, elem1, ...]
    emit_raw(gen, "");
    emit_raw(gen, "// Builtin: map(arr, closure)");
    emit_raw(gen, "_builtin_map:");
    emit(gen, "stp x29, x30, [sp, #-16]!");
    emit(gen, "mov x29, sp");
    // Save callee-saved regs we'll use
    emit(gen, "stp x19, x20, [sp, #-16]!");  // x19=arr, x20=closure
    emit(gen, "stp x21, x22, [sp, #-16]!");  // x21=len, x22=result
    emit(gen, "stp x23, x24, [sp, #-16]!");  // x23=index, x24=fn_ptr
    emit(gen, "str x25, [sp, #-16]!");        // x25=env_ptr
    emit(gen, "mov x19, x0");               // x19 = arr ptr
    emit(gen, "mov x20, x1");               // x20 = closure ptr
    emit(gen, "ldr x21, [x19]");             // x21 = length
    // Allocate result array: (len+1)*8 bytes
    emit(gen, "add x0, x21, #1");
    emit(gen, "lsl x0, x0, #3");             // x0 = (len+1)*8
    emit(gen, "bl _builtin_alloc");
    emit(gen, "mov x22, x0");               // x22 = result ptr
    emit(gen, "str x21, [x22]");             // result[0] = length
    // Load closure fn_ptr and env_ptr
    emit(gen, "ldr x24, [x20]");             // x24 = fn_ptr
    emit(gen, "ldr x25, [x20, #8]");         // x25 = env_ptr
    // Loop
    emit(gen, "mov x23, #0");               // x23 = index = 0
    emit_raw(gen, ".Lmap_loop:");
    emit(gen, "cmp x23, x21");
    emit(gen, "b.ge .Lmap_done");
    // Load arr[index+1] (skip length header)
    emit(gen, "add x9, x23, #1");
    emit(gen, "ldr x1, [x19, x9, lsl #3]"); // x1 = arr[index+1]
    // Call closure: x0=env, x1=elem
    emit(gen, "mov x0, x25");               // x0 = env_ptr
    emit(gen, "blr x24");                   // result in x0
    // Store result[index+1]
    emit(gen, "add x9, x23, #1");
    emit(gen, "str x0, [x22, x9, lsl #3]"); // result[index+1] = f(elem)
    emit(gen, "add x23, x23, #1");
    emit(gen, "b .Lmap_loop");
    emit_raw(gen, ".Lmap_done:");
    emit(gen, "mov x0, x22");               // return result ptr
    // Restore callee-saved regs
    emit(gen, "ldr x25, [sp], #16");
    emit(gen, "ldp x23, x24, [sp], #16");
    emit(gen, "ldp x21, x22, [sp], #16");
    emit(gen, "ldp x19, x20, [sp], #16");
    emit(gen, "ldp x29, x30, [sp], #16");
    emit(gen, "ret");
    
    // _builtin_filter: x0 = arr ptr, x1 = closure ptr
    // Returns: pointer to new heap-allocated array with elements where f(elem) is truthy
    emit_raw(gen, "");
    emit_raw(gen, "// Builtin: filter(arr, closure)");
    emit_raw(gen, "_builtin_filter:");
    emit(gen, "stp x29, x30, [sp, #-16]!");
    emit(gen, "mov x29, sp");
    emit(gen, "stp x19, x20, [sp, #-16]!");  // x19=arr, x20=closure
    emit(gen, "stp x21, x22, [sp, #-16]!");  // x21=len, x22=result
    emit(gen, "stp x23, x24, [sp, #-16]!");  // x23=index, x24=fn_ptr
    emit(gen, "stp x25, x26, [sp, #-16]!");  // x25=env_ptr, x26=out_count
    emit(gen, "mov x19, x0");               // x19 = arr ptr
    emit(gen, "mov x20, x1");               // x20 = closure ptr
    emit(gen, "ldr x21, [x19]");             // x21 = length
    // Allocate result array: (len+1)*8 bytes (worst case all pass)
    emit(gen, "add x0, x21, #1");
    emit(gen, "lsl x0, x0, #3");
    emit(gen, "bl _builtin_alloc");
    emit(gen, "mov x22, x0");               // x22 = result ptr
    // Load closure fn_ptr and env_ptr
    emit(gen, "ldr x24, [x20]");             // x24 = fn_ptr
    emit(gen, "ldr x25, [x20, #8]");         // x25 = env_ptr
    // Loop
    emit(gen, "mov x23, #0");               // x23 = index = 0
    emit(gen, "mov x26, #0");               // x26 = out_count = 0
    emit_raw(gen, ".Lfilter_loop:");
    emit(gen, "cmp x23, x21");
    emit(gen, "b.ge .Lfilter_done");
    // Load arr[index+1]
    emit(gen, "add x9, x23, #1");
    emit(gen, "ldr x1, [x19, x9, lsl #3]"); // x1 = arr[index+1]
    // Save the element value for potential storage
    emit(gen, "str x1, [sp, #-16]!");        // push element
    // Call closure: x0=env, x1=elem
    emit(gen, "mov x0, x25");               // x0 = env_ptr
    emit(gen, "blr x24");                   // result in x0
    // Pop saved element
    emit(gen, "ldr x9, [sp], #16");          // x9 = saved element
    // Check if f(elem) is truthy
    emit(gen, "cbz x0, .Lfilter_skip");
    // Store in result array
    emit(gen, "add x10, x26, #1");           // out_count + 1 (skip length header)
    emit(gen, "str x9, [x22, x10, lsl #3]"); // result[out_count+1] = elem
    emit(gen, "add x26, x26, #1");           // out_count++
    emit_raw(gen, ".Lfilter_skip:");
    emit(gen, "add x23, x23, #1");           // index++
    emit(gen, "b .Lfilter_loop");
    emit_raw(gen, ".Lfilter_done:");
    emit(gen, "str x26, [x22]");             // result[0] = out_count
    emit(gen, "mov x0, x22");               // return result ptr
    // Restore callee-saved regs
    emit(gen, "ldp x25, x26, [sp], #16");
    emit(gen, "ldp x23, x24, [sp], #16");
    emit(gen, "ldp x21, x22, [sp], #16");
    emit(gen, "ldp x19, x20, [sp], #16");
    emit(gen, "ldp x29, x30, [sp], #16");
    emit(gen, "ret");
    
    // _builtin_reduce: x0 = arr ptr, x1 = init, x2 = closure ptr
    // Returns: accumulated result
    emit_raw(gen, "");
    emit_raw(gen, "// Builtin: reduce(arr, init, closure)");
    emit_raw(gen, "_builtin_reduce:");
    emit(gen, "stp x29, x30, [sp, #-16]!");
    emit(gen, "mov x29, sp");
    emit(gen, "stp x19, x20, [sp, #-16]!");  // x19=arr, x20=closure
    emit(gen, "stp x21, x22, [sp, #-16]!");  // x21=len, x22=acc
    emit(gen, "stp x23, x24, [sp, #-16]!");  // x23=index, x24=fn_ptr
    emit(gen, "str x25, [sp, #-16]!");        // x25=env_ptr
    emit(gen, "mov x19, x0");               // x19 = arr ptr
    emit(gen, "mov x22, x1");               // x22 = acc = init
    emit(gen, "mov x20, x2");               // x20 = closure ptr
    emit(gen, "ldr x21, [x19]");             // x21 = length
    // Load closure fn_ptr and env_ptr
    emit(gen, "ldr x24, [x20]");             // x24 = fn_ptr
    emit(gen, "ldr x25, [x20, #8]");         // x25 = env_ptr
    // Loop
    emit(gen, "mov x23, #0");               // x23 = index = 0
    emit_raw(gen, ".Lreduce_loop:");
    emit(gen, "cmp x23, x21");
    emit(gen, "b.ge .Lreduce_done");
    // Load arr[index+1]
    emit(gen, "add x9, x23, #1");
    emit(gen, "ldr x2, [x19, x9, lsl #3]"); // x2 = arr[index+1]
    // Call closure: x0=env, x1=acc, x2=elem
    emit(gen, "mov x0, x25");               // x0 = env_ptr
    emit(gen, "mov x1, x22");               // x1 = acc
    emit(gen, "blr x24");                   // result in x0
    emit(gen, "mov x22, x0");               // acc = result
    emit(gen, "add x23, x23, #1");           // index++
    emit(gen, "b .Lreduce_loop");
    emit_raw(gen, ".Lreduce_done:");
    emit(gen, "mov x0, x22");               // return acc
    // Restore callee-saved regs
    emit(gen, "ldr x25, [sp], #16");
    emit(gen, "ldp x23, x24, [sp], #16");
    emit(gen, "ldp x21, x22, [sp], #16");
    emit(gen, "ldp x19, x20, [sp], #16");
    emit(gen, "ldp x29, x30, [sp], #16");
    emit(gen, "ret");
}

static void emit_bounds_check(CodeGen *gen) {
    // _bounds_check: x0 = index, x1 = array length
    // If index < 0 or index >= length, print error to stderr and exit(1)
    // Preserves x0 (index) on success
    emit_raw(gen, "");
    emit_raw(gen, "// Runtime: bounds check");
    emit_raw(gen, "_bounds_check:");
    // Fast path: check bounds without full frame setup
    emit(gen, "cmp x0, #0");
    emit(gen, "b.lt .bounds_fail_setup");
    emit(gen, "cmp x0, x1");
    emit(gen, "b.ge .bounds_fail_setup");
    emit(gen, "ret");                   // x0 preserved, return immediately
    
    // Slow path: bounds check failed - set up frame, print error, exit
    emit_raw(gen, ".bounds_fail_setup:");
    emit(gen, "stp x29, x30, [sp, #-16]!");
    emit(gen, "mov x29, sp");
    emit(gen, "sub sp, sp, #64");
    emit(gen, "str x0, [x29, #-8]");    // save index
    emit(gen, "str x1, [x29, #-16]");   // save length
    
    // Print "Error: array index out of bounds (index=" to stderr
    emit(gen, "mov x0, #2");            // stderr
    emit(gen, "adrp x1, .bounds_msg1");
    emit(gen, "add x1, x1, :lo12:.bounds_msg1");
    emit(gen, "mov x2, #40");
    emit(gen, "mov x8, #64");
    emit(gen, "svc #0");
    
    // Print the index value to stderr (inline int-to-string)
    emit(gen, "ldr x0, [x29, #-8]");
    emit(gen, "bl _put_int_stderr");
    
    // Print ", length=" to stderr
    emit(gen, "mov x0, #2");            // stderr
    emit(gen, "adrp x1, .bounds_msg2");
    emit(gen, "add x1, x1, :lo12:.bounds_msg2");
    emit(gen, "mov x2, #9");
    emit(gen, "mov x8, #64");
    emit(gen, "svc #0");
    
    // Print the length value to stderr
    emit(gen, "ldr x0, [x29, #-16]");
    emit(gen, "bl _put_int_stderr");
    
    // Print ")\n" to stderr
    emit(gen, "mov x0, #41");           // ')'
    emit(gen, "strb w0, [sp]");
    emit(gen, "mov x0, #10");           // '\n'
    emit(gen, "strb w0, [sp, #1]");
    emit(gen, "mov x0, #2");            // stderr
    emit(gen, "mov x1, sp");
    emit(gen, "mov x2, #2");
    emit(gen, "mov x8, #64");
    emit(gen, "svc #0");
    
    // Exit with code 1
    emit(gen, "mov x0, #1");
    emit(gen, "mov x8, #93");
    emit(gen, "svc #0");
    
    // _put_int_stderr: print integer in x0 to stderr (no newline)
    emit_raw(gen, "");
    emit_raw(gen, "_put_int_stderr:");
    emit(gen, "stp x29, x30, [sp, #-16]!");
    emit(gen, "mov x29, sp");
    emit(gen, "sub sp, sp, #32");
    emit(gen, "mov x9, x0");
    emit(gen, "mov x10, #0");           // negative flag
    emit(gen, "cmp x9, #0");
    emit(gen, "b.ge .stderr_pos");
    emit(gen, "mov x10, #1");
    emit(gen, "neg x9, x9");
    emit_raw(gen, ".stderr_pos:");
    emit(gen, "mov x11, sp");
    emit(gen, "mov x13, #10");
    emit_raw(gen, ".stderr_loop:");
    emit(gen, "udiv x14, x9, x13");
    emit(gen, "msub x15, x14, x13, x9");
    emit(gen, "add x15, x15, #48");
    emit(gen, "strb w15, [x11, #-1]!");
    emit(gen, "mov x9, x14");
    emit(gen, "cbnz x9, .stderr_loop");
    emit(gen, "cbz x10, .stderr_write");
    emit(gen, "mov x12, #45");          // '-'
    emit(gen, "strb w12, [x11, #-1]!");
    emit_raw(gen, ".stderr_write:");
    emit(gen, "mov x0, #2");            // stderr (fd 2)
    emit(gen, "mov x1, x11");
    emit(gen, "mov x2, sp");
    emit(gen, "sub x2, x2, x11");
    emit(gen, "mov x8, #64");
    emit(gen, "svc #0");
    emit(gen, "mov sp, x29");
    emit(gen, "ldp x29, x30, [sp], #16");
    emit(gen, "ret");
}

static void emit_start(CodeGen *gen) {
    emit_raw(gen, "");
    emit_raw(gen, "_start:");
    emit(gen, "bl main");
    emit(gen, "mov x8, #93");          // sys_exit on arm64
    emit(gen, "svc #0");
}

static void emit_data_section(CodeGen *gen) {
    emit_raw(gen, "");
    emit_raw(gen, ".data");
    
    // User string literals
    for (int i = 0; i < gen->string_count; i++) {
        emit_raw(gen, ".str%d:", gen->strings[i].label);
        fprintf(gen->out, "    .ascii \"");
        // Escape special characters
        for (int j = 0; j < gen->strings[i].length; j++) {
            char c = gen->strings[i].value[j];
            switch (c) {
                case '\n': fprintf(gen->out, "\\n"); break;
                case '\t': fprintf(gen->out, "\\t"); break;
                case '\r': fprintf(gen->out, "\\r"); break;
                case '\0': fprintf(gen->out, "\\0"); break;
                case '\\': fprintf(gen->out, "\\\\"); break;
                case '"':  fprintf(gen->out, "\\\""); break;
                default:   fputc(c, gen->out); break;
            }
        }
        fprintf(gen->out, "\"\n");
    }
    
    // Runtime error messages
    emit_raw(gen, ".bounds_msg1:");
    emit_raw(gen, "    .ascii \"Error: array index out of bounds (index=\"");
    emit_raw(gen, ".bounds_msg2:");
    emit_raw(gen, "    .ascii \", length=\"");
}

void codegen_init(CodeGen *gen, FILE *out) {
    gen->out = out;
    gen->label_count = 0;
    gen->stack_offset = 0;
    gen->local_count = 0;
    gen->string_count = 0;
    gen->struct_count = 0;
    gen->enum_count = 0;
    gen->loop_depth = 0;
    gen->closure_count = 0;
}

void codegen_program(CodeGen *gen, Program *prog) {
    // Store struct definitions for field lookup
    for (int i = 0; i < prog->struct_count; i++) {
        if (gen->struct_count >= 64) {
            fprintf(stderr, "Error: too many struct definitions (max 64)\n");
            exit(1);
        }
        StructDef *sd = prog->structs[i];
        gen->structs[gen->struct_count].name = my_strdup(sd->name);
        gen->structs[gen->struct_count].field_names = malloc(sizeof(char*) * sd->field_count);
        for (int j = 0; j < sd->field_count; j++) {
            gen->structs[gen->struct_count].field_names[j] = my_strdup(sd->field_names[j]);
        }
        gen->structs[gen->struct_count].field_count = sd->field_count;
        gen->struct_count++;
    }
    
    // Store enum definitions for variant value lookup
    for (int i = 0; i < prog->enum_count; i++) {
        if (gen->enum_count >= 64) {
            fprintf(stderr, "Error: too many enum definitions (max 64)\n");
            exit(1);
        }
        EnumDef *ed = prog->enums[i];
        gen->enums[gen->enum_count].name = my_strdup(ed->name);
        gen->enums[gen->enum_count].variant_names = malloc(sizeof(char*) * ed->variant_count);
        gen->enums[gen->enum_count].variant_has_data = malloc(sizeof(int) * ed->variant_count);
        for (int j = 0; j < ed->variant_count; j++) {
            gen->enums[gen->enum_count].variant_names[j] = my_strdup(ed->variant_names[j]);
            gen->enums[gen->enum_count].variant_has_data[j] = ed->variant_has_data[j];
        }
        gen->enums[gen->enum_count].variant_count = ed->variant_count;
        gen->enum_count++;
    }
    
    // Header
    emit_raw(gen, "// Generated by Teddy Compiler");
    emit_raw(gen, "// Assemble: as -o out.o out.s");
    emit_raw(gen, "// Link:     ld -o out out.o");
    emit_raw(gen, "");
    emit_raw(gen, ".global _start");
    emit_raw(gen, ".text");
    
    // Runtime
    emit_runtime(gen);
    emit_print_char(gen);
    emit_print_str(gen);
    emit_print_int_raw(gen);
    emit_file_io(gen);
    emit_alloc_and_strings(gen);
    emit_higher_order_ops(gen);
    emit_bounds_check(gen);
    
    // Functions
    for (int i = 0; i < prog->function_count; i++) {
        codegen_function(gen, prog->functions[i]);
    }
    
    // Closure functions (generated during function compilation)
    emit_closure_functions(gen);
    
    // Entry point
    emit_start(gen);
    
    // Data section with string literals
    emit_data_section(gen);
}
