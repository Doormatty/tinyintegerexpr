#include "tinyintegerexpr.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef USE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#else
static char *readline(const char *prompt) {
    fprintf(stderr, "%s", prompt);
    char buf[1024];
    char *line = fgets(buf, sizeof(buf), stdin);
    if (line == NULL && feof(stdin)) {
        return NULL;
    } else if (line == NULL) {
        perror("fgets");
        return NULL;
    }

    size_t len = strlen(line);

    if (len < 1)
        return NULL;

    if (line[len - 1] == '\n') {
        line[len - 1] = '\0';
        len -= 1;
    }

    line = malloc(len + 1);
    strcpy(line, buf);
    return line;
}

static void add_history(const char *line) {(void) line;}
#endif

static int eval(const char *str) {
    int err = 0;
    tie_status status = TIE_OK;
    int r = tie_interp_status(str, &err, &status, NULL);
    if (status != TIE_OK) {
        if (err != 0) {
            printf("Error at position %i: %s\n", err, tie_status_message(status));
        } else {
            printf("%s\n", tie_status_message(status));
        }
        return -1;
    } else if (err != 0) {
        printf("Error at position %i\n", err);
        return -1;
    } else {
        printf("%d\n", r);
        return 0;
    }
}

static void repl() {
    while (1) {
        char *line = readline("> ");
        if (line == NULL) {
            break;
        } else if (strcmp(line, "q") == 0 || strcmp(line, "quit") == 0) {
            free(line);
            break;
        }

        if (eval(line) != -1) {
            add_history(line);
        }

        free(line);
    }
}

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "-e") == 0) {
        if (eval(argv[2]) == -1) {
            return 1;
        } else {
            return 0;
        }
    } else if (argc == 1) {
        repl();
        return 0;
    } else {
        printf("Usage: %s\n", argv[0]);
        printf("       %s -e <expression>\n", argv[0]);
        return 1;
    }
}
