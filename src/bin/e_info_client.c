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
   int                use_gl, use_hwc, use_multi_layer, hwc, hwc_windows;
   int                use_buffer_flush, deiconify_approve;
   int                config_engine;
   const char        *engine;
   Eina_List         *win_list;

   Eina_List         *input_dev;

   /* output mode */
   Eina_List         *mode_list;
   int               gl;

   /* pending_commit */
   Eina_List         *pending_commit_list;

   /* layer fps */
   Eina_List         *fps_list;

   /* dump_buffers */
   const char *dump_fullpath;
   Eina_Bool dump_success;
} E_Info_Client;

typedef struct _E_Win_Info
{
   Ecore_Window     id;         // native window id
   uint32_t      res_id;
   int           pid;
   const char  *name;       // name of client window
   int          x, y, w, h; // geometry
   int          layer;      // value of E_Layer
   int          vis;        // ec->visible
   int          mapped;     // map state (ec->comp_data->mapped)
   int          alpha;      // alpha window
   int          opaque;
   int          visibility; // visibillity
   int          iconic;
   int          frame_visible;  //ec->frame obj visible get
   int          focused;
   int          hwc;
   int          pl_zpos;
   Ecore_Window parent_id;
   const char  *layer_name; // layer name
   Eina_Bool has_input_region;
} E_Win_Info;

typedef struct output_mode_info
{
   unsigned int hdisplay, hsync_start, hsync_end, htotal;
   unsigned int vdisplay, vsync_start, vsync_end, vtotal;
   unsigned int refresh, vscan, clock;
   unsigned int flags;
   int current, output, connect, dpms;
   const char *name;
} E_Info_Output_Mode;

typedef struct _E_Pending_Commit_Info
{
   unsigned int plane;
   int zpos;
   unsigned int data;
   unsigned int tsurface;
} E_Pending_Commit_Info;

typedef struct _E_Fps_Info
{
   E_Info_Fps_Type type;
   const char *output;
   int zpos;
   unsigned int window;
   double fps;
} E_Fps_Info;

#define VALUE_TYPE_FOR_TOPVWINS "uuisiiiiibbbiiibbiiusb"
#define VALUE_TYPE_REQUEST_RESLIST "ui"
#define VALUE_TYPE_REPLY_RESLIST "ssi"
#define VALUE_TYPE_FOR_INPUTDEV "ssi"
#define VALUE_TYPE_FOR_PENDING_COMMIT "uiuu"
#define VALUE_TYPE_FOR_FPS "usiud"
#define VALUE_TYPE_REQUEST_FOR_KILL "uts"
#define VALUE_TYPE_REPLY_KILL "s"
#define VALUE_TYPE_REQUEST_FOR_WININFO "t"
#define VALUE_TYPE_REPLY_WININFO "uiiiiiibbiibbbiitsiiib"
#define VALUE_TYPE_REQUEST_FOR_WININFO_TREE "ti"
#define VALUE_TYPE_REPLY_WININFO_TREE "tsia(tsiiiiiiii)"

static E_Info_Client e_info_client;

static Eina_Bool compobjs_simple = EINA_FALSE;

static void end_program(int sig);
static Eina_Bool _e_info_client_eldbus_message(const char *method, E_Info_Message_Cb cb);
static Eina_Bool _e_info_client_eldbus_message_with_args(const char *method, E_Info_Message_Cb cb, const char *signature, ...);
static void _e_info_client_eldbus_message_cb(void *data, const Eldbus_Message *msg, Eldbus_Pending *p);

