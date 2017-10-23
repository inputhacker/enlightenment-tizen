#include "e.h"

/* TODO List:
 *
 * * add module types/classes
 * * add list of exclusions that a module can't work with Api
 *
 */

/* local subsystem functions */
static void _e_module_free(E_Module *m);
static void _e_module_dialog_disable_defer(const char *title, const char *body, E_Module *m);
static void _e_module_dialog_disable_create(const char *title, const char *body, E_Module *m);
static void _e_module_cb_dialog_disable(void *data, E_Dialog *dia);
static void _e_module_event_update_free(void *data, void *event);
static void _e_module_create_wm_start(void);
static int  _e_module_sort_priority(const void *d1, const void *d2);
static Eina_Bool _e_module_cb_idler(void *data);

/* local subsystem globals */
static Eina_List *_e_modules = NULL;
static Eina_Hash *_e_modules_hash = NULL;
static Eina_List *_e_modules_delayed = NULL;
static Eina_Bool _e_modules_initting = EINA_FALSE;
static Eina_Bool _e_modules_init_end = EINA_FALSE;

static Eina_List *_e_module_path_lists = NULL;
static Eina_List *handlers = NULL;
static Eina_Hash *_e_module_path_hash = NULL;

static Ecore_Idle_Enterer *_e_module_idler = NULL;
static int _e_module_delayed_load_idle_cnt = 0;

E_API int E_EVENT_MODULE_UPDATE = 0;
E_API int E_EVENT_MODULE_INIT_END = 0;
E_API int E_EVENT_MODULE_DEFER_JOB = 0;

static Eina_Stringshare *mod_src_path = NULL;
static Eina_List *deferred_dialogs = NULL;

typedef struct _Defer_Dialog
{
   const char *title;
   const char *body;
   E_Module *m;
} Defer_Dialog;

static Eina_Bool
_module_filter_cb(void *d EINA_UNUSED, Eio_File *ls EINA_UNUSED, const Eina_File_Direct_Info *info)
{
   struct stat st;

   if (lstat(info->path, &st)) return EINA_FALSE;
   return (info->path[info->name_start] != '.');
}

static void
_module_main_cb(void *d, Eio_File *ls EINA_UNUSED, const Eina_File_Direct_Info *info)
{
   Eina_Stringshare *s;

   s = eina_hash_set(_e_module_path_hash, info->path + info->name_start, eina_stringshare_add(info->path));
   if (!s) return;
   if (!d)
     {
        if (!strstr(s, e_user_dir_get()))
          INF("REPLACING DUPLICATE MODULE PATH: %s -> %s", s, info->path);
        else
          {
             INF("NOT REPLACING DUPLICATE MODULE PATH: %s -X> %s", s, info->path);
             s = eina_hash_set(_e_module_path_hash, info->path + info->name_start, s);
          }
     }
   eina_stringshare_del(s);
}

static void
_module_done_cb(void *d EINA_UNUSED, Eio_File *ls)
{
   _e_module_path_lists = eina_list_remove(_e_module_path_lists, ls);
   if (_e_module_path_lists) return;
   if (_e_modules_initting) e_module_all_load();
   else if (!_e_modules_init_end)
     {
        ecore_event_add(E_EVENT_MODULE_INIT_END, NULL, NULL, NULL);
        _e_modules_init_end = EINA_TRUE;
     }
}

static void
_module_error_cb(void *d EINA_UNUSED, Eio_File *ls, int error EINA_UNUSED)
{
   _e_module_path_lists = eina_list_remove(_e_module_path_lists, ls);
   if (_e_module_path_lists) return;
   if (_e_modules_initting) e_module_all_load();
}

static Eina_Bool
_module_is_nosave(const char *name)
{
   const char *blacklist[] =
   {
      "comp",
      "conf_comp",
      "xwayland",
   };
   unsigned int i;

   for (i = 0; i < EINA_C_ARRAY_LENGTH(blacklist); i++)
     if (eina_streq(name, blacklist[i])) return EINA_TRUE;
   return !strncmp(name, "wl_", 3); //block wl_* modules from being saved
}

