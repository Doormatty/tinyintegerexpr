#include "tinyintegerexpr.h"
#include "minctest.h"

#include <limits.h>
#include <stdio.h>

#define ARRAY_LEN(a) ((int) (sizeof(a) / sizeof((a)[0])))

typedef struct {
  const char *expr;
  int answer;
} test_case;

typedef struct {
  const char *expr;
  tie_status status;
} status_case;

typedef struct {
  const char *expr;
  int t;
  int a;
  int b;
  int c;
  int answer;
} equation_case;

static void expect_ok(const char *expr, int answer) {
  int err = -1;
  tie_status status = TIE_ERR_PARSE;
  const int result = tie_interp_status(expr, &err, &status, NULL);

  lequal(err, 0);
  lequal(status, TIE_OK);
  lequal(result, answer);
}

static void expect_status(const char *expr, tie_status expected) {
  int err = -1;
  tie_status status = TIE_OK;
  (void) tie_interp_status(expr, &err, &status, NULL);

  lequal(err, 0);
  lequal(status, expected);
}

static void test_results(void) {
  const test_case cases[] = {
      {"1", 1},
      {"1 ", 1},
      {"(1)", 1},
      {"2+3*4", 14},
      {"(2+3)*4", 20},
      {"10/3", 3},
      {"10%3", 1},
      {"2*-3", -6},
      {"~0", -1},
      {"abs -5", 5},
      {"abs(-5)", 5},
      {"1<<3", 8},
      {"16>>2", 4},
      {"1<3", 8},
      {"16>2", 4},
      {"6&3|8", 10},
      {"1|2&0", 1},
      {"1^3&1", 0},
      {"if(1,2,1/0)", 2},
      {"if(0,1/0,3)", 3},
      {"1,2+3", 5},
  };

  for (int i = 0; i < ARRAY_LEN(cases); ++i) {
    expect_ok(cases[i].expr, cases[i].answer);
  }
}

static void test_syntax(void) {
  const char *errors[] = {
      "",
      "1+",
      "1)",
      "(1",
      "a+5",
      "_a+5",
      "!+5",
      "if(1,2)",
      "sum(1)",
  };

  for (int i = 0; i < ARRAY_LEN(errors); ++i) {
    int err = 0;
    tie_status status = TIE_OK;
    tie_expression *n = tie_compile_ex(errors[i], NULL, 0, &err, &status, NULL);

    lok(n == NULL);
    lok(err != 0);
    lequal(status, TIE_ERR_PARSE);
  }
}

static void test_runtime_errors(void) {
  const status_case cases[] = {
      {"1/0", TIE_ERR_DIVIDE_BY_ZERO},
      {"1%0", TIE_ERR_MODULO_BY_ZERO},
      {"1<<-1", TIE_ERR_SHIFT_RANGE},
  };

  for (int i = 0; i < ARRAY_LEN(cases); ++i) {
    expect_status(cases[i].expr, cases[i].status);
  }

  char expr[64];
  (void) snprintf(expr, sizeof(expr), "1<<%d", (int) (sizeof(int) * CHAR_BIT));
  expect_status(expr, TIE_ERR_SHIFT_RANGE);

  (void) snprintf(expr, sizeof(expr), "%d+1", INT_MAX);
  expect_status(expr, TIE_ERR_OVERFLOW);

  int min_value = INT_MIN;
  tie_variable vars[] = {
      TIE_VAR("x", &min_value),
  };

  int err = 0;
  tie_status status = TIE_OK;
  tie_expression *n = tie_compile_ex("abs x", vars, ARRAY_LEN(vars), &err, &status, NULL);
  lok(n != NULL);
  lequal(err, 0);
  lequal(status, TIE_OK);
  (void) tie_eval_status(n, &status);
  lequal(status, TIE_ERR_OVERFLOW);
  tie_free(n);

  int x = INT_MAX;
  tie_variable fast_vars[] = {
      TIE_VAR("x", &x),
  };

  n = tie_compile_ex("x+1", fast_vars, ARRAY_LEN(fast_vars), &err, &status, NULL);
  lok(n != NULL);
  lequal(err, 0);
  lequal(status, TIE_OK);
  (void) tie_eval_status(n, &status);
  lequal(status, TIE_ERR_OVERFLOW);
  lequal(tie_eval(n), 0);
  tie_free(n);

  x = 0;
  n = tie_compile_ex("1/x", fast_vars, ARRAY_LEN(fast_vars), &err, &status, NULL);
  lok(n != NULL);
  lequal(err, 0);
  lequal(status, TIE_OK);
  (void) tie_eval_status(n, &status);
  lequal(status, TIE_ERR_DIVIDE_BY_ZERO);
  lequal(tie_eval(n), 0);
  tie_free(n);

  x = -1;
  n = tie_compile_ex("1<<x", fast_vars, ARRAY_LEN(fast_vars), &err, &status, NULL);
  lok(n != NULL);
  lequal(err, 0);
  lequal(status, TIE_OK);
  (void) tie_eval_status(n, &status);
  lequal(status, TIE_ERR_SHIFT_RANGE);
  lequal(tie_eval(n), 0);
  tie_free(n);
}

