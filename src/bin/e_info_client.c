#include "e.h"
#include "e_info_shared_types.h"
#include <time.h>
#include <dirent.h>
#include <sys/mman.h>

typedef void (*E_Info_Message_Cb)(const Eldbus_Message *msg);

typedef struct _E_Info_Client
{
   /* eldbus */
   int                eldbus_init;
   Eldbus_Proxy      *proxy;
   Eldbus_Connection *conn;
   Eldbus_Object     *obj;

   /* topvwins */
   Eina_List         *win_list;

   Eina_List         *input_dev;

   /* output mode */
   Eina_List         *mode_list;
   int               gl;

   /* pending_commit */
   Eina_List         *pending_commit_list;
} E_Info_Client;

typedef struct _E_Win_Info
{
   Ecore_Window     id;         // native window id
   uint32_t      res_id;
   int           pid;
   const char  *name;       // name of client window
   int          x, y, w, h; // geometry
   int          layer;      // value of E_Layer
   int          vis;        // visibility
   int          alpha;      // alpha window
   int          opaque;
   int          visibility;
   int          iconic;
   int          frame_visible;  //ec->frame obj visible get
   int          focused;
   int          hwc;
   int          pl_zpos;
   Ecore_Window parent_id;
   const char  *layer_name; // layer name
} E_Win_Info;

typedef struct output_mode_info
{
   unsigned int hdisplay, hsync_start, hsync_end, htotal;
   unsigned int vdisplay, vsync_start, vsync_end, vtotal;
   unsigned int refresh, vscan, clock;
   unsigned int flags;
   int current, output, connect;
   const char *name;
} E_Info_Output_Mode;

typedef struct _E_Pending_Commit_Info
{
   unsigned int plane;
   int zpos;
   unsigned int data;
   unsigned int tsurface;
} E_Pending_Commit_Info;

#define VALUE_TYPE_FOR_TOPVWINS "uuisiiiiibbiibbbiius"
#define VALUE_TYPE_REQUEST_RESLIST "ui"
#define VALUE_TYPE_REPLY_RESLIST "ssi"
#define VALUE_TYPE_FOR_INPUTDEV "ssi"
#define VALUE_TYPE_FOR_PENDING_COMMIT "uiuu"

static E_Info_Client e_info_client;

static Eina_Bool compobjs_simple = EINA_FALSE;

static int keepRunning = 1;
static void end_program(int sig);
static Eina_Bool _e_info_client_eldbus_message(const char *method, E_Info_Message_Cb cb);
static Eina_Bool _e_info_client_eldbus_message_with_args(const char *method, E_Info_Message_Cb cb, const char *signature, ...);
static void _e_info_client_eldbus_message_cb(void *data, const Eldbus_Message *msg, Eldbus_Pending *p);

