/* Copyright (c) 2004-2013 Sergey Lyubka <valenok@gmail.com>
 * Copyright (c) 2013-2015 Cesanta Software Limited
 * All rights reserved
 *
 * This library is dual-licensed: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation. For the terms of this
 * license, see <http://www.gnu.org/licenses/>.
 *
 * You are free to use this library under the terms of the GNU General
 * Public License, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * Alternatively, you can license this library under a commercial
 * license, as set out in <https://www.cesanta.com/license>.
 */

#define _POSIX_C_SOURCE 200809L

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h> /* for O_RDWR */

#ifndef _WIN32
#include <unistd.h>
#ifndef __WATCOM__
#include <pthread.h>
#endif
#endif

#ifdef V7_UNAMALGAMATED_UNIT_TEST
#include "common/mbuf.h"
#include "v7/src/varint.h"
#include "v7/src/vm.h"
#include "v7/src/types.h"
#include "v7/src/string.h"
#include "v7/src/object.h"
#include "../v7.h"
#include "../src/internal.h"
#include "../src/gc.h"
#include "../src/bcode.h"
#include "../src/compiler.h"
#else
#include "../v7.c"
#endif

#ifdef _WIN32
#define isinf(x) (!_finite(x))
#ifndef NAN
#define NAN atof("NAN")
#endif
/* #define INFINITY    BLAH */
#endif

extern long timezone;

#include "common/test_util.h"

#ifdef _WIN32
#define isnan(x) _isnan(x)
#endif

#define STRINGIFY(x) #x

static enum v7_err eval(struct v7 *v7, const char *code, v7_val_t *res) {
  return v7_exec(v7, code, res);
}

static enum v7_err parse_js(struct v7 *v7, const char *src, struct ast *a) {
  enum v7_err parse_result = parse(v7, a, src, 1 /* verbose */, 0);
  if (parse_result != V7_OK) {
    fprintf(stderr, "Parse error. Expression:\n  %s\nMessage:\n  %s\n", src,
            v7->error_msg);
  }
  return parse_result;
}

int STOP = 0; /* For xcode breakpoints conditions */

static int check_value(struct v7 *v7, val_t v, const char *str) {
  int res = 1;
  char buf[2048];
  char *p = v7_stringify(v7, v, buf, sizeof(buf), V7_STRINGIFY_DEBUG);
  if (strcmp(p, str) != 0) {
    _strfail(p, str, -1);
    res = 0;
  }
  if (p != buf) {
    free(p);
  }
  return res;
}

static int check_js_expr(struct v7 *v7, val_t v_actual,
                         const char *expect_js_expr) {
  int res = 1;
  v7_val_t v_expect;
  enum v7_err e;

  v7_own(v7, &v_actual);

  /* execute expected value */
  e = v7_exec(v7, expect_js_expr, &v_expect);
  v7_own(v7, &v_expect);

  if (e != V7_OK) {
    /* failed to execute expected value */
    printf("Exec expected '%s' failed, err=%d\n", expect_js_expr, e);
    res = 0;
  } else {
    /* now, stringify both values (actual and expected) and compare them */

    char buf_actual[2048];
    char *p_actual = v7_to_json(v7, v_actual, buf_actual, sizeof(buf_actual));

    char buf_expect[2048];
    char *p_expect = v7_to_json(v7, v_expect, buf_expect, sizeof(buf_expect));

    if (strcmp(p_actual, p_expect) != 0) {
      _strfail(p_actual, p_expect, -1);
      res = 0;
    }
    if (p_actual != buf_actual) {
      free(p_actual);
    }

    if (p_expect != buf_expect) {
      free(p_expect);
    }
  }

  v7_disown(v7, &v_expect);
  v7_disown(v7, &v_actual);

  return res;
}

static int check_num(struct v7 *v7, val_t v, double num) {
  int ret = isnan(num) ? isnan(v7_to_number(v)) : v7_to_number(v) == num;
  (void) v7;
  if (!ret) {
    printf("Num: want %f got %f\n", num, v7_to_number(v));
  }

  return ret;
}

static int check_bool(val_t v, int is_true) {
  int b = v7_to_boolean(v);
  return is_true ? b : !b;
}

static int check_str(struct v7 *v7, val_t v, const char *expected) {
  size_t n1, n2 = strlen(expected);
  const char *actual = v7_get_string_data(v7, &v, &n1);
  int ret = (n1 == n2 && memcmp(actual, expected, n1) == 0);
  if (!ret) {
    _strfail(actual, expected, -1);
  }
  return ret;
}

static int test_if_expr(struct v7 *v7, const char *expr, int result) {
  val_t v;
  if (v7_exec(v7, expr, &v) != V7_OK) return 0;
  return result == (v7_is_truthy(v7, v) ? 1 : 0);
}

/*
 * check that bcode stack is zero (should be zero after each call to
 * `v7_exec()`)
 */
#define CHECK_BCODE_STACK_ZERO(v7) (v7->stack.len == 0)
/*
 * Print stack error
 */
#define PRINT_BCODE_STACK_ERROR(v7, js_expr)                \
  do {                                                      \
    printf("Exec '%s': non-zero stack size: %u\n", js_expr, \
           (unsigned int)(v7->stack.len / sizeof(val_t)));  \
  } while (0)

#if defined(UNIT_TEST_TRACE)
#define TRACE_EXPR(js_expr) printf("Executing: '%s' ...\n", js_expr)
#else
#define TRACE_EXPR(js_expr) /* nothing */
#endif

#define ASSERT_EVAL_OK(v7, js_expr)                     \
  do {                                                  \
    v7_val_t v;                                         \
    enum v7_err e;                                      \
    num_tests++;                                        \
    e = v7_exec(v7, js_expr, &v);                       \
    if (e != V7_OK) {                                   \
      printf("Exec '%s' failed, err=%d\n", js_expr, e); \
      FAIL("ASSERT_EVAL_OK(" #js_expr ")", __LINE__);   \
    }                                                   \
  } while (0)

#define _ASSERT_XXX_EVAL_EQ(v7, js_expr, expected, check_fun, eval_fun) \
  do {                                                                  \
    v7_val_t v;                                                         \
    enum v7_err e;                                                      \
    int r = 1;                                                          \
    num_tests++;                                                        \
    TRACE_EXPR(js_expr);                                                \
    e = eval_fun(v7, js_expr, &v);                                      \
    if (e != V7_OK) {                                                   \
      printf("Exec '%s' failed, err=%d\n", js_expr, e);                 \
      r = 0;                                                            \
    } else if (!(CHECK_BCODE_STACK_ZERO(v7))) {                         \
      PRINT_BCODE_STACK_ERROR(v7, js_expr);                             \
      r = 0;                                                            \
    } else {                                                            \
      r = check_fun(v7, v, expected);                                   \
    }                                                                   \
    if (r == 0) {                                                       \
      FAIL("ASSERT_EVAL_EQ(" #js_expr ", " #expected ")", __LINE__);    \
    }                                                                   \
  } while (0)

#define _ASSERT_XXX_EVAL_ERR(v7, js_expr, expected_err, eval_fun)        \
  do {                                                                   \
    v7_val_t v;                                                          \
    enum v7_err e;                                                       \
    int r = 1;                                                           \
    num_tests++;                                                         \
    TRACE_EXPR(js_expr);                                                 \
    e = eval_fun(v7, js_expr, &v);                                       \
    if (e != (expected_err)) {                                           \
      printf("Exec '%s' returned err=%d, expected err=%d\n", js_expr, e, \
             expected_err);                                              \
      r = 0;                                                             \
    } else if (!(CHECK_BCODE_STACK_ZERO(v7))) {                          \
      PRINT_BCODE_STACK_ERROR(v7, js_expr);                              \
      r = 0;                                                             \
    }                                                                    \
    if (r == 0) {                                                        \
      FAIL("wrong eval err", __LINE__);                                  \
    }                                                                    \
  } while (0)

#define _ASSERT_EVAL_EQ(v7, js_expr, expected, check_fun) \
  _ASSERT_XXX_EVAL_EQ(v7, js_expr, expected, check_fun, v7_exec)

#define ASSERT_EVAL_EQ(v7, js_expr, expected) \
  _ASSERT_EVAL_EQ(v7, js_expr, expected, check_value)
#define ASSERT_EVAL_JS_EXPR_EQ(v7, js_expr, expected) \
  _ASSERT_EVAL_EQ(v7, js_expr, expected, check_js_expr)
#define ASSERT_EVAL_NUM_EQ(v7, js_expr, expected) \
  _ASSERT_EVAL_EQ(v7, js_expr, expected, check_num)
#define ASSERT_EVAL_STR_EQ(v7, js_expr, expected) \
  _ASSERT_EVAL_EQ(v7, js_expr, expected, check_str)

#define ASSERT_EVAL_ERR(v7, js_expr, expected_err) \
  _ASSERT_XXX_EVAL_ERR(v7, js_expr, expected_err, v7_exec)

static const char *test_is_true(void) {
  struct v7 *v7 = v7_create();

  ASSERT(test_if_expr(v7, "true", 1));
  ASSERT(test_if_expr(v7, "false", 0));
  ASSERT(test_if_expr(v7, "1", 1));
  ASSERT(test_if_expr(v7, "123.24876", 1));
  ASSERT(test_if_expr(v7, "0", 0));
  ASSERT(test_if_expr(v7, "-1", 1));
  ASSERT(test_if_expr(v7, "'true'", 1));
  ASSERT(test_if_expr(v7, "'false'", 1));
  ASSERT(test_if_expr(v7, "'hi'", 1));
  ASSERT(test_if_expr(v7, "'1'", 1));
  ASSERT(test_if_expr(v7, "'0'", 1));
  ASSERT(test_if_expr(v7, "'-1'", 1));
  ASSERT(test_if_expr(v7, "''", 0));
  ASSERT(test_if_expr(v7, "null", 0));
  ASSERT(test_if_expr(v7, "undefined", 0));
  ASSERT(test_if_expr(v7, "Infinity", 1));
  ASSERT(test_if_expr(v7, "-Infinity", 1));
  ASSERT(test_if_expr(v7, "[]", 1));
  ASSERT(test_if_expr(v7, "[[]]", 1));
  ASSERT(test_if_expr(v7, "[0]", 1));
  ASSERT(test_if_expr(v7, "[1]", 1));
  ASSERT(test_if_expr(v7, "NaN", 0));

  v7_destroy(v7);
  return NULL;
}

static const char *test_closure(void) {
  struct v7 *v7 = v7_create();

  ASSERT_EVAL_OK(v7, "function a(x){return function(y){return x*y}}");
  ASSERT_EVAL_OK(v7, "var f1 = a(5);");
  ASSERT_EVAL_OK(v7, "var f2 = a(7);");
  ASSERT_EVAL_EQ(v7, "f1(3);", "15");
  ASSERT_EVAL_EQ(v7, "f2(3);", "21");

  v7_destroy(v7);
  return NULL;
}

static enum v7_err adder(struct v7 *v7, v7_val_t *res) {
  enum v7_err rcode = V7_OK;
  double sum = 0;
  unsigned long i;

  for (i = 0; i < v7_argc(v7); i++) {
    sum += v7_to_number(v7_arg(v7, i));
  }
  *res = v7_mk_number(sum);

  return rcode;
}

static const char *test_native_functions(void) {
  struct v7 *v7 = v7_create();

  ASSERT_EQ(v7_set(v7, v7_get_global(v7), "adder", 5, v7_mk_cfunction(adder)),
            0);
  ASSERT_EVAL_EQ(v7, "adder(1, 2, 3 + 4);", "10");
  v7_destroy(v7);

  return NULL;
}

static const char *test_stdlib(void) {
  v7_val_t v = v7_mk_undefined();
  struct v7 *v7 = v7_create();
#if V7_ENABLE__RegExp
  const char *c;
#endif

  v7_own(v7, &v);

  ASSERT_EVAL_EQ(v7, "Boolean()", "false");
  ASSERT_EVAL_EQ(v7, "Boolean(0)", "false");
  ASSERT_EVAL_EQ(v7, "Boolean(1)", "true");
  ASSERT_EVAL_EQ(v7, "Boolean([])", "true");
  ASSERT_EVAL_EQ(v7, "new Boolean([])", "{}");

/* Math */
#if V7_BUILD_PROFILE > 1
  ASSERT_EVAL_EQ(v7, "Math.sqrt(144)", "12");

  /* Number */
  ASSERT_EVAL_NUM_EQ(v7, "Math.PI", M_PI);
#endif
  ASSERT_EVAL_NUM_EQ(v7, "Number.NaN", NAN);
  ASSERT_EQ(eval(v7, "1 == 2", &v), V7_OK);
  ASSERT(check_bool(v, 0));
  ASSERT_EQ(eval(v7, "1 + 2 * 7 === 15", &v), V7_OK);
  ASSERT(check_bool(v, 1));
  ASSERT_EQ(eval(v7, "Number(1.23) === 1.23", &v), V7_OK);
  ASSERT(check_bool(v, 1));
  ASSERT_EVAL_NUM_EQ(v7, "Number(1.23)", 1.23);
  ASSERT_EVAL_NUM_EQ(v7, "Number(0.123)", 0.123);
  ASSERT_EVAL_NUM_EQ(v7, "Number(1.23e+5)", 123000);
  ASSERT_EVAL_NUM_EQ(v7, "Number(1.23e5)", 123000);
  ASSERT_EVAL_NUM_EQ(v7, "Number(1.23e-5)", 1.23e-5);
/*
 * TODO(dfrank) : uncomment when we polish `strtod` from `esp_libc.c`
 * and put it to `common/str_util.c` as `cs_strtod`
 */
#if 0
  ASSERT_EVAL_NUM_EQ(v7, "Number(010)", 8);
  ASSERT_EVAL_NUM_EQ(v7, "Number(0777)", 511);
  ASSERT_EVAL_NUM_EQ(v7, "Number(0778)", 778);
  ASSERT_EVAL_NUM_EQ(v7, "Number(07781.1)", 7781.1);
#endif
  ASSERT_EVAL_OK(v7, "new Number(21.23)");

/* Cesanta-specific String API */
#ifdef CS_ENABLE_UTF8
  ASSERT_EVAL_NUM_EQ(v7, "'ы'.length", 1);
  ASSERT_EVAL_NUM_EQ(v7, "'ы'.charCodeAt(0)", 1099);
#endif
  ASSERT_EVAL_NUM_EQ(v7, "'ы'.blen", 2);
  ASSERT_EVAL_NUM_EQ(v7, "'ы'.at(0)", 0xd1);
  ASSERT_EVAL_NUM_EQ(v7, "'ы'.at(1)", 0x8b);
  ASSERT_EVAL_NUM_EQ(v7, "'ы'.at(2)", NAN);

  /* String */
  ASSERT_EVAL_NUM_EQ(v7, "'hello'.charCodeAt(1)", 'e');
  ASSERT_EVAL_NUM_EQ(v7, "'hello'.charCodeAt(4)", 'o');
  ASSERT_EVAL_NUM_EQ(v7, "'hello'.charCodeAt(5)", NAN);
  ASSERT_EVAL_NUM_EQ(v7, "'hello'.indexOf()", -1.0);
  ASSERT_EVAL_NUM_EQ(v7, "'HTTP/1.0\\r\\n'.indexOf('\\r\\n')", 8.0);
  ASSERT_EVAL_NUM_EQ(v7, "'hi there'.indexOf('e')", 5.0);
  ASSERT_EVAL_NUM_EQ(v7, "'hi there'.indexOf('e', 6)", 7.0);
  ASSERT_EVAL_NUM_EQ(v7, "'hi there'.indexOf('e', NaN)", 5.0);
  ASSERT_EVAL_NUM_EQ(v7, "'hi there'.indexOf('e', -Infinity)", 5.0);
  ASSERT_EVAL_NUM_EQ(v7, "'hi there'.indexOf('e', Infinity)", -1.0);
  ASSERT_EVAL_NUM_EQ(v7, "'hi there'.indexOf('e', 8)", -1.0);
  ASSERT_EVAL_NUM_EQ(v7, "'aabb'.indexOf('a', false)", 0.0);
  ASSERT_EVAL_NUM_EQ(v7, "'aabb'.indexOf('a', true)", 1.0);

#ifdef CS_ENABLE_UTF8
  ASSERT_EVAL_NUM_EQ(v7, "'1234д6 1234д6'.indexOf('34д')", 2.0);
  ASSERT_EVAL_NUM_EQ(v7, "'1234д6 1234д6'.indexOf('34д', 2)", 2.0);
  ASSERT_EVAL_NUM_EQ(v7, "'1234д6 1234д6'.indexOf('34д', 3)", 9.0);
  ASSERT_EVAL_NUM_EQ(v7, "'1234д6 1234д6'.indexOf('34д', 9)", 9.0);
  ASSERT_EVAL_NUM_EQ(v7, "'1234д6 1234д6'.indexOf('34д', 10)", -1.0);

  ASSERT_EVAL_NUM_EQ(v7, "'1234д6 1234д6'.lastIndexOf('34д')", 9.0);
  ASSERT_EVAL_NUM_EQ(v7, "'1234д6 1234д6'.lastIndexOf('34д', 10)", 9.0);
  ASSERT_EVAL_NUM_EQ(v7, "'1234д6 1234д6'.lastIndexOf('34д', 9)", 9.0);
  ASSERT_EVAL_NUM_EQ(v7, "'1234д6 1234д6'.lastIndexOf('34д', 8)", 2.0);
  ASSERT_EVAL_NUM_EQ(v7, "'1234д6 1234д6'.lastIndexOf('34д', 2)", 2.0);
  ASSERT_EVAL_NUM_EQ(v7, "'1234д6 1234д6'.lastIndexOf('34д', 1)", -1.0);
#endif

  ASSERT_EVAL_NUM_EQ(v7, "'123'.indexOf('')", 0.0);
  ASSERT_EVAL_NUM_EQ(v7, "'123'.indexOf('', 0)", 0.0);
  ASSERT_EVAL_NUM_EQ(v7, "'123'.indexOf('', -100)", 0.0);
  ASSERT_EVAL_NUM_EQ(v7, "'123'.indexOf('', 100)", 3.0);

  ASSERT_EVAL_NUM_EQ(v7, "'123'.lastIndexOf('')", 3.0);
  ASSERT_EVAL_NUM_EQ(v7, "'123'.lastIndexOf('', 0)", 0.0);
  ASSERT_EVAL_NUM_EQ(v7, "'123'.lastIndexOf('', -100)", 0.0);
  ASSERT_EVAL_NUM_EQ(v7, "'123'.lastIndexOf('', 100)", 3.0);

  ASSERT_EVAL_NUM_EQ(v7, "''.indexOf('')", 0.0);
  ASSERT_EVAL_NUM_EQ(v7, "''.indexOf('', 100)", 0.0);
  ASSERT_EVAL_NUM_EQ(v7, "''.indexOf('', -100)", 0.0);

  ASSERT_EVAL_NUM_EQ(v7, "''.lastIndexOf('')", 0.0);
  ASSERT_EVAL_NUM_EQ(v7, "''.lastIndexOf('', 100)", 0.0);
  ASSERT_EVAL_NUM_EQ(v7, "''.lastIndexOf('', -100)", 0.0);

  ASSERT_EVAL_NUM_EQ(v7, "''.indexOf('a')", -1.0);
  ASSERT_EVAL_NUM_EQ(v7, "''.lastIndexOf('a')", -1.0);

  ASSERT_EVAL_NUM_EQ(v7, "'123'.indexOf('23', 100)", -1.0);
  ASSERT_EVAL_NUM_EQ(v7, "'123'.indexOf('23', -100)", 1.0);
  ASSERT_EVAL_NUM_EQ(v7, "'123'.lastIndexOf('23', 100)", 1.0);
  ASSERT_EVAL_NUM_EQ(v7, "'123'.lastIndexOf('23', -100)", -1.0);

  ASSERT_EVAL_NUM_EQ(v7, "'123'.indexOf('12', 100)", -1.0);
  ASSERT_EVAL_NUM_EQ(v7, "'123'.indexOf('12', -100)", 0.0);
  ASSERT_EVAL_NUM_EQ(v7, "'123'.lastIndexOf('12', 100)", 0.0);
  ASSERT_EVAL_NUM_EQ(v7, "'123'.lastIndexOf('12', -100)", 0.0);

  ASSERT_EVAL_STR_EQ(v7, "'hi there'.substr(3, 2)", "th");
  ASSERT_EVAL_STR_EQ(v7, "'hi there'.substring(3, 5)", "th");
  ASSERT_EVAL_STR_EQ(v7, "'hi there'.substr(3)", "there");
  ASSERT_EVAL_STR_EQ(v7, "'hi there'.substr(-2)", "re");
  ASSERT_EVAL_STR_EQ(v7, "'hi there'.substr(NaN)", "hi there");
  ASSERT_EVAL_STR_EQ(v7, "'hi there'.substr(0, 300)", "hi there");
#if V7_ENABLE__RegExp
  ASSERT_EQ(eval(v7, "'dew dee'.match(/\\d+/)", &v), V7_OK);
  ASSERT_EQ(v, v7_mk_null());
  ASSERT_EVAL_OK(v7, "m = 'foo 1234 bar'.match(/\\S+ (\\d+)/)");
  ASSERT_EVAL_NUM_EQ(v7, "m.length", 2.0);
  ASSERT_EVAL_STR_EQ(v7, "m[0]", "foo 1234");
  ASSERT_EVAL_STR_EQ(v7, "m[1]", "1234");
  ASSERT_EQ(eval(v7, "m[2]", &v), V7_OK);
  ASSERT(v7_is_undefined(v));
  ASSERT_EVAL_OK(v7, "m = 'should match empty string at index 0'.match(/x*/)");
  ASSERT_EVAL_NUM_EQ(v7, "m.length", 1.0);
  ASSERT_EVAL_STR_EQ(v7, "m[0]", "");
  ASSERT_EVAL_NUM_EQ(v7, "m = 'aa bb cc'.split(RegExp('')); m.length", 8.0);
  ASSERT_EVAL_NUM_EQ(v7, "m = 'aa bb cc'.split(/x*/); m.length", 8.0);
  ASSERT_EVAL_NUM_EQ(v7, "m = 'aa bb cc'.split(/(x)*/); m.length", 15.0);
  ASSERT_EVAL_STR_EQ(v7, "m[0]", "a");
  ASSERT_EQ(eval(v7, "m[1]", &v), V7_OK);
  ASSERT(v7_is_undefined(v));
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'123'.split(RegExp('.'));", "['','','','']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'123'.split(RegExp(''));", "['1','2','3']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'123'.split(RegExp('1'));", "['','23']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'123'.split(RegExp('2'));", "['1','3']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'123'.split(RegExp('3'));", "['12','']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'123'.split(RegExp('1*'));", "['','2','3']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'123'.split(RegExp('3*'));", "['1','2','']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'123'.split(RegExp('2*'));", "['1','3']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'123'.split(RegExp('4*'));", "['1','2','3']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'123'.split(RegExp('2*'), 1);", "['1']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'123'.split(RegExp('2*'), 2);", "['1','3']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'123'.split(RegExp('2*'), 3);", "['1','3']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'123'.split(RegExp('2*'), 4);", "['1','3']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'123'.split(RegExp('4*'), 1);", "['1']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'123'.split(RegExp('4*'), 2);", "['1','2']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'123'.split(RegExp('4*'), 3);", "['1','2','3']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'123'.split(RegExp('4*'), 4);", "['1','2','3']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'123'.split(/.*/);", "['', '']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'1234'.split(/.*/);", "['', '']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'12345'.split(/.*/);", "['', '']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'123456'.split(/.*/);", "['', '']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "''.split(RegExp(''));", "[]");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "''.split(RegExp('.'));", "['']");
  ASSERT_EVAL_JS_EXPR_EQ(
      v7, "'1234'.split(/(x)*/);",
      "['1', undefined, '2', undefined, '3', undefined, '4']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'1234'.split(/(2)*/);",
                         "['1', '2', '3', undefined, '4']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'1234'.split(/(\\d)/);",
                         "['', '1', '', '2', '', '3', '', '4', '']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'1234'.split(/(\\d)*/);", "['', '4', '']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'ab12 cd34'.split(/([a-z]*)(\\d*)/);",
                         "['', 'ab', '12', ' ', 'cd', '34', '']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'1234'.split(/(x)*/, 0);", "[]");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'1234'.split(/(x)*/, 1);", "['1']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'1234'.split(/(x)*/, 2);", "['1', undefined]");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'1234'.split(/(x)*/, 3);",
                         "['1', undefined, '2']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'1234'.split(/(x)*/, 4);",
                         "['1', undefined, '2', undefined]");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'1234'.split(/(x)*/, 5);",
                         "['1', undefined, '2', undefined, '3']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'1234'.split(/(x)*/, 6);",
                         "['1', undefined, '2', undefined, '3', undefined]");
  ASSERT_EVAL_JS_EXPR_EQ(
      v7, "'1234'.split(/(x)*/, 7);",
      "['1', undefined, '2', undefined, '3', undefined, '4']");
  ASSERT_EVAL_JS_EXPR_EQ(
      v7, "'1234'.split(/(x)*/, 8);",
      "['1', undefined, '2', undefined, '3', undefined, '4']");
  ASSERT_EVAL_NUM_EQ(v7, "m = 'aa bb cc'.split(/ /, 2); m.length", 2.0);
  ASSERT_EVAL_NUM_EQ(
      v7, "({z: '123456'}).z.toString().substr(0, 3).split('').length", 3.0);
  c = "\"a\\nb\".replace(/\\n/g, \"\\\\\");";
  ASSERT_EVAL_STR_EQ(v7, c, "a\\b");
  c = "\"\"";
  ASSERT_EVAL_EQ(v7, "'abc'.replace(/.+/, '')", c);
