#include "tinyintegerexpr.h"

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
#include <new>
#endif

#ifndef TIE_DISABLE_PRINT
#include <stdio.h>
#endif

enum {
  TIE_CONSTANT = 1,
  TIE_BUILTIN1,
  TIE_BUILTIN2,
  TIE_BUILTIN3
};

enum {
  NULL_TOKEN = TIE_CLOSURE7 + 1,
  ERROR_TOKEN,
  END_TOKEN,
  SEPARATOR_TOKEN,
  OPEN_TOKEN,
  CLOSE_TOKEN,
  NUMBER_TOKEN,
  VARIABLE_TOKEN,
  INFIX_TOKEN
};

typedef enum tie_operation {
  TIE_OP_ADD,
  TIE_OP_SUB,
  TIE_OP_MUL,
  TIE_OP_DIV,
  TIE_OP_MOD,
  TIE_OP_NEG,
  TIE_OP_BIT_NOT,
  TIE_OP_BIT_AND,
  TIE_OP_BIT_OR,
  TIE_OP_BIT_XOR,
  TIE_OP_SHL,
  TIE_OP_SHR,
  TIE_OP_COMMA,
  TIE_OP_IF,
  TIE_OP_ABS
} tie_operation;

typedef struct tie_builtin {
  const char *name;
  int type;
  tie_operation operation;
} tie_builtin;

typedef struct state {
  const char *start;
  const char *next;
  int type;
  int value;
  const volatile int *bound;
  tie_callable callable;
  tie_operation operation;
  void *context;

  const tie_variable *lookup;
  int lookup_len;

  tie_status status;
  const tie_allocator *allocator;
  unsigned int node_count;
  unsigned int max_nodes;
  unsigned int depth;
  unsigned int max_depth;
} state;

static void *default_malloc(size_t size, void *context) {
  (void) context;
  return malloc(size);
}

static void default_free(void *ptr, void *context) {
  (void) context;
  free(ptr);
}

static const tie_allocator default_allocator = {
    default_malloc,
    default_free,
    NULL
};

static const tie_builtin builtins[] = {
    {"abs", TIE_BUILTIN1 | TIE_FLAG_PURE, TIE_OP_ABS},
    {"if",  TIE_BUILTIN3 | TIE_FLAG_PURE, TIE_OP_IF}
};

#define TYPE_MASK(TYPE) ((TYPE) & 0x0000001F)
#define IS_PURE(TYPE) (((TYPE) & TIE_FLAG_PURE) != 0)
#define IS_FUNCTION(TYPE) (TYPE_MASK(TYPE) >= TIE_FUNCTION0 && TYPE_MASK(TYPE) <= TIE_FUNCTION7)
#define IS_CLOSURE(TYPE) (TYPE_MASK(TYPE) >= TIE_CLOSURE0 && TYPE_MASK(TYPE) <= TIE_CLOSURE7)

static int arity_of(const int type) {
  switch (TYPE_MASK(type)) {
    case TIE_BUILTIN1:
      return 1;
    case TIE_BUILTIN2:
      return 2;
    case TIE_BUILTIN3:
      return 3;
    default:
      if (IS_FUNCTION(type) || IS_CLOSURE(type)) {
        return type & 0x00000007;
      }
      return 0;
  }
}

#define ARITY(TYPE) arity_of(TYPE)

static void set_state_error(state *s, const tie_status status) {
  if (s->status == TIE_OK) {
    s->status = status;
  }
  s->type = ERROR_TOKEN;
}

static void set_eval_error(tie_status *status, const tie_status value) {
  if (status != NULL && *status == TIE_OK) {
    *status = value;
  }
}

static const tie_allocator *allocator_for_alloc(const tie_allocator *allocator) {
  if (allocator != NULL && allocator->malloc_fn != NULL) {
    return allocator;
  }
  return &default_allocator;
}

static const tie_allocator *allocator_for_free(const tie_allocator *allocator) {
  if (allocator != NULL) {
    return allocator;
  }
  return &default_allocator;
}

static void release_node(tie_expression *n, const tie_allocator *allocator) {
  const tie_allocator *actual = allocator_for_free(allocator);
#ifdef __cplusplus
  n->~tie_expression();
#endif
  if (actual->free_fn != NULL) {
    actual->free_fn(n, actual->context);
  }
}

static void tie_free_internal(tie_expression *n, const tie_allocator *allocator);

static void tie_free_parameters_internal(tie_expression *n, const tie_allocator *allocator) {
  if (n == NULL) {
    return;
  }

  const int arity = ARITY(n->type);
  for (int i = 0; i < arity; ++i) {
    tie_free_internal(n->parameters[i], allocator);
  }
}

static void tie_free_internal(tie_expression *n, const tie_allocator *allocator) {
  if (n == NULL) {
    return;
  }

  tie_free_parameters_internal(n, allocator);
  release_node(n, allocator);
}

void tie_free_ex(tie_expression *n, const tie_allocator *allocator) {
  tie_free_internal(n, allocator);
}

void tie_free(tie_expression *n) {
  tie_free_internal(n, &default_allocator);
}

