#ifdef E_TYPEDEFS

#else
#ifndef E_UTILS_H
#define E_UTILS_H

#define e_util_dialog_show(title, args...) \
{ \
   char __tmpbuf[4096]; \
   \
   snprintf(__tmpbuf, sizeof(__tmpbuf), ##args); \
   e_util_dialog_internal(title, __tmpbuf); \
}

/* These macros are used to just mark strings for translation, this is useful
 * for string lists which are not dynamically allocated
 */
#define _(str) (str)
#define P_(str, str_p, n) (str_p)
#define N_(str) (str)

E_API void         e_util_wakeup(void);
E_API void         e_util_env_set(const char *var, const char *val);
E_API int          e_util_glob_match(const char *str, const char *glob);
E_API int          e_util_glob_case_match(const char *str, const char *glob);
E_API int          e_util_strcasecmp(const char *s1, const char *s2);
E_API int          e_util_strcmp(const char *s1, const char *s2);
E_API int          e_util_both_str_empty(const char *s1, const char *s2);
E_API int          e_util_immortal_check(void);
E_API unsigned int e_util_icon_size_normalize(unsigned int desired);
E_API E_Client    *e_util_desk_client_above(E_Client *ec);
E_API E_Client    *e_util_desk_client_below(E_Client *ec);
E_API int          e_util_edje_collection_exists(const char *file, const char *coll);
E_API E_Dialog     *e_util_dialog_internal(const char *title, const char *txt);
E_API const char  *e_util_filename_escape(const char *filename);
//E_API char        *e_util_shell_env_path_eval(const char *path);
E_API char        *e_util_size_string_get(off_t size);
E_API char        *e_util_file_time_get(time_t ftime);
E_API Evas_Object *e_util_icon_add(const char *path, Evas *evas);
//E_API Evas_Object *e_util_desktop_icon_add(Efreet_Desktop *desktop, unsigned int size, Evas *evas);
E_API Evas_Object *e_util_icon_theme_icon_add(const char *icon_name, unsigned int size, Evas *evas);
E_API int          e_util_dir_check(const char *dir);
E_API void         e_util_defer_object_del(E_Object *obj);
E_API void         e_util_win_auto_resize_fill(Evas_Object *win);
/* check if loaded config version matches the current version, show a
   dialog warning if loaded version is older or newer than current */
E_API Eina_Bool    e_util_module_config_check(const char *module_name, int loaded, int current);

E_API Eina_Bool e_util_fullscreen_current_any(void);
E_API Eina_Bool e_util_fullscreen_any(void);
E_API const char *e_util_time_str_get(long int seconds);
E_API void e_util_size_debug_set(Evas_Object *obj, Eina_Bool enable);
//E_API Efreet_Desktop *e_util_terminal_desktop_get(void);

E_API char *e_util_string_append_char(char *str, size_t *size, size_t *len, char c);
E_API char *e_util_string_append_quoted(char *str, size_t *size, size_t *len, const char *src);
E_API Eina_Bool e_util_string_to_int(const char *str, int *num, int base);
E_API Eina_Bool e_util_string_to_int_token(const char *str, char **next, int *num, int base);
E_API Eina_Bool e_util_string_to_double(const char *str, double *num);

E_API void e_util_evas_objects_above_print(Evas_Object *o);
E_API void e_util_evas_objects_above_print_smart(Evas_Object *o);

E_API void e_util_string_list_free(Eina_List *l);

E_API void e_util_memclear(void *s, size_t n);

static inline void
e_util_pointer_center(const E_Client *ec)
{
   int x = 0, y = 0;

   if (ec->zone)
     x = ec->zone->x, y = ec->zone->y;
   if (e_config->focus_policy != E_FOCUS_CLICK)
     ecore_evas_pointer_warp(e_comp->ee,
                             x + ec->x + (ec->w / 2),
                             y + ec->y + (ec->h / 2));
}

static inline Eina_Bool
isedje(const Evas_Object *obj)
{
   return obj && !e_util_strcmp(evas_object_type_get(obj), "edje");
}

static inline Eina_Bool
dblequal(double a, double b)
{
   return fabs(a - b) < DBL_EPSILON;
}

#endif
#endif
