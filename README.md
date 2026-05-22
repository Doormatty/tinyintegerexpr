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

## Benchmark

Run:

```sh
make bench
./bench
```

Sample output from a local development host using GCC 13.3.0 with the default
`-O2` Makefile flags:

```text
Simple timing lines run 100000000 evaluations; bytebeat timing lines run 1000000 evaluations.
ns/eval is total CPU time divided by that line's evaluation count.

Expression: a+5
native 500450000000     2.140 ns/eval    467.2 mops     214.0 ms
interp 500450000000     3.508 ns/eval    285.1 mops     350.8 ms
63.87% longer

Expression: 5+a+5
native 500950000000     2.127 ns/eval    470.2 mops     212.7 ms
interp 500950000000     4.255 ns/eval    235.0 mops     425.5 ms
100.08% longer

Expression: abs(a+5)
native 500450000000     2.192 ns/eval    456.3 mops     219.2 ms
interp 500450000000     4.103 ns/eval    243.7 mops     410.3 ms
87.23% longer

Expression: a+(5*2)
native 500950000000     2.150 ns/eval    465.1 mops     215.0 ms
interp 500950000000     3.523 ns/eval    283.9 mops     352.3 ms
63.84% longer

Expression: (a+5)*2
native 1000900000000    2.188 ns/eval    457.1 mops     218.8 ms
interp 1000900000000    4.563 ns/eval    219.2 mops     456.3 ms
108.56% longer

Expression: ((a&255)<<2)^17
native 51023200000      2.748 ns/eval    363.9 mops     274.8 ms
interp 51023200000      3.889 ns/eval    257.1 mops     388.9 ms
41.51% longer

Expression: if(t & (4 << c), ((t * (t ^ t & a) | (t >> b)) >> 1), (t >> 4) | if(t & (c << b), t << 1, t))
native 163686800000     3.822 ns/eval    261.6 mops       3.8 ms
interp 163686800000   117.836 ns/eval      8.5 mops     117.8 ms
2983.10% longer

Expression: (if(t >> 6, 2, a)&t * (t >> c) | b - (t >> a)) % (t >> b) + (4 | (t >> c))
native 171276000        3.295 ns/eval    303.5 mops       3.3 ms
interp 171276000      176.832 ns/eval      5.7 mops     176.8 ms
5266.68% longer

Expression: ((t*(t>>a|t>>(a+1))&b&t>>8))^(t&t>>13|t>>6)
native 11752000         2.769 ns/eval    361.1 mops       2.8 ms
interp 11752000       168.764 ns/eval      5.9 mops     168.8 ms
5994.76% longer

Expression: (t * 5 & t >> 7) | (t * 3 & t >> 10)
native 2772000          2.666 ns/eval    375.1 mops       2.7 ms
interp 2772000         84.255 ns/eval     11.9 mops      84.3 ms
3060.35% longer

Expression: (t * a & t >> b | t * c & t >> 7 | t * 3 & t / 1024) - 1
native 12676000         6.001 ns/eval    166.6 mops       6.0 ms
interp 12676000       150.893 ns/eval      6.6 mops     150.9 ms
2414.46% longer

Expression: t >> c ^ t & 37 | t + (t ^ t >> a) - t * (if(t >> a, 2, 6)&t >> b)^t << 1 & if(t & b,t >> 4,t >> 10)
native 850984000        3.363 ns/eval    297.4 mops       3.4 ms
interp 850984000      246.469 ns/eval      4.1 mops     246.5 ms
7228.84% longer
```

## Verification

Run:

```sh
make
```

The default build runs the smoke tests, compiles the core as C++17, and builds
the examples, REPL, and benchmark.
