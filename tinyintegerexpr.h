#ifndef TINYINTEGEREXPR_H
#define TINYINTEGEREXPR_H

#include <stddef.h>

enum {
  TIE_VARIABLE = 0,

  TIE_FUNCTION0 = 8, TIE_FUNCTION1, TIE_FUNCTION2, TIE_FUNCTION3,
  TIE_FUNCTION4, TIE_FUNCTION5, TIE_FUNCTION6, TIE_FUNCTION7,

  TIE_CLOSURE0 = 16, TIE_CLOSURE1, TIE_CLOSURE2, TIE_CLOSURE3,
  TIE_CLOSURE4, TIE_CLOSURE5, TIE_CLOSURE6, TIE_CLOSURE7,

  TIE_FLAG_PURE = 32
};

typedef enum tie_status {
  TIE_OK = 0,
  TIE_ERR_PARSE,
  TIE_ERR_OUT_OF_MEMORY,
  TIE_ERR_DIVIDE_BY_ZERO,
  TIE_ERR_MODULO_BY_ZERO,
  TIE_ERR_SHIFT_RANGE,
  TIE_ERR_OVERFLOW,
  TIE_ERR_TOO_DEEP,
  TIE_ERR_TOO_MANY_NODES,
  TIE_ERR_INVALID_ARGUMENT
} tie_status;

typedef int (*tie_function0)(void);
typedef int (*tie_function1)(int);
typedef int (*tie_function2)(int, int);
typedef int (*tie_function3)(int, int, int);
typedef int (*tie_function4)(int, int, int, int);
typedef int (*tie_function5)(int, int, int, int, int);
typedef int (*tie_function6)(int, int, int, int, int, int);
typedef int (*tie_function7)(int, int, int, int, int, int, int);

typedef int (*tie_closure0)(void *);
typedef int (*tie_closure1)(void *, int);
typedef int (*tie_closure2)(void *, int, int);
typedef int (*tie_closure3)(void *, int, int, int);
typedef int (*tie_closure4)(void *, int, int, int, int);
typedef int (*tie_closure5)(void *, int, int, int, int, int);
typedef int (*tie_closure6)(void *, int, int, int, int, int, int);
typedef int (*tie_closure7)(void *, int, int, int, int, int, int, int);

#ifdef __cplusplus
union tie_callable {
  constexpr tie_callable() : bound(nullptr) {}
  constexpr tie_callable(const volatile int *value) : bound(value) {}
  constexpr tie_callable(tie_function0 value) : function0(value) {}
  constexpr tie_callable(tie_function1 value) : function1(value) {}
  constexpr tie_callable(tie_function2 value) : function2(value) {}
  constexpr tie_callable(tie_function3 value) : function3(value) {}
  constexpr tie_callable(tie_function4 value) : function4(value) {}
  constexpr tie_callable(tie_function5 value) : function5(value) {}
  constexpr tie_callable(tie_function6 value) : function6(value) {}
  constexpr tie_callable(tie_function7 value) : function7(value) {}
  constexpr tie_callable(tie_closure0 value) : closure0(value) {}
  constexpr tie_callable(tie_closure1 value) : closure1(value) {}
  constexpr tie_callable(tie_closure2 value) : closure2(value) {}
  constexpr tie_callable(tie_closure3 value) : closure3(value) {}
  constexpr tie_callable(tie_closure4 value) : closure4(value) {}
  constexpr tie_callable(tie_closure5 value) : closure5(value) {}
  constexpr tie_callable(tie_closure6 value) : closure6(value) {}
  constexpr tie_callable(tie_closure7 value) : closure7(value) {}

