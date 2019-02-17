#include "e.h"
#ifdef __linux__
# include <sys/prctl.h>
#endif
#ifdef HAVE_SYSTEMD
# include <systemd/sd-daemon.h>
#endif

#define MAX_LEVEL 80

#define TS_DO
#ifdef TS_DO
# define TS(x)                                                    \
  {                                                               \
     t1 = ecore_time_unix_get();                                  \
     printf("ESTART: %1.5f [%1.5f] - %s\n", t1 - t0, t1 - t2, x); \
     t2 = t1;                                                     \
  }

# define TSB(x)                                                  \
  do {                                                           \
     TRACE_DS_BEGIN(ESTART: %s, x);                              \
     TS(x);                                                      \
  } while (0)
# define TSE(x)                                                  \
  do {                                                           \
     TRACE_DS_END();                                             \
     TS(x);                                                      \
  } while (0)
# define TSM(x)                                                  \
  do {                                                           \
     TRACE_DS_MARK(ESTART: %s, x);                               \
     TS(x);                                                      \
  } while (0)
static double t0, t1, t2;
#else
# define TS(x)
# define TSB(x)
# define TSE(x)
# define TSM(x)
#endif
/*
 * i need to make more use of these when i'm baffled as to when something is
 * up. other hooks:
 *
 *      void *(*__malloc_hook)(size_t size, const void *caller);
 *
 *      void *(*__realloc_hook)(void *ptr, size_t size, const void *caller);
 *
 *      void *(*__memalign_hook)(size_t alignment, size_t size,
 *                               const void *caller);
 *
 *      void (*__free_hook)(void *ptr, const void *caller);
 *
 *      void (*__malloc_initialize_hook)(void);
 *
 *      void (*__after_morecore_hook)(void);
 *

   static void my_init_hook(void);
   static void my_free_hook(void *p, const void *caller);

   static void (*old_free_hook)(void *ptr, const void *caller) = NULL;
   void (*__free_hook)(void *ptr, const void *caller);

   void (*__malloc_initialize_hook) (void) = my_init_hook;
   static void
   my_init_hook(void)
   {
   old_free_hook = __free_hook;
   __free_hook = my_free_hook;
   }

   //void *magicfree = NULL;

   static void
   my_free_hook(void *p, const void *caller)
   {
   __free_hook = old_free_hook;
   //   if ((p) && (p == magicfree))
   //     {
   //	printf("CAUGHT!!!!! %p ...\n", p);
   //	abort();
   //     }
   free(p);
   __free_hook = my_free_hook;
   }
 */

/* local function prototypes */
static void      _e_main_shutdown(int errcode);
static void      _e_main_shutdown_push(int (*func)(void));
static void      _e_main_parse_arguments(int argc, char **argv);
static Eina_Bool _e_main_cb_signal_exit(void *data EINA_UNUSED, int ev_type EINA_UNUSED, void *ev EINA_UNUSED);
static Eina_Bool _e_main_cb_signal_hup(void *data EINA_UNUSED, int ev_type EINA_UNUSED, void *ev EINA_UNUSED);
static int       _e_main_dirs_init(void);
static int       _e_main_dirs_shutdown(void);
static int       _e_main_path_init(void);
static int       _e_main_path_shutdown(void);
static int       _e_main_screens_init(void);
static int       _e_main_screens_shutdown(void);
static void      _e_main_desk_save(void);
static void      _e_main_desk_restore(void);
static Eina_Bool _e_main_cb_idle_before(void *data EINA_UNUSED);
static Eina_Bool _e_main_cb_idle_after(void *data EINA_UNUSED);
static void      _e_main_create_wm_ready(void);
static void      _e_main_hooks_clean(void);
static void      _e_main_hook_call(E_Main_Hook_Point hookpoint, void *data EINA_UNUSED);

/* local variables */
static int _e_main_lvl = 0;
static int(*_e_main_shutdown_func[MAX_LEVEL]) (void);

static Ecore_Idle_Enterer *_idle_before = NULL;
static Ecore_Idle_Enterer *_idle_after = NULL;

static Eina_List *hooks = NULL;

static int _e_main_hooks_delete = 0;
static int _e_main_hooks_walking = 0;

static Eina_Inlist *_e_main_hooks[] =
{
   [E_MAIN_HOOK_MODULE_LOAD_DONE] = NULL,
   [E_MAIN_HOOK_E_INFO_READY] = NULL
};

/* external variables */
E_API Eina_Bool starting = EINA_TRUE;
E_API Eina_Bool stopping = EINA_FALSE;

static Eina_Bool
_xdg_check_str(const char *env, const char *str)
{
   const char *p;
   size_t len;

   len = strlen(str);
   for (p = strstr(env, str); p; p++, p = strstr(p, str))
     {
        if ((!p[len]) || (p[len] == ':')) return EINA_TRUE;
     }
   return EINA_FALSE;
}

