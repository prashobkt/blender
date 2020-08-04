/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef __CLG_LOG_H__
#define __CLG_LOG_H__

/** \file
 * \ingroup clog
 *
 * C Logging Library (clog)
 * ========================
 *
 * Usage
 * -----
 *
 * - `CLG_LOGREF_DECLARE_GLOBAL` macro to declare #CLG_LogRef pointers.
 * - `CLOG_` prefixed macros for logging.
 *
 * Identifiers
 * -----------
 *
 * #CLG_LogRef holds an identifier which defines the category of the logger.
 *
 * You can define and use identifiers as needed, logging will lazily initialize them.
 *
 * By convention lower case dot separated identifiers are used, eg:
 * `module.sub_module`, this allows filtering by `module.*`,
 * see #CLG_type_filter_include, #CLG_type_filter_exclude
 *
 * There is currently no functionality to remove a category once it's created.
 *
 * Severity
 * --------
 *
 * - `INFO`: Simply log events, uses verbosity levels to control how much information to show.
 * - `WARN`: General warnings (which aren't necessary to show to users).
 * - `ERROR`: An error we can recover from, should not happen.
 * - `FATAL`: Similar to assert. This logs the message, then a stack trace and abort.
 * Verbosity Level
 * ---------------
 *
 * Usage:
 *
 * - 0: Always show (used for warnings, errors).
 *   Should never get in the way or become annoying.
 *
 * - 1: Top level module actions (eg: load a file, create a new window .. etc).
 *
 * - 2: Actions within a module (steps which compose an action, but don't flood output).
 *   Running a tool, full data recalculation.
 *
 * - 3: Detailed actions which may be of interest when debugging internal logic of a module
 *   These *may* flood the log with details.
 *
 * - 4+: May be used for more details than 3, should be avoided but not prevented.
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifdef __GNUC__
#  define _CLOG_ATTR_NONNULL(args...) __attribute__((nonnull(args)))
#else
#  define _CLOG_ATTR_NONNULL(...)
#endif

#ifdef __GNUC__
#  define _CLOG_ATTR_PRINTF_FORMAT(format_param, dots_param) \
    __attribute__((format(printf, format_param, dots_param)))
#else
#  define _CLOG_ATTR_PRINTF_FORMAT(format_param, dots_param)
#endif

/* For printing timestamp. */
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <stdbool.h>

#define STRINGIFY_ARG(x) "" #x
#define STRINGIFY_APPEND(a, b) "" a #b
#define STRINGIFY(x) STRINGIFY_APPEND("", x)

struct CLogContext;

/* Don't typedef enums. */
enum CLG_LogFlag {
  CLG_FLAG_USE = (1 << 0),
};

enum CLG_Severity {
  CLG_SEVERITY_DEBUG = 0,
  CLG_SEVERITY_VERBOSE,
  CLG_SEVERITY_INFO,
  CLG_SEVERITY_WARN,
  CLG_SEVERITY_ERROR,
  CLG_SEVERITY_FATAL,
};
#define CLG_SEVERITY_LEN (CLG_SEVERITY_FATAL + 1)

/* Each logger ID has one of these. */
typedef struct CLG_LogType {
  struct CLG_LogType *next;
  char identifier[64];
  /** FILE output. */
  struct CLogContext *ctx;
  unsigned short level;
  unsigned short severity_level;
  enum CLG_LogFlag flag;
} CLG_LogType;

typedef struct CLG_LogRef {
  const char *identifier;
  /** set to true to skip all filtering and always print logger
   * for debug only, do not commit when set to true
   */
  bool force_enable;
  CLG_LogType *type;
} CLG_LogRef;

typedef struct CLG_LogRecord {
  /** Link for clog version of ListBase */
  struct CLG_LogRecord *next, *prev;
  /** track where does the log comes from */
  CLG_LogType *type;
  enum CLG_Severity severity;
  unsigned short verbosity;
  uint64_t timestamp;
  const char *file_line;
  const char *function;
  const char *message;
} CLG_LogRecord;

/** clog version of ListBase */
typedef struct CLG_LogRecordList {
  struct CLG_LogRecord *first, *last;
} CLG_LogRecordList;

void CLG_log_str(CLG_LogType *lg,
                 enum CLG_Severity severity,
                 unsigned short verbosity,
                 const char *file_line,
                 const char *fn,
                 const char *message) _CLOG_ATTR_NONNULL(1, 4, 5, 6);
