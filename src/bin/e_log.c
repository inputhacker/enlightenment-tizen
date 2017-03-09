#include "e.h"

E_API int e_log_dom = -1;

static const char *_names[] = {
   "CRI",
   "ERR",
   "WRN",
   "INF",
   "DBG",
};

#ifdef HAVE_DLOG
static Eina_Bool _dlog_enabled = EINA_FALSE;
#endif

static void
_e_log_cb(const Eina_Log_Domain *d, Eina_Log_Level level, const char *file, const char *fnc EINA_UNUSED, int line, const char *fmt, void *data EINA_UNUSED, va_list args)
{
   const char *color;
#ifdef HAVE_DLOG
   if (_dlog_enabled)
     {
        int log_level, len = 0;
        const char buf[512];
        char tmp_log_level[512];

        len =  sizeof(tmp_log_level) - 1;
        tmp_log_level[len] = '\0';

        switch (level)
          {
           case EINA_LOG_LEVEL_CRITICAL:
              log_level = DLOG_ERROR;
              strncpy(tmp_log_level, "FATAL", len);
              break;
           case EINA_LOG_LEVEL_ERR:
              log_level = DLOG_ERROR;
              strncpy(tmp_log_level, "ERROR", len);
              break;
           case EINA_LOG_LEVEL_WARN:
              log_level = DLOG_WARN;
              strncpy(tmp_log_level, "WARNING", len);
              break;
           case EINA_LOG_LEVEL_INFO:
              log_level = DLOG_INFO;
              strncpy(tmp_log_level, "INFO", len);
              break;
           case EINA_LOG_LEVEL_DBG:
              log_level = DLOG_DEBUG;
              strncpy(tmp_log_level, "DEBUG", len);
              break;
           default:
              log_level = DLOG_VERBOSE;
              strncpy(tmp_log_level, "VERBOSE", len);
              break;
          }

        vsnprintf((char *)buf, sizeof(buf), fmt, args);

        if (e_config)
          {
             if (e_config->log_type == E_LOG_TYPE_SYSTEM)
               {
                  print_system_log(log_level, LOG_TAG,
                                   "%s<%s> %30.30s:%04d %s",
                                   _names[level > EINA_LOG_LEVEL_DBG ? EINA_LOG_LEVEL_DBG : level],
                                   d->domain_str,file, line, buf);
               }
             else
               {
                  dlog_print(log_level, LOG_TAG,
                             "%s<%s> %30.30s:%04d %s",
                             _names[level > EINA_LOG_LEVEL_DBG ? EINA_LOG_LEVEL_DBG : level],
                             d->domain_str,file, line, buf);
               }
          }
        else
          {
             dlog_print(log_level, LOG_TAG,
                        "%s<%s> %30.30s:%04d %s",
                        _names[level > EINA_LOG_LEVEL_DBG ? EINA_LOG_LEVEL_DBG : level],
                        d->domain_str,file, line, buf);
          }
        return;
     }
#endif
   color = eina_log_level_color_get(level);

   fprintf(stdout,
           "%s%s<" EINA_COLOR_RESET "%s%s>" EINA_COLOR_RESET "%30.30s:%04d" EINA_COLOR_RESET " ",
           color, _names[level > EINA_LOG_LEVEL_DBG ? EINA_LOG_LEVEL_DBG : level],
           d->domain_str, color, file, line);
   vfprintf(stdout, fmt, args);
   putc('\n', stdout);
}

#ifdef HAVE_DLOG
static void
_e_log_wayland_dlog(const char *format, va_list args)
{
   if (e_config)
     {
        if (e_config->log_type == E_LOG_TYPE_SYSTEM)
          vprint_system_log(DLOG_INFO, LOG_TAG, format, args);
        else
          dlog_vprint(DLOG_INFO, LOG_TAG, format, args);
     }
   else
     dlog_vprint(DLOG_INFO, LOG_TAG, format, args);
}

static void
_e_log_wayland_stderr(const char *format, va_list args)
{
   vfprintf(stderr, format, args);
}

EINTERN void
e_log_dlog_enable(Eina_Bool enable)
{
   if (_dlog_enabled == enable) return;

   _dlog_enabled = enable;

   if (_dlog_enabled)
     wl_log_set_handler_server(_e_log_wayland_dlog);
   else
     wl_log_set_handler_server(_e_log_wayland_stderr);
}
#endif

EINTERN Eina_Bool
e_log_path_set(const char *path)
{
   int  log_fd = -1;
   FILE *log_fl;

   if (!path)
     {
        ERR("no eina-log-path");
        return EINA_FALSE;
     }

   log_fl = fopen(path, "a");
   if (!log_fl)
     {
        ERR("failed: open file(%s)\n", path);
        return EINA_FALSE;
     }

   fflush(stdout);
   close(STDOUT_FILENO);

   fflush(stderr);
   close(STDERR_FILENO);

   setvbuf(log_fl, NULL, _IOLBF, 512);
   log_fd = fileno(log_fl);

   dup2(log_fd, STDOUT_FILENO);
   dup2(log_fd, STDERR_FILENO);

   fclose(log_fl);

   return EINA_TRUE;
}

EINTERN int
e_log_init(void)
{
   const char *s;

   e_log_dom = eina_log_domain_register("e", EINA_COLOR_WHITE);
   eina_log_print_cb_set(_e_log_cb, NULL);
   eina_log_domain_level_set("e", 3);

#ifdef HAVE_DLOG
   if (getenv("E_LOG_DLOG_ENABLE"))
     e_log_dlog_enable(EINA_TRUE);
#endif

   s = getenv("E_LOG_FILE_PATH");
   if (s)
     {
#ifdef HAVE_DLOG
        e_log_dlog_enable(EINA_FALSE);
#endif
        e_log_path_set(s);
     }

   return 1;
}

EINTERN int
e_log_shutdown(void)
{
   eina_log_domain_unregister(e_log_dom);
   e_log_dom = -1;
   return 0;
}

