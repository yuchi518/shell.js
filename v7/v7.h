/*
 * Copyright (c) 2013-2014 Cesanta Software Limited
 * All rights reserved
 *
 * This software is dual-licensed: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation. For the terms of this
 * license, see <http: *www.gnu.org/licenses/>.
 *
 * You are free to use this software under the terms of the GNU General
 * Public License, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * Alternatively, you can license this software under a commercial
 * license, as set out in <https://www.cesanta.com/license>.
 */

/*
 * === C/C++ API
 *
 * V7 uses 64-bit `v7_val_t` type to store JavaScript values. There are
 * several families of functions V7 provides:
 *
 * - `v7_exec_*()` execute a piece of JavaScript code, put result in `v7_val_t`
 * - `v7_mk_*()` convert C/C++ values into JavaScript `v7_val_t` values
 * - `v7_to_*()` convert JavaScript `v7_val_t` values into C/C++ values
 * - `v7_is_*()` test whether JavaScript `v7_val_t` value is of given type
 * - misc functions that throw exceptions, operate on arrays & objects,
 *   call JS functions, etc
 *
 * NOTE: V7 instance is single threaded. It does not protect
 * it's data structures by mutexes. If V7 instance is shared between several
 * threads, a care should be taken to serialize accesses.
 */

#ifndef V7_HEADER_INCLUDED
#define V7_HEADER_INCLUDED

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stddef.h> /* For size_t */
#include <stdio.h>  /* For FILE */

/*
 * TODO(dfrank) : improve amalgamation, so that we'll be able to include
 * files here, and include common/osdep.h
 *
 * For now, copy-pasting `WARN_UNUSED_RESULT` here
 */
#ifdef __GNUC__
#define WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#else
#define WARN_UNUSED_RESULT
#endif

#define V7_VERSION "1.0"

#if (defined(_WIN32) && !defined(__MINGW32__) && !defined(__MINGW64__)) || \
    (defined(_MSC_VER) && _MSC_VER <= 1200)
#define V7_WINDOWS
#endif

#ifdef V7_WINDOWS
typedef unsigned __int64 uint64_t;
#else
#include <inttypes.h>
#endif
typedef uint64_t v7_val_t;

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*
 * Property attributes bitmask
 */
typedef unsigned int v7_prop_attr_t;
#define V7_PROPERTY_NON_WRITABLE (1 << 0)
#define V7_PROPERTY_NON_ENUMERABLE (1 << 1)
#define V7_PROPERTY_NON_CONFIGURABLE (1 << 2)
#define V7_PROPERTY_GETTER (1 << 3)
#define V7_PROPERTY_SETTER (1 << 4)
#define _V7_PROPERTY_HIDDEN (1 << 5)
/* property not managed by V7 HEAP */
#define _V7_PROPERTY_OFF_HEAP (1 << 6)
/*
 * not a property attribute, but a flag for `v7_def()`. It's here in order to
 * keep all offsets in one place
 */
#define _V7_DESC_PRESERVE_VALUE (1 << 7)

/*
 * Internal helpers for `V7_DESC_...` macros
 */
#define _V7_DESC_SHIFT 16
#define _V7_DESC_MASK ((1 << _V7_DESC_SHIFT) - 1)
#define _V7_MK_DESC(v, n) \
  (((v7_prop_attr_desc_t)(n)) << _V7_DESC_SHIFT | ((v) ? (n) : 0))
#define _V7_MK_DESC_INV(v, n) _V7_MK_DESC(!(v), (n))

/*
 * Property attribute descriptors that may be given to `v7_def()`: for each
 * attribute (`v7_prop_attr_t`), there is a corresponding macro, which takes
 * param: either 1 (set attribute) or 0 (clear attribute). If some particular
 * attribute isn't mentioned at all, it's left unchanged (or default, if the
 * property is being created)
 *
 * There is additional flag: `V7_DESC_PRESERVE_VALUE`. If it is set, the
 * property value isn't changed (or set to `undefined` if the property is being
 * created)
 */