static Eina_Bool
_module_is_important(const char *name)
{
   const char *list[] =
   {
      "wl_desktop_shell",
      "wl_drm",
   };
   unsigned int i;

   for (i = 0; i < EINA_C_ARRAY_LENGTH(list); i++)
     if (eina_streq(name, list[i])) return EINA_TRUE;
   return EINA_FALSE;
}

/* externally accessible functions */
EINTERN int
e_module_init(void)
{
   Eina_List *module_paths;
   Eina_List *next_path;
   E_Path_Dir *epd;
   Eio_File *ls;

   if (_e_modules_hash) return 1;

   E_EVENT_MODULE_UPDATE = ecore_event_type_new();
   E_EVENT_MODULE_INIT_END = ecore_event_type_new();
   E_EVENT_MODULE_DEFER_JOB = ecore_event_type_new();

   _e_module_path_hash = eina_hash_string_superfast_new((Eina_Free_Cb)eina_stringshare_del);
   _e_modules_hash = eina_hash_string_superfast_new(NULL);

   if (!mod_src_path)
     {
        const char *src_path = getenv("E_MODULE_SRC_PATH");
        if (src_path)
          {
             char buf_p[PATH_MAX];
             snprintf(buf_p, sizeof(buf_p), "%s", src_path);
             mod_src_path = eina_stringshare_add((const char*)buf_p);
          }
     }

   if (mod_src_path)
     {
        if (ecore_file_is_dir(mod_src_path))
          {
             ls = eio_file_direct_ls(mod_src_path, _module_filter_cb, _module_main_cb, _module_done_cb, _module_error_cb, NULL);
             _e_module_path_lists = eina_list_append(_e_module_path_lists, ls);
             return 1;
          }
     }

   module_paths = e_path_dir_list_get(path_modules);
   EINA_LIST_FOREACH(module_paths, next_path, epd)
     {
        if (ecore_file_is_dir(epd->dir))
          {
             void *data = NULL;

             data = (intptr_t*)(long)!!strstr(epd->dir, e_user_dir_get());
             ls = eio_file_direct_ls(epd->dir, _module_filter_cb, _module_main_cb, _module_done_cb, _module_error_cb, data);
             _e_module_path_lists = eina_list_append(_e_module_path_lists, ls);
          }
     }
   e_path_dir_list_free(module_paths);

   return 1;
}

EINTERN int
e_module_shutdown(void)
{
   E_Module *m;

#ifdef HAVE_VALGRIND
   /* do a leak check now before we dlclose() all those plugins, cause
    * that means we won't get a decent backtrace to leaks in there
    */
   VALGRIND_DO_LEAK_CHECK
#endif

   /* do not use EINA_LIST_FREE! e_object_del modifies list */
   while (_e_modules)
     {
        m = _e_modules->data;
        if ((m) && (m->enabled) && !(m->error))
          {
             // if (m->func.save) m->func.save(m); dont' save config, there is a possibility
             // that the file save operation at the time of system shutdown
             // may break when filesystem unmount.
             if (m->func.shutdown) m->func.shutdown(m);
             m->enabled = 0;
          }
        e_object_del(E_OBJECT(m));
     }

   E_FREE_FUNC(_e_module_path_hash, eina_hash_free);
   E_FREE_FUNC(_e_modules_hash, eina_hash_free);
   E_FREE_LIST(handlers, ecore_event_handler_del);
   E_FREE_LIST(_e_module_path_lists, eio_file_cancel);

   return 1;
}

