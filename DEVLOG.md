# Teddy Compiler - Dev Log

A compiler for the Teddy language, written in C. Compiles to ARM64.

## Language Vision

**Teddy** aims to be an **expressive systems programming language** combining:
- **Systems-level control** - No GC, manual memory, compiles to native code
- **Expressive types** - Algebraic data types (enums with data), pattern matching, Option/Result
- **Effect system** - Track what functions can do (IO, allocate, panic) at compile time
- **Metaprogramming** - Compile-time macros and code generation

Think: Rust's expressiveness + effect tracking + powerful macros, but starting simple.

### Feature Roadmap

**Phase 1: Core Language (Current)**
- [x] Functions, variables, basic types
- [x] Structs with fields
- [x] Arrays
- [ ] Type annotations
- [ ] Better error messages

**Phase 2: Expressive Types**
- [x] Enums (simple)
- [x] Enums with data (ADTs)
- [x] Pattern matching (`match`)
- [x] Option<T> / Result<T,E> style enums (no generics yet, but pattern works)
- [ ] Generics

**Phase 3: Effect System**
- [ ] Effect annotations (`fn foo() with IO`)
- [ ] Pure functions (no effects)
- [ ] Effect inference
- [ ] Effect polymorphism

**Phase 4: Metaprogramming**
- [ ] Compile-time constants
- [ ] Simple macros
- [ ] Procedural macros
- [ ] Compile-time execution

**Phase 5: Polish**
- [ ] Module system
- [ ] Standard library
- [ ] Package manager
- [ ] Great error messages
- [ ] LSP server

---

**Near-term Goal: Self-hosting** - Rewrite the compiler in Teddy itself.

---

## Session 5 - File I/O, Bug Fixes, and Almost Self-Hosting! (2026-02-22)

**Completed:**
- [x] Fixed field access (`p.x`) - removed debug prints, works correctly
- [x] Fixed field assignment (`p.x = 5`) - works correctly
- [x] Added `read_file(filename)` builtin - uses mmap for 128KB buffer
- [x] Added `write_file(filename, content, len)` builtin
- [x] Fixed large stack offset bug - added `emit_ldr_fp`/`emit_str_fp` helpers for offsets > 255
- [x] Fixed stack allocation bug - increased from 256 to 512 bytes per function
- [x] Compiler now reads from file and writes assembly to file

**Self-hosting status:**
The Teddy compiler can now:
- ✅ Read source files (up to 128KB)
- ✅ Write assembly output to files
- ✅ Compile programs with functions, recursion, structs, arrays, strings
- ✅ Successfully compiles fibonacci, struct programs, etc.

**Blocker for self-hosting:**
The compiler source uses `&&` and `||` operators, but these aren't implemented in the self-hosted compiler yet. When it tries to compile itself, it hits:
```teddy
if c >= 65 && c <= 90 {  // parse error!
```

**Technical debt documented as GitHub issues:**
- #1: Fixed 512-byte stack allocation
- #2: Struct field names must be globally unique
- #3: Variable shadowing bug in C compiler
- #4: No bounds checking
- #5: read_file leaks 128KB per call
- #6: **Add && and || operators** (blocker!)