typedef unsigned long v7_prop_attr_desc_t;
#define V7_DESC_WRITABLE(v) _V7_MK_DESC_INV(v, V7_PROPERTY_NON_WRITABLE)
#define V7_DESC_ENUMERABLE(v) _V7_MK_DESC_INV(v, V7_PROPERTY_NON_ENUMERABLE)
#define V7_DESC_CONFIGURABLE(v) _V7_MK_DESC_INV(v, V7_PROPERTY_NON_CONFIGURABLE)
#define V7_DESC_GETTER(v) _V7_MK_DESC(v, V7_PROPERTY_GETTER)
#define V7_DESC_SETTER(v) _V7_MK_DESC(v, V7_PROPERTY_SETTER)
#define V7_DESC_PRESERVE_VALUE _V7_DESC_PRESERVE_VALUE

#define _V7_DESC_HIDDEN(v) _V7_MK_DESC(v, _V7_PROPERTY_HIDDEN)
#define _V7_DESC_OFF_HEAP(v) _V7_MK_DESC(v, _V7_PROPERTY_OFF_HEAP)

/*
 * Object attributes bitmask
 */
typedef unsigned char v7_obj_attr_t;
#define V7_OBJ_NOT_EXTENSIBLE (1 << 0) /* TODO(lsm): store this in LSB */
#define V7_OBJ_DENSE_ARRAY (1 << 1)    /* TODO(mkm): store in some tag */
#define V7_OBJ_FUNCTION (1 << 2)       /* function object */
#define V7_OBJ_OFF_HEAP (1 << 3)       /* object not managed by V7 HEAP */

/* Opaque structure. V7 engine handler. */
struct v7;

enum v7_err {
  V7_OK,
  V7_SYNTAX_ERROR,
  V7_EXEC_EXCEPTION,
  V7_STACK_OVERFLOW,
  V7_AST_TOO_LARGE,
  V7_INVALID_ARG,
  V7_INTERNAL_ERROR,
};

/* JavaScript -> C call interface */
WARN_UNUSED_RESULT
typedef enum v7_err(v7_cfunction_t)(struct v7 *v7, v7_val_t *res);

/* Create V7 instance */
struct v7 *v7_create(void);

struct v7_mk_opts {
  size_t object_arena_size;
  size_t function_arena_size;
  size_t property_arena_size;
#ifdef V7_STACK_SIZE
  void *c_stack_base;
#endif
#ifdef V7_FREEZE
  /* if not NULL, dump JS heap after init */
  char *freeze_file;
#endif
};
struct v7 *v7_mk_opt(struct v7_mk_opts opts);

/* Destroy V7 instance */
void v7_destroy(struct v7 *v7);

/*
 * Execute JavaScript `js_code`. The result of evaluation is stored in
 * the `result` variable.
 *
 * Return:
 *
 *  - V7_OK on success. `result` contains the result of execution.
 *  - V7_SYNTAX_ERROR if `js_code` in not a valid code. `result` is undefined.
 *  - V7_EXEC_EXCEPTION if `js_code` threw an exception. `result` stores
 *    an exception object.
 *  - V7_AST_TOO_LARGE if `js_code` contains an AST segment longer than 16 bit.
 *    `result` is undefined. To avoid this error, build V7 with V7_LARGE_AST.
 */
WARN_UNUSED_RESULT
enum v7_err v7_exec(struct v7 *v7, const char *js_code, v7_val_t *result);

/*
 * Same as `v7_exec()`, but loads source code from `path` file.
 */
WARN_UNUSED_RESULT
enum v7_err v7_exec_file(struct v7 *v7, const char *path, v7_val_t *result);

/*
 * Same as `v7_exec()`, but passes `this_obj` as `this` to the execution
 * context.
 */
