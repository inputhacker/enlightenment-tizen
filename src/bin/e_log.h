#ifdef E_TYPEDEFS

#else
#ifndef E_LOG_H
#define E_LOG_H

#ifdef HAVE_DLOG
# include <dlog.h>
#  ifdef LOG_TAG
#   undef LOG_TAG
#  endif
# define LOG_TAG "E20"
#endif

#ifdef E_LOGGING
#undef DBG
#undef INF
#undef WRN
#undef ERR
#undef CRI
#define DBG(...)            EINA_LOG_DOM_DBG(e_log_dom, __VA_ARGS__)
#define INF(...)            EINA_LOG_DOM_INFO(e_log_dom, __VA_ARGS__)
#define WRN(...)            EINA_LOG_DOM_WARN(e_log_dom, __VA_ARGS__)
#define ERR(...)            EINA_LOG_DOM_ERR(e_log_dom, __VA_ARGS__)
#define CRI(...)            EINA_LOG_DOM_CRIT(e_log_dom, __VA_ARGS__)

#undef PRCTL
#undef PRCTL_BACKTRACE
#undef DLOG_BACKTRACE

#ifdef ENABLE_FUNCTION_TRACE
# include <sys/prctl.h>
# undef PR_TASK_PERF_USER_TRACE
# define PR_TASK_PERF_USER_TRACE 666
# define PRCTL(str, x...) \
   ({ \
    char __buf__[256]; \
    snprintf(__buf__, sizeof(__buf__), str, ##x); \
    prctl(PR_TASK_PERF_USER_TRACE, __buf__, strlen(__buf__)); \
    })

# include <execinfo.h>
# define PRCTL_BACKTRACE()                               \
   ({                                                    \
    void *__buf__[256];                                  \
    char **__strings__;                                  \
    int __nptrs__, i;                                    \
    __nptrs__ = backtrace(__buf__, 256);                 \
    __strings__ = backtrace_symbols(__buf__, __nptrs__); \
    if (__strings__)                                     \
    {                                                    \
       for(i = 0 ; i < __nptrs__ ; ++i)                  \
       PRCTL("%s", __strings__[i]);                      \
       free(__strings__);                                \
    }                                                    \
})

# ifdef HAVE_DLOG
#  define DLOG_BACKTRACE()                               \
   ({                                                    \
    void *__buf__[256];                                  \
    char **__strings__;                                  \
    int __nptrs__, i;                                    \
    __nptrs__ = backtrace(__buf__, 256);                 \
    __strings__ = backtrace_symbols(__buf__, __nptrs__); \
    if (__strings__)                                     \
    {                                                    \
       for(i = 0 ; i < __nptrs__ ; ++i)                  \
       dlog_print(DLOG_ERROR, LOG_TAG, "%s", __strings__[i]); \
       free(__strings__);                                \
    }                                                    \
})
# endif
#else
# define PRCTL
# define PRCTL_BACKTRACE
# define DLOG_BACKTRACE
#endif

# if (E_LOGGING >= 2) /* new version */
#  define ELOG(t, ec)                                       \
   do                                                       \
     {                                                      \
        if (ec)                                             \
          INF("EWL|%20.20s|w:0x%08zx|ec:%8p|",              \
              (t), (e_client_util_win_get(ec)), (ec));      \
        else                                                \
          INF("EWL|%20.20s|            |             |",    \
              (t));                                         \
     }                                                      \
   while (0)

#  define ELOGF(t, f, ec, x...)                             \
   do                                                       \
     {                                                      \
        if (ec)                                             \
          INF("EWL|%20.20s|w:0x%08zx|ec:%8p|"f,             \
              (t), (e_client_util_win_get(ec)), (ec), ##x); \
        else                                                \
          INF("EWL|%20.20s|            |             |"f,   \
              (t), ##x);                                    \
     }                                                      \
   while (0)
# else /* old version */
#  define ELOG(t, cp, ec)                                   \
   do                                                       \
     {                                                      \
        if (ec)                                             \
          INF("EWL|%20.20s|w:0x%08zx|ec:%8p|",              \
              (t), (e_client_util_win_get(ec)), (ec));      \
        else                                                \
          INF("EWL|%20.20s|            |             |",    \
              (t));                                         \
     }                                                      \
   while (0)

#  define ELOGF(t, f, cp, ec, x...)                         \
   do                                                       \
     {                                                      \
        if (ec)                                             \
          INF("EWL|%20.20s|w:0x%08zx|ec:%8p|"f,             \
              (t), (e_client_util_win_get(ec)), (ec), ##x); \
        else                                                \
          INF("EWL|%20.20s|            |             |"f,   \
              (t), ##x);                                    \
     }                                                      \
   while (0)
# endif /* end of if (E_LOGGING >= 2) */

typedef enum _E_Log_Type
{
   E_LOG_TYPE_DEFAULT = 0,
   E_LOG_TYPE_SYSTEM  = 1,
} E_Log_Type;

extern E_API int e_log_dom;

#ifdef HAVE_DLOG
EINTERN void e_log_dlog_enable(Eina_Bool enable);
#endif
EINTERN Eina_Bool e_log_path_set(const char *path);

EINTERN int e_log_init(void);
EINTERN int e_log_shutdown(void);
#else
#undef DBG
#undef INF
#undef WRN
#undef ERR
#undef CRI
#undef ELOG
#undef ELOGF
#define DBG(...)            do { printf(__VA_ARGS__); putc('\n', stdout); } while(0)
#define INF(...)            do { printf(__VA_ARGS__); putc('\n', stdout); } while(0)
#define WRN(...)            do { printf(__VA_ARGS__); putc('\n', stdout); } while(0)
#define ERR(...)            do { printf(__VA_ARGS__); putc('\n', stdout); } while(0)
#define CRI(...)            do { printf(__VA_ARGS__); putc('\n', stdout); } while(0)
#define ELOG(...)           ;
#define ELOGF(...)          ;

#undef PRCTL
#undef PRCTL_BACKTRACE
#undef DLOG_BACKTRACE
#define PRCTL
#define PRCTL_BACKTRACE
#define DLOG_BACKTRACE
#endif

#endif
#endif