static tie_expression *new_expr(state *s, const int type) {
  const int arity = ARITY(type);

  if (s->max_nodes != 0U && s->node_count >= s->max_nodes) {
    set_state_error(s, TIE_ERR_TOO_MANY_NODES);
    return NULL;
  }

  size_t size = sizeof(tie_expression);
  if (arity > 1) {
    size += (size_t) (arity - 1) * sizeof(tie_expression *);
  }

  tie_expression *ret = (tie_expression *) s->allocator->malloc_fn(size, s->allocator->context);
  if (ret == NULL) {
    set_state_error(s, TIE_ERR_OUT_OF_MEMORY);
    return NULL;
  }

#ifdef __cplusplus
  ::new ((void *) ret) tie_expression{};
  for (int i = 0; i < arity; ++i) {
    ret->parameters[i] = NULL;
  }
#else
  memset(ret, 0, size);
#endif
  ret->type = type;
  ++s->node_count;
  return ret;
}

static int enter_depth(state *s) {
  if (s->max_depth != 0U && s->depth >= s->max_depth) {
    set_state_error(s, TIE_ERR_TOO_DEEP);
    return 0;
  }
  ++s->depth;
  return 1;
}

static void leave_depth(state *s) {
  if (s->depth > 0U) {
    --s->depth;
  }
}

static int parse_decimal_literal(const char **cursor, int *value) {
  const char *p = *cursor;
  unsigned int result = 0U;

  do {
    const unsigned int digit = (unsigned int) (*p - '0');
    const unsigned int limit = (unsigned int) INT_MAX;

    if (result > (limit - digit) / 10U) {
      *cursor = p;
      return 0;
    }

    result = result * 10U + digit;
    ++p;
  } while (isdigit((unsigned char) *p));

  *cursor = p;
  *value = (int) result;
  return 1;
}

static int name_matches(const char *candidate, const char *name, const size_t len) {
  const size_t candidate_len = strlen(candidate);
  return candidate_len == len && memcmp(candidate, name, len) == 0;
}

static const tie_builtin *find_builtin(const char *name, const size_t len) {
  int imin = 0;
  int imax = (int) (sizeof(builtins) / sizeof(builtins[0])) - 1;

  while (imax >= imin) {
    const int i = imin + ((imax - imin) / 2);
    const size_t builtin_len = strlen(builtins[i].name);
    const size_t compare_len = len < builtin_len ? len : builtin_len;
    int cmp = memcmp(name, builtins[i].name, compare_len);

    if (cmp == 0) {
      if (len < builtin_len) {
        cmp = -1;
      } else if (len > builtin_len) {
        cmp = 1;
      }
    }

    if (cmp == 0) {
      return builtins + i;
    }
    if (cmp > 0) {
      imin = i + 1;
    } else {
      imax = i - 1;
    }
  }

  return NULL;
}

static const tie_variable *find_lookup(const state *s, const char *name, const size_t len) {
  if (s->lookup == NULL || s->lookup_len <= 0) {
    return NULL;
  }

  for (int i = 0; i < s->lookup_len; ++i) {
    const tie_variable *var = s->lookup + i;
    if (var->name != NULL && name_matches(var->name, name, len)) {
      return var;
    }
  }

  return NULL;
}

static int is_name_start(const char c) {
  return isalpha((unsigned char) c);
}

static int is_name_char(const char c) {
  return isalnum((unsigned char) c) || c == '_';
}

static void next_token(state *s) {
  s->type = NULL_TOKEN;

  do {
    const unsigned char current = (unsigned char) *s->next;

    if (current == '\0') {
      s->type = END_TOKEN;
      return;
    }

    if (isdigit(current)) {
      if (!parse_decimal_literal(&s->next, &s->value)) {
        set_state_error(s, TIE_ERR_OVERFLOW);
        return;
      }
      s->type = NUMBER_TOKEN;
      return;
    }

    if (is_name_start((char) current)) {
      const char *start = s->next;
      do {
        ++s->next;
      } while (is_name_char(*s->next));

      const size_t len = (size_t) (s->next - start);
      const tie_variable *var = find_lookup(s, start, len);

      if (var != NULL) {
        switch (TYPE_MASK(var->type)) {
          case TIE_VARIABLE:
            s->type = VARIABLE_TOKEN;
            s->bound = var->address.bound;
            return;

          case TIE_FUNCTION0:
          case TIE_FUNCTION1:
          case TIE_FUNCTION2:
          case TIE_FUNCTION3:
          case TIE_FUNCTION4:
          case TIE_FUNCTION5:
          case TIE_FUNCTION6:
          case TIE_FUNCTION7:
          case TIE_CLOSURE0:
          case TIE_CLOSURE1:
          case TIE_CLOSURE2:
          case TIE_CLOSURE3:
          case TIE_CLOSURE4:
          case TIE_CLOSURE5:
          case TIE_CLOSURE6:
          case TIE_CLOSURE7:
            s->type = var->type;
            s->callable = var->address;
            s->context = var->context;
            return;

          default:
            set_state_error(s, TIE_ERR_PARSE);
            return;
        }
      }

      const tie_builtin *builtin = find_builtin(start, len);
      if (builtin != NULL) {
        s->type = builtin->type;
        s->operation = builtin->operation;
        return;
      }

      set_state_error(s, TIE_ERR_PARSE);
      return;
    }

    ++s->next;
    switch (current) {
      case '&':
        s->type = INFIX_TOKEN;
        s->operation = TIE_OP_BIT_AND;
        return;
      case '|':
        s->type = INFIX_TOKEN;
        s->operation = TIE_OP_BIT_OR;
        return;
      case '^':
        s->type = INFIX_TOKEN;
        s->operation = TIE_OP_BIT_XOR;
        return;
      case '~':
        s->type = INFIX_TOKEN;
        s->operation = TIE_OP_BIT_NOT;
        return;
      case '>':
        if (*s->next == '>') {
          ++s->next;
        }
        s->type = INFIX_TOKEN;
        s->operation = TIE_OP_SHR;
        return;
      case '<':
        if (*s->next == '<') {
          ++s->next;
        }
        s->type = INFIX_TOKEN;
        s->operation = TIE_OP_SHL;
        return;
      case '+':
        s->type = INFIX_TOKEN;
        s->operation = TIE_OP_ADD;
        return;
      case '-':
        s->type = INFIX_TOKEN;
        s->operation = TIE_OP_SUB;
        return;
      case '*':
        s->type = INFIX_TOKEN;
        s->operation = TIE_OP_MUL;
        return;
      case '/':
        s->type = INFIX_TOKEN;
        s->operation = TIE_OP_DIV;
        return;
      case '%':
        s->type = INFIX_TOKEN;
        s->operation = TIE_OP_MOD;
        return;
      case '(':
        s->type = OPEN_TOKEN;
        return;
      case ')':
        s->type = CLOSE_TOKEN;
        return;
      case ',':
        s->type = SEPARATOR_TOKEN;
        return;
      default:
        if (isspace(current)) {
          break;
        }
        set_state_error(s, TIE_ERR_PARSE);
        return;
    }
  } while (s->type == NULL_TOKEN);
}