static Eina_Bool
_util_string_to_uint(const char *str, unsigned int *num, int base)
{
   char *end;
   int errsv;

   EINA_SAFETY_ON_NULL_RETURN_VAL(str, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(num, EINA_FALSE);

   const unsigned long int ul = strtoul(str, &end, base);
   errsv = errno;

   EINA_SAFETY_ON_TRUE_RETURN_VAL((end == str), EINA_FALSE); /* given string is not a decimal number */
   EINA_SAFETY_ON_TRUE_RETURN_VAL(('\0' != *end), EINA_FALSE); /* given string has extra characters */
   EINA_SAFETY_ON_TRUE_RETURN_VAL(((ULONG_MAX == ul) && (ERANGE == errsv)), EINA_FALSE); /* out of range of type unsigned long int */
   EINA_SAFETY_ON_TRUE_RETURN_VAL((ul > UINT_MAX), EINA_FALSE); /* greater than UINT_MAX */

   *num = (unsigned int)ul;

   return EINA_TRUE;
}

/* buff: string to be parsed
 * next: return values it contains the address of the first invalid character
 * num: return value it contains integer value according to the given base
 */
static Eina_Bool
_util_string_to_int_token(const char *str, char **next, int *num, int base)
{
   int errsv;

   EINA_SAFETY_ON_NULL_RETURN_VAL(str, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(next, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(num, EINA_FALSE);

   const long int sl = strtol(str, next, base);
   errsv = errno;

   EINA_SAFETY_ON_TRUE_RETURN_VAL((*next == str), EINA_FALSE); /* given string is not a decimal number */
   EINA_SAFETY_ON_TRUE_RETURN_VAL(((LONG_MIN == sl || LONG_MAX == sl) && (ERANGE == errsv)), EINA_FALSE); /* out of range of type long */
   EINA_SAFETY_ON_TRUE_RETURN_VAL((sl > INT_MAX), EINA_FALSE); /* greater than INT_MAX */
   EINA_SAFETY_ON_TRUE_RETURN_VAL((sl < INT_MIN), EINA_FALSE); /* less than INT_MIN */

   *num = (int)sl;

   return EINA_TRUE;
}

static Eina_Bool
_util_string_to_double(const char *str, double *num)
{
   char *end;
   int errsv;

   EINA_SAFETY_ON_FALSE_RETURN_VAL(str, EINA_FALSE);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(num, EINA_FALSE);

   const double sd = strtod(str, &end);
   errsv = errno;

   EINA_SAFETY_ON_TRUE_RETURN_VAL((end == str), EINA_FALSE); /* given string is not a floating point number */
   EINA_SAFETY_ON_TRUE_RETURN_VAL(('\0' != *end), EINA_FALSE); /* given string has extra characters */
   EINA_SAFETY_ON_TRUE_RETURN_VAL(((DBL_MIN == sd || DBL_MAX == sd) && (ERANGE == errsv)), EINA_FALSE); /* out of range of type double */

   *num = sd;

   return EINA_TRUE;
}

static Eina_Bool
_util_string_to_ulong(const char *str, unsigned long *num, int base)
{
   char *end;
   int errsv;

   EINA_SAFETY_ON_NULL_RETURN_VAL(str, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(num, EINA_FALSE);

   const long sul = strtoul(str, &end, base);
   errsv = errno;

   EINA_SAFETY_ON_TRUE_RETURN_VAL((end == str), EINA_FALSE); /* given string is not a decimal number */
   EINA_SAFETY_ON_TRUE_RETURN_VAL(('\0' != *end), EINA_FALSE); /* given string has extra characters */
   EINA_SAFETY_ON_TRUE_RETURN_VAL(((ULONG_MAX == sul) && (ERANGE == errsv)), EINA_FALSE); /* out of range of type unsigned long */

   *num = (int)sul;

   return EINA_TRUE;
}

static void
_e_signal_get_window_under_touch(void *data, const Eldbus_Message *msg)
{
   Eina_Bool res;
   uint64_t w;
   Ecore_Window *win = data;

   res = eldbus_message_arguments_get(msg, "t", &w);
   EINA_SAFETY_ON_FALSE_GOTO(res, finish);

   *win = (Ecore_Window)w;

finish:
   ecore_main_loop_quit();
}

static void
_e_message_get_window_under_touch(const Eldbus_Message *msg)
{
   const char *name = NULL, *text = NULL;
   Eina_Bool res;
   int result = 0;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_GOTO(res, finish);

   res = eldbus_message_arguments_get(msg, "i", &result);
   EINA_SAFETY_ON_FALSE_GOTO(res, finish);
   EINA_SAFETY_ON_TRUE_GOTO(result, finish);

   return;

finish:
  if ((name) || (text))
    {
       printf("errname:%s errmsg:%s\n", name, text);
    }

   ecore_main_loop_quit();
}

static int
_e_get_window_under_touch(Ecore_Window *win)
{
   Eina_Bool res;
   Eldbus_Signal_Handler *signal_handler = NULL;

   *win = 0;

   signal_handler = eldbus_proxy_signal_handler_add(e_info_client.proxy, "win_under_touch", _e_signal_get_window_under_touch, win);
   EINA_SAFETY_ON_NULL_GOTO(signal_handler, fail);

   res = _e_info_client_eldbus_message("get_win_under_touch",
                                       _e_message_get_window_under_touch);
   EINA_SAFETY_ON_FALSE_GOTO(res, fail);

   ecore_main_loop_begin();

   eldbus_signal_handler_del(signal_handler);

   return 0;

fail:
   if (signal_handler)
     eldbus_signal_handler_del(signal_handler);

   return -1;
}

static void
_e_message_get_window_name(void *data, const Eldbus_Message *msg, Eldbus_Pending *p EINA_UNUSED)
{
   char **win = data;

   const char *name = NULL, *text = NULL;
   Eina_Bool res;
   const char *w;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_GOTO(res, finish);

   res = eldbus_message_arguments_get(msg, "s", &w);
   EINA_SAFETY_ON_FALSE_GOTO(res, finish);
   EINA_SAFETY_ON_NULL_GOTO(w, finish);

   *win = strdup(w);

   ecore_main_loop_quit();

   return;

finish:
   if ((name) || (text))
     {
        printf("errname:%s errmsg:%s\n", name, text);
     }

   ecore_main_loop_quit();
}

static char *
_e_get_window_name(uint64_t win)
{
   Eldbus_Pending *p;
   char *win_name = NULL;

   p = eldbus_proxy_call(e_info_client.proxy, "get_window_name",
                         _e_message_get_window_name,
                         &win_name, -1, "t", win);
   EINA_SAFETY_ON_NULL_RETURN_VAL(p, NULL);

   ecore_main_loop_begin();

   return win_name;
}

static void
_e_message_get_windows(void *data, const Eldbus_Message *msg, Eldbus_Pending *p EINA_UNUSED)
{
   const char *name = NULL, *text = NULL;
   Eina_Bool res;
   Eldbus_Message_Iter *array_of_windows;
   uint64_t win;
   Eina_List **win_list = data;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_GOTO(res, finish);

   res = eldbus_message_arguments_get(msg, "at", &array_of_windows);
   EINA_SAFETY_ON_FALSE_GOTO(res, finish);

   while (eldbus_message_iter_get_and_next(array_of_windows, 't', &win))
     {
        *win_list = eina_list_append(*win_list, (void *)((Ecore_Window)win));
     }

   ecore_main_loop_quit();

   return;

finish:
   if ((name) || (text))
     {
        printf("errname:%s errmsg:%s\n", name, text);
     }

   ecore_main_loop_quit();
}

const static int _E_GET_WINDOWS_NAME_MODE = 1;
const static int _E_GET_WINDOWS_PID_MODE = 2;

static Eina_List *
_e_get_windows(int mode, char *value)
{
   Eldbus_Pending *p;
   Eina_List *win_list = NULL;

   p = eldbus_proxy_call(e_info_client.proxy, "get_windows",
                         _e_message_get_windows,
                         &win_list, -1, "is", mode, value);
   EINA_SAFETY_ON_NULL_RETURN_VAL(p, NULL);

   ecore_main_loop_begin();

   return win_list;
}

static E_Win_Info *
_e_win_info_new(Ecore_Window id, uint32_t res_id, int pid, Eina_Bool alpha, int opaque, const char *name, int x, int y, int w, int h, int layer, int visible, int mapped, int visibility, int iconic, int frame_visible, int focused, int hwc, int pl_zpos, Ecore_Window parent_id, const char *layer_name, Eina_Bool has_input_region)
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
   win->mapped = mapped;
   win->visibility = visibility;
   win->frame_visible = frame_visible;
   win->iconic = iconic;
   win->focused = focused;
   win->hwc = hwc;
   win->pl_zpos = pl_zpos;
   win->parent_id = parent_id;
   win->layer_name = eina_stringshare_add(layer_name);
   win->has_input_region = has_input_region;

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
_e_win_info_make_array(Eldbus_Message_Iter *array)
{
   Eldbus_Message_Iter *ec;
   Eina_Bool res;

   while (eldbus_message_iter_get_and_next(array, 'r', &ec))
     {
        const char *win_name;
        const char *layer_name;
        int x, y, w, h, layer, visibility, opaque, hwc, pl_zpos;
        Eina_Bool visible, mapped, alpha, iconic, focused, frame_visible;
        Ecore_Window id, parent_id;
        uint32_t res_id;
        int pid;
        E_Win_Info *win = NULL;
        Eina_Bool has_input_region = EINA_FALSE;
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
                                                &mapped,
                                                &alpha,
                                                &opaque,
                                                &visibility,
                                                &iconic,
                                                &frame_visible,
                                                &focused,
                                                &hwc,
                                                &pl_zpos,
                                                &parent_id,
                                                &layer_name,
                                                &has_input_region);
        if (!res)
          {
             printf("Failed to get win info\n");
             continue;
          }

        win = _e_win_info_new(id, res_id, pid, alpha, opaque, win_name, x, y, w, h, layer, visible, mapped, visibility, iconic, frame_visible, focused, hwc, pl_zpos, parent_id, layer_name, has_input_region);
        e_info_client.win_list = eina_list_append(e_info_client.win_list, win);
     }
}

static void
_cb_window_info_get(const Eldbus_Message *msg)
{
   const char *name = NULL, *text = NULL;
   Eldbus_Message_Iter *array;
   Eina_Bool res;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_GOTO(res, finish);

   res = eldbus_message_arguments_get(msg, "a("VALUE_TYPE_FOR_TOPVWINS")", &array);
   EINA_SAFETY_ON_FALSE_GOTO(res, finish);

   _e_win_info_make_array(array);

finish:
   if ((name) || (text))
     {
        printf("errname:%s errmsg:%s\n", name, text);
     }
}

static void
_cb_vwindow_info_get(const Eldbus_Message *msg)
{
   const char *name = NULL, *text = NULL;
   Eldbus_Message_Iter *array;
   char *engine;
   Eina_Bool res;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_GOTO(res, finish);

   res = eldbus_message_arguments_get(msg, "iiiiisiiia("VALUE_TYPE_FOR_TOPVWINS")",
                                      &e_info_client.use_gl, &e_info_client.use_hwc, &e_info_client.use_multi_layer,
                                      &e_info_client.hwc, &e_info_client.hwc_windows,
                                      &engine, &e_info_client.config_engine,
                                      &e_info_client.use_buffer_flush, &e_info_client.deiconify_approve,
                                      &array);
   EINA_SAFETY_ON_FALSE_GOTO(res, finish);
   e_info_client.engine = eina_stringshare_add(engine);

   _e_win_info_make_array(array);

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
        "===========================================================================================================================\n"
        "                        /-- Object Type        /-- Alpha                                                                   \n"
        "                        |    r  : Rectangle    |                                          /-- Edj: group                   \n"
        "                        |    EDJ: Edje         | /-- Pass Events                          |   Edj Member: part, value      \n"
        "                        |    IMG: Image        | |/-- Freeze Events                       |   Native Image:                \n"
        "                        |    EC : ec->frame    | ||/-- Focused                            |    type, buff, size, load, fill\n"
        "                        |                      | |||/-- EvasMap                           |                      size  size\n"
        "                        |                      | ||||                                     |   File Image:                  \n"
        "                        |                      | |||| /-- Visibility                      |    data, size, load, fill      \n"
        "                        |                      | |||| |                                   |                size  size      \n"
        "                        |                      | |||| |                                   |                                \n"
        "========================|======================|=||||=|===================================|================================\n"
        "Layer  ObjectID         |     X    Y    W    H | |||| |   ObjectName                      | Additional Info                \n"
        "========================|======================|=||||=|===================================|================================\n"
        );
   else
     printf(
        "=======================================================================================================================\n"
        "                        /-- Object Type                            /-- Alpha                                           \n"
        "                        |    r  : Rectangle Object                 |                                                   \n"
        "                        |    EDJ: Edje Object                      | /-- Pass Events                                   \n"
        "                        |    IMG: Image Object                     | |/-- Freeze Events                                \n"
        "                        |    EC : ec->frame Object                 | ||/-- Focused                                     \n"
        "                        |                                          | |||/-  EvasMap                                    \n"
        "                        |                                          | ||||                                              \n"
        "                        |    /-- Render Operation                  | |||| /-- Visibility                               \n"
        "                        |    |    BL: EVAS_RENDER_BLEND            | |||| |                                            \n"
        "                        |    |    CP: EVAS_RENDER_COPY             | |||| |                                            \n"
        "                        |    |                                     | |||| |                           [Additional Info]\n"
        "                        |    |                                     | |||| |                          EDJ: group, file |\n"
        "                        |    |                                     | |||| |                   EDJ member: part, value |\n"
        "                        |    |                                     | |||| |   Image: Type, Size, Load Size, Fill Size |\n"
        "                        |    |                                     | |||| |             Map: Enable, Alpha, UV, Coord |\n"
        "                        |    |                                     | |||| |                                           |\n"
        "========================|====|=====================================|=||||=|============================================\n"
        "Layer  ObjectID         |    |    X    Y    W    H  Color(RGBA)    | |||| |     ObjectName                            |\n"
        "========================|====|=====================================|=||||=|============================================\n"
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
                                                &cobj.img.dirty,
                                                &cobj.map.enable,
                                                &cobj.map.alpha,
                                                &cobj.map.u[0], &cobj.map.u[1], &cobj.map.u[2], &cobj.map.u[3],
                                                &cobj.map.v[0], &cobj.map.v[1], &cobj.map.v[2], &cobj.map.v[3],
                                                &cobj.map.x[0], &cobj.map.x[1], &cobj.map.x[2], &cobj.map.x[3],
                                                &cobj.map.y[0], &cobj.map.y[1], &cobj.map.y[2], &cobj.map.y[3],
                                                &cobj.map.z[0], &cobj.map.z[1], &cobj.map.z[2], &cobj.map.z[3]);
        if (!res)
          {
             printf("Failed to get composite obj info\n");
             continue;
          }

        if (cobj.depth == 0)
          {
             if (!compobjs_simple)
               printf(" - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -- - - - - - - - - |\n");
             printf("%4d ", cobj.ly);
          }
        else
          printf("     ");

        for (i = 0; i < cobj.depth; i++) printf(" ");
        printf("%08x ", cobj.obj);
        for (i = 6; i > cobj.depth; i--) printf(" ");

        if (compobjs_simple)
          printf("%5.5s "
                 "|%4d,%4d %4dx%4d|%s|%s%s%s%s|%s|",
                 cobj.type,
                 cobj.x, cobj.y, cobj.w, cobj.h,
                 cobj.img.alpha == 1 ? "A" : " ",
                 cobj.pass_events == 1 ? "p" : " ",
                 cobj.freeze_events == 1 ? "z" : " ",
                 cobj.focus == 1 ? "F" : " ",
                 cobj.map.enable == 1 ? "M" : " ",
                 cobj.vis == 1 ? "V" : " ");
        else
          printf("%5.5s "
                 "|%3.3s"
                 "|%4d,%4d %4dx%4d|%3d %3d %3d %3d|%s|%s%s%s%s|%s|",
                 cobj.type,
                 cobj.opmode,
                 cobj.x, cobj.y, cobj.w, cobj.h,
                 cobj.r, cobj.g, cobj.b, cobj.a,
                 cobj.img.alpha == 1 ? "A" : " ",
                 cobj.pass_events == 1 ? "p" : " ",
                 cobj.freeze_events == 1 ? "z" : " ",
                 cobj.focus == 1 ? "F" : " ",
                 cobj.map.enable == 1 ? "M" : " ",
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
        if (!compobjs_simple && cobj.map.enable)
          {
             printf("                                                                                                            ");
             printf("|Map: %s\n", (cobj.map.alpha == 1) ? "alpha(on)" : "alpha(off)");
             printf("                                                                                                            ");
             printf("|    UV (  %4d,%4d   |  %4d,%4d   |  %4d,%4d   |  %4d,%4d   )\n",
                    (int)cobj.map.u[0], (int)cobj.map.v[0],
                    (int)cobj.map.u[1], (int)cobj.map.v[1],
                    (int)cobj.map.u[2], (int)cobj.map.v[2],
                    (int)cobj.map.u[3], (int)cobj.map.v[3]);
             printf("                                                                                                            ");
             printf("| Coord (%4d,%4d,%4d|%4d,%4d,%4d|%4d,%4d,%4d|%4d,%4d,%4d)\n",
                    cobj.map.x[0], cobj.map.y[0], cobj.map.z[0],
                    cobj.map.x[1], cobj.map.y[1], cobj.map.z[1],
                    cobj.map.x[2], cobj.map.y[2], cobj.map.z[2],
                    cobj.map.x[3], cobj.map.y[3], cobj.map.z[3]);
          }
     }

   if (compobjs_simple)
     printf("===========================================================================================================================\n");
   else
     printf("=======================================================================================================================\n");

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
   E_Devicemgr_Input_Device *dev = NULL;

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

        dev = E_NEW(E_Devicemgr_Input_Device, 1);
        EINA_SAFETY_ON_NULL_GOTO(dev, finish);

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
        xkb_context_unref(context);
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

        printf("\t%4d%-5s%-25s%-20x%-5d\n", i, "", keyname, sym, xkb_keymap_key_repeats(keymap, i));
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
   Eina_Bool res = EINA_FALSE;

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
        EINA_SAFETY_ON_NULL_RETURN(new_s1);

        snprintf(new_s1, PATH_MAX, "%s", "no_data");
        new_argv[1] = new_s1;
        new_argc++;
     }
   if (new_argc < 3)
     {
        new_s2 = (char *)calloc (1, PATH_MAX);
        EINA_SAFETY_ON_NULL_GOTO(new_s2, finish);

        snprintf(new_s2, PATH_MAX, "%s", "no_data");
        new_argv[2] = new_s2;
        new_argc++;
     }
   if (new_argc != 3)
     {
        printf("protocol-trace: Usage> enlightenment_info -protocol_rule [add | remove | print | help] [allow/deny/all]\n");
        goto finish;
     }

   res = _e_info_client_eldbus_message_with_args("protocol_rule", _cb_protocol_rule, "sss", new_argv[0], new_argv[1], new_argv[2]);
   if (!res) printf("Error occured while send send message\n\n");

finish:
   if (new_s1) free(new_s1);
   if (new_s2) free(new_s2);
}

static void
_cb_vec_info_get(const Eldbus_Message *msg)
{
   const char *name = NULL, *text = NULL;
   Eldbus_Message_Iter *array;
   Eina_Bool res;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_GOTO(res, finish);

   res = eldbus_message_arguments_get(msg, "a("VALUE_TYPE_FOR_TOPVWINS")", &array);
   EINA_SAFETY_ON_FALSE_GOTO(res, finish);

   _e_win_info_make_array(array);

finish:
   if ((name) || (text))
     {
        printf("errname:%s errmsg:%s\n", name, text);
     }
}

static void
_e_info_client_proc_ec_list_info(void)
{
   E_Win_Info *win;
   Eina_List *l;
   int i = 0;
   int prev_layer = -1;
   int hwc_off = 0;

   const char *prev_layer_name = NULL;

   if (!_e_info_client_eldbus_message("get_ec_info", _cb_vec_info_get))
     return;

   printf("\n\n%d Top level windows in EC list\n", eina_list_count(e_info_client.win_list));
   printf("--------------------------------------[ topvwins ]----------------------------------------------------------------------------------\n");
   printf(" No   Win_ID    RcsID    PID     w     h       x      y  Foc InReg Dep Opaq Vsbt Icon Vis Map  Frame  PL@ZPos  Parent     Title\n");
   printf("------------------------------------------------------------------------------------------------------------------------------------\n");

   if (!e_info_client.win_list)
     {
        printf("no ECs\n");
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
                printf("------------------------------------------------------------------------------------------------------------------------------------[%s]\n",
                       prev_layer_name ? prev_layer_name : " ");
             prev_layer = win->layer;
             prev_layer_name = win->layer_name;
          }

        if (win->hwc >= 0)
          {
             if ((!win->iconic) && (win->frame_visible))
               {
                  if (win->pl_zpos == -999)
                    snprintf(tmp, sizeof(tmp), " - ");
                  else
                    {
                       if (win->hwc) snprintf(tmp, sizeof(tmp), "hwc@%i", win->pl_zpos);
                       else snprintf(tmp, sizeof(tmp), "comp@%i", win->pl_zpos);
                    }
               }
             else
               snprintf(tmp, sizeof(tmp), " - ");
          }
        else // hwc is not initialized or hwc_deactive 1
          {
             hwc_off = 1;
             snprintf(tmp, sizeof(tmp), " - ");
          }

        printf("%3d 0x%08zx  %5d  %5d  %5d %5d %6d %6d   %c    %c   %3d  %2d   ", i, win->id, win->res_id, win->pid, win->w, win->h, win->x, win->y, win->focused ? 'O':' ', win->has_input_region?'C':' ', win->alpha? 32:24, win->opaque);
        printf("%2d    %d   %d   %s   %3d    %-8s %-8zx   %s\n", win->visibility, win->iconic, win->vis, win->mapped? "V":"N", win->frame_visible, tmp, win->parent_id, win->name?:"No Name");
     }

   if (prev_layer_name)
      printf("------------------------------------------------------------------------------------------------------------------------------------[%s]\n",
             prev_layer_name ? prev_layer_name : " ");

   if(hwc_off)
     printf("\nHWC is disabled\n\n");

   E_FREE_LIST(e_info_client.win_list, _e_win_info_free);
   if (e_info_client.engine)
     {
        eina_stringshare_del(e_info_client.engine);
        e_info_client.engine = NULL;
     }
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

   if (!_e_info_client_eldbus_message("get_window_info", _cb_vwindow_info_get))
     goto ec_info;

   printf("GL :  %s\n", e_info_client.use_gl ? "on":"off");
   printf("ENG:  %s (config: %d)\n", e_info_client.engine, e_info_client.config_engine);
   if (e_info_client.use_hwc)
     {
        if (e_info_client.hwc)
          {
             printf("HWC:  ");
             if (e_info_client.hwc_windows)
               printf("hwc windows policy\n");
             else
               printf("hwc planes policy and multiple plane is %s\n", e_info_client.use_multi_layer ? "on":"off");
          }
        else
          printf("HWC:  off");
     }
   else
     printf("HWC:  configuration is off");

   printf("Buffer flush: %s\n", e_info_client.use_buffer_flush ? "on":"off");
   if (e_info_client.use_buffer_flush)
     printf("Deiconify Approve: %s\n", "auto on");
   else
     printf("Deiconify Approve: %s\n", e_info_client.deiconify_approve ? "on":"off");

   printf("\n%d Top level windows in evas object list\n", eina_list_count(e_info_client.win_list));
   printf("--------------------------------------[ topvwins ]----------------------------------------------------------------------------------\n");
   printf(" No   Win_ID    RcsID    PID     w     h       x      y  Foc InReg Dep Opaq Vsbt Icon Vis Map  Frame  PL@ZPos  Parent     Title\n");
   printf("------------------------------------------------------------------------------------------------------------------------------------\n");

   if (!e_info_client.win_list)
     {
        printf("no window\n");
        goto ec_info;
     }

   EINA_LIST_FOREACH(e_info_client.win_list, l, win)
     {
        if (!win) goto ec_info;
        char tmp[20];
        i++;
        if (win->layer != prev_layer)
          {
             if (prev_layer != -1)
                printf("------------------------------------------------------------------------------------------------------------------------------------[%s]\n",
                       prev_layer_name ? prev_layer_name : " ");
             prev_layer = win->layer;
             prev_layer_name = win->layer_name;
          }

        if (win->hwc >= 0)
          {
             if ((!win->iconic) && (win->frame_visible))
               {
                  if (win->pl_zpos == -999)
                    snprintf(tmp, sizeof(tmp), " - ");
                  else
                    {
                       if (win->hwc) snprintf(tmp, sizeof(tmp), "hwc@%i", win->pl_zpos);
                       else snprintf(tmp, sizeof(tmp), "comp@%i", win->pl_zpos);
                    }
               }
             else
               snprintf(tmp, sizeof(tmp), " - ");
          }
        else // hwc is not initialized or hwc_deactive 1
          {
             hwc_off = 1;
             snprintf(tmp, sizeof(tmp), " - ");
          }

        printf("%3d 0x%08zx  %5d  %5d  %5d %5d %6d %6d   %c    %c   %3d  %2d   ", i, win->id, win->res_id, win->pid, win->w, win->h, win->x, win->y, win->focused ? 'O':' ', win->has_input_region?'C':' ', win->alpha? 32:24, win->opaque);
        printf("%2d    %d   %d   %s   %3d    %-8s %-8zx   %s\n", win->visibility, win->iconic, win->vis, win->mapped? "V":"N", win->frame_visible, tmp, win->parent_id, win->name?:"No Name");
     }

   if (prev_layer_name)
      printf("------------------------------------------------------------------------------------------------------------------------------------[%s]\n",
             prev_layer_name ? prev_layer_name : " ");

   if(hwc_off)
     printf("\nHWC is disabled\n\n");

   E_FREE_LIST(e_info_client.win_list, _e_win_info_free);
   if (e_info_client.engine)
     {
        eina_stringshare_del(e_info_client.engine);
        e_info_client.engine = NULL;
     }

ec_info:
   _e_info_client_proc_ec_list_info();

}