WARN_UNUSED_RESULT
enum v7_err v7_exec_with(struct v7 *v7, const char *js_code, v7_val_t this_obj,
                         v7_val_t *result);

/*
 * Parse `str` and store corresponding JavaScript object in `res` variable.
 * String `str` should be '\0'-terminated.
 * Return value and semantic is the same as for `v7_exec()`.
 */
WARN_UNUSED_RESULT
enum v7_err v7_parse_json(struct v7 *v7, const char *str, v7_val_t *res);

/*
 * Same as `v7_parse_json()`, but loads JSON string from `path`.
 */
WARN_UNUSED_RESULT
enum v7_err v7_parse_json_file(struct v7 *v7, const char *path, v7_val_t *res);

/*
 * Compile JavaScript code `js_code` into the byte code and write generated
 * byte code into opened file stream `fp`. If `generate_binary_output` is 0,
 * then generated byte code is in human-readable text format. Otherwise, it is
 * in the binary format, suitable for execution by V7 instance.
 * NOTE: `fp` must be a valid, opened, writable file stream.
 */
WARN_UNUSED_RESULT
enum v7_err v7_compile(const char *js_code, int generate_binary_output,
                       int use_bcode, FILE *fp);

/* Make an empty object */
v7_val_t v7_mk_object(struct v7 *v7);

/* Make an empty array object */
v7_val_t v7_mk_array(struct v7 *v7);

/*
 * Make a JS function object backed by a cfunction.
 *
 * `func` is a C callback.
 *
 * A function object is JS object having the Function prototype that holds a
 * cfunction value in a hidden property.
 *
 * The function object will have a `prototype` property holding an object that
 * will be used as the prototype of objects created when calling the function
 * with the `new` operator.
 */
v7_val_t v7_mk_function(struct v7 *, v7_cfunction_t *func);

/* Make f a JS constructor function for objects with prototype in proto. */
v7_val_t v7_mk_constructor(struct v7 *v7, v7_val_t proto, v7_cfunction_t *f);

/* Make numeric primitive value */
v7_val_t v7_mk_number(double num);

/* Make boolean primitive value (either `true` or `false`) */
v7_val_t v7_mk_boolean(int is_true);

/* Make `null` primitive value */
v7_val_t v7_mk_null(void);

/* Make `undefined` primitive value */
v7_val_t v7_mk_undefined(void);

/*
 * Creates a string primitive value.
 * `str` must point to the utf8 string of length `len`.
 * If `len` is ~0, `str` is assumed to be NUL-terminated and `strlen(str)` is
 * used.
 *
 * If `copy` is non-zero, the string data is copied and owned by the GC. The
 * caller can free the string data afterwards. Otherwise (`copy` is zero), the
 * caller owns the string data, and is responsible for not freeing it while it
 * is used.
 */
v7_val_t v7_mk_string(struct v7 *v7, const char *str, size_t len, int copy);

/*
 * Make RegExp object.
 * `regex`, `regex_len` specify a pattern, `flags` and `flags_len` specify
 * flags. Both utf8 encoded. For example, `regex` is `(.+)`, `flags` is `gi`.
 * If `regex_len` is ~0, `regex` is assumed to be NUL-terminated and
 * `strlen(regex)` is used.
 */
WARN_UNUSED_RESULT
enum v7_err v7_mk_regexp(struct v7 *v7, const char *regex, size_t regex_len,
                         const char *flags, size_t flags_len, v7_val_t *res);