static tie_expression *parse_list(state *s);
static tie_expression *parse_bit_or(state *s);
static tie_expression *parse_unary(state *s);

static tie_expression *make_builtin1(state *s, const tie_operation operation, tie_expression *child) {
  tie_expression *ret = new_expr(s, TIE_BUILTIN1 | TIE_FLAG_PURE);
  if (ret == NULL) {
    return NULL;
  }

  ret->data.operation = operation;
  ret->parameters[0] = child;
  return ret;
}

static tie_expression *make_builtin2(state *s, const tie_operation operation, tie_expression *left, tie_expression *right) {
  tie_expression *ret = new_expr(s, TIE_BUILTIN2 | TIE_FLAG_PURE);
  if (ret == NULL) {
    return NULL;
  }

  ret->data.operation = operation;
  ret->parameters[0] = left;
  ret->parameters[1] = right;
  return ret;
}

static tie_expression *parse_base(state *s) {
  tie_expression *ret = NULL;

  switch (TYPE_MASK(s->type)) {
    case NUMBER_TOKEN:
      ret = new_expr(s, TIE_CONSTANT);
      if (ret == NULL) {
        return NULL;
      }
      ret->data.value = s->value;
      next_token(s);
      return ret;

    case VARIABLE_TOKEN:
      if (s->bound == NULL) {
        set_state_error(s, TIE_ERR_INVALID_ARGUMENT);
        return NULL;
      }
      ret = new_expr(s, TIE_VARIABLE);
      if (ret == NULL) {
        return NULL;
      }
      ret->data.bound = s->bound;
      next_token(s);
      return ret;

    case TIE_FUNCTION0:
    case TIE_CLOSURE0:
      ret = new_expr(s, s->type);
      if (ret == NULL) {
        return NULL;
      }
      ret->data.callable = s->callable;
      ret->context = s->context;
      next_token(s);
      if (s->type == OPEN_TOKEN) {
        next_token(s);
        if (s->type != CLOSE_TOKEN) {
          set_state_error(s, TIE_ERR_PARSE);
          tie_free_internal(ret, s->allocator);
          return NULL;
        }
        next_token(s);
      }
      return ret;

    case TIE_BUILTIN1:
    case TIE_FUNCTION1:
    case TIE_CLOSURE1: {
      const int type = s->type;
      const tie_operation operation = s->operation;
      const tie_callable callable = s->callable;
      void *context = s->context;

      ret = new_expr(s, type);
      if (ret == NULL) {
        return NULL;
      }
      ret->context = context;
      if (TYPE_MASK(type) == TIE_BUILTIN1) {
        ret->data.operation = operation;
      } else {
        ret->data.callable = callable;
      }

      next_token(s);
      if (!enter_depth(s)) {
        tie_free_internal(ret, s->allocator);
        return NULL;
      }
      ret->parameters[0] = parse_unary(s);
      leave_depth(s);

      if (ret->parameters[0] == NULL) {
        tie_free_internal(ret, s->allocator);
        return NULL;
      }
      return ret;
    }

    case TIE_BUILTIN2:
    case TIE_BUILTIN3:
    case TIE_FUNCTION2:
    case TIE_FUNCTION3:
    case TIE_FUNCTION4:
    case TIE_FUNCTION5:
    case TIE_FUNCTION6:
    case TIE_FUNCTION7:
    case TIE_CLOSURE2:
    case TIE_CLOSURE3:
    case TIE_CLOSURE4:
    case TIE_CLOSURE5:
    case TIE_CLOSURE6:
    case TIE_CLOSURE7: {
      const int type = s->type;
      const int arity = ARITY(type);
      const tie_operation operation = s->operation;
      const tie_callable callable = s->callable;
      void *context = s->context;

      ret = new_expr(s, type);
      if (ret == NULL) {
        return NULL;
      }
      ret->context = context;
      if (TYPE_MASK(type) == TIE_BUILTIN2 || TYPE_MASK(type) == TIE_BUILTIN3) {
        ret->data.operation = operation;
      } else {
        ret->data.callable = callable;
      }

      next_token(s);
      if (s->type != OPEN_TOKEN) {
        set_state_error(s, TIE_ERR_PARSE);
        tie_free_internal(ret, s->allocator);
        return NULL;
      }

      next_token(s);
      for (int i = 0; i < arity; ++i) {
        if (!enter_depth(s)) {
          tie_free_internal(ret, s->allocator);
          return NULL;
        }
        ret->parameters[i] = parse_bit_or(s);
        leave_depth(s);

        if (ret->parameters[i] == NULL) {
          tie_free_internal(ret, s->allocator);
          return NULL;
        }

        if (i + 1 < arity) {
          if (s->type != SEPARATOR_TOKEN) {
            set_state_error(s, TIE_ERR_PARSE);
            tie_free_internal(ret, s->allocator);
            return NULL;
          }
          next_token(s);
        }
      }

      if (s->type != CLOSE_TOKEN) {
        set_state_error(s, TIE_ERR_PARSE);
        tie_free_internal(ret, s->allocator);
        return NULL;
      }
      next_token(s);
      return ret;
    }

    case OPEN_TOKEN:
      next_token(s);
      if (!enter_depth(s)) {
        return NULL;
      }
      ret = parse_list(s);
      leave_depth(s);

      if (ret == NULL) {
        return NULL;
      }
      if (s->type != CLOSE_TOKEN) {
        set_state_error(s, TIE_ERR_PARSE);
        tie_free_internal(ret, s->allocator);
        return NULL;
      }
      next_token(s);
      return ret;

    default:
      set_state_error(s, TIE_ERR_PARSE);
      return NULL;
  }
}