#endif /* V7_ENABLE__RegExp */

  ASSERT_EVAL_NUM_EQ(v7, "m = 'aa bb cc'.split(); m.length", 1.0);
  ASSERT_EVAL_NUM_EQ(v7, "m = 'aa bb cc'.split(''); m.length", 8.0);
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'123'.split('1');", "['','23']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'123'.split('2');", "['1','3']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'123'.split('3');", "['12','']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'123'.split('12');", "['','3']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'123'.split('23');", "['1','']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'123'.split('123');", "['','']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'123'.split('1234');", "['123']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'123'.split('');", "['1','2','3']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'111'.split('1');", "['','','','']");
#ifdef CS_ENABLE_UTF8
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'абв'.split('б');", "['а','в']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'абв'.split('');", "['а','б','в']");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'rбв'.split('');", "['r','б','в']");
  ASSERT_EVAL_NUM_EQ(v7, "(String.fromCharCode(0,1) + '\\x00\\x01').length", 4);
  ASSERT_EVAL_NUM_EQ(
      v7, "(String.fromCharCode(1,0) + '\\x00\\x01').charCodeAt(1)", 0);
  ASSERT_EVAL_NUM_EQ(
      v7, "(String.fromCharCode(0,1) + '\\x00\\x01').charCodeAt(1)", 1);
#endif
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'12.34.56'.split('.');", "['12','34','56']");
  ASSERT_EVAL_NUM_EQ(v7, "m = 'aa bb cc'.split(' '); m.length", 3.0);
  ASSERT_EVAL_NUM_EQ(v7, "m = 'aa bb cc'.split(' ', 2); m.length", 2.0);
  ASSERT_EVAL_NUM_EQ(v7, "'aa bb cc'.substr(0, 4).split(' ').length", 2.0);
  ASSERT_EVAL_STR_EQ(v7, "'aa bb cc'.substr(0, 4).split(' ')[1]", "b");

  ASSERT_EVAL_STR_EQ(v7, "String('hi')", "hi");
  ASSERT_EVAL_OK(v7, "new String('blah')");

/* Date() tests interact with external object (local date & time), so
    if host have strange date/time setting it won't be work */

#ifdef V7_ENABLE__Date
  ASSERT_EVAL_EQ(v7, "Number(new Date('IncDec 01 2015 00:00:00'))", "NaN");
  ASSERT_EVAL_EQ(v7, "Number(new Date('My Jul 01 2015 00:00:00'))", "NaN");
#endif

  ASSERT_EVAL_NUM_EQ(v7, "(function() {var x = 42; return eval('x')})()", 42);

  v7_destroy(v7);
  return NULL;
}

static const char *test_tokenizer(void) {
  static const char *str =
      "1.23e-15 'fo\\'\\'o\\x25\n\\'' /\\s+/ $_12foo{}(),[].:;== === != !== "
      "= %= *= /= ^= += -= |= &= <<= >>= >>>= & || + - ++ -- "
      "&&|?~%*/^ <= < >= > << >> >>> !";
  enum v7_tok tok = TOK_END_OF_INPUT;
  double num;
  const char *p = str;
  unsigned int i = 1;

  skip_to_next_tok(&p);

  /* Make sure divisions are parsed correctly - set previous token */
  while ((tok = get_tok(&p, &num, i > TOK_REGEX_LITERAL ? TOK_NUMBER : tok)) !=
         TOK_END_OF_INPUT) {
    skip_to_next_tok(&p);
    ASSERT_EQ(tok, i);
    i++;
  }
  ASSERT_EQ(i, TOK_BREAK);

  p = "/foo/";
  ASSERT_EQ(get_tok(&p, &num, TOK_NUMBER), TOK_DIV);

  p = "/foo/";
  ASSERT_EQ(get_tok(&p, &num, TOK_COMMA), TOK_REGEX_LITERAL);

  p = "/foo";
  ASSERT_EQ(get_tok(&p, &num, TOK_COMMA), TOK_DIV);

  p = "/fo\\/o";
  ASSERT_EQ(get_tok(&p, &num, TOK_COMMA), TOK_DIV);

  return NULL;
}

static const char *test_runtime(void) {
  struct v7 *v7 = v7_create();
  val_t v;
  struct v7_property *p;
  size_t n;
  const char *s;
  int i;
  char test_str[] =
      "Lorem ipsum dolor sit amet, consectetur adipiscing elit, "
      "sed do eiusmod tempor incididunt ut labore et dolore magna "
      "aliqua. Ut enim ad minim veniam, quis nostrud exercitation "
      "ullamco laboris nisi ut aliquip ex ea commodo consequat. "
      "Duis aute irure dolor in reprehenderit in voluptate velit "
      "esse cillum dolore eu fugiat nulla pariatur. Excepteur "
      "sint occaecat cupidatat non proident, sunt in culpa qui "
      "officia deserunt mollit anim id est laborum.";

  v = v7_mk_null();
  ASSERT(v7_is_null(v));

  v7_own(v7, &v);

  v = v7_mk_undefined();
  ASSERT(v7_is_undefined(v));

  v = v7_mk_number(1.0);
  ASSERT_EQ(val_type(v7, v), V7_TYPE_NUMBER);
  ASSERT_EQ(v7_to_number(v), 1.0);
  ASSERT(check_value(v7, v, "1"));

  v = v7_mk_number(1.5);
  ASSERT_EQ(v7_to_number(v), 1.5);
  ASSERT(check_value(v7, v, "1.5"));

  v = v7_mk_boolean(1);
  ASSERT_EQ(val_type(v7, v), V7_TYPE_BOOLEAN);
  ASSERT_EQ(v7_to_boolean(v), 1);
  ASSERT(check_value(v7, v, "true"));

  v = v7_mk_boolean(0);
  ASSERT(check_value(v7, v, "false"));

  v = v7_mk_string(v7, "foo", 3, 1);
  ASSERT_EQ(val_type(v7, v), V7_TYPE_STRING);
  v7_get_string_data(v7, &v, &n);
  ASSERT_EQ(n, 3);
  s = "\"foo\"";
  ASSERT(check_value(v7, v, s));

  v = v7_mk_string(v7, "foo", 3, 1);
  s = v7_to_cstring(v7, &v);
  ASSERT(strcmp("foo", s) == 0);

  /* short strings are embedded even if unowned */
  v = v7_mk_string(v7, "foobarbaz", 8, 0);
  s = v7_to_cstring(v7, &v);
  /* null because at index 8 there is 'z' instead of a null */
  ASSERT(s == NULL);

  v = v7_mk_string(v7, "foo\0bar", 7, 1);
  s = v7_to_cstring(v7, &v);
  ASSERT(s == NULL);

  for (i = 1; i < (int) sizeof(test_str); i++) {
    v = v7_mk_string(v7, 0, i, 1);
    s = v7_get_string_data(v7, &v, &n);
    memcpy((char *) s, test_str, i);
    ASSERT_EQ(val_type(v7, v), V7_TYPE_STRING);
    s = v7_get_string_data(v7, &v, &n);
    ASSERT(n == (size_t) i);
    ASSERT(memcmp(s, test_str, n) == 0);
  }

  v = v7_mk_object(v7);
  ASSERT_EQ(val_type(v7, v), V7_TYPE_GENERIC_OBJECT);
  ASSERT(v7_to_generic_object(v) != NULL);
  ASSERT(v7_to_generic_object(v)->prototype != NULL);
  ASSERT(v7_to_generic_object(
             v7_object_to_value(v7_to_generic_object(v)->prototype))
             ->prototype == NULL);

  ASSERT_EQ(v7_set(v7, v, "foo", -1, v7_mk_null()), 0);
  ASSERT((p = v7_get_property(v7, v, "foo", -1)) != NULL);
  ASSERT_EQ(p->attributes, 0);
  ASSERT(v7_is_null(p->value));
  ASSERT(check_value(v7, p->value, "null"));

  ASSERT_EQ(v7_set(v7, v, "foo", -1, v7_mk_undefined()), 0);
  ASSERT((p = v7_get_property(v7, v, "foo", -1)) != NULL);
  ASSERT(check_value(v7, p->value, "undefined"));

  ASSERT(v7_set(v7, v, "foo", -1, v7_mk_string(v7, "bar", 3, 1)) == 0);
  ASSERT((p = v7_get_property(v7, v, "foo", -1)) != NULL);
  s = "\"bar\"";
  ASSERT(check_value(v7, p->value, s));

  ASSERT(v7_set(v7, v, "foo", -1, v7_mk_string(v7, "zar", 3, 1)) == 0);
  ASSERT((p = v7_get_property(v7, v, "foo", -1)) != NULL);
  s = "\"zar\"";
  ASSERT(check_value(v7, p->value, s));

  ASSERT_EQ(v7_del(v7, v, "foo", ~0), 0);
  ASSERT(v7_to_object(v)->properties == NULL);
  ASSERT_EQ(v7_del(v7, v, "foo", -1), -1);
  ASSERT(v7_set(v7, v, "foo", -1, v7_mk_string(v7, "bar", 3, 1)) == 0);
  ASSERT(v7_set(v7, v, "bar", -1, v7_mk_string(v7, "foo", 3, 1)) == 0);
  ASSERT(v7_set(v7, v, "aba", -1, v7_mk_string(v7, "bab", 3, 1)) == 0);
  ASSERT_EQ(v7_del(v7, v, "foo", -1), 0);
  ASSERT((p = v7_get_property(v7, v, "foo", -1)) == NULL);
  ASSERT_EQ(v7_del(v7, v, "aba", -1), 0);
  ASSERT((p = v7_get_property(v7, v, "aba", -1)) == NULL);
  ASSERT_EQ(v7_del(v7, v, "bar", -1), 0);
  ASSERT((p = v7_get_property(v7, v, "bar", -1)) == NULL);

  v = v7_mk_object(v7);
  ASSERT_EQ(v7_set(v7, v, "foo", -1, v7_mk_number(1.0)), 0);
  ASSERT((p = v7_get_property(v7, v, "foo", -1)) != NULL);
  ASSERT((p = v7_get_property(v7, v, "f", -1)) == NULL);

  v = v7_mk_object(v7);
  ASSERT_EQ(v7_set(v7, v, "foo", -1, v), 0);
  s = "{\"foo\":[Circular]}";
  ASSERT(check_value(v7, v, s));

  v = v7_mk_object(v7);
  ASSERT(v7_def(v7, v, "foo", -1, V7_DESC_CONFIGURABLE(0), v7_mk_number(1.0)) ==
         0);
  s = "{\"foo\":1}";
  ASSERT(check_value(v7, v, s));
  ASSERT(v7_def(v7, v, "foo", -1, V7_DESC_CONFIGURABLE(0), v7_mk_number(2.0)) ==
         0);
  s = "{\"foo\":2}";
  ASSERT(check_value(v7, v, s));
  ASSERT_EQ(v7_to_number(v7_get(v7, v, "foo", -1)), 2.0);
  ASSERT(v7_get_property(v7, v, "foo", -1)->attributes &
         V7_PROPERTY_NON_CONFIGURABLE);

#if 0
  ASSERT_EQ(
      v7_def(v7, v, "foo", -1, (V7_PROPERTY_NON_WRITABLE | V7_OVERRIDE_ATTRIBUTES),
             v7_mk_number(1.0)),
      0);
  ASSERT(v7_def(v7, v, "foo", -1, 0, v7_mk_number(2.0)) != 0);
  s = "{\"foo\":1}";
  ASSERT(check_value(v7, v, s));
#endif

  v = v7_mk_string(v7, "fooakbar", 8, 1);
  for (i = 0; i < 100; i++) {
    s = v7_get_string_data(v7, &v, &n);
    v7_mk_string(v7, s, 8, 1);
  }

  v = v7_mk_array(v7);
  ASSERT(v7_is_instanceof_v(v7, v,
                            v7_get(v7, v7->vals.global_object, "Array", ~0)));
  ASSERT(v7_is_instanceof(v7, v, "Array"));

  v = v7_mk_number(42);
  ASSERT(!v7_is_instanceof(v7, v, "Object"));
  ASSERT(!v7_is_instanceof(v7, v, "Number"));

  v = v7_mk_string(v7, "fooakbar", 8, 1);
  ASSERT(!v7_is_instanceof(v7, v, "Object"));
  ASSERT(!v7_is_instanceof(v7, v, "String"));

  v7_destroy(v7);
  return NULL;
}

static const char *test_apply(void) {
  struct v7 *v7 = v7_create();
  val_t v = v7_mk_undefined(), fn = v7_mk_undefined(), args = v7_mk_undefined();
  v7_own(v7, &v);
  v7_own(v7, &fn);
  v7_own(v7, &args);

  fn = v7_get(v7, v7->vals.global_object, "test0", 5); /* no such function */
  ASSERT_EQ(v7_apply(v7, fn, v7->vals.global_object, v7_mk_undefined(), &v),
            V7_EXEC_EXCEPTION);

  ASSERT_EQ(eval(v7, "function test1(){return 1}", &v), V7_OK);
  fn = v7_get(v7, v7->vals.global_object, "test1", 5);
  ASSERT_EQ(v7_apply(v7, fn, v7->vals.global_object, v7_mk_undefined(), &v),
            V7_OK);
  ASSERT(check_num(v7, v, 1));

  ASSERT_EQ(v7_apply(v7, fn, v7->vals.global_object, v7_mk_undefined(), NULL),
            V7_OK);

  ASSERT_EQ(eval(v7, "function test2(){throw 2}", &v), V7_OK);
  fn = v7_get(v7, v7->vals.global_object, "test2", 5);
  ASSERT_EQ(v7_apply(v7, fn, v7->vals.global_object, v7_mk_undefined(), &v),
            V7_EXEC_EXCEPTION);
  ASSERT(check_num(v7, v, 2));

  ASSERT_EQ(eval(v7, "function test1(){return arguments}", &v), V7_OK);
  fn = v7_get(v7, v7->vals.global_object, "test1", 5);
  args = v7_mk_array(v7);
  v7_array_push(v7, args, v7_mk_number(1));
  v7_array_push(v7, args, v7_mk_number(2));
  v7_array_push(v7, args, v7_mk_number(3));
  ASSERT_EQ(v7_apply(v7, fn, v7->vals.global_object, args, &v), V7_OK);
  ASSERT(v7_array_length(v7, v) == 3);

  v7_destroy(v7);
  return NULL;
}