E_API void
e_module_all_load(void)
{
   Eina_List *l, *ll;
   E_Config_Module *em;
   char buf[128];

   TRACE_DS_BEGIN(MODULE:ALL LOAD);

   _e_modules_initting = EINA_TRUE;
   if (_e_module_path_lists)
     {
        TRACE_DS_END();
        return;
     }

   e_config->modules =
     eina_list_sort(e_config->modules, 0, _e_module_sort_priority);

   EINA_LIST_FOREACH_SAFE(e_config->modules, l, ll, em)
     {
        if ((!em) || (!em->name)) continue;

        if (_module_is_nosave(em->name))
          {
             eina_stringshare_del(em->name);
             e_config->modules = eina_list_remove_list(e_config->modules, l);
             free(em);
             continue;
          }

        if ((em->delayed) && (em->enabled))
          {
             if (!_e_module_idler)
               _e_module_idler = ecore_idle_enterer_add(_e_module_cb_idler, NULL);
             _e_modules_delayed =
                eina_list_append(_e_modules_delayed,
                                 eina_stringshare_add(em->name));
             PRCTL("[Winsys] Delayled module list added: %s", em->name);
          }
        else if (em->enabled)
          {
             E_Module *m;

             if (eina_hash_find(_e_modules_hash, em->name)) continue;

             e_util_env_set("E_MODULE_LOAD", em->name);
             snprintf(buf, sizeof(buf), _("Loading Module: %s"), em->name);
             e_init_status_set(buf);

             PRCTL("[Winsys] start of Loading Module: %s", em->name);
             TRACE_DS_BEGIN(MODULE:LOAD MODULE %s, em->name);
             m = e_module_new(em->name);
             if (m)
               {
                  e_module_enable(m);
                  PRCTL("[Winsys] end of Loading Module: %s", em->name);
                  TRACE_DS_END();
               }
             else
               {
                  PRCTL("[Winsys] end of Loading Module: Failed to load %s", em->name);
                  TRACE_DS_END();
               }
          }
     }
   PRCTL("[Winsys] Non-delayed modules loaded");

   if (e_config->create_wm_start)
     _e_module_create_wm_start();

   if (!_e_modules_delayed)
     {
        ecore_event_add(E_EVENT_MODULE_INIT_END, NULL, NULL, NULL);
        _e_modules_init_end = EINA_TRUE;
        _e_modules_initting = EINA_FALSE;
     }

   e_main_hook_call(E_MAIN_HOOK_MODULE_LOAD_DONE);

   unsetenv("E_MODULE_LOAD");

   TRACE_DS_END();
}

E_API Eina_Bool
e_module_loading_get(void)
{
   return !_e_modules_init_end;
}