  const volatile int *bound;
  tie_function0 function0;
  tie_function1 function1;
  tie_function2 function2;
  tie_function3 function3;
  tie_function4 function4;
  tie_function5 function5;
  tie_function6 function6;
  tie_function7 function7;
  tie_closure0 closure0;
  tie_closure1 closure1;
  tie_closure2 closure2;
  tie_closure3 closure3;
  tie_closure4 closure4;
  tie_closure5 closure5;
  tie_closure6 closure6;
  tie_closure7 closure7;
};
#else
typedef union tie_callable {
  const volatile int *bound;
  tie_function0 function0;
  tie_function1 function1;
  tie_function2 function2;
  tie_function3 function3;
  tie_function4 function4;
  tie_function5 function5;
  tie_function6 function6;
  tie_function7 function7;
  tie_closure0 closure0;
  tie_closure1 closure1;
  tie_closure2 closure2;
  tie_closure3 closure3;
  tie_closure4 closure4;
  tie_closure5 closure5;
  tie_closure6 closure6;
  tie_closure7 closure7;
} tie_callable;
#endif

typedef struct tie_variable {
  const char *name;
  tie_callable address;
  int type;
  void *context;
} tie_variable;

#ifdef __cplusplus
union tie_expression_data {
  constexpr tie_expression_data() : value(0) {}
  constexpr tie_expression_data(int v) : value(v) {}
  constexpr tie_expression_data(const volatile int *v) : bound(v) {}
  constexpr tie_expression_data(tie_callable v) : callable(v) {}

  int value;
  int operation;
  const volatile int *bound;
  tie_callable callable;
};
#else
typedef union tie_expression_data {
  int value;
  int operation;
  const volatile int *bound;
  tie_callable callable;
} tie_expression_data;
#endif

typedef struct tie_expression {
  int type;
  tie_expression_data data;
  void *context;
  struct tie_expression *parameters[1];
} tie_expression;

typedef void *(*tie_malloc_fn)(size_t size, void *context);
typedef void (*tie_free_fn)(void *ptr, void *context);

typedef struct tie_allocator {
  tie_malloc_fn malloc_fn;
  tie_free_fn free_fn;
  void *context;
} tie_allocator;

typedef struct tie_options {
  const tie_allocator *allocator;
  unsigned int max_nodes;
  unsigned int max_depth;
} tie_options;

#ifndef TIE_DEFAULT_MAX_NODES
#define TIE_DEFAULT_MAX_NODES 128u
#endif

#ifndef TIE_DEFAULT_MAX_DEPTH
#define TIE_DEFAULT_MAX_DEPTH 32u
#endif