#ifdef V7_ENABLE_DENSE_ARRAYS
static const char *test_dense_arrays(void) {
  struct v7 *v7 = v7_create();
  val_t a;

  a = v7_mk_dense_array(v7);

  v7_array_set(v7, a, 0, v7_mk_number(42));
  ASSERT(check_num(v7, v7_array_get(v7, a, 0), 42));
  ASSERT_EQ(v7_array_length(v7, a), 1);

  v7_array_set(v7, a, 1, v7_mk_number(24));
  ASSERT(check_num(v7, v7_array_get(v7, a, 0), 42));
  ASSERT(check_num(v7, v7_array_get(v7, a, 1), 24));
  ASSERT_EQ(v7_array_length(v7, a), 2);

  a = v7_mk_dense_array(v7);
  v7_array_set(v7, a, 0, v7_mk_number(42));
  v7_array_set(v7, a, 2, v7_mk_number(42));
  ASSERT_EQ(v7_array_length(v7, a), 3);

  a = v7_mk_dense_array(v7);
  v7_array_set(v7, a, 1, v7_mk_number(42));
  ASSERT_EQ(v7_array_length(v7, a), 2);

  ASSERT_EVAL_OK(v7, "function mka(){return arguments}");

  ASSERT_EVAL_EQ(v7, "a=mka(1,2,3);a.splice(0,1);a", "[2,3]");
  ASSERT_EVAL_EQ(v7, "a=mka(1,2,3);a.splice(2,1);a", "[1,2]");
  ASSERT_EVAL_EQ(v7, "a=mka(1,2,3);a.splice(1,1);a", "[1,3]");

  ASSERT_EVAL_EQ(v7, "a=mka(1,2,3);a.slice(0,1)", "[1]");
  ASSERT_EVAL_EQ(v7, "a=mka(1,2,3);a.slice(2,3)", "[3]");
  ASSERT_EVAL_EQ(v7, "a=mka(1,2,3);a.slice(1,3)", "[2,3]");

  ASSERT_EVAL_NUM_EQ(v7, "a=mka(1,2,3);a.indexOf(3)", 2);

  ASSERT_EVAL_NUM_EQ(v7, "(function(){return arguments})(1,2,3).length", 3);
  ASSERT_EVAL_NUM_EQ(
      v7, "(function(){return arguments}).apply(this, [1,2,3]).length", 3);

  /* ensure that a zero length dense arrays is correctly recognized */
  a = v7_mk_dense_array(v7);
  ASSERT(v7_next_prop(v7, a, NULL) == NULL);

  v7_destroy(v7);
  return NULL;
}
#endif

static const char *test_parser(void) {
  int i;
  struct ast a;
  struct v7 *v7 = v7_create();
  const char *cases[] = {
    "1",
    "true",
    "false",
    "null",
    "undefined",
    "1+2",
    "1-2",
    "1*2",
    "1/2",
    "1%2",
    "1/-2",
    "(1 + 2) * x + 3",
    "1 + (2, 3) * 4, 5",
    "(a=1 + 2) + 3",
    "1 ? 2 : 3",
    "1 ? 2 : 3 ? 4 : 5",
    "1 ? 2 : (3 ? 4 : 5)",
    "1 || 2 + 2",
    "1 && 2 || 3 && 4 + 5",
    "1|2 && 3|3",
    "1^2|3^4",
    "1&2^2&4|5&6",
    "1==2&3",
    "1<2",
    "1<=2",
    "1>=2",
    "1>2",
    "1==1<2",
    "a instanceof b",
    "1 in b",
    "1!=2&3",
    "1!==2&3",
    "1===2",
    "1<<2",
    "1>>2",
    "1>>>2",
    "1<<2<3",
    "1/2/2",
    "(1/2)/2",
    "1 + + 1",
    "1- -2",
    "!1",
    "~0",
    "void x()",
    "delete x",
    "typeof x",
    "++x",
    "--i",
    "x++",
    "i--",
    "a.b",
    "a.b.c",
    "a[0]",
    "a[0].b",
    "a[0][1]",
    "a[b[0].c]",
    "a()",
    "a(0)",
    "a(0, 1)",
    "a(0, (1, 2))",
    "1 + a(0, (1, 2)) + 2",
    "new x",
    "new x(0, 1)",
    "new x.y(0, 1)",
    "new x.y",
    "1;",
    "1;2;",
    "1;2",
    "1\nx",
    "p()\np()\np();p()",
    ";1",
    "if (1) 2",
    "if (1) 2; else 3",
    "if (1) {2;3}",
    "if (1) {2;3}; 4",
    "if (1) {2;3} else {4;5}",
    "while (1);",
    "while(1) {}",
    "while (1) 2;",
    "while (1) {2;3}",
    "for (i = 0; i < 3; i++) i++",
    "for (i=0; i<3;) i++",
    "for (i=0;;) i++",
    "for (;i<3;) i++",
    "for (;;) i++",
    "debugger",
    "while(1) break",
    "while(1) break loop",
    "while(1) continue",
    "while(1) continue loop",
    "function f() {return}",
    "function f() {return 1+2}",
    "function f() {if (1) {return;}}",
    "function f() {if (1) {return 2}}",
    "throw 1+2",
    "try { 1 } catch (e) { 2 }",
    "try {1} finally {3}",
    "try {1} catch (e) {2} finally {3}",
    "var x",
    "var x, y",
    "var x=1, y",
    "var y, x=y=1",
    "function x(a) {return a}",
    "function x() {return 1}",
    "[1,2,3]",
    "[1+2,3+4,5+6]",
    "[1,[2,[[3]]]]",
    "({a: 1})",
    "({a: 1, b: 2})",
    "({})",
    "(function(a) { return a + 1 })",
    "(function f(a) { return a + 1 })",
    "(function f() { return; 1;})",
    "function f() {while (1) {return;2}}",
    "switch(a) {case 1: break;}",
    "switch(a) {case 1: p(); break;}",
    "switch(a) {case 1: a; case 2: b; c;}",
    "switch(a) {case 1: a; b; default: c;}",
    "switch(a) {case 1: p(); break; break; }",
    "switch(a) {case 1: break; case 2: 1; case 3:}",
    "switch(a) {case 1: break; case 2: 1; case 3: default: break}",
    "switch(a) {case 1: break; case 3: default: break; case 2: 1}",
    "for (var i = 0; i < 3; i++) i++",
    "for (var i=0, j=i; i < 3; i++, j++) i++",
    "a%=1",
    "a*=1",
    "a/=1",
    "a+=1",
    "a-=1",
    "a|=1",
    "a&=1",
    "a<<=1",
    "a>>2",
    "a>>=1",
    "a>>>=1",
    "a=b=c+=1",
    "a=(b=(c+=1))",
    "\"foo\"",
    "var undefined = 1",
    "undefined",
    "u",
    "{var x=1;2;}",
#ifdef V7_ENABLE_JS_GETTERS
    "({get a() { return 1 }})",
    "({get a() { return 1 }, set b(c) { this.x = c }, d: 0})",
#endif
    "({get: function() {return 42;}})",
#ifdef V7_ENABLE_JS_SETTERS
    "({set a(b) { this.x = b }})",
#endif
    "Object.defineProperty(o, \"foo\", {get: function() {return 42;}});",
    "({a: 0, \"b\": 1})",
    "({a: 0, 42: 1})",
    "({a: 0, 42.99: 1})",
    "({a: 0, })",
    "({true: 0, null: 1, undefined: 2, this: 3})",
    "[]",
    "[,2]",
    "[,]",
    "[,2]",
    "[,,,1,2]",
    "delete z",
    "delete (1+2)",
    "delete (delete z)",
    "delete delete z",
    "+ + + 2",
    "throw 'ex'",
    "switch(a) {case 1: try { 1; } catch (e) { 2; } finally {}; break}",
    "switch(a) {case 1: try { 1; } catch (e) { 2; } finally {break}; break}",
    "switch(a) {case 1: try { 1; } catch (e) { 2; } finally {break}; break; "
    "default: 1; break;}",
    "try {1} catch(e){}\n1",
    "try {1} catch(e){} 1",
    "switch(v) {case 0: break;} 1",
    "switch(a) {case 1: break; case 3: default: break; case 2: 1; default: "
    "2}",
    "do { i-- } while (i > 0)",
    "if (false) 1; 1;",
    "while(true) 1; 1;",
    "while(true) {} 1;",
    "do {} while(false) 1;",
    "with (a) 1; 2;",
    "with (a) {1} 2;",
    "for(var i in a) {1}",
    "for(i in a) {1}",
    "!function(){function d(){}var x}();",
#ifdef V7_ENABLE_JS_GETTERS
    "({get a() { function d(){} return 1 }})",
#endif
#ifdef V7_ENABLE_JS_SETTERS
    "({set a(v) { function d(a){} d(v) }})",
#endif
    "({a:1, b() { return 2 }, c(d) {}})",
    "{function d(){}var x}",
    "try{function d(){}var x}catch(e){function d(){}var x}finally{function "
    "d(){}var x}",
    "{} {}",
    "if(1){function d(){}var x}",
    "if(1){} else {function d(){}var x}",
#if CS_ENABLE_UTF8
    "var \\u0076, _\\u0077, a\\u0078b, жабоскрипт;",
#else
    "",
#endif
    "a.in + b.for",
    "var x = { null: 5, else: 4 }",
    "lab: x=1",
    "'use strict';0;'use strict';",
    "'use strict';if(0){'use strict';}",
    "(function(){'use strict';0;'use strict';})()"
  };
  const char *invalid[] = {
      "function(a) { return 1 }", "i\n++", "{a: 1, b: 2}", "({, a: 0})",
      "break", "break loop", "continue", "continue loop", "return",
      "return 1+2", "if (1) {return;}", "if (1) {return 2}", "({g x() {}})'",
      "({s x() {}})'", "(function(){'use strict'; with({}){}})", "v = [",
      "var v = [", "\n1a"};
  FILE *fp;
  const char *want_ast_db = "want_ast.db";
  char got_ast[102400];
  char want_ast[102400];
  char *next_want_ast = want_ast;
  size_t want_ast_len;
  enum v7_err rcode = V7_OK;
  ast_init(&a, 0);

/* Save with `make save_want_ast` */
#ifndef SAVE_AST

  ASSERT((fp = fopen(want_ast_db, "r")) != NULL);
  memset(want_ast, 0, sizeof(want_ast));
  if (fread(want_ast, sizeof(want_ast), 1, fp) < sizeof(want_ast)) {
    ASSERT_EQ(ferror(fp), 0);
  }
  ASSERT(feof(fp));
  fclose(fp);

  for (i = 0; i < (int) ARRAY_SIZE(cases); i++) {
    char *current_want_ast = next_want_ast;
    ast_free(&a);
    ASSERT((next_want_ast = strchr(current_want_ast, '\0') + 1) != NULL);
    if (cases[i][0] == '\0') continue;
    want_ast_len = (size_t)(next_want_ast - current_want_ast - 1);
    ASSERT((fp = fopen("/tmp/got_ast", "w")) != NULL);
#if 0
      printf("-- Parsing \"%s\"\n", cases[i]);
#endif
    ASSERT_EQ(parse_js(v7, cases[i], &a), V7_OK);

    if (want_ast_len == 0) {
      printf("Test case not found in %s:\n", want_ast_db);
      rcode = v7_compile(cases[i], 0, 0, stdout);
      (void) rcode;
      abort();
    }
    rcode = v7_compile(cases[i], 0, 0, fp);
    (void) rcode;
    fclose(fp);

    ASSERT((fp = fopen("/tmp/got_ast", "r")) != NULL);
    memset(got_ast, 0, sizeof(got_ast));
    if (fread(got_ast, sizeof(got_ast), 1, fp) < sizeof(got_ast)) {
      ASSERT_EQ(ferror(fp), 0);
    }
    ASSERT(feof(fp));
    fclose(fp);
#if !defined(_WIN32)
    if (strncmp(got_ast, current_want_ast, sizeof(got_ast)) != 0) {
      fp = fopen("/tmp/want_ast", "w");
      fwrite(current_want_ast, want_ast_len, 1, fp);
      fclose(fp);
      ASSERT(system("diff -u /tmp/want_ast /tmp/got_ast") != -1);
    }
    ASSERT_EQ(strncmp(got_ast, current_want_ast, sizeof(got_ast)), 0);
#endif
  }

#else /* SAVE_AST */

  (void) got_ast;
  (void) next_want_ast;
  (void) want_ast_len;
  ASSERT((fp = fopen(want_ast_db, "w")) != NULL);
  for (i = 0; i < (int) ARRAY_SIZE(cases); i++) {
    ast_free(&a);
    ASSERT_EQ(parse_js(v7, cases[i], &a), V7_OK);
    v7_compile(cases[i], 0, fp);
    fwrite("\0", 1, 1, fp);
  }
  fclose(fp);

#endif /* SAVE_AST */

  for (i = 0; i < (int) ARRAY_SIZE(invalid); i++) {
    ast_free(&a);
#if 0
    printf("-- Parsing \"%s\"\n", invalid[i]);
#endif
    ASSERT_EQ(parse(v7, &a, invalid[i], 0, 0), V7_SYNTAX_ERROR);
  }

  ast_free(&a);
  v7_destroy(v7);
  return NULL;
}

static char *read_file(const char *path, size_t *size) {
  FILE *fp;
  struct stat st;
  char *data = NULL;
  if ((fp = fopen(path, "rb")) != NULL && !fstat(fileno(fp), &st)) {
    *size = st.st_size;
    data = (char *) malloc(*size + 1);
    if (data != NULL) {
      if (fread(data, 1, *size, fp) < *size) {
        if (ferror(fp) == 0) return NULL;
      }
      data[*size] = '\0';
    }
    fclose(fp);
  }
  return data;
}

static const char *test_parser_large_ast(void) {
  struct ast a;
  struct v7 *v7 = v7_create();
  size_t script_len;
  char *script = read_file("large_ast.js", &script_len);

  ast_init(&a, 0);
  ASSERT_EQ(parse(v7, &a, script, 0, 0), V7_AST_TOO_LARGE);
  return NULL;
}

static const char *test_ecmac(void) {
  struct ast a;
  int i, passed = 0;
  size_t db_len, driver_len;
  char *db = read_file("ecmac.db", &db_len);
  char *driver = read_file("ecma_driver.js", &driver_len);
  char *next_case = db - 1;
  FILE *r;
  struct v7 *v7;
  val_t res;

#ifdef _WIN32
  fprintf(stderr, "Skipping ecma tests on windows\n");
  return NULL;
#endif

  ASSERT((r = fopen(".ecma_report.txt", "wb")) != NULL);

  ast_init(&a, 0);

  for (i = 0; next_case < db + db_len; i++) {
    char tail_cmd[100];
    char *current_case = next_case + 1;
    char *chap_begin = NULL, *chap_end = NULL;
    int chap_len = 0;
    clock_t start_time = clock(), execution_time;
    ASSERT((next_case = strchr(current_case, '\0')) != NULL);
    if ((chap_begin = strstr(current_case, " * @path ")) != NULL) {
      chap_begin += 9;
      if ((chap_end = strchr(chap_begin, '\r')) != NULL ||
          (chap_end = strchr(chap_begin, '\n')) != NULL) {
        chap_len = chap_end - chap_begin;
      }
    }
    snprintf(tail_cmd, sizeof(tail_cmd),
             "%.*s (tail -c +%lu tests/ecmac.db|head -c %lu)", chap_len,
             chap_begin == NULL ? "" : chap_begin,
             (unsigned long) (current_case - db + 1),
             (unsigned long) (next_case - current_case));

#if 0
    if (i != 1070) continue;
#endif

    if (i == 1231 || i == 1250 || i == 1252 || i == 1253 || i == 1251 ||
        i == 1255 || i == 2649 || i == 2068 || i == 7445 || i == 7446 ||
        i == 3400 || i == 3348 || i == 3349 || i == 3401 || i == 89 ||
        i == 462 ||

        /*
         * TODO(lsm): re-enable these slow tests
         * This list is created by running the unit test with execution time
         * tracing (see printf at the end of the loop block) and piping through
         * this filter:
         * grep '^--' | sort -nk2 | tail -60 | cut -d' ' -f3 | sort | uniq | \
         *    while read i ; do echo "i == $i ||"; done | xargs
         */
        i == 3247 || i == 3287 || i == 3423 || i == 3424 || i == 3425 ||
        i == 3426 || i == 3427 || i == 3451 || i == 3452 || i == 3453 ||
        i == 3454 || i == 3455 || i == 8101 || i == 8315 || i == 8710 ||
        i == 8929) {
      fprintf(r, "%i\tSKIP %s\n", i, tail_cmd);
      continue;
    }

#if !defined(V7_ENABLE_JS_GETTERS) || !defined(V7_ENABLE_JS_SETTERS)
    if ((i >= 189 && i <= 204) || (i >= 253 && i <= 268) ||
        (i >= 568 && i <= 573) || (i >= 1066 && i <= 1083) || i == 1855) {
      fprintf(r, "%i\tSKIP %s\n", i, tail_cmd);
      continue;
    }
#endif

    v7 = v7_create();

#if V7_VERBOSE_ECMA
    printf("-- Parsing %d: \"%s\"\n", i, current_case);
#endif
    ASSERT_EQ(parse_js(v7, current_case, &a), V7_OK);
    ast_free(&a);

    if (eval(v7, driver, &res) != V7_OK) {
      fprintf(stderr, "%s: %s\n", "Cannot load ECMA driver", v7->error_msg);
    } else {
      if (eval(v7, current_case, &res) != V7_OK) {
        char buf[2048], *err_str = v7_to_json(v7, res, buf, sizeof(buf));
        fprintf(r, "%i\tFAIL %s: [%s]\n", i, tail_cmd, err_str);
        if (err_str != buf) {
          free(err_str);
        }
      } else {
        passed++;
        fprintf(r, "%i\tPASS %s\n", i, tail_cmd);
      }
    }
    v7_destroy(v7);
    execution_time = clock() - start_time;
    (void) execution_time;
#if 0
    printf("--> %g %d [%s]\n",
           execution_time / (double) CLOCKS_PER_SEC, i, tail_cmd);
#endif
  }
  printf("ECMA tests coverage: %.2f%% (%d of %d)\n",
         (double) passed / i * 100.0, passed, i);

  free(db);
  free(driver);
  fclose(r);
  rename(".ecma_report.txt", "ecma_report.txt");
  return NULL;
}

static const char *test_string_encoding(void) {
  unsigned char buf[10] = ":-)";
  int llen;

  ASSERT_EQ(encode_varint(3, buf), 1);
  ASSERT_EQ(decode_varint(buf, &llen), 3);
  ASSERT_EQ(buf[0], 3);
  ASSERT_EQ(llen, 1);

  ASSERT_EQ(encode_varint(127, buf), 1);
  ASSERT_EQ(decode_varint(buf, &llen), 127);
  ASSERT_EQ(buf[0], 127);
  ASSERT_EQ(llen, 1);

  ASSERT_EQ(encode_varint(128, buf), 2);
  ASSERT_EQ(decode_varint(buf, &llen), 128);
  ASSERT_EQ(buf[0], 128);
  ASSERT_EQ(buf[1], 1);
  ASSERT_EQ(llen, 2);

  ASSERT_EQ(encode_varint(0x4000, buf), 3);
  ASSERT_EQ(decode_varint(buf, &llen), 0x4000);
  ASSERT_EQ(buf[0], 128);
  ASSERT_EQ(buf[1], 128);
  ASSERT_EQ(buf[2], 1);
  ASSERT_EQ(llen, 3);

  return NULL;
}