void CLG_logf(CLG_LogType *lg,
              enum CLG_Severity severity,
              unsigned short verbosity,
              const char *file_line,
              const char *fn,
              const char *format,
              ...) _CLOG_ATTR_NONNULL(1, 4, 5, 6) _CLOG_ATTR_PRINTF_FORMAT(6, 7);

const char *clg_severity_as_text(enum CLG_Severity severity);
CLG_LogRecord *clog_log_record_init(CLG_LogType *type,
                                    enum CLG_Severity severity,
                                    unsigned short verbosity,
                                    const char *file_line,
                                    const char *function,
                                    char *message);
void clog_log_record_free(CLG_LogRecord *log_record);

/* Main initializer and distructor (per session, not logger). */
void CLG_init(void);
void CLG_exit(void);

bool CLG_use_stdout_get(void);
void CLG_use_stdout_set(bool value);
char *CLG_file_output_path_get(void);
void CLG_file_output_path_set(const char *value);
bool CLG_output_use_basename_get(void);
void CLG_output_use_basename_set(int value);
bool CLG_output_use_timestamp_get(void);
void CLG_output_use_timestamp_set(int value);
void CLG_fatal_fn_set(void (*fatal_fn)(void *file_handle));
void CLG_backtrace_fn_set(void (*fatal_fn)(void *file_handle));

void CLG_type_filter_set(const char *glob_str);
int CLG_type_filter_get(char *buff, int buff_len);
void CLG_type_filter_include(const char *type_filter, int type_filter_len);
void CLG_type_filters_clear(void);
void CLG_type_filter_exclude(const char *type_filter, int type_filter_len);

enum CLG_Severity CLG_severity_level_get(void);
void CLG_severity_level_set(enum CLG_Severity log_level);
unsigned short CLG_level_get(void);
void CLG_level_set(unsigned short log_level);
struct CLG_LogRecordList *CLG_log_record_get(void);

void CLG_logref_init(CLG_LogRef *clg_ref);

/** Declare outside function, declare as extern in header. */
#define CLG_LOGREF_DECLARE_GLOBAL(var, id) \
  static CLG_LogRef _static_##var = {id}; \
  CLG_LogRef *var = &_static_##var

/** Same as CLG_LOGREF_DECLARE_GLOBAL, but omits filters. For fast debugging, do not commit */
#define CLG_LOGREF_DECLARE_GLOBAL_FORCE(var, id) \
  static CLG_LogRef _static_##var = {id, true}; \
  CLG_LogRef *var = &_static_##var

/** Initialize struct once. */
#define CLOG_ENSURE(clg_ref) \
  ((clg_ref)->type ? (clg_ref)->type : (CLG_logref_init(clg_ref), (clg_ref)->type))

#define CLOG_CHECK_IN_USE(clg_ref) \
  ((void)CLOG_ENSURE(clg_ref), ((clg_ref)->force_enable || (clg_ref)->type->flag & CLG_FLAG_USE))

#ifdef DEBUG
/** same as CLOG_CHECK_IN_USE, but will be automatically disable in release build */
#  define CLOG_DEBUG_CHECK_IN_USE(clg_ref) CLOG_CHECK_IN_USE(clg_ref)
#else
#  define CLOG_DEBUG_CHECK_IN_USE(clg_ref) false
#endif  // DEBUG

/** check verbosity/debug level when using severity DEBUG/VERBOSE */
#define CLOG_CHECK_LEVEL(clg_ref, log_level, ...) \
  (CLOG_CHECK_IN_USE(clg_ref) && ((clg_ref)->type->severity_level <= CLG_SEVERITY_VERBOSE) && \
   ((clg_ref)->type->level >= log_level))

#ifdef DEBUG
/** same as CLOG_CHECK_LEVEL, but will be automatically disable in release build */
#  define CLOG_DEBUG_CHECK_LEVEL(clg_ref, log_level, ...) \
    CLOG_CHECK_LEVEL(clg_ref, log_level, __VA_ARGS__)
#else
#  define CLOG_DEBUG_CHECK_LEVEL(clg_ref, log_level, ...) (void)0
#endif  // DEBUG

