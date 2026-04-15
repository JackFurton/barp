# Barp 0.1

Barp 0.1 is a self-hosted native language for small CLI tools, compilers, and systems-flavored scripting.

This is not a "do everything" release. The goal is to make Barp genuinely pleasant for a narrow class of real programs, then expand from there.

## Product Shape

Barp 0.1 should feel good for:

- Command-line tools
- File and text transformers
- Parsers and interpreters
- Code generators
- Build and developer tooling
- Self-hosted compiler work

Barp 0.1 is not yet trying to be:

- A Rust replacement
- A large application language
- A network/server platform
- A language with a full ownership or effect system

## Barp Identity

Barp should lean into the things it already wants to be:

- Native code generation
- Manual memory and systems-level control
- Expressive syntax with structs, enums, and pattern matching
- Strong compile-time semantics
- A language good for writing language tools

Short version:

Barp is a language for making small, weird, fast native tools, especially compilers and CLI programs.

## 0.1 Requirements

Barp 0.1 must have:

- Reliable self-hosting
- A trustworthy core type checker
- Functions, variables, arrays, strings, structs, enums, and match
- Closures and methods
- File I/O and enough runtime support for real tools
- Multi-file modules
- Decent compiler errors
- A tiny but useful standard library

## 0.1 Non-Goals

These are explicitly out of scope for 0.1 unless they become unexpectedly cheap:

- Borrow checker
- Full effect system
- Trait system
- Rich generics everywhere
- Package registry
- Macro system beyond very early experiments
- Multiple backend targets as a release requirement

## Tiny Standard Library

Barp 0.1 should ship with enough standard support to write real tools without fighting the language:

- Strings
- Arrays
- Option and Result style enums
- File reads and writes
- Simple path and text helpers
- Basic formatting / conversions

If a feature does not help build small real tools, it is probably not a 0.1 priority.

## Release Gates

Before calling Barp 0.1 real, it should be able to build these programs in Barp itself:

1. A cleaner self-hosted compiler driver
2. A small formatter or linter for Teddy/Barp source
3. A JSON or config parser
4. A codegen or build-helper CLI
5. A small text/file processing utility

If Barp can do those well, it is real enough to matter.

## Roadmap

### Stage A: Semantic Core

- Strengthen the type checker
- Track array element types, struct field types, and enum payload types
- Improve match checking
- Reduce "unknown" type flows
- Make errors more specific and useful

### Stage B: Modules And Library

- Add multi-file compilation
- Add imports and name resolution across files
- Build the first tiny stdlib
- Split the self-hosted compiler into cleaner modules

### Stage C: Self-Hosted Tooling

- Make the Teddy/Barp compiler workflow reliable
- Dogfood Barp for compiler-adjacent tools
- Start replacing rough bootstrap-only workflows

### Stage D: Real Programs

- Write the first serious Barp CLIs
- Use them to shake out stdlib and language gaps
- Fix ergonomics based on real tool-building pain

### Stage E: Polish

- Better diagnostics
- Formatter
- Linter
- Documentation
- Friendlier compiler UX

## Decision Filter

When choosing what to build next, ask:

1. Does this help Barp write real native tools?
2. Does this strengthen the semantic core?
3. Does this improve self-hosting and dogfooding?
4. Would this make a first-time Barp program feel more real?

If the answer is "no" to all four, it is probably not a 0.1 priority.

## Current Best Bets

The highest-leverage next steps are:

- Better type checking
- Array and collection typing
- Module system
- Tiny stdlib
- Self-hosted compiler cleanup
- Better error reporting

That is enough to make Barp feel like a real language without losing the fun.