**Next steps:**
- [ ] Implement `&&` and `||` operators (issue #6)
- [ ] Self-compile the Teddy compiler!
- [ ] Fix technical debt issues

---

## Session 4 - Enums, Pattern Matching, and ADTs! (2026-02-22)

**Completed:**
- [x] Simple enum definitions (`enum Color { Red, Green, Blue }`)
- [x] Enum variant expressions (`Color::Red`)
- [x] Pattern matching (`match color { Red => 1, Green => 2, Blue => 3 }`)
- [x] **Enums with data (ADTs!)** (`enum Option { None, Some(value) }`)
- [x] **Variant payloads** (`Option::Some(42)`)
- [x] **Match bindings** (`Some(v) => v + 1`)

**How it works:**

*Simple enums* (no variants have data):
- Represented as integers (0, 1, 2, ...)
- `Color::Red` compiles to `mov x0, #0`

*Fat enums* (any variant has data):
- Represented as pointer to [tag (8 bytes)][payload (8 bytes)]
- `Option::Some(42)` allocates 16 bytes, stores tag=1 and payload=42
- `Option::None` also allocates 16 bytes (tag=0, payload unused)
- Match loads tag from pointer, compares, and extracts payload if bound

**Examples:**
```teddy
// Simple enum
enum Color { Red, Green, Blue }
let c = Color::Blue;
let x = match c { Red => 10, Green => 20, Blue => 30 };

// Enum with data (ADT)
enum Option { None, Some(value) }
let x = Option::Some(42);
let result = match x {
    None => 0,
    Some(v) => v + 1  // v is bound to 42
};
print result;  // prints 43
```

**Also this session - MAJOR PROGRESS ON SELF-HOSTING!**

New builtins:
- [x] `alloc(size)` - heap allocation using mmap
- [x] `str_copy(dest, src, len)` - copy bytes  
- [x] `int_to_str(dest, n)` - convert int to string, returns length
- [x] `peek(addr)` / `poke(addr, val)` - raw memory access

**SELF-HOSTING COMPILER IN TEDDY!**

The `teddy_src/compiler.teddy` file now contains a working compiler that can:
- ✅ Parse and compile functions with parameters
- ✅ Local variables with symbol table
- ✅ Full expression parsing with operator precedence
- ✅ Comparison operators
- ✅ If/else statements  
- ✅ While loops
- ✅ Function calls with multiple arguments
- ✅ **Recursive functions!**
- ✅ Generate complete ARM64 assembly with runtime
- ✅ Output runs and produces correct results!

**Test case - Recursive Fibonacci:**
```teddy
fn fib(n) { if n <= 1 { return n; } return fib(n - 1) + fib(n - 2); }
fn main() { print fib(10); return 0; }
```
Compiled by Teddy compiler written in Teddy → outputs `55` ✓

**What's still needed for full self-hosting:**
- [ ] Arrays and indexing
- [ ] Structs
- [ ] String literals
- [ ] More builtins (char_at, etc.)
- [ ] File I/O to write output to file
- [ ] Compile the compiler with itself!

**Next steps:**
- [ ] Exhaustiveness checking for match
- [ ] Multiple payload fields (`Result { Ok(value), Err(code, msg) }`)
- [ ] Generics (`Option<T>`)

---

## Session 3 - LEXER IN TEDDY! (2026-02-21)

**Completed:**
- [x] Struct definitions (`struct Point { x, y }`)
- [x] Struct instantiation (`let p = Point { x: 1, y: 2 }`)
- [x] Field access (`p.x`)
- [x] Field assignment (`p.x = 5`)
- [x] `char_at(string, index)` builtin for string character access
- [x] Fixed file I/O (read_file, write_file)
- [x] Fixed `\0` escape sequence parsing
- [x] Fixed struct allocation bug (now uses local area, not stack growth)
- [x] Fixed parser bug with `if variable {` being parsed as struct literal
- [x] **WROTE LEXER IN TEDDY!** (`teddy_src/lexer.teddy`)

**Major milestone: First self-hosting component!**
The lexer is now implemented in Teddy itself. It successfully tokenizes:
```
fn main() { let x = 10; print x; }
```
Output: `28 27 0 1 2 29 27 15 25 6 35 27 6 3 38` (token types)

**Bug fixes this session:**
1. Parser: `IDENT {` was being parsed as struct literal even for lowercase identifiers
   - Fix: Only treat as struct literal if identifier starts with uppercase A-Z
2. Struct allocation: Structs were allocated below sp, getting clobbered by function calls
   - Fix: Allocate structs in the local variable area (negative offsets from x29)
3. Field name collision: Multiple structs with same field name caused wrong offsets
   - Workaround: Use unique field names (e.g., `tok_type` instead of `type`)

**Test programs:**
- `teddy_src/lexer.teddy` - Full lexer implementation (~300 lines)
- `structs.teddy`, `struct_nested.teddy`, etc.

**Technical notes:**
- Structs are passed by pointer (the value stored in local var is a pointer)
- Field lookup searches all structs - field names should be unique across structs
- Struct literals are now allocated at compile-time-known offsets from frame pointer

---

## Session 2 - Strings & Arrays! (2026-02-20)

**Completed:**
- [x] Assignment statements (`x = 5`)
- [x] String literals in lexer
- [x] String literal AST nodes
- [x] String parsing with escape sequences
- [x] String data section in assembly
- [x] print_str runtime function
- [x] Array literals (`[1, 2, 3]`)
- [x] Array indexing (`arr[0]`)

**New working features:**
- Assignment (`x = 5;`)
- String literals (`"hello"`)
- Escape sequences (`\n`, `\t`, `\\`, `\"`)
- Print strings (`print "Hello, World!";`)
- Array literals (`let arr = [1, 2, 3];`)
- Array indexing (`arr[0]`)

**Test programs:**
- `hello.teddy` - prints 30 (10 + 20)
- `fib.teddy` - prints 55 (fib(10))
- `loop.teddy` - prints 45 (sum 0..9)
- `strings.teddy` - prints "Hello, World!" etc.
- `arrays.teddy` - prints 10, 30, 50, 150

---

## Session 1 - Project Kickoff (2026-02-20)

**Completed:**
- [x] Project setup
- [x] Lexer (tokenizer) 
- [x] Parser (recursive descent)
- [x] AST (expressions + statements)
- [x] Code generation (ARM64)
- [x] Runtime (print_int)

**Working features:**
- Functions with parameters
- Variables (`let x = 10`)
- Arithmetic (`+`, `-`, `*`, `/`, `%`)
- Comparisons (`<`, `<=`, `>`, `>=`, `==`, `!=`)
- Boolean ops (`&&`, `||`, `!`)
- If/else statements
- While loops
- Recursion
- Print statement

---

## Roadmap to Self-Hosting

### Phase 1: Core Language Features
- [x] Assignment (`x = 5`)
- [x] Strings (`let s = "hello"`)
- [x] String printing (`print "hello"`)
- [x] Arrays (`let a = [1, 2, 3]`)
- [x] Array indexing (`a[0]`)
- [x] Array assignment (`a[0] = 5`)
- [x] Len operator (`len(a)`)

### Phase 2: Structs & Memory
- [x] Struct definitions (`struct Token { type, start, len }`)
- [x] Struct instantiation (`let t = Token { ... }`)
- [x] Field access (`t.type`)
- [x] Field assignment (`t.type = 5`)
- [ ] Heap allocation (malloc/free or GC) - structs on stack for now

### Phase 3: Standard Library
- [x] File I/O (read_file, write_file)
- [x] String operations (char_at)
- [ ] String operations (concat, substring) - not needed yet
- [ ] Character operations - implemented in Teddy (is_alpha, is_digit)

### Phase 4: Self-Hosting
- [x] Write lexer in Teddy - **DONE!**
- [ ] Write parser in Teddy
- [ ] Write codegen in Teddy
- [ ] Compile the Teddy compiler with itself!

---

## Architecture

```
source.teddy → Lexer → Tokens → Parser → AST → Codegen → ARM64 asm → binary
```

**Source files:**
- `src/main.c` - CLI entry point
- `src/lexer.c` - Tokenizer
- `src/parser.c` - Recursive descent parser
- `src/ast.c` - AST node constructors
- `src/codegen.c` - ARM64 code generator

---

## Notes

- Keep it simple, get things working end-to-end
- Test each feature before moving on
- ARM64 (aarch64) target - running on EC2 Graviton