static void
_xdg_data_dirs_augment(void)
{
   char *s;
   const char *p = e_prefix_get();
   char newpath[4096], buf[4096];

   if (!p) return;

   s = e_util_env_get("XDG_DATA_DIRS");
   if (s)
     {
        Eina_Bool pfxdata, pfx;

        pfxdata = !_xdg_check_str(s, e_prefix_data_get());
        snprintf(newpath, sizeof(newpath), "%s/share", p);
        pfx = !_xdg_check_str(s, newpath);
        if (pfxdata || pfx)
          {
             snprintf(buf, sizeof(buf), "%s%s%s%s%s",
               pfxdata ? e_prefix_data_get() : "",
               pfxdata ? ":" : "",
               pfx ? newpath : "",
               pfx ? ":" : "",
               s);
             e_util_env_set("XDG_DATA_DIRS", buf);
          }
        E_FREE(s);
     }
   else
     {
        snprintf(buf, sizeof(buf), "%s:%s/share:/usr/local/share:/usr/share", e_prefix_data_get(), p);
        e_util_env_set("XDG_DATA_DIRS", buf);
     }

   s = e_util_env_get("XDG_CONFIG_DIRS");
   snprintf(newpath, sizeof(newpath), "%s/etc/xdg", p);
   if (s)
     {
        if (!_xdg_check_str(s, newpath))
          {
             snprintf(buf, sizeof(buf), "%s:%s", newpath, s);
             e_util_env_set("XDG_CONFIG_DIRS", buf);
          }
        E_FREE(s);
     }
   else
     {
        snprintf(buf, sizeof(buf), "%s:/etc/xdg", newpath);
        e_util_env_set("XDG_CONFIG_DIRS", buf);
     }

   s = e_util_env_get("XDG_RUNTIME_DIR");
   if (s)
     E_FREE(s);
   else
     {
        const char *dir;

        snprintf(buf, sizeof(buf), "/tmp/xdg-XXXXXX");
        dir = mkdtemp(buf);
        if (!dir) dir = "/tmp";
        else
          {
             e_util_env_set("XDG_RUNTIME_DIR", dir);
             snprintf(buf, sizeof(buf), "%s/.e-deleteme", dir);
             ecore_file_mkdir(buf);
          }
     }

   /* set menu prefix so we get our e menu */
   s = e_util_env_get("XDG_MENU_PREFIX");
   if (s)
     E_FREE(s);
   else
     e_util_env_set("XDG_MENU_PREFIX", "e-");
}

static Eina_Bool
_e_main_subsystem_defer(void *data EINA_UNUSED)
{
   TRACE_DS_BEGIN(MAIN:SUBSYSTEMS DEFER);

   /* try to init delayed subsystems */

   TRACE_DS_BEGIN(MAIN:DEFERRED INTERNAL SUBSYSTEMS INIT);

   TSB("[DEFERRED] DPMS Init");
   if (!e_dpms_init())
     {
        e_error_message_show(_("Enlightenment cannot set up dpms.\n"));
        goto failed;
     }
   TSE("[DEFERRED] DPMS Init Done");
   _e_main_shutdown_push(e_dpms_shutdown);

   TSB("[DEFERRED] Screens Init: win");
   if (!e_win_init())
     {
        e_error_message_show(_("Enlightenment cannot setup elementary trap!\n"));
        goto failed;
     }
   TSE("[DEFERRED] Screens Init: win Done");

   TSB("[DEFERRED] E_Dnd Init");
   if (!e_dnd_init())
     {
        e_error_message_show(_("Enlightenment cannot set up its dnd system.\n"));
        goto failed;
     }
   TSE("[DEFERRED] E_Dnd Init Done");
   _e_main_shutdown_push(e_dnd_shutdown);

   TSB("[DEFERRED] E_Scale Init");
   if (!e_scale_init())
     {
        e_error_message_show(_("Enlightenment cannot set up its scale system.\n"));
        goto failed;
     }
   TSE("[DEFERRED] E_Scale Init Done");
   _e_main_shutdown_push(e_scale_shutdown);

   TSB("[DEFERRED] E_Test_Helper Init");
   e_test_helper_init();
   _e_main_shutdown_push(e_test_helper_shutdown);
   TSE("[DEFERRED] E_Test_Helper Done");

   TSB("[DEFERRED] E_INFO_SERVER Init");
   e_info_server_init();
   _e_main_shutdown_push(e_info_server_shutdown);
   TSE("[DEFERRED] E_INFO_SERVER Done");

   TRACE_DS_END();
   TRACE_DS_BEGIN(MAIN:DEFERRED COMP JOB);

   /* try to do deferred job of any subsystems*/
   TSB("[DEFERRED] Compositor's deferred job");
   e_comp_deferred_job();
   TSE("[DEFERRED] Compositor's deferred job Done");

   if (e_config->use_e_policy)
     {
        TSB("[DEFERRED] E_Policy's deferred job");
        e_policy_deferred_job();
        TSE("[DEFERRED] E_Policy's deferred job Done");
     }

   TSB("[DEFERRED] E_Module's deferred job");
   e_module_deferred_job();
   TSE("[DEFERRED] E_Module's deferred job Done");

   TRACE_DS_END();
   TRACE_DS_END();
   return ECORE_CALLBACK_DONE;

failed:
   TSE("INIT FAILED");
   TRACE_DS_END();
   TRACE_DS_END();
   _e_main_shutdown(-1);
   return ECORE_CALLBACK_DONE;
}