static tie_expression *parse_unary(state *s) {
  if (s->type == INFIX_TOKEN && s->operation == TIE_OP_ADD) {
    next_token(s);
    return parse_unary(s);
  }

  if (s->type == INFIX_TOKEN && (s->operation == TIE_OP_SUB || s->operation == TIE_OP_BIT_NOT)) {
    const tie_operation operation = s->operation == TIE_OP_SUB ? TIE_OP_NEG : TIE_OP_BIT_NOT;
    next_token(s);

    if (!enter_depth(s)) {
      return NULL;
    }
    tie_expression *child = parse_unary(s);
    leave_depth(s);
    if (child == NULL) {
      return NULL;
    }

    tie_expression *ret = make_builtin1(s, operation, child);
    if (ret == NULL) {
      tie_free_internal(child, s->allocator);
      return NULL;
    }
    return ret;
  }

  return parse_base(s);
}

static tie_expression *parse_term(state *s) {
  tie_expression *ret = parse_unary(s);
  if (ret == NULL) {
    return NULL;
  }

  while (s->type == INFIX_TOKEN &&
         (s->operation == TIE_OP_MUL || s->operation == TIE_OP_DIV || s->operation == TIE_OP_MOD)) {
    const tie_operation operation = s->operation;
    next_token(s);

    tie_expression *right = parse_unary(s);
    if (right == NULL) {
      tie_free_internal(ret, s->allocator);
      return NULL;
    }

    tie_expression *left = ret;
    ret = make_builtin2(s, operation, left, right);
    if (ret == NULL) {
      tie_free_internal(left, s->allocator);
      tie_free_internal(right, s->allocator);
      return NULL;
    }
  }

  return ret;
}

static tie_expression *parse_additive(state *s) {
  tie_expression *ret = parse_term(s);
  if (ret == NULL) {
    return NULL;
  }

  while (s->type == INFIX_TOKEN && (s->operation == TIE_OP_ADD || s->operation == TIE_OP_SUB)) {
    const tie_operation operation = s->operation;
    next_token(s);

    tie_expression *right = parse_term(s);
    if (right == NULL) {
      tie_free_internal(ret, s->allocator);
      return NULL;
    }

    tie_expression *left = ret;
    ret = make_builtin2(s, operation, left, right);
    if (ret == NULL) {
      tie_free_internal(left, s->allocator);
      tie_free_internal(right, s->allocator);
      return NULL;
    }
  }

  return ret;
}

static tie_expression *parse_shift(state *s) {
  tie_expression *ret = parse_additive(s);
  if (ret == NULL) {
    return NULL;
  }

  while (s->type == INFIX_TOKEN && (s->operation == TIE_OP_SHL || s->operation == TIE_OP_SHR)) {
    const tie_operation operation = s->operation;
    next_token(s);

    tie_expression *right = parse_additive(s);
    if (right == NULL) {
      tie_free_internal(ret, s->allocator);
      return NULL;
    }

    tie_expression *left = ret;
    ret = make_builtin2(s, operation, left, right);
    if (ret == NULL) {
      tie_free_internal(left, s->allocator);
      tie_free_internal(right, s->allocator);
      return NULL;
    }
  }

  return ret;
}