#define CLOG_AT_SEVERITY(clg_ref, severity, log_level, ...) \
  { \
    CLG_LogType *_lg_ty = CLOG_ENSURE(clg_ref); \
    if ((clg_ref)->force_enable) { \
      CLG_logf( \
          _lg_ty, severity, log_level, __FILE__ ":" STRINGIFY(__LINE__), __func__, __VA_ARGS__); \
    } \
    else if ((_lg_ty->flag & CLG_FLAG_USE) && severity >= _lg_ty->severity_level) { \
      switch (severity) { \
        case CLG_SEVERITY_DEBUG: \
        case CLG_SEVERITY_VERBOSE: \
          if (log_level > _lg_ty->level) { \
            break; \
          } \
          __attribute__((fallthrough)); \
        default: \
          CLG_logf(_lg_ty, \
                   severity, \
                   log_level, \
                   __FILE__ ":" STRINGIFY(__LINE__), \
                   __func__, \
                   __VA_ARGS__); \
      } \
    } \
  } \
  ((void)0)

#define CLOG_STR_AT_SEVERITY(clg_ref, severity, log_level, str) \
  { \
    CLG_LogType *_lg_ty = CLOG_ENSURE(clg_ref); \
    if ((clg_ref)->force_enable) { \
      CLG_log_str(_lg_ty, severity, log_level, __FILE__ ":" STRINGIFY(__LINE__), __func__, str); \
    } \
    else if ((_lg_ty->flag & CLG_FLAG_USE) && severity >= _lg_ty->severity_level) { \
      switch (severity) { \
        case CLG_SEVERITY_DEBUG: \
        case CLG_SEVERITY_VERBOSE: \
          if (log_level > _lg_ty->level) { \
            break; \
          } \
          __attribute__((fallthrough)); \
        default: \
          CLG_log_str( \
              _lg_ty, severity, log_level, __FILE__ ":" STRINGIFY(__LINE__), __func__, str); \
      } \
    } \
  } \
  ((void)0)

#define CLOG_STR_AT_SEVERITY_N(clg_ref, severity, log_level, str) \
  { \
    CLG_LogType *_lg_ty = CLOG_ENSURE(clg_ref); \
    if ((clg_ref)->force_enable) { \
      const char *_str = str; \
      CLG_log_str(_lg_ty, severity, log_level, __FILE__ ":" STRINGIFY(__LINE__), __func__, _str); \
      MEM_freeN((void *)_str); \
    } \
    else if ((_lg_ty->flag & CLG_FLAG_USE) && severity >= _lg_ty->severity_level) { \
      switch (severity) { \
        case CLG_SEVERITY_DEBUG: \
        case CLG_SEVERITY_VERBOSE: \
          if (log_level > _lg_ty->level) { \
            break; \
          } \
          __attribute__((fallthrough)); \
        default: { \
          const char *_str = str; \
          CLG_log_str( \
              _lg_ty, severity, log_level, __FILE__ ":" STRINGIFY(__LINE__), __func__, _str); \
          MEM_freeN((void *)_str); \
        } \
      } \
    } \
  } \
  ((void)0)

/* CLOG_DEBUG is the same as CLOG_VERBOSE, but available only in debug builds */
#ifdef DEBUG
#  define CLOG_DEBUG(clg_ref, log_level, ...) \
    CLOG_AT_SEVERITY(clg_ref, CLG_SEVERITY_DEBUG, log_level, __VA_ARGS__)
#else
#  define CLOG_DEBUG(clg_ref, log_level, ...) \
    do { \
    } while (false)
#endif  // DEBUG
#define CLOG_VERBOSE(clg_ref, log_level, ...) \
  CLOG_AT_SEVERITY(clg_ref, CLG_SEVERITY_VERBOSE, log_level, __VA_ARGS__)
#define CLOG_INFO(clg_ref, ...) CLOG_AT_SEVERITY(clg_ref, CLG_SEVERITY_INFO, 0, __VA_ARGS__)
#define CLOG_WARN(clg_ref, ...) CLOG_AT_SEVERITY(clg_ref, CLG_SEVERITY_WARN, 0, __VA_ARGS__)
#define CLOG_ERROR(clg_ref, ...) CLOG_AT_SEVERITY(clg_ref, CLG_SEVERITY_ERROR, 0, __VA_ARGS__)
#define CLOG_FATAL(clg_ref, ...) CLOG_AT_SEVERITY(clg_ref, CLG_SEVERITY_FATAL, 0, __VA_ARGS__)