E_API E_Module *
e_module_new(const char *name)
{
   E_Module *m;
   char buf[PATH_MAX];
   char body[4096], title[1024];
   const char *modpath = NULL;
   char *s;
   Eina_List *l, *ll;
   E_Config_Module *em;
   int in_list = 0;
   char str[1024];

   if (!name) return NULL;
   m = E_OBJECT_ALLOC(E_Module, E_MODULE_TYPE, _e_module_free);
   if (!m) return NULL;

   snprintf(str, sizeof(str), "\t%s Module Open", name);
   e_main_ts_begin(str);

   if (name[0] != '/')
     {
        Eina_Stringshare *path = NULL;

        if (!mod_src_path)
          {
             const char *src_path = getenv("E_MODULE_SRC_PATH");
             if (src_path)
               {
                  char buf_p[PATH_MAX];
                  snprintf(buf_p, sizeof(buf_p), "%s", src_path);
                  mod_src_path = eina_stringshare_add((const char*)buf_p);
               }
          }
        if (mod_src_path)
          {
             snprintf(buf, sizeof(buf), "%s/%s/.libs/module.so", mod_src_path, name);
             modpath = eina_stringshare_add(buf);
          }
        if (!modpath)
          path = eina_hash_find(_e_module_path_hash, name);
        if (path)
          {
             snprintf(buf, sizeof(buf), "%s/%s/module.so", path, MODULE_ARCH);
             modpath = eina_stringshare_add(buf);
          }
        else if (!modpath)
          {
             snprintf(buf, sizeof(buf), "%s/%s/module.so", name, MODULE_ARCH);
             modpath = e_path_find(path_modules, buf);
          }
     }
   else if (eina_str_has_extension(name, ".so"))
     modpath = eina_stringshare_add(name);
   if (!modpath)
     {
        snprintf(body, sizeof(body),
                 _("There was an error loading the module named: %s<br>"
                   "No module named %s could be found in the<br>"
                   "module search directories.<br>"), name, buf);
        _e_module_dialog_disable_create(_("Error loading Module"), body, m);
        m->error = 1;
        goto init_done;
     }
   m->handle = dlopen(modpath, (RTLD_NOW | RTLD_GLOBAL));
   if (!m->handle)
     {
        snprintf(body, sizeof(body),
                 _("There was an error loading the module named: %s<br>"
                   "The full path to this module is:<br>"
                   "%s<br>"
                   "The error reported was:<br>"
                   "%s<br>"), name, buf, dlerror());
        _e_module_dialog_disable_create(_("Error loading Module"), body, m);
        m->error = 1;
        goto init_done;
     }
   m->api = dlsym(m->handle, "e_modapi");
   m->func.init = dlsym(m->handle, "e_modapi_init");
   m->func.shutdown = dlsym(m->handle, "e_modapi_shutdown");
   m->func.save = dlsym(m->handle, "e_modapi_save");

   if ((!m->func.init) || (!m->api))
     {
        snprintf(body, sizeof(body),
                 _("There was an error loading the module named: %s<br>"
                   "The full path to this module is:<br>"
                   "%s<br>"
                   "The error reported was:<br>"
                   "%s<br>"),
                 name, buf, _("Module does not contain all needed functions"));
        _e_module_dialog_disable_create(_("Error loading Module"), body, m);
        m->api = NULL;
        m->func.init = NULL;
        m->func.shutdown = NULL;
        m->func.save = NULL;

        dlclose(m->handle);
        m->handle = NULL;
        m->error = 1;
        goto init_done;
     }
   if (m->api->version != E_MODULE_API_VERSION)
     {
        snprintf(body, sizeof(body),
                 _("Module API Error<br>Error initializing Module: %s<br>"
                   "It requires a module API version of: %i.<br>"
                   "The module API advertized by Enlightenment is: %i.<br>"),
                 _(m->api->name), m->api->version, E_MODULE_API_VERSION);

        snprintf(title, sizeof(title), _("Enlightenment %s Module"),
                 _(m->api->name));

        _e_module_dialog_disable_create(title, body, m);
        m->api = NULL;
        m->func.init = NULL;
        m->func.shutdown = NULL;
        m->func.save = NULL;
        dlclose(m->handle);
        m->handle = NULL;
        m->error = 1;
        goto init_done;
     }

init_done:

   _e_modules = eina_list_append(_e_modules, m);
   if (!_e_modules_hash)
     {
        /* wayland module preloading */
        if (!e_module_init())
          CRI("WTFFFFF");
     }
   eina_hash_add(_e_modules_hash, name, m);
   m->name = eina_stringshare_add(name);
   if (modpath)
     {
        s = ecore_file_dir_get(modpath);
        if (s)
          {
             char *s2;

             s2 = ecore_file_dir_get(s);
             free(s);
             if (s2)
               {
                  m->dir = eina_stringshare_add(s2);
                  free(s2);
               }
          }
     }
   EINA_LIST_FOREACH_SAFE(e_config->modules, l, ll, em)
     {
        if (!em) continue;
        if (em->name == m->name)
          {
             if (in_list)
               {
                  /* duplicate module config */
                  e_config->modules = eina_list_remove_list(e_config->modules, l);
                  eina_stringshare_del(em->name);
                  free(em);
               }
             else
               in_list = 1;
          }
     }
   if (!in_list)
     {
        E_Config_Module *module;

        module = E_NEW(E_Config_Module, 1);
        if (module)
          {
             module->name = eina_stringshare_add(m->name);
             module->enabled = 0;
             e_config->modules = eina_list_append(e_config->modules, module);
          }
        e_config_save_queue();
     }
   if (modpath) eina_stringshare_del(modpath);

   snprintf(str, sizeof(str), "\t%s Module Open Done m:%p", name, m);
   e_main_ts_end(str);

   return m;
}

E_API int
e_module_save(E_Module *m)
{
   E_OBJECT_CHECK_RETURN(m, 0);
   E_OBJECT_TYPE_CHECK_RETURN(m, E_MODULE_TYPE, 0);
   if ((!m->enabled) || (m->error)) return 0;
   return m->func.save ? m->func.save(m) : 1;
}