/*
 * Make JavaScript value that holds C/C++ `void *` pointer.
 *
 * A foreign value is completely opaque and JS code cannot do anything useful
 * with it except holding it in properties and passing it around.
 * It behaves like a sealed object with no properties.
 *
 * NOTE:
 * Only valid pointers (as defined by each supported architecture) will fully
 * preserved. In particular, all supported 64-bit architectures (x86_64, ARM-64)
 * actually define a 48-bit virtual address space.
 * Foreign values will be sign-extended as required, i.e creating a foreign
 * value of something like `(void *) -1` will work as expected. This is
 * important because in some 64-bit OSs (e.g. Solaris) the user stack grows
 * downwards from the end of the address space.
 *
 * If you need to store exactly sizeof(void*) bytes of raw data where
 * `sizeof(void*)` >= 8, please use byte arrays instead.
 */
v7_val_t v7_mk_foreign(void *ptr);

/*
 * Make a JS value that holds C/C++ callback pointer.
 *
 * This is a low-level function value. It's not a real object and cannot hold
 * user defined properties. You should use `v7_mk_function` unless you know
 * what you're doing.
 */
v7_val_t v7_mk_cfunction(v7_cfunction_t *func);

/*
 * Returns true if the given value is an object or function.
 * i.e. it returns true if the value holds properties and can be
 * used as argument to `v7_get`, `v7_set` and `v7_def`.
 */
int v7_is_object(v7_val_t v);

/* Returns true if given value is a JavaScript function object */
int v7_is_function(v7_val_t v);

/* Returns true if given value is a primitive string value */
int v7_is_string(v7_val_t v);

/* Returns true if given value is a primitive boolean value */
int v7_is_boolean(v7_val_t v);

/* Returns true if given value is a primitive number value */
int v7_is_number(v7_val_t v);

/* Returns true if given value is a primitive `null` value */
int v7_is_null(v7_val_t v);

/* Returns true if given value is a primitive `undefined` value */
int v7_is_undefined(v7_val_t v);

/* Returns true if given value is a JavaScript RegExp object*/
int v7_is_regexp(struct v7 *v7, v7_val_t v);

/* Returns true if given value holds a pointer to C callback */
int v7_is_cfunction_ptr(v7_val_t v);

/* Returns true if given value holds an object which represents C callback */
int v7_is_cfunction_obj(struct v7 *v7, v7_val_t v);

/*
 * Returns true if given value is either cfunction pointer or cfunction object
 */
int v7_is_cfunction(struct v7 *v7, v7_val_t v);

/*
 * Returns true if given value is callable (i.e. it's either a JS function,
 * cfunction pointer or cfunction object)
 */
int v7_is_callable(struct v7 *v7, v7_val_t v);

/* Returns true if given value holds `void *` pointer */
int v7_is_foreign(v7_val_t v);

/* Returns true if given value is an array object */
int v7_is_array(struct v7 *v7, v7_val_t v);

/* Returns true if the object is an instance of a given constructor. */
int v7_is_instanceof(struct v7 *v7, v7_val_t o, const char *c);

/* Returns true if the object is an instance of a given constructor. */
int v7_is_instanceof_v(struct v7 *v7, v7_val_t o, v7_val_t c);

/* Returns true if given value evaluates to true, as in `if (v)` statement. */
int v7_is_truthy(struct v7 *v7, v7_val_t v);

/*
 * Returns `void *` pointer stored in `v7_val_t`.
 *
 * Returns NULL if the value is not a foreign pointer.
 */
void *v7_to_foreign(v7_val_t v);

/*
 * Returns boolean stored in `v7_val_t`:
 *  0 for `false` or non-boolean, non-0 for `true`
 */
int v7_to_boolean(v7_val_t v);

/*
 * Returns `double` value stored in `v7_val_t`.
 *
 * Returns NaN for non-numbers.
 */
double v7_to_number(v7_val_t v);

/*
 * Returns `v7_cfunction_t *` callback pointer stored in `v7_val_t`, or NULL
 * if given value is neither cfunction pointer nor cfunction object.
 */
v7_cfunction_t *v7_to_cfunction(struct v7 *v7, v7_val_t v);