#ifdef DEBUG
#  define CLOG_STR_DEBUG(clg_ref, log_level, str) \
    CLOG_STR_AT_SEVERITY(clg_ref, CLG_SEVERITY_DEBUG, log_level, str)
#else
#  define CLOG_STR_DEBUG(clg_ref, log_level, str) \
    do { \
    } while (false)
#endif  // DEBUG
#define CLOG_STR_VERBOSE(clg_ref, log_level, str) \
  CLOG_STR_AT_SEVERITY(clg_ref, CLG_SEVERITY_VERBOSE, log_level, str)
#define CLOG_STR_INFO(clg_ref, str) CLOG_STR_AT_SEVERITY(clg_ref, CLG_SEVERITY_INFO, 0, str)
#define CLOG_STR_WARN(clg_ref, str) CLOG_STR_AT_SEVERITY(clg_ref, CLG_SEVERITY_WARN, 0, str)
#define CLOG_STR_ERROR(clg_ref, str) CLOG_STR_AT_SEVERITY(clg_ref, CLG_SEVERITY_ERROR, 0, str)
#define CLOG_STR_FATAL(clg_ref, str) CLOG_STR_AT_SEVERITY(clg_ref, CLG_SEVERITY_FATAL, 0, str)

/* Allocated string which is immediately freed. */
#ifdef DEBUG
#  define CLOG_STR_DEBUG_N(clg_ref, log_level, str) \
    CLOG_STR_AT_SEVERITY_N(clg_ref, CLG_SEVERITY_DEBUG, log_level, str)
#else
#  define CLOG_STR_DEBUG_N(clg_ref, log_level, str) \
    do { \
    } while (false)
#endif  // DEBUG
#define CLOG_STR_VERBOSE_N(clg_ref, log_level, str) \
  CLOG_STR_AT_SEVERITY(clg_ref, CLG_SEVERITY_VERBOSE, log_level, str)
#define CLOG_STR_INFO_N(clg_ref, str) CLOG_STR_AT_SEVERITY_N(clg_ref, CLG_SEVERITY_INFO, 0, str)
#define CLOG_STR_WARN_N(clg_ref, str) CLOG_STR_AT_SEVERITY_N(clg_ref, CLG_SEVERITY_WARN, 0, str)
#define CLOG_STR_ERROR_N(clg_ref, str) CLOG_STR_AT_SEVERITY_N(clg_ref, CLG_SEVERITY_ERROR, 0, str)
#define CLOG_STR_FATAL_N(clg_ref, str) CLOG_STR_AT_SEVERITY_N(clg_ref, CLG_SEVERITY_FATAL, 0, str)

#define LOG_EVERY_N_VARNAME(base, line) base##line
#define LOG_OCCURRENCES LOG_EVERY_N_VARNAME(occurrences_, __LINE__)
#define LOG_OCCURRENCES_MOD_N LOG_EVERY_N_VARNAME(occurrences_mod_n_, __LINE__)

/* every n times do something */
#define EVERY_N(n, what_to_do) \
  static int LOG_OCCURRENCES = 0, LOG_OCCURRENCES_MOD_N = 0; \
  ++LOG_OCCURRENCES; \
  if (++LOG_OCCURRENCES_MOD_N > n) \
    LOG_OCCURRENCES_MOD_N -= n; \
  if (LOG_OCCURRENCES_MOD_N == 1) \
  what_to_do

/* same as CLOG_VERBOSE, but every n times */
#define CLOG_VERBOSE_EVERY_N(clg_ref, log_level, n, ...) \
  EVERY_N(n, CLOG_VERBOSE(clg_ref, log_level, __VA_ARGS__))
/* same as CLOG_STR_VERBOSE, but every n times */
#define CLOG_STR_VERBOSE_EVERY_N(clg_ref, log_level, n, str) \
  EVERY_N(n, CLOG_STR_VERBOSE(clg_ref, log_level, str))
/* same as CLOG_STR_VERBOSE_N, but every n times */
#define CLOG_STR_VERBOSE_N_EVERY_N(clg_ref, log_level, n, str) \
  EVERY_N(n, CLOG_STR_VERBOSE_N(clg_ref, log_level, str))

#ifdef __cplusplus
}
#endif

#endif /* __CLG_LOG_H__ */