static const char *test_interpreter(void) {
  struct v7 *v7 = v7_create();
  val_t v;
  const char *s, *c, *c0;

  v7_set(v7, v7->vals.global_object, "x", -1, v7_mk_number(42.0));

  ASSERT_EVAL_EQ(v7, "1%2/2", "0.5");

  ASSERT_EVAL_EQ(v7, "1+x", "43");
  ASSERT_EVAL_EQ(v7, "2-'1'", "1");
  ASSERT_EVAL_EQ(v7, "1+2", "3");
  /*
   * VC6 doesn't like string literals with escaped quotation marks to
   * be passed to macro arguments who stringify the arguments.
   *
   * With this workaround we allow us to build on windows but
   * it will make ASSERT_EVAL_EQ print out `c` instead of the actual
   * expected expression.
   *
   * TODO(mkm): since most of the expressions are constant
   * perhaps we could evaluate them instead of pasting stringifications.
   */
  c = "\"12\"";
  ASSERT_EVAL_EQ(v7, "'1'+'2'", c);
  ASSERT_EVAL_EQ(v7, "'1'+2", c);

  ASSERT_EVAL_EQ(v7, "false+1", "1");
  ASSERT_EVAL_EQ(v7, "true+1", "2");

  ASSERT_EVAL_EQ(v7, "'1'<2", "true");
  ASSERT_EVAL_EQ(v7, "'1'>2", "false");

  ASSERT_EVAL_EQ(v7, "1==1", "true");
  ASSERT_EVAL_EQ(v7, "1==2", "false");
  ASSERT_EVAL_EQ(v7, "'1'==1", "true");
  ASSERT_EVAL_EQ(v7, "'1'!=0", "true");
  ASSERT_EVAL_EQ(v7, "'-1'==-1", "true");
  ASSERT_EVAL_EQ(v7, "a={};a===a", "true");
  ASSERT_EVAL_EQ(v7, "a={};a!==a", "false");
  ASSERT_EVAL_EQ(v7, "a={};a==a", "true");
  ASSERT_EVAL_EQ(v7, "a={};a!=a", "false");
  ASSERT_EVAL_EQ(v7, "a={};b={};a===b", "false");
  ASSERT_EVAL_EQ(v7, "a={};b={};a!==b", "true");
  ASSERT_EVAL_EQ(v7, "a={};b={};a==b", "false");
  ASSERT_EVAL_EQ(v7, "a={};b={};a!=b", "true");
  ASSERT_EVAL_EQ(v7, "1-{}", "NaN");
  ASSERT_EVAL_EQ(v7, "a={};a===(1-{})", "false");
  ASSERT_EVAL_EQ(v7, "a={};a!==(1-{})", "true");
  ASSERT_EVAL_EQ(v7, "a={};a==(1-{})", "false");
  ASSERT_EVAL_EQ(v7, "a={};a!=(1-{})", "true");
  ASSERT_EVAL_EQ(v7, "a={};a===1", "false");
  ASSERT_EVAL_EQ(v7, "a={};a!==1", "true");
  ASSERT_EVAL_EQ(v7, "a={};a==1", "false");
  ASSERT_EVAL_EQ(v7, "a={};a!=1", "true");

  ASSERT_EVAL_EQ(v7, "'x'=='x'", "true");
  ASSERT_EVAL_EQ(v7, "'x'==='x'", "true");
  ASSERT_EVAL_EQ(v7, "'object'=='object'", "true");
  ASSERT_EVAL_EQ(v7, "'stringlong'=='longstring'", "false");
  ASSERT_EVAL_EQ(v7, "'object'==='object'", "true");
  ASSERT_EVAL_EQ(v7, "'a'<'b'", "true");
  ASSERT_EVAL_EQ(v7, "'b'>'a'", "true");
  ASSERT_EVAL_EQ(v7, "'a'>='a'", "true");
  ASSERT_EVAL_EQ(v7, "'a'<='a'", "true");

  ASSERT_EVAL_EQ(v7, "+'1'", "1");
  ASSERT_EVAL_EQ(v7, "-'-1'", "1");
  ASSERT_EQ(eval(v7, "v=[10+1,20*2,30/3]", &v), V7_OK);
  ASSERT_EQ(val_type(v7, v), V7_TYPE_ARRAY_OBJECT);
  ASSERT_EQ(v7_array_length(v7, v), 3);
  ASSERT(check_value(v7, v, "[11,40,10]"));
  ASSERT_EVAL_EQ(v7, "v[0]", "11");
  ASSERT_EVAL_EQ(v7, "v[1]", "40");
  ASSERT_EVAL_EQ(v7, "v[2]", "10");

  ASSERT_EQ(eval(v7, "v=[10+1,undefined,30/3]", &v), V7_OK);
  ASSERT_EQ(v7_array_length(v7, v), 3);
  ASSERT(check_value(v7, v, "[11,undefined,10]"));

  ASSERT_EQ(eval(v7, "v=[10+1,,30/3]", &v), V7_OK);
  ASSERT_EQ(v7_array_length(v7, v), 3);
  ASSERT(check_value(v7, v, "[11,,10]"));

  ASSERT_EVAL_EQ(v7, "3,2,1", "1");

  ASSERT_EVAL_EQ(v7, "x=1", "1");

  ASSERT_EVAL_EQ(v7, "1+2; 1", "1");
  ASSERT_EVAL_EQ(v7, "x=42; x", "42");
  ASSERT_EVAL_EQ(v7, "x=y=42; x+y", "84");

  ASSERT_EQ(eval(v7, "o={a: 1, b: 2}", &v), V7_OK);
  ASSERT_EVAL_EQ(v7, "o['a'] + o['b']", "3");

  ASSERT_EVAL_EQ(v7, "o.a + o.b", "3");

  ASSERT_EVAL_EQ(v7, "Array(1,2)", "[1,2]");
  ASSERT_EVAL_EQ(v7, "new Array(1,2)", "[1,2]");
  ASSERT_EVAL_OK(v7,
                 "Object.isPrototypeOf(Array(1,2), Object.getPrototypeOf([]))");
  ASSERT_EVAL_EQ(v7, "a=[];r=a.push(1,2,3);[r,a]", "[3,[1,2,3]]");

  ASSERT_EVAL_EQ(v7, "x=1;if(x>0){x=2};x", "2");
  ASSERT_EVAL_EQ(v7, "x=1;if(x<0){x=2};x", "1");
  ASSERT_EVAL_EQ(v7, "x=0;if(true)x=2;else x=3;x", "2");
  ASSERT_EVAL_EQ(v7, "x=0;if(false)x=2;else x=3;x", "3");

  ASSERT_EVAL_EQ(v7, "y=1;x=5;while(x > 0){y=y*x;x=x-1};y", "120");
  ASSERT_EVAL_EQ(v7, "y=1;x=5;do{y=y*x;x=x-1}while(x>0);y", "120");
  ASSERT_EVAL_EQ(v7, "for(y=1,i=1;i<=5;i=i+1)y=y*i;y", "120");
  ASSERT_EVAL_EQ(v7, "for(i=0;1;i++)if(i==5)break;i", "5");
  ASSERT_EVAL_EQ(v7, "for(i=0;1;i++)if(i==5)break;i", "5");
  ASSERT_EVAL_EQ(v7, "i=0;while(++i)if(i==5)break;i", "5");
  ASSERT_EVAL_EQ(v7, "i=0;do{if(i==5)break}while(++i);i", "5");
  ASSERT_EVAL_EQ(v7, "(function(){i=0;do{if(i==5)break}while(++i);i+=10})();i",
                 "15");
  ASSERT_EVAL_EQ(v7,
                 "(function(){x=i=0;do{if(i==5)break;if(i%2)continue;x++}while("
                 "++i);i+=10})();[i,x]",
                 "[15,3]");
  ASSERT_EVAL_EQ(v7, "(function(){i=0;while(++i){if(i==5)break};i+=10})();i",
                 "15");
  ASSERT_EVAL_EQ(v7,
                 "(function(){x=i=0;while(++i){if(i==5)break;if(i%2)continue;x+"
                 "+};i+=10})();[i,x]",
                 "[15,2]");
  ASSERT_EVAL_EQ(v7, "(function(){for(i=0;1;++i){if(i==5)break};i+=10})();i",
                 "15");
  ASSERT_EVAL_EQ(v7,
                 "(function(){x=0;for(i=0;1;++i){if(i==5)break;if(i%2)continue;"
                 "x++};i+=10})();[i,x]",
                 "[15,3]");
  ASSERT_EVAL_EQ(v7, "a=1,[(function(){function a(){1+2}; return a})(),a]",
                 "[[function a()],1]");
  ASSERT_EVAL_EQ(
      v7, "x=0;(function(){try{ff; x=42}catch(e){x=1};function ff(){}})();x",
      "42");
  ASSERT_EVAL_EQ(v7, "a=1,[(function(){return a; function a(){1+2}})(),a]",
                 "[[function a()],1]");
  ASSERT_EVAL_EQ(v7, "(function(){f=42;function f(){};return f})()", "42");

  ASSERT_EVAL_EQ(v7, "x=0;try{x=1}finally{};x", "1");
  ASSERT_EVAL_EQ(v7, "x=0;try{x=1}finally{x=x+1};x", "2");
  ASSERT_EVAL_EQ(v7, "x=0;try{x=1}catch(e){x=100}finally{x=x+1};x", "2");

  ASSERT_EVAL_EQ(v7, "x=0;try{xxx;var xxx;x=42}catch(e){x=1};x", "42");

  ASSERT_EVAL_EQ(v7, "(function(a) {return a})", "[function(a)]");
  ASSERT_EVAL_EQ(v7, "(function() {var x=1,y=2; return x})",
                 "[function(){var x,y}]");
  ASSERT_EVAL_EQ(v7, "(function(a) {var x=1,y=2; return x})",
                 "[function(a){var x,y}]");
  ASSERT_EVAL_EQ(v7, "(function(a,b) {var x=1,y=2; return x; var z})",
                 "[function(a,b){var x,y,z}]");
  ASSERT_EVAL_EQ(v7, "(function(a) {var x=1; for(var y in x){}; var z})",
                 "[function(a){var x,y,z}]");
  ASSERT_EVAL_EQ(v7, "(function(a) {var x=1; for(var y=0;y<x;y++){}; var z})",
                 "[function(a){var x,y,z}]");
  ASSERT_EVAL_EQ(v7, "(function() {var x=(function y(){for(var z;;){}})})",
                 "[function(){var x}]");
  ASSERT_EVAL_EQ(v7, "function square(x){return x*x;};square",
                 "[function square(x)]");
  ASSERT_EVAL_EQ(v7, "0;f=(function(x){return x*x;})", "[function(x)]");

  ASSERT_EVAL_EQ(v7, "f=(function(x){return x*x;}); f(2)", "4");
  ASSERT_EVAL_EQ(v7, "(function(x){x*x;})(2)", "undefined");
  ASSERT_EVAL_EQ(v7, "f=(function(x){return x*x;x});v=f(2);v*2", "8");
  ASSERT_EVAL_EQ(v7, "(function(x,y){return x+y;})(40,2)", "42");
  ASSERT_EVAL_EQ(v7, "(function(x,y){if(x==40)return x+y})(40,2)", "42");
  ASSERT_EVAL_EQ(v7, "(function(x,y){return x+y})(40)", "NaN");
  ASSERT_EVAL_EQ(v7, "(function(x){return x+y; var y})(40)", "NaN");
  ASSERT_EVAL_EQ(v7, "x=1;(function(a){return a})(40,(function(){x=x+1})())+x",
                 "42");
  ASSERT_EVAL_EQ(v7, "(function(){x=42;return;x=0})();x", "42");
  ASSERT_EVAL_EQ(v7, "(function(){for(i=0;1;i++)if(i==5)return i})()", "5");
  ASSERT_EVAL_EQ(v7, "(function(){i=0;while(++i)if(i==5)return i})()", "5");
  ASSERT_EVAL_EQ(v7, "(function(){i=0;do{if(i==5)return i}while(++i)})()", "5");

  ASSERT_EQ(
      eval(v7, "(function(x,y){return x+y})(40,2,(function(){return fail})())",
           &v),
      V7_EXEC_EXCEPTION);

  ASSERT_EVAL_EQ(v7, "x=42; (function(){return x})()", "42");
  ASSERT_EVAL_EQ(v7, "x=2; (function(x){return x})(40)+x", "42");
  ASSERT_EVAL_EQ(v7, "x=1; (function(y){x=x+1; return y})(40)+x", "42");
  ASSERT_EVAL_EQ(
      v7, "x=0;f=function(){x=42; return function() {return x}; var x};f()()",
      "42");
  ASSERT_EVAL_EQ(v7, "x=42;o={x:66,f:function(){return this}};o.f().x", "66");
  ASSERT_EVAL_EQ(v7, "x=42;o={x:66,f:function(){return this}};(1,o.f)().x",
                 "42");
  ASSERT_EVAL_EQ(v7, "x=66;o={x:42,f:function(){return this.x}};o.f()", "42");

  ASSERT_EVAL_NUM_EQ(
      v7,
      "x=0; function foo() { x++; return {bar: function() {}}}; foo().bar(); "
      "x;",
      1);

  ASSERT_EVAL_NUM_EQ(
      v7,
      "x=0; function foo() { x++; return [0,function() {}]}; foo()[0+1](); "
      "x;",
      1);

  ASSERT_EVAL_EQ(v7, "o={};o.x=24", "24");
  ASSERT_EVAL_EQ(v7, "o.a={};o.a.b={c:66};o.a.b.c", "66");
  ASSERT_EVAL_EQ(v7, "o['a']['b'].c", "66");
  ASSERT_EVAL_EQ(v7, "o={a:1}; o['a']=2;o.a", "2");
  ASSERT_EVAL_EQ(v7, "a={f:function(){return {b:55}}};a.f().b", "55");
  ASSERT_EVAL_EQ(v7, "(function(){fox=1})();fox", "1");

  ASSERT_EVAL_EQ(
      v7,
      "fin=0;(function(){while(1){try{xxxx}finally{fin=1;return 1}}})();fin",
      "1");
  ASSERT_EVAL_EQ(v7,
                 "ca=0;fin=0;(function(){try{(function(){try{xxxx}finally{fin="
                 "1}})()}catch(e){ca=1}})();fin+ca",
                 "2");
  ASSERT_EVAL_EQ(v7, "x=0;try{throw 1}catch(e){x=42};x", "42");

  ASSERT_EVAL_EQ(v7, "x=1;x=x<<3;x", "8");
  ASSERT_EVAL_EQ(v7, "x=1;x<<=4;x", "16");
  ASSERT_EVAL_EQ(v7, "x=1;x++", "1");
  ASSERT_EVAL_EQ(v7, "x", "2");
  ASSERT_EVAL_EQ(v7, "x=1;++x", "2");
  ASSERT_EVAL_EQ(v7, "x", "2");
  ASSERT_EVAL_EQ(v7, "o={x:1};o.x++", "1");
  ASSERT_EVAL_EQ(v7, "o.x", "2");

  c = "\"undefined\"";
  ASSERT_EVAL_EQ(v7, "x=undefined; typeof x", c);
  c = "\"undefined\"";
  ASSERT_EVAL_EQ(v7, "typeof undefined", c);
  c = "\"undefined\"";
  ASSERT_EVAL_EQ(v7, "typeof dummyx", c);
  c = "\"object\"";
  ASSERT_EVAL_EQ(v7, "typeof null", c);
  c = "\"number\"";
  ASSERT_EVAL_EQ(v7, "typeof 1", c);
  ASSERT_EVAL_EQ(v7, "typeof (1+2)", c);
  c = "\"string\"";
  ASSERT_EVAL_EQ(v7, "typeof 'test'", c);
  c = "\"object\"";
  ASSERT_EVAL_EQ(v7, "typeof [1,2]", c);
  c = "\"function\"";
  ASSERT_EVAL_EQ(v7, "typeof function(){}", c);

  ASSERT_EVAL_EQ(v7, "void(1+2)", "undefined");
  ASSERT_EVAL_EQ(v7, "true?1:2", "1");
  ASSERT_EVAL_EQ(v7, "false?1:2", "2");
  ASSERT_EVAL_EQ(v7, "'a' in {a:1}", "true");
  ASSERT_EVAL_EQ(v7, "'b' in {a:1}", "false");
  ASSERT_EVAL_EQ(v7, "1 in [10,20]", "true");
  ASSERT_EVAL_EQ(v7, "20 in [10,20]", "false");

  c = "\"undefined\"";
  ASSERT_EVAL_EQ(v7, "x=1; delete x; typeof x", c);
  ASSERT_EVAL_EQ(v7, "x=1; (function(){x=2;delete x; return typeof x})()", c);
  ASSERT_EVAL_EQ(v7, "x=1; (function(){x=2;delete x})(); typeof x", c);
  ASSERT_EVAL_EQ(v7, "x=1; (function(){var x=2;delete x})(); x", "1");
  ASSERT_EVAL_EQ(v7, "o={a:1};delete o.a;o", "{}");
  ASSERT_EVAL_EQ(v7, "o={a:1};delete o['a'];o", "{}");
  ASSERT_EVAL_EQ(v7, "x=0;if(delete 1 == true)x=42;x", "42");

  c = "[{\"a\":[Circular]}]";
  ASSERT_EVAL_EQ(v7, "o={};a=[o];o.a=a;a", c);

  ASSERT_EVAL_EQ(v7, "new TypeError instanceof Error", "true");
  ASSERT_EVAL_EQ(v7, "new TypeError instanceof TypeError", "true");
  ASSERT_EVAL_EQ(v7, "new Error instanceof Object", "true");
  ASSERT_EVAL_EQ(v7, "new Error instanceof TypeError", "false");
  ASSERT_EVAL_EQ(v7, "({}) instanceof Object", "true");

  c = "\"Error: foo\"";
  ASSERT_EVAL_EQ(v7, "(new Error('foo'))+''", c);

  ASSERT_EQ(eval(v7, "", &v), V7_OK);
  ASSERT(v7_is_undefined(v));
#if 0
  ASSERT_EVAL_EQ(v7, "x=0;a=1;o={a:2};with(o){x=a};x", "2");
#endif

  ASSERT_EVAL_EQ(
      v7,
      "(function(){try {throw new Error}catch(e){c=e}})();c instanceof Error",
      "true");
  c = "\"undefined\"";
  ASSERT_EVAL_EQ(
      v7, "delete e;(function(){try {throw new Error}catch(e){}})();typeof e",
      c);
  ASSERT_EVAL_EQ(
      v7, "x=(function(){c=1;try {throw 1}catch(e){c=0};return c})()", "0");
  ASSERT_EVAL_EQ(
      v7, "x=(function(){var c=1;try {throw 1}catch(e){c=0};return c})()", "0");
  ASSERT_EVAL_EQ(
      v7, "c=1;x=(function(){try {throw 1}catch(e){var c=0};return c})();[c,x]",
      "[1,0]");
  ASSERT_EVAL_EQ(
      v7, "c=1;x=(function(){try {throw 1}catch(e){c=0};return c})();[c,x]",
      "[0,0]");

  ASSERT_EVAL_EQ(v7, "Object.keys(new Boolean(1))", "[]");
  c = "[\"d\"]";
  ASSERT_EVAL_EQ(v7, "b={c:1};a=Object.create(b); a.d=4;Object.keys(a)", c);
  ASSERT_EVAL_EQ(v7, "Object.getOwnPropertyNames(new Boolean(1))", "[]");
  c = "[\"d\"]";
  ASSERT_EVAL_EQ(
      v7, "b={c:1};a=Object.create(b); a.d=4;Object.getOwnPropertyNames(a)", c);
  c = "o={};Object.defineProperty(o, \"x\", {value:2});[o.x,o]";
  ASSERT_EVAL_EQ(v7, c, "[2,{}]");
  c = "[2,3,{\"y\":3}]";
  ASSERT_EVAL_EQ(v7,
                 "o={};Object.defineProperties(o,{x:{value:2},y:{value:3,"
                 "enumerable:true}});[o.x,o.y,o]",
                 c);
  c0 =
      "o={};Object.defineProperty(o, \"x\", {value:2,enumerable:true});[o.x,o]";
  c = "[2,{\"x\":2}]";
  ASSERT_EVAL_EQ(v7, c0, c);
  ASSERT_EVAL_EQ(
      v7,
      "o={};Object.defineProperty(o,'a',{value:1});o.propertyIsEnumerable('a')",
      "false");
  ASSERT_EVAL_EQ(v7,
                 "o={};Object.defineProperty(o,'a',{value:1,enumerable:true});"
                 "o.propertyIsEnumerable('a')",
                 "true");
  ASSERT_EVAL_EQ(v7, "o={a:1};o.propertyIsEnumerable('a')", "true");
  ASSERT_EVAL_EQ(v7, "b={a:1};o=Object.create(b);o.propertyIsEnumerable('a')",
                 "false");
  ASSERT_EVAL_EQ(v7, "b={a:1};o=Object.create(b);o.hasOwnProperty('a')",
                 "false");
  ASSERT_EVAL_EQ(v7, "o={a:1};o.hasOwnProperty('a')", "true");
  ASSERT_EVAL_EQ(v7,
                 "o={a:1};d=Object.getOwnPropertyDescriptor(o, 'a');"
                 "[d.value,d.writable,d.enumerable,d.configurable]",
                 "[1,true,true,true]");
  ASSERT_EVAL_EQ(v7,
                 "o={};Object.defineProperty(o,'a',{value:1,enumerable:true});"
                 "d=Object.getOwnPropertyDescriptor(o, 'a');"
                 "[d.value,d.writable,d.enumerable,d.configurable]",
                 "[1,false,true,false]");
  ASSERT_EVAL_EQ(v7,
                 "o=Object.defineProperty({},'a',{value:1,enumerable:true});o."
                 "a=2;o.a",
                 "1");
  ASSERT_EVAL_EQ(v7,
                 "o=Object.defineProperty({},'a',{value:1,enumerable:true});r="
                 "delete o.a;[r,o.a]",
                 "[false,1]");

  ASSERT_EVAL_EQ(v7, "r=0;o={a:1,b:2};for(i in o){r+=o[i]};r", "3");
  ASSERT_EVAL_EQ(v7, "r=0;o={a:1,b:2};for(var i in o){r+=o[i]};r", "3");
  ASSERT_EVAL_EQ(v7, "r=1;for(var i in null){r=0};r", "1");
  ASSERT_EVAL_EQ(v7, "r=1;for(var i in undefined){r=0};r", "1");
  ASSERT_EVAL_EQ(v7, "r=1;for(var i in 42){r=0};r", "1");

  ASSERT_EQ(v7_exec_with(v7, "this", v7_mk_number(42), &v), V7_OK);
  ASSERT(check_value(v7, v, "42"));
  ASSERT(v7_exec_with(v7, "a=666;(function(a){return a})(this)",
                      v7_mk_number(42), &v) == V7_OK);
  ASSERT(check_value(v7, v, "42"));

  c = "\"aa bb\"";
  ASSERT_EVAL_EQ(v7, "a='aa', b='bb';(function(){return a + ' ' + b;})()", c);

  s = "{\"fall\":2,\"one\":1}";
  ASSERT_EVAL_EQ(v7,
                 "o={};switch(1) {case 1: o.one=1; case 2: o.fall=2; break; "
                 "case 3: o.three=1; };o",
                 s);
  ASSERT_EVAL_EQ(v7,
                 "o={};for(i=0;i<1;i++) switch(1) {case 1: o.one=1; case 2: "
                 "o.fall=2; continue; case 3: o.three=1; };o",
                 s);
  ASSERT_EVAL_EQ(v7,
                 "(function(){o={};switch(1) {case 1: o.one=1; case 2: "
                 "o.fall=2; return o; case 3: o.three=1; }})()",
                 s);
  ASSERT_EVAL_EQ(v7,
                 "o={};switch(1) {case 1: o.one=1; default: o.fall=2; break; "
                 "case 3: o.three=1; };o",
                 s);
  c = "{\"def\":1}";
  ASSERT_EVAL_EQ(v7,
                 "o={};switch(10) {case 1: o.one=1; case 2: o.fall=2; break; "
                 "case 3: o.three=1; break; default: o.def=1};o",
                 c);

#ifdef V7_ENABLE_JS_GETTERS
  ASSERT_EVAL_EQ(v7, "o={get x(){return 42}};o.x", "42");
  ASSERT_EVAL_EQ(v7, "o={get x(){return 10},set x(v){}};o.x", "10");
#endif
#ifdef V7_ENABLE_JS_SETTERS
  ASSERT_EVAL_EQ(v7, "o={set x(a){this.y=a}};o.x=42;o.y", "42");
  ASSERT_EVAL_EQ(v7, "o={set x(v){},get x(){return 10}};o.x", "10");
#endif
#if defined(V7_ENABLE_JS_GETTERS) && defined(V7_ENABLE_JS_SETTERS)
  ASSERT_EVAL_EQ(v7, "r=0;o={get x() {return 10}, set x(v){r=v}};o.x=10;r",
                 "10");
#endif
#ifdef V7_ENABLE_JS_GETTERS
  ASSERT_EVAL_EQ(v7,
                 "g=0;function O() {}; O.prototype = {set x(v) {g=v}};o=new "
                 "O;o.x=42;[g,Object.keys(o)]",
                 "[42,[]]");
#endif

/* TODO(dfrank): implement this for bcode */
#if 0
  ASSERT_EVAL_EQ(v7, "({foo(x){return x*2}}).foo(21)", "42");
#endif

  c = "\"42\"";
  ASSERT_EVAL_EQ(v7, "String(new Number(42))", c);

/* TODO(dfrank): implement labelled blocks for bcode */
#if 0
  ASSERT_EVAL_EQ(
      v7, "L: for(i=0;i<10;i++){for(j=4;j<10;j++){if(i==j) break L}};i", "4");
  ASSERT_EVAL_EQ(
      v7, "L: for(i=0;i<10;i++){M:for(j=4;j<10;j++){if(i==j) break L}};i", "4");
  ASSERT_EVAL_EQ(v7,
                 "x=0;L: for(i=0;i<10;i++){try{for(j=4;j<10;j++){if(i==j) "
                 "break L}}finally{x++}};x",
                 "5");
  ASSERT_EVAL_EQ(v7, "x=0;L: for(i=0;i<11;i++) {if(i==5) continue L; x+=i}; x",
                 "50");
  ASSERT_EVAL_EQ(
      v7, "x=0;L: if(true) for(i=0;i<11;i++) {if(i==5) continue L; x+=i}; x",
      "50");
  ASSERT_EVAL_EQ(
      v7, "x=0;L: if(true) for(i=0;i<11;i++) {if(i==5) continue L; x+=i}; x",
      "50");
  ASSERT_EVAL_EQ(v7, "L:do {i=0;continue L;}while(i>0);i", "0");
  ASSERT_EVAL_EQ(v7, "i=1; L:while(i>0){i=0;continue L;};i", "0");
#endif

  ASSERT_EVAL_EQ(v7, "1 | NaN", "1");
  ASSERT_EVAL_EQ(v7, "NaN | 1", "1");
  ASSERT_EVAL_EQ(v7, "NaN | NaN", "0");

  ASSERT_EVAL_EQ(v7, "0 || 1", "1");
  ASSERT_EVAL_EQ(v7, "0 || {}", "{}");
  ASSERT_EVAL_EQ(v7, "1 && 0", "0");
  ASSERT_EVAL_EQ(v7, "1 && {}", "{}");
  c = "\"\"";
  ASSERT_EVAL_EQ(v7, "'' && {}", c);

  ASSERT_EQ(v7_exec_with(v7, "a=this;a", v7_mk_foreign((void *) "foo"), &v),
            V7_OK);
  ASSERT(v7_is_foreign(v));
  ASSERT_EQ(strcmp((char *) v7_to_foreign(v), "foo"), 0);

  ASSERT_EVAL_EQ(v7, "a=[1,2,3];a.splice(0,1);a", "[2,3]");
  ASSERT_EVAL_EQ(v7, "a=[1,2,3];a.splice(2,1);a", "[1,2]");
  ASSERT_EVAL_EQ(v7, "a=[1,2,3];a.splice(1,1);a", "[1,3]");

  ASSERT_EVAL_EQ(v7, "a=[1,2,3];a.slice(0,1)", "[1]");
  ASSERT_EVAL_EQ(v7, "a=[1,2,3];a.slice(2,3)", "[3]");
  ASSERT_EVAL_EQ(v7, "a=[1,2,3];a.slice(1,3)", "[2,3]");

  c = "[\"b\",\"a\"]";
  ASSERT_EVAL_EQ(v7,
                 "function foo(){}; foo.prototype={a:1}; f=new foo; f.b=2; "
                 "r=[];for(p in f) r.push(p); r",
                 c);

  /* here temporarily because test_stdlib has memory violations */
  ASSERT_EVAL_EQ(v7, "a=[2,1];a.sort();a", "[1,2]");

  /* check execution failure caused by bad parsing */
  ASSERT_EQ(eval(v7, "function", &v), V7_SYNTAX_ERROR);

  v7_destroy(v7);
  return NULL;
} /* test_interpreter */