static Eina_Bool
_e_main_deferred_job_schedule(void *d EINA_UNUSED, int type EINA_UNUSED, void *ev EINA_UNUSED)
{
   PRCTL("[Winsys] all modules loaded");
   ecore_idler_add(_e_main_subsystem_defer, NULL);
   return ECORE_CALLBACK_DONE;
}

/* externally accessible functions */
int
main(int argc, char **argv)
{
   char *s = NULL;
   struct sigaction action;

#ifdef __linux__
# ifdef PR_SET_PTRACER
#  ifdef PR_SET_PTRACER_ANY
   prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY);
#  endif
# endif
#endif
#ifdef TS_DO
   t0 = t1 = t2 = ecore_time_unix_get();
   printf("ESTART(main) %1.5f\n", t0);
#endif
   TRACE_DS_BEGIN(MAIN:BEGIN STARTUP);
   TSB("Begin Startup");
   PRCTL("[Winsys] start of main");

   /* trap deadly bug signals and allow some form of sane recovery */
   /* or ability to gdb attach and debug at this point - better than your */
   /* wm/desktop vanishing and not knowing what happened */

   /* don't install SIGBUS handler */
   /* Wayland shm sets up a sigbus handler for catching invalid shm region */
   /* access. If we setup our sigbus handler here, then the wl-shm sigbus */
   /* handler will not function properly */
   s = e_util_env_get("NOTIFY_SOCKET");
   if (s)
     E_FREE(s);
   else
     {
        TSB("Signal Trap");
        action.sa_sigaction = e_sigseg_act;
        action.sa_flags = SA_NODEFER | SA_RESETHAND | SA_SIGINFO;
        sigemptyset(&action.sa_mask);
        sigaction(SIGSEGV, &action, NULL);

        action.sa_sigaction = e_sigill_act;
        action.sa_flags = SA_NODEFER | SA_RESETHAND | SA_SIGINFO;
        sigemptyset(&action.sa_mask);
        sigaction(SIGILL, &action, NULL);

        action.sa_sigaction = e_sigfpe_act;
        action.sa_flags = SA_NODEFER | SA_RESETHAND | SA_SIGINFO;
        sigemptyset(&action.sa_mask);
        sigaction(SIGFPE, &action, NULL);

        action.sa_sigaction = e_sigabrt_act;
        action.sa_flags = SA_NODEFER | SA_RESETHAND | SA_SIGINFO;
        sigemptyset(&action.sa_mask);
        sigaction(SIGABRT, &action, NULL);
        TSE("Signal Trap Done");
     }

   TSB("Eina Init");
   if (!eina_init())
     {
        e_error_message_show(_("Enlightenment cannot initialize Eina!\n"));
        goto failed;
     }
   TSE("Eina Init Done");
   _e_main_shutdown_push(eina_shutdown);

#ifdef OBJECT_HASH_CHECK
   TSB("E_Object Hash Init");
   e_object_hash_init();
   TSE("E_Object Hash Init Done");
