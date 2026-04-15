/*
 * Teddy Type System
 * Minimal type representation for semantic analysis.
 */

#ifndef TEDDY_TYPE_H
#define TEDDY_TYPE_H

#include <stddef.h>

typedef enum {
    TYPE_ERROR,
    TYPE_UNKNOWN,
    TYPE_VOID,
    TYPE_INT,
    TYPE_BOOL,
    TYPE_STRING,
    TYPE_ARRAY,
    TYPE_STRUCT,
    TYPE_ENUM,
    TYPE_FUNCTION,
} TypeKind;

typedef struct Type Type;

struct Type {
    TypeKind kind;
    const char *name;
    Type *element;
};

Type type_error(void);
Type type_unknown(void);
Type type_void(void);
Type type_int(void);
Type type_bool(void);
Type type_string(void);
Type type_array(Type element);
Type type_struct(const char *name);
Type type_enum(const char *name);
Type type_function(const char *name);

int type_equals(Type left, Type right);
int type_is_known(Type type);
int type_is_truthy(Type type);
const char *type_describe(Type type, char *buffer, size_t buffer_size);

#endif // TEDDY_TYPE_H
