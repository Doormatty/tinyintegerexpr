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
  TIE_BUILTIN3,
  TIE_FAST_CHAIN
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

typedef enum tie_fast_instruction_type {
  TIE_FAST_NEG,
  TIE_FAST_BIT_NOT,
  TIE_FAST_ABS,
  TIE_FAST_ADD_RIGHT,
  TIE_FAST_SUB_RIGHT,
  TIE_FAST_SUB_LEFT,
  TIE_FAST_MUL_RIGHT,
  TIE_FAST_DIV_RIGHT,
  TIE_FAST_DIV_LEFT,
  TIE_FAST_MOD_RIGHT,
  TIE_FAST_MOD_LEFT,
  TIE_FAST_BIT_AND_RIGHT,
  TIE_FAST_BIT_OR_RIGHT,
  TIE_FAST_BIT_XOR_RIGHT,
  TIE_FAST_SHL_RIGHT,
  TIE_FAST_SHL_LEFT,
  TIE_FAST_SHR_RIGHT,
  TIE_FAST_SHR_LEFT
} tie_fast_instruction_type;

typedef enum tie_fast_chain_shape {
  TIE_FAST_SHAPE_GENERIC,
  TIE_FAST_SHAPE_ADD,
  TIE_FAST_SHAPE_ADD_ADD,
  TIE_FAST_SHAPE_ADD_ABS,
  TIE_FAST_SHAPE_ADD_MUL,
  TIE_FAST_SHAPE_BIT_AND_SHL_XOR
} tie_fast_chain_shape;

typedef struct tie_fast_instruction {
  tie_fast_instruction_type type;
  int value;
} tie_fast_instruction;

typedef struct tie_fast_chain {
  const volatile int *bound;
  int instruction_count;
  tie_fast_chain_shape shape;
  tie_fast_instruction instructions[1];
} tie_fast_chain;

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

#define TIE_CONST_RIGHT 0x00000040
#define TIE_CONST_LEFT 0x00000080
#define TIE_ENCODED_OPERATION_SHIFT 8
#define TIE_ENCODED_OPERATION_MASK 0x0000FF00
#define HAS_INLINE_CONSTANT(TYPE) (((TYPE) & (TIE_CONST_RIGHT | TIE_CONST_LEFT)) != 0)
#define INLINE_CONSTANT_LEFT(TYPE) (((TYPE) & TIE_CONST_LEFT) != 0)
#define ENCODE_OPERATION(OPERATION) (((int) (OPERATION)) << TIE_ENCODED_OPERATION_SHIFT)
#define DECODE_OPERATION(TYPE) \
  ((tie_operation) (((TYPE) & TIE_ENCODED_OPERATION_MASK) >> TIE_ENCODED_OPERATION_SHIFT))

#if defined(__GNUC__) || defined(__clang__)
#define TIE_HAS_OVERFLOW_BUILTINS 1
#define TIE_FORCE_INLINE static inline __attribute__((always_inline))
#define TIE_NOINLINE static __attribute__((noinline))
#else
#define TIE_HAS_OVERFLOW_BUILTINS 0
#define TIE_FORCE_INLINE static inline
#define TIE_NOINLINE static
#endif

#ifndef TIE_FAST_CHAIN_LIMIT
#define TIE_FAST_CHAIN_LIMIT 128
#endif

