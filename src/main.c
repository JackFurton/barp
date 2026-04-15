/*
 * Teddy Compiler
 * A simple compiler for the Teddy language, targeting x86-64.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "codegen.h"
#include "sema.h"

static char *read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "Could not open file '%s'\n", path);
        exit(1);
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);

    char *buffer = malloc(size + 1);
    if (!buffer) {
        fprintf(stderr, "Not enough memory to read '%s'\n", path);
        exit(1);
    }

    size_t read = fread(buffer, 1, size, file);
    buffer[read] = '\0';

    fclose(file);
    return buffer;
}

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s <source.teddy> [-o output.asm]\n", prog);
    fprintf(stderr, "       %s --tokens <source.teddy>\n", prog);
    fprintf(stderr, "       %s --ast <source.teddy>\n", prog);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    int tokens_only = 0;
    int ast_only = 0;
    const char *source_path = NULL;
    const char *output_path = NULL;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--tokens") == 0) {
            tokens_only = 1;
        } else if (strcmp(argv[i], "--ast") == 0) {
            ast_only = 1;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_path = argv[++i];
        } else if (argv[i][0] != '-') {
            source_path = argv[i];
        }
    }

    if (!source_path) {
        print_usage(argv[0]);
        return 1;
    }

    char *source = read_file(source_path);

    Lexer lexer;
    lexer_init(&lexer, source);

    if (tokens_only) {
        printf("=== Teddy Compiler ===\n");
        printf("Tokenizing: %s\n\n", source_path);
        printf("--- Tokens ---\n");
        Token token;
        do {
            token = lexer_next_token(&lexer);
            token_print(&token);
        } while (token.type != TOKEN_EOF);
        free(source);
        return 0;
    }

    // Parse
    Parser parser;
    parser_init(&parser, &lexer);
    Program *program = parser_parse(&parser);

    if (parser.had_error) {
        fprintf(stderr, "Compilation failed due to errors.\n");
        program_free(program);
        free(source);
        return 1;
    }

    if (ast_only) {
        printf("=== Teddy Compiler ===\n");
        printf("Parsing: %s\n\n", source_path);
        ast_print_program(program);
        program_free(program);
        free(source);
        return 0;
    }

    if (!sema_check_program(program)) {
        fprintf(stderr, "Compilation failed due to semantic errors.\n");
        program_free(program);
        free(source);
        return 1;
    }

    // Code generation
    // Default output path: replace .teddy with .asm
    char default_output[256];
    if (!output_path) {
        strncpy(default_output, source_path, sizeof(default_output) - 5);
        char *dot = strrchr(default_output, '.');
        if (dot) {
            strcpy(dot, ".asm");
        } else {
            strcat(default_output, ".asm");
        }
        output_path = default_output;
    }

    FILE *out = fopen(output_path, "w");
    if (!out) {
        fprintf(stderr, "Could not open output file '%s'\n", output_path);
        program_free(program);
        free(source);
        return 1;
    }

    CodeGen gen;
    codegen_init(&gen, out);
    codegen_program(&gen, program);
    fclose(out);

    printf("=== Teddy Compiler ===\n");
    printf("Compiled: %s -> %s\n", source_path, output_path);
    printf("\nTo build executable:\n");
    printf("  nasm -f elf64 -o out.o %s\n", output_path);
    printf("  ld -o out out.o\n");
    printf("  ./out\n");

    program_free(program);
    free(source);
    return 0;
}