E_API const char *
e_module_dir_get(E_Module *m)
{
   E_OBJECT_CHECK_RETURN(m, NULL);
   E_OBJECT_TYPE_CHECK_RETURN(m, E_MODULE_TYPE, 0);
   return m->dir;
}

E_API int
e_module_enable(E_Module *m)
{
   Eina_List *l;
   E_Event_Module_Update *ev;
   E_Config_Module *em;
   char str[1024];

   E_OBJECT_CHECK_RETURN(m, 0);
   E_OBJECT_TYPE_CHECK_RETURN(m, E_MODULE_TYPE, 0);
   if ((m->enabled) || (m->error)) return 0;

   snprintf(str, sizeof(str), "\t%s Module Enable", m->name);
   e_main_ts_begin(str);

   m->data = m->func.init(m);
   if (m->data)
     {
        m->enabled = 1;
        EINA_LIST_FOREACH(e_config->modules, l, em)
          {
             if (!em) continue;
             if (!e_util_strcmp(em->name, m->name))
               {
                  em->enabled = 1;
                  e_config_save_queue();

                  ev = E_NEW(E_Event_Module_Update, 1);
                  if (!ev) break;
                  ev->name = eina_stringshare_ref(em->name);
                  ev->enabled = 1;
                  ecore_event_add(E_EVENT_MODULE_UPDATE, ev,
                                  _e_module_event_update_free, NULL);
                  break;
               }
          }

        snprintf(str, sizeof(str), "\t%s Module Enable Done 1", m->name);
        e_main_ts_end(str);

        return 1;
     }

   snprintf(str, sizeof(str), "\t%s Module Enable Done 0", m->name);
   e_main_ts_end(str);

   return 0;
}

E_API int
e_module_disable(E_Module *m)
{
   E_Event_Module_Update *ev;
   Eina_List *l;
   E_Config_Module *em;
   int ret;

   E_OBJECT_CHECK_RETURN(m, 0);
   E_OBJECT_TYPE_CHECK_RETURN(m, E_MODULE_TYPE, 0);
   if ((!m->enabled) || (m->error)) return 0;
   if ((!stopping) && _module_is_important(m->name)) return 0;
   ret = m->func.shutdown ? m->func.shutdown(m) : 1;
   m->data = NULL;
   m->enabled = 0;
   EINA_LIST_FOREACH(e_config->modules, l, em)
     {
        if (!em) continue;
        if (!e_util_strcmp(em->name, m->name))
          {
             em->enabled = 0;
             e_config_save_queue();

             ev = E_NEW(E_Event_Module_Update, 1);
             if (!ev) break;
             ev->name = eina_stringshare_ref(em->name);
             ev->enabled = 0;
             ecore_event_add(E_EVENT_MODULE_UPDATE, ev,
                             _e_module_event_update_free, NULL);
             break;
          }
     }
   return ret;
}

E_API int
e_module_enabled_get(E_Module *m)
{
   E_OBJECT_CHECK_RETURN(m, 0);
   E_OBJECT_TYPE_CHECK_RETURN(m, E_MODULE_TYPE, 0);
   return m->enabled;
}

E_API int
e_module_save_all(void)
{
   Eina_List *l;
   E_Module *m;
   int ret = 1;

   EINA_LIST_FOREACH(_e_modules, l, m)
     {
        e_object_ref(E_OBJECT(m));
        if ((m->enabled) && (!m->error))
          {
             if (m->func.save && (!m->func.save(m))) ret = 0;
          }
        e_object_unref(E_OBJECT(m));
     }
   return ret;
}

E_API E_Module *
e_module_find(const char *name)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(name, NULL);
   return eina_hash_find(_e_modules_hash, name);
}

E_API Eina_List *
e_module_list(void)
{
   return _e_modules;
}