static void expect_equation_ok(const equation_case *sample) {
  int t = sample->t;
  int a = sample->a;
  int b = sample->b;
  int c = sample->c;
  tie_variable vars[] = {
      TIE_VAR("t", &t),
      TIE_VAR("a", &a),
      TIE_VAR("b", &b),
      TIE_VAR("c", &c),
  };

  int err = 0;
  tie_status status = TIE_OK;
  tie_expression *n = tie_compile_ex(sample->expr, vars, ARRAY_LEN(vars), &err, &status, NULL);
  lok(n != NULL);
  lequal(err, 0);
  lequal(status, TIE_OK);

  if (n == NULL) {
    return;
  }

  lequal(tie_eval_status(n, &status), sample->answer);
  lequal(status, TIE_OK);
  tie_free(n);
}

static void test_equation_samples(void) {
  const equation_case cases[] = {
      {"if(t & (4 << c), ((t * (t ^ t & a) | (t >> b)) >> 1), "
       "(t >> 4) | if(t & (c << b), t << 1, t))",
       257, 3, 5, 2, 273},
      {"if(t & (4 << c), ((t * (t ^ t & a) | (t >> b)) >> 1), "
       "(t >> 4) | if(t & (c << b), t << 1, t))",
       80, 3, 5, 2, 3201},
      {"(if(t >> 6, 2, a)&t * (t >> c) | b - (t >> a)) % (t >> b) + "
       "(4 | (t >> c))",
       257, 3, 5, 2, 65},
      {"(if(t >> b, c, a)&t * a | 8 - (t >> 1)) % (t >> b) + "
       "(4 | (t >> c))",
       257, 3, 5, 2, 62},
      {"((t*(t>>a|t>>(a+1))&b&t>>8))^(t&t>>13|t>>6)",
       257, 3, 5, 2, 4},
      {"t+(t&t^t>>(b*2-c))-t*((t>>a)&if(t%c,2,a-c)&t>>b)",
       257, 3, 5, 2, 513},
      {"t>>6^t&37|t+(t^t>>11)-t*(if(t%a,2,6)&t>>11)^t<<1&"
       "if(t&b,t>>4,t>>10)",
       257, 3, 5, 2, 519},
      {"if(t>>b&t,t>>a,-t>>c)", 257, 3, 5, 2, 1073741759},
      {"if(t>>b&t,t>>a,-t>>c)", 291, 3, 5, 2, 36},
      {"if(t & (4 << a), ((-t * (t ^ t) | (t >> b)) >> c), "
       "(t >> 4) | if(t & (c << b), t << 1, t))",
       64, 3, 5, 2, 132},
      {"((t>>a&t)-(t>>a)+(t>>a&t))+(t*((t>>b)&b))",
       257, 3, 5, 2, -32},
      {"(t * 5 & t >> 7) | (t * 3 & t >> 10)",
       257, 3, 5, 2, 0},
      {"(t * a & t >> b | t * c & t >> 7 | t * 3 & t / 1024) - 1",
       257, 3, 5, 2, 1},
      {"(t*((t>>a|t<<c)&29&t>>b))", 257, 3, 5, 2, 0},
      {"((t & (t >> a)) + (t | (t >> b))) & (t >> (c + 1)) | "
       "(t >> a) & (t * (t >> b))",
       257, 3, 5, 2, 0},
      {"((t * (t >> a | t >> (a & c))&b & t >> 8)) ^ "
       "(t & t >> c | t >> 6)",
       257, 3, 5, 2, 4},
      {"((t * (t >> a) & (b * t >> 7) & (8 * t >> c)))",
       257, 3, 5, 2, 0},
      {"((t >> c) * 7 | (t >> a) * 8 | (t >> b) * 7) & (t >> 7)",
       257, 3, 5, 2, 0},
      {"(t >> a | c | t >> (t >> 16)) * b + (t >> (b + 1))",
       257, 3, 5, 2, 1459},
      {"b * t >> a ^ t & (37 - c) | t + (t ^ t >> 11) - "
       "t * (if(t >> 6, 2, a)&t >> (c + b))^t << 1 & "
       "if(t & 6,t >> 4,t >> c)",
       257, 3, 5, 2, 161},
      {"t >> c ^ t & 37 | t + (t ^ t >> a) - "
       "t * (if(t >> a, 2, 6)&t >> b)^t << 1 & "
       "if(t & b,t >> 4,t >> 10)",
       257, 3, 5, 2, 611},
  };

  for (int i = 0; i < ARRAY_LEN(cases); ++i) {
    expect_equation_ok(cases + i);
  }
}