static tie_expression *parse_bit_and(state *s) {
  tie_expression *ret = parse_shift(s);
  if (ret == NULL) {
    return NULL;
  }

  while (s->type == INFIX_TOKEN && s->operation == TIE_OP_BIT_AND) {
    next_token(s);

    tie_expression *right = parse_shift(s);
    if (right == NULL) {
      tie_free_internal(ret, s->allocator);
      return NULL;
    }

    tie_expression *left = ret;
    ret = make_builtin2(s, TIE_OP_BIT_AND, left, right);
    if (ret == NULL) {
      tie_free_internal(left, s->allocator);
      tie_free_internal(right, s->allocator);
      return NULL;
    }
  }

  return ret;
}

static tie_expression *parse_bit_xor(state *s) {
  tie_expression *ret = parse_bit_and(s);
  if (ret == NULL) {
    return NULL;
  }

  while (s->type == INFIX_TOKEN && s->operation == TIE_OP_BIT_XOR) {
    next_token(s);

    tie_expression *right = parse_bit_and(s);
    if (right == NULL) {
      tie_free_internal(ret, s->allocator);
      return NULL;
    }

    tie_expression *left = ret;
    ret = make_builtin2(s, TIE_OP_BIT_XOR, left, right);
    if (ret == NULL) {
      tie_free_internal(left, s->allocator);
      tie_free_internal(right, s->allocator);
      return NULL;
    }
  }

  return ret;
}

static tie_expression *parse_bit_or(state *s) {
  tie_expression *ret = parse_bit_xor(s);
  if (ret == NULL) {
    return NULL;
  }

  while (s->type == INFIX_TOKEN && s->operation == TIE_OP_BIT_OR) {
    next_token(s);

    tie_expression *right = parse_bit_xor(s);
    if (right == NULL) {
      tie_free_internal(ret, s->allocator);
      return NULL;
    }

    tie_expression *left = ret;
    ret = make_builtin2(s, TIE_OP_BIT_OR, left, right);
    if (ret == NULL) {
      tie_free_internal(left, s->allocator);
      tie_free_internal(right, s->allocator);
      return NULL;
    }
  }

  return ret;
}

static tie_expression *parse_list(state *s) {
  tie_expression *ret = parse_bit_or(s);
  if (ret == NULL) {
    return NULL;
  }

  while (s->type == SEPARATOR_TOKEN) {
    next_token(s);

    tie_expression *right = parse_bit_or(s);
    if (right == NULL) {
      tie_free_internal(ret, s->allocator);
      return NULL;
    }

    tie_expression *left = ret;
    ret = make_builtin2(s, TIE_OP_COMMA, left, right);
    if (ret == NULL) {
      tie_free_internal(left, s->allocator);
      tie_free_internal(right, s->allocator);
      return NULL;
    }
  }

  return ret;
}

static int in_int_range(const intmax_t value) {
  return value >= (intmax_t) INT_MIN && value <= (intmax_t) INT_MAX;
}

static int checked_int_result(const intmax_t value, tie_status *status) {
  if (!in_int_range(value)) {
    set_eval_error(status, TIE_ERR_OVERFLOW);
    return 0;
  }
  return (int) value;
}

static int eval_impl(const tie_expression *n, tie_status *status);

static int eval_builtin(const tie_expression *n, tie_status *status) {
  const tie_operation operation = (tie_operation) n->data.operation;

  if (operation == TIE_OP_IF) {
    const int condition = eval_impl(n->parameters[0], status);
    if (*status != TIE_OK) {
      return 0;
    }
    return eval_impl(n->parameters[condition ? 1 : 2], status);
  }

  if (operation == TIE_OP_COMMA) {
    (void) eval_impl(n->parameters[0], status);
    if (*status != TIE_OK) {
      return 0;
    }
    return eval_impl(n->parameters[1], status);
  }

  if (TYPE_MASK(n->type) == TIE_BUILTIN1) {
    const int a = eval_impl(n->parameters[0], status);
    if (*status != TIE_OK) {
      return 0;
    }

    switch (operation) {
      case TIE_OP_NEG:
        if (a == INT_MIN) {
          set_eval_error(status, TIE_ERR_OVERFLOW);
          return 0;
        }
        return -a;
      case TIE_OP_BIT_NOT:
        return ~a;
      case TIE_OP_ABS:
        if (a == INT_MIN) {
          set_eval_error(status, TIE_ERR_OVERFLOW);
          return 0;
        }
        return a < 0 ? -a : a;
      default:
        set_eval_error(status, TIE_ERR_INVALID_ARGUMENT);
        return 0;
    }
  }

  const int a = eval_impl(n->parameters[0], status);
  if (*status != TIE_OK) {
    return 0;
  }
  const int b = eval_impl(n->parameters[1], status);
  if (*status != TIE_OK) {
    return 0;
  }

  switch (operation) {
    case TIE_OP_ADD:
      return checked_int_result((intmax_t) a + (intmax_t) b, status);
    case TIE_OP_SUB:
      return checked_int_result((intmax_t) a - (intmax_t) b, status);
    case TIE_OP_MUL:
      return checked_int_result((intmax_t) a * (intmax_t) b, status);
    case TIE_OP_DIV:
      if (b == 0) {
        set_eval_error(status, TIE_ERR_DIVIDE_BY_ZERO);
        return 0;
      }
      if (a == INT_MIN && b == -1) {
        set_eval_error(status, TIE_ERR_OVERFLOW);
        return 0;
      }
      return a / b;
    case TIE_OP_MOD:
      if (b == 0) {
        set_eval_error(status, TIE_ERR_MODULO_BY_ZERO);
        return 0;
      }
      if (a == INT_MIN && b == -1) {
        set_eval_error(status, TIE_ERR_OVERFLOW);
        return 0;
      }
      return a % b;
    case TIE_OP_BIT_AND:
      return a & b;
    case TIE_OP_BIT_OR:
      return a | b;
    case TIE_OP_BIT_XOR:
      return a ^ b;
    case TIE_OP_SHL:
    case TIE_OP_SHR: {
      const int width = (int) (sizeof(int) * CHAR_BIT);
      if (b < 0 || b >= width) {
        set_eval_error(status, TIE_ERR_SHIFT_RANGE);
        return 0;
      }
      if (operation == TIE_OP_SHL) {
        return (int) ((unsigned int) a << b);
      }
      return (int) ((unsigned int) a >> b);
    }
    default:
      set_eval_error(status, TIE_ERR_INVALID_ARGUMENT);
      return 0;
  }
}

