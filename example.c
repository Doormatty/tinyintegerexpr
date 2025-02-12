#include "tinyintegerexpr.h"
#include <stdio.h>

int main(int argc, char *argv[])
{
    const char *c = "sqrt(5^2+7^2+11^2+(8-2)^2)";
    double r = tie_interp(c, 0);
    printf("The expression:\n\t%s\nevaluates to:\n\t%f\n", c, r);
    return 0;
}