static void test_variables(void) {
  int x = 3;
  int y = 4;
  tie_variable vars[] = {
      TIE_VAR("x", &x),
      TIE_VAR("y", &y),
  };

  int err = 0;
  tie_status status = TIE_OK;
  tie_expression *n = tie_compile_ex("x*2+y", vars, ARRAY_LEN(vars), &err, &status, NULL);
  lok(n != NULL);
  lequal(err, 0);
  lequal(status, TIE_OK);

  lequal(tie_eval_status(n, &status), 10);
  lequal(status, TIE_OK);

  x = 7;
  y = -1;
  lequal(tie_eval_status(n, &status), 13);
  lequal(status, TIE_OK);

  tie_free(n);
}

static int sum0(void) {
  return 6;
}

static int sum1(int a) {
  return a * 2;
}

static int sum2(int a, int b) {
  return a + b;
}

static int sum3(int a, int b, int c) {
  return a + b + c;
}

static void test_functions(void) {
  int x = 4;
  tie_variable vars[] = {
      TIE_VAR("x", &x),
      TIE_FN0("sum0", sum0),
      TIE_FN1("sum1", sum1),
      TIE_FN2("sum2", sum2),
      TIE_FN3("sum3", sum3),
  };

  int err = 0;
  tie_status status = TIE_OK;

  tie_expression *n = tie_compile_ex("sum0()+sum1 x+sum2(2,3)+sum3(1,2,3)",
                                     vars, ARRAY_LEN(vars), &err, &status, NULL);
  lok(n != NULL);
  lequal(err, 0);
  lequal(status, TIE_OK);
  lequal(tie_eval_status(n, &status), 25);
  lequal(status, TIE_OK);
  tie_free(n);
}

static int plus_context(void *context, int value) {
  return *((int *) context) + value;
}

static void test_closure(void) {
  int extra = 10;
  tie_variable vars[] = {
      TIE_CLOSURE_FN1("plus_extra", plus_context, &extra),
  };

  int err = 0;
  tie_status status = TIE_OK;
  tie_expression *n = tie_compile_ex("plus_extra 5", vars, ARRAY_LEN(vars), &err, &status, NULL);
  lok(n != NULL);
  lequal(err, 0);
  lequal(status, TIE_OK);
  lequal(tie_eval_status(n, &status), 15);

  extra = -2;
  lequal(tie_eval_status(n, &status), 3);
  lequal(status, TIE_OK);
  tie_free(n);
}

static void test_limits(void) {
  tie_options options = {NULL, 2U, 0U};

  int err = 0;
  tie_status status = TIE_OK;
  tie_expression *n = tie_compile_ex("1+2+3", NULL, 0, &err, &status, &options);
  lok(n == NULL);
  lequal(status, TIE_ERR_TOO_MANY_NODES);

  options.max_nodes = 0U;
  options.max_depth = 2U;
  n = tie_compile_ex("((((1))))", NULL, 0, &err, &status, &options);
  lok(n == NULL);
  lequal(status, TIE_ERR_TOO_DEEP);
}

int main(void) {
  lrun("Results", test_results);
  lrun("Syntax", test_syntax);
  lrun("Runtime", test_runtime_errors);
  lrun("Equations", test_equation_samples);
  lrun("Variables", test_variables);
  lrun("Functions", test_functions);
  lrun("Closure", test_closure);
  lrun("Limits", test_limits);
  lresults();

  return lfails != 0;
}