static int arity_of(const int type) {
  switch (TYPE_MASK(type)) {
    case TIE_BUILTIN1:
      return 1;
    case TIE_BUILTIN2:
      return 2;
    case TIE_BUILTIN3:
      return 3;
    case TIE_FAST_CHAIN:
      return 1;
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

TIE_FORCE_INLINE tie_operation node_operation(const tie_expression *n) {
  if (HAS_INLINE_CONSTANT(n->type)) {
    return DECODE_OPERATION(n->type);
  }
  return (tie_operation) n->data.operation;
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
  if (TYPE_MASK(n->type) == TIE_FAST_CHAIN && n->context != NULL && actual->free_fn != NULL) {
    actual->free_fn(n->context, actual->context);
    n->context = NULL;
  }
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

static tie_expression *allocate_expr(const tie_allocator *allocator, const int type) {
  const int arity = ARITY(type);
  size_t size = sizeof(tie_expression);

  if (arity > 1) {
    size += (size_t) (arity - 1) * sizeof(tie_expression *);
  }

  tie_expression *ret = (tie_expression *) allocator->malloc_fn(size, allocator->context);
  if (ret == NULL) {
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
  return ret;
}

static tie_expression *new_expr(state *s, const int type) {
  if (s->max_nodes != 0U && s->node_count >= s->max_nodes) {
    set_state_error(s, TIE_ERR_TOO_MANY_NODES);
    return NULL;
  }

  tie_expression *ret = allocate_expr(s->allocator, type);
  if (ret == NULL) {
    set_state_error(s, TIE_ERR_OUT_OF_MEMORY);
    return NULL;
  }

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

#if !TIE_HAS_OVERFLOW_BUILTINS
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
#endif

TIE_FORCE_INLINE int checked_add(const int a, const int b, tie_status *status) {
#if TIE_HAS_OVERFLOW_BUILTINS
  int result = 0;
  if (__builtin_add_overflow(a, b, &result)) {
    set_eval_error(status, TIE_ERR_OVERFLOW);
    return 0;
  }
  return result;
#else
  return checked_int_result((intmax_t) a + (intmax_t) b, status);
#endif
}

TIE_FORCE_INLINE int checked_sub(const int a, const int b, tie_status *status) {
#if TIE_HAS_OVERFLOW_BUILTINS
  int result = 0;
  if (__builtin_sub_overflow(a, b, &result)) {
    set_eval_error(status, TIE_ERR_OVERFLOW);
    return 0;
  }
  return result;
#else
  return checked_int_result((intmax_t) a - (intmax_t) b, status);
#endif
}

TIE_FORCE_INLINE int checked_mul(const int a, const int b, tie_status *status) {
#if TIE_HAS_OVERFLOW_BUILTINS
  int result = 0;
  if (__builtin_mul_overflow(a, b, &result)) {
    set_eval_error(status, TIE_ERR_OVERFLOW);
    return 0;
  }
  return result;
#else
  return checked_int_result((intmax_t) a * (intmax_t) b, status);
#endif
}

TIE_FORCE_INLINE int eval_binary_operation(const tie_operation operation, const int a, const int b, tie_status *status) {
  switch (operation) {
    case TIE_OP_ADD:
      return checked_add(a, b, status);
    case TIE_OP_SUB:
      return checked_sub(a, b, status);
    case TIE_OP_MUL:
      return checked_mul(a, b, status);
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

TIE_FORCE_INLINE int eval_unary_operation(const tie_operation operation, const int a, tie_status *status) {
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

TIE_FORCE_INLINE int checked_shift_amount(const int amount, tie_status *status) {
  const int width = (int) (sizeof(int) * CHAR_BIT);
  if (amount < 0 || amount >= width) {
    set_eval_error(status, TIE_ERR_SHIFT_RANGE);
    return 0;
  }
  return 1;
}

static int eval_fast_chain(const tie_fast_chain *chain, tie_status *status) {
  if (chain == NULL || chain->bound == NULL) {
    set_eval_error(status, TIE_ERR_INVALID_ARGUMENT);
    return 0;
  }

  int value = *chain->bound;

  switch (chain->shape) {
    case TIE_FAST_SHAPE_ADD:
      return checked_add(value, chain->instructions[0].value, status);

    case TIE_FAST_SHAPE_ADD_ADD:
      value = checked_add(value, chain->instructions[0].value, status);
      if (*status != TIE_OK) {
        return 0;
      }
      return checked_add(value, chain->instructions[1].value, status);

    case TIE_FAST_SHAPE_ADD_ABS:
      value = checked_add(value, chain->instructions[0].value, status);
      if (*status != TIE_OK) {
        return 0;
      }
      return eval_unary_operation(TIE_OP_ABS, value, status);

    case TIE_FAST_SHAPE_ADD_MUL:
      value = checked_add(value, chain->instructions[0].value, status);
      if (*status != TIE_OK) {
        return 0;
      }
      return checked_mul(value, chain->instructions[1].value, status);

    case TIE_FAST_SHAPE_BIT_AND_SHL_XOR:
      value &= chain->instructions[0].value;
      value = (int) ((unsigned int) value << chain->instructions[1].value);
      return value ^ chain->instructions[2].value;

    case TIE_FAST_SHAPE_GENERIC:
    default:
      break;
  }

  for (int i = 0; i < chain->instruction_count; ++i) {
    const tie_fast_instruction *instruction = chain->instructions + i;

    switch (instruction->type) {
      case TIE_FAST_NEG:
        if (value == INT_MIN) {
          set_eval_error(status, TIE_ERR_OVERFLOW);
          return 0;
        }
        value = -value;
        break;

      case TIE_FAST_BIT_NOT:
        value = ~value;
        break;

      case TIE_FAST_ABS:
        if (value == INT_MIN) {
          set_eval_error(status, TIE_ERR_OVERFLOW);
          return 0;
        }
        value = value < 0 ? -value : value;
        break;

      case TIE_FAST_ADD_RIGHT:
        value = checked_add(value, instruction->value, status);
        if (*status != TIE_OK) {
          return 0;
        }
        break;

      case TIE_FAST_SUB_RIGHT:
        value = checked_sub(value, instruction->value, status);
        if (*status != TIE_OK) {
          return 0;
        }
        break;

      case TIE_FAST_SUB_LEFT:
        value = checked_sub(instruction->value, value, status);
        if (*status != TIE_OK) {
          return 0;
        }
        break;

      case TIE_FAST_MUL_RIGHT:
        value = checked_mul(value, instruction->value, status);
        if (*status != TIE_OK) {
          return 0;
        }
        break;

      case TIE_FAST_DIV_RIGHT:
        if (instruction->value == 0) {
          set_eval_error(status, TIE_ERR_DIVIDE_BY_ZERO);
          return 0;
        }
        if (value == INT_MIN && instruction->value == -1) {
          set_eval_error(status, TIE_ERR_OVERFLOW);
          return 0;
        }
        value /= instruction->value;
        break;

      case TIE_FAST_DIV_LEFT:
        if (value == 0) {
          set_eval_error(status, TIE_ERR_DIVIDE_BY_ZERO);
          return 0;
        }
        if (instruction->value == INT_MIN && value == -1) {
          set_eval_error(status, TIE_ERR_OVERFLOW);
          return 0;
        }
        value = instruction->value / value;
        break;

      case TIE_FAST_MOD_RIGHT:
        if (instruction->value == 0) {
          set_eval_error(status, TIE_ERR_MODULO_BY_ZERO);
          return 0;
        }
        if (value == INT_MIN && instruction->value == -1) {
          set_eval_error(status, TIE_ERR_OVERFLOW);
          return 0;
        }
        value %= instruction->value;
        break;

      case TIE_FAST_MOD_LEFT:
        if (value == 0) {
          set_eval_error(status, TIE_ERR_MODULO_BY_ZERO);
          return 0;
        }
        if (instruction->value == INT_MIN && value == -1) {
          set_eval_error(status, TIE_ERR_OVERFLOW);
          return 0;
        }
        value = instruction->value % value;
        break;

      case TIE_FAST_BIT_AND_RIGHT:
        value &= instruction->value;
        break;

      case TIE_FAST_BIT_OR_RIGHT:
        value |= instruction->value;
        break;

      case TIE_FAST_BIT_XOR_RIGHT:
        value ^= instruction->value;
        break;

      case TIE_FAST_SHL_RIGHT:
        if (!checked_shift_amount(instruction->value, status)) {
          return 0;
        }
        value = (int) ((unsigned int) value << instruction->value);
        break;

      case TIE_FAST_SHL_LEFT:
        if (!checked_shift_amount(value, status)) {
          return 0;
        }
        value = (int) ((unsigned int) instruction->value << value);
        break;

      case TIE_FAST_SHR_RIGHT:
        if (!checked_shift_amount(instruction->value, status)) {
          return 0;
        }
        value = (int) ((unsigned int) value >> instruction->value);
        break;

      case TIE_FAST_SHR_LEFT:
        if (!checked_shift_amount(value, status)) {
          return 0;
        }
        value = (int) ((unsigned int) instruction->value >> value);
        break;

      default:
        set_eval_error(status, TIE_ERR_INVALID_ARGUMENT);
        return 0;
    }
  }

  return value;
}

TIE_FORCE_INLINE int checked_add_value(const int a, const int b, int *out) {
#if TIE_HAS_OVERFLOW_BUILTINS
  return !__builtin_add_overflow(a, b, out);
#else
  const intmax_t value = (intmax_t) a + (intmax_t) b;
  if (!in_int_range(value)) {
    return 0;
  }
  *out = (int) value;
  return 1;
#endif
}

TIE_FORCE_INLINE int checked_sub_value(const int a, const int b, int *out) {
#if TIE_HAS_OVERFLOW_BUILTINS
  return !__builtin_sub_overflow(a, b, out);
#else
  const intmax_t value = (intmax_t) a - (intmax_t) b;
  if (!in_int_range(value)) {
    return 0;
  }
  *out = (int) value;
  return 1;
#endif
}

TIE_FORCE_INLINE int checked_mul_value(const int a, const int b, int *out) {
#if TIE_HAS_OVERFLOW_BUILTINS
  return !__builtin_mul_overflow(a, b, out);
#else
  const intmax_t value = (intmax_t) a * (intmax_t) b;
  if (!in_int_range(value)) {
    return 0;
  }
  *out = (int) value;
  return 1;
#endif
}

static int eval_fast_chain_or_zero(const tie_fast_chain *chain) {
  if (chain == NULL || chain->bound == NULL) {
    return 0;
  }

  int value = *chain->bound;

  switch (chain->shape) {
    case TIE_FAST_SHAPE_ADD:
      return checked_add_value(value, chain->instructions[0].value, &value) ? value : 0;

    case TIE_FAST_SHAPE_ADD_ADD:
      if (!checked_add_value(value, chain->instructions[0].value, &value) ||
          !checked_add_value(value, chain->instructions[1].value, &value)) {
        return 0;
      }
      return value;

    case TIE_FAST_SHAPE_ADD_ABS:
      if (!checked_add_value(value, chain->instructions[0].value, &value) || value == INT_MIN) {
        return 0;
      }
      return value < 0 ? -value : value;

    case TIE_FAST_SHAPE_ADD_MUL:
      if (!checked_add_value(value, chain->instructions[0].value, &value) ||
          !checked_mul_value(value, chain->instructions[1].value, &value)) {
        return 0;
      }
      return value;

    case TIE_FAST_SHAPE_BIT_AND_SHL_XOR:
      value &= chain->instructions[0].value;
      value = (int) ((unsigned int) value << chain->instructions[1].value);
      return value ^ chain->instructions[2].value;

    case TIE_FAST_SHAPE_GENERIC:
    default:
      break;
  }

  for (int i = 0; i < chain->instruction_count; ++i) {
    const tie_fast_instruction *instruction = chain->instructions + i;

    switch (instruction->type) {
      case TIE_FAST_NEG:
        if (value == INT_MIN) {
          return 0;
        }
        value = -value;
        break;

      case TIE_FAST_BIT_NOT:
        value = ~value;
        break;

      case TIE_FAST_ABS:
        if (value == INT_MIN) {
          return 0;
        }
        value = value < 0 ? -value : value;
        break;

      case TIE_FAST_ADD_RIGHT:
        if (!checked_add_value(value, instruction->value, &value)) {
          return 0;
        }
        break;

      case TIE_FAST_SUB_RIGHT:
        if (!checked_sub_value(value, instruction->value, &value)) {
          return 0;
        }
        break;

      case TIE_FAST_SUB_LEFT:
        if (!checked_sub_value(instruction->value, value, &value)) {
          return 0;
        }
        break;

      case TIE_FAST_MUL_RIGHT:
        if (!checked_mul_value(value, instruction->value, &value)) {
          return 0;
        }
        break;

      case TIE_FAST_DIV_RIGHT:
        if (instruction->value == 0 || (value == INT_MIN && instruction->value == -1)) {
          return 0;
        }
        value /= instruction->value;
        break;

      case TIE_FAST_DIV_LEFT:
        if (value == 0 || (instruction->value == INT_MIN && value == -1)) {
          return 0;
        }
        value = instruction->value / value;
        break;

      case TIE_FAST_MOD_RIGHT:
        if (instruction->value == 0 || (value == INT_MIN && instruction->value == -1)) {
          return 0;
        }
        value %= instruction->value;
        break;

      case TIE_FAST_MOD_LEFT:
        if (value == 0 || (instruction->value == INT_MIN && value == -1)) {
          return 0;
        }
        value = instruction->value % value;
        break;

      case TIE_FAST_BIT_AND_RIGHT:
        value &= instruction->value;
        break;

      case TIE_FAST_BIT_OR_RIGHT:
        value |= instruction->value;
        break;

      case TIE_FAST_BIT_XOR_RIGHT:
        value ^= instruction->value;
        break;

      case TIE_FAST_SHL_RIGHT:
        value = (int) ((unsigned int) value << instruction->value);
        break;

      case TIE_FAST_SHL_LEFT:
        if (value < 0 || value >= (int) (sizeof(int) * CHAR_BIT)) {
          return 0;
        }
        value = (int) ((unsigned int) instruction->value << value);
        break;

      case TIE_FAST_SHR_RIGHT:
        value = (int) ((unsigned int) value >> instruction->value);
        break;

      case TIE_FAST_SHR_LEFT:
        if (value < 0 || value >= (int) (sizeof(int) * CHAR_BIT)) {
          return 0;
        }
        value = (int) ((unsigned int) instruction->value >> value);
        break;

      default:
        return 0;
    }
  }

  return value;
}

static int eval_impl(const tie_expression *n, tie_status *status);

TIE_NOINLINE int eval_builtin(const tie_expression *n, tie_status *status) {
  const tie_operation operation = node_operation(n);

  if (HAS_INLINE_CONSTANT(n->type)) {
    const int value = eval_impl(n->parameters[0], status);
    if (*status != TIE_OK) {
      return 0;
    }
    if (INLINE_CONSTANT_LEFT(n->type)) {
      return eval_binary_operation(operation, n->data.value, value, status);
    }
    return eval_binary_operation(operation, value, n->data.value, status);
  }

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

    return eval_unary_operation(operation, a, status);
  }

  const int a = eval_impl(n->parameters[0], status);
  if (*status != TIE_OK) {
    return 0;
  }
  const int b = eval_impl(n->parameters[1], status);
  if (*status != TIE_OK) {
    return 0;
  }

  return eval_binary_operation(operation, a, b, status);
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
      if (HAS_INLINE_CONSTANT(n->type)) {
        const tie_operation operation = node_operation(n);
        const int value = eval_impl(n->parameters[0], status);
        if (*status != TIE_OK) {
          return 0;
        }
        if (INLINE_CONSTANT_LEFT(n->type)) {
          return eval_binary_operation(operation, n->data.value, value, status);
        }
        return eval_binary_operation(operation, value, n->data.value, status);
      }
      return eval_builtin(n, status);
    case TIE_BUILTIN2:
    case TIE_BUILTIN3:
      return eval_builtin(n, status);
    case TIE_FAST_CHAIN:
      return eval_fast_chain((const tie_fast_chain *) n->context, status);
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
  if (status == NULL && n != NULL && TYPE_MASK(n->type) == TIE_FAST_CHAIN) {
    return eval_fast_chain_or_zero((const tie_fast_chain *) n->context);
  }

  tie_status local_status = TIE_OK;
  int ret = 0;

  if (n != NULL && TYPE_MASK(n->type) == TIE_FAST_CHAIN) {
    ret = eval_fast_chain((const tie_fast_chain *) n->context, &local_status);
  } else {
    ret = eval_impl(n, &local_status);
  }

  if (status != NULL) {
    *status = local_status;
  }
  return local_status == TIE_OK ? ret : 0;
}

int tie_eval(const tie_expression *n) {
  if (n != NULL && TYPE_MASK(n->type) == TIE_FAST_CHAIN) {
    return eval_fast_chain_or_zero((const tie_fast_chain *) n->context);
  }

  tie_status status = TIE_OK;
  int ret = 0;

  ret = eval_impl(n, &status);
  return status == TIE_OK ? ret : 0;
}

static int is_constant(const tie_expression *n) {
  return n != NULL && TYPE_MASK(n->type) == TIE_CONSTANT;
}

static int can_inline_constant_operand(const tie_operation operation) {
  switch (operation) {
    case TIE_OP_ADD:
    case TIE_OP_SUB:
    case TIE_OP_MUL:
    case TIE_OP_DIV:
    case TIE_OP_MOD:
    case TIE_OP_BIT_AND:
    case TIE_OP_BIT_OR:
    case TIE_OP_BIT_XOR:
    case TIE_OP_SHL:
    case TIE_OP_SHR:
      return 1;
    default:
      return 0;
  }
}

static void inline_constant_operand(tie_expression *n, const tie_operation operation,
                                    const int constant_on_left, tie_expression *expr,
                                    tie_expression *constant, const tie_allocator *allocator) {
  const int value = constant->data.value;

  release_node(constant, allocator);
  n->type = TIE_BUILTIN1 | TIE_FLAG_PURE |
            (constant_on_left ? TIE_CONST_LEFT : TIE_CONST_RIGHT) |
            ENCODE_OPERATION(operation);
  n->data.value = value;
  n->context = NULL;
  n->parameters[0] = expr;
  n->parameters[1] = NULL;
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
    return;
  }

  if (TYPE_MASK(n->type) == TIE_BUILTIN2 && IS_PURE(n->type)) {
    const tie_operation operation = node_operation(n);
    if (can_inline_constant_operand(operation)) {
      if (is_constant(n->parameters[1])) {
        inline_constant_operand(n, operation, 0, n->parameters[0], n->parameters[1], allocator);
      } else if (is_constant(n->parameters[0])) {
        inline_constant_operand(n, operation, 1, n->parameters[1], n->parameters[0], allocator);
      }
    }
  }
}

static int can_compile_unary_operation(const tie_operation operation) {
  return operation == TIE_OP_NEG || operation == TIE_OP_BIT_NOT || operation == TIE_OP_ABS;
}

static tie_fast_instruction_type fast_unary_type(const tie_operation operation) {
  switch (operation) {
    case TIE_OP_NEG:
      return TIE_FAST_NEG;
    case TIE_OP_BIT_NOT:
      return TIE_FAST_BIT_NOT;
    case TIE_OP_ABS:
      return TIE_FAST_ABS;
    default:
      return TIE_FAST_NEG;
  }
}

static tie_fast_instruction_type fast_binary_type(const tie_operation operation, const int constant_on_left) {
  switch (operation) {
    case TIE_OP_ADD:
      return TIE_FAST_ADD_RIGHT;
    case TIE_OP_SUB:
      return constant_on_left ? TIE_FAST_SUB_LEFT : TIE_FAST_SUB_RIGHT;
    case TIE_OP_MUL:
      return TIE_FAST_MUL_RIGHT;
    case TIE_OP_DIV:
      return constant_on_left ? TIE_FAST_DIV_LEFT : TIE_FAST_DIV_RIGHT;
    case TIE_OP_MOD:
      return constant_on_left ? TIE_FAST_MOD_LEFT : TIE_FAST_MOD_RIGHT;
    case TIE_OP_BIT_AND:
      return TIE_FAST_BIT_AND_RIGHT;
    case TIE_OP_BIT_OR:
      return TIE_FAST_BIT_OR_RIGHT;
    case TIE_OP_BIT_XOR:
      return TIE_FAST_BIT_XOR_RIGHT;
    case TIE_OP_SHL:
      return constant_on_left ? TIE_FAST_SHL_LEFT : TIE_FAST_SHL_RIGHT;
    case TIE_OP_SHR:
      return constant_on_left ? TIE_FAST_SHR_LEFT : TIE_FAST_SHR_RIGHT;
    default:
      return TIE_FAST_ADD_RIGHT;
  }
}

static int measure_fast_chain_node(const tie_expression *n, const volatile int **bound,
                                   int *instruction_count) {
  if (n == NULL || *instruction_count >= TIE_FAST_CHAIN_LIMIT) {
    return 0;
  }

  switch (TYPE_MASK(n->type)) {
    case TIE_VARIABLE:
      if (n->data.bound == NULL) {
        return 0;
      }
      if (*bound != NULL && *bound != n->data.bound) {
        return 0;
      }
      *bound = n->data.bound;
      return 1;

    case TIE_BUILTIN1:
      if (HAS_INLINE_CONSTANT(n->type)) {
        const tie_operation operation = node_operation(n);
        if (!INLINE_CONSTANT_LEFT(n->type) &&
            (operation == TIE_OP_SHL || operation == TIE_OP_SHR)) {
          const int amount = n->data.value;
          const int width = (int) (sizeof(int) * CHAR_BIT);
          if (amount < 0 || amount >= width) {
            return 0;
          }
        }
        if (!measure_fast_chain_node(n->parameters[0], bound, instruction_count)) {
          return 0;
        }
        ++*instruction_count;
        return 1;
      }
      if (!can_compile_unary_operation(node_operation(n))) {
        return 0;
      }
      if (!measure_fast_chain_node(n->parameters[0], bound, instruction_count)) {
        return 0;
      }
      ++*instruction_count;
      return 1;

    default:
      return 0;
  }
}

static tie_fast_instruction *emit_fast_instruction(tie_fast_chain *chain, int *index,
                                                  const tie_fast_instruction_type type) {
  tie_fast_instruction *instruction = chain->instructions + *index;
  ++*index;
  instruction->type = type;
  instruction->value = 0;
  return instruction;
}

static void emit_fast_chain_node(const tie_expression *n, tie_fast_chain *chain, int *index) {
  tie_fast_instruction *instruction = NULL;

  switch (TYPE_MASK(n->type)) {
    case TIE_VARIABLE:
      return;

    case TIE_BUILTIN1:
      emit_fast_chain_node(n->parameters[0], chain, index);
      if (HAS_INLINE_CONSTANT(n->type)) {
        instruction = emit_fast_instruction(chain, index,
                                            fast_binary_type(node_operation(n), INLINE_CONSTANT_LEFT(n->type)));
        instruction->value = n->data.value;
        return;
      }
      (void) emit_fast_instruction(chain, index, fast_unary_type(node_operation(n)));
      return;

    default:
      return;
  }
}

static tie_fast_chain_shape detect_fast_chain_shape(const tie_fast_chain *chain) {
  const tie_fast_instruction *instructions = chain->instructions;

  if (chain->instruction_count == 1 &&
      instructions[0].type == TIE_FAST_ADD_RIGHT) {
    return TIE_FAST_SHAPE_ADD;
  }

  if (chain->instruction_count == 2 &&
      instructions[0].type == TIE_FAST_ADD_RIGHT) {
    if (instructions[1].type == TIE_FAST_ADD_RIGHT) {
      return TIE_FAST_SHAPE_ADD_ADD;
    }
    if (instructions[1].type == TIE_FAST_ABS) {
      return TIE_FAST_SHAPE_ADD_ABS;
    }
    if (instructions[1].type == TIE_FAST_MUL_RIGHT) {
      return TIE_FAST_SHAPE_ADD_MUL;
    }
  }

  if (chain->instruction_count == 3 &&
      instructions[0].type == TIE_FAST_BIT_AND_RIGHT &&
      instructions[1].type == TIE_FAST_SHL_RIGHT &&
      instructions[2].type == TIE_FAST_BIT_XOR_RIGHT) {
    return TIE_FAST_SHAPE_BIT_AND_SHL_XOR;
  }

  return TIE_FAST_SHAPE_GENERIC;
}

static tie_expression *make_fast_chain_expression(state *s, tie_expression *root) {
  const volatile int *bound = NULL;
  int instruction_count = 0;

  if (!measure_fast_chain_node(root, &bound, &instruction_count) ||
      bound == NULL || instruction_count <= 0) {
    return NULL;
  }
  if (s->max_nodes != 0U && s->node_count >= s->max_nodes) {
    return NULL;
  }

  size_t size = sizeof(tie_fast_chain);
  if (instruction_count > 1) {
    size += (size_t) (instruction_count - 1) * sizeof(tie_fast_instruction);
  }

  tie_fast_chain *chain = (tie_fast_chain *) s->allocator->malloc_fn(size, s->allocator->context);
  if (chain == NULL) {
    return NULL;
  }
  chain->bound = bound;
  chain->instruction_count = instruction_count;
  chain->shape = TIE_FAST_SHAPE_GENERIC;

  int index = 0;
  emit_fast_chain_node(root, chain, &index);
  if (index != instruction_count) {
    if (s->allocator->free_fn != NULL) {
      s->allocator->free_fn(chain, s->allocator->context);
    }
    return NULL;
  }
  chain->shape = detect_fast_chain_shape(chain);

  tie_expression *chain_expr = allocate_expr(s->allocator, TIE_FAST_CHAIN);
  if (chain_expr == NULL) {
    if (s->allocator->free_fn != NULL) {
      s->allocator->free_fn(chain, s->allocator->context);
    }
    return NULL;
  }
  chain_expr->context = chain;
  chain_expr->parameters[0] = root;
  ++s->node_count;
  return chain_expr;
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
  tie_expression *chain = make_fast_chain_expression(&s, root);
  if (chain != NULL) {
    root = chain;
  }
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
      printf("%s\n", operation_name(node_operation(n)));
      break;
    case TIE_FAST_CHAIN:
      print_node(n->parameters[0], depth);
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
