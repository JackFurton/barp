# Teddy Compiler Makefile

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g
SRCDIR = src
BUILDDIR = build

SOURCES = $(SRCDIR)/main.c $(SRCDIR)/lexer.c $(SRCDIR)/ast.c $(SRCDIR)/parser.c $(SRCDIR)/codegen.c
OBJECTS = $(BUILDDIR)/main.o $(BUILDDIR)/lexer.o $(BUILDDIR)/ast.o $(BUILDDIR)/parser.o $(BUILDDIR)/codegen.o
TARGET = teddy

.PHONY: all clean test tokens run

all: $(BUILDDIR) $(TARGET)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILDDIR) $(TARGET) examples/*.s examples/*.asm out out.o

test: $(TARGET)
	@echo "=== Compiling hello.teddy ==="
	./$(TARGET) examples/hello.teddy -o examples/hello.s
	@echo ""
	@echo "=== Assembling (ARM64) ==="
	as -o out.o examples/hello.s
	ld -o out out.o
	@echo ""
	@echo "=== Running ==="
	./out

tokens: $(TARGET)
	./$(TARGET) --tokens examples/hello.teddy

ast: $(TARGET)
	./$(TARGET) --ast examples/hello.teddy