static const char *test_strings(void) {
  val_t s;
  struct v7 *v7;
  size_t off;

  v7 = v7_create();
  off = v7->owned_strings.len;
  ASSERT(off > 0); /* properties names use it */

  s = v7_mk_string(v7, "hi", 2, 1);
  ASSERT_EQ(memcmp(&s, "\x02\x68\x69\x00\x00\x00\xfa\xff", sizeof(s)), 0);
  ASSERT_EQ(v7->foreign_strings.len, 0);
  ASSERT_EQ(v7->owned_strings.len, off);

  /* Make sure strings with length 5 & 6 are nan-packed */
  s = v7_mk_string(v7, "length", 4, 1);
  ASSERT_EQ(v7->owned_strings.len, off);
  s = v7_mk_string(v7, "length", 5, 1);
  ASSERT_EQ(v7->owned_strings.len, off);
  ASSERT_EQ(memcmp(&s, "\x6c\x65\x6e\x67\x74\x00\xf9\xff", sizeof(s)), 0);

  s = v7_mk_string(v7, "longer one", ~0 /* use strlen */, 1);
  ASSERT(v7->owned_strings.len == off + 12);
  ASSERT_EQ(memcmp(v7->owned_strings.buf + off, "\x0alonger one\x00", 12), 0);

  s = v7_mk_string(v7, "with embedded \x00 one", 19, 1);

  ASSERT(v7->owned_strings.len == off + 33);
  ASSERT(memcmp(v7->owned_strings.buf + off,
                "\x0alonger one\x00"
                "\x13with embedded \x00 one\x00",
                33) == 0);

  {
    const char *lit = "foobarbaz";
    size_t l;
    const char *p;
    s = v7_mk_string(v7, lit, ~0, 0);
    p = v7_get_string_data(v7, &s, &l);
    /* ASSERT_EQ(l, (size_t) 9); */
    ASSERT(p == lit);

    if (sizeof(void *) <= 4) {
      val_t n = v7_mk_string(v7, lit, ~0, 0);
      ASSERT(n == s);
    }
  }

  v7_destroy(v7);

  return NULL;
}

static const char *test_interp_unescape(void) {
  struct v7 *v7 = v7_create();
  val_t v;

  ASSERT_EQ(eval(v7, "'1234'", &v), V7_OK);
  ASSERT_EQ((v & V7_TAG_MASK), V7_TAG_STRING_I);
  ASSERT_EQ(eval(v7, "'12345'", &v), V7_OK);
  ASSERT_EQ((v & V7_TAG_MASK), V7_TAG_STRING_5);
  ASSERT_EQ(eval(v7, "'123456'", &v), V7_OK);
  ASSERT_EQ((v & V7_TAG_MASK), V7_TAG_STRING_O);

  ASSERT_EVAL_NUM_EQ(v7, "'123'.length", 3);
  ASSERT_EVAL_NUM_EQ(v7, "'123\\n'.length", 4);
  ASSERT_EVAL_NUM_EQ(v7, "'123\\n\\n'.length", 5);
  ASSERT_EVAL_NUM_EQ(v7, "'123\\n\\n\\n'.length", 6);
  ASSERT_EVAL_NUM_EQ(v7, "'123\\n\\n\\n\\n'.length", 7);
  ASSERT_EVAL_NUM_EQ(v7, "'123\\n\\n\\n\\n\\n'.length", 8);

  ASSERT_EVAL_EQ(v7, "'123\\\\\\\\'.length == '1234\\\\\\\\'.length", "false");

  ASSERT_EVAL_NUM_EQ(v7, "'123'.length", 3);
  ASSERT_EVAL_NUM_EQ(v7, "'123\\\\'.length", 4);
  ASSERT_EVAL_NUM_EQ(v7, "'123\\\\\\\\'.length", 5);
  ASSERT_EVAL_NUM_EQ(v7, "'123\\\\\\\\\\\\'.length", 6);
  ASSERT_EVAL_NUM_EQ(v7, "'123\\\\\\\\\\\\\\\\'.length", 7);
  ASSERT_EVAL_NUM_EQ(v7, "'123\\\\\\\\\\\\\\\\\\\\'.length", 8);

  ASSERT_EQ(eval(v7, "'1234\\\\\\\\'", &v), V7_OK);
  ASSERT_EQ((v & V7_TAG_MASK), V7_TAG_STRING_O);

  v7_destroy(v7);
  return NULL;
}

static const char *test_to_json(void) {
  char buf[100], *p;
  const char *c;
  struct v7 *v7 = v7_create();
  val_t v = v7_mk_undefined();
  v7_own(v7, &v);

  c = "123.45";
  eval(v7, "123.45", &v);
  ASSERT((p = v7_to_json(v7, v, buf, sizeof(buf))) == buf);
  ASSERT_STREQ(p, c);
#if TODO
  eval(v7, "({a: new Number(123.45)})", &v);
  ASSERT((p = v7_to_json(v7, v, buf, sizeof(buf))) == buf);
  ASSERT_STREQ(p, c);
#endif
  ASSERT((p = v7_to_json(v7, v, buf, 3)) != buf);
  ASSERT_EQ(strcmp(p, "123.45"), 0);
  free(p);

  c = "\"foo\"";
  eval(v7, "'foo'", &v);
  ASSERT((p = v7_to_json(v7, v, buf, sizeof(buf))) == buf);
  ASSERT_STREQ(p, c);
#if TODO
  eval(v7, "new String('foo')", &v);
  ASSERT((p = v7_to_json(v7, v, buf, sizeof(buf))) == buf);
  ASSERT_STREQ(p, c);
#endif

  ASSERT((p = v7_to_json(v7, v7_mk_null(), buf, sizeof(buf))) == buf);
  ASSERT_STREQ(p, "null");

  c = "{\"a\":null}";
  eval(v7, "({a: null})", &v);
  ASSERT((p = v7_to_json(v7, v, buf, sizeof(buf))) == buf);
  ASSERT_STREQ(p, c);

  c = "\"\\\"foo\\\"\"";
  eval(v7, "'\"foo\"'", &v);
  ASSERT((p = v7_to_json(v7, v, buf, sizeof(buf))) == buf);
  ASSERT_STREQ(p, c);

  eval(v7, "[\"foo\"]", &v);
  ASSERT((p = v7_to_json(v7, v, buf, sizeof(buf))) == buf);
  c = "[\"foo\"]";
  ASSERT_STREQ(p, c);

  c = "{\"a\":true}";
  eval(v7, "({a: true})", &v);
  ASSERT((p = v7_to_json(v7, v, buf, sizeof(buf))) == buf);
  ASSERT_STREQ(p, c);
#if TODO
  eval(v7, "({a: new Boolean(true)})", &v);
  ASSERT((p = v7_to_json(v7, v, buf, sizeof(buf))) == buf);
  ASSERT_STREQ(p, c);
#endif

  eval(v7, "({a: [\"foo\"]})", &v);
  ASSERT((p = v7_to_json(v7, v, buf, sizeof(buf))) == buf);
  c = "{\"a\":[\"foo\"]}";
  ASSERT_STREQ(p, c);

  eval(v7, "({a: function(){}, b: 123.45})", &v);
  ASSERT((p = v7_to_json(v7, v, buf, sizeof(buf))) == buf);
  c = "{\"b\":123.45}";
  ASSERT_STREQ(p, c);

  eval(v7, "[1, function(){}, 2]", &v);
  ASSERT((p = v7_to_json(v7, v, buf, sizeof(buf))) == buf);
  c = "[1,null,2]";
  ASSERT_STREQ(p, c);

  eval(v7, "({a: new Date(2015, 10, 3)})", &v);
  ASSERT((p = v7_to_json(v7, v, buf, sizeof(buf))) == buf);
  c = "{\"a\":\"2015-11-03T00:00:00.000Z\"}";
  ASSERT_STREQ(p, c);

  v7_destroy(v7);
  return NULL;
}

static const char *test_json_parse(void) {
  struct v7 *v7 = v7_create();
  const char *c1, *c2;

  ASSERT_EVAL_NUM_EQ(v7, "JSON.parse(42)", 42);
  c1 = "JSON.parse('\"foo\"')";
  c2 = "\"foo\"";
  ASSERT_EVAL_EQ(v7, c1, c2);
  c2 = "\"foo\"";
  ASSERT_EVAL_EQ(v7, "JSON.parse(JSON.stringify('foo'))", c2);
  c2 = "{\"foo\":\"bar\"}";
  ASSERT_EVAL_EQ(v7, "JSON.parse(JSON.stringify({'foo':'bar'}))", c2);

  c1 =
      "JSON.parse(JSON.stringify('"
      "foooooooooooooooooooooooooooooooooooooooooooooooo"
      "ooooooooooooooooooooooooooooooooooooooooooooooooo'))",

  c2 =
      "\"foooooooooooooooooooooooooooooooooooooooooooooooo"
      "ooooooooooooooooooooooooooooooooooooooooooooooooo\"";

  /* big string, will cause malloc */
  ASSERT_EVAL_EQ(v7, c1, c2);

  c2 = "\"{\\\"a\\\":\\\"\\\\n\\\"}\"";
  ASSERT_EVAL_EQ(v7, "JSON.stringify({a:'\n'})", c2);

  c1 = "\"\\b\"";
  ASSERT_EVAL_EQ(v7, c1, c1);
  c1 = "\"\\t\"";
  ASSERT_EVAL_EQ(v7, c1, c1);
  c1 = "\"\\n\"";
  ASSERT_EVAL_EQ(v7, c1, c1);
  c1 = "\"\\v\"";
  ASSERT_EVAL_EQ(v7, c1, c1);
  c1 = "\"\\f\"";
  ASSERT_EVAL_EQ(v7, c1, c1);
  c1 = "\"\\r\"";
  ASSERT_EVAL_EQ(v7, c1, c1);
  c1 = "\"\\\\\"";
  ASSERT_EVAL_EQ(v7, c1, c1);

  {
    val_t res;
    const char *s;
    size_t len;

    eval(v7, "JSON.stringify('\"\\n\"')", &res);
    s = v7_get_string_data(v7, &res, &len);

    c1 = "\"\\\"\\n\\\"\"";
    ASSERT_STREQ(s, c1);
  }

  v7_destroy(v7);
  return NULL;
}