static E_Win_Info *
_e_win_info_new(Ecore_Window id, uint32_t res_id, int pid, Eina_Bool alpha, int opaque, const char *name, int x, int y, int w, int h, int layer, int visible, int visibility, int iconic, int frame_visible, int focused, int hwc, int pl_zpos, Ecore_Window parent_id, const char *layer_name)
{
   E_Win_Info *win = NULL;

   win = E_NEW(E_Win_Info, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(win, NULL);

   win->id = id;
   win->res_id = res_id;
   win->pid = pid;
   win->name = eina_stringshare_add(name);
   win->x = x;
   win->y = y;
   win->w = w;
   win->h = h;
   win->layer = layer;
   win->alpha = alpha;
   win->opaque = opaque;
   win->vis = visible;
   win->visibility = visibility;
   win->frame_visible = frame_visible;
   win->iconic = iconic;
   win->focused = focused;
   win->hwc = hwc;
   win->pl_zpos = pl_zpos;
   win->parent_id = parent_id;
   win->layer_name = eina_stringshare_add(layer_name);

   return win;
}

static void
_e_win_info_free(E_Win_Info *win)
{
   EINA_SAFETY_ON_NULL_RETURN(win);

   if (win->name)
     eina_stringshare_del(win->name);

   if (win->layer_name)
     eina_stringshare_del(win->layer_name);

   E_FREE(win);
}

static void
_cb_window_info_get(const Eldbus_Message *msg)
{
   const char *name = NULL, *text = NULL;
   Eldbus_Message_Iter *array, *ec;
   Eina_Bool res;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_GOTO(res, finish);

   res = eldbus_message_arguments_get(msg, "a("VALUE_TYPE_FOR_TOPVWINS")", &array);
   EINA_SAFETY_ON_FALSE_GOTO(res, finish);

   while (eldbus_message_iter_get_and_next(array, 'r', &ec))
     {
        const char *win_name;
        const char *layer_name;
        int x, y, w, h, layer, visibility, opaque, hwc, pl_zpos;
        Eina_Bool visible, alpha, iconic, focused, frame_visible;
        Ecore_Window id, parent_id;
        uint32_t res_id;
        int pid;
        E_Win_Info *win = NULL;
        res = eldbus_message_iter_arguments_get(ec,
                                                VALUE_TYPE_FOR_TOPVWINS,
                                                &id,
                                                &res_id,
                                                &pid,
                                                &win_name,
                                                &x,
                                                &y,
                                                &w,
                                                &h,
                                                &layer,
                                                &visible,
                                                &alpha,
                                                &opaque,
                                                &visibility,
                                                &iconic,
                                                &frame_visible,
                                                &focused,
                                                &hwc,
                                                &pl_zpos,
                                                &parent_id,
                                                &layer_name);
        if (!res)
          {
             printf("Failed to get win info\n");
             continue;
          }

        win = _e_win_info_new(id, res_id, pid, alpha, opaque, win_name, x, y, w, h, layer, visible, visibility, iconic, frame_visible, focused, hwc, pl_zpos, parent_id, layer_name);
        e_info_client.win_list = eina_list_append(e_info_client.win_list, win);
     }

finish:
   if ((name) || (text))
     {
        printf("errname:%s errmsg:%s\n", name, text);
     }
}

static void
_e_info_client_cb_compobjs(const Eldbus_Message *msg)
{
   const char *name = NULL, *text = NULL, *obj_name;
   Eldbus_Message_Iter *array, *obj;
   Eina_Bool res;
   E_Info_Comp_Obj cobj;
   int i;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_GOTO(res, finish);

   res = eldbus_message_arguments_get(msg,
                                      "a("SIGNATURE_COMPOBJS_CLIENT")",
                                      &array);
   EINA_SAFETY_ON_FALSE_GOTO(res, finish);

   if (compobjs_simple)
     printf(
        "==========================================================================================================================\n"
        "                        /-- Object Type        /-- Alpha                                 /-- Edj: group                   \n"
        "                        |    r  : Rectangle    |                                         |   Edj Member: part, value      \n"
        "                        |    EDJ: Edje         | /-- Pass Events                         |   Native Image:                \n"
        "                        |    IMG: Image        | |/-- Freeze Events                      |    type, buff, size, load, fill\n"
        "                        |    EC : ec->frame    | ||/-- Focused                           |                      size  size\n"
        "                        |                      | |||                                     |   File Image:                  \n"
        "                        |                      | ||| /-- Visibility                      |    data, size, load, fill      \n"
        "                        |                      | ||| |                                   |                size  size      \n"
        "                        |                      | ||| |                                   |                                \n"
        "========================|======================|=|||=|===================================|================================\n"
        "Layer  ObjectID         |     X    Y    W    H | ||| |   ObjectName                      | Additional Info                \n"
        "========================|======================|=|||=|===================================|================================\n"
        );
   else
     printf(
        "======================================================================================================================\n"
        "                        /-- Object Type                            /-- Alpha                                          \n"
        "                        |    r  : Rectangle Object                 |                                                  \n"
        "                        |    EDJ: Edje Object                      | /-- Pass Events                                  \n"
        "                        |    IMG: Image Object                     | |/-- Freeze Events                               \n"
        "                        |    EC : ec->frame Object                 | ||/-- Focused                                    \n"
        "                        |                                          | |||                                              \n"
        "                        |    /-- Render Operation                  | ||| /-- Visibility                               \n"
        "                        |    |    BL: EVAS_RENDER_BLEND            | ||| |                                            \n"
        "                        |    |    CP: EVAS_RENDER_COPY             | ||| |                                            \n"
        "                        |    |                                     | ||| |                           [Additional Info]\n"
        "                        |    |                                     | ||| |                          EDJ: group, file |\n"
        "                        |    |                                     | ||| |                   EDJ member: part, value |\n"
        "                        |    |                                     | ||| |   Image: Type, Size, Load Size, Fill Size |\n"
        "                        |    |                                     | ||| |                                           |\n"
        "                        |    |                                     | ||| |                                           |\n"
        "========================|====|=====================================|=|||=|============================================\n"
        "Layer  ObjectID         |    |    X    Y    W    H  Color(RGBA)    | ||| |     ObjectName                            |\n"
        "========================|====|=====================================|=|||=|============================================\n"
        );

   while (eldbus_message_iter_get_and_next(array, 'r', &obj))
     {
        memset(&cobj, 0, sizeof(E_Info_Comp_Obj));

        res = eldbus_message_iter_arguments_get(obj,
                                                SIGNATURE_COMPOBJS_CLIENT,
                                                &cobj.obj,
                                                &cobj.depth,
                                                &cobj.type,
                                                &cobj.name,
                                                &cobj.ly,
                                                &cobj.opmode,
                                                &cobj.x, &cobj.y, &cobj.w, &cobj.h,
                                                &cobj.r, &cobj.g, &cobj.b, &cobj.a,
                                                &cobj.pass_events,
                                                &cobj.freeze_events,
                                                &cobj.focus,
                                                &cobj.vis,
                                                &cobj.edje.file,
                                                &cobj.edje.group,
                                                &cobj.edje.part,
                                                &cobj.edje.val,
                                                &cobj.img.native,
                                                &cobj.img.native_type,
                                                &cobj.img.file,
                                                &cobj.img.key,
                                                &cobj.img.data,
                                                &cobj.img.w, &cobj.img.h,
                                                &cobj.img.lw, &cobj.img.lh,
                                                &cobj.img.fx, &cobj.img.fy, &cobj.img.fw, &cobj.img.fh,
                                                &cobj.img.alpha,
                                                &cobj.img.dirty);
        if (!res)
          {
             printf("Failed to get composite obj info\n");
             continue;
          }

        if (cobj.depth == 0)
          {
             if (!compobjs_simple)
               printf(" - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -- - - - - - - - -|\n");
             printf("%4d ", cobj.ly);
          }
        else
          printf("     ");

        for (i = 0; i < cobj.depth; i++) printf(" ");
        printf("%08x ", cobj.obj);
        for (i = 6; i > cobj.depth; i--) printf(" ");

        if (compobjs_simple)
          printf("%5.5s "
                 "|%4d,%4d %4dx%4d|%s|%s%s%s|%s|",
                 cobj.type,
                 cobj.x, cobj.y, cobj.w, cobj.h,
                 cobj.img.alpha == 1 ? "A" : " ",
                 cobj.pass_events == 1 ? "p" : " ",
                 cobj.freeze_events == 1 ? "z" : " ",
                 cobj.focus == 1 ? "F" : " ",
                 cobj.vis == 1 ? "V" : " ");
        else
          printf("%5.5s "
                 "|%3.3s"
                 "|%4d,%4d %4dx%4d|%3d %3d %3d %3d|%s|%s%s%s|%s|",
                 cobj.type,
                 cobj.opmode,
                 cobj.x, cobj.y, cobj.w, cobj.h,
                 cobj.r, cobj.g, cobj.b, cobj.a,
                 cobj.img.alpha == 1 ? "A" : " ",
                 cobj.pass_events == 1 ? "p" : " ",
                 cobj.freeze_events == 1 ? "z" : " ",
                 cobj.focus == 1 ? "F" : " ",
                 cobj.vis == 1 ? "V" : " ");

        obj_name = cobj.name;
        if (!strncmp(obj_name, "no_use", 6)) obj_name = "";
        printf("%-32.32s|", obj_name);

        if (!strncmp(cobj.type, "EDJ", 3))
          {
             if (strncmp(cobj.edje.group, "no_use", 6)) printf("%s ", cobj.edje.group);

             if (!compobjs_simple)
               if (strncmp(cobj.edje.file,  "no_use", 6)) printf("%s ", cobj.edje.file);
          }

        if (strncmp(cobj.edje.part,  "no_use", 6)) printf("%s %1.1f", cobj.edje.part, cobj.edje.val);

        if (!strncmp(cobj.type, "IMG", 3))
          {
             if (cobj.img.native)
               {
                  if (strncmp(cobj.img.native_type, "no_use", 6)) printf("%s ", cobj.img.native_type);
               }
             else
               {
                  if (!compobjs_simple)
                    if (strncmp(cobj.img.file, "no_use", 6)) printf("%s ", cobj.img.file);

                  if (strncmp(cobj.img.key, "no_use", 6)) printf("%s ", cobj.img.key);

               }

             printf("d:%x %dx%d %dx%d (%d,%d %dx%d)",
                    cobj.img.data,
                    cobj.img.w, cobj.img.h,
                    cobj.img.lw, cobj.img.lh,
                    cobj.img.fx, cobj.img.fy, cobj.img.fw, cobj.img.fh);
          }

        printf("\n");
     }

   if (compobjs_simple)
     printf("==========================================================================================================================\n");
   else
     printf("======================================================================================================================\n");

finish:
   if ((name) || (text))
     {
        printf("errname:%s errmsg:%s\n", name, text);
     }
}

static void
_cb_input_device_info_get(const Eldbus_Message *msg)
{
   const char *name = NULL, *text = NULL;
   Eldbus_Message_Iter *array, *eldbus_msg;
   Eina_Bool res;
   E_Comp_Wl_Input_Device *dev;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_GOTO(res, finish);

   res = eldbus_message_arguments_get(msg, "a("VALUE_TYPE_FOR_INPUTDEV")", &array);
   EINA_SAFETY_ON_FALSE_GOTO(res, finish);

   while (eldbus_message_iter_get_and_next(array, 'r', &eldbus_msg))
     {
        char *dev_name;
        char *identifier;
        int clas;
        res = eldbus_message_iter_arguments_get(eldbus_msg,
                                                VALUE_TYPE_FOR_INPUTDEV,
                                                &dev_name,
                                                &identifier,
                                                &clas);
        if (!res)
          {
             printf("Failed to get device info\n");
             continue;
          }

        dev = E_NEW(E_Comp_Wl_Input_Device, 1);
        dev->name = strdup(dev_name);
        dev->identifier = strdup(identifier);
        dev->clas = clas;

        e_info_client.input_dev = eina_list_append(e_info_client.input_dev, dev);
     }

finish:
   if ((name) || (text))
     {
        printf("errname:%s errmsg:%s\n", name, text);
     }
}

static void
_cb_input_keymap_info_get(const Eldbus_Message *msg)
{
   const char *name = NULL, *text = NULL;
   Eina_Bool res;
   int i;
   int min_keycode=0, max_keycode=0, fd=0, size=0, num_mods=0, num_groups = 0;
   struct xkb_context *context = NULL;
   struct xkb_keymap *keymap = NULL;
   struct xkb_state *state = NULL;
   xkb_keysym_t sym = XKB_KEY_NoSymbol;
   char keyname[256] = {0, };
   char *map = NULL;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_GOTO(res, finish);

   res = eldbus_message_arguments_get(msg, "hi", &fd, &size);
   EINA_SAFETY_ON_FALSE_GOTO(res, finish);

   context = xkb_context_new(0);
   EINA_SAFETY_ON_NULL_GOTO(context, finish);

   map = mmap(NULL, size, 0x01, 0x0001, fd, 0);
   if (map == ((void *)-1))
     {
        close(fd);
        return;
     }

   keymap = xkb_map_new_from_string(context, map, XKB_KEYMAP_FORMAT_TEXT_V1, 0);

   munmap(map, size);
   close(fd);

   EINA_SAFETY_ON_NULL_GOTO(keymap, finish);
   state = xkb_state_new(keymap);
   EINA_SAFETY_ON_NULL_GOTO(state, finish);

   min_keycode = xkb_keymap_min_keycode(keymap);
   max_keycode = xkb_keymap_max_keycode(keymap);
   num_groups = xkb_map_num_groups(keymap);
   num_mods = xkb_keymap_num_mods(keymap);

   printf("\n");
   printf("    min keycode: %d\n", min_keycode);
   printf("    max keycode: %d\n", max_keycode);
   printf("    num_groups : %d\n", num_groups);
   printf("    num_mods   : %d\n", num_mods);
   for (i = 0; i < num_mods; i++)
     {
        printf("        [%2d] mod: %s\n", i, xkb_keymap_mod_get_name(keymap, i));
     }

   printf("\n\n\tkeycode\t\tkeyname\t\t  keysym\t    repeat\n");
   printf("    ----------------------------------------------------------------------\n");

   for (i = min_keycode; i < (max_keycode + 1); i++)
     {
        sym = xkb_state_key_get_one_sym(state, i);

        memset(keyname, 0, sizeof(keyname));
        xkb_keysym_get_name(sym, keyname, sizeof(keyname));

        printf("\t%4d%-5s%-25s%-20p%-5d\n", i, "", keyname, (void *)sym, xkb_keymap_key_repeats(keymap, i));
     }
finish:
   if ((name) || (text ))
     {
        printf("errname:%s errmsg:%s\n", name, text);
     }
   if (state) xkb_state_unref(state);
   if (keymap) xkb_map_unref(keymap);
   if (context) xkb_context_unref(context);
}

#define PROTOCOL_RULE_USAGE \
  "[COMMAND] [ARG]...\n" \
  "\tadd    : add the rule to trace events (Usage: add [allow|deny] [RULE(iface=wl_touch and msg=down)]\n" \
  "\tremove  : remove the rule (Usage: remove [all|RULE_INDEX])\n" \
  "\tfile    : add rules from file (Usage: file [RULE_FILE_PATH])\n" \
  "\tprint   : print current rules\n" \
  "\thelp\n" \

static void
_e_info_client_proc_protocol_trace(int argc, char **argv)
{
   char fd_name[PATH_MAX];
   int pid;
   char cwd[PATH_MAX];

   if (argc != 3 || !argv[2])
     {
        printf("protocol-trace: Usage> enlightenment_info -protocol_trace [console | file path | disable]\n");
        return;
     }

   pid = getpid();

   cwd[0] = '\0';
   if (!getcwd(cwd, sizeof(cwd)))
     snprintf(cwd, sizeof(cwd), "/tmp");

   if (!strncmp(argv[2], "console", 7))
     snprintf(fd_name, PATH_MAX, "/proc/%d/fd/1", pid);
   else if (!strncmp(argv[2], "elog", 4))
     snprintf(fd_name, PATH_MAX, "elog");
   else if (!strncmp(argv[2], "disable", 7))
     snprintf(fd_name, PATH_MAX, "disable");
   else
     {
        if (argv[2][0] == '/')
          snprintf(fd_name, PATH_MAX, "%s", argv[2]);
        else
          {
             if (strlen(cwd) > 0)
               snprintf(fd_name, PATH_MAX, "%s/%s", cwd, argv[2]);
             else
               snprintf(fd_name, PATH_MAX, "%s", argv[2]);
          }
     }

   printf("protocol-trace: %s\n", fd_name);

   if (!_e_info_client_eldbus_message_with_args("protocol_trace", NULL, "s", fd_name))
     return;
}

static void
_cb_protocol_rule(const Eldbus_Message *msg)
{
   const char *name = NULL, *text = NULL;
   Eina_Bool res;
   const char *reply;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_GOTO(res, finish);

   res = eldbus_message_arguments_get(msg, "s", &reply);
   EINA_SAFETY_ON_FALSE_GOTO(res, finish);
   printf("%s\n", reply);

finish:
   if ((name) || (text ))
     {
        printf("errname:%s errmsg:%s\n", name, text);
     }
}

static void
_e_info_client_proc_protocol_rule(int argc, char **argv)
{
   char *new_argv[3];
   char *new_s1 = NULL;
   char *new_s2 = NULL;
   int new_argc;
   int i;

   if (argc < 3 ||
      (argc > 3 && !eina_streq(argv[2], "print") && !eina_streq(argv[2], "help") && !eina_streq(argv[2], "file") && !eina_streq(argv[2], "add") && !eina_streq(argv[2], "remove")))
     {
        printf("protocol-trace: Usage> enlightenment_info -protocol_rule [add | remove | print | help] [allow/deny/all]\n");
        return;
     }

   new_argc = argc - 2;
   for (i = 0; i < new_argc; i++)
     new_argv[i] = argv[i + 2];
   if (new_argc < 2)
     {
        new_s1 = (char *)calloc (1, PATH_MAX);
        snprintf(new_s1, PATH_MAX, "%s", "no_data");
        new_argv[1] = new_s1;
        new_argc++;
     }
   if (new_argc < 3)
     {
        new_s2 = (char *)calloc (1, PATH_MAX);
        snprintf(new_s2, PATH_MAX, "%s", "no_data");
        new_argv[2] = new_s2;
        new_argc++;
     }
   if (new_argc != 3)
     {
        printf("protocol-trace: Usage> enlightenment_info -protocol_rule [add | remove | print | help] [allow/deny/all]\n");
        return;
     }

   _e_info_client_eldbus_message_with_args("protocol_rule", _cb_protocol_rule, "sss", new_argv[0], new_argv[1], new_argv[2]);

   if (new_s1) free(new_s1);
   if (new_s2) free(new_s2);
}

static void
_e_info_client_proc_topvwins_info(int argc, char **argv)
{
   E_Win_Info *win;
   Eina_List *l;
   int i = 0;
   int prev_layer = -1;
   int hwc_off = 0;

   const char *prev_layer_name = NULL;

   if (!_e_info_client_eldbus_message("get_window_info", _cb_window_info_get))
     return;

   printf("%d Top level windows\n", eina_list_count(e_info_client.win_list));
   printf("--------------------------------------[ topvwins ]----------------------------------------------------------------------------\n");
   printf(" No   Win_ID    RcsID    PID     w     h       x      y  Foc Dep Opaq Visi Icon  Map  Frame  PL@ZPos  Parent     Title\n");
   printf("------------------------------------------------------------------------------------------------------------------------------\n");

   if (!e_info_client.win_list)
     {
        printf("no window\n");
        return;
     }

   EINA_LIST_FOREACH(e_info_client.win_list, l, win)
     {
        if (!win) return;
        char tmp[20];
        i++;
        if (win->layer != prev_layer)
          {
             if (prev_layer != -1)
                printf("---------------------------------------------------------------------------------------------------------------------[%s]\n",
                       prev_layer_name ? prev_layer_name : " ");
             prev_layer = win->layer;
             prev_layer_name = win->layer_name;
          }

        if (win->hwc >= 0)
          {
             if ((!win->iconic) && (!win->visibility) && (win->frame_visible))
               {
                  if (win->hwc) snprintf(tmp, sizeof(tmp), "hwc@%i", win->pl_zpos);
                  else snprintf(tmp, sizeof(tmp), "comp@%i", win->pl_zpos);
               }
             else
               snprintf(tmp, sizeof(tmp), " - ");
          }
        else // hwc is not initialized or hwc_deactive 1
          {
             hwc_off = 1;
             snprintf(tmp, sizeof(tmp), " - ");
          }

        printf("%3d 0x%08x  %5d  %5d  %5d %5d %6d %6d   %c  %3d  %2d   ", i, win->id, win->res_id, win->pid, win->w, win->h, win->x, win->y, win->focused ? 'O':' ', win->alpha? 32:24, win->opaque);
        printf("%2d    %d    %s   %3d    %-8s %-8x   %s\n", win->visibility, win->iconic, win->vis? "V":"N", win->frame_visible, tmp, win->parent_id, win->name?:"No Name");
     }

   if (prev_layer_name)
      printf("---------------------------------------------------------------------------------------------------------------------[%s]\n",
             prev_layer_name ? prev_layer_name : " ");

   if(hwc_off)
     printf("\nHWC is disabled\n\n");

   E_FREE_LIST(e_info_client.win_list, _e_win_info_free);
}

static void
_e_info_client_proc_compobjs_info(int argc, char **argv)
{
   Eina_Bool res;

   if ((argc == 3) && (argv[2]))
     {
        if (!strncmp(argv[2], "simple", 6))
          compobjs_simple = EINA_TRUE;
     }

   res = _e_info_client_eldbus_message("compobjs",
                                       _e_info_client_cb_compobjs);
   EINA_SAFETY_ON_FALSE_RETURN(res);
}

static void
_e_info_client_proc_input_device_info(int argc, char **argv)
{
   E_Comp_Wl_Input_Device *dev;
   Eina_List *l;
   int i = 0;

   if (!_e_info_client_eldbus_message("get_input_devices", _cb_input_device_info_get))
     return;

   printf("--------------------------------------[ input devices ]----------------------------------------------------------\n");
   printf(" No                               Name                        identifier            Cap\n");
   printf("-----------------------------------------------------------------------------------------------------------------\n");

   if (!e_info_client.input_dev)
     {
        printf("no devices\n");
        return;
     }

   EINA_LIST_FOREACH(e_info_client.input_dev, l, dev)
     {
        i++;
        printf("%3d %50s %20s         ", i, dev->name, dev->identifier);
        if (dev->clas == ECORE_DEVICE_CLASS_MOUSE) printf("Mouse | ");
        else if (dev->clas == ECORE_DEVICE_CLASS_KEYBOARD) printf("Keyboard | ");
        else if (dev->clas == ECORE_DEVICE_CLASS_TOUCH) printf("Touch | ");
        printf("(0x%x)\n", dev->clas);
     }

   E_FREE_LIST(e_info_client.input_dev, free);
}

static void
_e_info_client_proc_keymap_info(int argc, char **argv)
{
   if (!_e_info_client_eldbus_message("get_keymap", _cb_input_keymap_info_get))
      return;
}

static void
_e_info_client_proc_module_info(int argc, char **argv)
{
   char fd_name[PATH_MAX];
   int pid;

   if (argc != 3 || !argv[2])
     {
        printf("Usage> enlightenment_info -module_info [module name]\n");
        return;
     }

   pid = getpid();

   snprintf(fd_name, PATH_MAX, "/proc/%d/fd/1", pid);

   if (!_e_info_client_eldbus_message_with_args("get_module_info", NULL, "ss", argv[2], fd_name))
     return;
}

static void
_e_info_client_proc_keygrab_status(int argc, char **argv)
{
   char fd_name[PATH_MAX];
   int pid;
   char cwd[PATH_MAX];

   if (argc != 3 || !argv[2])
     {
        printf("Usage> enlightenment_info -keygrab_status [console | file path]\n");
        return;
     }

   pid = getpid();

   cwd[0] = '\0';
   if (!getcwd(cwd, sizeof(cwd)))
     snprintf(cwd, sizeof(cwd), "/tmp");

   if (!strncmp(argv[2], "console", sizeof("console")))
     snprintf(fd_name, PATH_MAX, "/proc/%d/fd/1", pid);
   else
     {
        if (argv[2][0] == '/')
          snprintf(fd_name, PATH_MAX, "%s", argv[2]);
        else
          {
             if (strlen(cwd) > 0)
               snprintf(fd_name, PATH_MAX, "%s/%s", cwd, argv[2]);
             else
               snprintf(fd_name, PATH_MAX, "%s", argv[2]);
          }
     }

   if (!_e_info_client_eldbus_message_with_args("get_keygrab_status", NULL, "s", fd_name))
     return;
}

static char *
_directory_make(char *path)
{
   char dir[PATH_MAX], curdir[PATH_MAX], stamp[PATH_MAX];
   time_t timer;
   struct tm *t, *buf;
   char *fullpath;
   DIR *dp;

   timer = time(NULL);

   buf = calloc (1, sizeof (struct tm));
   EINA_SAFETY_ON_NULL_RETURN_VAL(buf, NULL);

   t = localtime_r(&timer, buf);
   if (!t)
     {
        free(buf);
        printf("fail to get local time\n");
        return NULL;
     }

   fullpath = (char*) calloc(1, PATH_MAX*sizeof(char));
   if (!fullpath)
     {
        free(buf);
        printf("fail to alloc pathname memory\n");
        return NULL;
     }

   if (path && path[0] == '/')
     snprintf(dir, PATH_MAX, "%s", path);
   else
     {
        char *temp = getcwd(curdir, PATH_MAX);
        if (!temp)
          {
             free(buf);
             free(fullpath);
             return NULL;
          }
        if (path)
          {
             if (strlen(curdir) == 1 && curdir[0] == '/')
               snprintf(dir, PATH_MAX, "/%s", path);
             else
               snprintf(dir, PATH_MAX, "%s/%s", curdir, path);
          }
        else
          snprintf(dir, PATH_MAX, "%s", curdir);
     }

   if (!(dp = opendir (dir)))
     {
        free(buf);
        free(fullpath);
        printf("not exist: %s\n", dir);
        return NULL;
     }
   else
      closedir (dp);

   /* make the folder for the result of xwd files */
   snprintf(stamp, PATH_MAX, "%04d%02d%02d.%02d%02d%02d", t->tm_year+1900, t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);

   if (strlen(dir) == 1 && dir[0] == '/')
     snprintf(fullpath, PATH_MAX, "/topvwins-%s", stamp);
   else
     snprintf(fullpath, PATH_MAX, "%s/topvwins-%s", dir, stamp);

   free (buf);

   if ((mkdir(fullpath, 0755)) < 0)
     {
        printf("fail: mkdir '%s'\n", fullpath);
        free(fullpath);
        return NULL;
     }

   printf("directory: %s\n", fullpath);

   return fullpath;
}

static void
_e_info_client_proc_topvwins_shot(int argc, char **argv)
{
   char *directory = _directory_make(argv[2]);
   EINA_SAFETY_ON_NULL_RETURN(directory);

   if (!_e_info_client_eldbus_message_with_args("dump_topvwins", NULL, "s", directory))
     {
        free(directory);
        return;
     }

   free(directory);
}

static void
_cb_subsurface_info_get(const Eldbus_Message *msg)
{
   const char *name = NULL, *text = NULL;
   Eldbus_Message_Iter *array, *ec;
   Eina_Bool res;
   int count = 0;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_GOTO(res, finish);

   res = eldbus_message_arguments_get(msg, "a("SIGNATURE_SUBSURFACE")", &array);
   EINA_SAFETY_ON_FALSE_GOTO(res, finish);

   printf("--------------------------------------[ subsurfaces ]---------------------------------------------------------\n");
   printf(" No     Win_ID  Parent_ID  Buf_ID    w    h    x    y Rot(f) Visi Alph Igno Mask Video Stand    BgRect   Title\n");
   printf("--------------------------------------------------------------------------------------------------------------\n");

   while (eldbus_message_iter_get_and_next(array, 'r', &ec))
     {
        Ecore_Window win = 0, parent = 0, bgrect = 0;
        unsigned int buf_id = 0;
        int x = 0, y = 0, w = 0, h = 0;
        unsigned int transform = 0, visible = 0, alpha = 0, ignore = 0, maskobj = 0, video = 0, stand = 0;
        const char *name = NULL;
        char temp[128] = {0,};

        res = eldbus_message_iter_arguments_get(ec,
                                                SIGNATURE_SUBSURFACE,
                                                &win, &parent,
                                                &buf_id, &x, &y, &w, &h, &transform,
                                                &visible, &alpha, &ignore, &maskobj, &video, &stand, &bgrect, &name);
        if (!res)
          {
             printf("Failed to get win info\n");
             continue;
          }

        count++;

        printf("%3d 0x%08x ", count, win);
        temp[0] = '\0';
        if (parent > 0) snprintf(temp, sizeof(temp), "0x%08x", parent);
        printf("%10s", temp);
        temp[0] = '\0';
        if (buf_id != 0)
          snprintf(temp, sizeof(temp), "%5u%c",
                   buf_id & (~WAYLAND_SERVER_RESOURCE_ID_MASK),
                   (buf_id & WAYLAND_SERVER_RESOURCE_ID_MASK) ? 's' : 'c');
        printf("  %6s", temp);
        printf(" %4d %4d %4d %4d %3d(%d) %4s %4s %4s %4s %4s %4s   ",
               w, h, x, y, (4 - (transform & 3)) * 90 % 360, (transform & 4) ? 1 : 0,
               (visible)?"O":"", (alpha)?"O":"", (ignore)?"O":"", (maskobj)?"O":"", (video)?"O":"", (stand)?"O":"");
        temp[0] = '\0';
        if (bgrect > 0) snprintf(temp, sizeof(temp), "0x%08x", bgrect);
        printf("%10s", temp);
        printf(" %s\n", name);
     }

   if (!count)
     printf("no subsurface\n");

finish:
   if ((name) || (text))
     printf("errname:%s errmsg:%s\n", name, text);
}

static void
_e_info_client_proc_subsurface(int argc, char **argv)
{
   _e_info_client_eldbus_message("subsurface", _cb_subsurface_info_get);
}

static void
_e_info_client_proc_eina_log_levels(int argc, char **argv)
{
   EINA_SAFETY_ON_FALSE_RETURN(argc == 3);
   EINA_SAFETY_ON_NULL_RETURN(argv[2]);

   if (!_e_info_client_eldbus_message_with_args("eina_log_levels", NULL, "s", argv[2]))
     {
        return;
     }
}

static void
_e_info_client_proc_eina_log_path(int argc, char **argv)
{
   char fd_name[PATH_MAX];
   int pid;
   char cwd[PATH_MAX];

   EINA_SAFETY_ON_FALSE_RETURN(argc == 3);
   EINA_SAFETY_ON_NULL_RETURN(argv[2]);

   pid = getpid();

   cwd[0] = '\0';
   if (!getcwd(cwd, sizeof(cwd)))
     snprintf(cwd, sizeof(cwd), "/tmp");

   if (!strncmp(argv[2], "console", 7))
     snprintf(fd_name, PATH_MAX, "/proc/%d/fd/1", pid);
   else
     {
        if (argv[2][0] == '/')
          snprintf(fd_name, PATH_MAX, "%s", argv[2]);
        else
          {
             if (strlen(cwd) > 0)
               snprintf(fd_name, PATH_MAX, "%s/%s", cwd, argv[2]);
             else
               snprintf(fd_name, PATH_MAX, "%s", argv[2]);
          }
     }

   printf("eina-log-path: %s\n", fd_name);

   if (!_e_info_client_eldbus_message_with_args("eina_log_path", NULL, "s", fd_name))
     {
        return;
     }
}

#ifdef HAVE_DLOG
static void
_e_info_client_proc_dlog_switch(int argc, char **argv)
{
   uint32_t onoff;

   EINA_SAFETY_ON_FALSE_RETURN(argc == 3);
   EINA_SAFETY_ON_NULL_RETURN(argv[2]);

   onoff = atoi(argv[2]);
   if ((onoff == 1) || (onoff == 0))
     {

        if (!_e_info_client_eldbus_message_with_args("dlog", NULL, "i", onoff))
          {
             printf("Error to switch %s logging system using dlog logging.", onoff?"on":"off");
             return;
          }
        if (onoff)
          printf("Now you can try to track enlightenment log with dlog logging system.\n"
                 "Track dlog with LOGTAG \"E20\" ex) dlogutil E20\n");
        else
          printf("Logging of enlightenment with dlog is disabled.\n");
     }
}
#endif

static void
_cb_window_prop_get(const Eldbus_Message *msg)
{
   const char *name = NULL, *text = NULL;
   Eldbus_Message_Iter *array, *ec;
   Eina_Bool res;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_GOTO(res, finish);

   res = eldbus_message_arguments_get(msg, "a(ss)", &array);
   EINA_SAFETY_ON_FALSE_GOTO(res, finish);

   printf("--------------------------------------[ window prop ]-----------------------------------------------------\n");
   while (eldbus_message_iter_get_and_next(array, 'r', &ec))
     {
        const char *title;
        const char *value;
        res = eldbus_message_iter_arguments_get(ec,
                                                "ss",
                                                &title,
                                                &value);
        if (!res)
          {
             printf("Failed to get win prop info\n");
             continue;
          }

        if (!strcmp(title, "[WINDOW PROP]"))
           printf("---------------------------------------------------------------------------------------------------------\n");
        else
           printf("%20s : %s\n", title, value);
     }
   printf("----------------------------------------------------------------------------------------------------------\n");

finish:
   if ((name) || (text))
     {
        printf("errname:%s errmsg:%s\n", name, text);
     }
}

static void
_e_info_client_prop_prop_info(int argc, char **argv)
{
   const static int WINDOW_ID_MODE = 0;
   const static int WINDOW_PID_MODE = 1;
   const static int WINDOW_NAME_MODE = 2;
   const char *value;
   uint32_t mode = 0;

   if (argc < 3 || argv[2] == NULL)
     {
        printf("Error Check Args: enlightenment_info -prop [windowID]\n"
               "                  enlightenment_info -prop -id [windowID]\n"
               "                  enlightenment_info -prop -pid [PID]\n"
               "                  enlightenment_info -prop -name [name]\n");
        return;
     }

   if (strlen(argv[2]) > 2 && argv[2][0] == '-')
     {
        if (!strcmp(argv[2], "-id")) mode = WINDOW_ID_MODE;
        if (!strcmp(argv[2], "-pid")) mode = WINDOW_PID_MODE;
        if (!strcmp(argv[2], "-name")) mode = WINDOW_NAME_MODE;
        value = (argc >= 4 ? argv[3] : NULL);
     }
   else
     {
        mode = WINDOW_ID_MODE;
        value = argv[2];
     }

   if (!_e_info_client_eldbus_message_with_args("get_window_prop", _cb_window_prop_get, "us", mode, value))
     {
        printf("_e_info_client_eldbus_message_with_args error");
        return;
     }
}

static void
_cb_window_proc_connected_clients_get(const Eldbus_Message *msg)
{
   const char *name = NULL, *text = NULL;
   Eldbus_Message_Iter *array, *ec;
   Eina_Bool res;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_GOTO(res, finish);

   res = eldbus_message_arguments_get(msg, "a(ss)", &array);
   EINA_SAFETY_ON_FALSE_GOTO(res, finish);

   printf("--------------------------------------[ connected clients ]-----------------------------------------------------\n");
   int cnt = 0;
   while (eldbus_message_iter_get_and_next(array, 'r', &ec))
     {
        const char *title;
        const char *value;
        res = eldbus_message_iter_arguments_get(ec,
                                                "ss",
                                                &title,
                                                &value);
        if (!res)
          {
             printf("Failed to get connected clients info\n");
             continue;
          }

        if (!strcmp(title, "[Connected Clients]"))
          {
             printf("\n[%2d] %s\n", ++cnt, value);
          }
        else if (!strcmp(title, "[E_Client Info]"))
          {
             printf("      |----- %s :: %s\n", title, value);
          }
     }

finish:
   if ((name) || (text))
     {
        printf("errname:%s errmsg:%s\n", name, text);
     }
}

static void
_e_info_client_proc_connected_clients(int argc, char **argv)
{
   if (!_e_info_client_eldbus_message("get_connected_clients", _cb_window_proc_connected_clients_get))
     {
        printf("_e_info_client_eldbus_message error");
        return;
     }
}

#define ROTATION_USAGE \
   "[COMMAND] [ARG]...\n" \
   "\tset     : Set the orientation of zone (Usage: set [zone-no] [rval(0|90|180|270)]\n" \
   "\tinfo    : Get the information of zone's rotation (Usage: info [zone-no]) (Not Implemented)\n" \
   "\tenable  : Enable the rotation of zone (Usage: enable [zone-no]\n" \
   "\tdisable : Disable the rotation of zone (Usage: disable [zone-no]\n"

static void
_cb_rotation_query(const Eldbus_Message *msg)
{
   (void)msg;
   /* TODO: need implementation */
}

static void
_e_info_client_proc_rotation(int argc, char **argv)
{
   E_Info_Rotation_Message req;
   int32_t zone_num = -1;
   int32_t rval = -1;
   const int off_len = 2, cmd_len = 1;
   Eina_Bool res = EINA_FALSE;

   if (argc < off_len + cmd_len)
     goto arg_err;

   if (eina_streq(argv[off_len], "info"))
     {
        if (argc > off_len + cmd_len)
          zone_num = atoi(argv[off_len + 1]);

        res = _e_info_client_eldbus_message_with_args("rotation_query",
                                                      _cb_rotation_query,
                                                      "i", zone_num);
     }
   else
     {
        if (eina_streq(argv[off_len], "set"))
          {
             if (argc < off_len + cmd_len + 1)
               goto arg_err;
             else if (argc > off_len + cmd_len + 1)
               {
                  zone_num = atoi(argv[off_len + 1]);
                  rval = atoi(argv[off_len + 2]);
               }
             else
               rval = atoi(argv[off_len + 1]);

             if ((rval < 0) || (rval > 270) || (rval % 90 != 0))
               goto arg_err;

             req = E_INFO_ROTATION_MESSAGE_SET;
          }
        else
          {
             if (argc > off_len + cmd_len)
               zone_num = atoi(argv[off_len + 1]);

             if (eina_streq(argv[off_len], "enable"))
               req = E_INFO_ROTATION_MESSAGE_ENABLE;
             else if (eina_streq(argv[off_len], "disable"))
               req = E_INFO_ROTATION_MESSAGE_DISABLE;
             else
               goto arg_err;
          }

        res = _e_info_client_eldbus_message_with_args("rotation_message",
                                                      NULL, "iii",
                                                      req, zone_num, rval);
     }

   if (!res)
     printf("_e_info_client_eldbus_message_with_args error");

   return;
arg_err:
   printf("Usage: enlightenment_info -rotation %s", ROTATION_USAGE);
}

#define RESLIST_USAGE \
   "[-tree|-p]\n" \
   "\t-tree     : All resources\n" \
   "\t-p {pid}  : Specify client pid\n"

enum
{
   DEFAULT_SUMMARY = 0,
   TREE,
   PID
};

static void
_pname_get(pid_t pid, char *name, int size)
{
   if (!name) return;

   FILE *h;
   char proc[512], pname[512];
   size_t len;

   snprintf(proc, 512,"/proc/%d/cmdline", pid);

   h = fopen(proc, "r");
   if (!h) return;

   len = fread(pname, sizeof(char), 512, h);
   if (len > 0)
     {
        if ('\n' == pname[len - 1])
          pname[len - 1] = '\0';
     }

   fclose(h);

   strncpy(name, pname, size);
}


static void
_cb_disp_res_lists_get(const Eldbus_Message *msg)
{
   const char *name = NULL, *text = NULL;
   Eldbus_Message_Iter *array, *resource;
   Eina_Bool res;
   int nClient = 0, nResource = 0;
   char temp[PATH_MAX];
   int pid = 0;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_GOTO(res, finish);

   res = eldbus_message_arguments_get(msg, "a("VALUE_TYPE_REPLY_RESLIST")", &array);
   EINA_SAFETY_ON_FALSE_GOTO(res, finish);

   snprintf(temp, PATH_MAX,"%6s   %6s   %s   %s\n", "NO", "PID", "N_of_Res", "NAME");
   printf("%s",temp);

   while (eldbus_message_iter_get_and_next(array, 'r', &resource))
     {
        char cmd[512] = {0, };
        const char *type;
        const char *item;
        int id = 0;
        res = eldbus_message_iter_arguments_get(resource,
                                                VALUE_TYPE_REPLY_RESLIST,
                                                &type,
                                                &item,
                                                &id);
        if (!res)
          {
             printf("Failed to get connected clients info\n");
             continue;
          }
        if (!strcmp(type, "[client]"))
          {
             pid = id;
             nResource = 0;
             ++nClient;
          }
        else if (!strcmp(type, "[count]"))
          {
             nResource = id;
             _pname_get(pid, cmd, sizeof(cmd));

             printf("%6d   %6d   %4d      %9s\n", nClient, pid, nResource, cmd);
             pid = 0;
          }
     }

finish:
   if ((name) || (text))
     {
        printf("errname:%s errmsg:%s\n", name, text);
     }
}

static void
_cb_disp_res_lists_get_detail(const Eldbus_Message *msg)
{
   const char *name = NULL, *text = NULL;
   Eldbus_Message_Iter *array, *resource;
   Eina_Bool res;
   int nClient = 0, nResource = 0;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_GOTO(res, finish);

   res = eldbus_message_arguments_get(msg, "a("VALUE_TYPE_REPLY_RESLIST")", &array);
   EINA_SAFETY_ON_FALSE_GOTO(res, finish);

   while (eldbus_message_iter_get_and_next(array, 'r', &resource))
     {
        const char *type;
        const char *item;
        char cmd[512] = {0, };
        int id = 0, pid = 0;

        res = eldbus_message_iter_arguments_get(resource,
                                                VALUE_TYPE_REPLY_RESLIST,
                                                &type,
                                                &item,
                                                &id);

        if (!res)
          {
             printf("Failed to get connected clients info\n");
             continue;
          }
        if (!strcmp(type, "[client]"))
          {
             nResource = 0;
             pid = id;
             ++nClient;
             _pname_get(pid, cmd, sizeof(cmd));
             printf("[%2d] pid %d  (%s)\n", nClient, pid, cmd);

          }
        else if (!strcmp(type, "[resource]"))
          {
             ++nResource;
             printf("      |----- %s obj@%d\n", item, id);
          }

     }

finish:
   if ((name) || (text))
     {
        printf("errname:%s errmsg:%s\n", name, text);
     }
}

static void
_e_info_client_proc_res_lists(int argc, char **argv)
{
   uint32_t mode;
   int pid = 0;

   if (argc == 2)
     {
        mode = DEFAULT_SUMMARY;
        if (!_e_info_client_eldbus_message_with_args("get_res_lists", _cb_disp_res_lists_get, VALUE_TYPE_REQUEST_RESLIST, mode, pid))
          {
             printf("%s error\n", __FUNCTION__);
             return;
          }
     }
   else if (argc == 3)
     {
        if (eina_streq(argv[2], "-tree")) mode = TREE;
        else goto arg_err;

        if (!_e_info_client_eldbus_message_with_args("get_res_lists", _cb_disp_res_lists_get_detail, VALUE_TYPE_REQUEST_RESLIST, mode, pid))
          {
             printf("%s error\n", __FUNCTION__);
             return;
          }
     }
   else if (argc == 4)
     {
        if (eina_streq(argv[2], "-p"))
          {
             mode = PID;
             pid = atoi(argv[3]);
             if (pid <= 0) goto arg_err;
          }
        else goto arg_err;

        if (!_e_info_client_eldbus_message_with_args("get_res_lists", _cb_disp_res_lists_get_detail, VALUE_TYPE_REQUEST_RESLIST, mode, pid))
          {
             printf("%s error\n", __FUNCTION__);
             return;
          }
     }
   else goto arg_err;

   return;
arg_err:
   printf("Usage: enlightenment_info -reslist\n%s", RESLIST_USAGE);

}

static void
_cb_fps_info_get(const Eldbus_Message *msg)
{
   const char *name = NULL, *text = NULL;
   Eina_Bool res;
   const char *fps;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_GOTO(res, finish);

   res = eldbus_message_arguments_get(msg, "s", &fps);
   EINA_SAFETY_ON_FALSE_GOTO(res, finish);
   if (strcmp(fps, "no_update"))
        printf("%s\n", fps);

finish:
   if ((name) || (text ))
     {
        printf("errname:%s errmsg:%s\n", name, text);
     }
}

static void
_e_info_client_proc_fps_info(int argc, char **argv)
{
   keepRunning = 1;

   do
     {
        if (!_e_info_client_eldbus_message("get_fps_info", _cb_fps_info_get))
          return;
        usleep(500000);
     }
   while (keepRunning);
}

static void
_e_info_client_proc_punch(int argc, char **argv)
{
   int onoff = 0, x = 0, y = 0, w = 0, h = 0;
   int a = 0, r = 0, g = 0, b = 0;
   char *arg;

   EINA_SAFETY_ON_FALSE_RETURN(argc >= 3);
   EINA_SAFETY_ON_NULL_RETURN(argv[2]);

   arg = argv[2];
   if (!strncmp(arg, "on", 2))
     onoff = 1;

   arg = argv[3];
   if (arg && sscanf(arg, "%dx%d+%d+%d", &w, &h, &x, &y) < 0)
     {
        printf("wrong geometry arguments(<w>x<h>+<x>+<y>\n");
        return;
     }

   if (argc == 5 && argv[4])
     {
        arg = argv[4];
        if (sscanf(arg, "%d,%d,%d,%d", &a, &r, &g, &b) < 0)
          {
             printf("wrong color arguments(<a>,<r>,<g>,<b>)\n");
             return;
          }
     }

   _e_info_client_eldbus_message_with_args("punch", NULL, "iiiiiiiii", onoff, x, y, w, h, a, r, g, b);
}

static void
_e_info_client_proc_transform_set(int argc, char **argv)
{
   int32_t id_enable_xy_sxsy_angle[8];
   int i;

   if (argc < 5)
     {
        printf("Error Check Args: enlightenment_info -transform [windowID] [transform id] [enable] [x] [y] [scale_x(percent)] [scale_y(percent)] [degree] [keep_ratio]\n");
        return;
     }

   id_enable_xy_sxsy_angle[0] = 0;      // transform id
   id_enable_xy_sxsy_angle[1] = 1;      // enable
   id_enable_xy_sxsy_angle[2] = 0;      // move x
   id_enable_xy_sxsy_angle[3] = 0;      // move y
   id_enable_xy_sxsy_angle[4] = 100;    // scale x percent
   id_enable_xy_sxsy_angle[5] = 100;    // scale y percent
   id_enable_xy_sxsy_angle[6] = 0;      // rotation degree
   id_enable_xy_sxsy_angle[7] = 0;      // keep ratio

   for (i = 0 ; i < 8 &&  i+3 < argc; ++i)
      id_enable_xy_sxsy_angle[i] = atoi(argv[i+3]);

   if (!_e_info_client_eldbus_message_with_args("transform_message", NULL, "siiiiiiii",
                                                argv[2], id_enable_xy_sxsy_angle[0] , id_enable_xy_sxsy_angle[1], id_enable_xy_sxsy_angle[2],
                                                id_enable_xy_sxsy_angle[3], id_enable_xy_sxsy_angle[4], id_enable_xy_sxsy_angle[5],
                                                id_enable_xy_sxsy_angle[6], id_enable_xy_sxsy_angle[7]))
     {
        printf("_e_info_client_eldbus_message_with_args error");
        return;
     }
}

#define DUMP_BUFFERS_USAGE \
  "  enlightenment_info -dump_buffers [ARG]...\n" \
  "  enlightenment_info -dump_buffers 1                : start dump buffer (default - buffer_count:100, path:/tmp/dump_xxxx/\n" \
  "  enlightenment_info -dump_buffers 1 -c 50          : start dump buffer with 50 buffers\n" \
  "  enlightenment_info -dump_buffers 1 -p /tmp/test   : start dump buffer - the dump path is '/tmp/test/dump_xxxx'\n" \
  "  enlightenment_info -dump_buffers 1 -c 60 -p /test : start dump buffer with 60 buffers to '/test/dump_xxxx' folder\n" \
  "  enlightenment_info -dump_buffers 0                : stop dump buffer (store dump files to dump path)\n" \

static char *
_buffer_shot_directory_check(char *path)
{
   char dir[PATH_MAX], curdir[PATH_MAX];
   char *fullpath;
   DIR *dp;

   fullpath = (char*)calloc(1, PATH_MAX * sizeof(char));
   if (!fullpath)
     {
        printf("fail to alloc pathname memory\n");
        return NULL;
     }

   if (path && path[0] == '/')
     snprintf(dir, PATH_MAX, "%s", path);
   else
     {
        char *temp = getcwd(curdir, PATH_MAX);
        if (!temp)
          {
             free(fullpath);
             return NULL;
          }
        if (path)
          {
             if (strlen(curdir) == 1 && curdir[0] == '/')
               snprintf(dir, PATH_MAX, "/%s", path);
             else
               snprintf(dir, PATH_MAX, "%s/%s", curdir, path);
          }
        else
          snprintf(dir, PATH_MAX, "%s", curdir);
     }

   if (!(dp = opendir(dir)))
     {
        free(fullpath);
        printf("not exist: %s\n", dir);
        return NULL;
     }
   else
      closedir (dp);

   snprintf(fullpath, PATH_MAX, "%s", dir);

   return fullpath;
}

static void
_cb_window_proc_slot_get(const Eldbus_Message *msg)
{
   const char *name = NULL, *text = NULL;
   Eina_Bool res;
   Eldbus_Message_Iter *array, *ec;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_GOTO(res, finish);

   res = eldbus_message_arguments_get(msg, "a(ss)", &array);
   EINA_SAFETY_ON_FALSE_GOTO(res, finish);

   printf("--------------------------------------[ slot info ]-----------------------------------------------------\n");
   int cnt = 0;
   int client_cnt = 0;
   while (eldbus_message_iter_get_and_next(array, 'r', &ec))
     {
        const char *title;
        const char *value;
        res = eldbus_message_iter_arguments_get(ec,
                                                "ss",
                                                &title,
                                                &value);
        if (!res)
          {
             printf("Failed to get slot info\n");
             continue;
          }

        if (!strcmp(title, "[SLOT LIST]"))
          {
             printf("[%02d] %s\n", ++cnt, value ? value : " ");
             client_cnt = 0;
          }
        else if (!strcmp(title, "[SLOT CLIENT]"))
          {
             printf("\t\t|---[%02d] %s\n", ++client_cnt, value ? value : " ");
          }
        else if (!strcmp(title, "[SLOT INFO]"))
          {
             printf("::: %s\n", value ? value : " ");
          }
     }
finish:
   if ((name) || (text ))
     {
        printf("errname:%s errmsg:%s\n", name, text);
     }
}


static void
_e_info_client_proc_slot_set(int argc, char **argv)
{
   int32_t param[5];
   int i;
   int mode = 0;
   const char *value = NULL;

   if (argc < 3)
     {
        printf("\t\t\t   %s\n", USAGE_SLOT);
        return;
     }

   /* check mode */
   if (strlen(argv[2]) > 2)
     {
        mode = -1;
        if (!strncmp(argv[2], "start", strlen("start"))) mode = E_INFO_CMD_MESSAGE_START;
        if (!strncmp(argv[2], "list", strlen("list"))) mode = E_INFO_CMD_MESSAGE_LIST;
        if (!strncmp(argv[2], "create", strlen("add"))) mode = E_INFO_CMD_MESSAGE_CREATE;
        if (!strncmp(argv[2], "modify", strlen("modify"))) mode = E_INFO_CMD_MESSAGE_MODIFY;
        if (!strncmp(argv[2], "del", strlen("del"))) mode = E_INFO_CMD_MESSAGE_DEL;
        if (!strncmp(argv[2], "raise", strlen("raise"))) mode = E_INFO_CMD_MESSAGE_RAISE;
        if (!strncmp(argv[2], "lower", strlen("lower"))) mode = E_INFO_CMD_MESSAGE_LOWER;
        if (!strncmp(argv[2], "add_ec_t", strlen("add_ec_t"))) mode = E_INFO_CMD_MESSAGE_ADD_EC_TRANSFORM;
        if (!strncmp(argv[2], "add_ec_r", strlen("add_ec_r"))) mode = E_INFO_CMD_MESSAGE_ADD_EC_RESIZE;
        if (!strncmp(argv[2], "del_ec", strlen("del_ec"))) mode = E_INFO_CMD_MESSAGE_DEL_EC;
        if (!strncmp(argv[2], "focus", strlen("focus"))) mode = E_INFO_CMD_MESSAGE_FOCUS;
     }
   else
     {
        printf("Error Check Args!\n");
        return;
     }

   param[0] = 0;
   param[1] = 0;
   param[2] = 0;
   param[3] = 0;
   param[4] = 0;

   for (i = 0; (i < 5) && ((i+3) < argc); ++i)
     {
        param[i] = atoi(argv[i+3]);
     }

   if (mode == E_INFO_CMD_MESSAGE_ADD_EC_TRANSFORM ||
       mode == E_INFO_CMD_MESSAGE_ADD_EC_RESIZE ||
       mode == E_INFO_CMD_MESSAGE_DEL_EC)
     {
        value = argv[4];
        int32_t value_number;
        if (strlen(value) >= 2 && value[0] == '0' && value[1] == 'x')
          sscanf(value, "%x", &value_number);
        else
          sscanf(value, "%d", &value_number);

        param[1] = value_number;
     }

   if (!_e_info_client_eldbus_message_with_args("slot_message", _cb_window_proc_slot_get, "iiiiii",
                                                mode, param[0] , param[1], param[2],
                                                param[3], param[4]))
     {
        printf("_e_info_client_eldbus_message_with_args error");
        return;
     }
}

static void
_e_info_client_proc_desk(int argc, char **argv)
{
   const int offset = 2, cmd_len = 1;
   Eina_Bool res = EINA_FALSE;

   if (argc < offset + cmd_len)
     goto arg_err;

   if (eina_streq(argv[offset], "geometry"))
     {
        const int narg = 4;
        int geom[narg];
        int i;

        if (argc < offset + cmd_len + narg)
          goto arg_err;

        for (i = 0; i < narg; i++)
          geom[i] = atoi(argv[offset + cmd_len + i]);

        if ((geom[2] < 0) || (geom[3] < 0))
          {
             printf("Error Check Args: Width(%d) and Height(%d) must not be less than 1.\n", geom[2], geom[3]);
             return;
          }

        res = _e_info_client_eldbus_message_with_args("desktop_geometry_set", NULL, "iiii",
                                                      geom[0], geom[1], geom[2], geom[3]);
     }
   else if (eina_streq(argv[offset], "zoom"))
     {
        const int narg = 4;
        double zx, zy;
        int cx, cy;

        if (argc < offset + cmd_len + narg)
          goto arg_err;

        zx = atof(argv[offset + cmd_len]);
        zy = atof(argv[offset + cmd_len + 1]);
        cx = atoi(argv[offset + cmd_len + 2]);
        cy = atoi(argv[offset + cmd_len + 3]);

        res = _e_info_client_eldbus_message_with_args("desk_zoom", NULL, "ddii",
                                                      zx, zy, cx, cy);
     }

   if (!res)
     {
        printf("_e_info_client_eldbus_message_with_args error");
        return;
     }

   return;
arg_err:
   printf("Usage: enlightenment_info -desk\n");
}

static void
_e_info_client_proc_buffer_shot(int argc, char **argv)
{
   int dumprun = 0;
   int count = 100;
   char path[PATH_MAX];

   strncpy(path, "/tmp", PATH_MAX);
   if (argc == 3)
     {
        dumprun = atoi(argv[2]);

        if (dumprun < 0 || dumprun > 1) goto err;

        if (!_e_info_client_eldbus_message_with_args("dump_buffers", NULL, "iis", dumprun, count, path))
          {
             printf("_e_info_client_proc_buffer_shot fail (%d)\n", dumprun);
             return;
          }
        printf("_e_info_client_proc_buffer_shot %s\n", (dumprun == 1 ? "start" : "stop"));
     }
   else if (argc == 5)
     {
        dumprun = atoi(argv[2]);

        if (dumprun < 0 || dumprun > 1) goto err;

        if (eina_streq(argv[3], "-c"))
          {
             count = atoi(argv[4]);
             if (count < 0) goto err;

             if (!_e_info_client_eldbus_message_with_args("dump_buffers", NULL, "iis", dumprun, count, path))
               {
                  printf("_e_info_client_proc_buffer_shot fail (%d)\n", dumprun);
                  return;
               }
             printf("_e_info_client_proc_buffer_shot %s\n", (dumprun == 1 ? "start" : "stop"));
          }
        else if (eina_streq(argv[3], "-p"))
          {
             char *tmp_path = _buffer_shot_directory_check(argv[4]);
             if (tmp_path == NULL)
               {
                  printf("cannot find directory: %s\n", argv[4]);
                  goto err;
               }

             if (!_e_info_client_eldbus_message_with_args("dump_buffers", NULL, "iis", dumprun, count, tmp_path))
               {
                  printf("_e_info_client_proc_buffer_shot fail (%d)\n", dumprun);
                  free(tmp_path);
                  return;
               }
             free(tmp_path);
          }
        else
          goto err;
     }
   else if (argc == 7)
     {
        dumprun = atoi(argv[2]);

        if (dumprun < 0 || dumprun > 1) goto err;

        if (eina_streq(argv[3], "-c"))
          {
             char *tmp_path = NULL;

             if (!eina_streq(argv[5], "-p")) goto err;

             count = atoi(argv[4]);
             if (count < 0) goto err;

             tmp_path = _buffer_shot_directory_check(argv[6]);
             if (tmp_path == NULL)
               {
                  printf("cannot find directory: %s\n", argv[6]);
                  goto err;
               }

             if (!_e_info_client_eldbus_message_with_args("dump_buffers", NULL, "iis", dumprun, count, tmp_path))
               {
                  printf("_e_info_client_proc_buffer_shot fail (%d)\n", dumprun);
                  free(tmp_path);
                  return;
               }
             printf("_e_info_client_proc_buffer_shot %s\n", (dumprun == 1 ? "start" : "stop"));
             free(tmp_path);
          }
        else if (eina_streq(argv[3], "-p"))
          {
             char *tmp_path = NULL;

             if (!eina_streq(argv[5], "-c")) goto err;

             count = atoi(argv[6]);
             if (count < 0) goto err;

             tmp_path = _buffer_shot_directory_check(argv[4]);
             if (tmp_path == NULL)
               {
                  printf("cannot find directory: %s\n", argv[4]);
                  goto err;
               }

             if (!_e_info_client_eldbus_message_with_args("dump_buffers", NULL, "iis", dumprun, count, tmp_path))
               {
                  printf("_e_info_client_proc_buffer_shot fail (%d)\n", dumprun);
                  free(tmp_path);
                  return;
               }
             free(tmp_path);
          }
        else
          goto err;
     }
   else
     goto err;

   return;

err:
   printf("Error Check Args\n%s\n", DUMP_BUFFERS_USAGE);
return;
}

static E_Info_Output_Mode *
_e_output_mode_info_new(uint32_t h, uint32_t hsync_start, uint32_t hsync_end, uint32_t htotal,
                        uint32_t v, uint32_t vsync_start, uint32_t vsync_end, uint32_t vtotal,
                        uint32_t refresh, uint32_t vscan, uint32_t clock, uint32_t flags,
                        int current, int output, int connect, const char *name)
{
   E_Info_Output_Mode *mode = NULL;

   mode = E_NEW(E_Info_Output_Mode, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(mode, NULL);

   mode->hdisplay = h;
   mode->hsync_start = hsync_start;
   mode->hsync_end = hsync_end;
   mode->htotal = htotal;
   mode->vdisplay = v;
   mode->vsync_start = vsync_start;
   mode->vsync_end = vsync_end;
   mode->vtotal = vtotal;
   mode->refresh = refresh;
   mode->vscan = vscan;
   mode->clock = clock;
   mode->flags = flags;
   mode->current = current;
   mode->output = output;
   mode->connect = connect;
   mode->name = eina_stringshare_add(name);

   return mode;
}

static void
_e_output_mode_info_free(E_Info_Output_Mode *mode)
{
   EINA_SAFETY_ON_NULL_RETURN(mode);

   if (mode->name)
     eina_stringshare_del(mode->name);

   E_FREE(mode);
}

static void
_cb_output_mode_info(const Eldbus_Message *msg)
{
   const char *name = NULL, *text = NULL;
   Eldbus_Message_Iter *array, *ec;
   Eina_Bool res;
   int gl = 0;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_GOTO(res, finish);

   res = eldbus_message_arguments_get(msg, "a("SIGNATURE_OUTPUT_MODE_SERVER")", &array);
   EINA_SAFETY_ON_FALSE_GOTO(res, finish);

   while (eldbus_message_iter_get_and_next(array, 'r', &ec))
     {
        uint32_t h, hsync_start, hsync_end, htotal;
        uint32_t v, vsync_start, vsync_end, vtotal;
        uint32_t refresh, vscan, clock, flag;
        int current, output, connect;
        const char *name;
        E_Info_Output_Mode *mode = NULL;
        res = eldbus_message_iter_arguments_get(ec,
                                                SIGNATURE_OUTPUT_MODE_SERVER,
                                                &h, &hsync_start, &hsync_end, &htotal,
                                                &v, &vsync_start, &vsync_end, &vtotal,
                                                &refresh, &vscan, &clock, &flag, &name,
                                                &current, &output, &connect, &gl);
        if (!res)
          {
             printf("Failed to get output mode info\n");
             continue;
          }

        mode = _e_output_mode_info_new(h, hsync_start, hsync_end, htotal,
                                       v, vsync_start, vsync_end, vtotal,
                                       refresh, vscan, clock, flag,
                                       current, output, connect, name);
        e_info_client.mode_list = eina_list_append(e_info_client.mode_list, mode);
     }
   e_info_client.gl = gl;

finish:
   if ((name) || (text))
     {
        printf("errname:%s errmsg:%s\n", name, text);
     }
}

static void
_e_info_client_proc_output_mode(int argc, char **argv)
{
   E_Info_Output_Mode *mode = NULL;
   Eina_List *l;
   int output = 0;
   int idx = 0;
   char curr;

   if (!_e_info_client_eldbus_message_with_args("output_mode", _cb_output_mode_info,
                                                SIGNATURE_OUTPUT_MODE_CLIENT, E_INFO_CMD_OUTPUT_MODE_GET, 0))
     {
        printf("_e_info_client_proc_output_mode fail (%d)\n", 1);
        return;
     }

   if (!e_info_client.mode_list)
     {
        printf("no list\n");
        return;
     }

   if (e_info_client.gl == 0)
     {
        E_FREE_LIST(e_info_client.mode_list, _e_output_mode_info_free);

        printf("not support output_mode.\n");
        return;
     }

   printf("--------------------------------------[ output mode ]---------------------------------------------\n");
   printf(" idx   modename     h  hss  hse  htot  v  vss  vse  vtot  refresh  clk  vscan  preferred  current\n");
   printf("--------------------------------------------------------------------------------------------------\n");

   EINA_LIST_FOREACH(e_info_client.mode_list, l, mode)
     {
        if (!mode) return;

        if (output == mode->output)
          {
             printf("output %u : ", mode->output);
             output++;
             idx = 0;

             if (mode->connect == 1)
               printf("%s\n", "connected");
             else
               {
                  printf("%s\n", "disconnected");
                  continue;
               }
          }

        if (mode->current == 1)
          curr = 'O';
        else
          curr = ' ';

        printf("%3d%13s %5u%5u%5u%5u%5u%5u%5u%5u  %3u %8u  %2u        ",
               idx++, mode->name,
               mode->hdisplay, mode->hsync_start, mode->hsync_end, mode->htotal,
               mode->vdisplay, mode->vsync_start, mode->vsync_end, mode->vtotal,
               mode->refresh, mode->clock, mode->vscan);

        if (mode->flags == 1)
          printf("O         %c\n", curr);
        else
          printf("          %c\n", curr);
     }

   E_FREE_LIST(e_info_client.mode_list, _e_output_mode_info_free);

   printf("\n");

   return;
}

#ifdef ENABLE_HWC_MULTI
static void
_e_info_client_proc_hwc_trace(int argc, char **argv)
{
   uint32_t onoff;

   if (argc < 3)
     {
        printf("Error Check Args: enlightenment_info -hwc_trace [0/1/2]\n");
        return;
     }

   onoff = atoi(argv[2]);

   if (onoff == 1 || onoff == 0 || onoff == 2)
     {
        if (!_e_info_client_eldbus_message_with_args("hwc_trace_message", NULL, "i", onoff))
          {
             printf("_e_info_client_eldbus_message_with_args error");
             return;
          }
     }
   else
     printf("Error Check Args: enlightenment_info -hwc_trace [0/1/2]\n");
}

static void
_e_info_client_proc_hwc(int argc, char **argv)
{
   uint32_t onoff;

   if (argc < 3)
     {
        printf("Error Check Args: enlightenment_info -hwc [1: on, 0: off]\n");
        return;
     }

   onoff = atoi(argv[2]);

   if (onoff == 1 || onoff == 0)
     {
        if (!_e_info_client_eldbus_message_with_args("hwc", NULL, "i", onoff))
          {
             printf("_e_info_client_eldbus_message_with_args error");
             return;
          }
     }
   else
     printf("Error Check Args: enlightenment_info -hwc [1: on, 0: off]\n");

}

static void
_e_info_client_proc_show_plane_state(int argc, char **argv)
{
   if (!_e_info_client_eldbus_message("show_plane_state", NULL))
     return;

   printf("e20 print planes state with eina_log\n");
}

static E_Pending_Commit_Info *
_e_pending_commit_info_new(unsigned int plane, int zpos, unsigned int data, unsigned int tsurface)
{
   E_Pending_Commit_Info *pending_commit = NULL;

   pending_commit = E_NEW(E_Pending_Commit_Info, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(pending_commit, NULL);

   pending_commit->plane = plane;
   pending_commit->zpos = zpos;
   pending_commit->data = data;
   pending_commit->tsurface = tsurface;

   return pending_commit;
}

static void
_e_pending_commit_info_free(E_Pending_Commit_Info *pending_commit)
{
   E_FREE(pending_commit);
}

static void
_cb_pending_commit_info_get(const Eldbus_Message *msg)
{
   const char *name = NULL, *text = NULL;
   Eldbus_Message_Iter *array, *eldbus_msg;
   Eina_Bool res;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_GOTO(res, finish);

   res = eldbus_message_arguments_get(msg, "a("VALUE_TYPE_FOR_PENDING_COMMIT")", &array);
   EINA_SAFETY_ON_FALSE_GOTO(res, finish);

   while (eldbus_message_iter_get_and_next(array, 'r', &eldbus_msg))
     {
        E_Pending_Commit_Info *pending_commit = NULL;
        unsigned int plane, tsurface, data;
        int zpos;
        res = eldbus_message_iter_arguments_get(eldbus_msg,
                                                VALUE_TYPE_FOR_PENDING_COMMIT,
                                                &plane,
                                                &zpos,
                                                &data,
                                                &tsurface);
        if (!res)
          {
             printf("Failed to get pending_commit info\n");
             continue;
          }

        pending_commit = _e_pending_commit_info_new(plane, zpos, data, tsurface);
        if (!pending_commit) continue;

        e_info_client.pending_commit_list = eina_list_append(e_info_client.pending_commit_list, pending_commit);
     }

finish:
   if ((name) || (text))
     {
        printf("errname:%s errmsg:%s\n", name, text);
     }
}

static void
_e_info_client_proc_show_pending_commit(int argc, char **argv)
{
   Eina_List *l;
   int i = 0;
   E_Pending_Commit_Info *pending_commit;

   if (!_e_info_client_eldbus_message("show_pending_commit", _cb_pending_commit_info_get))
     return;

   printf("----------------------------[ pending commit ]-----------------------------------\n");
   printf(" No          Plane          Zpos          Data          tsurface\n");
   printf("---------------------------------------------------------------------------------\n");

   if (!e_info_client.pending_commit_list)
     {
        printf("no peding commit\n");
        return;
     }

   EINA_LIST_FOREACH(e_info_client.pending_commit_list, l, pending_commit)
     {
        i++;
        printf("%3d        %12p   %5d         %12p  %12p\n",
               i,
               (void *)pending_commit->plane,
               pending_commit->zpos,
               (void *)pending_commit->data,
               (void *)pending_commit->tsurface);
     }

   E_FREE_LIST(e_info_client.pending_commit_list, _e_pending_commit_info_free);
}
#endif

static void
_e_info_client_proc_effect_control(int argc, char **argv)
{
   uint32_t onoff;

   if (argc < 3)
     {
        printf("Error Check Args: enlightenment_info -effect [1: on, 0: off]\n");
        return;
     }

   onoff = atoi(argv[2]);

   if (onoff == 1 || onoff == 0)
     {
        if (!_e_info_client_eldbus_message_with_args("effect_control", NULL, "i", onoff))
          {
             printf("_e_info_client_eldbus_message_with_args error");
             return;
          }
     }
   else
     printf("Error Check Args: enlightenment_info -effect [1: on, 0: off]\n");
}

static void
_e_info_client_proc_aux_message(int argc, char **argv)
{
   const char *win, *key, *val;
   Eldbus_Message *msg;
   Eldbus_Message_Iter *itr, *opt_itr;
   Eldbus_Pending *p;
   int i;

   if (argc < 5)
     {
        printf("Error Check Args: enlightenment_info -aux_msg [window] [key] [val] [options]\n");
        return;
     }

   win = argv[2];
   key = argv[3];
   val = argv[4];

   msg = eldbus_proxy_method_call_new(e_info_client.proxy, "aux_msg");
   itr = eldbus_message_iter_get(msg);
   eldbus_message_iter_basic_append(itr, 's', win);
   eldbus_message_iter_basic_append(itr, 's', key);
   eldbus_message_iter_basic_append(itr, 's', val);

   opt_itr = eldbus_message_iter_container_new(itr, 'a', "s");
   for (i = 5; i < argc; i++)
     eldbus_message_iter_basic_append(opt_itr, 's', argv[i]);
   eldbus_message_iter_container_close(itr, opt_itr);

   p = eldbus_proxy_send(e_info_client.proxy, msg,
                        _e_info_client_eldbus_message_cb,
                         NULL, -1);
   if (!p)
     {
        printf("\"aux_msg\" proxy_send error");
        return;
     }

   ecore_main_loop_begin();
}

static void
_e_info_client_cb_scrsaver(const Eldbus_Message *msg)
{
   const char *name = NULL, *text = NULL;
   Eina_Bool res;
   const char *result = NULL;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_GOTO(res, finish);

   res = eldbus_message_arguments_get(msg,
                                      SIGNATURE_SCRSAVER_SERVER,
                                      &result);
   EINA_SAFETY_ON_FALSE_GOTO(res, finish);

   printf("%s\n", result);
   return;

finish:
   if ((name) || (text))
     {
        printf("errname:%s errmsg:%s\n", name, text);
     }
}

static void
_e_info_client_proc_scrsaver(int argc, char **argv)
{
   E_Info_Cmd_Scrsaver cmd = E_INFO_CMD_SCRSAVER_UNKNOWN;
   Eina_Bool res;
   double sec = 0.0;

   if (eina_streq(argv[2], "info"))
     {
        if (argc != 3) goto arg_err;
        cmd = E_INFO_CMD_SCRSAVER_INFO;
     }
   else if (eina_streq(argv[2], "enable"))
     {
        if (argc != 3) goto arg_err;
        cmd = E_INFO_CMD_SCRSAVER_ENABLE;
     }
   else if (eina_streq(argv[2], "disable"))
     {
        if (argc != 3) goto arg_err;
        cmd = E_INFO_CMD_SCRSAVER_DISABLE;
     }
   else if (eina_streq(argv[2], "timeout"))
     {
        if (argc != 4) goto arg_err;
        sscanf(argv[3], "%lf", &sec);
        cmd = E_INFO_CMD_SCRSAVER_TIMEOUT;

        printf("sec: %lf\n", sec);
     }
   else
     goto arg_err;

   res = _e_info_client_eldbus_message_with_args("scrsaver",
                                                 _e_info_client_cb_scrsaver,
                                                 SIGNATURE_SCRSAVER_CLIENT,
                                                 cmd, sec);
   EINA_SAFETY_ON_FALSE_RETURN(res);
   return;

arg_err:
   printf("Usage: enlightenment_info -scrsaver %s", USAGE_SCRSAVER);
}

static void
_e_info_client_proc_force_render(int argc, char **argv)
{
   E_Info_Cmd_Force_Render cmd = E_INFO_CMD_FRENDER_NONE;
   Eina_Bool res;

   if (eina_streq(argv[2], "all"))
     {
        if (argc != 3) goto arg_err;
        cmd = E_INFO_CMD_FRENDER_ALL;
     }
   else if (eina_streq(argv[2], "cls"))
     {
        if (argc != 3) goto arg_err;
        cmd = E_INFO_CMD_FRENDER_CLS;
     }
   else if (eina_streq(argv[2], "canvas"))
     {
        if (argc != 3) goto arg_err;
        cmd = E_INFO_CMD_FRENDER_CANVAS;
     }
   else
     goto arg_err;

   res = _e_info_client_eldbus_message_with_args("frender",
                                                 NULL,
                                                 "i",
                                                 cmd);
   EINA_SAFETY_ON_FALSE_RETURN(res);
   return;

arg_err:
   printf("Usage: enlightenment_info -frender %s", USAGE_FORCE_RENDER);

}

static struct
{
   const char *option;
   const char *params;
   const char *description;
   void (*func)(int argc, char **argv);
} procs[] =
{
   {
      "protocol_trace", "[console|file_path|disable]",
      "Enable/disable wayland protocol trace",
      _e_info_client_proc_protocol_trace
   },
   {
      "protocol_rule",
      PROTOCOL_RULE_USAGE,
      "Add/remove wayland protocol rule you want to trace",
      _e_info_client_proc_protocol_rule
   },
   {
      "topvwins", NULL,
      "Print top visible windows",
      _e_info_client_proc_topvwins_info
   },
   {
      "compobjs", "[simple]",
      "Display detailed information of all composite objects",
      _e_info_client_proc_compobjs_info
   },
   {
      "subsurface", NULL,
      "Print subsurface information",
      _e_info_client_proc_subsurface
   },
   {
      "dump_topvwins", "[directory_path]",
      "Dump top-level visible windows (default directory_path : current working directory)",
      _e_info_client_proc_topvwins_shot
   },
   {
      "eina_log_levels", "[mymodule1:5,mymodule2:2]",
      "Set EINA_LOG_LEVELS in runtime",
      _e_info_client_proc_eina_log_levels
   },
   {
      "eina_log_path", "[console|file_path]",
      "Set eina-log path in runtime",
      _e_info_client_proc_eina_log_path
   },
#ifdef HAVE_DLOG
   {
      "dlog",
      "[on:1,off:0]",
      "Logging using dlog system (on:1, off:0)",
      _e_info_client_proc_dlog_switch
   },
#endif
   {
      "prop", "[id]",
      "Print window infomation",
      _e_info_client_prop_prop_info
   },
   {
      "connected_clients", NULL,
      "Print connected clients on Enlightenment",
      _e_info_client_proc_connected_clients
   },
   {
      "rotation",
      ROTATION_USAGE,
      "Send a message about rotation",
      _e_info_client_proc_rotation
   },
   {
      "reslist",
      RESLIST_USAGE,
      "Print connected client's resources",
      _e_info_client_proc_res_lists
   },
   {
      "input_devices", NULL,
      "Print connected input devices",
      _e_info_client_proc_input_device_info
   },
   {
      "fps", NULL,
      "Print FPS in every sec",
      _e_info_client_proc_fps_info
   },
   {
      "punch", "[on/off] [<X>x<H>+<X>+<Y>] [<a>,<r>,<g>,<b>]",
      "HWC should be disabled first with \"-hwc\" option. Punch a UI framebuffer [on/off].",
      _e_info_client_proc_punch
   },
   {
      "transform",
      "[id enable x y w h angle is_bg]",
      "Set transform in runtime",
      _e_info_client_proc_transform_set
   },
   {
      "dump_buffers", DUMP_BUFFERS_USAGE,
      "Dump attach buffers [on:1,off:0] (default path:/tmp/dump_xxx/)",
      _e_info_client_proc_buffer_shot
   },
   {
      "output_mode", NULL,
      "Get output mode info",
      _e_info_client_proc_output_mode
   },
#ifdef ENABLE_HWC_MULTI
   {
      "hwc_trace",
      "[off: 0, on: 1, info:2]",
      "Show the hwc trace log",
      _e_info_client_proc_hwc_trace
   },
   {
      "hwc",
      "[on: 1, off: 0]",
      "On/Off the hw composite",
      _e_info_client_proc_hwc
   },
   {
      "show_plane_state",
      NULL,
      "show state of plane",
      _e_info_client_proc_show_plane_state
   },
   {
      "show_pending_commit",
      NULL,
      "show state of pending commit",
      _e_info_client_proc_show_pending_commit
   },
#endif
   {
      "keymap", NULL,
      "Print a current keymap",
      _e_info_client_proc_keymap_info
   },
   {
      "effect",
      "[on: 1, off: 0]",
      "On/Off the window effect",
      _e_info_client_proc_effect_control
   },
   {
      "keygrab_status", NULL,
      "Print a keygrab status",
      _e_info_client_proc_keygrab_status
   },
   {
      "module_info", NULL,
      "Print information maintained by extra modules",
      _e_info_client_proc_module_info
   },
   {
      "aux_msg",
      "[window] [key] [value] [options]",
      "send aux message to client",
      _e_info_client_proc_aux_message
   },
   {
      "scrsaver",
      USAGE_SCRSAVER,
      "Set parameters of the screen saver",
      _e_info_client_proc_scrsaver
   },
   {
      "slot",
      USAGE_SLOT,
      "Set slot in runtime",
      _e_info_client_proc_slot_set
   },
   {
      "desk",
      NULL,
      "current desktop",
      _e_info_client_proc_desk
   },
   {
      "frender",
      USAGE_FORCE_RENDER,
      "force render according to parameters",
      _e_info_client_proc_force_render
   }
};

static void
_e_info_client_eldbus_message_cb(void *data, const Eldbus_Message *msg, Eldbus_Pending *p EINA_UNUSED)
{
   E_Info_Message_Cb cb = (E_Info_Message_Cb)data;

   if (cb) cb(msg);

   ecore_main_loop_quit();
}

static Eina_Bool
_e_info_client_eldbus_message(const char *method, E_Info_Message_Cb cb)
{
   Eldbus_Pending *p;

   p = eldbus_proxy_call(e_info_client.proxy, method,
                         _e_info_client_eldbus_message_cb,
                         cb, -1, "");
   EINA_SAFETY_ON_NULL_RETURN_VAL(p, EINA_FALSE);

   ecore_main_loop_begin();
   return EINA_TRUE;
}

static Eina_Bool
_e_info_client_eldbus_message_with_args(const char *method, E_Info_Message_Cb cb, const char *signature, ...)
{
   Eldbus_Pending *p;
   va_list ap;

   va_start(ap, signature);
   p = eldbus_proxy_vcall(e_info_client.proxy, method,
                          _e_info_client_eldbus_message_cb,
                          cb, -1, signature, ap);
   va_end(ap);
   EINA_SAFETY_ON_NULL_RETURN_VAL(p, EINA_FALSE);

   ecore_main_loop_begin();
   return EINA_TRUE;
}

static void
_e_info_client_eldbus_disconnect(void)
{
   if (e_info_client.proxy)
     {
        eldbus_proxy_unref(e_info_client.proxy);
        e_info_client.proxy = NULL;
     }

   if (e_info_client.obj)
     {
        eldbus_object_unref(e_info_client.obj);
        e_info_client.obj = NULL;
     }

   if (e_info_client.conn)
     {
        eldbus_connection_unref(e_info_client.conn);
        e_info_client.conn = NULL;
     }

   if (e_info_client.eldbus_init)
     {
        eldbus_shutdown();
        e_info_client.eldbus_init = 0;
     }
}

static Eina_Bool
_e_info_client_eldbus_connect(void)
{
   e_info_client.eldbus_init = eldbus_init();
   EINA_SAFETY_ON_FALSE_GOTO(e_info_client.eldbus_init > 0, err);

   e_info_client.conn = eldbus_connection_get(ELDBUS_CONNECTION_TYPE_SYSTEM);
   EINA_SAFETY_ON_NULL_GOTO(e_info_client.conn, err);

   e_info_client.obj = eldbus_object_get(e_info_client.conn,
                                         "org.enlightenment.wm",
                                         "/org/enlightenment/wm");
   EINA_SAFETY_ON_NULL_GOTO(e_info_client.obj, err);

   e_info_client.proxy = eldbus_proxy_get(e_info_client.obj, "org.enlightenment.wm.info");
   EINA_SAFETY_ON_NULL_GOTO(e_info_client.proxy, err);

   return EINA_TRUE;

err:
   _e_info_client_eldbus_disconnect();
   return EINA_FALSE;
}

static Eina_Bool
_e_info_client_process(int argc, char **argv)
{
   int nproc = sizeof(procs) / sizeof(procs[0]);
   int i;

   signal(SIGINT,  end_program);
   signal(SIGALRM, end_program);
   signal(SIGHUP,  end_program);
   signal(SIGPIPE, end_program);
   signal(SIGQUIT, end_program);
   signal(SIGTERM, end_program);

   for (i = 0; i < nproc; i++)
     {
        if (!strncmp(argv[1]+1, procs[i].option, strlen(procs[i].option)))
          {
             if (procs[i].func)
               procs[i].func(argc, argv);

             return EINA_TRUE;
          }
     }

   return EINA_FALSE;
}

static void
_e_info_client_print_usage(const char *exec)
{
   int nproc = sizeof(procs) / sizeof(procs[0]);
   int i;

   printf("\nUsage:\n");

   for (i = 0; i < nproc; i++)
     printf("  %s -%s %s\n", exec, procs[i].option, (procs[i].params)?procs[i].params:"");

   printf("\nOptions:\n");

   for (i = 0; i < nproc; i++)
     {
        printf("  -%s\n", procs[i].option);
        printf("      %s\n", (procs[i].description)?procs[i].description:"");
     }

   printf("\n");
}

static void
end_program(int sig)
{
   keepRunning = 0;
}

int
main(int argc, char **argv)
{
   if (argc < 2 || argv[1][0] != '-')
     {
        _e_info_client_print_usage(argv[0]);
        return 0;
     }

   /* connecting dbus */
   if (!_e_info_client_eldbus_connect())
     goto err;

   if (!strcmp(argv[1], "-h") ||
       !strcmp(argv[1], "-help") ||
       !strcmp(argv[1], "--help"))
     {
        _e_info_client_print_usage(argv[0]);
     }
   else
     {
        /* handling a client request */
        if (!_e_info_client_process(argc, argv))
          {
             printf("unknown option: %s\n", argv[1]);
             _e_info_client_print_usage(argv[0]);
          }
     }

   /* disconnecting dbus */
   _e_info_client_eldbus_disconnect();

   return 0;

err:
   _e_info_client_eldbus_disconnect();
   return -1;
}
