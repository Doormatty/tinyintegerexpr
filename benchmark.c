#include "tinyintegerexpr.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define loops 10000

typedef int (*function1)(int);

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

  printf("native ");
  start = clock();
  total = 0;
  for (j = 0; j < loops; ++j) {
    for (i = 0; i < loops; ++i) {
      tmp = i;
      total += func(tmp);
    }
  }
  const int nelapsed = (int) ((clock() - start) * 1000 / CLOCKS_PER_SEC);

  printf(" %lld", total);
  if (nelapsed) {
    printf("\t%5dms\t%5dmops\n", nelapsed, loops * loops / nelapsed / 1000);
  } else {
    printf("\tinf\n");
  }

  printf("interp ");
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
  const int eelapsed = (int) ((clock() - start) * 1000 / CLOCKS_PER_SEC);
  tie_free(n);

  printf(" %lld", total);
  if (eelapsed) {
    printf("\t%5dms\t%5dmops\n", eelapsed, loops * loops / eelapsed / 1000);
  } else {
    printf("\tinf\n");
  }

  if (nelapsed) {
    printf("%.2f%% longer\n", (((double) eelapsed / nelapsed) - 1.0) * 100.0);
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
  bench("a+5", a5);
  bench("5+a+5", a55);
  bench("abs(a+5)", a5abs);
  bench("a+(5*2)", a10);
  bench("(a+5)*2", a52);
  bench("((a&255)<<2)^17", bitmix);

  return 0;
}
