/*
 * Teddy Semantic Analysis
 * A minimal type checker for the bootstrap compiler.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sema.h"
#include "type.h"

typedef struct {
    Program *prog;
    int had_error;
    Function *current_function;
    Type current_return_type;

    struct {
        char *name;
        Type type;
    } locals[512];
    int local_count;

    int scope_stack[64];
    int scope_depth;
} Sema;

static Type resolve_annotation(Sema *sema, const char *name, int line);

static void sema_error(Sema *sema, int line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[line %d] Type error: ", line);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    sema->had_error = 1;
}

static StructDef *find_struct_def(Sema *sema, const char *name) {
    for (int i = 0; i < sema->prog->struct_count; i++) {
        if (strcmp(sema->prog->structs[i]->name, name) == 0) {
            return sema->prog->structs[i];
        }
    }
    return NULL;
}

static EnumDef *find_enum_def(Sema *sema, const char *name) {
    for (int i = 0; i < sema->prog->enum_count; i++) {
        if (strcmp(sema->prog->enums[i]->name, name) == 0) {
            return sema->prog->enums[i];
        }
    }
    return NULL;
}

static Function *find_function_def(Sema *sema, const char *name) {
    for (int i = 0; i < sema->prog->function_count; i++) {
        if (strcmp(sema->prog->functions[i]->name, name) == 0) {
            return sema->prog->functions[i];
        }
    }
    return NULL;
}

static int struct_has_field(StructDef *sd, const char *field_name) {
    for (int i = 0; i < sd->field_count; i++) {
        if (strcmp(sd->field_names[i], field_name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int enum_variant_index(EnumDef *ed, const char *variant_name) {
    for (int i = 0; i < ed->variant_count; i++) {
        if (strcmp(ed->variant_names[i], variant_name) == 0) {
            return i;
        }
    }
    return -1;
}

static int struct_field_index(StructDef *sd, const char *field_name) {
    for (int i = 0; i < sd->field_count; i++) {
        if (strcmp(sd->field_names[i], field_name) == 0) {
            return i;
        }
    }
    return -1;
}

static Type struct_field_type(Sema *sema, StructDef *sd, const char *field_name, int line) {
    int field_index = struct_field_index(sd, field_name);
    if (field_index < 0) return type_unknown();
    if (!sd->field_types || !sd->field_types[field_index]) return type_unknown();
    return resolve_annotation(sema, sd->field_types[field_index], line);
}

static Type enum_variant_payload_type(Sema *sema, EnumDef *ed, int variant_index, int line) {
    if (!ed->variant_payload_types || variant_index < 0) return type_unknown();
    if (!ed->variant_payload_types[variant_index]) return type_unknown();
    return resolve_annotation(sema, ed->variant_payload_types[variant_index], line);
}

static Type resolve_annotation(Sema *sema, const char *name, int line) {
    if (!name) return type_unknown();
    if (strcmp(name, "int") == 0) return type_int();
    if (strcmp(name, "bool") == 0) return type_bool();
    if (strcmp(name, "string") == 0) return type_string();
    if (strcmp(name, "void") == 0) return type_void();
    if (find_struct_def(sema, name)) return type_struct(name);
    if (find_enum_def(sema, name)) return type_enum(name);

    sema_error(sema, line, "unknown type '%s'", name);
    return type_error();
}

static void push_scope(Sema *sema) {
    if (sema->scope_depth >= 64) {
        fprintf(stderr, "Too many semantic scopes\n");
        exit(1);
    }
    sema->scope_stack[sema->scope_depth++] = sema->local_count;
}

static void pop_scope(Sema *sema) {
    sema->local_count = sema->scope_stack[--sema->scope_depth];
}

static void add_local(Sema *sema, const char *name, Type type) {
    if (sema->local_count >= 512) {
        fprintf(stderr, "Too many semantic locals\n");
        exit(1);
    }
    sema->locals[sema->local_count].name = (char *)name;
    sema->locals[sema->local_count].type = type;
    sema->local_count++;
}

static Type *find_local(Sema *sema, const char *name) {
    for (int i = sema->local_count - 1; i >= 0; i--) {
        if (strcmp(sema->locals[i].name, name) == 0) {
            return &sema->locals[i].type;
        }
    }
    return NULL;
}

static int types_compatible(Type expected, Type actual) {
    if (expected.kind == TYPE_ERROR || actual.kind == TYPE_ERROR) return 1;
    if (expected.kind == TYPE_UNKNOWN || actual.kind == TYPE_UNKNOWN) return 1;
    return type_equals(expected, actual);
}

static void report_type_mismatch(Sema *sema, int line, Type expected, Type actual,
                                 const char *context) {
    char expected_buf[64];
    char actual_buf[64];
    sema_error(sema, line, "%s: expected %s, got %s",
               context,
               type_describe(expected, expected_buf, sizeof(expected_buf)),
               type_describe(actual, actual_buf, sizeof(actual_buf)));
}

static int is_builtin_name(const char *name) {
    static const char *builtins[] = {
        "alloc", "free", "peek", "poke", "put_str", "put_char", "print_char",
        "char_at", "str_copy", "int_to_str", "read_file", "write_file",
        "map", "filter", "reduce", "len"
    };
    int count = (int)(sizeof(builtins) / sizeof(builtins[0]));
    for (int i = 0; i < count; i++) {
        if (strcmp(name, builtins[i]) == 0) return 1;
    }
    return 0;
}

static Type builtin_return_type(const char *name) {
    if (strcmp(name, "char_at") == 0) return type_int();
    if (strcmp(name, "int_to_str") == 0) return type_int();
    if (strcmp(name, "peek") == 0) return type_int();
    if (strcmp(name, "len") == 0) return type_int();
    if (strcmp(name, "alloc") == 0) return type_unknown();
    if (strcmp(name, "read_file") == 0) return type_unknown();
    return type_void();
}

static Type check_expr(Sema *sema, Expr *expr);
static void check_stmt(Sema *sema, Stmt *stmt);

static Type check_call(Sema *sema, Expr *expr) {
    CallExpr *call = &expr->as.call;

    for (int i = 0; i < call->arg_count; i++) {
        check_expr(sema, call->args[i]);
    }

    Type *local_type = find_local(sema, call->name);
    if (local_type && (local_type->kind == TYPE_FUNCTION || local_type->kind == TYPE_UNKNOWN)) {
        return type_unknown();
    }

    Function *target = find_function_def(sema, call->name);
    if (!target) {
        if (is_builtin_name(call->name)) {
            return builtin_return_type(call->name);
        }
        return type_unknown();
    }

    int used_params[256];
    for (int i = 0; i < target->param_count && i < 256; i++) {
        used_params[i] = 0;
    }

    for (int i = 0; i < call->arg_count; i++) {
        int param_index = i;
        if (call->arg_names && call->arg_names[i]) {
            param_index = -1;
            for (int j = 0; j < target->param_count; j++) {
                if (strcmp(call->arg_names[i], target->params[j]) == 0) {
                    param_index = j;
                    break;
                }
            }
            if (param_index < 0) {
                sema_error(sema, expr->line, "unknown parameter '%s' in call to %s",
                           call->arg_names[i], call->name);
                continue;
            }
        }

        if (param_index >= target->param_count) {
            sema_error(sema, expr->line, "too many arguments in call to %s", call->name);
            continue;
        }

        if (used_params[param_index]) {
            sema_error(sema, expr->line, "parameter '%s' provided multiple times in call to %s",
                       target->params[param_index], call->name);
            continue;
        }
        used_params[param_index] = 1;

        if (target->param_types && target->param_types[param_index]) {
            Type expected = resolve_annotation(sema, target->param_types[param_index], expr->line);
            Type actual = check_expr(sema, call->args[i]);
            if (!types_compatible(expected, actual)) {
                char context[128];
                snprintf(context, sizeof(context), "argument '%s' in call to %s",
                         target->params[param_index], call->name);
                report_type_mismatch(sema, expr->line, expected, actual, context);
            }
        }
    }

    for (int i = 0; i < target->param_count; i++) {
        if (!used_params[i] && (!target->defaults || !target->defaults[i])) {
            sema_error(sema, expr->line, "missing argument '%s' in call to %s",
                       target->params[i], call->name);
        }
    }

    if (target->return_type) {
        return resolve_annotation(sema, target->return_type, expr->line);
    }
    return type_unknown();
}

static Type check_method_call(Sema *sema, Expr *expr) {
    MethodCallExpr *call = &expr->as.method_call;
    Type object_type = check_expr(sema, call->object);

    for (int i = 0; i < call->arg_count; i++) {
        check_expr(sema, call->args[i]);
    }

    if (object_type.kind != TYPE_STRUCT) {
        if (object_type.kind != TYPE_UNKNOWN && object_type.kind != TYPE_ERROR) {
            char type_buf[64];
            sema_error(sema, expr->line, "method call requires a struct receiver, got %s",
                       type_describe(object_type, type_buf, sizeof(type_buf)));
        }
        return type_unknown();
    }

    char mangled[256];
    snprintf(mangled, sizeof(mangled), "%s_%s", object_type.name, call->method_name);
    Function *target = find_function_def(sema, mangled);
    if (!target) return type_unknown();

    int expected_args = target->param_count > 0 ? target->param_count - 1 : 0;
    if (call->arg_count > expected_args) {
        sema_error(sema, expr->line, "too many arguments in method call %s.%s",
                   object_type.name, call->method_name);
    }

    for (int i = 0; i < call->arg_count && i + 1 < target->param_count; i++) {
        if (target->param_types && target->param_types[i + 1]) {
            Type expected = resolve_annotation(sema, target->param_types[i + 1], expr->line);
            Type actual = check_expr(sema, call->args[i]);
            if (!types_compatible(expected, actual)) {
                char context[128];
                snprintf(context, sizeof(context), "argument %d in method call %s.%s",
                         i + 1, object_type.name, call->method_name);
                report_type_mismatch(sema, expr->line, expected, actual, context);
            }
        }
    }

    if (target->return_type) {
        return resolve_annotation(sema, target->return_type, expr->line);
    }
    return type_unknown();
}

static Type check_expr(Sema *sema, Expr *expr) {
    switch (expr->type) {
        case EXPR_INT_LITERAL:
            return type_int();
        case EXPR_BOOL_LITERAL:
            return type_bool();
        case EXPR_STRING_LITERAL:
        case EXPR_STRING_INTERP: {
            if (expr->type == EXPR_STRING_INTERP) {
                for (int i = 0; i < expr->as.string_interp.part_count; i++) {
                    check_expr(sema, expr->as.string_interp.parts[i]);
                }
            }
            return type_string();
        }
        case EXPR_ARRAY_LITERAL: {
            Type element_type = type_unknown();
            for (int i = 0; i < expr->as.array_literal.count; i++) {
                Type current = check_expr(sema, expr->as.array_literal.elements[i]);
                if (!type_is_known(element_type) && type_is_known(current)) {
                    element_type = current;
                } else if (type_is_known(element_type) && type_is_known(current) &&
                           !type_equals(element_type, current)) {
                    report_type_mismatch(sema, expr->line, element_type, current,
                                         "array literal element type");
                }
            }
            return type_array();
        }
        case EXPR_VARIABLE: {
            Type *local = find_local(sema, expr->as.variable.name);
            if (local) return *local;
            if (find_function_def(sema, expr->as.variable.name)) {
                return type_function(expr->as.variable.name);
            }
            sema_error(sema, expr->line, "undefined variable '%s'", expr->as.variable.name);
            return type_error();
        }
        case EXPR_BINARY: {
            Type left = check_expr(sema, expr->as.binary.left);
            Type right = check_expr(sema, expr->as.binary.right);
            switch (expr->as.binary.op) {
                case OP_ADD:
                case OP_SUB:
                case OP_MUL:
                case OP_DIV:
                case OP_MOD:
                    if (left.kind == TYPE_INT && right.kind == TYPE_INT) return type_int();
                    if (left.kind == TYPE_STRUCT && right.kind == TYPE_STRUCT &&
                        type_equals(left, right)) return left;
                    if (left.kind == TYPE_UNKNOWN || right.kind == TYPE_UNKNOWN) {
                        return type_unknown();
                    }
                    report_type_mismatch(sema, expr->line, type_int(), left,
                                         "arithmetic operands must be numeric or overloaded structs");
                    return type_error();
                case OP_EQ:
                case OP_NE:
                case OP_LT:
                case OP_LE:
                case OP_GT:
                case OP_GE:
                    if (type_is_known(left) && type_is_known(right) && !type_equals(left, right) &&
                        left.kind != TYPE_STRUCT && right.kind != TYPE_STRUCT) {
                        char context[128];
                        snprintf(context, sizeof(context), "comparison operands");
                        report_type_mismatch(sema, expr->line, left, right, context);
                    }
                    return type_bool();
                case OP_AND:
                case OP_OR:
                    if (!type_is_truthy(left)) {
                        char left_buf[64];
                        sema_error(sema, expr->line, "logical operand must be bool/int, got %s",
                                   type_describe(left, left_buf, sizeof(left_buf)));
                    }
                    if (!type_is_truthy(right)) {
                        char right_buf[64];
                        sema_error(sema, expr->line, "logical operand must be bool/int, got %s",
                                   type_describe(right, right_buf, sizeof(right_buf)));
                    }
                    return type_bool();
                default:
                    return type_unknown();
            }
        }
        case EXPR_UNARY: {
            Type operand = check_expr(sema, expr->as.unary.operand);
            if (expr->as.unary.op == OP_NEG) {
                if (operand.kind == TYPE_INT || operand.kind == TYPE_STRUCT ||
                    operand.kind == TYPE_UNKNOWN) {
                    return operand.kind == TYPE_UNKNOWN ? type_unknown() : operand;
                }
                report_type_mismatch(sema, expr->line, type_int(), operand,
                                     "unary '-' operand");
                return type_error();
            }
            if (expr->as.unary.op == OP_NOT) {
                if (!type_is_truthy(operand)) {
                    char operand_buf[64];
                    sema_error(sema, expr->line, "unary '!' expects bool/int, got %s",
                               type_describe(operand, operand_buf, sizeof(operand_buf)));
                }
                return type_bool();
            }
            return type_unknown();
        }
        case EXPR_CALL:
            return check_call(sema, expr);
        case EXPR_INDEX: {
            Type array = check_expr(sema, expr->as.index.array);
            Type index = check_expr(sema, expr->as.index.index);
            if (index.kind != TYPE_INT && index.kind != TYPE_UNKNOWN && index.kind != TYPE_ERROR) {
                report_type_mismatch(sema, expr->line, type_int(), index, "array index");
            }
            if (array.kind != TYPE_ARRAY && array.kind != TYPE_UNKNOWN && array.kind != TYPE_ERROR) {
                char array_buf[64];
                sema_error(sema, expr->line, "indexing requires an array, got %s",
                           type_describe(array, array_buf, sizeof(array_buf)));
            }
            return type_unknown();
        }
        case EXPR_STRUCT_LITERAL: {
            StructDef *sd = find_struct_def(sema, expr->as.struct_literal.struct_name);
            if (!sd) {
                sema_error(sema, expr->line, "unknown struct '%s'",
                           expr->as.struct_literal.struct_name);
                return type_error();
            }
            for (int i = 0; i < expr->as.struct_literal.field_count; i++) {
                if (!struct_has_field(sd, expr->as.struct_literal.field_names[i])) {
                    sema_error(sema, expr->line, "struct %s has no field '%s'",
                               sd->name, expr->as.struct_literal.field_names[i]);
                }
                Type field_value = check_expr(sema, expr->as.struct_literal.field_values[i]);
                Type expected = struct_field_type(sema, sd, expr->as.struct_literal.field_names[i],
                                                  expr->line);
                if (!types_compatible(expected, field_value)) {
                    char context[128];
                    snprintf(context, sizeof(context), "field '%s' in %s literal",
                             expr->as.struct_literal.field_names[i], sd->name);
                    report_type_mismatch(sema, expr->line, expected, field_value, context);
                }
            }
            return type_struct(sd->name);
        }
        case EXPR_FIELD_ACCESS: {
            Type object = check_expr(sema, expr->as.field_access.object);
            if (object.kind == TYPE_STRUCT) {
                StructDef *sd = find_struct_def(sema, object.name);
                if (sd && !struct_has_field(sd, expr->as.field_access.field_name)) {
                    sema_error(sema, expr->line, "struct %s has no field '%s'",
                               sd->name, expr->as.field_access.field_name);
                }
                if (sd) return struct_field_type(sema, sd, expr->as.field_access.field_name, expr->line);
                return type_unknown();
            }
            if (object.kind != TYPE_UNKNOWN && object.kind != TYPE_ERROR) {
                char object_buf[64];
                sema_error(sema, expr->line, "field access requires a struct, got %s",
                           type_describe(object, object_buf, sizeof(object_buf)));
            }
            return type_unknown();
        }
        case EXPR_ENUM_VARIANT: {
            EnumDef *ed = find_enum_def(sema, expr->as.enum_variant.enum_name);
            if (!ed) {
                sema_error(sema, expr->line, "unknown enum '%s'",
                           expr->as.enum_variant.enum_name);
                return type_error();
            }
            int variant_index = enum_variant_index(ed, expr->as.enum_variant.variant_name);
            if (variant_index < 0) {
                sema_error(sema, expr->line, "enum %s has no variant '%s'",
                           ed->name, expr->as.enum_variant.variant_name);
                return type_error();
            }
            if (expr->as.enum_variant.payload) {
                Type payload = check_expr(sema, expr->as.enum_variant.payload);
                Type expected = enum_variant_payload_type(sema, ed, variant_index, expr->line);
                if (!types_compatible(expected, payload)) {
                    char context[128];
                    snprintf(context, sizeof(context), "payload for %s::%s",
                             ed->name, expr->as.enum_variant.variant_name);
                    report_type_mismatch(sema, expr->line, expected, payload, context);
                }
            }
            if (expr->as.enum_variant.payload && !ed->variant_has_data[variant_index]) {
                sema_error(sema, expr->line, "variant %s::%s does not accept a payload",
                           ed->name, expr->as.enum_variant.variant_name);
            }
            if (!expr->as.enum_variant.payload && ed->variant_has_data[variant_index]) {
                sema_error(sema, expr->line, "variant %s::%s requires a payload",
                           ed->name, expr->as.enum_variant.variant_name);
            }
            return type_enum(ed->name);
        }
        case EXPR_MATCH: {
            Type value_type = check_expr(sema, expr->as.match.value);
            Type result_type = type_unknown();
            for (int i = 0; i < expr->as.match.arm_count; i++) {
                MatchArm *arm = expr->as.match.arms[i];
                EnumDef *ed = NULL;
                if (arm->enum_name) ed = find_enum_def(sema, arm->enum_name);
                else if (value_type.kind == TYPE_ENUM) ed = find_enum_def(sema, value_type.name);

                if (ed) {
                    int variant_index = enum_variant_index(ed, arm->variant_name);
                    if (variant_index < 0) {
                        sema_error(sema, expr->line, "enum %s has no variant '%s'",
                                   ed->name, arm->variant_name);
                    }
                }

                push_scope(sema);
                if (arm->binding_name) {
                    if (ed) {
                        int variant_index = enum_variant_index(ed, arm->variant_name);
                        add_local(sema, arm->binding_name,
                                  enum_variant_payload_type(sema, ed, variant_index, expr->line));
                    } else {
                        add_local(sema, arm->binding_name, type_unknown());
                    }
                }
                Type arm_type = check_expr(sema, arm->body);
                pop_scope(sema);

                if (!type_is_known(result_type) && type_is_known(arm_type)) {
                    result_type = arm_type;
                } else if (type_is_known(result_type) && type_is_known(arm_type) &&
                           !type_equals(result_type, arm_type)) {
                    report_type_mismatch(sema, expr->line, result_type, arm_type,
                                         "match arm result");
                }
            }
            return result_type;
        }
        case EXPR_METHOD_CALL:
            return check_method_call(sema, expr);
        case EXPR_CLOSURE: {
            push_scope(sema);
            for (int i = 0; i < expr->as.closure.param_count; i++) {
                add_local(sema, expr->as.closure.params[i], type_unknown());
            }
            if (expr->as.closure.body_expr) {
                check_expr(sema, expr->as.closure.body_expr);
            } else if (expr->as.closure.body_block) {
                check_stmt(sema, expr->as.closure.body_block);
            }
            pop_scope(sema);
            return type_function("<closure>");
        }
    }

    return type_unknown();
}

static void check_stmt(Sema *sema, Stmt *stmt) {
    switch (stmt->type) {
        case STMT_EXPR:
            check_expr(sema, stmt->as.expr.expr);
            break;
        case STMT_PRINT:
            check_expr(sema, stmt->as.print.expr);
            break;
        case STMT_LET: {
            Type initializer = check_expr(sema, stmt->as.let.initializer);
            Type declared = stmt->as.let.type_name
                ? resolve_annotation(sema, stmt->as.let.type_name, stmt->line)
                : type_unknown();
            if (stmt->as.let.type_name && !types_compatible(declared, initializer)) {
                report_type_mismatch(sema, stmt->line, declared, initializer,
                                     "let binding");
            }
            add_local(sema, stmt->as.let.name,
                      stmt->as.let.type_name ? declared : initializer);
            break;
        }
        case STMT_ASSIGN: {
            Type *slot = find_local(sema, stmt->as.assign.name);
            Type value = check_expr(sema, stmt->as.assign.value);
            if (!slot) {
                sema_error(sema, stmt->line, "undefined variable '%s' in assignment",
                           stmt->as.assign.name);
                break;
            }
            if (!types_compatible(*slot, value)) {
                report_type_mismatch(sema, stmt->line, *slot, value, "assignment");
            }
            break;
        }
        case STMT_INDEX_ASSIGN:
            check_expr(sema, stmt->as.index_assign.array);
            if (check_expr(sema, stmt->as.index_assign.index).kind != TYPE_INT) {
                Type index = check_expr(sema, stmt->as.index_assign.index);
                if (index.kind != TYPE_UNKNOWN && index.kind != TYPE_ERROR) {
                    report_type_mismatch(sema, stmt->line, type_int(), index, "array index");
                }
            }
            check_expr(sema, stmt->as.index_assign.value);
            break;
        case STMT_FIELD_ASSIGN: {
            Type object = check_expr(sema, stmt->as.field_assign.object);
            if (object.kind == TYPE_STRUCT) {
                StructDef *sd = find_struct_def(sema, object.name);
                if (sd && !struct_has_field(sd, stmt->as.field_assign.field_name)) {
                    sema_error(sema, stmt->line, "struct %s has no field '%s'",
                               sd->name, stmt->as.field_assign.field_name);
                }
                Type expected = sd
                    ? struct_field_type(sema, sd, stmt->as.field_assign.field_name, stmt->line)
                    : type_unknown();
                Type value = check_expr(sema, stmt->as.field_assign.value);
                if (!types_compatible(expected, value)) {
                    char context[128];
                    snprintf(context, sizeof(context), "field assignment to %s.%s",
                             object.name, stmt->as.field_assign.field_name);
                    report_type_mismatch(sema, stmt->line, expected, value, context);
                }
                break;
            }
            check_expr(sema, stmt->as.field_assign.value);
            break;
        }
        case STMT_BLOCK:
            push_scope(sema);
            for (int i = 0; i < stmt->as.block.count; i++) {
                check_stmt(sema, stmt->as.block.statements[i]);
            }
            pop_scope(sema);
            break;
        case STMT_IF:
            if (!type_is_truthy(check_expr(sema, stmt->as.if_stmt.condition))) {
                Type cond = check_expr(sema, stmt->as.if_stmt.condition);
                char cond_buf[64];
                sema_error(sema, stmt->line, "if condition must be bool/int, got %s",
                           type_describe(cond, cond_buf, sizeof(cond_buf)));
            }
            check_stmt(sema, stmt->as.if_stmt.then_branch);
            if (stmt->as.if_stmt.else_branch) check_stmt(sema, stmt->as.if_stmt.else_branch);
            break;
        case STMT_WHILE:
            if (!type_is_truthy(check_expr(sema, stmt->as.while_stmt.condition))) {
                Type cond = check_expr(sema, stmt->as.while_stmt.condition);
                char cond_buf[64];
                sema_error(sema, stmt->line, "while condition must be bool/int, got %s",
                           type_describe(cond, cond_buf, sizeof(cond_buf)));
            }
            check_stmt(sema, stmt->as.while_stmt.body);
            break;
        case STMT_FOR:
            push_scope(sema);
            if (stmt->as.for_stmt.iterable) {
                check_expr(sema, stmt->as.for_stmt.iterable);
                add_local(sema, stmt->as.for_stmt.var_name, type_unknown());
            } else {
                Type start = check_expr(sema, stmt->as.for_stmt.start);
                Type end = check_expr(sema, stmt->as.for_stmt.end);
                if (start.kind != TYPE_INT && start.kind != TYPE_UNKNOWN && start.kind != TYPE_ERROR) {
                    report_type_mismatch(sema, stmt->line, type_int(), start, "for-range start");
                }
                if (end.kind != TYPE_INT && end.kind != TYPE_UNKNOWN && end.kind != TYPE_ERROR) {
                    report_type_mismatch(sema, stmt->line, type_int(), end, "for-range end");
                }
                add_local(sema, stmt->as.for_stmt.var_name, type_int());
            }
            check_stmt(sema, stmt->as.for_stmt.body);
            pop_scope(sema);
            break;
        case STMT_RETURN: {
            Type value = stmt->as.return_stmt.value
                ? check_expr(sema, stmt->as.return_stmt.value)
                : type_void();
            if (type_is_known(sema->current_return_type) &&
                !types_compatible(sema->current_return_type, value)) {
                report_type_mismatch(sema, stmt->line, sema->current_return_type, value,
                                     "return type");
            }
            break;
        }
        case STMT_BREAK:
        case STMT_CONTINUE:
            break;
        case STMT_DESTRUCTURE: {
            Type initializer = check_expr(sema, stmt->as.destructure.initializer);
            if (initializer.kind == TYPE_STRUCT) {
                StructDef *sd = find_struct_def(sema, initializer.name);
                for (int i = 0; i < stmt->as.destructure.field_count; i++) {
                    if (sd && !struct_has_field(sd, stmt->as.destructure.field_names[i])) {
                        sema_error(sema, stmt->line, "struct %s has no field '%s'",
                                   sd->name, stmt->as.destructure.field_names[i]);
                    }
                    add_local(sema, stmt->as.destructure.field_names[i],
                              sd ? struct_field_type(sema, sd, stmt->as.destructure.field_names[i],
                                                     stmt->line)
                                 : type_unknown());
                }
            } else {
                for (int i = 0; i < stmt->as.destructure.field_count; i++) {
                    add_local(sema, stmt->as.destructure.field_names[i], type_unknown());
                }
            }
            break;
        }
    }
}

static void infer_method_self_type(Sema *sema, Function *fn) {
    if (fn->param_count == 0 || fn->param_types[0]) return;
    if (strcmp(fn->params[0], "self") != 0) return;

    char *underscore = strchr(fn->name, '_');
    if (!underscore) return;

    size_t prefix_len = (size_t)(underscore - fn->name);
    char buffer[128];
    if (prefix_len >= sizeof(buffer)) return;
    memcpy(buffer, fn->name, prefix_len);
    buffer[prefix_len] = '\0';

    if (find_struct_def(sema, buffer)) {
        fn->param_types[0] = strdup(buffer);
    }
}

static void check_function(Sema *sema, Function *fn) {
    sema->current_function = fn;
    sema->current_return_type = fn->return_type
        ? resolve_annotation(sema, fn->return_type, fn->body ? fn->body->line : 0)
        : type_unknown();
    sema->local_count = 0;
    sema->scope_depth = 0;

    infer_method_self_type(sema, fn);

    for (int i = 0; i < fn->param_count; i++) {
        Type param_type = fn->param_types && fn->param_types[i]
            ? resolve_annotation(sema, fn->param_types[i], fn->body ? fn->body->line : 0)
            : type_unknown();

        if (fn->defaults && fn->defaults[i]) {
            Type default_type = check_expr(sema, fn->defaults[i]);
            if (fn->param_types && fn->param_types[i] &&
                !types_compatible(param_type, default_type)) {
                char context[128];
                snprintf(context, sizeof(context), "default value for parameter '%s'", fn->params[i]);
                report_type_mismatch(sema, fn->defaults[i]->line, param_type, default_type, context);
            }
        }

        add_local(sema, fn->params[i], param_type);
    }

    check_stmt(sema, fn->body);
}

int sema_check_program(Program *prog) {
    Sema sema;
    memset(&sema, 0, sizeof(sema));
    sema.prog = prog;
    sema.current_return_type = type_unknown();

    for (int i = 0; i < prog->function_count; i++) {
        check_function(&sema, prog->functions[i]);
    }

    return !sema.had_error;
}