/*
 * Returns a pointer to the string stored in `v7_val_t`.
 *
 * String length returned in `len`.
 *
 * JS strings can contain embedded NUL chars and may or may not be NUL
 * terminated.
 *
 * CAUTION: creating new JavaScript object, array, or string may kick in a
 * garbage collector, which in turn may relocate string data and invalidate
 * pointer returned by `v7_get_string_data()`.
 *
 * Short JS strings are embedded inside the `v7_val_t` value itself. This is why
 * a pointer to a `v7_val_t` is required. It also means that the string data
 * will become invalid once that `v7_val_t` value goes out of scope.
 */
const char *v7_get_string_data(struct v7 *v7, v7_val_t *v, size_t *len);

/*
 * Returns a pointer to the string stored in `v7_val_t`.
 *
 * Returns NULL if the value is not a string or if the string is not compatible
 * with a C string.
 *
 * C compatible strings contain exactly one NUL char, in terminal position.
 *
 * All strings owned by the V7 engine (see v7_mk_string) are guaranteed to
 * be NUL terminated.
 * Out of these, those that don't include embedded NUL chars are guaranteed to
 * be C compatible.
 */
const char *v7_to_cstring(struct v7 *v7, v7_val_t *v);

/* Return root level (`global`) object of the given V7 instance. */
v7_val_t v7_get_global(struct v7 *v);

/* Return current `this` object. */
v7_val_t v7_get_this(struct v7 *v);

/* Return current `arguments` array */
v7_val_t v7_get_arguments(struct v7 *v);

/* Return i-th argument */
v7_val_t v7_arg(struct v7 *v, unsigned long i);

/* Return the length of `arguments` */
unsigned long v7_argc(struct v7 *v7);

/*
 * Object interface.
 */

/* Set object's prototype. Return old prototype or undefined on error. */
v7_val_t v7_set_proto(struct v7 *v7, v7_val_t obj, v7_val_t proto);

/*
 * Lookup property `name` in object `obj`. If `obj` holds no such property,
 * an `undefined` value is returned.
 *
 * If `name_len` is ~0, `name` is assumed to be NUL-terminated and
 * `strlen(name)` is used.
 */
v7_val_t v7_get(struct v7 *v7, v7_val_t obj, const char *name, size_t name_len);

/*
 * Like `v7_get()`, but "returns" value through `res` pointer argument.
 * `res` must not be `NULL`.
 *
 * Caller should check the error code returned, and if it's something other
 * than `V7_OK`, perform cleanup and return this code further.
 */
WARN_UNUSED_RESULT
enum v7_err v7_get_throwing(struct v7 *v7, v7_val_t obj, const char *name,
                            size_t name_len, v7_val_t *res);

/*
 * Define object property, similar to JavaScript `Object.defineProperty()`.
 *
 * `name`, `name_len` specify property name, `val` is a property value.
 * `attrs_desc` is a set of flags which can affect property's attributes,
 * see comment of `v7_prop_attr_desc_t` for details.
 *
 * If `name_len` is ~0, `name` is assumed to be NUL-terminated and
 * `strlen(name)` is used.
 *
 * Returns non-zero on success, 0 on error (e.g. out of memory).
 *
 * See also `v7_set()`.
 */
int v7_def(struct v7 *v7, v7_val_t obj, const char *name, size_t name_len,
           v7_prop_attr_desc_t attrs_desc, v7_val_t v);

/*
 * Set object property. Behaves just like JavaScript assignment.
 *
 * See also `v7_def()`.
 */
int v7_set(struct v7 *v7, v7_val_t obj, const char *name, size_t len,
           v7_val_t val);

/*
 * A helper function to define object's method backed by a C function `func`.
 * `name` must be NUL-terminated.
 *
 * Return value is the same as for `v7_set()`.
 */
int v7_set_method(struct v7 *, v7_val_t obj, const char *name,
                  v7_cfunction_t *func);

