/*
 * Teddy Type System Implementation
 */

#include <stdio.h>
#include <string.h>

#include "type.h"

Type type_error(void) {
    Type type = { TYPE_ERROR, NULL };
    return type;
}

Type type_unknown(void) {
    Type type = { TYPE_UNKNOWN, NULL };
    return type;
}

Type type_void(void) {
    Type type = { TYPE_VOID, NULL };
    return type;
}

Type type_int(void) {
    Type type = { TYPE_INT, NULL };
    return type;
}

Type type_bool(void) {
    Type type = { TYPE_BOOL, NULL };
    return type;
}

Type type_string(void) {
    Type type = { TYPE_STRING, NULL };
    return type;
}

Type type_array(void) {
    Type type = { TYPE_ARRAY, NULL };
    return type;
}

Type type_struct(const char *name) {
    Type type = { TYPE_STRUCT, name };
    return type;
}

Type type_enum(const char *name) {
    Type type = { TYPE_ENUM, name };
    return type;
}

Type type_function(const char *name) {
    Type type = { TYPE_FUNCTION, name };
    return type;
}

int type_equals(Type left, Type right) {
    if (left.kind != right.kind) return 0;
    if (left.kind == TYPE_STRUCT || left.kind == TYPE_ENUM || left.kind == TYPE_FUNCTION) {
        if (!left.name || !right.name) return left.name == right.name;
        return strcmp(left.name, right.name) == 0;
    }
    return 1;
}

int type_is_known(Type type) {
    return type.kind != TYPE_UNKNOWN && type.kind != TYPE_ERROR;
}

int type_is_truthy(Type type) {
    return type.kind == TYPE_BOOL || type.kind == TYPE_INT || type.kind == TYPE_UNKNOWN;
}

const char *type_describe(Type type, char *buffer, size_t buffer_size) {
    switch (type.kind) {
        case TYPE_ERROR:
            return "<error>";
        case TYPE_UNKNOWN:
            return "<unknown>";
        case TYPE_VOID:
            return "void";
        case TYPE_INT:
            return "int";
        case TYPE_BOOL:
            return "bool";
        case TYPE_STRING:
            return "string";
        case TYPE_ARRAY:
            return "array";
        case TYPE_STRUCT:
            if (type.name) return type.name;
            return "struct";
        case TYPE_ENUM:
            if (type.name) return type.name;
            return "enum";
        case TYPE_FUNCTION:
            if (type.name && buffer && buffer_size > 0) {
                snprintf(buffer, buffer_size, "fn %s", type.name);
                return buffer;
            }
            return "function";
    }
    return "<unknown>";
}