E_API void
e_module_delayed_set(E_Module *m, int delayed)
{
   Eina_List *l;
   E_Config_Module *em;

   EINA_LIST_FOREACH(e_config->modules, l, em)
     {
        if (!em) continue;
        if (!e_util_strcmp(m->name, em->name))
          {
             if (em->delayed != delayed)
               {
                  em->delayed = delayed;
                  e_config_save_queue();
               }
             break;
          }
     }
}

E_API void
e_module_priority_set(E_Module *m, int priority)
{
   /* Set the loading order for a module.
      More priority means load earlier */
   Eina_List *l;
   E_Config_Module *em;

   EINA_LIST_FOREACH(e_config->modules, l, em)
     {
        if (!em) continue;
        if (!e_util_strcmp(m->name, em->name))
          {
             if (em->priority != priority)
               {
                  em->priority = priority;
                  e_config_save_queue();
               }
             break;
          }
     }
}

static void
_e_module_dialog_disable_show(const char *title, const char *body, E_Module *m)
{
   E_Dialog *dia;
   char buf[4096];

   printf("MODULE ERR:\n%s\n", body);

   dia = e_dialog_new(NULL, "E", "_module_unload_dialog");

   snprintf(buf, sizeof(buf), "%s<br>%s", body,
            _("What action should be taken with this module?<br>"));

   if (!dia) return;

   e_dialog_title_set(dia, title);
   e_dialog_icon_set(dia, "enlightenment", 64);
   e_dialog_text_set(dia, buf);
   e_dialog_button_add(dia, _("Unload"), NULL, _e_module_cb_dialog_disable, m);
   e_dialog_button_add(dia, _("Keep"), NULL, NULL, NULL);
   //elm_win_center(dia->win, 1, 1);
   e_win_no_remember_set(dia->win, 1);
   e_dialog_show(dia);
}

E_API void
e_module_deferred_job(void)
{
   Defer_Dialog *dd;

   ecore_event_add(E_EVENT_MODULE_DEFER_JOB, NULL, NULL, NULL);

   if (!deferred_dialogs) return;

   EINA_LIST_FREE(deferred_dialogs, dd)
     {
        _e_module_dialog_disable_show(dd->title, dd->body, dd->m);
        eina_stringshare_del(dd->title);
        eina_stringshare_del(dd->body);
        E_FREE(dd);
     }
}

/* local subsystem functions */
static void
_e_module_free(E_Module *m)
{
   E_Config_Module *em;
   Eina_List *l;

   EINA_LIST_FOREACH(e_config->modules, l, em)
     {
        if (!em) continue;
        if (!e_util_strcmp(em->name, m->name))
          {
             e_config->modules = eina_list_remove(e_config->modules, em);
             if (em->name) eina_stringshare_del(em->name);
             E_FREE(em);
             break;
          }
     }

   if ((m->enabled) && (!m->error))
     {
        // if (m->func.save) m->func.save(m); dont' save config, there is a possibility
        // that the file save operation at the time of system shutdown
        // may break when filesystem unmount.
        if (m->func.shutdown) m->func.shutdown(m);
     }
   if (m->name) eina_stringshare_del(m->name);
   if (m->dir) eina_stringshare_del(m->dir);
// if (m->handle) dlclose(m->handle); DONT dlclose! causes problems with deferred callbacks for free etc. - when their code goes away!
   _e_modules = eina_list_remove(_e_modules, m);
   eina_hash_del(_e_modules_hash, m->name, m);
   free(m);
}

typedef struct Disable_Dialog
{
   char *title;
   char *body;
   E_Module *m;
} Disable_Dialog;

static void
_e_module_dialog_disable_defer(const char *title, const char *body, E_Module *m)
{
   Defer_Dialog *dd;

   dd = E_NEW(Defer_Dialog, 1);
   if (!dd)
     {
        ERR("Failed to allocate Defer_Dialog");
        return;
     }

   dd->title = eina_stringshare_add(title);
   dd->body = eina_stringshare_add(body);
   dd->m = m;

   deferred_dialogs = eina_list_append(deferred_dialogs, dd);
}

static Eina_Bool
_e_module_dialog_disable_timer(Disable_Dialog *dd)
{
   _e_module_dialog_disable_show(dd->title, dd->body, dd->m);
   free(dd->title);
   free(dd->body);
   free(dd);
   return EINA_FALSE;
}