#endif

   TSB("E_Log Init");
   if (!e_log_init())
     {
        e_error_message_show(_("Enlightenment could not create a logging domain!\n"));
        goto failed;
     }
   TSE("E_Log Init Done");
   _e_main_shutdown_push(e_log_shutdown);

   TSB("Determine Prefix");
   if (!e_prefix_determine(argv[0]))
     {
        fprintf(stderr,
                "ERROR: Enlightenment cannot determine it's installed\n"
                "       prefix from the system or argv[0].\n"
                "       This is because it is not on Linux AND has been\n"
                "       executed strangely. This is unusual.\n");
     }
   TSE("Determine Prefix Done");

   /* for debugging by redirecting stdout of e to a log file to tail */
   setvbuf(stdout, NULL, _IONBF, 0);

   TSB("Parse Arguments");
   _e_main_parse_arguments(argc, argv);
   TSE("Parse Arguments Done");

   /*** Initialize Core EFL Libraries We Need ***/

   TSB("Eet Init");
   if (!eet_init())
     {
        e_error_message_show(_("Enlightenment cannot initialize Eet!\n"));
        goto failed;
     }
   TSE("Eet Init Done");
   _e_main_shutdown_push(eet_shutdown);

   /* Allow ecore to not load system modules.
    * Without it ecore_init will block until dbus authentication
    * and registration are complete.
    */
   ecore_app_no_system_modules();

   TSB("Ecore Init");
   if (!ecore_init())
     {
        e_error_message_show(_("Enlightenment cannot initialize Ecore!\n"));
        goto failed;
     }
   TSE("Ecore Init Done");
   _e_main_shutdown_push(ecore_shutdown);

   TSB("EIO Init");
   if (!eio_init())
     {
        e_error_message_show(_("Enlightenment cannot initialize EIO!\n"));
        goto failed;
     }
   TSE("EIO Init Done");
   _e_main_shutdown_push(eio_shutdown);

   TSB("Ecore Event Handlers");
   if (!ecore_event_handler_add(ECORE_EVENT_SIGNAL_EXIT,
                                _e_main_cb_signal_exit, NULL))
     {
        e_error_message_show(_("Enlightenment cannot set up an exit signal handler.\n"
                               "Perhaps you are out of memory?"));
        goto failed;
     }
   if (!ecore_event_handler_add(ECORE_EVENT_SIGNAL_HUP,
                                _e_main_cb_signal_hup, NULL))
     {
        e_error_message_show(_("Enlightenment cannot set up a HUP signal handler.\n"
                               "Perhaps you are out of memory?"));
        goto failed;
     }
   TSE("Ecore Event Handlers Done");

   TSB("Ecore_File Init");
   if (!ecore_file_init())
     {
        e_error_message_show(_("Enlightenment cannot initialize Ecore_File!\n"));
        goto failed;
     }
   TSE("Ecore_File Init Done");
   _e_main_shutdown_push(ecore_file_shutdown);

   TSB("E_Util_File_Monitor Init");
   e_util_file_monitor_init();
   TSE("E_Util_File_Monitor Init Done");
   _e_main_shutdown_push(e_util_file_monitor_shutdown);

   _idle_before = ecore_idle_enterer_before_add(_e_main_cb_idle_before, NULL);

   TSB("XDG_DATA_DIRS Init");
   _xdg_data_dirs_augment();
   TSE("XDG_DATA_DIRS Init Done");

   TSB("Ecore_Evas Init");
   if (!ecore_evas_init())
     {
        e_error_message_show(_("Enlightenment cannot initialize Ecore_Evas!\n"));
        goto failed;
     }
   TSE("Ecore_Evas Init Done");

   /* e doesn't sync to compositor - it should be one */
   ecore_evas_app_comp_sync_set(0);

   TSB("Edje Init");
   if (!edje_init())
     {
        e_error_message_show(_("Enlightenment cannot initialize Edje!\n"));
        goto failed;
     }
   TSE("Edje Init Done");
   _e_main_shutdown_push(edje_shutdown);

   /*** Initialize E Subsystems We Need ***/

   TSB("E User Init");
   if (!e_user_init())
     {
        e_error_message_show(_("Enlightenment cannot set up user home path\n"));
        goto failed;
     }
   TSE("E User Init Done");
   _e_main_shutdown_push(e_user_shutdown);

   TSB("E Directories Init");
   /* setup directories we will be using for configurations storage etc. */
   if (!_e_main_dirs_init())
     {
        e_error_message_show(_("Enlightenment cannot create directories in your home directory.\n"
                               "Perhaps you have no home directory or the disk is full?"));
        goto failed;
     }
   TSE("E Directories Init Done");
   _e_main_shutdown_push(_e_main_dirs_shutdown);

   TSB("E_Config Init");
   if (!e_config_init())
     {
        e_error_message_show(_("Enlightenment cannot set up its config system.\n"));
        goto failed;
     }
   TSE("E_Config Init Done");
   _e_main_shutdown_push(e_config_shutdown);

   TSB("E_Env Init");
   if (!e_env_init())
     {
        e_error_message_show(_("Enlightenment cannot set up its environment.\n"));
        goto failed;
     }
   TSE("E_Env Init Done");
   _e_main_shutdown_push(e_env_shutdown);

   ecore_exe_run_priority_set(e_config->priority);

   TSB("E Paths Init");
   if (!_e_main_path_init())
     {
        e_error_message_show(_("Enlightenment cannot set up paths for finding files.\n"
                               "Perhaps you are out of memory?"));
        goto failed;
     }
   TSE("E Paths Init Done");
   _e_main_shutdown_push(_e_main_path_shutdown);

   ecore_animator_frametime_set(1.0 / e_config->framerate);

   TSB("E_Theme Init");
   if (!e_theme_init())
     {
        e_error_message_show(_("Enlightenment cannot set up its theme system.\n"));
        goto failed;
     }
   TSE("E_Theme Init Done");
   _e_main_shutdown_push(e_theme_shutdown);

   TSB("E_Actions Init");
   if (!e_actions_init())
     {
        e_error_message_show(_("Enlightenment cannot set up its actions system.\n"));
        goto failed;
     }
   TSE("E_Actions Init Done");
   _e_main_shutdown_push(e_actions_shutdown);

   /* these just add event handlers and can't fail
    * timestamping them is dumb.
    */
   e_screensaver_preinit();
   e_zone_init();
   e_desk_init();
   e_slot_init();
   e_magnifier_init();

   TRACE_DS_BEGIN(MAIN:WAIT /dev/dri/card0);
   if (e_config->sleep_for_dri)
     {
        while(access("/dev/dri/card0", F_OK) != 0)
          {
             struct timespec req, rem;
             req.tv_sec = 0;
             req.tv_nsec = 50000000L;
             nanosleep(&req, &rem);
          }
     }
   TRACE_DS_END();

   e_module_event_init();

   TSB("E_Pointer Init");
   if (!e_pointer_init())
     {
        e_error_message_show(_("Enlightenment cannot set up its pointer system.\n"));
        goto failed;
     }
   TSE("E_Pointer Init Done");
   _e_main_shutdown_push(e_pointer_shutdown);

   TRACE_DS_BEGIN(MAIN:SCREEN INIT);
   TSB("Screens Init");
   if (!_e_main_screens_init())
     {
        e_error_message_show(_("Enlightenment set up window management for all the screens on your system\n"
                               "failed. Perhaps another window manager is running?\n"));
        goto failed;
     }
   TSE("Screens Init Done");
   _e_main_shutdown_push(_e_main_screens_shutdown);
   TRACE_DS_END();

   TSB("E_Devicemgr Init");
   if (!e_devicemgr_init())
     {
        e_error_message_show(_("Enlightenment cannot set up its device_manager system.\n"));
        goto failed;
     }
   TSE("E_Devicemgr Init Done");
   _e_main_shutdown_push(e_devicemgr_shutdown);

   TSB("E_Keyrouter Init");
   if (!e_keyrouter_init())
     {
        e_error_message_show(_("Enlightenment cannot set up its keyrouting system.\n"));
        goto failed;
     }
   TSE("E_Keyrouter Init Done");
   _e_main_shutdown_push(e_keyrouter_shutdown);

   if (e_config->eom_enable)
     {
        TSB("Eom Init");
        if (!e_eom_init())
          {
             e_error_message_show(_("Enlightenment cannot set up eom.\n"));
             goto failed;
          }
        TSE("Eom Init Done");
        _e_main_shutdown_push(e_eom_shutdown);
     }

   TSB("E_Screensaver Init");
   if (!e_screensaver_init())
     {
        e_error_message_show(_("Enlightenment cannot configure the X screensaver.\n"));
        goto failed;
     }
   TSE("E_Screensaver Init Done");
   _e_main_shutdown_push(e_screensaver_shutdown);

   TSB("E_Comp Freeze");
   e_comp_all_freeze();
   TSE("E_Comp Freeze Done");

   TSB("E_Grabinput Init");
   if (!e_grabinput_init())
     {
        e_error_message_show(_("Enlightenment cannot set up its grab input handling system.\n"));
        goto failed;
     }
   TSE("E_Grabinput Init Done");
   _e_main_shutdown_push(e_grabinput_shutdown);

   TS("E_Gesture Init");
   e_gesture_init();
   _e_main_shutdown_push(e_gesture_shutdown);

   ecore_event_handler_add(E_EVENT_MODULE_INIT_END, _e_main_deferred_job_schedule, NULL);

   TSB("E_Module Init");
   if (!e_module_init())
     {
        e_error_message_show(_("Enlightenment cannot set up its module system.\n"));
        goto failed;
     }
   TSE("E_Module Init Done");
   _e_main_shutdown_push(e_module_shutdown);

   TSB("E_Mouse Init");
   if (!e_mouse_update())
     {
        e_error_message_show(_("Enlightenment cannot configure the mouse settings.\n"));
        goto failed;
     }
   TSE("E_Mouse Init Done");

   TSB("E_Icon Init");
   if (!e_icon_init())
     {
        e_error_message_show(_("Enlightenment cannot initialize the Icon Cache system.\n"));
        goto failed;
     }
   TSE("E_Icon Init Done");
   _e_main_shutdown_push(e_icon_shutdown);

   if (e_config->use_e_policy)
     {
        TSB("E_Policy Init");
        if (!e_policy_init())
          {
             e_error_message_show(_("Enlightenment cannot setup policy system!\n"));
             goto failed;
          }
        TSE("E_Policy Init Done");
        _e_main_shutdown_push(e_policy_shutdown);
     }

   TSB("E_Process Init");
   if (!e_process_init())
     {
        e_error_message_show(_("Enlightenment cannot setup process managing system!\n"));
        goto failed;
     }
   TSE("E_Process Init Done");
   _e_main_shutdown_push(e_process_shutdown);

   TSB("E_Security Init");
   if (!e_security_init())
     {
        e_error_message_show(_("Enlightenment cannot setup security system!\n"));
        goto failed;
     }
   TSE("E_Security Init Done");
   _e_main_shutdown_push(e_security_shutdown);

   TSB("Load Modules");
   e_module_all_load();
   TSE("Load Modules Done");

   TSB("E_Comp Thaw");
   e_comp_all_thaw();
   TSE("E_Comp Thaw Done");

   _idle_after = ecore_idle_enterer_add(_e_main_cb_idle_after, NULL);

   starting = EINA_FALSE;

   TSM("MAIN LOOP AT LAST");

   if (e_config->create_wm_ready)
     _e_main_create_wm_ready();

   TRACE_DS_END();