static int eval_function(const tie_expression *n, tie_status *status) {
  int values[7] = {0, 0, 0, 0, 0, 0, 0};
  const int arity = ARITY(n->type);

  for (int i = 0; i < arity; ++i) {
    values[i] = eval_impl(n->parameters[i], status);
    if (*status != TIE_OK) {
      return 0;
    }
  }

  switch (TYPE_MASK(n->type)) {
    case TIE_FUNCTION0:
      if (n->data.callable.function0 == NULL) {
        set_eval_error(status, TIE_ERR_INVALID_ARGUMENT);
        return 0;
      }
      return n->data.callable.function0();
    case TIE_FUNCTION1:
      if (n->data.callable.function1 == NULL) {
        set_eval_error(status, TIE_ERR_INVALID_ARGUMENT);
        return 0;
      }
      return n->data.callable.function1(values[0]);
    case TIE_FUNCTION2:
      if (n->data.callable.function2 == NULL) {
        set_eval_error(status, TIE_ERR_INVALID_ARGUMENT);
        return 0;
      }
      return n->data.callable.function2(values[0], values[1]);
    case TIE_FUNCTION3:
      if (n->data.callable.function3 == NULL) {
        set_eval_error(status, TIE_ERR_INVALID_ARGUMENT);
        return 0;
      }
      return n->data.callable.function3(values[0], values[1], values[2]);
    case TIE_FUNCTION4:
      if (n->data.callable.function4 == NULL) {
        set_eval_error(status, TIE_ERR_INVALID_ARGUMENT);
        return 0;
      }
      return n->data.callable.function4(values[0], values[1], values[2], values[3]);
    case TIE_FUNCTION5:
      if (n->data.callable.function5 == NULL) {
        set_eval_error(status, TIE_ERR_INVALID_ARGUMENT);
        return 0;
      }
      return n->data.callable.function5(values[0], values[1], values[2], values[3], values[4]);
    case TIE_FUNCTION6:
      if (n->data.callable.function6 == NULL) {
        set_eval_error(status, TIE_ERR_INVALID_ARGUMENT);
        return 0;
      }
      return n->data.callable.function6(values[0], values[1], values[2], values[3], values[4], values[5]);
    case TIE_FUNCTION7:
      if (n->data.callable.function7 == NULL) {
        set_eval_error(status, TIE_ERR_INVALID_ARGUMENT);
        return 0;
      }
      return n->data.callable.function7(values[0], values[1], values[2], values[3], values[4], values[5], values[6]);
    case TIE_CLOSURE0:
      if (n->data.callable.closure0 == NULL) {
        set_eval_error(status, TIE_ERR_INVALID_ARGUMENT);
        return 0;
      }
      return n->data.callable.closure0(n->context);
    case TIE_CLOSURE1:
      if (n->data.callable.closure1 == NULL) {
        set_eval_error(status, TIE_ERR_INVALID_ARGUMENT);
        return 0;
      }
      return n->data.callable.closure1(n->context, values[0]);
    case TIE_CLOSURE2:
      if (n->data.callable.closure2 == NULL) {
        set_eval_error(status, TIE_ERR_INVALID_ARGUMENT);
        return 0;
      }
      return n->data.callable.closure2(n->context, values[0], values[1]);
    case TIE_CLOSURE3:
      if (n->data.callable.closure3 == NULL) {
        set_eval_error(status, TIE_ERR_INVALID_ARGUMENT);
        return 0;
      }
      return n->data.callable.closure3(n->context, values[0], values[1], values[2]);
    case TIE_CLOSURE4:
      if (n->data.callable.closure4 == NULL) {
        set_eval_error(status, TIE_ERR_INVALID_ARGUMENT);
        return 0;
      }
      return n->data.callable.closure4(n->context, values[0], values[1], values[2], values[3]);
    case TIE_CLOSURE5:
      if (n->data.callable.closure5 == NULL) {
        set_eval_error(status, TIE_ERR_INVALID_ARGUMENT);
        return 0;
      }
      return n->data.callable.closure5(n->context, values[0], values[1], values[2], values[3], values[4]);
    case TIE_CLOSURE6:
      if (n->data.callable.closure6 == NULL) {
        set_eval_error(status, TIE_ERR_INVALID_ARGUMENT);
        return 0;
      }
      return n->data.callable.closure6(n->context, values[0], values[1], values[2], values[3], values[4], values[5]);
    case TIE_CLOSURE7:
      if (n->data.callable.closure7 == NULL) {
        set_eval_error(status, TIE_ERR_INVALID_ARGUMENT);
        return 0;
      }
      return n->data.callable.closure7(n->context, values[0], values[1], values[2], values[3], values[4], values[5], values[6]);
    default:
      set_eval_error(status, TIE_ERR_INVALID_ARGUMENT);
      return 0;
  }
}

