# TinyIntegerExpr

TinyIntegerExpr is a small integer-only recursive descent parser and evaluator.
It is intended for C, C++, embedded projects, and Arduino-style builds that need
runtime integer expressions without floating-point math.

## Features

- C99 core with a C++17-clean header/source.
- Single source file and header file.
- Integer arithmetic, bitwise operators, shifts, variables, and custom functions.
- Explicit parse/runtime status reporting; no NaN sentinel values.
- Safe built-in integer operators: divide-by-zero, modulo-by-zero, bad shifts,
  and signed overflow are reported instead of invoking undefined behavior.
- Optional compile-time node/depth limits and caller-provided allocators.
- `printf`-based tree debug output can be removed with `TIE_DISABLE_PRINT`.

## Building

Add `tinyintegerexpr.c` and `tinyintegerexpr.h` to your project. For Arduino,
compile `tinyintegerexpr.c` as C, include `tinyintegerexpr.h` from your sketch or
C++ code, and prefer the status-returning APIs shown below.

The default expression limits are:

```c
#define TIE_DEFAULT_MAX_NODES 128u
#define TIE_DEFAULT_MAX_DEPTH 32u
```

Override them at compile time or pass `tie_options` to `tie_compile_ex()` /
`tie_interp_status()`.

## Short Example

```c
#include "tinyintegerexpr.h"
#include <stdio.h>

int main(void) {
    int err = 0;
    tie_status status = TIE_OK;
    int value = tie_interp_status("5 * (2 + 3)", &err, &status, NULL);

    if (status == TIE_OK) {
        printf("%d\n", value); /* Prints 25. */
    }
}
```

## API

```c
int tie_interp(const char *expression, int *error);
int tie_interp_status(const char *expression, int *error,
                      tie_status *status, const tie_options *options);

tie_expression *tie_compile(const char *expression,
                            const tie_variable *variables,
                            int var_count, int *error);
tie_expression *tie_compile_ex(const char *expression,
                               const tie_variable *variables,
                               int var_count, int *error,
                               tie_status *status,
                               const tie_options *options);

int tie_eval(const tie_expression *expr);
int tie_eval_status(const tie_expression *expr, tie_status *status);

void tie_free(tie_expression *expr);
void tie_free_ex(tie_expression *expr, const tie_allocator *allocator);

const char *tie_status_message(tie_status status);
```

The compatibility functions `tie_interp()`, `tie_compile()`, and `tie_eval()`
return `0` on runtime error. Use the `_status` variants when `0` is a valid
result or when you need to distinguish parse, allocation, limit, and runtime
errors.

## Variables

```c
int x = 3;
int y = 4;

tie_variable vars[] = {
    TIE_VAR("x", &x),
    TIE_VAR("y", &y),
};

int err = 0;
tie_status status = TIE_OK;
tie_expression *expr = tie_compile_ex("x * 2 + y", vars, 2, &err, &status, NULL);

if (expr != NULL) {
    int value = tie_eval_status(expr, &status); /* 10 */
    tie_free(expr);
}
```

## Custom Functions

Custom functions must use `int` arguments and return `int`.

```c
static int my_sum(int a, int b) {
    return a + b;
}

tie_variable vars[] = {
    TIE_FN2("mysum", my_sum),
};

tie_expression *expr = tie_compile("mysum(5, 6)", vars, 1, NULL);
```

Closures receive the configured context pointer as their first argument:

```c
static int plus_context(void *context, int value) {
    return *((int *) context) + value;
}

int extra = 10;
tie_variable vars[] = {
    TIE_CLOSURE_FN1("plus_extra", plus_context, &extra),
};
```

## Embedded Options

`tie_options` lets embedded callers constrain parser resource use and provide
their own allocator:

```c
static unsigned char arena[256];
static tie_allocator my_allocator = {
    my_malloc,
    my_free,
    arena,
};

tie_options options = {
    &my_allocator,
    32u, /* max_nodes */
    16u, /* max_depth */
};
```

If you compile with a custom allocator, release the tree with
`tie_free_ex(expr, options.allocator)`.

## Grammar

```text
<list>      = <bit_or> {"," <bit_or>}
<bit_or>    = <bit_xor> {"|" <bit_xor>}
<bit_xor>   = <bit_and> {"^" <bit_and>}
<bit_and>   = <shift> {"&" <shift>}
<shift>     = <expr> {("<<" | ">>" | "<" | ">") <expr>}
<expr>      = <term> {("+" | "-") <term>}
<term>      = <unary> {("*" | "/" | "%") <unary>}
<unary>     = {("+" | "-" | "~")} <base>
<base>      = <integer>
            | <variable>
            | <function-0> {"(" ")"}
            | <function-1> <unary>
            | <function-N> "(" <bit_or> {"," <bit_or>} ")"
            | "(" <list> ")"
```

Whitespace between tokens is ignored. Variable names start with a letter and may
then contain letters, digits, or underscores.

## Built-ins

- Arithmetic: `+`, `-`, `*`, `/`, `%`
- Unary: `+`, `-`, `~`, `abs`
- Bitwise: `&`, `^`, `|`
- Shifts: `<<`, `>>`; single-character `<` and `>` are still accepted for
  compatibility
- Comma list operator: `,`
- Short-circuit conditional function: `if(condition, if_true, if_false)`

## Verification

Run:

```sh
make
```

The default build runs the smoke tests, compiles the core as C++17, and builds
the examples, REPL, and benchmark.