/*
 * Delete own property `name` of the object `obj`. Does not follow the
 * prototype chain.
 *
 * If `name_len` is ~0, `name` is assumed to be NUL-terminated and
 * `strlen(name)` is used.
 *
 * Returns 0 on success, -1 on error.
 */
int v7_del(struct v7 *v7, v7_val_t obj, const char *name, size_t name_len);

/*
 * Iterate over the `obj`'s properties.
 *
 * Usage example:
 *
 *     void *h = NULL;
 *     v7_val_t name, val;
 *     unsigned int attrs;
 *     while ((h = v7_next_prop(h, obj, &name, &val, &attrs)) != NULL) {
 *       ...
 *     }
 */
void *v7_next_prop(void *handle, v7_val_t obj, v7_val_t *name, v7_val_t *value,
                   v7_prop_attr_t *attrs);

/*
 * Array interface.
 */

/* Returns length on an array. If `arr` is not an array, 0 is returned. */
unsigned long v7_array_length(struct v7 *v7, v7_val_t arr);

/* Insert value `v` in array `arr` at the end of the array. */
int v7_array_push(struct v7 *, v7_val_t arr, v7_val_t v);

/*
 * Like `v7_array_push()`, but "returns" value through the `res` pointer
 * argument. `res` is allowed to be `NULL`.
 *
 * Caller should check the error code returned, and if it's something other
 * than `V7_OK`, perform cleanup and return this code further.
 */
WARN_UNUSED_RESULT
enum v7_err v7_array_push_throwing(struct v7 *v7, v7_val_t arr, v7_val_t v,
                                   int *res);

/*
 * Return array member at index `index`. If `index` is out of bounds, undefined
 * is returned.
 */
v7_val_t v7_array_get(struct v7 *, v7_val_t arr, unsigned long index);

/* Insert value `v` into `arr` at index `index`. */
int v7_array_set(struct v7 *v7, v7_val_t arr, unsigned long index, v7_val_t v);

/*
 * Like `v7_array_set()`, but "returns" value through the `res` pointer
 * argument. `res` is allowed to be `NULL`.
 *
 * Caller should check the error code returned, and if it's something other
 * than `V7_OK`, perform cleanup and return this code further.
 */
WARN_UNUSED_RESULT
enum v7_err v7_array_set_throwing(struct v7 *v7, v7_val_t arr,
                                  unsigned long index, v7_val_t v, int *res);

/* Delete value in array `arr` at index `index`, if it exists. */
void v7_array_del(struct v7 *v7, v7_val_t arr, unsigned long index);

/*
 * Generate string representation of the JavaScript value `val` into a buffer
 * `buf`, `len`. If `len` is too small to hold a generated string,
 * `v7_stringify()` allocates required memory. In that case, it is caller's
 * responsibility to free the allocated buffer. Generated string is guaranteed
 * to be 0-terminated.
 *
 * Available stringification modes are:
 *
 * - V7_STRINGIFY_DEFAULT:
 *   Convert JS value to string, using common JavaScript semantics:
 *   - If value is an object:
 *     - call `toString()`;
 *     - If `toString()` returned non-primitive value, call `valueOf()`;
 *     - If `valueOf()` returned non-primitive value, throw `TypeError`.
 *   - Now we have a primitive, and if it's not a string, then stringify it.
 *
 * - V7_STRINGIFY_JSON:
 *   Generate JSON output
 *
 * - V7_STRINGIFY_DEBUG:
 *   Mostly like JSON, but will not omit non-JSON objects like functions.
 *
 * Example code:
 *
 *     char buf[100], *p;
 *     p = v7_stringify(v7, obj, buf, sizeof(buf), V7_STRINGIFY_DEFAULT);
 *     printf("JSON string: [%s]\n", p);
 *     if (p != buf) {
 *       free(p);
 *     }
 */