static int eval_impl(const tie_expression *n, tie_status *status) {
  if (n == NULL) {
    set_eval_error(status, TIE_ERR_INVALID_ARGUMENT);
    return 0;
  }

  switch (TYPE_MASK(n->type)) {
    case TIE_CONSTANT:
      return n->data.value;
    case TIE_VARIABLE:
      if (n->data.bound == NULL) {
        set_eval_error(status, TIE_ERR_INVALID_ARGUMENT);
        return 0;
      }
      return *n->data.bound;
    case TIE_BUILTIN1:
    case TIE_BUILTIN2:
    case TIE_BUILTIN3:
      return eval_builtin(n, status);
    case TIE_FUNCTION0:
    case TIE_FUNCTION1:
    case TIE_FUNCTION2:
    case TIE_FUNCTION3:
    case TIE_FUNCTION4:
    case TIE_FUNCTION5:
    case TIE_FUNCTION6:
    case TIE_FUNCTION7:
    case TIE_CLOSURE0:
    case TIE_CLOSURE1:
    case TIE_CLOSURE2:
    case TIE_CLOSURE3:
    case TIE_CLOSURE4:
    case TIE_CLOSURE5:
    case TIE_CLOSURE6:
    case TIE_CLOSURE7:
      return eval_function(n, status);
    default:
      set_eval_error(status, TIE_ERR_INVALID_ARGUMENT);
      return 0;
  }
}

int tie_eval_status(const tie_expression *n, tie_status *status) {
  tie_status local_status = TIE_OK;
  int ret = eval_impl(n, &local_status);

  if (status != NULL) {
    *status = local_status;
  }
  return local_status == TIE_OK ? ret : 0;
}

int tie_eval(const tie_expression *n) {
  return tie_eval_status(n, NULL);
}

static int is_constant(const tie_expression *n) {
  return n != NULL && TYPE_MASK(n->type) == TIE_CONSTANT;
}

static void optimize(tie_expression *n, const tie_allocator *allocator) {
  if (n == NULL || TYPE_MASK(n->type) == TIE_CONSTANT || TYPE_MASK(n->type) == TIE_VARIABLE) {
    return;
  }

  const int arity = ARITY(n->type);
  int known = 1;

  for (int i = 0; i < arity; ++i) {
    optimize(n->parameters[i], allocator);
    if (!is_constant(n->parameters[i])) {
      known = 0;
    }
  }

  if (known && IS_PURE(n->type)) {
    tie_status status = TIE_OK;
    const int value = eval_impl(n, &status);
    if (status == TIE_OK) {
      tie_free_parameters_internal(n, allocator);
      n->type = TIE_CONSTANT;
      n->data.value = value;
      n->context = NULL;
    }
  }
}

static int error_position(const state *s) {
  ptrdiff_t position = s->next - s->start;
  if (position < 1) {
    return 1;
  }
  if (position > (ptrdiff_t) INT_MAX) {
    return INT_MAX;
  }
  return (int) position;
}

tie_expression *tie_compile_ex(const char *expression, const tie_variable *variables, int var_count,
                               int *error, tie_status *status, const tie_options *options) {
  if (error != NULL) {
    *error = 0;
  }
  if (status != NULL) {
    *status = TIE_OK;
  }

  if (expression == NULL || var_count < 0) {
    if (status != NULL) {
      *status = TIE_ERR_INVALID_ARGUMENT;
    }
    if (error != NULL) {
      *error = -1;
    }
    return NULL;
  }

#ifdef __cplusplus
  state s = {};
#else
  state s;
  memset(&s, 0, sizeof(s));
#endif
  s.start = expression;
  s.next = expression;
  s.lookup = variables;
  s.lookup_len = var_count;
  s.status = TIE_OK;
  s.allocator = allocator_for_alloc(options != NULL ? options->allocator : NULL);
  s.max_nodes = options != NULL && options->max_nodes != 0U ? options->max_nodes : TIE_DEFAULT_MAX_NODES;
  s.max_depth = options != NULL && options->max_depth != 0U ? options->max_depth : TIE_DEFAULT_MAX_DEPTH;

  next_token(&s);
  tie_expression *root = parse_list(&s);

  if (root == NULL) {
    if (status != NULL) {
      *status = s.status == TIE_OK ? TIE_ERR_PARSE : s.status;
    }
    if (error != NULL) {
      *error = s.status == TIE_ERR_OUT_OF_MEMORY ? -1 : error_position(&s);
    }
    return NULL;
  }

  if (s.status != TIE_OK || s.type != END_TOKEN) {
    if (s.status == TIE_OK) {
      s.status = TIE_ERR_PARSE;
    }
    tie_free_internal(root, s.allocator);
    if (status != NULL) {
      *status = s.status;
    }
    if (error != NULL) {
      *error = error_position(&s);
    }
    return NULL;
  }

  optimize(root, s.allocator);
  if (status != NULL) {
    *status = TIE_OK;
  }
  if (error != NULL) {
    *error = 0;
  }
  return root;
}