#ifdef __cplusplus
#define TIE_VAR(name, address) {(name), tie_callable((address)), TIE_VARIABLE, nullptr}
#define TIE_FN0(name, function) {(name), tie_callable(static_cast<tie_function0>(function)), TIE_FUNCTION0, nullptr}
#define TIE_FN1(name, function) {(name), tie_callable(static_cast<tie_function1>(function)), TIE_FUNCTION1, nullptr}
#define TIE_FN2(name, function) {(name), tie_callable(static_cast<tie_function2>(function)), TIE_FUNCTION2, nullptr}
#define TIE_FN3(name, function) {(name), tie_callable(static_cast<tie_function3>(function)), TIE_FUNCTION3, nullptr}
#define TIE_FN4(name, function) {(name), tie_callable(static_cast<tie_function4>(function)), TIE_FUNCTION4, nullptr}
#define TIE_FN5(name, function) {(name), tie_callable(static_cast<tie_function5>(function)), TIE_FUNCTION5, nullptr}
#define TIE_FN6(name, function) {(name), tie_callable(static_cast<tie_function6>(function)), TIE_FUNCTION6, nullptr}
#define TIE_FN7(name, function) {(name), tie_callable(static_cast<tie_function7>(function)), TIE_FUNCTION7, nullptr}
#define TIE_CLOSURE_FN0(name, function, context) {(name), tie_callable(static_cast<tie_closure0>(function)), TIE_CLOSURE0, (context)}
#define TIE_CLOSURE_FN1(name, function, context) {(name), tie_callable(static_cast<tie_closure1>(function)), TIE_CLOSURE1, (context)}
#define TIE_CLOSURE_FN2(name, function, context) {(name), tie_callable(static_cast<tie_closure2>(function)), TIE_CLOSURE2, (context)}
#define TIE_CLOSURE_FN3(name, function, context) {(name), tie_callable(static_cast<tie_closure3>(function)), TIE_CLOSURE3, (context)}
#define TIE_CLOSURE_FN4(name, function, context) {(name), tie_callable(static_cast<tie_closure4>(function)), TIE_CLOSURE4, (context)}
#define TIE_CLOSURE_FN5(name, function, context) {(name), tie_callable(static_cast<tie_closure5>(function)), TIE_CLOSURE5, (context)}
#define TIE_CLOSURE_FN6(name, function, context) {(name), tie_callable(static_cast<tie_closure6>(function)), TIE_CLOSURE6, (context)}
#define TIE_CLOSURE_FN7(name, function, context) {(name), tie_callable(static_cast<tie_closure7>(function)), TIE_CLOSURE7, (context)}
#else
#define TIE_VAR(name, address) {(name), {.bound = (address)}, TIE_VARIABLE, NULL}
#define TIE_FN0(name, function) {(name), {.function0 = (function)}, TIE_FUNCTION0, NULL}
#define TIE_FN1(name, function) {(name), {.function1 = (function)}, TIE_FUNCTION1, NULL}
#define TIE_FN2(name, function) {(name), {.function2 = (function)}, TIE_FUNCTION2, NULL}
#define TIE_FN3(name, function) {(name), {.function3 = (function)}, TIE_FUNCTION3, NULL}
#define TIE_FN4(name, function) {(name), {.function4 = (function)}, TIE_FUNCTION4, NULL}
#define TIE_FN5(name, function) {(name), {.function5 = (function)}, TIE_FUNCTION5, NULL}
#define TIE_FN6(name, function) {(name), {.function6 = (function)}, TIE_FUNCTION6, NULL}
#define TIE_FN7(name, function) {(name), {.function7 = (function)}, TIE_FUNCTION7, NULL}
#define TIE_CLOSURE_FN0(name, function, context) {(name), {.closure0 = (function)}, TIE_CLOSURE0, (context)}
#define TIE_CLOSURE_FN1(name, function, context) {(name), {.closure1 = (function)}, TIE_CLOSURE1, (context)}
#define TIE_CLOSURE_FN2(name, function, context) {(name), {.closure2 = (function)}, TIE_CLOSURE2, (context)}
#define TIE_CLOSURE_FN3(name, function, context) {(name), {.closure3 = (function)}, TIE_CLOSURE3, (context)}
#define TIE_CLOSURE_FN4(name, function, context) {(name), {.closure4 = (function)}, TIE_CLOSURE4, (context)}
#define TIE_CLOSURE_FN5(name, function, context) {(name), {.closure5 = (function)}, TIE_CLOSURE5, (context)}
#define TIE_CLOSURE_FN6(name, function, context) {(name), {.closure6 = (function)}, TIE_CLOSURE6, (context)}
#define TIE_CLOSURE_FN7(name, function, context) {(name), {.closure7 = (function)}, TIE_CLOSURE7, (context)}
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Parses the input expression, evaluates it, and frees it. Returns 0 on error. */
int tie_interp(const char *expression, int *error);

/* As above, but reports parse, allocation, limit, and runtime errors explicitly. */
int tie_interp_status(const char *expression, int *error, tie_status *status, const tie_options *options);

/* Parses the input expression and binds variables/functions. Returns NULL on error. */
tie_expression *tie_compile(const char *expression, const tie_variable *variables, int var_count, int *error);

/* As above, with explicit status reporting and embedded allocation/limit options. */
tie_expression *tie_compile_ex(const char *expression, const tie_variable *variables, int var_count,
                               int *error, tie_status *status, const tie_options *options);

/* Evaluates the expression. Returns 0 on runtime error. */
int tie_eval(const tie_expression *n);

/* As above, but reports runtime errors explicitly. */
int tie_eval_status(const tie_expression *n, tie_status *status);

/* Frees an expression allocated by tie_compile(). Safe to call on NULL. */
void tie_free(tie_expression *n);

/* Frees an expression allocated with a custom allocator. Safe to call on NULL. */
void tie_free_ex(tie_expression *n, const tie_allocator *allocator);

const char *tie_status_message(tie_status status);

#ifndef TIE_DISABLE_PRINT
/* Prints debugging information on the syntax tree. */
void tie_print(const tie_expression *n);
#endif

#ifdef __cplusplus
}
#endif

#endif
