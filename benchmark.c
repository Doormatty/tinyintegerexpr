#include "tinyintegerexpr.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define loops 10000
#define eval_count ((long long) loops * (long long) loops)

typedef int (*function1)(int);

static void print_timing(const char *label, long long total, clock_t ticks) {
  printf("%-6s %lld", label, total);

  if (ticks > 0) {
    const double seconds = (double) ticks / (double) CLOCKS_PER_SEC;
    const double ns_per_eval = seconds * 1000000000.0 / (double) eval_count;
    const double mops = (double) eval_count / seconds / 1000000.0;
    const double ms = seconds * 1000.0;

    printf("\t%8.3f ns/eval\t%8.1f mops\t%8.1f ms\n", ns_per_eval, mops, ms);
  } else {
    printf("\t< clock resolution\tinf mops\t0.0 ms\n");
  }
}

static void bench(const char *expr, function1 func) {
  int i;
  int j;
  volatile long long total;
  int tmp;
  clock_t start;

  tie_variable vars[] = {
      TIE_VAR("a", &tmp),
  };

  printf("Expression: %s\n", expr);

  start = clock();
  total = 0;
  for (j = 0; j < loops; ++j) {
    for (i = 0; i < loops; ++i) {
      tmp = i;
      total += func(tmp);
    }
  }
  const clock_t nticks = clock() - start;
  print_timing("native", total, nticks);

  int err = 0;
  tie_status status = TIE_OK;
  tie_expression *n = tie_compile_ex(expr, vars, 1, &err, &status, NULL);
  if (n == NULL) {
    printf("compile failed at %d: %s\n", err, tie_status_message(status));
    return;
  }

  start = clock();
  total = 0;
  for (j = 0; j < loops; ++j) {
    for (i = 0; i < loops; ++i) {
      tmp = i;
      total += tie_eval(n);
    }
  }
  const clock_t eticks = clock() - start;
  tie_free(n);
  print_timing("interp", total, eticks);

  if (nticks > 0) {
    printf("%.2f%% longer\n", (((double) eticks / (double) nticks) - 1.0) * 100.0);
  }
  printf("\n");
}

static int a5(int a) {
  return a + 5;
}

static int a55(int a) {
  return 5 + a + 5;
}

static int a5abs(int a) {
  return abs(a + 5);
}

static int a52(int a) {
  return (a + 5) * 2;
}

static int a10(int a) {
  return a + (5 * 2);
}

static int bitmix(int a) {
  return ((a & 255) << 2) ^ 17;
}

int main(void) {
  printf("Each timing line runs %lld evaluations; ns/eval is total CPU time divided by that count.\n\n",
         eval_count);

  bench("a+5", a5);
  bench("5+a+5", a55);
  bench("abs(a+5)", a5abs);
  bench("a+(5*2)", a10);
  bench("(a+5)*2", a52);
  bench("((a&255)<<2)^17", bitmix);

  return 0;
}