enum v7_stringify_mode {
  V7_STRINGIFY_DEFAULT,
  V7_STRINGIFY_JSON,
  V7_STRINGIFY_DEBUG,
};
char *v7_stringify(struct v7 *v7, v7_val_t v, char *buf, size_t len,
                   enum v7_stringify_mode mode);

/*
 * Like `v7_stringify()`, but "returns" value through the `res` pointer
 * argument. `res` must not be `NULL`.
 *
 * Caller should check the error code returned, and if it's something other
 * than `V7_OK`, perform cleanup and return this code further.
 */
WARN_UNUSED_RESULT
enum v7_err v7_stringify_throwing(struct v7 *v7, v7_val_t v, char *buf,
                                  size_t size, enum v7_stringify_mode mode,
                                  char **res);

/*
 * A shortcut for `v7_stringify()` with `V7_STRINGIFY_JSON`
 */
#define v7_to_json(a, b, c, d) v7_stringify(a, b, c, d, V7_STRINGIFY_JSON)

/* Output a string representation of the value to stdout.
 * V7_STRINGIFY_DEBUG mode is used. */
void v7_print(struct v7 *v7, v7_val_t v);

/* Output a string representation of the value to stdout followed by a newline.
 * V7_STRINGIFY_DEBUG mode is used. */
void v7_println(struct v7 *v7, v7_val_t v);

/* Output a string representation of the value to a file.
 * V7_STRINGIFY_DEBUG mode is used. */
void v7_fprint(FILE *f, struct v7 *v7, v7_val_t v);

/* Output a string representation of the value to a file followed by a newline.
 * V7_STRINGIFY_DEBUG mode is used. */
void v7_fprintln(FILE *f, struct v7 *v7, v7_val_t v);

/* Output stack trace recorded in the exception `e` to file `f` */
void v7_fprint_stack_trace(FILE *f, struct v7 *v7, v7_val_t e);

/* Output error object message and possibly stack trace to f */
void v7_print_error(FILE *f, struct v7 *v7, const char *ctx, v7_val_t e);

/*
 * Call function `func` with arguments `args`, using `this_obj` as `this`.
 * `args` should be an array containing arguments or `undefined`.
 *
 * `res` can be `NULL` if return value is not required.
 */
WARN_UNUSED_RESULT
enum v7_err v7_apply(struct v7 *v7, v7_val_t func, v7_val_t this_obj,
                     v7_val_t args, v7_val_t *res);

/* Throw an exception with an already existing value. */
WARN_UNUSED_RESULT
enum v7_err v7_throw(struct v7 *v7, v7_val_t v);

/*
 * Throw an exception with given formatted message.
 *
 * Pass "Error" as typ for a generic error.
 */
WARN_UNUSED_RESULT
enum v7_err v7_throwf(struct v7 *v7, const char *typ, const char *err_fmt, ...);

/*
 * Rethrow the currently thrown object. In fact, it just returns
 * V7_EXEC_EXCEPTION.
 */
WARN_UNUSED_RESULT
enum v7_err v7_rethrow(struct v7 *v7);

/*
 * Returns the value that is being thrown at the moment, or `undefined` if
 * nothing is being thrown. If `is_thrown` is not `NULL`, it will be set
 * to either 0 or 1, depending on whether something is thrown at the moment.
 */
v7_val_t v7_get_thrown_value(struct v7 *v7, unsigned char *is_thrown);

/* Clears currently thrown value, if any. */
void v7_clear_thrown_value(struct v7 *v7);

/* Returns last parser error message. */
const char *v7_get_parser_error(struct v7 *v7);

