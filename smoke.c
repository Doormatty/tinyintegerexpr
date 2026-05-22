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
  lrun("Variables", test_variables);
  lrun("Functions", test_functions);
  lrun("Closure", test_closure);
  lrun("Limits", test_limits);
  lresults();

  return lfails != 0;
}