static const char *test_unescape(void) {
  char buf[100];
  ASSERT_EQ(unescape("\\n", 2, buf), 1);
  ASSERT(buf[0] == '\n');
  ASSERT_EQ(unescape("\\u0061", 6, buf), 1);
  ASSERT(buf[0] == 'a');
  ASSERT_EQ(unescape("гы", 4, buf), 4);
  ASSERT_EQ(memcmp(buf, "\xd0\xb3\xd1\x8b", 4), 0);
  ASSERT_EQ(unescape("\\\"", 2, buf), 1);
  ASSERT_EQ(memcmp(buf, "\"", 1), 0);
  ASSERT_EQ(unescape("\\'", 2, buf), 1);
  ASSERT_EQ(memcmp(buf, "'", 1), 0);
  ASSERT_EQ(unescape("\\\n", 2, buf), 1);
  ASSERT_EQ(memcmp(buf, "\n", 1), 0);
  return NULL;
}

#ifndef V7_DISABLE_GC
static const char *test_gc_ptr_check(void) {
  struct v7 *v7 = v7_create();
  val_t v;

  eval(v7, "o=({})", &v);
  assert(gc_check_val(v7, v));
  ASSERT(gc_check_ptr(&v7->generic_object_arena, v7_to_generic_object(v)));
#ifndef V7_MALLOC_GC
  ASSERT(!gc_check_ptr(&v7->generic_object_arena, "foo"));
#endif

  v7_destroy(v7);
  return NULL;
}

static const char *test_gc_mark(void) {
  struct v7 *v7 = v7_create();
  val_t v;

  eval(v7, "o=({a:{b:1},c:{d:2},e:null});o.e=o;o", &v);
  gc_mark(v7, v);
  ASSERT(MARKED(v7_to_generic_object(v)));
  v7_gc(v7, 0); /* cleanup marks */
  v7_destroy(v7);
  v7 = v7_create();

  eval(v7, "o=({a:{b:1},c:{d:2},e:null});o.e=o;o", &v);
  gc_mark(v7, v7->vals.global_object);
  ASSERT(MARKED(v7_to_generic_object(v)));
  v7_gc(v7, 0); /* cleanup marks */
  v7_destroy(v7);
  v7 = v7_create();

  eval(v7, "function f() {}; o=new f;o", &v);
  gc_mark(v7, v);
  ASSERT(MARKED(v7_to_generic_object(v)));
  v7_gc(v7, 0); /* cleanup marks */
  v7_destroy(v7);
  v7 = v7_create();

  eval(v7, "function f() {}; Object.getPrototypeOf(new f)", &v);
  gc_mark(v7, v7->vals.global_object);
  ASSERT(MARKED(v7_to_generic_object(v)));
  v7_gc(v7, 0); /* cleanup marks */
  v7_destroy(v7);
  v7 = v7_create();

  eval(v7, "({a:1})", &v);
  gc_mark(v7, v7->vals.global_object);
  ASSERT(!MARKED(v7_to_generic_object(v)));
  v7_gc(v7, 0); /* cleanup marks */
  v7_destroy(v7);
  v7 = v7_create();

  eval(v7, "var f;(function() {var x={a:1};f=function(){return x};return x})()",
       &v);
  gc_mark(v7, v7->vals.global_object);
  /* `x` is reachable through `f`'s closure scope */
  ASSERT(MARKED(v7_to_generic_object(v)));
  v7_gc(v7, 0); /* cleanup marks */
  v7_destroy(v7);
  v7 = v7_create();

  eval(v7, "(function() {var x={a:1};var f=function(){return x};return x})()",
       &v);
  gc_mark(v7, v7->vals.global_object);
  /* `f` is unreachable, hence `x` is not marked through the scope */
  ASSERT(!MARKED(v7_to_generic_object(v)));
  v7_gc(v7, 0); /* cleanup marks */
  v7_destroy(v7);

  return NULL;
}

#ifndef V7_MALLOC_GC
static const char *test_gc_sweep(void) {
  struct v7 *v7 = v7_create();
  val_t v;
#if V7_ENABLE__Memory__stats
  uint32_t alive;
#endif

  v7_gc(v7, 0);
#if V7_ENABLE__Memory__stats
  alive = v7->generic_object_arena.alive;
#endif
  eval(v7, "x=({a:1})", &v);
  v7_to_generic_object(v);
  v7_gc(v7, 0);
#if V7_ENABLE__Memory__stats
  ASSERT(v7->generic_object_arena.alive > alive);
#endif
  ASSERT_EVAL_EQ(v7, "x.a", "1");

  ASSERT_EVAL_OK(v7, "x=null");
  v7_gc(v7, 0);
#if V7_ENABLE__Memory__stats
  ASSERT_EQ(v7->generic_object_arena.alive, alive);
#endif

  v7_destroy(v7);

  v7 = v7_create();
  v7_gc(v7, 0);
  ASSERT_EVAL_EQ(
      v7,
      "for(i=0;i<9;i++)({});for(i=0;i<7;i++){x=(new Number(1))+({} && 1)};x",
      "2");
  v7_gc(v7, 0);

  v7_destroy(v7);
  return NULL;
}
#endif

static const char *test_gc_own(void) {
  struct v7 *v7 = v7_create();
  val_t v1, v2;
  const char *s;
  size_t len;

  v1 = v7_mk_string(v7, "foobar", 6, 1);
  v2 = v7_mk_string(v7, "barfoo", 6, 1);

  v7_own(v7, &v1);
  v7_own(v7, &v2);

  /*
   * fully gc will shrink the mbuf. given that v2 is the last entry
   * if it were not correctly rooted, it will now lie outside realloced
   * area and ASAN will complain.
   */
  v7_gc(v7, 1);
  s = v7_get_string_data(v7, &v2, &len);
  ASSERT_STREQ(s, "barfoo");

  ASSERT_EQ(v7_disown(v7, &v2), 1);

  v7_gc(v7, 1);
  s = v7_get_string_data(v7, &v1, &len);
  ASSERT_STREQ(s, "foobar");

  ASSERT_EQ(v7_disown(v7, &v2), 0);

  v7_destroy(v7);
  return NULL;
}
#endif

#ifdef V7_ENABLE_FILE
static int check_file(struct v7 *v7, v7_val_t s, const char *file_name) {
  size_t n1, n2;
  char *s1 = read_file(file_name, &n1);
  const char *s2 = v7_get_string_data(v7, &s, &n2);
  int result = n1 == n2 && memcmp(s1, s2, n1) == 0;
  free(s1);
  if (result == 0) {
    printf("want '%.*s' (len %d), got '%.*s' (len %d)\n", (int) n2, s2,
           (int) n2, (int) n1, s1, (int) n1);
  }
  return result;
}

static const char *test_file(void) {
  const char *data = "some test string", *test_file_name = "ft.txt";
  struct v7 *v7 = v7_create();
  v7_val_t v, data_str = v7_mk_string(v7, data, strlen(data), 1);

  v7_own(v7, &data_str);
  v7_set(v7, v7_get_global(v7), "ts", 2, data_str);
  ASSERT(eval(v7,
              "f = File.open('ft.txt', 'w+'); "
              " f.write(ts); f.close();",
              &v) == V7_OK);
  ASSERT(check_file(v7, data_str, test_file_name));
  ASSERT_EQ(remove(test_file_name), 0);
  ASSERT_EVAL_EQ(v7, "File.open('\\0test.mk')", "null");
  ASSERT_EVAL_EQ(v7, "File.open('test.mk', '\\0')", "null");
  ASSERT_EQ(eval(v7, "f = File.open('test.mk'); f.readAll()", &v), V7_OK);
  ASSERT(check_file(v7, v, "test.mk"));
  ASSERT_EVAL_EQ(v7, "File.list('non existent directory')", "undefined");
  ASSERT_EVAL_EQ(v7, "File.list('bad\\0file')", "undefined");
  ASSERT_EQ(eval(v7, "l = File.list('.');", &v), V7_OK);
  ASSERT(v7_is_array(v7, v));
  ASSERT_EVAL_EQ(v7, "l.indexOf('unit_test.c') >= 0", "true");

  v7_disown(v7, &data_str);
  v7_destroy(v7);
  return NULL;
}
#endif

#ifdef V7_ENABLE_SOCKET
static const char *test_socket(void) {
  struct v7 *v7 = v7_create();
  const char *c;

  ASSERT_EVAL_OK(v7, "s1 = Socket.listen(1239, '127.0.0.1'); ");
  ASSERT_EVAL_OK(v7, "s2 = Socket.connect('127.0.0.1', 1239); ");
  ASSERT_EVAL_OK(v7, "s2.send('hi'); ");
  c = "\"hi\"";
  ASSERT_EVAL_EQ(v7, "s3 = s1.accept(); s3.recv();", c);
  ASSERT_EVAL_OK(v7, "s1.close(); s2.close(); s3.close();");

  v7_destroy(v7);
  return NULL;
}
#endif

#ifdef V7_ENABLE_CRYPTO
static const char *test_crypto(void) {
  struct v7 *v7 = v7_create();
  const char *c;

  c = "\"\"";
  ASSERT_EVAL_EQ(v7, "Crypto.base64_encode('');", c);
  c = "\"IA==\"";
  ASSERT_EVAL_EQ(v7, "Crypto.base64_encode(' ');", c);
  c = "\"Oi0p\"";
  ASSERT_EVAL_EQ(v7, "Crypto.base64_encode(':-)');", c);
  c = "\"0beec7b5ea3f0fdbc95d0dd47f3c5bc275da8a33\"";
  ASSERT_EVAL_EQ(v7, "Crypto.sha1_hex('foo');", c);
  c = "\"acbd18db4cc2f85cedef654fccc4a4d8\"";
  ASSERT_EVAL_EQ(v7, "Crypto.md5_hex('foo');", c);

  v7_destroy(v7);
  return NULL;
}
#endif

#ifdef V7_ENABLE_UBJSON
static const char *test_ubjson(void) {
  struct v7 *v7 = v7_create();
  const char *c;

  ASSERT_EVAL_OK(v7, "o = {a:1,b:2}");
  ASSERT_EVAL_OK(v7, "b='';UBJSON.render(o, function(v) {b+=v},function(){})");
  c = "{i\001bi\002i\001ai\001}";
  ASSERT_EVAL_STR_EQ(v7, "b", c);

  ASSERT_EVAL_OK(v7,
                 "var c, o = {x:new UBJSON.Bin(3,function(){"
                 "  c=this;this.send('')"
                 "}),a:1,b:2}");
  ASSERT_EVAL_OK(v7, "b='';UBJSON.render(o, function(v) {b+=v},function(e){})");
  c = "{i\001bi\002i\001ai\001i\001x[$U#i\003";
  ASSERT_EVAL_STR_EQ(v7, "b", c);

  ASSERT_EVAL_OK(v7, "c.send('f')");
  c = "{i\001bi\002i\001ai\001i\001x[$U#i\003f";
  ASSERT_EVAL_STR_EQ(v7, "b", c);

  ASSERT_EVAL_OK(v7, "c.send('oo')");
  c = "{i\001bi\002i\001ai\001i\001x[$U#i\003foo}";
  ASSERT_EVAL_STR_EQ(v7, "b", c);

  v7_destroy(v7);
  return NULL;
}
#endif

#define MK_OP_PUSH_LIT(n) OP_PUSH_LIT, (enum opcode)(n)
#define MK_OP_PUSH_VAR_NAME(n) OP_PUSH_VAR_NAME, (enum opcode)(n)
#define MK_OP_GET_VAR(n) OP_GET_VAR, (enum opcode)(n)
#define MK_OP_SET_VAR(n) OP_SET_VAR, (enum opcode)(n)

static const char *test_exec_generic(void) {
  struct v7 *v7 = v7_create();
  const char *c;

  v7_set(v7, v7_get_global(v7), "ES", ~0, v7_mk_string(v7, "", 0, 0));

  ASSERT_EVAL_NUM_EQ(v7, "0+1", 1);
  ASSERT_EVAL_NUM_EQ(v7, "2+3", 5);
  ASSERT_EVAL_NUM_EQ(v7, "1+2*3", 7);
  ASSERT_EVAL_NUM_EQ(v7, "(1+2)*3", 9);
  c = "\"12\"";
  ASSERT_EVAL_EQ(v7, "1+'2'", c);

  ASSERT_EVAL_NUM_EQ(v7, "x=42", 42);
  ASSERT_EVAL_NUM_EQ(v7, "x", 42);
  ASSERT_EVAL_NUM_EQ(v7, "42+42", 84);
  ASSERT_EVAL_NUM_EQ(v7, "x+x", 84);

  ASSERT_EVAL_OK(v7, "x={a:42}");
  ASSERT_EVAL_NUM_EQ(v7, "x.a", 42);
  ASSERT_EVAL_NUM_EQ(v7, "x['a']", 42);
  ASSERT_EVAL_NUM_EQ(v7, "x.a=0", 0);
  ASSERT_EVAL_NUM_EQ(v7, "x.a", 0);
  ASSERT_EVAL_NUM_EQ(v7, "x.a+=1", 1);
  ASSERT_EVAL_NUM_EQ(v7, "x.a", 1);
  ASSERT_EVAL_NUM_EQ(v7, "x['a']=0", 0);
  ASSERT_EVAL_NUM_EQ(v7, "x['a']", 0);
  ASSERT_EVAL_NUM_EQ(v7, "x['a']+=1", 1);
  ASSERT_EVAL_NUM_EQ(v7, "x['a']", 1);
  ASSERT_EVAL_NUM_EQ(v7, "a={};a[0]=1;a[0]", 1);
  ASSERT_EVAL_NUM_EQ(v7, "a={};a[0]=1;a['0']", 1);

  ASSERT_EVAL_NUM_EQ(v7, "a=0", 0);
  ASSERT_EVAL_NUM_EQ(v7, "a++", 0);
  ASSERT_EVAL_NUM_EQ(v7, "a", 1);
  ASSERT_EVAL_NUM_EQ(v7, "++a", 2);

  ASSERT_EVAL_NUM_EQ(v7, "x.a=0", 0);
  ASSERT_EVAL_NUM_EQ(v7, "x.a++", 0);
  ASSERT_EVAL_NUM_EQ(v7, "x.a", 1);
  ASSERT_EVAL_NUM_EQ(v7, "++x.a", 2);

  ASSERT_EVAL_NUM_EQ(v7, "x['a']=0", 0);
  ASSERT_EVAL_NUM_EQ(v7, "x['a']++", 0);
  ASSERT_EVAL_NUM_EQ(v7, "x['a']", 1);
  ASSERT_EVAL_NUM_EQ(v7, "++x['a']", 2);

  ASSERT_EVAL_NUM_EQ(v7, "1,2,3", 3);
  ASSERT_EVAL_NUM_EQ(v7, "a=0;b=40; a++,b++,a+b", 42);

  ASSERT_EVAL_NUM_EQ(v7, "if(true) 1; else 2", 1);
  ASSERT_EVAL_NUM_EQ(v7, "if(false) 1; else 2", 2);
  ASSERT_EVAL_NUM_EQ(v7, "if(true) 1", 1);
  ASSERT_EVAL_EQ(v7, "if(false) 1", "undefined");
  ASSERT_EVAL_NUM_EQ(v7, "1; if(false) {}", 1);
  ASSERT_EVAL_NUM_EQ(v7, "1; if(true) {}", 1);
  ASSERT_EVAL_NUM_EQ(v7, "1; if(true) {2}", 2);
  ASSERT_EVAL_NUM_EQ(v7, "1; if(false) {2}", 1);
  ASSERT_EVAL_NUM_EQ(v7, "1; if(false) {2} else {}", 1);
  ASSERT_EVAL_NUM_EQ(v7, "1; if(false) {2} else {3}", 3);
  ASSERT_EVAL_NUM_EQ(v7, "1; if(true) {} else {3}", 1);

  ASSERT_EVAL_EQ(v7, "while(0) 1", "undefined");
  ASSERT_EVAL_EQ(v7, "while(1) break", "undefined");
  ASSERT_EVAL_NUM_EQ(v7, "a=1;while(a) a-=1", 0);
  ASSERT_EVAL_NUM_EQ(v7, "a=3;b=0;while(a) {a-=1;b+=1}", 3);
  ASSERT_EVAL_NUM_EQ(v7, "a=3;b=0;while(a) {a-=1;b+=1};b", 3);
  ASSERT_EVAL_NUM_EQ(v7, "1; while(false) {}", 1);
  ASSERT_EVAL_NUM_EQ(v7, "1; while(true) break", 1);
  ASSERT_EVAL_NUM_EQ(v7, "1; while(true) break; 2", 2);
  ASSERT_EVAL_NUM_EQ(v7, "a=4;while(a) {if(a<2) break; a--;}", 2);
  ASSERT_EVAL_NUM_EQ(v7, "a=4; while(a) {a--; if(a<2) continue; }", 1);
  ASSERT_EVAL_STR_EQ(
      v7, "b=''; a=4; while(a) {a--; if(a<2) continue; b+='c-'};b", "c-c-");

  ASSERT_EVAL_NUM_EQ(v7, "1; do {break;} while(true)", 1);
  ASSERT_EVAL_NUM_EQ(v7, "1; do {break;} while(true); 2", 2);
  ASSERT_EVAL_NUM_EQ(v7, "a=4;do {if(a<2) break; a--;} while(a)", 2);
  ASSERT_EVAL_NUM_EQ(v7, "do {42; continue; 24} while(false);", 42);
  ASSERT_EVAL_STR_EQ(
      v7, "b=''; a=4; do {a--; if(a<2) continue; b+='c-'} while(a); b", "c-c-");

  ASSERT_EVAL_NUM_EQ(v7, "b=0;for(i=0;i<10;i+=1) b+=1", 10);
  ASSERT_EVAL_EQ(v7, "for(1;false;1) 1", "undefined");
  ASSERT_EVAL_EQ(v7, "for(1;false;) 1", "undefined");
  ASSERT_EVAL_NUM_EQ(v7, "1; for(;false;) {}", 1);
  ASSERT_EVAL_NUM_EQ(v7, "1; for(;true;) break", 1);
  ASSERT_EVAL_NUM_EQ(v7, "1; for(;true;) break; 2", 2);
  ASSERT_EVAL_NUM_EQ(v7, "b=0;for(i=0;i<10;i+=1) {if(i<5) continue; b+=1}", 5);

  ASSERT_EVAL_NUM_EQ(v7, "o={a:40,b:2}; r=0; for(i in o) r+=o[i]; r", 42);
  ASSERT_EVAL_NUM_EQ(v7, "42; for(i in {}) 0", 42);
  ASSERT_EVAL_NUM_EQ(v7, "0; for(i in {a:1}) 42", 42);
  ASSERT_EVAL_STR_EQ(v7, "for(i in {a:1}) i", "a");
  ASSERT_EVAL_NUM_EQ(v7, "42; for(i in {a:1}) {}", 42);
  ASSERT_EVAL_JS_EXPR_EQ(v7, "for(i in {a:1}){break}", "undefined");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "1;for(i in {a:1}){break}", "1");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "1;for(i in {a:1}){2;break}", "2");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "for(i in {a:1}){continue}", "undefined");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "1;for(i in {a:1}){continue}", "1");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "1;for(i in {a:1}){2;continue}", "2");
/* TODO(dfrank) fix stack usage when `break` is used inside `for .. in` */
#if 0
  ASSERT_EVAL_NUM_EQ(v7, "42; for(i in {a:1}) break", 42);
  ASSERT_EVAL_NUM_EQ(v7, "42; for(i in {a:1}) break; 2", 2);
  ASSERT_EVAL_NUM_EQ(
      v7, "n=0; for(i in {a:1,b:2,c:3,d:4}) {if(n>2) break; n++}", 2);
  ASSERT_EVAL_NUM_EQ(
      v7, "n=0; for(i in {a:1,b:2,c:3,d:4}) {n++; if(n<2) continue}", 3);
#endif

