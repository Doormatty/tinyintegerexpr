#include "tinyintegerexpr.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define ARRAY_LEN(a) ((int) (sizeof(a) / sizeof((a)[0])))
#define SIMPLE_LOOPS 10000
#define BYTEBEAT_LOOPS 1000
#define BYTEBEAT_T_OFFSET 256
#define BYTEBEAT_A 3
#define BYTEBEAT_B 5
#define BYTEBEAT_C 2
#define SIMPLE_EVAL_COUNT ((long long) SIMPLE_LOOPS * (long long) SIMPLE_LOOPS)
#define BYTEBEAT_EVAL_COUNT ((long long) BYTEBEAT_LOOPS * (long long) BYTEBEAT_LOOPS)

typedef int (*function1)(int);

static void print_timing(const char *label, long long total, clock_t ticks, long long eval_count) {
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
  for (j = 0; j < SIMPLE_LOOPS; ++j) {
    for (i = 0; i < SIMPLE_LOOPS; ++i) {
      tmp = i;
      total += func(tmp);
    }
  }
  const clock_t nticks = clock() - start;
  print_timing("native", total, nticks, SIMPLE_EVAL_COUNT);

  int err = 0;
  tie_status status = TIE_OK;
  tie_expression *n = tie_compile_ex(expr, vars, 1, &err, &status, NULL);
  if (n == NULL) {
    printf("compile failed at %d: %s\n", err, tie_status_message(status));
    return;
  }

  start = clock();
  total = 0;
  for (j = 0; j < SIMPLE_LOOPS; ++j) {
    for (i = 0; i < SIMPLE_LOOPS; ++i) {
      tmp = i;
      total += tie_eval(n);
    }
  }
  const clock_t eticks = clock() - start;
  tie_free(n);
  print_timing("interp", total, eticks, SIMPLE_EVAL_COUNT);

  if (nticks > 0) {
    printf("%.2f%% longer\n", (((double) eticks / (double) nticks) - 1.0) * 100.0);
  }
  printf("\n");
}

static void bench_bytebeat(const char *expr, function1 func) {
  int i;
  int j;
  volatile long long total;
  int t;
  int a = BYTEBEAT_A;
  int b = BYTEBEAT_B;
  int c = BYTEBEAT_C;
  clock_t start;

  tie_variable vars[] = {
      TIE_VAR("t", &t),
      TIE_VAR("a", &a),
      TIE_VAR("b", &b),
      TIE_VAR("c", &c),
  };

  printf("Expression: %s\n", expr);

  start = clock();
  total = 0;
  for (j = 0; j < BYTEBEAT_LOOPS; ++j) {
    for (i = 0; i < BYTEBEAT_LOOPS; ++i) {
      t = i + BYTEBEAT_T_OFFSET;
      total += func(t);
    }
  }
  const clock_t nticks = clock() - start;
  print_timing("native", total, nticks, BYTEBEAT_EVAL_COUNT);

  int err = 0;
  tie_status status = TIE_OK;
  tie_expression *n = tie_compile_ex(expr, vars, ARRAY_LEN(vars), &err, &status, NULL);
  if (n == NULL) {
    printf("compile failed at %d: %s\n", err, tie_status_message(status));
    return;
  }

  start = clock();
  total = 0;
  for (j = 0; j < BYTEBEAT_LOOPS; ++j) {
    for (i = 0; i < BYTEBEAT_LOOPS; ++i) {
      t = i + BYTEBEAT_T_OFFSET;
      total += tie_eval(n);
    }
  }
  const clock_t eticks = clock() - start;
  tie_free(n);
  print_timing("interp", total, eticks, BYTEBEAT_EVAL_COUNT);

  if (nticks > 0) {
    printf("%.2f%% longer\n", (((double) eticks / (double) nticks) - 1.0) * 100.0);
  }
  printf("\n");
}

