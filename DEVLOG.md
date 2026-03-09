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

**Phase 1: Core Language** (DONE)
- [x] Functions, variables, basic types
- [x] Structs with fields
- [x] Arrays
- [x] Self-hosting compiler

**Phase 2: Expressive Types** (MOSTLY DONE)
- [x] Enums (simple)
- [x] Enums with data (ADTs)
- [x] Pattern matching (`match`)
- [x] Struct field type tracking (issue #2 fix)
- [ ] Generics (`Option<T>`, `Result<T,E>`, `Vec<T>`)
- [ ] Multi-field enum payloads (`Err(code, msg)`)
- [ ] Exhaustive match checking
- [ ] Tuple types (`(int, string)`)
- [ ] Type aliases (`type Point = { x: int, y: int }`)

**Phase 3: Language Ergonomics**
- [ ] `for` loops (`for i in 0..10`, `for x in array`)
- [ ] `else if` chains (no nesting needed)
- [ ] `break` / `continue`
- [ ] String interpolation (`"hello {name}"`)
- [ ] Closures / anonymous functions (`|x| x + 1`)
- [ ] First-class functions (pass functions as args)
- [ ] Methods on structs (`impl Point { fn distance(...) }`)
- [ ] Operator overloading
- [ ] Destructuring (`let (a, b) = pair;`, `let Point { x, y } = p;`)
- [ ] Default parameter values
- [ ] Named arguments

**Phase 4: Type System**
- [ ] Full type checker (enforce type annotations)
- [ ] Type inference (Hindley-Milner style)
- [ ] Traits / interfaces (`trait Printable { fn to_string(self) -> string }`)
- [ ] Trait bounds on generics (`fn sort<T: Ord>(arr: Vec<T>)`)
- [ ] Associated types
- [ ] Const generics (`Array<T, N>`)
- [ ] Sum types as first-class (not just enums)

**Phase 5: Ownership & Memory Safety**
- [ ] Linear types / move semantics (use-once, transfer ownership)
- [ ] Borrow checker (references with lifetime tracking)
- [ ] Region-based memory management
- [ ] `&T` (shared ref) and `&mut T` (unique ref)
- [ ] Automatic drop/destructor calls
- [ ] Stack vs heap analysis (escape analysis)
- [ ] Arena allocators as a language primitive
- [ ] No-GC guarantee with compile-time memory safety

**Phase 6: Effect System & Capabilities**
- [ ] Effect annotations (`fn foo() with IO`)
- [ ] Pure functions (no effects) - compiler enforced
- [ ] Effect inference
- [ ] Effect polymorphism (`fn map<F, E>(f: F) with E`)
- [ ] Effect handlers (algebraic effects a la Koka/Eff)
- [ ] Capability-based security (`fn serve(cap: NetCap)`)
- [ ] Effect rows / sets
- [ ] Resumable effects (delimited continuations)
- [ ] Built-in effects: `IO`, `Alloc`, `Panic`, `Async`, `State<T>`

**Phase 7: Concurrency & Async**
- [ ] Async/await with effect tracking (`fn fetch() with Async, IO`)
- [ ] Structured concurrency (nurseries / task groups)
- [ ] Channels (typed, bounded)
- [ ] Actors (lightweight, message-passing)
- [ ] Data-race freedom via ownership (Send/Sync traits)
- [ ] Green threads / coroutines
- [ ] Parallel iterators

**Phase 8: Metaprogramming & Compile-Time**
- [ ] Compile-time constants (`const N = 10`)
- [ ] `comptime` blocks (evaluate arbitrary code at compile time)
- [ ] Compile-time reflection (inspect types, fields, methods)
- [ ] Hygienic macros (pattern-based)
- [ ] Procedural macros (code that generates code)
- [ ] Derive macros (`#[derive(Debug, Eq)]`)
- [ ] Custom DSLs via macro system
- [ ] Compile-time string processing (regex, SQL, etc.)

**Phase 9: Module System & Tooling**
- [ ] Multi-file compilation (`import math;`)
- [ ] Namespaces / module paths (`math::sqrt`)
- [ ] Visibility modifiers (`pub`, `priv`)
- [ ] Package manifest (`teddy.toml`)
- [ ] Package manager / registry
- [ ] Build system with dependency resolution
- [ ] Standard library (collections, IO, strings, math, net)
- [ ] FFI (call C functions, export to C)

**Phase 10: Developer Experience**
- [ ] Great error messages with source spans and suggestions
- [ ] LSP server (autocomplete, go-to-def, hover types)
- [ ] Formatter (`teddy fmt`)
- [ ] Linter (`teddy lint`)
- [ ] REPL / playground
- [ ] Documentation generator
- [ ] Test framework built-in (`teddy test`)
- [ ] Benchmarking framework
- [ ] Debugger integration (DWARF debug info)

**Phase 11: Backend & Performance**
- [ ] x86-64 backend (in addition to ARM64)
- [ ] LLVM backend (unlock optimizations)
- [ ] WebAssembly backend
- [ ] Incremental compilation
- [ ] Link-time optimization
- [ ] Compile-time evaluation / constant folding
- [ ] Inlining / devirtualization
- [ ] SIMD intrinsics

**Moonshot Ideas**
- [ ] Dependent types (types that depend on values)
- [ ] Proof system (lightweight verification, like Lean-lite)
- [ ] Hot code reloading
- [ ] GPU compute shaders as Teddy functions
- [ ] Formal memory safety proof (certified compiler)
- [ ] Capability-secure sandboxing (Wasm-style isolation)
- [ ] Self-modifying compilation (compiler learns from usage patterns)

---

**Near-term Goal: Self-hosting** - ~~Rewrite the compiler in Teddy itself.~~ **ACHIEVED!**

---

## Session 9 - Ergonomics Blitz (2026-03-09)

Closed 7 issues in one session. The language is starting to feel nice to write.

### Issues Fixed:

**#12 - Closures:** Full closure support with variable capture. `|x| x * 2` syntax for anonymous functions. Closures are represented as `[fn_ptr, env_ptr]` pairs. Environment capture copies values from the enclosing scope into a stack-allocated env struct. Closure bodies are lambda-lifted into top-level functions that receive the env as a hidden first parameter.

```teddy
let offset = 10;
let add = |x| x + offset;  // captures 'offset'
print add(5);               // 15

fn apply(f, x) { return f(x); }
print apply(|x| x * 2, 21); // 42
```

**#2 - Struct field scoping:** Added lightweight struct type tracking so two structs can share field names.

**#4 - Bounds checking:** Runtime array bounds checks on every read/write. Compile-time capacity checks on all fixed-size compiler tables.

**#5 - Memory leak:** Added `free(ptr, size)` builtin using `munmap`.

**#8 - For loops:** Both range (`for i in 0..10`) and array (`for x in arr`) iteration, with break/continue support. Desugars to while-loop equivalent in codegen.

**#9 - Else if:** Parser change only -- `else if` desugars to nested if/else.

**#10 - Break/continue:** New AST nodes + loop label stack in codegen. Nested loops work correctly (break only exits inner loop).

**#11 - Methods on structs:** `impl` blocks with method definitions. `p.distance(q)` desugars to `Point_distance(p, q)`. Methods can return structs and type tracking flows through.

### How methods work:

```teddy
struct Point { x, y }

impl Point {
    fn sum(self) { return self.x + self.y; }
    fn add(self, other) {
        return Point { x: self.x + other.x, y: self.y + other.y };
    }
}

fn main() {
    let p = Point { x: 3, y: 4 };
    print p.sum();       // 7
    let q = p.add(Point { x: 10, y: 20 });
    print q.x;           // 13
}
```

Under the hood: `impl Point { fn sum(self) { ... } }` creates a function named `Point_sum`. Method call `p.sum()` becomes `Point_sum(p)`. The struct type is inferred from the receiver variable's type tracking.

---

## Session 9 (earlier) - Issues #2, #4, #5 Fixed + Roadmap (2026-03-09)

### Issue #2: Struct field names must be globally unique - DONE

Field access (`p.x`) and field assignment (`p.x = 5`) searched ALL struct definitions for the field name. If two structs had a field with the same name, the wrong offset could be used:

```teddy
struct Foo { a, b, c }
struct Bar { c, b, a }  // Same fields, different order!

let b = Bar { c: 30, b: 20, a: 10 };
print b.a;  // BUG: found 'a' in Foo (offset 0), got 30 instead of 10!
```

**Fix:** Added lightweight struct type tracking to the code generator:

1. **`struct_type` field on locals** - Each variable in the symbol table now tracks what struct type it holds (or NULL if not a struct)
2. **Type inference** - `infer_struct_type()` determines the struct type from expressions (struct literals, variables, propagates through let/assign)
3. **Scoped field lookup** - `codegen_field_access` and `codegen_field_assign` check the inferred type first, falls back to global search for untyped cases

### Issue #5: read_file leaks 128KB memory per call - DONE

**Fix:** Added `free(ptr, size)` builtin that calls `munmap` to release mmap'd memory. This pairs with the existing `alloc(size)` builtin. Updated `file_test.teddy` to free its read buffer.

```teddy
let buf = alloc(4096);    // allocate with mmap
poke(buf, 42);            // use it
free(buf, 4096);          // release with munmap

let data = read_file("file.txt\0");
// ... use data ...
free(data, 131072);       // free the 128KB read buffer
```

### Issue #4: No bounds checking - DONE

**Runtime array bounds checking:**
- Array read (`arr[i]`) and write (`arr[i] = x`) now check `0 <= i < len(arr)` at runtime
- Out-of-bounds access prints a clear error to stderr and exits with code 1:
  ```
  Error: array index out of bounds (index=5, length=3)
  ```
- Fast path: just two compares + conditional branch (no overhead on success)
- Error path: prints index and length values, exits

**Compile-time capacity checks (C compiler internals):**
- Local variables: max 256 per function
- Scope depth: max 64 nested scopes
- String literals: max 256
- Struct definitions: max 64
- Enum definitions: max 64
- All now print clear error messages and exit instead of silently corrupting memory

### Files Changed:
- `src/codegen.h` - Added `struct_type` to locals table
- `src/codegen.c` - Struct type tracking, `free()` builtin, `_bounds_check` runtime, `_put_int_stderr` runtime, capacity checks on all fixed-size tables

### New Test Files:
- `examples/struct_shared_fields.teddy` - Two structs with same field names
- `examples/struct_field_order.teddy` - Same fields in different order (the killer case)
- `examples/free_test.teddy` - alloc + free in a loop (proves no leak)
- `examples/bounds_test.teddy` - Out of bounds read (should error)
- `examples/bounds_neg_test.teddy` - Negative index (should error)

### Roadmap Updated:
Expanded the feature roadmap from 5 phases to 11 phases + moonshots. Going wild.

---

## Session 8 - Variable Shadowing Fixed! (2026-02-22)

**Issue #3 is DONE!** Variable shadowing now works correctly in both compilers.

### The Fix:

**Problem:** Variables in inner scopes were not shadowing outer variables properly. The symbol table searched from index 0, so it always found the first (outermost) variable.

**Solution:** Implemented proper scope tracking with a scope stack:

1. **Reverse search in `sym_find`/`find_local`**: Search backwards from most recent variable to find innermost scope first

2. **Scope stack**: On block entry (`{`), push `sym_count` onto a stack. On block exit (`}`), restore `sym_count` to forget inner variables.

3. **Added to both compilers**:
   - C compiler: `push_scope()`/`pop_scope()` in `codegen.c`, scope_stack in CodeGen struct
   - Teddy compiler: `scope_push()`/`scope_pop()`, `cmp_scope_stack`/`cmp_scope_depth` fields

### Example that now works:

```teddy
fn main() {
    let x = 10;
    print x;      // 10
    if 1 {
        let x = 20;
        print x;  // 20
        if 1 {
            let x = 30;
            print x;  // 30
        }
        print x;  // 20 (inner x is gone)
    }
    print x;      // 10 (both inner x's are gone)
    return 0;
}
```

### Bootstrap Verified:
```
Stage 3 output == Stage 4 output (self-hosting confirmed!)
```

---

## Session 7 - Type & Effect Annotation Syntax Complete! (2026-02-22)

**Effect annotations now work!** The `with` keyword parsing bug is fixed.

### Bug Fixed:

**Variable shadowing struck again!** The `c2` variable in `ident_type()` was defined twice:
- Line 230: `let c2 = ...` (for checking 'false'/'fn')
- Line 244: `let c2 = ...` (for checking 'while'/'with')

Due to Teddy's global symbol table, the second `c2` was shadowed by the first, causing `with` to always be lexed as `TOK_IDENT` instead of `TOK_WITH`.

**Fix:** Renamed variables to unique names:
- `c` → `ident_c` (first character of identifier)
- `c2` → `f_c2` (second char in 'f...' branch)
- `c2` → `w_c2` (second char in 'w...' branch)

### New Syntax Working:

```teddy
// Parameter type annotations
fn add(a: int, b: int) -> int { return a + b; }

// Return type annotations
fn get_value() -> int { return 42; }

// Effect annotations (single)
fn greet() with IO { print 42; return 0; }

// Effect annotations (multiple)
fn process() with IO, Alloc { ... }

// Combined
fn compute(x: int) -> int with Pure { return x * 2; }

// Let type annotations
let x: int = 5;
```

### Bootstrap Verified:

```
Stage 1: C compiler → compiler.teddy → stage1
Stage 2: stage1 → compiler.teddy → stage2
Stage 3: stage2 → compiler.teddy → stage3
Result: diff stage2.s stage3.s → IDENTICAL!
```

**Note:** Types and effects are currently parsed but ignored. The infrastructure is in place for future type/effect checking.

---

## Session 6 - FULL SELF-HOSTING ACHIEVED! (2026-02-22)

**THE TEDDY COMPILER CAN NOW COMPILE ITSELF!**

This session fixed all remaining bugs preventing self-compilation:

### Bugs Fixed:

1. **Variable shadowing in Teddy compiler** - The Teddy compiler's symbol table is global, so duplicate variable names in different scopes interfere. Fixed by renaming ~50 variables to unique names:
   - `foff` → `foff_nested`, `foff_final`, `init_foff`
   - `end_label` → `and_end_lbl`, `or_end_lbl`, `if_end_lbl`, `wh_end_lbl`
   - `i` → `streq_idx`, `symfind_idx`, `nameis_idx`, etc.
   - `name_start`/`name_len` → `prim_name_start`, `stmt_name_start`, etc.
   - `arg_count` → `prim_arg_cnt`, `stmt_arg_cnt`

2. **Missing builtins in parse_statement** - `poke`, `str_copy`, and `write_file` were only recognized in expressions (`let x = poke(...)`) but not in statements (`poke(...);`). Added builtin handling for these.

3. **Stack frame too small** - Functions allocated only 256 bytes but the Compiler struct needs 27*8=216 bytes plus locals. Increased to 512 bytes.

### Bootstrap Verification:

```
Stage 1: C compiler → compiler.teddy → stage1.s
Stage 2: stage1 compiler → compiler.teddy → stage2.s
Result: diff stage1.s stage2.s → IDENTICAL!
```

The Teddy compiler is **fully self-hosting**!

### How to use:

```bash
# Build the compiler (using C implementation)
cd ~/teddy
./teddy teddy_src/compiler.teddy -o /tmp/compiler.s
as -o /tmp/compiler.o /tmp/compiler.s
ld -o /tmp/compiler /tmp/compiler.o

# Use the Teddy compiler
echo 'fn main() { print 42; return 0; }' > /tmp/simple.teddy
/tmp/compiler  # reads /tmp/simple.teddy, outputs /tmp/out.s
as -o /tmp/out.o /tmp/out.s
ld -o /tmp/out /tmp/out.o
/tmp/out  # prints: 42

# Self-compile! (bootstrap)
cp teddy_src/compiler.teddy /tmp/simple.teddy
/tmp/compiler  # outputs /tmp/out.s (the self-compiled compiler!)
```

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