/*
 * TODO(dfrank) uncomment it when we switch to bcode completely.
 * Now we can't since `v7_get` throws via longjmp
 */
#if 0
  ASSERT_EVAL_ERR(v7, "var u=undefined; u.b", V7_EXEC_EXCEPTION);
#endif

  /* clang-format off */
  ASSERT_EVAL_NUM_EQ(v7, STRINGIFY(
                           3;
                           function ob(){
                             r={};
                             for (i in {}){
                               r[i]=5;
                             }
                             return r;
                           }
                           for (var i in ob()) {
                           }), 3);
  /* clang-format on */

  ASSERT_EVAL_NUM_EQ(v7, "2; do {1} while(false);", 1);

  ASSERT_EVAL_EQ(v7, "!0", "true");
  ASSERT_EVAL_NUM_EQ(v7, "~0", -1);
  ASSERT_EVAL_EQ(v7, "!false", "true");
  ASSERT_EVAL_EQ(v7, "!''", "true");
  ASSERT_EVAL_EQ(v7, "!'abc'", "false");
  ASSERT_EVAL_EQ(v7, "!123", "false");

  ASSERT_EVAL_NUM_EQ(v7, "0&&1", 0);
  ASSERT_EVAL_NUM_EQ(v7, "1&&0", 0);
  ASSERT_EVAL_NUM_EQ(v7, "1&&1", 1);
  ASSERT_EVAL_NUM_EQ(v7, "1&&2", 2);
  ASSERT_EVAL_EQ(v7, "false&&1", "false");
  ASSERT_EVAL_EQ(v7, "1&&false", "false");

  ASSERT_EVAL_NUM_EQ(v7, "0||1", 1);
  ASSERT_EVAL_NUM_EQ(v7, "1||0", 1);
  ASSERT_EVAL_NUM_EQ(v7, "1||2", 1);
  ASSERT_EVAL_NUM_EQ(v7, "false||2", 2);
  ASSERT_EVAL_EQ(v7, "0||false", "false");

  ASSERT_EVAL_NUM_EQ(v7, "true ? 1 : 2", 1);
  ASSERT_EVAL_NUM_EQ(v7, "false ? 1 : 2", 2);
  ASSERT_EVAL_NUM_EQ(v7, "true ? true ? 1 : 2 : 3", 1);
  ASSERT_EVAL_NUM_EQ(v7, "true ? false ? 1 : 2 : 3", 2);
  ASSERT_EVAL_NUM_EQ(v7, "false ? true ? 1 : 2 : 3", 3);
  ASSERT_EVAL_NUM_EQ(v7, "false ? false ? 1 : 2 : 3", 3);

  ASSERT_EVAL_NUM_EQ(v7, "x={a:1,b:2};x.a+x.b", 3);

  ASSERT_EVAL_NUM_EQ(v7, "a=[42];a[0]", 42);
  ASSERT_EVAL_NUM_EQ(v7, "a=[41,42];a[1]", 42);
  ASSERT_EVAL_NUM_EQ(v7, "a=[41,,42];a[2]", 42);

  ASSERT_EVAL_EQ(v7, "void (1+2)", "undefined");
  ASSERT_EVAL_NUM_EQ(v7, "x=42;this.x", 42);

  ASSERT_EVAL_EQ(v7, "x={};'a' in x", "false");
  ASSERT_EVAL_EQ(v7, "x={a:1};'a' in x", "true");
  ASSERT_EVAL_EQ(v7, "x={a:undefined};'a' in x", "true");

  ASSERT_EVAL_EQ(v7, "Number instanceof Object", "true");
  ASSERT_EVAL_EQ(v7, "Number instanceof Function", "true");
  ASSERT_EVAL_EQ(v7, "Object instanceof Number", "false");

  ASSERT_EVAL_STR_EQ(v7, "typeof 1", "number");
  ASSERT_EVAL_STR_EQ(v7, "typeof null", "object");
  ASSERT_EVAL_STR_EQ(v7, "typeof {}", "object");
  ASSERT_EVAL_STR_EQ(v7, "typeof 'foox'", "string");
  ASSERT_EVAL_STR_EQ(v7, "typeof undefined", "undefined");
  ASSERT_EVAL_STR_EQ(v7, "typeof novar", "undefined");
  ASSERT_EVAL_STR_EQ(v7, "function a(){}; typeof a", "function");
  ASSERT_EVAL_STR_EQ(v7, "function a(){}; typeof a()", "undefined");
  ASSERT_EVAL_STR_EQ(v7, "var a = 1; typeof a", "number");

  ASSERT_EVAL_NUM_EQ(v7, "Object.keys({a:1,b:2}).length", 2);

  ASSERT_EVAL_NUM_EQ(v7, "var x=2; 2", 2);
  ASSERT_EVAL_EQ(v7, "(function(){})()", "undefined");
  ASSERT_EVAL_EQ(v7, "(function(a){a*2})(21)", "undefined");
  ASSERT_EVAL_NUM_EQ(v7, "(function(a){return a*2})(21)", 42);
  ASSERT_EVAL_NUM_EQ(v7, "b=1;(function(a){var b = 2; return a+b})(39)+b", 42);
  ASSERT_EVAL_NUM_EQ(v7, "(function(){var b = 2; return b})()+40", 42);

  ASSERT_EVAL_NUM_EQ(
      v7, "a=1; (function(a) {return function(){return a}})(42)()", 42);

  /* clang-format off */

  /* for loop with var declaration containing more than one variable */
  ASSERT_EVAL_JS_EXPR_EQ(v7, "for(var i=0,a=10;i<10;i++){a+=i};a", "55");

  /* var and function declarations should be stack-neutral {{{ */

  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_JS_EXPR_EQ(v7,
      "var a = 5;",
      "undefined");

  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_JS_EXPR_EQ(v7,
      "1; var a = 5;",
      "1");

  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_JS_EXPR_EQ(v7,
      "a; var a = 5;",
      "undefined");

  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_JS_EXPR_EQ(v7,
      "1; a; var a = 5;",
      "undefined");

  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_JS_EXPR_EQ(v7,
      "1; var a = 5; a",
      "5");

  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_JS_EXPR_EQ(v7,
      "1; a = 5;",
      "5");

  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_JS_EXPR_EQ(v7,
      "function a(){};",
      "undefined");

  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_JS_EXPR_EQ(v7,
      "var a = function a(){};",
      "undefined");

  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_JS_EXPR_EQ(v7,
      "1; function a(){};",
      "1");

  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_JS_EXPR_EQ(v7,
      "1; var a = function a(){};",
      "1");

  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_JS_EXPR_EQ(v7,
      "1; a = function a(){};",
      "function a(){}; a;");

  /* }}} */

  /* exceptions {{{ */

  ASSERT_EVAL_JS_EXPR_EQ(v7, "try{} finally{}", "undefined");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "1;try{} finally{}", "1");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "1;try{2} finally{}", "2");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "1;try{2} finally{3}", "3");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "try{foo} catch(e){}", "undefined");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "try{1+foo} catch(e){}", "undefined");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "2;try{1+foo} catch(e){}", "2");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "2;try{3;1+foo} catch(e){}", "3");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "1;try{foo} catch(e){}", "1");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "1;try{2;foo} catch(e){}", "2");
  ASSERT_EVAL_JS_EXPR_EQ(v7,
      "var f=(function(){foo});"
      "2;try{f();} catch(e){}",
      "2");
  ASSERT_EVAL_JS_EXPR_EQ(v7,
      "var f=(function(){foo});"
      "2;try{3;f();} catch(e){}",
      "3");
  ASSERT_EVAL_JS_EXPR_EQ(v7,
      "var f=(function(){foo});"
      "2;try{3;f();} catch(e){} finally{}",
      "3");

  /* plain try{} is a syntax error */
  ASSERT_EVAL_ERR(
      v7, "try{}", V7_SYNTAX_ERROR
      );

  ASSERT_EVAL_NUM_EQ(
      v7, STRINGIFY(
          var a;
          try       { a = 10; }
          catch (e) { a = 15; }
          finally   { a = 20; }
          a
        ), 20
      );

  ASSERT_EVAL_STR_EQ(
      v7, STRINGIFY(
        var a;
        try       { a = 'a-'; throw 'test'; }
        catch (e) { a = a + 'b-'; }
        finally   { a = a + 'c-'; }
        a
        ), "a-b-c-"
      );

  ASSERT_EVAL_STR_EQ(
      v7, STRINGIFY(
        var a;
        try       { a = 'a-'; throw 'test'; }
        catch (e) { a = a + e; }
        a
        ), "a-test"
      );

  /* try-catch with empty `catch` body should evaluate to `undefined` */
  ASSERT_EVAL_JS_EXPR_EQ(
      v7, STRINGIFY(
        try       { foo }
        catch (e) { }
        ), "undefined"
      );

  ASSERT_EVAL_STR_EQ(
      v7, STRINGIFY(
        var f1 = function(){
          var a = '1-';
          try {
            try { a += '2-' }
            catch (e) { a += e + '-3-'; }
            finally { a += '4-'; }
          }
          catch (e) { a += e + '-5-'; }
          finally { a += '6-'; }

          return a;
        };
        f1();
        ), "1-2-4-6-"
      );

  /* TODO(dfrank): avoid depending on exact error message */
  ASSERT_EVAL_STR_EQ(
      v7, STRINGIFY(
        var a = ES;

        var b = (function() {
          try {foo}
          catch(e) { a += 'GOT:' + e + ';'; }
          finally { a += 'FINALLY;'; return 42 };
          return 10;
        }
        )();

        var c = a + '|' + b;
        c;
        ), "GOT:Error: [foo] is not defined;FINALLY;|42"
      );

  ASSERT_EVAL_STR_EQ(
      v7, STRINGIFY(
        var f1 = function(acc, val){
          if (val == 0) {
            throw acc;
          }
          f1(acc + '.', val - 1);
        };

        var a;
        try {
          f1(ES, 10);
        } catch (e) {
          a = e;
        }
        ), ".........."
      );

  /*
   * should take first return from `try`, and ignore last return outside of
   * `try`
   */
  ASSERT_EVAL_STR_EQ(
      v7, STRINGIFY(
        var a = '1-';

        var f1 = function(){
          var b = '1-';
          try {
            try { a += '2-';
                  b += '2-';
                  return b + '_1'; }
            catch (e) { a += e + '-3-';
                        b += e + '-3-'; }
            finally { a += '4-';
                      b += '4-'; }
          }
          catch (e) { a += e + '-5-';
                      b += e + '-5-'; }
          finally { a += '6-';
                    b += '6-'; }

          return b + '_2';
        };

        var c = f1() + '|' + a;
        c;
        ), "1-2-_1|1-2-4-6-"
      );

  /*
   * should take second return from `finally`, and ignore last return outside of
   * `try`
   */
  ASSERT_EVAL_STR_EQ(
      v7, STRINGIFY(
        var a = '1-';

        var f1 = function(){
          var b = '1-';
          try {
            try { a += '2-';
                  b += '2-';
                  return b + '_1'; }
            catch (e) { a += e + '-3-';
                        b += e + '-3-'; }
            finally { a += '4-';
                      b += '4-';
                      return b + '_2'; }
          }
          catch (e) { a += e + '-5-';
                      b += e + '-5-'; }
          finally { a += '6-';
                    b += '6-'; }

          return b + '_3';
        };

        var c = f1() + '|' + a;
        c;
        ), "1-2-4-_2|1-2-4-6-"
      );

  /*
   * should take third return from `finally`, and ignore last return outside of
   * `try`
   */
  ASSERT_EVAL_STR_EQ(
      v7, STRINGIFY(
        var a = '1-';

        var f1 = function(){
          var b = '1-';
          try {
            try { a += '2-';
                  b += '2-';
                  return b + '_1'; }
            catch (e) { a += e + '-3-';
                        b += e + '-3-'; }
            finally { a += '4-';
                      b += '4-';
                      return b + '_2'; }
          }
          catch (e) { a += e + '-5-';
                      b += e + '-5-'; }
          finally { a += '6-';
                    b += '6-';
                    return b + '_3'; }

          return b + '_4';
        };

        var c = f1() + '|' + a;
        c;
        ), "1-2-4-6-_3|1-2-4-6-"
      );

  /*
   * should take third return from `finally`, and ignore last return outside of
   * `try`
   */
  ASSERT_EVAL_STR_EQ(
      v7, STRINGIFY(
        var a = '1-';

        var f1 = function(){
          var b = '1-';
          try {
            try { a += '2-';
                  b += '2-';
                  return b + '_1'; }
            catch (e) { a += e + '-3-';
                        b += e + '-3-'; }
            finally { a += '4-';
                      b += '4-';
                      return b + f2() + '_2'; }
          }
          catch (e) { a += e + '-5-';
                      b += e + '-5-'; }
          finally { a += '6-';
                    b += '6-'; }

          return b + '_4';
        };

        var f2 = function(){
          var b = 'a-';
          try {
            try { a += 'b-';
                  b += 'b-';
                  return b + '_1'; }
            catch (e) { a += e + '-c-';
                        b += e + '-c-'; }
            finally { a += 'd-';
                      b += 'd-';
                      return b + '_2'; }
          }
          catch (e) { a += e + '-e-';
                      b += e + '-e-'; }
          finally { a += 'f-';
                    b += 'f-';
                    return b + '_3'; }
        };

        var c = f1() + '|' + a;
        c;
        ), "1-2-4-a-b-d-f-_3_2|1-2-4-b-d-f-6-"
      );


  /*
   * Throw should dismiss any pending returns
   */
  ASSERT_EVAL_STR_EQ(
      v7, STRINGIFY(
        var f1 = function(){
          var b = '1-';
          try {
            b += '2-';
            try       { b += '3-'; return b + '-ret1'; }
            catch (e) { b += '4-'; }
            finally   { b += '5-'; throw 'test'; }
          }
          catch (e) { b += e + '-'; }
          finally { b += '6-'; }
          return b + '-ret2';
        };

        var c = f1();
        c;
        ), "1-2-3-5-test-6--ret2"
      );

  /*
   * Return should dismiss any pending thrown values
   */
  ASSERT_EVAL_STR_EQ(
      v7, STRINGIFY(
        var f1 = function(){
          var b = '1-';
          try {
            b += '2-';
            try       { b += '3-'; throw 'test'; }
            finally   { b += '5-'; return b + '-ret1'; }
          }
          catch (e) { b += e + '-'; }
          finally { b += '6-'; }
          return b + '-ret2';
        };

        var c = f1();
        c
        ), "1-2-3-5--ret1"
      );

  ASSERT_EVAL_STR_EQ(
      v7, STRINGIFY(
        var a = '1-';

        var f1 = function(){
          var b = '1-';
          try {
            try { a += '2-';
                  b += '2-';
                  return b + '_1'; }
            catch (e) { a += e + '-3-';
                        b += e + '-3-'; }
            finally { a += '4-';
                      b += '4-';
                      return b + f2() + '_2'; }
          }
          catch (e) { a += e + '-5-';
                      b += e + '-5-'; }
          finally { a += '6-';
                    b += '6-'; }

          return b + '_4';
        };

        var f2 = function(){
          var b = 'a-';
          try {
            try { a += 'b-';
                  b += 'b-';
                  return b + '_1'; }
            catch (e) { a += e + '-c-';
                        b += e + '-c-'; }
            finally { a += 'd-';
                      b += 'd-';
                      throw '-test-'; }
          }
          catch (e) { a += e + '-e-';
                      b += e + '-e-'; }
          finally { a += 'f-';
                    b += 'f-';
                    return b + '_3'; }
        };

        var c = f1() + '|' + a;
        c;
        ), "1-2-4-a-b-d--test--e-f-_3_2|1-2-4-b-d--test--e-f-6-"
      );

  /* exception in comma expression should abort the whole expression */
  ASSERT_EVAL_NUM_EQ(v7, "42; try { 1,2,b } catch(e) {}", 42);
  ASSERT_EVAL_NUM_EQ(v7, "66; try { 42; 1,2,b } catch(e) {}", 42);
  ASSERT_EVAL_NUM_EQ(v7, "42; try { 1,2,3 } catch(e) {}", 3);

  /* }}} */

  /* use strict {{{ */

  /* duplicate properties in object literal {{{ */

  ASSERT_EVAL_ERR(
      v7, "'use strict'; var a = {b:1, c:2, b:3}; a.b", V7_SYNTAX_ERROR
      );

  ASSERT_EVAL_NUM_EQ(
      v7, "var a = {b:1, c:2, b:3}; a.b", 3
      );

  ASSERT_EVAL_ERR(
      v7, STRINGIFY(
        var a = (function(){
          'use strict';
          return {b:1, c:2, b:3};
        });
        a
        ), V7_SYNTAX_ERROR
      );

  ASSERT_EVAL_ERR(
      v7, STRINGIFY(
        'use strict';
        var a = (function(){
          return {b:1, c:2, b:3};
        });
        a
        ), V7_SYNTAX_ERROR
      );

  ASSERT_EVAL_ERR(
      v7, STRINGIFY(
        'use strict';
        var a = (function(){
          return {b:1, c:2};
        });
        var b = (function(){
          return {b:1, c:2, b:3};
        });
        a
        ), V7_SYNTAX_ERROR
      );

  ASSERT_EVAL_ERR(
      v7, STRINGIFY(
        var a = (function(){
          'use strict';
          return {b:1, c:2};
        });
        var b = (function(){
          'use strict';
          return {b:1, c:2, b:3};
        });
        b();
        ), V7_SYNTAX_ERROR
      );

  ASSERT_EVAL_NUM_EQ(
      v7, STRINGIFY(
        var a = (function(){
          'use strict';
          return {b:1, c:2};
          });
        var b = (function(){
          return {b:1, c:2, b:3};
          });
        b().b;
        ), 3
      );

  /* }}} */

  /* switch: fallthrough {{{ */

  ASSERT_EVAL_NUM_EQ(
      v7, STRINGIFY(
          switch(0) {
            case 1:
              1;
            default:
              2;
          }
        ), 2
      );

  ASSERT_EVAL_NUM_EQ(
      v7, STRINGIFY(
          x = 0;
          switch(0) {
            default:
              x = 2;
            case 1:
              x;
          }
        ), 2
      );

  ASSERT_EVAL_NUM_EQ(
      v7, STRINGIFY(
          x=0;
          switch(1) {
            default:
              x=2;
            case 1:
              x;
          }
        ), 0
      );

  ASSERT_EVAL_NUM_EQ(
      v7, STRINGIFY(
          42;
          switch(0) {
            default:
            case 2:
          }
        ), 42
      );

  ASSERT_EVAL_NUM_EQ(
      v7, STRINGIFY(
          42;
          switch(0) {
            case 1:
              1;
            case 2:
              2;
          }
        ), 42
      );

  ASSERT_EVAL_NUM_EQ(
      v7, STRINGIFY(
          switch(2) {
            case 1:
              1;
            case 2:
              2;
          }
        ), 2
      );

  ASSERT_EVAL_NUM_EQ(
      v7, STRINGIFY(
          switch(1) {
            case 1:
              1;
            case 2:
              2;
          }
        ), 2
      );

  ASSERT_EVAL_STR_EQ(
      v7, STRINGIFY(
        var s1 = ES;
        var s2 = ES;

        var f=function(v){
          s1 += v;
          return v;
        };

        switch (f('2')){
          case f('0'):
            s2 += '0';
          case f('1'):
            s2 += '1';
          case f('2'):
            s2 += '2';
          case f('3'):
            s2 += '3';
          case f('4'):
            s2 += '4';
        }

        var res = s1 + ':' + s2;
        res;
        ), "2012:234"
      );

  /* }}} */

  /* undefined variables {{{ */

  /*
   * TODO(dfrank): invent some more centralized way of cleaning up the
   * Global Object than invoking `v7_del()` all the time
   */

  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_NUM_EQ(
      v7, "a = 1;", 1
      );

  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_ERR(
      v7, "'use strict'; a = 1;", V7_EXEC_EXCEPTION
      );

  v7_del(v7, v7->vals.global_object, "b", 1);
  ASSERT_EVAL_NUM_EQ(
      v7, "b=1;(function(a){'use strict'; var b = 2; return a+b})(39)+b",
      42);

  v7_del(v7, v7->vals.global_object, "b", 1);
  ASSERT_EVAL_NUM_EQ(
      v7, "b=1;(function(a){'use strict'; b = 2; return a+b})(39)+b",
      43);

  v7_del(v7, v7->vals.global_object, "b", 1);
  ASSERT_EVAL_NUM_EQ(
      v7, "b=1;(function(a){var b = 2; return a+b})(39)+b",
      42);

  v7_del(v7, v7->vals.global_object, "b", 1);
  ASSERT_EVAL_NUM_EQ(
      v7, "b=1;(function(a){b = 2; return a+b})(39)+b",
      43);

  v7_del(v7, v7->vals.global_object, "b", 1);
  ASSERT_EVAL_ERR(
      v7, "'use strict'; b=1;(function(a){b = 2; return a+b})(39)+b",
      V7_EXEC_EXCEPTION);

  v7_del(v7, v7->vals.global_object, "b", 1);
  v7_del(v7, v7->vals.global_object, "c", 1);
  ASSERT_EVAL_ERR(
      v7, "'use strict'; var b=1;(function(a){c = 2; return a+b})(39)+b",
      V7_EXEC_EXCEPTION);

  v7_del(v7, v7->vals.global_object, "b", 1);
  v7_del(v7, v7->vals.global_object, "c", 1);
  ASSERT_EVAL_STR_EQ(
      v7, "b='1-';(function(a){c = '2-'; return a+b+c})('39-')+b",
      "39-1-2-1-");

  v7_del(v7, v7->vals.global_object, "b", 1);
  v7_del(v7, v7->vals.global_object, "c", 1);
  ASSERT_EVAL_ERR(
      v7, "b='1-';(function(a){'use strict'; c = '2-'; return a+b+c})('39-')+b",
      V7_EXEC_EXCEPTION);

  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_STR_EQ(
      v7, STRINGIFY(
        a = '1-';

        f1 = (function(){
          'use strict';
          var b = '2-';
          a += b;
          });

        var f2 = (function(){
          c = '3-';
          a += c;
          });

        f1();
        f2();
        a;
        ), "1-2-3-"
      );


  /* }}} */

  /* }}} */

  /*
   * TODO(dfrank): uncomment when `eval`-as-an-operator is implemented
   */
