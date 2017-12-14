#include "e.h"

static char *_e_user_homedir = NULL;
static size_t _e_user_homedir_len = 0;

/* externally accessible functions */
EINTERN int
e_user_init(void)
{
   char *d;

   /* e_user_shutdown will free for the d string */
   _e_user_homedir = d = e_util_env_get("HOME");
   if (!_e_user_homedir)
     {
        _e_user_homedir = "/tmp";
        _e_user_homedir_len = sizeof("/tmp") - 1;
        return 1;
     }

   _e_user_homedir_len = strlen(_e_user_homedir);
   while ((_e_user_homedir_len > 1) &&
          (d[_e_user_homedir_len - 1] == '/'))
     {
        _e_user_homedir_len--;
        d[_e_user_homedir_len] = '\0';
     }

   return 1;
}

EINTERN int
e_user_shutdown(void)
{
   E_FREE(_e_user_homedir);
   _e_user_homedir_len = 0;
   return 1;
}

E_API const char *
e_user_homedir_get(void)
{
   return _e_user_homedir;
}

/**
 * Concatenate '~/' and @a path.
 *
 * @return similar to snprintf(), this returns the number of bytes written or
 *     that would be required to write if greater or equal than size.
 */
E_API size_t
e_user_homedir_concat_len(char *dst, size_t size, const char *path, size_t path_len)
{
   return eina_str_join_len(dst, size, '/', _e_user_homedir, _e_user_homedir_len, path, path_len);
}

E_API size_t
e_user_homedir_concat(char *dst, size_t size, const char *path)
{
   return e_user_homedir_concat_len(dst, size, path, strlen(path));
}

/**
 * same as snprintf("~/"fmt, ...).
 */
E_API size_t
e_user_homedir_snprintf(char *dst, size_t size, const char *fmt, ...)
{
   size_t off, ret;
   va_list ap;

   va_start(ap, fmt);

   off = _e_user_homedir_len + 1;
   if (size < _e_user_homedir_len + 2)
     {
        if (size > 1)
          {
             memcpy(dst, _e_user_homedir, size - 1);
             dst[size - 1] = '\0';
          }
        ret = off + vsnprintf(dst + off, size - off, fmt, ap);
        va_end(ap);
        return ret;
     }

   memcpy(dst, _e_user_homedir, _e_user_homedir_len);
   dst[_e_user_homedir_len] = '/';

   ret = off + vsnprintf(dst + off, size - off, fmt, ap);
   va_end(ap);
   return ret;
}

static const char *_e_user_dir = NULL;
static size_t _e_user_dir_len = 0;

/**
 * Return ~/.e/e
 */
E_API const char *
e_user_dir_get(void)
{
   static char dir[PATH_MAX] = "";

   if (!dir[0])
     {
        char *d = e_util_env_get("E_HOME");
        if (d)
          {
             snprintf(dir, sizeof(dir), "%s/e", d);
             _e_user_dir_len = strlen(dir);
             E_FREE(d);
          }
        else
          {
#ifdef DOXDG             
             d = e_util_env_get("XDG_CONFIG_HOME");
             if (d)
               {
                  snprintf(dir, sizeof(dir), "%s/e", d);
                  _e_user_dir_len = strlen(dir);
                  E_FREE(d);
               }
             else
#endif               
               {
#ifdef DOXDG             
                  _e_user_dir_len = e_user_homedir_concat(dir, sizeof(dir),
                                                          ".config/e");
#else                  
                  _e_user_dir_len = e_user_homedir_concat(dir, sizeof(dir),
                                                          ".e/e");
#endif                  
               }
          }
        _e_user_dir = dir;
     }
   return dir;
}

/**
 * Concatenate '~/.e/e' and @a path.
 *
 * @return similar to snprintf(), this returns the number of bytes written or
 *     that would be required to write if greater or equal than size.
 */
E_API size_t
e_user_dir_concat_len(char *dst, size_t size, const char *path, size_t path_len)
{
   if (!_e_user_dir)
     e_user_dir_get();

   return eina_str_join_len(dst, size, '/', _e_user_dir, _e_user_dir_len, path, path_len);
}

E_API size_t
e_user_dir_concat(char *dst, size_t size, const char *path)
{
   return e_user_dir_concat_len(dst, size, path, strlen(path));
}

/**
 * same as snprintf("~/.e/e/"fmt, ...).
 */
E_API size_t
e_user_dir_snprintf(char *dst, size_t size, const char *fmt, ...)
{
   size_t off, ret;
   va_list ap;

   if (!_e_user_dir)
     e_user_dir_get();
   if (!_e_user_dir)
     return 0;

   va_start(ap, fmt);

   off = _e_user_dir_len + 1;
   if (size < _e_user_dir_len + 2)
     {
        if (size > 1)
          {
             memcpy(dst, _e_user_dir, size - 1);
             dst[size - 1] = '\0';
          }
        ret = off + vsnprintf(dst + off, size - off, fmt, ap);
        va_end(ap);
        return ret;
     }

   memcpy(dst, _e_user_dir, _e_user_dir_len);
   dst[_e_user_dir_len] = '/';

   ret = off + vsnprintf(dst + off, size - off, fmt, ap);
   va_end(ap);
   return ret;
}

