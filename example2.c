#include "tinyintegerexpr.h"

#include <stdio.h>

int main(int argc, char *argv[]) {
  if (argc < 2) {
    printf("Usage: example2 \"expression\"\n");
    return 0;
  }

  const char *expression = argv[1];
  printf("Evaluating:\n\t%s\n", expression);

  int x = 3;
  int y = 4;
  tie_variable vars[] = {
      TIE_VAR("x", &x),
      TIE_VAR("y", &y),
  };

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