#ifdef HAVE_SYSTEMD
   TSM("[WM] Send start-up completion");
   sd_notify(0, "READY=1");
#else
   TSM("[WM] Skip sending start-up completion. (no systemd)");
#endif
   ecore_main_loop_begin();

   ELOGF("COMP", "STOPPING enlightenment...", NULL);
   stopping = EINA_TRUE;

   _e_main_desk_save();
   e_comp_internal_save();

   _e_main_shutdown(0);

   e_prefix_shutdown();

   return 0;

failed:
   TSE("INIT FAILED");
   TRACE_DS_END();
   _e_main_shutdown(-1);
}

E_API double
e_main_ts(const char *str)
{
   double ret;
   t1 = ecore_time_unix_get();
   printf("ESTART: %1.5f [%1.5f] - %s\n", t1 - t0, t1 - t2, str);
   ret = t1 - t2;
   t2 = t1;
   return ret;
}

E_API double
e_main_ts_begin(const char *str)
{
   TRACE_DS_BEGIN(ESTART: %s, str);
   return e_main_ts(str);
}

E_API double
e_main_ts_end(const char *str)
{
   TRACE_DS_END();
   return e_main_ts(str);
}

/* local functions */
static void
_e_main_shutdown(int errcode)
{
   int i = 0;
   char buf[PATH_MAX];
   char *dir;

   printf("E: Begin Shutdown Procedure!\n");

   E_FREE_LIST(hooks, e_main_hook_del);

   if (_idle_before) ecore_idle_enterer_del(_idle_before);
   _idle_before = NULL;
   if (_idle_after) ecore_idle_enterer_del(_idle_after);
   _idle_after = NULL;

   dir = e_util_env_get("XDG_RUNTIME_DIR");
   if (dir)
     {
        char buf_env[PATH_MAX];
        snprintf(buf_env, sizeof(buf_env), "%s", dir);
        snprintf(buf, sizeof(buf), "%s/.e-deleteme", buf_env);
        if (ecore_file_exists(buf)) ecore_file_recursive_rm(buf_env);
        E_FREE(dir);
     }
   for (i = (_e_main_lvl - 1); i >= 0; i--)
     (*_e_main_shutdown_func[i])();
#ifdef OBJECT_HASH_CHECK
   e_object_hash_shutdown();
#endif
   if (errcode < 0) exit(errcode);
}