enum v7_heap_stat_what {
  V7_HEAP_STAT_HEAP_SIZE,
  V7_HEAP_STAT_HEAP_USED,
  V7_HEAP_STAT_STRING_HEAP_RESERVED,
  V7_HEAP_STAT_STRING_HEAP_USED,
  V7_HEAP_STAT_OBJ_HEAP_MAX,
  V7_HEAP_STAT_OBJ_HEAP_FREE,
  V7_HEAP_STAT_OBJ_HEAP_CELL_SIZE,
  V7_HEAP_STAT_FUNC_HEAP_MAX,
  V7_HEAP_STAT_FUNC_HEAP_FREE,
  V7_HEAP_STAT_FUNC_HEAP_CELL_SIZE,
  V7_HEAP_STAT_PROP_HEAP_MAX,
  V7_HEAP_STAT_PROP_HEAP_FREE,
  V7_HEAP_STAT_PROP_HEAP_CELL_SIZE,
  V7_HEAP_STAT_FUNC_AST_SIZE,
  V7_HEAP_STAT_FUNC_BCODE_SIZE,
  V7_HEAP_STAT_FUNC_OWNED,
  V7_HEAP_STAT_FUNC_OWNED_MAX
};

enum v7_stack_stat_what {
  /* max stack size consumed by `i_exec()` */
  V7_STACK_STAT_EXEC,
  /* max stack size consumed by `parse()` (which is called from `i_exec()`) */
  V7_STACK_STAT_PARSER,

  V7_STACK_STATS_CNT
};

#if V7_ENABLE__Memory__stats
/* Returns a given heap statistics */
int v7_heap_stat(struct v7 *v7, enum v7_heap_stat_what what);
#endif

#if defined(V7_ENABLE_STACK_TRACKING)
int v7_stack_stat(struct v7 *v7, enum v7_stack_stat_what what);
void v7_stack_stat_clean(struct v7 *v7);
#endif

/*
 * Set an optional C stack limit.
 *
 * It sets a flag that will cause the interpreter
 * to throw an InterruptedError.
 * It's safe to call it from signal handlers and ISRs
 * on single threaded environments.
 */
void v7_interrupt(struct v7 *v7);

/*
 * Enable or disable GC.
 *
 * Must be called before invoking v7_exec or v7_apply
 * from within a cfunction unless you know what you're doing.
 *
 * GC is disabled during execution of cfunctions in order to simplify
 * memory management of simple cfunctions.
 * However executing even small snippets of JS code causes a lot of memory
 * pressure. Enabling GC solves that but forces you to take care of the
 * reachability of your temporary V7 v7_val_t variables, as the GC needs
 * to know where they are since objects and strings can be either reclaimed
 * or relocated during a GC pass.
 */
void v7_set_gc_enabled(struct v7 *v7, int enabled);

/*
 * Tells the GC about a JS value variable/field owned
 * by C code.
 *
 * User C code should own v7_val_t variables
 * if the value's lifetime crosses any invocation
 * to the v7 runtime that creates new objects or new
 * properties and thus can potentially trigger GC.
 *
 * The registration of the variable prevents the GC from mistakenly treat
 * the object as garbage. The GC might be triggered potentially
 * allows the GC to update pointers
 *
 * User code should also explicitly disown the variables with v7_disown once
 * it goes out of scope or the structure containing the v7_val_t field is freed.
 *
 * Example:
 *
 *  ```
 *    struct v7_val cb;
 *    v7_own(v7, &cb);
 *    cb = v7_array_get(v7, args, 0);
 *    // do something with cb
 *    v7_disown(v7, &cb);
 *  ```
 */
void v7_own(struct v7 *v7, v7_val_t *v);

/*
 * Returns 1 if value is found, 0 otherwise
 */
int v7_disown(struct v7 *v7, v7_val_t *v);

/*
 * Perform garbage collection.
 * Pass true to full in order to reclaim unused heap back to the OS.
 */
void v7_gc(struct v7 *v7, int full);

int v7_main(int argc, char *argv[], void (*init_func)(struct v7 *),
            void (*fini_func)(struct v7 *));

#ifdef V7_STACK_SIZE
/* Returns lowest recorded available stack size. */
int v7_get_stack_avail_lwm(struct v7 *v7);
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* V7_HEADER_INCLUDED */