static int shr_int(int value, int amount) {
  return (int) ((unsigned int) value >> amount);
}

static int shl_int(int value, int amount) {
  return (int) ((unsigned int) value << amount);
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

static int nested_if_music(int t) {
  const int a = BYTEBEAT_A;
  const int b = BYTEBEAT_B;
  const int c = BYTEBEAT_C;

  return (t & shl_int(4, c)) ? shr_int((t * (t ^ (t & a)) | shr_int(t, b)), 1)
                             : (shr_int(t, 4) | ((t & shl_int(c, b)) ? shl_int(t, 1) : t));
}

static int modulo_gate(int t) {
  const int a = BYTEBEAT_A;
  const int b = BYTEBEAT_B;
  const int c = BYTEBEAT_C;

  return ((((shr_int(t, 6) ? 2 : a) & (t * shr_int(t, c))) | (b - shr_int(t, a))) %
          shr_int(t, b)) +
         (4 | shr_int(t, c));
}

static int xor_phrase(int t) {
  const int a = BYTEBEAT_A;
  const int b = BYTEBEAT_B;

  return ((t * (shr_int(t, a) | shr_int(t, a + 1)) & b & shr_int(t, 8))) ^
         ((t & shr_int(t, 13)) | shr_int(t, 6));
}

static int classic_bytebeat(int t) {
  return (t * 5 & shr_int(t, 7)) | (t * 3 & shr_int(t, 10));
}

static int parameterized_bytebeat(int t) {
  const int a = BYTEBEAT_A;
  const int b = BYTEBEAT_B;
  const int c = BYTEBEAT_C;

  return ((t * a & shr_int(t, b)) | (t * c & shr_int(t, 7)) | (t * 3 & (t / 1024))) - 1;
}

static int dense_if_mix(int t) {
  const int a = BYTEBEAT_A;
  const int b = BYTEBEAT_B;
  const int c = BYTEBEAT_C;

  return (shr_int(t, c) ^ (t & 37)) |
         ((t + (t ^ shr_int(t, a)) -
           t * (((shr_int(t, a) ? 2 : 6) & shr_int(t, b)))) ^
          (shl_int(t, 1) & ((t & b) ? shr_int(t, 4) : shr_int(t, 10))));
}

int main(void) {
  printf("Simple timing lines run %lld evaluations; bytebeat timing lines run %lld evaluations.\n",
         SIMPLE_EVAL_COUNT, BYTEBEAT_EVAL_COUNT);
  printf("ns/eval is total CPU time divided by that line's evaluation count.\n\n");

  bench("a+5", a5);
  bench("5+a+5", a55);
  bench("abs(a+5)", a5abs);
  bench("a+(5*2)", a10);
  bench("(a+5)*2", a52);
  bench("((a&255)<<2)^17", bitmix);

  bench_bytebeat("if(t & (4 << c), ((t * (t ^ t & a) | (t >> b)) >> 1), "
                 "(t >> 4) | if(t & (c << b), t << 1, t))",
                 nested_if_music);
  bench_bytebeat("(if(t >> 6, 2, a)&t * (t >> c) | b - (t >> a)) % "
                 "(t >> b) + (4 | (t >> c))",
                 modulo_gate);
  bench_bytebeat("((t*(t>>a|t>>(a+1))&b&t>>8))^(t&t>>13|t>>6)",
                 xor_phrase);
  bench_bytebeat("(t * 5 & t >> 7) | (t * 3 & t >> 10)",
                 classic_bytebeat);
  bench_bytebeat("(t * a & t >> b | t * c & t >> 7 | t * 3 & t / 1024) - 1",
                 parameterized_bytebeat);
  bench_bytebeat("t >> c ^ t & 37 | t + (t ^ t >> a) - "
                 "t * (if(t >> a, 2, 6)&t >> b)^t << 1 & "
                 "if(t & b,t >> 4,t >> 10)",
                 dense_if_mix);

  return 0;
}