static void
_e_info_client_proc_topwins_info(int argc, char **argv)
{
   E_Win_Info *win;
   Eina_List *l;
   int i = 0;
   int prev_layer = -1;
   int hwc_off = 0;

   const char *prev_layer_name = NULL;

   if (!_e_info_client_eldbus_message("get_all_window_info", _cb_window_info_get))
     return;

   printf("%d Top level windows\n", eina_list_count(e_info_client.win_list));
   printf("--------------------------------------[ topvwins ]----------------------------------------------------------------------------------\n");
   printf(" No   Win_ID    RcsID    PID     w     h       x      y  Foc InReg Dep Opaq Vsbt Icon Vis Map  Frame  PL@ZPos  Parent     Title\n");
   printf("------------------------------------------------------------------------------------------------------------------------------------\n");

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
                printf("------------------------------------------------------------------------------------------------------------------------------------[%s]\n",
                       prev_layer_name ? prev_layer_name : " ");
             prev_layer = win->layer;
             prev_layer_name = win->layer_name;
          }

        if (win->hwc >= 0)
          {
             if ((!win->iconic) && (win->frame_visible))
               {
                  if (win->pl_zpos == -999)
                    snprintf(tmp, sizeof(tmp), " - ");
                  else
                    {
                       if (win->hwc) snprintf(tmp, sizeof(tmp), "hwc@%i", win->pl_zpos);
                       else snprintf(tmp, sizeof(tmp), "comp@%i", win->pl_zpos);
                    }
               }
             else
               snprintf(tmp, sizeof(tmp), " - ");
          }
        else // hwc is not initialized or hwc_deactive 1
          {
             hwc_off = 1;
             snprintf(tmp, sizeof(tmp), " - ");
          }

        printf("%3d 0x%08zx  %5d  %5d  %5d %5d %6d %6d   %c    %c   %3d  %2d   ", i, win->id, win->res_id, win->pid, win->w, win->h, win->x, win->y, win->focused ? 'O':' ', win->has_input_region ? 'C':' ',win->alpha? 32:24, win->opaque);
        printf("%2d    %d   %d   %s   %3d    %-8s %-8zx   %s\n", win->visibility, win->iconic, win->vis, win->mapped? "V":"N", win->frame_visible, tmp, win->parent_id, win->name?:"No Name");
     }

   if (prev_layer_name)
      printf("------------------------------------------------------------------------------------------------------------------------------------[%s]\n",
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
   E_Devicemgr_Input_Device *dev;
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
_directory_make(char *type, char *path)
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
     snprintf(fullpath, PATH_MAX, "/%s-%s", type, stamp);
   else
     snprintf(fullpath, PATH_MAX, "%s/%s-%s", dir, type, stamp);

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
_e_info_client_cb_dump_wins(const Eldbus_Message *msg)
{
   const char *log = NULL;
   Eina_Bool res;

   res = eldbus_message_arguments_get(msg, "s", &log);
   if (!res)
     {
        printf("Failed to get log of dump\n");
        return;
     }

   if (log)
     printf("%s\n", log);
}

static void
_e_info_client_proc_wins_shot(int argc, char **argv)
{
   char *directory = NULL;
   char *type = NULL;

   if (eina_streq(argv[2], "topvwins") || eina_streq(argv[2], "ns") || eina_streq(argv[2], "hwc_wins"))
     {
        if (argc == 3)
          directory = _directory_make(argv[2], NULL);
        else if (argc == 4)
          directory = _directory_make(argv[2], argv[3]);
        else
          goto arg_err;
     }

   if (!directory) goto arg_err;

   type = argv[2];
   if (!_e_info_client_eldbus_message_with_args("dump_wins", _e_info_client_cb_dump_wins, SIGNATURE_DUMP_WINS, type, directory))
     {
        free(directory);
        return;
     }

   free(directory);

   return;
arg_err:
   printf("Usage: enlightenment_info -dump %s\n", USAGE_DUMPIMAGE);
}

static void
_e_info_client_cb_force_visible(const Eldbus_Message *msg)
{
   const char *name = NULL, *text = NULL;
   Eina_Bool res;
   const char *result = NULL;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_GOTO(res, finish);

   res = eldbus_message_arguments_get(msg,
                                      SIGNATURE_FORCE_VISIBLE_SERVER,
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
_e_info_client_proc_force_visible(int argc, char **argv)
{
   unsigned int obj, visible;
   Eina_Bool res;

   if (argc != 4)
      goto arg_err;

   res = _util_string_to_uint(argv[2], &obj, 16);
   if (!res)
      goto arg_err;

   res = _util_string_to_uint(argv[3], &visible, 10);
   if (!res)
      goto arg_err;

   if (!_e_info_client_eldbus_message_with_args("set_force_visible", _e_info_client_cb_force_visible,
                                                SIGNATURE_FORCE_VISIBLE_CLIENT,
                                                obj, (visible) ? EINA_TRUE : EINA_FALSE))
      return;

   return;
arg_err:
   printf("Usage: enlightenment_info -set_force_visible [obj_pointer_address] [0 or 1]\n");
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

        printf("%3d 0x%08zx ", count, win);
        temp[0] = '\0';
        if (parent > 0) snprintf(temp, sizeof(temp), "0x%08zx", parent);
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
        if (bgrect > 0) snprintf(temp, sizeof(temp), "0x%08zx", bgrect);
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

#define PROP_USAGE \
   "0x<win_id> | -id win_id | -pid pid | -name \"win_name\" [property_name [property_value]]\n" \
   "Example:\n" \
   "\tenlightenment_info -prop                        : Get all properties for a window specified via a touch\n" \
   "\tenlightenment_info -prop Hidden                 : Get the \"Hidden\" property for a window specified via a touch\n" \
   "\tenlightenment_info -prop 0xb88ffaa0 Layer       : Get the \"Layer\" property for specified window\n" \
   "\tenlightenment_info -prop 0xb88ffaa0 Hidden TRUE : Set the \"Hidden\" property for specified window\n" \
   "\tenlightenment_info -prop -pid 2502 Hidden FALSE : Set the \"Hidden\" property for all windows belonged to a process\n" \
   "\tenlightenment_info -prop -name err              : Get all properties for windows whose names contain an \"err\" substring\n" \
   "\tenlightenment_info -prop -name \"\"               : Get all properties for all windows\n" \
   "\tenlightenment_info -prop -name \"\" Hidden TRUE   : Set the \"Hidden\" property for all windows\n"

/* property value can consist of several lines separated by '\n', which we got to print nicely */
static void
_parse_property(const char *prop_name, const char *prop_value)
{
   char *begin, *end;	/* current line */

   /* process a single line property value */
   if (!strchr(prop_value, '\n'))
     {
        printf("%27s : %s\n", prop_name, prop_value);
        return;
     }

   char *const tmp = strdup(prop_value);
   if (!tmp)
     return;

   begin = tmp;

   while (*begin != '\0')
     {
       end = strchr(begin, '\n');
       if (end)
         *end = '\0';

       printf("%27s : %s\n", begin == tmp ? prop_name : "", begin);

       /* it's the last line */
       if (!end)
         break;

       begin = end + 1;
     }

   free(tmp);
}

static void
_cb_window_prop_get(const Eldbus_Message *msg)
{
   const char *name = NULL, *text = NULL;
   Eldbus_Message_Iter *array, *ec;
   Eina_Bool res;
   int first_delimiter = 1;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_GOTO(res, finish);

   res = eldbus_message_arguments_get(msg, "a(ss)", &array);
   EINA_SAFETY_ON_FALSE_GOTO(res, finish);

   while (eldbus_message_iter_get_and_next(array, 'r', &ec))
     {
        const char *title = NULL;
        const char *value = NULL;

        res = eldbus_message_iter_arguments_get(ec,
                                                "ss",
                                                &title,
                                                &value);
        if (!res)
          {
             printf("Failed to get win prop info\n");
             continue;
          }

        if (title && !strncmp(title, "delimiter", sizeof("delimiter")))
          {
             if (first_delimiter)
               first_delimiter = 0;
             else
               printf("---------------------------------------------------------------------------------------------------------\n");
          }
        else
          _parse_property(title, value);
     }

   return;

finish:
   printf("error:\n");

   if ((name) || (text))
     {
        printf(" %s :: (%s)\n", name, text);
     }
}

static void
_e_info_client_prop_prop_info(int argc, char **argv)
{
   const static int WINDOW_ID_MODE = 0;
   const static int WINDOW_PID_MODE = 1;
   const static int WINDOW_NAME_MODE = 2;
   const char *value;
   const char *property_name = "", *property_value = "";
   uint32_t mode = 0;
   int simple_mode = 1;

   Ecore_Window win;
   char win_id[64] = {0, };

   /* for a window specified via a touch */
   /* TODO: what's about a property with "0x" as a substring? (e.g. kyky0xkyky) */
   if (argc < 3 || (argv[2][0] != '-' && !strstr(argv[2], "0x")))
     {
        printf("Select the window whose property(ies) you wish to get/set\n");
        if (_e_get_window_under_touch(&win))
          {
             printf("Error: cannot get window under touch\n");
             return;
          }

        snprintf(win_id, sizeof(win_id), "%lu", (unsigned long int)win);

        mode = WINDOW_ID_MODE;
        value = win_id;

        if (argc > 2) property_name  = argv[2];
        if (argc > 3) property_value = argv[3];
     }
   else
     {
        if (argv[2][0] == '-' && argc < 4) goto error;

        if (argv[2][0] == '-')
          {
             if (!strcmp(argv[2], "-id")) mode = WINDOW_ID_MODE;
             else if (!strcmp(argv[2], "-pid")) mode = WINDOW_PID_MODE;
             else if (!strcmp(argv[2], "-name")) mode = WINDOW_NAME_MODE;
             else goto error;

             value = argv[3];

             simple_mode = 0;
          }
        else
          {
             mode = WINDOW_ID_MODE;
             value = argv[2];
          }

        if (simple_mode)
          {
             if (argc > 3) property_name  = argv[3];
             if (argc > 4) property_value = argv[4];
          }
        else
          {
             if (argc > 4) property_name  = argv[4];
             if (argc > 5) property_value = argv[5];
          }
     }

   /* all checks about win_id/pid/win_name, property_name, property_value sanity are performed on server side,
    * in case of an error an error message contained error description will be returned */
   if (!_e_info_client_eldbus_message_with_args("get_window_prop", _cb_window_prop_get, "usss",
           mode, value, property_name, property_value))
     printf("_e_info_client_eldbus_message_with_args error");

   return;

error:
   printf("Error Check Args: enlightenment_info -prop [property_name [property_value]]\n"
          "                  enlightenment_info -prop 0x<win_id> [property_name [property_value]]\n"
          "                  enlightenment_info -prop -id win_id [property_name [property_value]]\n"
          "                  enlightenment_info -prop -pid pid [property_name [property_value]]\n"
          "                  enlightenment_info -prop -name win_name [property_name [property_value]]\n");
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

static Eina_Bool
_opt_parse(char *opt, char *delims, int *vals, int n_vals)
{
   Eina_Bool res;
   int n, i;

   EINA_SAFETY_ON_FALSE_RETURN_VAL(n_vals > 0, EINA_FALSE);

   for (i = 0; i < n_vals; i++)
     {
        res = _util_string_to_int_token(opt, &opt, &n, 10);
        EINA_SAFETY_ON_FALSE_RETURN_VAL(res, EINA_FALSE);

        vals[i] = n;

        if ((strlen(opt) == 0) || (i == (n_vals - 1)))
          return EINA_TRUE;

        EINA_SAFETY_ON_TRUE_RETURN_VAL((*opt != delims[i]), EINA_FALSE);

        opt = opt + 1;
     }

   return EINA_TRUE;
}

static void
_e_info_client_proc_bgcolor_set(int argc, char **argv)
{
   int r = 0, g = 0, b = 0, a = 0;
   char delims_col[] = { ',', ',', ',', '\0' };
   int vals_col[] = { 0, 0, 0, 0};
   Eina_Bool res;

   if (argc < 2)
     goto error_msg;

   res = _opt_parse(argv[2], delims_col, vals_col, (sizeof(vals_col) / sizeof(int)));

   if (!res)
     goto error_msg;

   a = vals_col[0];
   r = vals_col[1];
   g = vals_col[2];
   b = vals_col[3];

   printf("(A, R, G, B) : %d, %d, %d, %d\n", a, r, g, b);

   _e_info_client_eldbus_message_with_args("bgcolor_set", NULL, "iiii", a, r, g, b);
   return;

error_msg:
   printf("Wrong argument(s)! (<a>,<r>,<g>,<b>)\n");
   return;
}

static void
_e_info_client_proc_punch(int argc, char **argv)
{
   int onoff = 0, x = 0, y = 0, w = 0, h = 0;
   int a = 0, r = 0, g = 0, b = 0;
   char delims_geom[] = { 'x', '+', '+', '\0' };
   int vals_geom[] = { 0, 0, 0, 0 };
   char delims_col[] = { ',', ',', ',', '\0' };
   int vals_col[] = { 0, 0, 0, 0 };
   Eina_Bool res = EINA_FALSE;

   EINA_SAFETY_ON_FALSE_GOTO(argc >= 3, wrong_args);
   EINA_SAFETY_ON_NULL_GOTO(argv[2], wrong_args);

   if (!strncmp(argv[2], "on", 2)) onoff = 1;

   if (argc >= 4 && argv[3])
     {
        res = _opt_parse(argv[3], delims_geom, vals_geom, (sizeof(vals_geom) / sizeof(int)));
        EINA_SAFETY_ON_FALSE_GOTO(res, wrong_args);

        w = vals_geom[0]; h = vals_geom[1]; x = vals_geom[2]; y = vals_geom[3];
     }

   if (argc >= 5 && argv[4])
     {
        res = _opt_parse(argv[4], delims_col, vals_col, (sizeof(vals_col) / sizeof(int)));
        EINA_SAFETY_ON_FALSE_GOTO(res, wrong_args);

        a = vals_col[0]; r = vals_col[1]; g = vals_col[2]; b = vals_col[3];
     }

   res = _e_info_client_eldbus_message_with_args("punch", NULL, "iiiiiiiii", onoff, x, y, w, h, a, r, g, b);
   if (!res) printf("Error occured while send send message\n\n");

   return;

wrong_args:
   printf("wrong geometry arguments(<w>x<h>+<x>+<y>\n");
   printf("wrong color arguments(<a>,<r>,<g>,<b>)\n");
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
  "  enlightenment_info -dump_buffers 1 -m             : start dump buffer with marking of various color\n" \
  "  enlightenment_info -dump_buffers 1 -c 50          : start dump buffer with 50 buffers\n" \
  "  enlightenment_info -dump_buffers 1 -p /tmp/test   : start dump buffer - the dump path is '/tmp/test/dump_xxxx'\n" \
  "  enlightenment_info -dump_buffers 1 -c 60 -p /test : start dump buffer with 60 buffers to '/test/dump_xxxx' folder\n" \
  "  enlightenment_info -dump_buffers 0                : stop dump buffer (store dump files to dump path)\n" \
  "  enlightenment_info -dump_buffers 1 -s 0.5         : start dump buffer with 0.5 scale factor\n" \
  "  enlightenment_info -dump_selected_buffers Win_ID(from enlightenment_info -topvwins)   : dump Win_ID(store dump files to dump path)\n" \

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
   Eina_Bool res = EINA_FALSE;

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
        if (!strncmp(argv[2], "create", strlen("create"))) mode = E_INFO_CMD_MESSAGE_CREATE;
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
        uint32_t value_number;
        if (strlen(value) >= 2 && value[0] == '0' && value[1] == 'x')
          res = _util_string_to_uint(value, &value_number, 16);
        else
          res = _util_string_to_uint(value, &value_number, 10);

        EINA_SAFETY_ON_FALSE_RETURN(res);

        param[1] = (int32_t)value_number;
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
_cb_buffer_shot(const Eldbus_Message *msg)
{
   const char *name = NULL, *text = NULL;
   char *fullpath = NULL;
   int error = 0;
   Eina_Bool res;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_GOTO(res, finish);

   res = eldbus_message_arguments_get(msg, "is", &error, &fullpath);
   EINA_SAFETY_ON_FALSE_GOTO(res, finish);

   if (strcmp(fullpath, "nopath") && !e_info_client.dump_fullpath)
     e_info_client.dump_fullpath = eina_stringshare_add(fullpath);
   e_info_client.dump_success = error;

finish:
   if ((name) || (text))
     {
        printf("errname:%s errmsg:%s\n", name, text);
     }
}

static void
_e_info_client_proc_buffer_shot(int argc, char **argv)
{
   int dumprun = 0;
   int count = 100;
   int i;
   char path[PATH_MAX];
   double scale = 0.0;
   int mark = 0;

   strncpy(path, "/tmp", PATH_MAX);

   EINA_SAFETY_ON_TRUE_GOTO(argc < 3, err);

   dumprun = atoi(argv[2]);

   EINA_SAFETY_ON_TRUE_GOTO(dumprun < 0 || dumprun > 1, err);

   for (i = 3; i < argc; i++)
     {
        if (eina_streq(argv[i], "-c"))
          {
             if (++i >= argc || (argv[i][0] < '0' || argv[i][0] > '9'))
               {
                  printf("Error: -c requires argument\n");
                  goto err;
               }
             count = atoi(argv[i]);
             EINA_SAFETY_ON_TRUE_GOTO(count < 1, err);
             continue;
          }

          if (eina_streq(argv[i], "-p"))
            {
               int str_len;
               char *tmp_path;

               if (++i >= argc)
                 {
                    printf("Error: -p requires argument\n");
                    goto err;
                 }
               tmp_path = _buffer_shot_directory_check(argv[i]);
               if (tmp_path == NULL)
                 {
                    printf("cannot find directory: %s\n", argv[i]);
                    goto err;
                 }

               str_len = strlen(tmp_path);
               if (str_len >= PATH_MAX) str_len = PATH_MAX - 1;

               strncpy(path, tmp_path, str_len);
               path[str_len] = 0;

               free(tmp_path);
               continue;
            }
          if (eina_streq(argv[i], "-s"))
            {
               if (++i >= argc || (argv[i][0] < '0' || argv[i][0] > '9'))
                 {
                    printf("Error: -s requires argument\n");
                    goto err;
                 }
               scale = atof(argv[i]);
               EINA_SAFETY_ON_TRUE_GOTO(scale <= 0.0, err);
               continue;
            }
          if (eina_streq(argv[i], "-m"))
            {
               ++i;
               mark = 1;
               continue;
            }

          goto err;
     }

   if (!_e_info_client_eldbus_message_with_args("dump_buffers", _cb_buffer_shot, "iisdi",
                                                dumprun, count, path, scale, mark))
     {
        printf("dump_buffers fail (%d)\n", dumprun);
        return;
     }

   printf("dump_buffers %s %s.\n",
          (dumprun == 1 ? "start" : "stop"), (e_info_client.dump_success == 0 ? "success" : "fail"));

   if (e_info_client.dump_fullpath)
     {
        if (dumprun == 0 && e_info_client.dump_success == 0 && e_info_client.dump_fullpath)
          printf("saved : %s\n", e_info_client.dump_fullpath);

        eina_stringshare_del(e_info_client.dump_fullpath);
        e_info_client.dump_fullpath = NULL;
     }

   return;

err:
   printf("Error Check Args\n%s\n", DUMP_BUFFERS_USAGE);
   return;
}

static void
_e_info_client_cb_selected_buffer(const Eldbus_Message *msg)
{
   const char *log = NULL;
   Eina_Bool res;

   res = eldbus_message_arguments_get(msg, "s", &log);
   if (!res)
     {
        printf("Failed to get log of dump\n");
        return;
     }

   if (log)
     printf("%s\n", log);
}

static void
_e_info_client_proc_selected_buffer_shot(int argc, char **argv)
{
   const char *win_id=NULL;
   char path[PATH_MAX];

   strncpy(path, "/tmp", PATH_MAX);
   if (argc == 3)
     {
        win_id = argv[2];

        if (!_e_info_client_eldbus_message_with_args("dump_selected_buffers", _e_info_client_cb_selected_buffer, "ss", win_id, path))
          {
             printf("_e_info_client_proc_selected_buffer_shot fail (%s)\n", win_id);
             return;
          }
     }
   else
     goto err;

   return;

err:
   printf("Error Check Args\n%s\n", DUMP_BUFFERS_USAGE);
return;
}

static Eina_Bool
_e_info_client_proc_screen_shot_name_check(const char *name, int length)
{
   if (length < 5)
     return EINA_FALSE;

   if (name[length - 1] != 'g' || name[length - 2] != 'n' ||
       name[length - 3] != 'p' || name[length - 4] != '.')
     return EINA_FALSE;

   return EINA_TRUE;
}

static void
_e_info_client_proc_screen_shot(int argc, char **argv)
{
   int i;
   char *path = NULL;
   char *name = NULL;
   char *fname = NULL;
   int path_len;
   int name_len;
   Eina_Bool p = EINA_FALSE;
   Eina_Bool n = EINA_FALSE;

   for (i = 2; i < argc; i++)
     {
        if (eina_streq(argv[i], "-p"))
          {
             char *tmp_path;

             if (++i >= argc)
               {
                  printf("Error: -p requires argument\n");
                  goto err;
               }

             tmp_path = _buffer_shot_directory_check(argv[i]);
             if (tmp_path == NULL)
               {
                  printf("cannot find directory: %s, make directory before dump\n", argv[i]);
                  goto err;
               }
             free(tmp_path);

             path = argv[i];
             p = EINA_TRUE;

             continue;
          }
        if (eina_streq(argv[i], "-n"))
          {
             if (++i >= argc)
               {
                  printf("Error: -n requires argument\n");
                  goto err;
               }

             name = argv[i];
             n = EINA_TRUE;

             continue;
          }
     }


   if (!p)
     {
        path = (char *)calloc(1, PATH_MAX * sizeof(char));
        EINA_SAFETY_ON_NULL_RETURN(path);
        strncpy(path, "/tmp/", PATH_MAX);
     }
   if (!n)
     {
        name = (char *)calloc(1, PATH_MAX * sizeof(char));
        EINA_SAFETY_ON_NULL_GOTO(name, err);
        strncpy(name, "dump_screen.png", PATH_MAX);
     }
   path_len = strlen(path);
   name_len = strlen(name);

   if (n)
     {
        if (!_e_info_client_proc_screen_shot_name_check(name, name_len))
          {
             printf("Error: support only 'png' file\n       write like -n xxx.png\n");
             goto err;
          }
     }

   if (path_len + name_len >= PATH_MAX)
     {
        printf("_e_info_client_proc_screen_shot fail. long name\n");
        goto err;
     }

   fname = (char *)calloc(1, PATH_MAX * sizeof(char));
   EINA_SAFETY_ON_NULL_GOTO(fname, err);
   if (path[path_len - 1] == '/')
     snprintf(fname, PATH_MAX, "%s%s", path, name);
   else
     snprintf(fname, PATH_MAX, "%s/%s", path, name);

   printf("make dump: %s\n", fname);

   if (!_e_info_client_eldbus_message_with_args("dump_screen", NULL, "s", fname))
     printf("_e_info_client_proc_screen_shot fail\n");

err:
   if (!p) free(path);
   if (!n) free(name);
   if (fname) free(fname);

   return;
}


static E_Info_Output_Mode *
_e_output_mode_info_new(uint32_t h, uint32_t hsync_start, uint32_t hsync_end, uint32_t htotal,
                        uint32_t v, uint32_t vsync_start, uint32_t vsync_end, uint32_t vtotal,
                        uint32_t refresh, uint32_t vscan, uint32_t clock, uint32_t flags,
                        int current, int output, int connect, const char *name, int dpms)
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
   mode->dpms = dpms;

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
        int current, output, connect, dpms;
        const char *name;
        E_Info_Output_Mode *mode = NULL;
        res = eldbus_message_iter_arguments_get(ec,
                                                SIGNATURE_OUTPUT_MODE_SERVER,
                                                &h, &hsync_start, &hsync_end, &htotal,
                                                &v, &vsync_start, &vsync_end, &vtotal,
                                                &refresh, &vscan, &clock, &flag, &name,
                                                &current, &output, &connect, &gl, &dpms);
        if (!res)
          {
             printf("Failed to get output mode info\n");
             continue;
          }

        mode = _e_output_mode_info_new(h, hsync_start, hsync_end, htotal,
                                       v, vsync_start, vsync_end, vtotal,
                                       refresh, vscan, clock, flag,
                                       current, output, connect, name, dpms);
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
   const char *str_dpms[] = {
      "on",
      "standby",
      "suspend",
      "off"
   };
   int count = 0;

   if (argc == 2)
     {
        if (!_e_info_client_eldbus_message_with_args("output_mode", _cb_output_mode_info,
                                                     SIGNATURE_OUTPUT_MODE_CLIENT, E_INFO_CMD_OUTPUT_MODE_GET, 0))
          {
             printf("_e_info_client_proc_output_mode fail (%d)\n", E_INFO_CMD_OUTPUT_MODE_GET);
             return;
          }
     }
   else if (argc == 3)
     {
        if ((argv[2][0] < '0' || argv[2][0] > '9'))
          {
             printf("Error: invalid argument\n");
             return;
          }

        count = atoi(argv[2]);
        if (!_e_info_client_eldbus_message_with_args("output_mode", _cb_output_mode_info,
                                                     SIGNATURE_OUTPUT_MODE_CLIENT, E_INFO_CMD_OUTPUT_MODE_SET, count))
          {
             printf("_e_info_client_proc_output_mode fail (%d)\n", E_INFO_CMD_OUTPUT_MODE_SET);
             return;
          }
     }
   else
     {
        printf("Error: invalid argument\n");
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
               printf("%s, %s\n", "connected", str_dpms[mode->dpms]);
             else
               {
                  printf("%s, %s\n", "disconnected", str_dpms[mode->dpms]);
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

static void
_e_info_client_proc_trace(int argc, char **argv)
{
   uint32_t onoff;

   if (argc < 4)
     goto arg_err;

   onoff = atoi(argv[3]);

   if (onoff == 1 || onoff == 0)
     {
        if (eina_streq(argv[2], "hwc"))
          {
             if (!_e_info_client_eldbus_message_with_args("trace_message_hwc", NULL, "i", onoff))
               {
                  printf("_e_info_client_eldbus_message_with_args error");
               }
             return;
          }
        else if (eina_streq(argv[2], "serial"))
          {
             if (!_e_info_client_eldbus_message_with_args("trace_message_serial", NULL, "i", onoff))
               {
                  printf("_e_info_client_eldbus_message_with_args error");
               }
             return;
          }
     }

arg_err:
   printf("Error Check Args: enlightenment_info -trace %s\n", USAGE_TRACE);
}

static void
_e_info_client_proc_hwc(int argc, char **argv)
{
   uint32_t param;

   if (argc < 3)
     goto arg_err;

   param = atoi(argv[2]);

   if (param == 1 || param == 0 || param == 2)
     {
        if (!_e_info_client_eldbus_message_with_args("hwc", NULL, "i", param))
          {
             printf("_e_info_client_eldbus_message_with_args error");
          }
        return;
     }

arg_err:
     printf("Error Check Args: enlightenment_info -hwc [0: off, 1: on, 2: info]\n");

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
        printf("%3d        %12zx   %5d         %12zx  %12zx\n",
               i,
               (uintptr_t)pending_commit->plane,
               pending_commit->zpos,
               (uintptr_t)pending_commit->data,
               (uintptr_t)pending_commit->tsurface);
     }

   E_FREE_LIST(e_info_client.pending_commit_list, _e_pending_commit_info_free);
}

static E_Fps_Info *
_e_fps_info_new(E_Info_Fps_Type type, const char *output, int zpos, unsigned int window, double fps)
{
   E_Fps_Info *fps_info = NULL;

   fps_info = E_NEW(E_Fps_Info, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(fps_info, NULL);

   fps_info->type = type;
   fps_info->output = output;
   fps_info->zpos = zpos;
   fps_info->window = window;
   fps_info->fps = fps;

   return fps_info;
}

static void
_e_fps_info_free(E_Fps_Info *fps)
{
   E_FREE(fps);
}

static void
_cb_fps_info_get(const Eldbus_Message *msg)
{
   const char *name = NULL, *text = NULL;
   Eldbus_Message_Iter *array, *eldbus_msg;
   Eina_Bool res;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_GOTO(res, finish);

   res = eldbus_message_arguments_get(msg, "a("VALUE_TYPE_FOR_FPS")", &array);
   EINA_SAFETY_ON_FALSE_GOTO(res, finish);

   while (eldbus_message_iter_get_and_next(array, 'r', &eldbus_msg))
     {
        E_Fps_Info *fps_info = NULL;
        const char *output;
        int zpos, type;
        double fps;
        unsigned int window;
        res = eldbus_message_iter_arguments_get(eldbus_msg,
                                                VALUE_TYPE_FOR_FPS,
                                                &type,
                                                &output,
                                                &zpos,
                                                &window,
                                                &fps);
        if (!res)
          {
             printf("Failed to get fps info\n");
             continue;
          }

        fps_info = _e_fps_info_new(type, output, zpos, window, fps);
        if (!fps_info) continue;

        e_info_client.fps_list = eina_list_append(e_info_client.fps_list, fps_info);
     }

finish:
   if ((name) || (text))
     {
        printf("errname:%s errmsg:%s\n", name, text);
     }
}

static void
_e_info_client_proc_fps_info(int argc, char **argv)
{
   do
     {
        Eina_List *l;
        E_Fps_Info *fps;

        if (!_e_info_client_eldbus_message("get_fps_info", _cb_fps_info_get))
          return;

        if (!e_info_client.fps_list)
          goto fps_done;

        EINA_LIST_FOREACH(e_info_client.fps_list, l, fps)
          {
             if (fps->type == E_INFO_FPS_TYPE_OUTPUT)
               {
                  printf("%3s-OUTPUT...%3.1f\n",
                         fps->output,
                         fps->fps);
               }
             else if (fps->type == E_INFO_FPS_TYPE_LAYER)
               {
                  printf("%3s-Layer-ZPos@%d...%3.1f\n",
                         fps->output,
                         fps->zpos,
                         fps->fps);
               }
             else if (fps->type == E_INFO_FPS_TYPE_HWC_WIN)
               {
                  printf("%3s-HWC-Win_ID(0x%x)-ZPos@%d...%3.1f\n",
                         fps->output,
                         fps->window,
                         fps->zpos,
                         fps->fps);
               }
             else if (fps->type == E_INFO_FPS_TYPE_HWC_COMP)
               {
                  printf("%3s-HWC-COMP...%3.1f\n",
                         fps->output,
                         fps->fps);
               }
          }

        E_FREE_LIST(e_info_client.fps_list, _e_fps_info_free);
fps_done:
        usleep(500000);
     }
   while (1);
}

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

        res = _util_string_to_double(argv[3], &sec);
        EINA_SAFETY_ON_FALSE_GOTO(res, arg_err);

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

#define KILL_USAGE \
  "[COMMAND] [ARG]...\n" \
  "\t-id     : the identifier for the resource whose creator is to be killed.\n" \
  "\t-name   : the name for the resource whose creator is to be killed.\n" \
  "\t-pid    : the pid for the resource whose creator is to be killed.\n" \
  "\t-pid -f : the pid of the client is going to be killed immediately.\n" \
  "\t-all    : kill all clients with top level windows\n" \
  "\t-help\n" \
  "Example:\n" \
  "\tenlightenment_info -kill\n" \
  "\tenlightenment_info -kill [win_id]\n" \
  "\tenlightenment_info -kill -id [win_id]\n" \
  "\tenlightenment_info -kill -name [win_name]\n" \
  "\tenlightenment_info -kill -pid [pid]\n" \
  "\tenlightenment_info -kill -pid [pid] -f\n" \
  "\tenlightenment_info -kill -all\n" \
  "\tenlightenment_info -kill -help\n" \

static void
_e_info_client_proc_screen_rotation_pre(int argc, char **argv)
{
   int rotation_pre;

   if (argc < 3)
     {
        printf("Error Check Args: enlightenment_info -screen_rotation [0|90|180|270]\n");
        return;
     }

   rotation_pre = atoi(argv[2]);
   if (rotation_pre < 0 || rotation_pre > 360 || rotation_pre % 90)
     {
        printf("Error Check Args: enlightenment_info -screen_rotation_pre [0|90|180|270]\n");
        return;
     }

   if (!_e_info_client_eldbus_message_with_args("screen_rotation_pre", NULL, "i", rotation_pre))
     printf("_e_info_client_eldbus_message_with_args error");
}

static void
_e_info_client_proc_screen_rotation(int argc, char **argv)
{
   int rotation;

   if (argc < 3)
     {
        printf("Error Check Args: enlightenment_info -screen_rotation [0|90|180|270]\n");
        return;
     }

   rotation = atoi(argv[2]);
   if (rotation < 0 || rotation > 360 || rotation % 90)
     {
        printf("Error Check Args: enlightenment_info -screen_rotation [0|90|180|270]\n");
        return;
     }

   if (!_e_info_client_eldbus_message_with_args("screen_rotation", NULL, "i", rotation))
     printf("_e_info_client_eldbus_message_with_args error");
}

static void
_e_info_client_cb_remote_surface(const Eldbus_Message *msg)
{
   const char *name = NULL, *text = NULL;
   Eina_Bool res;
   Eldbus_Message_Iter *lines;
   char *result = NULL;

   res = eldbus_message_error_get(msg, &name, &text);
   if (res) goto finish;

   res = eldbus_message_arguments_get(msg, "as", &lines);
   if (!res) goto finish;

   while (eldbus_message_iter_get_and_next(lines, 's', &result))
     printf("%s\n", result);

   return;

finish:
   if ((name) || (text))
     {
        printf("errname:%s errmsg:%s\n", name, text);
     }
}

static void
_e_info_client_cb_kill_client(const Eldbus_Message *msg)
{
   const char *name = NULL, *text = NULL;
   Eina_Bool res;
   const char *result = NULL;
   Eldbus_Message_Iter *array_of_string;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_GOTO(res, finish);

   res = eldbus_message_arguments_get(msg, "a"VALUE_TYPE_REPLY_KILL, &array_of_string);
   EINA_SAFETY_ON_FALSE_GOTO(res, finish);

   while (eldbus_message_iter_get_and_next(array_of_string, 's', &result))
     {
        printf("%s\n", result);
     }

   return;

finish:
   if ((name) || (text))
     {
        printf("errname:%s errmsg:%s\n", name, text);
     }
}

static void
_e_info_client_proc_remote_surface(int argc, char **argv)
{
   Eina_Bool res;
   int i;
   int dump = -1, query = 0;

   if (argc < 3) goto arg_err;
   for (i = 2; i < argc; i++)
     {
        if (eina_streq(argv[i], "dump"))
          {
             if (argc == i + 1)
               goto arg_err;

             dump = atoi(argv[i+1]);
             i = i + 1;
          }

        if (eina_streq(argv[i], "info"))
          {
             query = 1;
          }
     }

   if (dump == -1 && query == 0)
     goto arg_err;

   res = _e_info_client_eldbus_message_with_args("remote_surface",
                                                 _e_info_client_cb_remote_surface,
                                                 "ii",
                                                 dump, query);
   EINA_SAFETY_ON_FALSE_RETURN(res);
   return;
arg_err:
   printf("%s\n", USAGE_REMOTE_SURFACE);
}

static void
_e_info_client_proc_kill_client(int argc, char **argv)
{
   const static int KILL_ID_MODE = 1;
   const static int KILL_NAME_MODE = 2;
   const static int KILL_PID_MODE = 3;
   const static int KILL_ALL_MODE = 4;
   const static int KILL_PID_FORCE_MODE = 5;

   Eina_Bool res;
   unsigned long tmp = 0;
   uintptr_t ecore_win = 0;
   uint64_t uint64_value = 0;
   const char *str_value = "";
   uint32_t mode = 0;

   if (argc == 2)
     {
        mode = KILL_ID_MODE;
        printf("Select the window whose client you wish to kill\n");
        if (_e_get_window_under_touch(&ecore_win))
          {
             printf("Error: cannot get window under touch\n");
             return;
          }
        uint64_value = (uint64_t)ecore_win;
     }
   else if (argc == 3)
     {
        if (eina_streq(argv[2], "-all"))
          mode = KILL_ALL_MODE;
        else if (eina_streq(argv[2], "-help"))
          goto usage;
        else
          {
             mode = KILL_ID_MODE;
             if (strlen(argv[2]) >= 2 && argv[2][0] == '0' && argv[2][1] == 'x')
               res = _util_string_to_ulong(argv[2], &tmp, 16);
             else
               res = _util_string_to_ulong(argv[2], &tmp, 10);

             uint64_value = (uint64_t)tmp;

             EINA_SAFETY_ON_FALSE_GOTO(res, usage);
          }
     }
   else if (argc == 4)
     {
        if (eina_streq(argv[2], "-id"))
          {
             mode = KILL_ID_MODE;
             if (strlen(argv[3]) >= 2 && argv[3][0] == '0' && argv[3][1] == 'x')
               res = _util_string_to_ulong(argv[3], &tmp, 16);
             else
               res = _util_string_to_ulong(argv[3], &tmp, 10);

             uint64_value = (uint64_t)tmp;

             EINA_SAFETY_ON_FALSE_GOTO(res, usage);
          }
        else if (eina_streq(argv[2], "-name"))
          {
             mode = KILL_NAME_MODE;
             str_value = argv[3];
          }
        else if (eina_streq(argv[2], "-pid"))
          {
             mode = KILL_PID_MODE;
             if (strlen(argv[3]) >= 2 && argv[3][0] == '0' && argv[3][1] == 'x')
               res = _util_string_to_ulong(argv[3], &tmp, 16);
             else
               res = _util_string_to_ulong(argv[3], &tmp, 10);

             uint64_value = (uint64_t)tmp;

             EINA_SAFETY_ON_FALSE_GOTO(res, usage);
          }
        else
          goto usage;
     }
   else if (argc == 5)
     {
        if (eina_streq(argv[2], "-pid") && eina_streq(argv[4], "-f"))
          {
             mode = KILL_PID_FORCE_MODE;

             if (strlen(argv[3]) >= 2 && argv[3][0] == '0' && argv[3][1] == 'x')
               res = _util_string_to_ulong(argv[3], &tmp, 16);
             else
               res = _util_string_to_ulong(argv[3], &tmp, 10);

             uint64_value = (uint64_t)tmp;
             str_value = argv[4];

             EINA_SAFETY_ON_FALSE_GOTO(res, usage);
          }
     }
   else
     goto usage;

   res = _e_info_client_eldbus_message_with_args("kill_client",
                                                 _e_info_client_cb_kill_client,
                                                 VALUE_TYPE_REQUEST_FOR_KILL,
                                                 mode, uint64_value, str_value);
   EINA_SAFETY_ON_FALSE_RETURN(res);

   return;
usage:
   printf("Usage: enlightenment_info %s", KILL_USAGE);
}

static int window_id_format_dec;

static void
_e_info_client_cb_wininfo(const Eldbus_Message *msg)
{
   const char *name = NULL, *text = NULL;
   Eina_Bool res;
   const char *layer_name;
   int x, y, w, h, layer, obscured, opaque, hwc, pl_zpos;
   Eina_Bool visible, alpha, iconic, focused, frame_visible, redirected;
   uint64_t parent_id;
   uint32_t res_id;
   int pid, xright, ybelow, border_size;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_GOTO(res, finish);

   res = eldbus_message_arguments_get(msg,
                                      VALUE_TYPE_REPLY_WININFO,
                                      &res_id,
                                      &pid,
                                      &x,
                                      &y,
                                      &w,
                                      &h,
                                      &layer,
                                      &visible,
                                      &alpha,
                                      &opaque,
                                      &obscured,
                                      &iconic,
                                      &frame_visible,
                                      &focused,
                                      &hwc,
                                      &pl_zpos,
                                      &parent_id,
                                      &layer_name,
                                      &xright,
                                      &ybelow,
                                      &border_size,
                                      &redirected);
   EINA_SAFETY_ON_FALSE_GOTO(res, finish);

   if (window_id_format_dec)
     printf("\n   Parent id: %lu\n", (unsigned long)parent_id);
   else
     printf("\n   Parent id: 0x%lx\n", (unsigned long)parent_id);

   printf("   Resource id: %u\n"
          "   PID: %d\n"
          "   X: %d\n"
          "   Y: %d\n"
          "   Width: %d\n"
          "   Height: %d\n"
          "   Border size: %d\n"
          "   Depth: %d\n"
          "   Focused: %d\n"
          "   Opaque: %d\n"
          "   Obscured: %d\n"
          "   Iconic: %d\n"
          "   Map State: %s\n"
          "   Frame visible: %d\n"
          "   Redirect State: %s\n"
          "   Layer name: %s\n",
          res_id, pid, x, y, w, h, border_size, alpha ? 32 : 24,
          focused, opaque, obscured, iconic, visible ? "Visible" : "Not visible",
          frame_visible, redirected ? "yes" : "no", layer_name);
   printf("   PL@ZPos:");
   if (hwc >= 0)
     {
        if ((!iconic) && (frame_visible))
          {
             if (pl_zpos == -999)
               printf(" - ");
             else
               {
                  if (hwc) printf(" hwc@%i\n", pl_zpos);
                  else printf(" comp@%i\n", pl_zpos);
               }
          }
        else
          printf(" - \n");
     }
   else
     {
        printf(" - \n");
     }
   printf ("   Corners:  +%d+%d  -%d+%d  -%d-%d  +%d-%d\n",
           x, y, xright, y, xright, ybelow, x, ybelow);

   return;

finish:
   if ((name) || (text))
     {
        printf("errname:%s errmsg:%s\n", name, text);
     }
}

static void
_e_info_client_cb_wininfo_tree(const Eldbus_Message *msg)
{
   const char *error_name = NULL, *error_text = NULL;
   Eina_Bool res;
   const char *pname;
   uint64_t pwin;
   Eldbus_Message_Iter *array_of_children, *child;
   int num_children;

   res = eldbus_message_error_get(msg, &error_name, &error_text);
   EINA_SAFETY_ON_TRUE_GOTO(res, finish);

   res = eldbus_message_arguments_get(msg, VALUE_TYPE_REPLY_WININFO_TREE,
                                      &pwin, &pname, &num_children, &array_of_children);
   EINA_SAFETY_ON_FALSE_GOTO(res, finish);

   if (window_id_format_dec)
     printf("\n   Parent window id: %lu \"%s\"\n", (unsigned long)pwin, pname);
   else
     printf("\n   Parent window id: 0x%lx \"%s\"\n", (unsigned long)pwin, pname);

   printf ("      %d child%s%s\n", num_children, num_children == 1 ? "" : "ren",
      num_children ? ":" : ".");

   while (eldbus_message_iter_get_and_next(array_of_children, 'r', &child))
     {
        uint64_t child_win;
        const char *child_name;
        int x, y, w, h, hwc, pl_zpos, level, j;

        res = eldbus_message_iter_arguments_get(child,
                                                "tsiiiiiiii",
                                                &child_win,
                                                &child_name,
                                                &num_children, &level,
                                                &x, &y, &w, &h, &hwc, &pl_zpos);
        EINA_SAFETY_ON_FALSE_GOTO(res, finish);

        for (j = 0; j <= level; j++) printf ("   ");
        if (window_id_format_dec)
          printf("%lu \"%s\":", (unsigned long)child_win, child_name);
        else
          printf("0x%lx \"%s\":", (unsigned long)child_win, child_name);
        printf (" %dx%d+%d+%d", w, h, x, y);
        if (pl_zpos == -999)
          printf(" - ");
        else
          {
             if (hwc > 0) printf(" hwc@%i", pl_zpos);
             else if (!hwc) printf(" comp@%i", pl_zpos);
          }
        printf("\n");
        if (num_children > 0)
          {
             for (j = 0; j <= level + 1; j++) printf ("   ");
             printf ("%d child%s:\n", num_children, num_children == 1 ? "" : "ren");
          }
     }


   return;

finish:
   if ((error_name) || (error_text))
     {
        printf("errname:%s errmsg:%s\n", error_name, error_text);
     }
}

static void
_e_info_client_cb_wininfo_print_hints(const Eldbus_Message *msg)
{
   const char *name = NULL, *text = NULL;
   Eina_Bool res;
   Eldbus_Message_Iter *array_of_hints;
   int count = 0;
   char *hint;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_GOTO(res, finish);

   res = eldbus_message_arguments_get(msg, "as", &array_of_hints);
   EINA_SAFETY_ON_FALSE_GOTO(res, finish);

   while (eldbus_message_iter_get_and_next(array_of_hints, 's', &hint))
     {
        printf("   %s\n", hint);
        count++;
     }

   if (!count)
     printf("   No window hints\n");

   ecore_main_loop_quit();

   return;

finish:
   if ((name) || (text))
     {
        printf("errname:%s errmsg:%s\n", name, text);
     }

   ecore_main_loop_quit();
}

static void
_e_info_client_cb_wininfo_print_shape(const Eldbus_Message *msg)
{
   const char *name = NULL, *text = NULL;
   Eina_Bool res;
   Eldbus_Message_Iter *array_of_shape, *array_of_shape_input;
   Eldbus_Message_Iter *struct_of_shape;
   int count = 0;
   int shape_rects_num, shape_input_rects_num;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_GOTO(res, finish);

   res = eldbus_message_arguments_get(msg, "ia(iiii)ia(iiii)",
                                      &shape_rects_num, &array_of_shape,
                                      &shape_input_rects_num, &array_of_shape_input);
   EINA_SAFETY_ON_FALSE_GOTO(res, finish);

   printf("   Number of shape rectangles: %d\n", shape_rects_num);
   if (shape_rects_num)
     {
        while (eldbus_message_iter_get_and_next(array_of_shape, 'r', &struct_of_shape))
          {
             int x, y, w, h;
             res = eldbus_message_iter_arguments_get(struct_of_shape,
                                                     "iiii",
                                                     &x, &y, &w, &h);
             EINA_SAFETY_ON_FALSE_GOTO(res, finish);
             count++;
             printf("      %d) x(%d), y(%d), w(%d), h(%d)\n", count, x, y, w, h);

          }
     }

   count = 0;
   printf("   Number of shape input rectangles: %d\n", shape_input_rects_num);
   if (shape_input_rects_num)
     {
        while (eldbus_message_iter_get_and_next(array_of_shape_input, 'r', &struct_of_shape))
          {
             int x, y, w, h;
             res = eldbus_message_iter_arguments_get(struct_of_shape,
                                                     "iiii",
                                                     &x, &y, &w, &h);
             EINA_SAFETY_ON_FALSE_GOTO(res, finish);
             count++;
             printf("      %d) x(%d), y(%d), w(%d), h(%d)\n", count, x, y, w, h);

          }
     }

   ecore_main_loop_quit();

   return;

finish:
   if ((name) || (text))
     {
        printf("errname:%s errmsg:%s\n", name, text);
     }

   ecore_main_loop_quit();
}

static Eina_Bool
_e_info_client_display_wininfo(uint64_t win, int children, int tree, int stats,
                               int wm, int size, int shape)
{
   Eina_Bool res;
   char *win_name;

   win_name = _e_get_window_name(win);
   EINA_SAFETY_ON_NULL_RETURN_VAL(win_name, EINA_FALSE);

   if (window_id_format_dec)
     printf("\nwininfo: Window id: %lu \"%s\"\n", (unsigned long)win, win_name);
   else
     printf("\nwininfo: Window id: 0x%lx \"%s\"\n", (unsigned long)win, win_name);

   free(win_name);

   if (!children && !tree && !wm && !size && !shape)
     stats = 1;

   if ((children || tree))
     {
        res = _e_info_client_eldbus_message_with_args("wininfo_tree",
                                                      _e_info_client_cb_wininfo_tree,
                                                      VALUE_TYPE_REQUEST_FOR_WININFO_TREE,
                                                      win, tree);
        EINA_SAFETY_ON_FALSE_RETURN_VAL(res, EINA_FALSE);
     }

   if (stats)
     {
        res = _e_info_client_eldbus_message_with_args("wininfo",
                                                      _e_info_client_cb_wininfo,
                                                      VALUE_TYPE_REQUEST_FOR_WININFO,
                                                      win);
        EINA_SAFETY_ON_FALSE_RETURN_VAL(res, EINA_FALSE);
     }

   if (wm)
     {
        printf("\nAux_Hint:\n");
        res = _e_info_client_eldbus_message_with_args("wininfo_hints",
                                                      _e_info_client_cb_wininfo_print_hints,
                                                      "it",
                                                      1, win);
        EINA_SAFETY_ON_FALSE_RETURN_VAL(res, EINA_FALSE);
     }

   if (size)
     {
        printf("\nSize hints:\n");
        res = _e_info_client_eldbus_message_with_args("wininfo_hints",
                                                      _e_info_client_cb_wininfo_print_hints,
                                                      "it",
                                                      0, win);
        EINA_SAFETY_ON_FALSE_RETURN_VAL(res, EINA_FALSE);
     }

   if (shape)
     {
        res = _e_info_client_eldbus_message_with_args("wininfo_shape",
                                                      _e_info_client_cb_wininfo_print_shape,
                                                      "t",
                                                      win);
        EINA_SAFETY_ON_FALSE_RETURN_VAL(res, EINA_FALSE);
     }

   return EINA_TRUE;
}

#define WININFO_USAGE \
  "[-options ...]\n\n" \
  "where options include:\n" \
  "\t-help             : print this message.\n" \
  "\t-children         : print parent and child identifiers.\n" \
  "\t-tree             : print children identifiers recursively.\n" \
  "\t-stats            : print window geometry [DEFAULT]\n" \
  "\t-id windowid      : use the window with the specified id\n" \
  "\t-name windowname  : use the window with the specified name\n" \
  "\t-pid windowpid    : use the window with the specified id\n" \
  "\t-int              : print window id in decimal\n" \
  "\t-size             : print size hints\n" \
  "\t-wm               : print window manager hints\n" \
  "\t-shape            : print shape rectangles\n" \
  "\t-all              : -tree, -stats, -wm, -size, -shape\n" \
  "Example:\n" \
  "\tenlightenment_info -wininfo\n" \
  "\tenlightenment_info -wininfo -id [win_id] -all\n" \
  "\tenlightenment_info -wininfo -children -stats -size\n" \
  "\tenlightenment_info -wininfo -name [win_name] -tree -wm\n" \
  "\tenlightenment_info -wininfo -pid [win_pid] -size -shape -int\n" \

static void
_e_info_client_proc_wininfo(int argc, char **argv)
{
   Eina_Bool res;
   unsigned long tmp = 0;
   uintptr_t ecore_win = 0;
   uint64_t win = 0;
   int i, children = 0, tree = 0, stats = 0, wm = 0, size = 0, shape = 0;
   char *name = NULL, *pid = NULL;
   Eina_List *win_list = NULL, *l;

   /* Handle our command line arguments */
   for (i = 2; i < argc; i++)
     {
        if (eina_streq(argv[i], "-help"))
          goto usage;

        if (eina_streq (argv[i], "-children"))
          {
             children = 1;
             continue;
          }

        if (eina_streq(argv[i], "-tree"))
          {
             tree = 1;
             continue;
          }

        if (eina_streq(argv[i], "-stats"))
          {
             stats = 1;
             continue;
          }

        if (eina_streq(argv[i], "-id"))
          {
             if (++i >= argc || (argv[i][0] < '0' || argv[i][0] > '9'))
               {
                  printf("Error: -id requires argument\n");
                  goto usage;
               }

             if (strlen(argv[i]) >= 2 && argv[i][0] == '0' && argv[i][1] == 'x')
               res = _util_string_to_ulong(argv[i], &tmp, 16);
             else
               res = _util_string_to_ulong(argv[i], &tmp, 10);

             win = (uint64_t)tmp;

             EINA_SAFETY_ON_FALSE_GOTO(res, usage);

             continue;
          }

        if (eina_streq(argv[i], "-name"))
          {
             if (++i >= argc)
               {
                  printf("Error: -name requires argument\n");
                  goto usage;
               }

             name = argv[i];
             continue;
          }

        if (eina_streq(argv[i], "-pid"))
          {
             if (++i >= argc || (argv[i][0] < '0' || argv[i][0] > '9'))
               {
                  printf("Error: -name requires argument\n");
                  goto usage;
               }

             pid = argv[i];
             continue;
          }
        if (eina_streq (argv[i], "-int"))
          {
             window_id_format_dec = 1;
             continue;
          }
        if (eina_streq (argv[i], "-wm"))
          {
             wm = 1;
             continue;
          }
        if (eina_streq (argv[i], "-size"))
          {
             size = 1;
             continue;
          }
        if (eina_streq (argv[i], "-shape"))
          {
             shape = 1;
             continue;
          }
        if (eina_streq (argv[i], "-all"))
          {
             tree = 1;
             stats = 1;
             wm = 1;
             size = 1;
             shape = 1;
             continue;
          }

        goto usage;
     }

   if (!win && (name || pid))
     {
        if (name)
          win_list = _e_get_windows(_E_GET_WINDOWS_NAME_MODE, name);
        else
          win_list = _e_get_windows(_E_GET_WINDOWS_PID_MODE, pid);

        if (!win_list)
          {
             printf("Error: cannot get windows\n");
             return;
          }
     }

   if (!win && !win_list)
     {
        printf("Please select the window about which you\n"
               "would like information by clicking the\n"
               "mouse in that window.\n");
        if (_e_get_window_under_touch(&ecore_win))
          {
             printf("Error: cannot get window under touch\n");
             return;
          }
        win = (uint64_t)ecore_win;
     }

   if (win)
     {
        res = _e_info_client_display_wininfo(win, children, tree, stats, wm, size, shape);
        EINA_SAFETY_ON_FALSE_RETURN(res);
     }
   else
     {
        for(l = win_list; l; l = eina_list_next(l))
          {
             uint64_t win;

             win = (uint64_t)((Ecore_Window)eina_list_data_get(l));
             res = _e_info_client_display_wininfo(win, children, tree, stats, wm, size, shape);
             EINA_SAFETY_ON_FALSE_GOTO(res, finish);
          }
     }

finish:
   if (win_list)
     eina_list_free(win_list);

   return;

usage:
   printf("Usage: enlightenment_info -wininfo %s", WININFO_USAGE);
}

static void
_cb_window_proc_version_get(const Eldbus_Message *msg)
{
   const char *name = NULL, *text = NULL;
   char *ver, *rel;
   Eina_Bool res;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_RETURN(res);

   res = eldbus_message_arguments_get(msg, "ss", &ver, &rel);
   EINA_SAFETY_ON_FALSE_RETURN(res);

   printf("Version: %s\n", ver);
   printf("Release: %s\n", rel);
}

static void
_e_info_client_proc_version(int argc, char **argv)
{
   if (!_e_info_client_eldbus_message("get_version", _cb_window_proc_version_get))
     {
        printf("_e_info_client_eldbus_message error:%s\n", "get_einfo");
        return;
     }
}

static void
_e_info_client_cb_module_list_get(const Eldbus_Message *msg)
{
   const char *errname = NULL, *errtext = NULL;
   Eldbus_Message_Iter *module_array = NULL;
   Eldbus_Message_Iter *inner_module_array = NULL;
   Eina_Stringshare *module_name = NULL;
   int count = 0;
   int onoff = 0;

   // check error
   EINA_SAFETY_ON_TRUE_GOTO(eldbus_message_error_get(msg, &errname, &errtext), err);

   // get arguments
   EINA_SAFETY_ON_FALSE_GOTO(eldbus_message_arguments_get(msg, "ia(si)", &count, &module_array), err);
   printf("============< print module list >===========\n");
   printf("module count : %d\n", count);
   while (eldbus_message_iter_get_and_next(module_array, 'r', &inner_module_array))
     {
        EINA_SAFETY_ON_FALSE_GOTO(
           eldbus_message_iter_arguments_get(inner_module_array, "si", &module_name, &onoff),
           err);
        printf("module [ %30s ]\t:\t%s\n", module_name, onoff?"enabled":"disabled");
     }
   goto finish;

err:
   if (errname || errtext)
     printf("errname : %s, errmsg : %s\n", errname, errtext);
   else
     printf("Error occurred in _e_info_client_cb_module_list_get\n");

finish:
   return;
}

static void
_e_info_client_cb_module_load(const Eldbus_Message *msg)
{
   const char *errname = NULL, *errtext = NULL;
   const char *result = NULL;

   EINA_SAFETY_ON_TRUE_GOTO(eldbus_message_error_get(msg, &errname, &errtext), err);

   EINA_SAFETY_ON_FALSE_GOTO(eldbus_message_arguments_get(msg, "s", &result), err);

   printf("%s\n", result);
   goto finish;

err:
   if (errname || errtext)
     printf("errname : %s, errmsg : %s\n", errname, errtext);
   else
     printf("Error occurred in _e_info_client_cb_module_load\n");

finish:
   return;
}

static void
_e_info_client_cb_module_unload(const Eldbus_Message *msg)
{
   const char *errname = NULL, *errtext = NULL;
   const char *result = NULL;

   EINA_SAFETY_ON_TRUE_GOTO(eldbus_message_error_get(msg, &errname, &errtext), err);

   EINA_SAFETY_ON_FALSE_GOTO(eldbus_message_arguments_get(msg, "s", &result), err);

   printf("%s\n", result);
   goto finish;

err:
   if (errname || errtext)
     printf("errname : %s, errmsg : %s\n", errname, errtext);
   else
     printf("Error occurred in _e_info_client_cb_module_unload\n");

finish:
   return;
}

static void
_e_info_client_proc_module(int argc, char **argv)
{
   const char *program = argv[0];
   const char *command = argv[2];
   const char *module_name = argv[3];
   Eina_Bool res = EINA_FALSE;

   if (((argc < 3) || (argc > 4)))
     {
       goto usage;
     }

   if (strncmp(command, "list", strlen(command)) == 0)
     {
        if (argc != 3)
          goto usage;

        res = _e_info_client_eldbus_message("module_list_get", _e_info_client_cb_module_list_get);
     }
   else if (strncmp(command, "load", strlen(command)) == 0)
     {
        if (argc != 4)
           goto usage;

        res = _e_info_client_eldbus_message_with_args("module_load",
                                                _e_info_client_cb_module_load,
                                                "s",
                                                module_name);
     }
   else if (strncmp(command, "unload", strlen(command)) == 0)
     {
        if (argc != 4)
           goto usage;

        res = _e_info_client_eldbus_message_with_args("module_unload",
                                                _e_info_client_cb_module_unload,
                                                "s",
                                                module_name);
     }
   else
     goto usage;

   EINA_SAFETY_ON_FALSE_GOTO(res, error);

   return;

error:
   printf("Error occured while send send message\n\n");

usage:
   printf("Usage : %s -module <command> [<module_name>]\n\n", program);
   printf("Commands:\n"
          "list : Print the current modules list loaded\n"
          "load <module_name> : Load module with the given name\n"
          "unload <module_name> : Unload module with the given name\n\n");
   printf("Example:\n"
          "%s -module load e-mod-tizen-effect\n"
          "%s -module unload e-mod-tizen-effect\n", program, program);
   return;
}

static void
_e_info_client_cb_shutdown(const Eldbus_Message *msg)
{
   const char *errname = NULL, *errtext = NULL;
   const char *result = NULL;

   EINA_SAFETY_ON_TRUE_GOTO(eldbus_message_error_get(msg, &errname, &errtext), err);

   EINA_SAFETY_ON_FALSE_GOTO(eldbus_message_arguments_get(msg, "s", &result), err);

   printf("%s", result);
   goto finish;

err:
   if(errname || errtext)
     printf("errname : %s, errmsg : %s\n", errname, errtext);
   else
     printf("Error occurred in _e_info_client_cb_shutdown\n");

finish:
   return;
}

static void
_e_info_client_proc_shutdown(int argc, char **argv)
{
   EINA_SAFETY_ON_FALSE_GOTO(argc == 2, usage);

   _e_info_client_eldbus_message("shutdown", _e_info_client_cb_shutdown);
   goto finish;

usage :
   printf("Usage : %s -shutdown\n\n", argv[0]);

finish:
   return;
}

static void
_e_info_client_cb_buffer_flush(const Eldbus_Message *msg)
{
   const char *errname = NULL, *errtext = NULL;
   const char *result = NULL;

   EINA_SAFETY_ON_TRUE_GOTO(eldbus_message_error_get(msg, &errname, &errtext), err);
   EINA_SAFETY_ON_FALSE_GOTO(eldbus_message_arguments_get(msg, "s", &result), err);

   printf("%s\n", result);

   goto finish;

err:
   if (errname || errtext)
     {
        printf("errname : %s, errmsg : %s\n", errname, errtext);
     }
   else
     {
        printf("Error occured in _e_info_client_cb_buffer_flush\n");
     }

finish:
   return;
}

static void
_e_info_client_proc_buffer_flush(int argc, char **argv)
{
   unsigned long winid = 0x0;
   uint64_t send_winid = 0x0;
   Ecore_Window win = 0;
   Eina_Bool res = EINA_FALSE;
   int option = -1;
   char *win_name = NULL;

   EINA_SAFETY_ON_FALSE_GOTO((argc == 3) || (argc == 4), usage);
   EINA_SAFETY_ON_FALSE_GOTO((!strcmp(argv[2], "on")) ||
                             (!strcmp(argv[2], "off")) ||
                             (!strcmp(argv[2], "show")), usage);

   if (argc == 4)
     {
        // if input has window id, convert to ulong
        if (!strcmp(argv[3], "all"))
          res = EINA_TRUE;
        else if ((strlen(argv[3]) >= 2) && (argv[3][0] == '0') && ((argv[3][1] == 'x') || (argv[3][1] == 'X')))
          res = _util_string_to_ulong(argv[3], &winid, 16);
        else
          res = _util_string_to_ulong(argv[3], &winid, 10);
        if (!res)
          {
             printf("error occured while parsing winid: %s\n", argv[3]);
             return;
          }

        send_winid = (uint64_t) winid;
     }

   if (!strcmp(argv[2], "show"))
     option = 2;
   else{
        if (argc == 3)
          {
             // get winid from touch
             printf("Select the window whose property(ies) you wish to get/set\n");
             if (!_e_get_window_under_touch(&win))
               {
                  win_name = _e_get_window_name(win);
                  if (!win_name)
                    {
                       printf("failed to get window under touch\n");
                       return;
                    }

                  printf("%s %s: window(%s) id : 0x%08zx\n", argv[0], argv[1], win_name, win);
                  send_winid = (uint64_t) win;

                  free(win_name);
               }
             else
               {
                  printf("failed to get window under touch\n");
                  return;
               }
          }

        if (!strcmp(argv[2], "on"))
          option = 1;
        else if (!strcmp(argv[2], "off"))
          option = 0;
        else
          goto usage;
   }

   res = _e_info_client_eldbus_message_with_args("buffer_flush",
                                                 _e_info_client_cb_buffer_flush,
                                                 "it",
                                                 option,
                                                 send_winid);
   EINA_SAFETY_ON_FALSE_GOTO(res, error);

   return;

error:
   printf("Error occured while send send message\n\n");

usage:
   printf("Usage : %s %s [on <win_id / all>], [off <win_id / all>], [show <win_id / all>]\n\n"
          "\t on : turn on buffer_flush option\n"
          "\t off : turn off buffer_flush option\n"
          "\t show : show buffer_flush configuration\n",
          argv[0], argv[1]);
   printf("\n\t %s %s on 0x12345678\n", argv[0], argv[1]);
   printf("\t %s %s off all\n", argv[0], argv[1]);
   printf("\t %s %s show 0x12345678\n", argv[0], argv[1]);

   return;
}

static void
_e_info_client_cb_deiconify_approve(const Eldbus_Message *msg)
{
   const char *errname = NULL, *errtext = NULL;
   const char *result = NULL;

   EINA_SAFETY_ON_TRUE_GOTO(eldbus_message_error_get(msg, &errname, &errtext), err);
   EINA_SAFETY_ON_FALSE_GOTO(eldbus_message_arguments_get(msg, "s", &result), err);

   printf("%s\n", result);

   goto finish;

err:
   if (errname || errtext)
     {
        printf("errname : %s, errmsg : %s\n", errname, errtext);
     }
   else
     {
        printf("Error occured in _e_info_client_cb_deiconify_approve\n");
     }

finish:
   return;
}

static void
_e_info_client_proc_deiconify_approve(int argc, char **argv)
{
   unsigned long winid = 0x0;
   uint64_t send_winid = 0x0;
   Ecore_Window win = 0;
   Eina_Bool res = EINA_FALSE;
   int option = -1;
   char *win_name = NULL;

   EINA_SAFETY_ON_TRUE_GOTO(((argc < 3) || (argc > 4)), usage);
   EINA_SAFETY_ON_FALSE_GOTO((!strcmp(argv[2], "on")) ||
                             (!strcmp(argv[2], "off")) ||
                             (!strcmp(argv[2], "show")), usage);
   if (argc == 4)
     {
        // if input has window id, convert to ulong
        if (!strcmp(argv[3], "all"))
          res = EINA_TRUE;
        else if ((strlen(argv[3]) >= 2) && (argv[3][0] == '0') && ((argv[3][1] == 'x') || (argv[3][1] == 'X')))
          res = _util_string_to_ulong(argv[3], &winid, 16);
        else
          res = _util_string_to_ulong(argv[3], &winid, 10);
        if (!res)
          {
             printf("error occured while parsing winid: %s\n", argv[3]);
             return;
          }

        send_winid = (uint64_t) winid;
     }

   if (!strcmp(argv[2], "show"))
     option = 2;
   else{
        if (argc == 3)
          {
             // get winid from touch
             printf("Select the window whose property(ies) you wish to get/set\n");
             if (!_e_get_window_under_touch(&win))
               {
                  win_name = _e_get_window_name(win);
                  if (!win_name)
                    {
                       printf("failed to get window under touch\n");
                       return;
                    }

                  printf("%s %s: window(%s) id : 0x%08zx\n", argv[0], argv[1], win_name, win);
                  send_winid = (uint64_t) win;

                  free(win_name);
               }
             else
               {
                  printf("failed to get window under touch\n");
                  return;
               }
          }

        if (!strcmp(argv[2], "on"))
          option = 1;
        else if (!strcmp(argv[2], "off"))
          option = 0;
        else
          goto usage;
   }

   res = _e_info_client_eldbus_message_with_args("deiconify_approve",
                                                 _e_info_client_cb_deiconify_approve,
                                                 "it",
                                                 option,
                                                 send_winid);
   EINA_SAFETY_ON_FALSE_GOTO(res, error);

   return;

error:
   printf("Error occured while send send message\n\n");

usage:
   printf("Usage : %s %s [on <win_id / all>], [off <win_id / all>], [show <win_id / all>]\n\n"
          "\t on : turn on deiconify_approve option\n"
          "\t off : turn off deiconify_approve option\n"
          "\t show : show deiconify_approve configuration\n",
          argv[0], argv[1]);
   printf("\n\t %s %s on 0x12345678\n", argv[0], argv[1]);
   printf("\t %s %s off all\n", argv[0], argv[1]);
   printf("\t %s %s show 0x12345678\n", argv[0], argv[1]);

   return;
}

static void
_e_info_client_proc_key_repeat(int argc, char **argv)
{
   char fd_name[PATH_MAX] = {0,};
   int pid, rate = 0, delay = 0;

   if (argc == 3 && !strncmp(argv[2], "print", sizeof("print")))
     {
        pid = getpid();
        snprintf(fd_name, PATH_MAX, "/proc/%d/fd/1", pid);
     }
   else if (argc > 3 && argc < 6 && !strncmp(argv[2], "set", sizeof("set")))
     {
        delay = atoi(argv[3]);
        if (argc > 4) rate = atoi(argv[4]);
     }
   else goto usage;

   if (!_e_info_client_eldbus_message_with_args("key_repeat", NULL, "sii", fd_name, delay, rate))
     printf("Error occured while send message\n");

   return;

usage:
   printf("Usage : %s %s [print], [set <delay> <rate>]\n\n"
          "\t print : print current key repeat info\n"
          "\t set : set delay and rate (0: do not change this option)\n",
          argv[0], argv[1]);
   printf("\n\t %s %s print\n", argv[0], argv[1]);
   printf("\t %s %s set 400 25\n", argv[0], argv[1]);
   printf("\t %s %s set 0 50\n", argv[0], argv[1]);
}

static void
_e_info_client_memchecker(int argc, char **argv)
{
   if (!_e_info_client_eldbus_message("dump_memchecker", NULL))
     return;

   printf("e20 dump log file under /tmp dir.\n");
}

static void
_e_info_client_magnifier(int argc, char **argv)
{
   uint32_t op;

   if (argc < 3)
     {
        printf("Error Check Args: enlightenment_info -magnifier [1: on, 0: off]\n");
        return;
     }

   if (!strncmp(argv[2], "off", sizeof("off"))) op = 0;
   else if (!strncmp(argv[2], "on", sizeof("on"))) op = 1;
   else if (!strncmp(argv[2], "new", sizeof("new"))) op = 2;
   else if (!strncmp(argv[2], "del", sizeof("del"))) op = 3;
   else if (!strncmp(argv[2], "set_stand_alone", sizeof("set_stand_alone"))) op = 4;
   else if (!strncmp(argv[2], "unset_stand_alone", sizeof("unset_stand_alone"))) op = 5;
   else if (!strncmp(argv[2], "show", sizeof("show"))) op = 6;
   else if (!strncmp(argv[2], "hide", sizeof("hide"))) op = 7;
   else
     {
        printf("Error Check Args: enlightenment_info -magnifier [on/off]\n");
        return;
     }

   if (!_e_info_client_eldbus_message_with_args("magnifier", NULL, "i", op))
     printf("_e_info_client_eldbus_message_with_args error");
}

static void
_cb_input_region_get(const Eldbus_Message *msg)
{
   const char *name = NULL, *text = NULL;
   Eldbus_Message_Iter *array;
   Eldbus_Message_Iter *iter;
   Eina_Bool res;
   int cnt = 0;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_GOTO(res, finish);

   printf("Input region\n");

   res = eldbus_message_arguments_get(msg, "a(iiii)", &array);
   if (!res)
     {
        printf("\tNo Input region\n");
        return;
     }

   while (eldbus_message_iter_get_and_next(array, 'r', &iter))
     {
        int x = 0, y = 0, w = 0, h = 0;
        res = eldbus_message_iter_arguments_get(iter,
                                                "iiii",
                                                &x,
                                                &y,
                                                &w,
                                                &h);
        if (!res)
          {
             printf("Failed to get input region info\n");
             continue;
          }
        cnt++;
        printf("\t[%d] [(%d, %d),  %dx%d]\n", cnt, x, y, w, h);
     }
   if (cnt == 0) printf("\tNo Input region\n");

finish:
   if ((name) || (text))
     {
        printf("errname:%s errmsg:%s\n", name, text);
     }
}

static void
_e_info_client_input_region_usage(void)
{
   printf("\nUsage: \n");
   printf("\twinfo -input_region [options] [window_id]\n");
   printf("\t\toption: -t: time to show input_regions area (sec)\n");
   printf("\t\t        -color: color to shwo input_regions area (r, g, b) default: red\n");
   printf("\tex> winfo -input_region\n");
   printf("\t    winfo -input_region -t 2 -color g 0xabc123\n");
}

static void
_e_info_client_proc_input_region(int argc, char **argv)
{
   const char *win_id = NULL;

   Ecore_Window win;
   char win_temp[64] = {0, };
   int time = 5;
   int cnt, idx;
   int color_r = 0, color_g = 0, color_b = 0;

   cnt = argc - 2;
   idx = 2;

   while (cnt > 0)
     {
        if (argv[idx][0] == '-')
          {
             if (argv[idx][1] == 't')
               {
                  idx++;
                  cnt--;
                  if (cnt <= 0)
                    {
                       printf("Please input correct options\n");
                       _e_info_client_input_region_usage();
                       return;
                    }
                  time = atoi(argv[idx]);
               }
             else if (!strncmp(argv[idx], "-color", sizeof("-color")))
               {
                  idx++;
                  cnt--;
                  if (cnt <= 0)
                    {
                       printf("Please input correct options\n");
                       _e_info_client_input_region_usage();
                       return;
                    }
                  if (argv[idx][0] == 'r')
                    {
                       color_r = 255;
                    }
                  else if (argv[idx][0] == 'g')
                    {
                       color_g = 255;
                    }
                  else if (argv[idx][0] == 'b')
                    {
                       color_b = 255;
                    }
               }
          }
        else if (strstr(argv[idx], "0x"))
          {
             win_id = argv[idx];
          }
        else if (!strncmp(argv[idx], "help", sizeof("help")))
          {
             _e_info_client_input_region_usage();
             return;
          }

        idx++;
        cnt--;
     }
   if (!win_id)
     {
        printf("Select the window whose input_regions you wish to show\n");
        printf("If you want to see more option, please input \"help\" > winfo -input_region help\n");
        if (_e_get_window_under_touch(&win))
          {
             printf("Error: cannot get window under touch\n");
             return;
          }

        snprintf(win_temp, sizeof(win_temp), "%lu", (unsigned long int)win);

        win_id = win_temp;
     }
   if (!color_r && !color_g && !color_b)
     color_r = 255;

   if (!_e_info_client_eldbus_message_with_args("input_region", _cb_input_region_get, "siiii", win_id, time, color_r, color_g, color_b))
     printf("Error occured while send message\n");

   return;
}

static void
_cb_hwc_wins_info_get(const Eldbus_Message *msg)
{
   const char *name = NULL, *text = NULL;
   Eina_Bool res;
   Eldbus_Message_Iter *lines;
   char *result = NULL;

   res = eldbus_message_error_get(msg, &name, &text);
   if (res) goto finish;

   res = eldbus_message_arguments_get(msg, "as", &lines);
   if (!res) goto finish;

   while (eldbus_message_iter_get_and_next(lines, 's', &result))
     printf("%s\n", result);

   return;

finish:
   if ((name) || (text))
     {
        printf("errname:%s errmsg:%s\n", name, text);
     }
}

static void
_e_info_client_proc_hwc_wins(int argc, char **argv)
{
   Eina_Bool res;
   E_Hwc_Wins_Debug_Cmd cmd;

   if (argc < 2)
     cmd = E_HWC_WINS_DEBUG_CMD_VIS;
   else
     {
        if (!argv[2])
          cmd = E_HWC_WINS_DEBUG_CMD_VIS;
        else if (eina_streq(argv[2], "all"))
          cmd = E_HWC_WINS_DEBUG_CMD_ALL;
        else if (eina_streq(argv[2], "dv"))
          cmd = E_HWC_WINS_DEBUG_CMD_DV;
        else if (eina_streq(argv[2], "cl"))
          cmd = E_HWC_WINS_DEBUG_CMD_CL;
        else if (eina_streq(argv[2], "cs"))
          cmd = E_HWC_WINS_DEBUG_CMD_CS;
        else if (eina_streq(argv[2], "vd"))
          cmd = E_HWC_WINS_DEBUG_CMD_VD;
        else if (eina_streq(argv[2], "no"))
          cmd = E_HWC_WINS_DEBUG_CMD_NO;
        else if (eina_streq(argv[2], "queue"))
          cmd = E_HWC_WINS_DEBUG_CMD_QUEUE;
        else if (eina_streq(argv[2], "help") || eina_streq(argv[2], "usage"))
          goto usage;
        else
          goto usage;
     }

   res = _e_info_client_eldbus_message_with_args("hwc_wins", _cb_hwc_wins_info_get, "i", cmd);

   EINA_SAFETY_ON_FALSE_RETURN(res);

   return;

usage:
   printf("Usage: wininfo_info %s", USAGE_HWC_WINS);

   return;
}

typedef struct _ProcInfo
{
   const char *option;
   const char *params;
   const char *description;
   void (*func)(int argc, char **argv);
} ProcInfo;

static ProcInfo procs_to_tracelogs[] =
{
   {
      "version",
      NULL,
      "Print version of enlightenment",
      _e_info_client_proc_version
   },
   {
      "protocol_trace", "[console|file_path|disable]",
      "Enable/Disable wayland protocol trace",
      _e_info_client_proc_protocol_trace
   },
   {
      "protocol_rule",
      PROTOCOL_RULE_USAGE,
      "Add/Remove wayland protocol rule you want to trace",
      _e_info_client_proc_protocol_rule
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
      "Logging using dlog system [on 1, off 0]",
      _e_info_client_proc_dlog_switch
   },
#endif
   {
      "trace",
      "[hwc | serial] [off: 0, on: 1]",
      "Show the trace log in detail",
      _e_info_client_proc_trace
   },
};

static ProcInfo procs_to_printinfo[] =
{
   {
      "topvwins", NULL,
      "Print top visible windows",
      _e_info_client_proc_topvwins_info
   },
   {
      "topwins", NULL,
      "Print all windows",
      _e_info_client_proc_topwins_info
   },
   {
      "compobjs", "[simple]",
      "Print detailed information of all composite objects",
      _e_info_client_proc_compobjs_info
   },
   {
      "subsurface", NULL,
      "Print subsurface information",
      _e_info_client_proc_subsurface
   },
   {
      "connected_clients", NULL,
      "Print connected clients on Enlightenment",
      _e_info_client_proc_connected_clients
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
      "output_mode",
      "[mode number to set]",
      "Print output mode info",
      _e_info_client_proc_output_mode
   },
   {
      "show_plane_state",
      NULL,
      "Print state of plane",
      _e_info_client_proc_show_plane_state
   },
   {
      "show_pending_commit",
      NULL,
      "Print state of pending commit",
      _e_info_client_proc_show_pending_commit
   },
   {
      "fps", NULL,
      "Print FPS in every sec per",
      _e_info_client_proc_fps_info
   },
   {
      "keymap", NULL,
      "Print a current keymap",
      _e_info_client_proc_keymap_info
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
      "wininfo",
      WININFO_USAGE,
      "Print information about windows",
      _e_info_client_proc_wininfo
   },
   {
      "input_region",
      NULL,
      "Print input regions",
      _e_info_client_proc_input_region
   },
   {
      "hwc_wins",
      NULL,
      "Print hwc windows information",
      _e_info_client_proc_hwc_wins
   },
};

static ProcInfo procs_to_execute[] =
{
   {
      "set_force_visible", NULL,
      "Show/Hide a composite object",
      _e_info_client_proc_force_visible
   },
   {
      "dump",
      USAGE_DUMPIMAGE,
      "Dump window images with options [topvwins, ns]",
      _e_info_client_proc_wins_shot
   },
   {
      "prop",
      PROP_USAGE,
      "Get/Set window(s) property(ies)",
      _e_info_client_prop_prop_info
   },
   {
      "rotation",
      ROTATION_USAGE,
      "Send a message about rotation",
      _e_info_client_proc_rotation
   },
   {
      "bgcolor_set", "[<a>,<r>,<g>,<b>]",
      "Set the background color of enlightenment canvas",
      _e_info_client_proc_bgcolor_set
   },
   {
      "key_repeat",
      "[print] [set <delay> <rate>]",
      "Print or Set key repeat info",
      _e_info_client_proc_key_repeat
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
      "dump_selected_buffers", DUMP_BUFFERS_USAGE,
      "Dump Win_ID buffers. Win_ID comed from enlightenment_info -topvwins(default path:/tmp/dump_xxx/)",
      _e_info_client_proc_selected_buffer_shot
   },
   {
      "dump_screen", "enlightenment_info -dump_screen -p /tmp/ -n xxx.png   :make dump /tmp/xxx.png",
      "Dump current screen (default path:/tmp/dump_screen.png)",
      _e_info_client_proc_screen_shot
   },
   {
      "hwc",
      "[on: 1, off: 0, 2: info]",
      "HW composite policy on(1) and off(0), or prints info(2) via dlog",
      _e_info_client_proc_hwc
   },
   {
      "effect",
      "[on: 1, off: 0]",
      "Window effect [on 1, off 0]",
      _e_info_client_proc_effect_control
   },
   {
      "aux_msg",
      "[window] [key] [value] [options]",
      "Send aux message to client",
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
      USAGE_DESK,
      "Set geometry or zoom for current desktop",
      _e_info_client_proc_desk
   },
   {
      "frender",
      USAGE_FORCE_RENDER,
      "Force render according to parameters",
      _e_info_client_proc_force_render
   },
   {
      "screen_rotation_pre",
      "[0|90|180|270]",
      "To rotate screen (pre)",
      _e_info_client_proc_screen_rotation_pre
   },
   {
      "screen_rotation",
      "[0|90|180|270]",
      "To rotate screen",
      _e_info_client_proc_screen_rotation
   },
   {
      "remote_surface",
      USAGE_REMOTE_SURFACE,
      "For remote surface debugging",
      _e_info_client_proc_remote_surface
   },
   {
      "kill",
      KILL_USAGE,
      "Kill a client",
      _e_info_client_proc_kill_client
   },
   {
      "module",
      "[list], [load <module_name>], [unload <module_name>]",
      "Manage modules on enlightenment",
      _e_info_client_proc_module
   },
   {
      "shutdown",
      NULL,
      "Shutdown Enlightenment",
      _e_info_client_proc_shutdown
   },
   {
      "buffer_flush",
      "[on <win_id / all>], [off <win_id / all>], [show <win_id / all>]",
      "Set buffer_flush configure",
      _e_info_client_proc_buffer_flush
   },
   {
      "deiconify_approve",
      "[on <win_id / all>], [off <win_id / all>], [show <win_id / all>]",
      "Set deiconify_approve configure",
      _e_info_client_proc_deiconify_approve
   },
   {
      "dump_memory",
      "file dumped under /tmp dir.",
      "Dump stack information by allocations",
      _e_info_client_memchecker
   },
   {
      "magnifier",
      NULL,
      "On/Off magnifier window",
      _e_info_client_magnifier
   },
};

static Eina_List *list_tracelogs = NULL;
static Eina_List *list_printinfo = NULL;
static Eina_List *list_exec= NULL;

static int
_util_sort_string_cb(const void *data1, const void *data2)
{
   const ProcInfo *info1, *info2;
   const char *s1, *s2;

   info1 = data1;
   info2 = data2;
   s1 = info1->option;
   s2 = info2->option;

   return strncmp(s1, s2, strlen(s2));
}

static void
_e_info_client_shutdown_list(void)
{
   list_tracelogs = eina_list_free(list_tracelogs);
   list_printinfo = eina_list_free(list_printinfo);
   list_exec = eina_list_free(list_exec);
}

static void
_e_info_client_init_list(void)
{
   int n_info = 0, i;
   list_tracelogs = list_printinfo = list_exec = NULL;

   n_info = sizeof(procs_to_tracelogs) / sizeof(procs_to_tracelogs[0]);
   for (i = 0; i < n_info; i++)
     {
        list_tracelogs = eina_list_append(list_tracelogs, &procs_to_tracelogs[i]);
     }

   n_info = sizeof(procs_to_printinfo) / sizeof(procs_to_printinfo[0]);
   for (i = 0; i < n_info; i++)
     {
        list_printinfo = eina_list_append(list_printinfo, &procs_to_printinfo[i]);
     }
   list_printinfo = eina_list_sort(list_printinfo, eina_list_count(list_printinfo), _util_sort_string_cb);

   n_info = sizeof(procs_to_execute) / sizeof(procs_to_execute[0]);
   for (i = 0; i < n_info; i++)
     {
        list_exec = eina_list_append(list_exec, &procs_to_execute[i]);
     }
   list_exec = eina_list_sort(list_exec, eina_list_count(list_exec), _util_sort_string_cb);
}

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
   Eina_List *l = NULL;
   ProcInfo  *procinfo = NULL;
   int proc_option_length, argv_len;

   signal(SIGINT,  end_program);
   signal(SIGALRM, end_program);
   signal(SIGHUP,  end_program);
   signal(SIGPIPE, end_program);
   signal(SIGQUIT, end_program);
   signal(SIGTERM, end_program);

   argv_len = strlen(argv[1]+1);
   EINA_LIST_FOREACH(list_tracelogs, l, procinfo)
     {
        proc_option_length = strlen(procinfo->option);
        if (argv_len != proc_option_length) continue;
        if (!strncmp(argv[1]+1, procinfo->option, proc_option_length))
          {
             if (procinfo->func)
               procinfo->func(argc, argv);

             return EINA_TRUE;
          }
     }

   EINA_LIST_FOREACH(list_printinfo, l, procinfo)
     {
        proc_option_length = strlen(procinfo->option);
        if (argv_len != proc_option_length) continue;
        if (!strncmp(argv[1]+1, procinfo->option, proc_option_length))
          {
             if (procinfo->func)
               procinfo->func(argc, argv);

             return EINA_TRUE;
          }
     }

   EINA_LIST_FOREACH(list_exec, l, procinfo)
     {
        proc_option_length = strlen(procinfo->option);
        if (argv_len != proc_option_length) continue;
        if (!strncmp(argv[1]+1, procinfo->option, proc_option_length))
          {
             if (procinfo->func)
               procinfo->func(argc, argv);

             return EINA_TRUE;
          }
     }

   return EINA_FALSE;
}

static void
_e_info_client_print_usage_all(const char *exec)
{
   Eina_List *l = NULL;
   ProcInfo  *procinfo = NULL;

   printf("\nUsage:\n");
   EINA_LIST_FOREACH(list_tracelogs, l, procinfo)
     {
        printf("  %s -%s %s\n", exec, procinfo->option, (procinfo->params)?procinfo->params:"");
     }
   printf("\n\n");
   EINA_LIST_FOREACH(list_printinfo, l, procinfo)
     {
        printf("  %s -%s %s\n", exec, procinfo->option, (procinfo->params)?procinfo->params:"");
     }
   printf("\n\n");
   EINA_LIST_FOREACH(list_exec, l, procinfo)
     {
        printf("  %s -%s %s\n", exec, procinfo->option, (procinfo->params)?procinfo->params:"");
     }
}

static void
_e_info_client_print_usage(int argc, char **argv)
{
   Eina_List *l = NULL;
   ProcInfo  *procinfo = NULL;

   EINA_LIST_FOREACH(list_tracelogs, l, procinfo)
     {
        if (!strncmp(argv[1]+1, procinfo->option, strlen(procinfo->option)))
          {
             printf("  %s\n\n", (procinfo->description)?procinfo->description:"");
             printf("  %s -%s %s\n", argv[0], procinfo->option, (procinfo->params)?procinfo->params:"");
             goto end;
          }
     }

   EINA_LIST_FOREACH(list_printinfo, l, procinfo)
     {
        if (!strncmp(argv[1]+1, procinfo->option, strlen(procinfo->option)))
          {
             printf("  %s\n\n", (procinfo->description)?procinfo->description:"");
             printf("  %s -%s %s\n", argv[0], procinfo->option, (procinfo->params)?procinfo->params:"");
             goto end;
          }
     }

   EINA_LIST_FOREACH(list_exec, l, procinfo)
     {
        if (!strncmp(argv[1]+1, procinfo->option, strlen(procinfo->option)))
          {
             printf("  %s\n\n", (procinfo->description)?procinfo->description:"");
             printf("  %s -%s %s\n", argv[0], procinfo->option, (procinfo->params)?procinfo->params:"");
             goto end;
          }
     }

end:
   printf("\n");
}

static void
_e_info_client_print_description(const char *exec)
{
   Eina_List *l = NULL;
   ProcInfo  *procinfo = NULL;

   printf("\n\n");

   EINA_LIST_FOREACH(list_tracelogs, l, procinfo)
     {
        printf(" -%-30s\t", procinfo->option);
        printf(": %s\n", (procinfo->description)?procinfo->description:"");
     }
   printf("\n");
   EINA_LIST_FOREACH(list_printinfo, l, procinfo)
     {
        printf(" -%-30s\t", procinfo->option);
        printf(": %s\n", (procinfo->description)?procinfo->description:"");
     }
   printf("\n");
   EINA_LIST_FOREACH(list_exec, l, procinfo)
     {
        printf(" -%-30s\t", procinfo->option);
        printf(": %s\n", (procinfo->description)?procinfo->description:"");
     }

   printf("\n");
}

static void
end_program(int sig)
{
   ecore_main_loop_quit();
   /* disconnecting dbus */
   _e_info_client_eldbus_disconnect();
   exit(EXIT_FAILURE);
}

int
main(int argc, char **argv)
{
   if (!eina_init())
     {
        printf("fail eina_init");
        return -1;
     }

   if (!ecore_init())
     {
        printf("fail ecore_init");
        eina_shutdown();
        return -1;
     }

   /* list up all proc*/
   _e_info_client_init_list();

   if (argc < 2 || argv[1][0] != '-')
     {
        _e_info_client_print_description(argv[0]);
        return 0;
     }

   /* connecting dbus */
   if (!_e_info_client_eldbus_connect())
     {
        printf("fail eldbus connection");
        goto err;
     }
   if (!strcmp(argv[1], "-h") ||
       !strcmp(argv[1], "-help") ||
       !strcmp(argv[1], "--help"))
     {
        _e_info_client_print_usage_all(argv[0]);
     }
   else if (argc >= 3 &&
      (!strcmp(argv[2], "-h") ||
       !strcmp(argv[2], "-help") ||
       !strcmp(argv[2], "--help")))
     {
        _e_info_client_print_usage(argc, argv);
     }
   else
     {
        /* handling a client request */
        if (!_e_info_client_process(argc, argv))
          {
             printf("unknown option: %s\n", argv[1]);
             _e_info_client_print_usage(argc, argv);
          }
     }

   /* list free proc*/
   _e_info_client_shutdown_list();

   /* disconnecting dbus */
   _e_info_client_eldbus_disconnect();

   return 0;

err:
   _e_info_client_shutdown_list();
   _e_info_client_eldbus_disconnect();
   return -1;
}
