#include "tinyintegerexpr.h"

#include <stdio.h>

static int my_sum(int a, int b) {
  printf("Called C function with %d and %d.\n", a, b);
  return a + b;
}

int main(void) {
  tie_variable vars[] = {
      TIE_FN2("mysum", my_sum),
  };

  const char *expression = "mysum(5, 6)";
  printf("Evaluating:\n\t%s\n", expression);

  int err = 0;
  tie_status status = TIE_OK;
  tie_expression *n = tie_compile_ex(expression, vars, (int) (sizeof(vars) / sizeof(vars[0])), &err, &status, NULL);

  if (n == NULL) {
    printf("\t%*s^\n%s\n", err > 0 ? err - 1 : 0, "", tie_status_message(status));
    return 1;
  }

  const int result = tie_eval_status(n, &status);
  tie_free(n);

  if (status != TIE_OK) {
    printf("%s\n", tie_status_message(status));
    return 1;
  }

  printf("Result:\n\t%d\n", result);
  return 0;
}