static void
_e_module_dialog_disable_create(const char *title, const char *body, E_Module *m)
{
   Disable_Dialog *dd;

#ifdef ENABLE_QUICK_INIT
   if (!_e_modules_init_end)
     {
        _e_module_dialog_disable_defer(title, body, m);
        return;
     }
#endif

   dd = E_NEW(Disable_Dialog, 1);
   if (!dd) return;
   dd->title = strdup(title);
   dd->body = strdup(body);
   dd->m = m;
   ecore_timer_add(1.5, (Ecore_Task_Cb)_e_module_dialog_disable_timer, dd);
}

static void
_e_module_cb_dialog_disable(void *data, E_Dialog *dia)
{
   E_Module *m;

   m = data;
   e_module_disable(m);
   e_object_del(E_OBJECT(m));
   e_object_del(E_OBJECT(dia));
   e_config_save_queue();
}

static int
_e_module_sort_priority(const void *d1, const void *d2)
{
   const E_Config_Module *m1, *m2;

   m1 = d1;
   m2 = d2;
   return m2->priority - m1->priority;
}

static void
_e_module_event_update_free(void *data EINA_UNUSED, void *event)
{
   E_Event_Module_Update *ev;

   if (!(ev = event)) return;
   eina_stringshare_del(ev->name);
   E_FREE(ev);
}

static Eina_Bool
_e_module_cb_idler(void *data EINA_UNUSED)
{
   if (_e_module_delayed_load_idle_cnt < e_config->delayed_load_idle_count)
     {
        PRCTL("[Winsys] ::: delayed :::  _e_module_cb_idler enter [%d]", _e_module_delayed_load_idle_cnt);
        _e_module_delayed_load_idle_cnt++;

        return ECORE_CALLBACK_PASS_ON;
     }

   if (_e_modules_delayed)
     {
        const char *name;
        E_Module *m = NULL;

        name = eina_list_data_get(_e_modules_delayed);
        _e_modules_delayed =
           eina_list_remove_list(_e_modules_delayed, _e_modules_delayed);

        if (name) m = e_module_new(name);
        if (m)
          {
             PRCTL("[Winsys] ::: delayed ::: start of Loading Delayed Module %s", name ? name : "NONAME");
             e_module_enable(m);
             PRCTL("[Winsys] ::: delayed ::: end of Loading Delayed Module %s", name ? name : "NONAME");
          }
        eina_stringshare_del(name);
     }
   if (_e_modules_delayed)
     {
        e_util_wakeup();
        return ECORE_CALLBACK_PASS_ON;
     }

   ecore_event_add(E_EVENT_MODULE_INIT_END, NULL, NULL, NULL);

   _e_module_idler = NULL;
   return ECORE_CALLBACK_DONE;
}

static void
_e_module_create_wm_start(void)
{
   FILE *_wm_start_checker = NULL;
   const char *path_wm_start_run = "/run/wm_start";
   const char *path_wm_start_tmp = "/tmp/wm_start";

   if (!e_util_file_realpath_check(path_wm_start_run, EINA_TRUE))
     {
        WRN("%s is maybe link, so delete it\n", path_wm_start_run);
     }

   _wm_start_checker = fopen(path_wm_start_run, "wb");
   if (_wm_start_checker)
     {
        PRCTL("[Winsys] /run/wm_start is created");
        fclose(_wm_start_checker);
     }
   else
     {
        PRCTL("[Winsys] Failed to create /run/wm_start");
     }

   if (!e_util_file_realpath_check(path_wm_start_tmp, EINA_TRUE))
     {
        WRN("%s is maybe link, so delete it\n", path_wm_start_tmp);
     }

   _wm_start_checker = fopen(path_wm_start_tmp, "wb");
   if (_wm_start_checker)
     {
        PRCTL("[Winsys] /tmp/wm_start is created");
        fclose(_wm_start_checker);
     }
   else
     {
        PRCTL("[Winsys] Failed to create /tmp/wm_start");
     }
}