static void
_e_main_shutdown_push(int (*func)(void))
{
   _e_main_lvl++;
   if (_e_main_lvl > MAX_LEVEL)
     {
        _e_main_lvl--;
        e_error_message_show("WARNING: too many init levels. MAX = %i\n",
                             MAX_LEVEL);
        return;
     }
   _e_main_shutdown_func[_e_main_lvl - 1] = func;
}

static void
_e_main_parse_arguments(int argc, char **argv)
{
   int i = 0;

   /* handle some command-line parameters */
   for (i = 1; i < argc; i++)
     {
        if ((!strcmp(argv[i], "-profile")) && (i < (argc - 1)))
          {
             i++;
             if (!getenv("E_CONF_PROFILE"))
               e_util_env_set("E_CONF_PROFILE", argv[i]);
          }
        else if ((!strcmp(argv[i], "-version")) ||
                 (!strcmp(argv[i], "--version")))
          {
             printf(_("Version: %s\n"), PACKAGE_VERSION);
             _e_main_shutdown(-1);
          }
        else if ((!strcmp(argv[i], "-h")) ||
                 (!strcmp(argv[i], "-help")) ||
                 (!strcmp(argv[i], "--help")))
          {
             printf
               (_(
                 "Options:\n"
                 "\t-profile CONF_PROFILE\n"
                 "\t\tUse the configuration profile CONF_PROFILE instead of the user selected default or just \"default\".\n"
                 "\t-version\n"
                 )
               );
             _e_main_shutdown(-1);
          }
     }
}

