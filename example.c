#include "tinyintegerexpr.h"

#include <stdio.h>

int main(void) {
  const char *expr = "(5 * 5) + (7 * 7) + (11 * 11) + ((8 - 2) * (8 - 2))";
  int err = 0;
  tie_status status = TIE_OK;
  const int result = tie_interp_status(expr, &err, &status, NULL);

  if (status != TIE_OK) {
    printf("Error at %d: %s\n", err, tie_status_message(status));
    return 1;
  }

  printf("The expression:\n\t%s\nevaluates to:\n\t%d\n", expr, result);
  return 0;
}