#if 0
  /* eval {{{ */

  {
    const char *src;

    src = STRINGIFY(
        var x="1-";
        (function() {var x = "2-"; eval("var x='3-';"); return x})() + x;
        );
    ASSERT_EVAL_STR_EQ(v7, src, "3-1-");

    src = STRINGIFY(
        "use strict";
        var x="1-";
        (function() {var x = "2-"; eval("var x='3-';"); return x})() + x;
        );
    ASSERT_EVAL_STR_EQ(v7, src, "2-1-");

    src = STRINGIFY(
        var x="1-";
        (function() {
         "use strict";
         var x = "2-"; eval("var x='3-';"); return x
        })() + x;
        );
    ASSERT_EVAL_STR_EQ(v7, src, "2-1-");

    src = STRINGIFY(
        var x="1-";
        (function() {
         var x = "2-"; eval("'use strict'; var x='3-';"); return x
         })() + x;
        );
    ASSERT_EVAL_STR_EQ(v7, src, "2-1-");

    src = STRINGIFY(
        var x="1-";
        (function() {var x = "2-"; eval("x='3-';"); return x})() + x;
        );
    ASSERT_EVAL_STR_EQ(v7, src, "3-1-");

    src = STRINGIFY(
        "use strict";
        var x="1-";
        (function() {var x = "2-"; eval("x='3-';"); return x})() + x;
        );
    ASSERT_EVAL_STR_EQ(v7, src, "3-1-");

    src = STRINGIFY(
        var x="1-";
        (function() {
         "use strict";
         var x = "2-"; eval("x='3-';"); return x
         })() + x;
        );
    ASSERT_EVAL_STR_EQ(v7, src, "3-1-");

    src = STRINGIFY(
        var x="1-";
        (function() {
         var x = "2-"; eval("'use strict'; x='3-';"); return x
         })() + x;
        );
    ASSERT_EVAL_STR_EQ(v7, src, "3-1-");

  }

  /* }}} */
#endif

  /* `this` {{{ */

  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_JS_EXPR_EQ(v7,
      "a = 1; (function(){ return this.a + 1; })(); ",
      "2");

  ASSERT_EVAL_JS_EXPR_EQ(v7,
      "'use strict'; (function(){ return this })(); ",
      "undefined");

  ASSERT_EVAL_JS_EXPR_EQ(v7,
      "(function(){ 'use strict'; return this })(); ",
      "undefined");

  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_JS_EXPR_EQ(v7,
      "var a={i:1, f:function(a){return this.i+a;}}; a.f(2)",
      "3");

  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_JS_EXPR_EQ(v7,
      "var a={i:1, f:function(a){return this.i+a;}}; a['f'](2)",
      "3");

  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_JS_EXPR_EQ(v7,
      "var a={i:1, f:function(){this.i++;}}; a.f(); a.i",
      "2");

  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_JS_EXPR_EQ(v7,
      "var a={i:1, f:function(){this.i++;}}; a['f'](); a.i",
      "2");

  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_JS_EXPR_EQ(v7,
      "i=10; a={i:1, f:function(){this.i++;}}; f=a.f; f(); i",
      "11");

  ASSERT_EVAL_STR_EQ(
      v7, STRINGIFY(
        var a = ES;

        function f1() {
          a += 'f1-';
          return {f2: f2};
        }

        function f2() {
          a += 'f2-';
          return 2;
        }

        function f3() {
          a += 'f3-';
          return 3;
        }

        function f4() {
          a += 'f4-';
          return 4;
        }

        f1().f2(f3(), f4());
        a;
        ), "f1-f3-f4-f2-"
      );

  /* }}} */

  /* function should be able to return itself */
  ASSERT_EVAL_JS_EXPR_EQ(
      v7, "(function foo() {return foo})()", "(function foo() {return foo})"
      );

  ASSERT_EVAL_JS_EXPR_EQ(
      v7, "function foo() {return foo}; foo();", "(function foo() {return foo})"
      );

  /* function are hoisted */
  ASSERT_EVAL_JS_EXPR_EQ(
      v7, STRINGIFY(
        var a = 1 + foo();
        function foo(){ return bar() * 2 };
        function bar(){ return 5; };
        a;
        ), "11"
      );

  ASSERT_EVAL_JS_EXPR_EQ(
      v7, STRINGIFY(
        var a = 1 + foo();
        function foo(){
          return bar() * 2;
          function bar(){ return 5; };
        };
        a;
        ), "11"
      );

  ASSERT_EVAL_ERR(
      v7, STRINGIFY(
        var a = 1 + foo();
        var foo = function foo(){
          return 2;
        };
        ), V7_EXEC_EXCEPTION
      );

  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_JS_EXPR_EQ(v7,
      "a; function a(){};",
      "function a(){}; a;");


  /* check several `var`s */
  ASSERT_EVAL_JS_EXPR_EQ(
      v7, STRINGIFY(
        var a;
        (function(){
          a = foo();
          function foo(){
            var a = 1, b;

            var c = 3;
            b = 2;

            return a + c + b;
          };
        })();
        a;
        ), "6"
      );

  /* should be able to call cfunction `print` */
  ASSERT_EVAL_ERR(
      v7, "print('foo');", V7_OK
      );

  /* switch: break {{{ */

  ASSERT_EVAL_NUM_EQ(
      v7, STRINGIFY(
          switch(1) {
            case 1:
              1;
              break;
            case 2:
              2;
              break;
          }
        ), 1
      );

  ASSERT_EVAL_NUM_EQ(
      v7, STRINGIFY(
          try {
            switch(1) {
              case 1:
                nonExisting;
              case 2:
                2;
            }
          } catch(e) {
            42
          }
        ), 42
      );

  ASSERT_EVAL_STR_EQ(
      v7, STRINGIFY(
          x=ES;
          switch(1) {
            case 1:
              try {
                x+='1-';
                break;
              } finally {
                x+='f-';
              }
            case 2:
              x+='2-';
          }
          x
        ), "1-f-"
      );

  ASSERT_EVAL_STR_EQ(
      v7, STRINGIFY(
          x=ES;
          switch(1) {
            case 1:
              try {
                x+='1-';
                switch(20) {
                  case 10:
                    x+='10-';
                    break;
                  case 20:
                    x+='20-';
                    break;
                }
                break;
              } finally {
                x+='f-';
              }
            case 2:
              x+='2-';
          }
          x
        ), "1-20-f-"
      );

  ASSERT_EVAL_STR_EQ(
      v7, STRINGIFY(
          x=ES;
          switch(1) {
            case 1:
              try {
                try {
                  x+='1-';
                  break;
                } finally {
                  x+='f1-';
                }
              } finally {
                x+='f2-';
              }
            case 2:
              x+='2-';
          }
          x
        ), "1-f1-f2-"
      );

  ASSERT_EVAL_STR_EQ(
      v7, STRINGIFY(
          x=ES;
          switch(1) {
            case 1:
              while(true) {
                x+='1-';
                break;
              }
              /* fallthrough */
            case 2:
              x+='2-';
          }
          x
        ), "1-2-"
      );

  ASSERT_EVAL_STR_EQ(
      v7, STRINGIFY(
          x=ES;
          for(i=0; i<2; i++) {
            switch(i) {
              case 0:
                x+='0-';
                continue;
              case 1:
                x+='1-';
            }
            x+='f-';
          }
          x
        ), "0-1-f-"
      );

  ASSERT_EVAL_JS_EXPR_EQ(
      v7, STRINGIFY(
          var i = 0;
          var a = 0;
          for (i = 0; i < 10; i++) {
            try {
              continue;
              a = 500;
            } finally {
              a += 10;
            }
            a = 500;
          }
          a
        ), "100"
      );

  ASSERT_EVAL_JS_EXPR_EQ(
      v7, STRINGIFY(
          var i = 0;
          var a = 0;
          do {
            i++;
            a += 2;
            continue;
            a = 500;
          } while (i < 10);
          a
        ), "20"
      )
      ;
  /* }}} */

  /* constructor {{{ */

  ASSERT_EVAL_JS_EXPR_EQ(
      v7, "function A(){this.p=1;} var a = new A(); a.p",
      "1"
      );

  ASSERT_EVAL_JS_EXPR_EQ(
      v7, "function A(){this.p=1; return {p:2};} var a = new A(); a.p",
      "2"
      );

  ASSERT_EVAL_JS_EXPR_EQ(
      v7, "function A(){this.p=1; return null;} var a = new A(); a.p",
      "1"
      );

  ASSERT_EVAL_JS_EXPR_EQ(
      v7, "function A(){this.p=1; return undefined;} var a = new A(); a.p",
      "1"
      );

  ASSERT_EVAL_JS_EXPR_EQ(
      v7, "function A(){this.p=1; return 10;} var a = new A(); a.p",
      "1"
      );

  ASSERT_EVAL_JS_EXPR_EQ(
      v7, STRINGIFY(
        function A(){
          this.p = '1';
        }
        A.prototype.test = '2';
        var a = new A();
        a.p + '-' + a.test;
        ),
        "'1-2'"
      );

  /* }}} */

  /* delete {{{ */

  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_JS_EXPR_EQ(
      v7, "var a = 1; var res = delete a; res + '|' + a;",
      "'false|1'"
      );

  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_JS_EXPR_EQ( v7, "a = 1; var res = delete a; res;", "true");

  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_ERR( v7, "a = 1; delete a; a;", V7_EXEC_EXCEPTION);

  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_ERR( v7, "'use strict'; a = 5; delete a;", V7_SYNTAX_ERROR);

  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_ERR( v7, "'use strict'; var a = 5; delete a;", V7_SYNTAX_ERROR);

  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_JS_EXPR_EQ( v7, "'use strict'; var a = {p:1}; delete a.p;", "true");

  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_JS_EXPR_EQ(
      v7, "'use strict'; var a = {p:1}; var res = delete a.p; res + '|' + a.p",
      "'true|undefined'"
      );

  ASSERT_EVAL_JS_EXPR_EQ( v7, "delete 1", "true");
  ASSERT_EVAL_JS_EXPR_EQ( v7, "delete 'foo'", "true");
  ASSERT_EVAL_JS_EXPR_EQ( v7, "delete {}", "true");
  ASSERT_EVAL_JS_EXPR_EQ( v7, "delete []", "true");
  ASSERT_EVAL_JS_EXPR_EQ( v7, "delete null", "true");
  ASSERT_EVAL_JS_EXPR_EQ( v7, "delete (function(){})", "true");
  ASSERT_EVAL_JS_EXPR_EQ( v7, "delete NaN", "false");
  ASSERT_EVAL_JS_EXPR_EQ( v7, "delete undefined", "false");

  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_JS_EXPR_EQ(
      v7,
      "function A(){}; A.prototype.prop = 'foo'; var a = new A(); "
      "a.prop = 'bar'; delete a.prop; a.prop;",
      "'foo'"
      );

  /* deletion of the object's property should not walk the prototype chain */
  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_JS_EXPR_EQ(
      v7,
      "function A(){}; A.prototype.prop = 'foo'; var a = new A(); "
      "delete a.prop; a.prop;",
      "'foo'"
      );

  /*
   * TODO(dfrank): reimplement array's `length` as a real property,
   * and uncomment this test (currently, it's '2|undefined|false')
   */
#if 0
  v7_del(v7, v7->vals.global_object, "arr", 3);
  ASSERT_EVAL_JS_EXPR_EQ(
      v7, "var arr = [1,2,3]; delete arr[2]; arr.length + '|' + arr[2] + '|' + (2 in arr);",
      "'3|undefined|false'"
      );
#endif

  ASSERT_EVAL_JS_EXPR_EQ( v7, "delete nonexisting;", "true");
  ASSERT_EVAL_ERR( v7, "'use strict'; delete nonexisting;",
      V7_SYNTAX_ERROR
      );


  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_JS_EXPR_EQ( v7, "a=10; delete 'a'; a", "10");

  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_ERR( v7, "a = 5; (function(){ delete a; })(); a;",
      V7_EXEC_EXCEPTION
      );


  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_JS_EXPR_EQ(
      v7, "a = 5; del = (function(){ var a=10; return delete a; })(); del + '|' + a",
      "'false|5'"
      );

  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_JS_EXPR_EQ(
      v7, "a = 5; del = (function(){ var a=10; return (function(){ return delete a;})(); })(); del + '|' + a",
      "'false|5'"
      );

  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_ERR(
      v7, "a = 5; del = (function(){ a=10; return (function(){ return delete a;})(); })(); del + '|' + a",
      V7_EXEC_EXCEPTION
      );

  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_JS_EXPR_EQ(
      v7, "a = 5; del = (function(){ b=10; return (function(){ return delete b;})(); })(); del + '|' + a",
      "'true|5'"
      );

  v7_del(v7, v7->vals.global_object, "a", 1);
  ASSERT_EVAL_ERR(
      v7, "a = 5; del = (function(){ b=10; return (function(){ delete b; return b;})(); })(); del + '|' + a",
      V7_EXEC_EXCEPTION
      );


  v7_del(v7, v7->vals.global_object, "a", 1);
  v7_del(v7, v7->vals.global_object, "f", 1);
  ASSERT_EVAL_JS_EXPR_EQ(
      v7, "function f(a, b){ del_a = delete a; del_b = delete b; del_f = delete f; return del_a + '|' + del_b + '|' + del_f; }; f();",
      "'false|false|false'"
      );

  v7_del(v7, v7->vals.global_object, "o", 1);
  ASSERT_EVAL_JS_EXPR_EQ(
      v7, "o = {}; Object.defineProperty(o, 'x', {value: 1}); delete o.x;",
      "false"
      );

  v7_del(v7, v7->vals.global_object, "o", 1);
  ASSERT_EVAL_ERR(
      v7, "'use strict'; o = {}; Object.defineProperty(o, 'x', {value: 1}); delete o.x;",
      V7_EXEC_EXCEPTION
      );

  /* }}} */

  /* calling cfunctions from bcode {{{ */

  ASSERT_EVAL_JS_EXPR_EQ(v7, "var a = new Object(); a", "({})");
  ASSERT_EVAL_JS_EXPR_EQ(v7, "'foo'.valueOf()", "'foo'");
  ASSERT_EVAL_JS_EXPR_EQ(v7,
      "var a = new String('bar'); a.valueOf();",
      "'bar'");
  ASSERT_EVAL_ERR(v7, "String.prototype.valueOf()", V7_EXEC_EXCEPTION);

  /* }}} */

  /* clang-format on */

  ASSERT_EVAL_NUM_EQ(v7, "(function() {var x = 42; return eval('x')})()", 42);

  /* `catch` block should execute in its own private scope */
  ASSERT_EVAL_NUM_EQ(v7, "e=1;try{throw foo}catch(e){e=2};e", 1);

  ASSERT_EVAL_JS_EXPR_EQ(v7,
                         "(function(a){return "
                         "arguments[0]+arguments[1]+arguments.length})('"
                         "1-', '2-');",
                         "'1-2-2'");

  ASSERT_EVAL_JS_EXPR_EQ(v7, "(function(a){return delete arguments})();",
                         "false");

  v7_destroy(v7);
  return NULL;
}

static const char *run_all_tests(const char *filter, double *total_elapsed) {
  RUN_TEST(test_unescape);
  RUN_TEST(test_to_json);
  RUN_TEST(test_json_parse);
  RUN_TEST(test_tokenizer);
  RUN_TEST(test_string_encoding);
  RUN_TEST(test_is_true);
  RUN_TEST(test_closure);
  RUN_TEST(test_native_functions);
  RUN_TEST(test_stdlib);
  RUN_TEST(test_runtime);
  RUN_TEST(test_apply);
  RUN_TEST(test_parser);
#ifndef V7_LARGE_AST
  RUN_TEST(test_parser_large_ast);
#endif
  RUN_TEST(test_interpreter);
  RUN_TEST(test_interp_unescape);
  RUN_TEST(test_strings);
#ifdef V7_ENABLE_DENSE_ARRAYS
  RUN_TEST(test_dense_arrays);
#endif
#ifdef V7_ENABLE_FILE
  RUN_TEST(test_file);
#endif
#ifdef V7_ENABLE_SOCKET
  RUN_TEST(test_socket);
#endif
#ifdef V7_ENABLE_CRYPTO
  RUN_TEST(test_crypto);
#endif
#ifdef V7_ENABLE_UBJSON
  RUN_TEST(test_ubjson);
#endif
#ifndef V7_DISABLE_GC
  RUN_TEST(test_gc_ptr_check);
  RUN_TEST(test_gc_mark);
#ifndef V7_MALLOC_GC
  RUN_TEST(test_gc_sweep);
#endif
  RUN_TEST(test_gc_own);
#endif
  RUN_TEST(test_exec_generic);
  RUN_TEST(test_ecmac);
  return NULL;
}

int main(int argc, char *argv[]) {
  const char *filter = argc > 1 ? argv[1] : "";
  double total_elapsed = 0.0;
  const char *fail_msg;
#ifdef V7_TEST_DIR
#define xstr(s) str(s)
#define str(s) #s
  chdir(xstr(V7_TEST_DIR));
#endif

  fail_msg = run_all_tests(filter, &total_elapsed);
  printf("%s, run %d in %.3fs\n", fail_msg ? "FAIL" : "PASS", num_tests,
         total_elapsed);
  return fail_msg == NULL ? EXIT_SUCCESS : EXIT_FAILURE;
}