tie_expression *tie_compile(const char *expression, const tie_variable *variables, int var_count, int *error) {
  return tie_compile_ex(expression, variables, var_count, error, NULL, NULL);
}

int tie_interp_status(const char *expression, int *error, tie_status *status, const tie_options *options) {
  tie_status local_status = TIE_OK;
  tie_expression *n = tie_compile_ex(expression, NULL, 0, error, &local_status, options);

  if (n == NULL) {
    if (status != NULL) {
      *status = local_status;
    }
    return 0;
  }

  const int ret = tie_eval_status(n, &local_status);
  tie_free_ex(n, options != NULL ? options->allocator : NULL);

  if (status != NULL) {
    *status = local_status;
  }
  return local_status == TIE_OK ? ret : 0;
}

int tie_interp(const char *expression, int *error) {
  return tie_interp_status(expression, error, NULL, NULL);
}

const char *tie_status_message(tie_status status) {
  switch (status) {
    case TIE_OK:
      return "ok";
    case TIE_ERR_PARSE:
      return "parse error";
    case TIE_ERR_OUT_OF_MEMORY:
      return "out of memory";
    case TIE_ERR_DIVIDE_BY_ZERO:
      return "divide by zero";
    case TIE_ERR_MODULO_BY_ZERO:
      return "modulo by zero";
    case TIE_ERR_SHIFT_RANGE:
      return "shift out of range";
    case TIE_ERR_OVERFLOW:
      return "integer overflow";
    case TIE_ERR_TOO_DEEP:
      return "expression too deep";
    case TIE_ERR_TOO_MANY_NODES:
      return "too many expression nodes";
    case TIE_ERR_INVALID_ARGUMENT:
      return "invalid argument";
    default:
      return "unknown error";
  }
}

#ifndef TIE_DISABLE_PRINT
static const char *operation_name(const tie_operation operation) {
  switch (operation) {
    case TIE_OP_ADD:
      return "+";
    case TIE_OP_SUB:
      return "-";
    case TIE_OP_MUL:
      return "*";
    case TIE_OP_DIV:
      return "/";
    case TIE_OP_MOD:
      return "%";
    case TIE_OP_NEG:
      return "neg";
    case TIE_OP_BIT_NOT:
      return "~";
    case TIE_OP_BIT_AND:
      return "&";
    case TIE_OP_BIT_OR:
      return "|";
    case TIE_OP_BIT_XOR:
      return "^";
    case TIE_OP_SHL:
      return "<<";
    case TIE_OP_SHR:
      return ">>";
    case TIE_OP_COMMA:
      return ",";
    case TIE_OP_IF:
      return "if";
    case TIE_OP_ABS:
      return "abs";
    default:
      return "?";
  }
}

static void print_node(const tie_expression *n, int depth) {
  if (n == NULL) {
    printf("%*s(null)\n", depth, "");
    return;
  }

  printf("%*s", depth, "");
  switch (TYPE_MASK(n->type)) {
    case TIE_CONSTANT:
      printf("%d\n", n->data.value);
      return;
    case TIE_VARIABLE:
      printf("bound %p\n", (const void *) (const int *) n->data.bound);
      return;
    case TIE_BUILTIN1:
    case TIE_BUILTIN2:
    case TIE_BUILTIN3:
      printf("%s\n", operation_name((tie_operation) n->data.operation));
      break;
    case TIE_FUNCTION0:
    case TIE_FUNCTION1:
    case TIE_FUNCTION2:
    case TIE_FUNCTION3:
    case TIE_FUNCTION4:
    case TIE_FUNCTION5:
    case TIE_FUNCTION6:
    case TIE_FUNCTION7:
    case TIE_CLOSURE0:
    case TIE_CLOSURE1:
    case TIE_CLOSURE2:
    case TIE_CLOSURE3:
    case TIE_CLOSURE4:
    case TIE_CLOSURE5:
    case TIE_CLOSURE6:
    case TIE_CLOSURE7:
      printf("f%d\n", ARITY(n->type));
      break;
    default:
      printf("unknown\n");
      return;
  }

  for (int i = 0; i < ARITY(n->type); ++i) {
    print_node(n->parameters[i], depth + 1);
  }
}

void tie_print(const tie_expression *n) {
  print_node(n, 0);
}
#endif