static Eina_Bool
_e_main_cb_signal_exit(void *data EINA_UNUSED, int ev_type EINA_UNUSED, void *ev EINA_UNUSED)
{
   /* called on ctrl-c, kill (pid) (also SIGINT, SIGTERM and SIGQIT) */
   ecore_main_loop_quit();
   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
_e_main_cb_signal_hup(void *data EINA_UNUSED, int ev_type EINA_UNUSED, void *ev EINA_UNUSED)
{
   ecore_main_loop_quit();
   return ECORE_CALLBACK_RENEW;
}

static int
_e_main_dirs_init(void)
{
   if(getenv("E_CONF_RO"))
     {
        return 1;
     }

   const char *base;
   const char *dirs[] =
   {
      "backgrounds",
      "config",
      "themes",
      NULL
   };

   base = e_user_dir_get();
   if (ecore_file_mksubdirs(base, dirs) != sizeof(dirs) / sizeof(dirs[0]) - 1)
     {
        e_error_message_show("Could not create one of the required "
                             "subdirectories of '%s'\n", base);
        return 0;
     }

   return 1;
}

static int
_e_main_dirs_shutdown(void)
{
   return 1;
}

static int
_e_main_path_init(void)
{
   char buf[PATH_MAX];

   /* setup data paths */
   path_data = e_path_new();
   if (!path_data)
     {
        e_error_message_show("Cannot allocate path for path_data\n");
        return 0;
     }
   e_prefix_data_concat_static(buf, "data");
   e_path_default_path_append(path_data, buf);

   /* setup image paths */
   path_images = e_path_new();
   if (!path_images)
     {
        e_error_message_show("Cannot allocate path for path_images\n");
        return 0;
     }
   e_user_dir_concat_static(buf, "/images");
   e_path_default_path_append(path_images, buf);
   e_prefix_data_concat_static(buf, "data/images");
   e_path_default_path_append(path_images, buf);

   /* setup font paths */
   path_fonts = e_path_new();
   if (!path_fonts)
     {
        e_error_message_show("Cannot allocate path for path_fonts\n");
        return 0;
     }
   e_user_dir_concat_static(buf, "/fonts");
   e_path_default_path_append(path_fonts, buf);
   e_prefix_data_concat_static(buf, "data/fonts");
   e_path_default_path_append(path_fonts, buf);

   /* setup icon paths */
   path_icons = e_path_new();
   if (!path_icons)
     {
        e_error_message_show("Cannot allocate path for path_icons\n");
        return 0;
     }
   e_user_dir_concat_static(buf, "/icons");
   e_path_default_path_append(path_icons, buf);
   e_prefix_data_concat_static(buf, "data/icons");
   e_path_default_path_append(path_icons, buf);

   /* setup module paths */
   path_modules = e_path_new();
   if (!path_modules)
     {
        e_error_message_show("Cannot allocate path for path_modules\n");
        return 0;
     }
   e_user_dir_concat_static(buf, "/modules");
   e_path_default_path_append(path_modules, buf);
   snprintf(buf, sizeof(buf), "%s/enlightenment/modules", e_prefix_lib_get());
   e_path_default_path_append(path_modules, buf);
   /* FIXME: eventually this has to go - moduels should have installers that
    * add appropriate install paths (if not installed to user homedir) to
    * e's module search dirs
    */
   snprintf(buf, sizeof(buf), "%s/enlightenment/modules_extra", e_prefix_lib_get());
   e_path_default_path_append(path_modules, buf);

   /* setup background paths */
   path_backgrounds = e_path_new();
   if (!path_backgrounds)
     {
        e_error_message_show("Cannot allocate path for path_backgrounds\n");
        return 0;
     }
   e_user_dir_concat_static(buf, "/backgrounds");
   e_path_default_path_append(path_backgrounds, buf);
   e_prefix_data_concat_static(buf, "data/backgrounds");
   e_path_default_path_append(path_backgrounds, buf);

   path_messages = e_path_new();
   if (!path_messages)
     {
        e_error_message_show("Cannot allocate path for path_messages\n");
        return 0;
     }
   e_user_dir_concat_static(buf, "/locale");
   e_path_default_path_append(path_messages, buf);
   e_path_default_path_append(path_messages, e_prefix_locale_get());

   return 1;
}

static int
_e_main_path_shutdown(void)
{
   if (path_data)
     {
        e_object_del(E_OBJECT(path_data));
        path_data = NULL;
     }
   if (path_images)
     {
        e_object_del(E_OBJECT(path_images));
        path_images = NULL;
     }
   if (path_fonts)
     {
        e_object_del(E_OBJECT(path_fonts));
        path_fonts = NULL;
     }
   if (path_icons)
     {
        e_object_del(E_OBJECT(path_icons));
        path_icons = NULL;
     }
   if (path_modules)
     {
        e_object_del(E_OBJECT(path_modules));
        path_modules = NULL;
     }
   if (path_backgrounds)
     {
        e_object_del(E_OBJECT(path_backgrounds));
        path_backgrounds = NULL;
     }
   if (path_messages)
     {
        e_object_del(E_OBJECT(path_messages));
        path_messages = NULL;
     }
   return 1;
}

static int
_e_main_screens_init(void)
{
   TSB("\tscreens: client");
   if (!e_client_init()) return 0;
   TSE("\tscreens: client Done");

   TSB("Compositor Init");
   PRCTL("[Winsys] start of compositor init");
   if (!e_comp_init())
     {
        e_error_message_show(_("Enlightenment cannot create a compositor.\n"));
        _e_main_shutdown(-1);
     }
   TSE("Compositor Init Done");

   PRCTL("[Winsys] end of compositor init");
   _e_main_desk_restore();

   return 1;
}

static int
_e_main_screens_shutdown(void)
{
   e_win_shutdown();
   e_comp_shutdown();
   e_client_shutdown();

   e_magnifier_shutdown();
   e_slot_shutdown();
   e_desk_shutdown();
   e_zone_shutdown();
   return 1;
}

static void
_e_main_desk_save(void)
{
   const Eina_List *l;
   char env[1024], name[1024];
   E_Zone *zone;

   EINA_LIST_FOREACH(e_comp->zones, l, zone)
     {
        snprintf(name, sizeof(name), "DESK_%d_%d", 0, zone->num);
        snprintf(env, sizeof(env), "%d,%d", zone->desk_x_current, zone->desk_y_current);
        e_util_env_set(name, env);
     }
}

static void
_e_main_desk_restore(void)
{
   E_Client *ec;

   E_CLIENT_REVERSE_FOREACH(ec)
     if ((!e_client_util_ignored_get(ec)) && e_client_util_desk_visible(ec, e_desk_current_get(ec->zone)))
       {
          ec->want_focus = ec->take_focus = 1;
          break;
       }
}

static Eina_Bool
_e_main_cb_idle_before(void *data EINA_UNUSED)
{
   e_client_idler_before();
   edje_thaw();
   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
_e_main_cb_idle_after(void *data EINA_UNUSED)
{
   static int first_idle = 1;

   eet_clearcache();
   edje_freeze();

   if (first_idle)
     {
        TSM("SLEEP");
        first_idle = 0;
     }

   return ECORE_CALLBACK_RENEW;
}

static void
_e_main_create_wm_ready(void)
{
   FILE *_wmready_checker = NULL;
   const char *path_wm_ready = "/run/.wm_ready";

   if (!e_util_file_realpath_check(path_wm_ready, EINA_TRUE))
     {
        WRN("%s is maybe link, so delete it\n", path_wm_ready);
     }

   _wmready_checker = fopen(path_wm_ready, "wb");
   if (_wmready_checker)
     {
        TSM("[WM] WINDOW MANAGER is READY!!!");
        PRCTL("[Winsys] WINDOW MANAGER is READY!!!");
        fclose(_wmready_checker);

        /*TODO: Next lines should be removed. */
        FILE *_tmp_wm_ready_checker;

        _tmp_wm_ready_checker = fopen(path_wm_ready, "wb");

        if (_tmp_wm_ready_checker)
          {
             TSM("[WM] temporary wm_ready path is created.");
             PRCTL("[Winsys] temporary wm_ready path is created.");
             fclose(_tmp_wm_ready_checker);
          }
        else
          {
             TSM("[WM] temporary wm_ready path create failed.");
             PRCTL("[Winsys] temporary wm_ready path create failed.");
          }
     }
   else
     {
        TSM("[WM] WINDOW MANAGER is READY. BUT, failed to create .wm_ready file.");
        PRCTL("[Winsys] WINDOW MANAGER is READY. BUT, failed to create .wm_ready file.");
     }
}

static void
_e_main_hooks_clean(void)
{
   Eina_Inlist *l;
   E_Main_Hook *mh;
   unsigned int x;

   for (x = 0; x < E_MAIN_HOOK_LAST; x++)
     EINA_INLIST_FOREACH_SAFE(_e_main_hooks[x], l, mh)
       {
          if (!mh->delete_me) continue;
          _e_main_hooks[x] = eina_inlist_remove(_e_main_hooks[x],
                                                EINA_INLIST_GET(mh));
          free(mh);
       }
}

static void
_e_main_hook_call(E_Main_Hook_Point hookpoint, void *data EINA_UNUSED)
{
   E_Main_Hook *mh;

   _e_main_hooks_walking++;
   EINA_INLIST_FOREACH(_e_main_hooks[hookpoint], mh)
     {
        if (mh->delete_me) continue;
        mh->func(mh->data);
     }
   _e_main_hooks_walking--;
   if ((_e_main_hooks_walking == 0) && (_e_main_hooks_delete > 0))
     _e_main_hooks_clean();
}

E_API E_Main_Hook *
e_main_hook_add(E_Main_Hook_Point hookpoint, E_Main_Hook_Cb func, const void *data)
{
   E_Main_Hook *mh;

   EINA_SAFETY_ON_TRUE_RETURN_VAL(hookpoint >= E_MAIN_HOOK_LAST, NULL);
   mh = E_NEW(E_Main_Hook, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(mh, NULL);
   mh->hookpoint = hookpoint;
   mh->func = func;
   mh->data = (void*)data;
   _e_main_hooks[hookpoint] = eina_inlist_append(_e_main_hooks[hookpoint],
                                                 EINA_INLIST_GET(mh));
   return mh;
}

E_API void
e_main_hook_del(E_Main_Hook *mh)
{
   mh->delete_me = 1;
   if (_e_main_hooks_walking == 0)
     {
        _e_main_hooks[mh->hookpoint] = eina_inlist_remove(_e_main_hooks[mh->hookpoint],
                                                          EINA_INLIST_GET(mh));
        free(mh);
     }
   else
     _e_main_hooks_delete++;
}

E_API void
e_main_hook_call(E_Main_Hook_Point hookpoint)
{
   if ((hookpoint < 0) || (hookpoint >= E_MAIN_HOOK_LAST)) return;

   _e_main_hook_call(hookpoint, NULL);
}
