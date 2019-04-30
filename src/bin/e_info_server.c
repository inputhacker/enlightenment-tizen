#include "e.h"
#include "e_info_server.h"
#include <tbm_bufmgr.h>
#include <tbm_surface.h>
#include <tbm_surface_internal.h>
#include <tdm_helper.h>
#include <wayland-tbm-server.h>
#include "e_comp_wl.h"
#include "e_info_protocol.h"
#include <dlfcn.h>
#include "e_comp_object.h"

#define EDJE_EDIT_IS_UNSTABLE_AND_I_KNOW_ABOUT_IT
#include <Edje_Edit.h>

#define USE_WAYLAND_LOG_TRACE

void wl_map_for_each(struct wl_map *map, void *func, void *data);

#define BUS "org.enlightenment.wm"
#define PATH "/org/enlightenment/wm"
#define IFACE "org.enlightenment.wm.info"

#define ERR_BASE "org.enlightenment.wm.Error."
#define INVALID_ARGS         ERR_BASE"InvalidArguments"
#define GET_CALL_MSG_ARG_ERR ERR_BASE"GetCallMsgArgFailed"
#define WIN_NOT_EXIST        ERR_BASE"WindowNotExist"
#define INVALID_PROPERTY_NAME        ERR_BASE"InvalidPropertyName"
#define FAIL_TO_SET_PROPERTY         ERR_BASE"FailedToSetProperty"
#define FAIL_TO_GET_PROPERTY         ERR_BASE"FailedToGetProperty"

E_API int E_EVENT_INFO_ROTATION_MESSAGE = -1;

typedef struct _E_Info_Server
{
   Eldbus_Connection *edbus_conn;
   Eldbus_Connection_Type edbus_conn_type;
   Eldbus_Service_Interface *iface;
   Ecore_Event_Handler *dbus_init_done_handler;
} E_Info_Server;

typedef struct _E_Info_Transform
{
   E_Client         *ec;
   E_Util_Transform *transform;
   int               id;
   int               enable;
   int               background;
} E_Info_Transform;

static E_Info_Server e_info_server;
static Eina_List    *e_info_transform_list = NULL;

static Eina_List    *e_info_dump_hdlrs;
static char         *e_info_dump_path;
static int           e_info_dump_running;
static int           e_info_dump_count;
static int           e_info_dump_mark;
static int           e_info_dump_mark_count;
static int           e_info_dump_remote_surface = 0;

//FILE pointer for protocol_trace
static FILE *log_fp_ptrace = NULL;

//wayland protocol logger
static struct wl_protocol_logger *e_info_protocol_logger;

// Module list for module info
static Eina_List *module_hook = NULL;

#define BUF_SNPRINTF(fmt, ARG...) do { \
   str_l = snprintf(str_buff, str_r, fmt, ##ARG); \
   str_buff += str_l; \
   str_r -= str_l; \
} while(0)

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

enum
{
   E_INFO_SERVER_SIGNAL_WIN_UNDER_TOUCH = 0
};

static E_Info_Transform *_e_info_transform_new(E_Client *ec, int id, int enable, int x, int y, int sx, int sy, int degree, int background);
static E_Info_Transform *_e_info_transform_find(E_Client *ec, int id);
static void              _e_info_transform_set(E_Info_Transform *transform, int enable, int x, int y, int sx, int sy, int degree);
static void              _e_info_transform_del(E_Info_Transform *transform);
static void              _e_info_transform_del_with_id(E_Client *ec, int id);

static int _e_info_server_hooks_delete = 0;
static int _e_info_server_hooks_walking = 0;

static Eina_Inlist *_e_info_server_hooks[] =
{
    [E_INFO_SERVER_HOOK_BUFFER_DUMP_BEGIN] = NULL,
    [E_INFO_SERVER_HOOK_BUFFER_DUMP_END] = NULL
};

static void
_e_info_server_hooks_clean(void)
{
   Eina_Inlist *l;
   E_Info_Server_Hook *iswh;
   unsigned int x;

   for (x = 0; x < E_INFO_SERVER_HOOK_LAST; x++)
     EINA_INLIST_FOREACH_SAFE(_e_info_server_hooks[x], l, iswh)
       {
          if (!iswh->delete_me) continue;
          _e_info_server_hooks[x] = eina_inlist_remove(_e_info_server_hooks[x],
                                                EINA_INLIST_GET(iswh));
          free(iswh);
       }
}

static void
_e_info_server_hook_call(E_Info_Server_Hook_Point hookpoint, void *data EINA_UNUSED)
{
   E_Info_Server_Hook *iswh;

   _e_info_server_hooks_walking++;
   EINA_INLIST_FOREACH(_e_info_server_hooks[hookpoint], iswh)
     {
        if (iswh->delete_me) continue;
        iswh->func(iswh->data);
     }
   _e_info_server_hooks_walking--;
   if ((_e_info_server_hooks_walking == 0) && (_e_info_server_hooks_delete > 0))
     _e_info_server_hooks_clean();
}

E_API E_Info_Server_Hook *
e_info_server_hook_add(E_Info_Server_Hook_Point hookpoint, E_Info_Server_Hook_Cb func, const void *data)
{
   E_Info_Server_Hook *iswh;

   EINA_SAFETY_ON_TRUE_RETURN_VAL(hookpoint >= E_INFO_SERVER_HOOK_LAST, NULL);
   iswh = E_NEW(E_Info_Server_Hook, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(iswh, NULL);
   iswh->hookpoint = hookpoint;
   iswh->func = func;
   iswh->data = (void*)data;
   _e_info_server_hooks[hookpoint] = eina_inlist_append(_e_info_server_hooks[hookpoint],
                                                 EINA_INLIST_GET(iswh));
   return iswh;
}

E_API void
e_info_server_hook_del(E_Info_Server_Hook *iswh)
{
   iswh->delete_me = 1;
   if (_e_info_server_hooks_walking == 0)
     {
        _e_info_server_hooks[iswh->hookpoint] = eina_inlist_remove(_e_info_server_hooks[iswh->hookpoint],
                                                          EINA_INLIST_GET(iswh));
        free(iswh);
     }
   else
     _e_info_server_hooks_delete++;
}

E_API void
e_info_server_hook_call(E_Info_Server_Hook_Point hookpoint)
{
   if (hookpoint >= E_INFO_SERVER_HOOK_LAST) return;

   _e_info_server_hook_call(hookpoint, NULL);
}

static void
_e_info_server_ec_hwc_info_get(E_Client *ec, int *hwc, int *pl_zpos)
{
   Eina_List *l;
   E_Output *eout;
   E_Plane *ep;
   E_Hwc_Window *hwc_window = NULL;

   *hwc = -1;
   *pl_zpos = -999;

   if ((!e_comp->hwc) || e_comp_hwc_deactive_get())
     return;

   *hwc = 0;

   eout = e_output_find(ec->zone->output_id);
   if (!eout) return;

   if (e_hwc_policy_get(eout->hwc) == E_HWC_POLICY_PLANES)
     {
        EINA_LIST_FOREACH(eout->planes, l, ep)
          {
             if (e_plane_is_fb_target(ep))
               *pl_zpos = ep->zpos;

             if (ep->ec == ec)
               {
                  *hwc = 1;
                  *pl_zpos = ep->zpos;
                  break;
               }
          }
     }
   else
     {
        if (!ec->hwc_window) return;
        hwc_window = ec->hwc_window;
        if (e_hwc_window_is_on_hw_overlay(hwc_window))
          *hwc = 1;

        *pl_zpos = e_hwc_window_zpos_get(hwc_window);
     }
}

static void
_msg_ecs_append(Eldbus_Message_Iter *iter, Eina_Bool is_visible)
{
   Eldbus_Message_Iter *array_of_ec;
   E_Client *ec;

   eldbus_message_iter_arguments_append(iter, "a("VALUE_TYPE_FOR_TOPVWINS")", &array_of_ec);

   // append clients.
   E_CLIENT_REVERSE_FOREACH(ec)
     {
        Eldbus_Message_Iter* struct_of_ec;
        Ecore_Window win;
        Ecore_Window pwin;
        uint32_t res_id = 0;
        pid_t pid = -1;
        char layer_name[32];
        int hwc = -1, pl_zpos = -999;
        int iconified = 0;
        Eina_Bool has_input_region = EINA_FALSE;
        Eina_List *list_input_region = NULL;
        Eina_Bool mapped = EINA_FALSE;

        if (is_visible && e_client_util_ignored_get(ec)) continue;

        win = e_client_util_win_get(ec);
        e_comp_layer_name_get(ec->layer, layer_name, sizeof(layer_name));

        pwin = e_client_util_win_get(ec->parent);

        if (ec->pixmap)
          res_id = e_pixmap_res_id_get(ec->pixmap);

        pid = ec->netwm.pid;
        if (pid <= 0)
          {
             if (ec->comp_data)
               {
                  E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)ec->comp_data;
                  if (cdata->surface)
                    wl_client_get_credentials(wl_resource_get_client(cdata->surface), &pid, NULL, NULL);
               }
          }

        if (ec->iconic)
          {
             if (ec->exp_iconify.by_client)
               iconified = 2;
             else
               iconified = 1;
          }
        else
          iconified = 0;

        if (ec->comp_data)
          mapped = ec->comp_data->mapped;

        _e_info_server_ec_hwc_info_get(ec, &hwc, &pl_zpos);

        e_comp_object_input_rect_get(ec->frame, &list_input_region);
        if (list_input_region)
          {
             has_input_region = EINA_TRUE;
             list_input_region = eina_list_free(list_input_region);
          }

        eldbus_message_iter_arguments_append(array_of_ec, "("VALUE_TYPE_FOR_TOPVWINS")", &struct_of_ec);

        eldbus_message_iter_arguments_append
           (struct_of_ec, VALUE_TYPE_FOR_TOPVWINS,
            win,
            res_id,
            pid,
            e_client_util_name_get(ec) ?: "NO NAME",
            ec->x, ec->y, ec->w, ec->h, ec->layer,
            ec->visible, mapped, ec->argb, ec->visibility.opaque, ec->visibility.obscured, iconified,
            evas_object_visible_get(ec->frame), ec->focused, hwc, pl_zpos, pwin, layer_name, has_input_region);

        eldbus_message_iter_container_close(array_of_ec, struct_of_ec);
     }

   eldbus_message_iter_container_close(iter, array_of_ec);
}

static void
_msg_clients_append(Eldbus_Message_Iter *iter, Eina_Bool is_visible)
{
   Eldbus_Message_Iter *array_of_ec;
   E_Client *ec;
   Evas_Object *o;

   eldbus_message_iter_arguments_append(iter, "a("VALUE_TYPE_FOR_TOPVWINS")", &array_of_ec);

   // append clients.
   for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
     {
        Eldbus_Message_Iter* struct_of_ec;
        Ecore_Window win;
        Ecore_Window pwin;
        uint32_t res_id = 0;
        pid_t pid = -1;
        char layer_name[32];
        int hwc = -1, pl_zpos = -999;
        int iconified = 0;
        Eina_Bool has_input_region = EINA_FALSE;
        Eina_List *list_input_region = NULL;
        Eina_Bool mapped = EINA_FALSE;

        ec = evas_object_data_get(o, "E_Client");
        if (!ec) continue;
        if (is_visible && e_client_util_ignored_get(ec)) continue;

        win = e_client_util_win_get(ec);
        e_comp_layer_name_get(ec->layer, layer_name, sizeof(layer_name));

        pwin = e_client_util_win_get(ec->parent);

        if (ec->pixmap)
          res_id = e_pixmap_res_id_get(ec->pixmap);

        pid = ec->netwm.pid;
        if (pid <= 0)
          {
             if (ec->comp_data)
               {
                  E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)ec->comp_data;
                  if (cdata->surface)
                    wl_client_get_credentials(wl_resource_get_client(cdata->surface), &pid, NULL, NULL);
               }
          }

        if (ec->iconic)
          {
             if (ec->exp_iconify.by_client)
               iconified = 2;
             else
               iconified = 1;
          }
        else
          iconified = 0;

        if (ec->comp_data)
          mapped = ec->comp_data->mapped;

        _e_info_server_ec_hwc_info_get(ec, &hwc, &pl_zpos);

        e_comp_object_input_rect_get(o, &list_input_region);
        if (list_input_region)
          {
             has_input_region = EINA_TRUE;
             list_input_region = eina_list_free(list_input_region);
          }

        eldbus_message_iter_arguments_append(array_of_ec, "("VALUE_TYPE_FOR_TOPVWINS")", &struct_of_ec);

        eldbus_message_iter_arguments_append
           (struct_of_ec, VALUE_TYPE_FOR_TOPVWINS,
            win,
            res_id,
            pid,
            e_client_util_name_get(ec) ?: "NO NAME",
            ec->x, ec->y, ec->w, ec->h, ec->layer,
            ec->visible, mapped, ec->argb, ec->visibility.opaque, ec->visibility.obscured, iconified,
            evas_object_visible_get(ec->frame), ec->focused, hwc, pl_zpos, pwin, layer_name, has_input_region);

        eldbus_message_iter_container_close(array_of_ec, struct_of_ec);
     }

   eldbus_message_iter_container_close(iter, array_of_ec);
}

static int
_e_info_server_is_hwc_windows()
{
   E_Output *primary_output;

   primary_output = e_comp_screen_primary_output_get(e_comp->e_comp_screen);
   if (!primary_output)
      return 0;

   if (e_hwc_policy_get(primary_output->hwc) == E_HWC_POLICY_WINDOWS)
     return 1;

   return 0;
}

/* Method Handlers */
static Eldbus_Message *
_e_info_server_cb_window_info_get(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   Eldbus_Message_Iter *iter = eldbus_message_iter_get(reply);

   eldbus_message_iter_basic_append(iter, 'i', e_comp_config_get()->engine);
   eldbus_message_iter_basic_append(iter, 'i', e_comp_config_get()->hwc);
   eldbus_message_iter_basic_append(iter, 'i', e_comp_config_get()->hwc_use_multi_plane);
   eldbus_message_iter_basic_append(iter, 'i', e_comp->hwc);
   eldbus_message_iter_basic_append(iter, 'i', _e_info_server_is_hwc_windows());
   eldbus_message_iter_basic_append(iter, 's', ecore_evas_engine_name_get(e_comp->ee));
   eldbus_message_iter_basic_append(iter, 'i', e_config->use_buffer_flush);
   eldbus_message_iter_basic_append(iter, 'i', e_config->deiconify_approve);

   _msg_clients_append(iter, EINA_TRUE);

   return reply;
}

/* Method Handlers */
static Eldbus_Message *
_e_info_server_cb_ec_info_get(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   Eldbus_Message_Iter *iter = eldbus_message_iter_get(reply);

   eldbus_message_iter_basic_append(iter, 'i', e_comp_config_get()->engine);
   eldbus_message_iter_basic_append(iter, 'i', e_comp_config_get()->hwc);
   eldbus_message_iter_basic_append(iter, 'i', e_comp_config_get()->hwc_use_multi_plane);
   eldbus_message_iter_basic_append(iter, 'i', e_comp->hwc);
   eldbus_message_iter_basic_append(iter, 'i', _e_info_server_is_hwc_windows());
   eldbus_message_iter_basic_append(iter, 's', ecore_evas_engine_name_get(e_comp->ee));
   eldbus_message_iter_basic_append(iter, 'i', e_config->use_buffer_flush);
   eldbus_message_iter_basic_append(iter, 'i', e_config->deiconify_approve);

   _msg_ecs_append(iter, EINA_TRUE);

   return reply;
}

/* Method Handlers */
static Eldbus_Message *
_e_info_server_cb_all_window_info_get(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);

   _msg_clients_append(eldbus_message_iter_get(reply), EINA_FALSE);

   return reply;
}

typedef struct _Obj_Info
{
   Evas_Object *po; /* parent object */
   Evas_Object *o;
   int          depth;
} Obj_Info;

static Obj_Info *
_obj_info_get(Evas_Object *po, Evas_Object *o, int depth)
{
   Obj_Info *info = E_NEW(Obj_Info, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(info, NULL);

   info->po = po;
   info->o = o;
   info->depth = depth;
   return info;
}

static E_Info_Comp_Obj *
_compobj_info_get(Evas_Object *po, Evas_Object *o, int depth)
{
   E_Info_Comp_Obj *cobj;
   const char *name = NULL, *type = NULL;
   const char *file = NULL, *group = NULL, *part = NULL, *key = NULL;
   E_Client *ec;
   char buf[PATH_MAX];
   double val = 0.0f;
   Evas_Object *edit_obj = NULL, *c = NULL;
   Eina_List *parts = NULL, *ll;
   Evas_Native_Surface *ns;

   cobj = E_NEW(E_Info_Comp_Obj, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(cobj, NULL);

   cobj->obj = (uintptr_t)o;
   cobj->depth = depth;

   type = evas_object_type_get(o);
   if      (!e_util_strcmp(type, "rectangle"    )) cobj->type = eina_stringshare_add("r");
   else if (!e_util_strcmp(type, "edje"         )) cobj->type = eina_stringshare_add("EDJ");
   else if (!e_util_strcmp(type, "image"        )) cobj->type = eina_stringshare_add("IMG");
   else if (!e_util_strcmp(type, "e_comp_object")) cobj->type = eina_stringshare_add("EC");
   else                                            cobj->type = eina_stringshare_add(type);

   cobj->name = eina_stringshare_add("no_use");
   name = evas_object_name_get(o);
   if (name)
     {
        eina_stringshare_del(cobj->name);
        cobj->name = eina_stringshare_add(name);
     }

   evas_object_geometry_get(o, &cobj->x, &cobj->y, &cobj->w, &cobj->h);
   evas_object_color_get(o, &cobj->r, &cobj->g, &cobj->b, &cobj->a);
   cobj->pass_events = evas_object_pass_events_get(o);
   cobj->freeze_events = evas_object_freeze_events_get(o);
   cobj->focus = evas_object_focus_get(o);
   cobj->vis = evas_object_visible_get(o);
   cobj->ly = evas_object_layer_get(o);

#define _CLAMP(x) if ((x >= 0) && (x > 9999)) x = 9999; else if (x < -999) x = -999;
   _CLAMP(cobj->ly);
   _CLAMP(cobj->x);
   _CLAMP(cobj->y);
   _CLAMP(cobj->w);
   _CLAMP(cobj->h);
#undef _CLAMP

   switch (evas_object_render_op_get(o))
     {
      case EVAS_RENDER_BLEND:    cobj->opmode = eina_stringshare_add("BL" ); break;
      case EVAS_RENDER_COPY:     cobj->opmode = eina_stringshare_add("CP" ); break;
      case EVAS_RENDER_COPY_REL: cobj->opmode = eina_stringshare_add("CPR"); break;
      case EVAS_RENDER_ADD:      cobj->opmode = eina_stringshare_add("AD" ); break;
      case EVAS_RENDER_ADD_REL:  cobj->opmode = eina_stringshare_add("ADR"); break;
      case EVAS_RENDER_SUB:      cobj->opmode = eina_stringshare_add("SB" ); break;
      case EVAS_RENDER_SUB_REL:  cobj->opmode = eina_stringshare_add("SBR"); break;
      case EVAS_RENDER_TINT:     cobj->opmode = eina_stringshare_add("TT" ); break;
      case EVAS_RENDER_TINT_REL: cobj->opmode = eina_stringshare_add("TTR"); break;
      case EVAS_RENDER_MASK:     cobj->opmode = eina_stringshare_add("MSK"); break;
      case EVAS_RENDER_MUL:      cobj->opmode = eina_stringshare_add("MUL"); break;
      default:                   cobj->opmode = eina_stringshare_add("NO" ); break;
     }

   if ((!e_util_strcmp(cobj->name, "no_use")) &&
       (!e_util_strcmp(cobj->type, "EC")))
     {
        ec = evas_object_data_get(o, "E_Client");
        if (ec)
          {
             /* append window id, client pid and window title */
             eina_stringshare_del(cobj->name);

             snprintf(buf, sizeof(buf), "%zx %d %s",
                      e_client_util_win_get(ec),
                      ec->netwm.pid,
                      e_client_util_name_get(ec));

             cobj->name = eina_stringshare_add(buf);
          }
     }

   /* get edje file path and group name if it is a edje object */
   cobj->edje.file = eina_stringshare_add("no_use");
   cobj->edje.group = eina_stringshare_add("no_use");
   if (!e_util_strcmp(cobj->type, "EDJ"))
     {
        edje_object_file_get(o, &file, &group);

        if (file)
          {
             eina_stringshare_del(cobj->edje.file);
             cobj->edje.file = eina_stringshare_add(file);
          }

        if (group)
          {
             eina_stringshare_del(cobj->edje.group);
             cobj->edje.group = eina_stringshare_add(group);
          }
     }

   /* get part name and part value if it is a member of parent edje object */
   cobj->edje.part = eina_stringshare_add("no_use");
   if (po)
     {
        type = evas_object_type_get(po);
        if (!e_util_strcmp(type, "edje"))
          {
             edje_object_file_get(po, &file, &group);
             edit_obj = edje_edit_object_add(e_comp->evas);
             if (edje_object_file_set(edit_obj, file, group))
               {
                  parts = edje_edit_parts_list_get(edit_obj);
                  EINA_LIST_FOREACH(parts, ll, part)
                    {
                       c = (Evas_Object *)edje_object_part_object_get(po, part);
                       if (c == o)
                         {
                            edje_object_part_state_get(po, part, &val);

                            eina_stringshare_del(cobj->edje.part);
                            cobj->edje.part = eina_stringshare_add(part);
                            cobj->edje.val = val;
                            break;
                         }
                    }
                  edje_edit_string_list_free(parts);
               }
             evas_object_del(edit_obj);
          }
     }

   /* get image object information */
   cobj->img.native_type = eina_stringshare_add("no_use");
   cobj->img.file = eina_stringshare_add("no_use");
   cobj->img.key = eina_stringshare_add("no_use");

   if (!e_util_strcmp(cobj->type, "IMG"))
     {
        ns = evas_object_image_native_surface_get(o);
        if (ns)
          {
             cobj->img.native = EINA_TRUE;
             eina_stringshare_del(cobj->img.native_type);
             switch (ns->type)
               {
                case EVAS_NATIVE_SURFACE_WL:
                   cobj->img.native_type = eina_stringshare_add("WL");
                   cobj->img.data = (uintptr_t)ns->data.wl.legacy_buffer;
                   break;
                case EVAS_NATIVE_SURFACE_TBM:
                   cobj->img.native_type = eina_stringshare_add("TBM");
                   cobj->img.data = (uintptr_t)ns->data.tbm.buffer;
                   break;
                default:
                   cobj->img.native_type = eina_stringshare_add("?");
                   cobj->img.data = 0;
                   break;
               }
          }
        else
          {
             evas_object_image_file_get(o, &file, &key);

             if (file)
               {
                  eina_stringshare_del(cobj->img.file);
                  cobj->img.file = eina_stringshare_add(file);
               }

             if (key)
               {
                  eina_stringshare_del(cobj->img.key);
                  cobj->img.key = eina_stringshare_add(key);
               }

             cobj->img.data = (uintptr_t)evas_object_image_data_get(o, 0);
          }

        evas_object_image_size_get(o, &cobj->img.w, &cobj->img.h);
        evas_object_image_load_size_get(o, &cobj->img.lw, &cobj->img.lh);
        evas_object_image_fill_get(o, &cobj->img.fx, &cobj->img.fy, &cobj->img.fw, &cobj->img.fh);
        cobj->img.alpha = evas_object_image_alpha_get(o);
        cobj->img.dirty = evas_object_image_pixels_dirty_get(o);
     }

   if (evas_object_map_enable_get(o))
     {
        const Evas_Map *m = evas_object_map_get(o);
        if (m)
          {
             int i;
             cobj->map.enable = EINA_TRUE;
             cobj->map.alpha = evas_map_alpha_get(m);
             for (i = 0; i < 4; i++)
               {
                  Evas_Coord x, y, z;
                  evas_map_point_image_uv_get(m, i, &cobj->map.u[i], &cobj->map.v[i]);
                  evas_map_point_coord_get(m, i, &x, &y, &z);
                  cobj->map.x[i] = x;
                  cobj->map.y[i] = y;
                  cobj->map.z[i] = z;
               }
          }
     }

   return cobj;
}

static Eldbus_Message *
_e_info_server_cb_compobjs(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   Eldbus_Message_Iter *iter = eldbus_message_iter_get(reply);
   Eldbus_Message_Iter *cobjs;
   Evas_Object *o, *c;
   Obj_Info *info, *info2;
   E_Info_Comp_Obj *cobj;
   Eina_List *stack = NULL; /* stack for DFS */
   Eina_List *queue = NULL; /* result queue */
   Eina_List *ll = NULL;

   eldbus_message_iter_arguments_append(iter,
                                        "a("SIGNATURE_COMPOBJS_CLIENT")",
                                        &cobjs);

   /* 1. push: top-level evas objects */
   for (o = evas_object_bottom_get(e_comp->evas); o; o = evas_object_above_get(o))
     {
        info = _obj_info_get(NULL, o, 0);
        if (!info) continue;
        stack = eina_list_append(stack, info);
     }

   while (1)
     {
        /* 2. pop */
        info = eina_list_last_data_get(stack);
        if (!info) break;

        /* store data */
        cobj = _compobj_info_get(info->po, info->o, info->depth);
        if (!cobj) continue;
        queue = eina_list_append(queue, cobj);

        /* 3. push : child objects */
        if (evas_object_smart_data_get(info->o))
          {
             EINA_LIST_FOREACH(evas_object_smart_members_get(info->o), ll, c)
               {
                  info2 = _obj_info_get(info->o, c, info->depth + 1);
                  stack = eina_list_append(stack, info2);
               }
          }

        stack = eina_list_remove(stack, info);
        E_FREE(info);
     }

   /* send result */
   EINA_LIST_FREE(queue, cobj)
     {
        Eldbus_Message_Iter *struct_of_cobj;
        eldbus_message_iter_arguments_append(cobjs,
                                             "("SIGNATURE_COMPOBJS_CLIENT")",
                                             &struct_of_cobj);

        eldbus_message_iter_arguments_append(struct_of_cobj,
                                             SIGNATURE_COMPOBJS_CLIENT,
                                             cobj->obj,
                                             cobj->depth,
                                             cobj->type,
                                             cobj->name,
                                             cobj->ly,
                                             cobj->opmode,
                                             cobj->x, cobj->y, cobj->w, cobj->h,
                                             cobj->r, cobj->g, cobj->b, cobj->a,
                                             cobj->pass_events,
                                             cobj->freeze_events,
                                             cobj->focus,
                                             cobj->vis,
                                             cobj->edje.file,
                                             cobj->edje.group,
                                             cobj->edje.part,
                                             cobj->edje.val,
                                             cobj->img.native,
                                             cobj->img.native_type,
                                             cobj->img.file,
                                             cobj->img.key,
                                             cobj->img.data,
                                             cobj->img.w, cobj->img.h,
                                             cobj->img.lw, cobj->img.lh,
                                             cobj->img.fx, cobj->img.fy, cobj->img.fw, cobj->img.fh,
                                             cobj->img.alpha,
                                             cobj->img.dirty,
                                             cobj->map.enable,
                                             cobj->map.alpha,
                                             cobj->map.u[0], cobj->map.u[1], cobj->map.u[2], cobj->map.u[3],
                                             cobj->map.v[0], cobj->map.v[1], cobj->map.v[2], cobj->map.v[3],
                                             cobj->map.x[0], cobj->map.x[1], cobj->map.x[2], cobj->map.x[3],
                                             cobj->map.y[0], cobj->map.y[1], cobj->map.y[2], cobj->map.y[3],
                                             cobj->map.z[0], cobj->map.z[1], cobj->map.z[2], cobj->map.z[3]);

        eldbus_message_iter_container_close(cobjs, struct_of_cobj);

        eina_stringshare_del(cobj->type);
        eina_stringshare_del(cobj->name);
        eina_stringshare_del(cobj->opmode);
        eina_stringshare_del(cobj->edje.file);
        eina_stringshare_del(cobj->edje.group);
        eina_stringshare_del(cobj->edje.part);
        eina_stringshare_del(cobj->img.native_type);
        eina_stringshare_del(cobj->img.file);
        eina_stringshare_del(cobj->img.key);
        E_FREE(cobj);
     }

   eldbus_message_iter_container_close(iter, cobjs);
   return reply;
}

static void
_input_msg_clients_append(Eldbus_Message_Iter *iter)
{
   Eldbus_Message_Iter *array_of_input;
   Eina_List *l;
   E_Devicemgr_Input_Device *dev;

   eldbus_message_iter_arguments_append(iter, "a("VALUE_TYPE_FOR_INPUTDEV")", &array_of_input);

   EINA_LIST_FOREACH(e_devicemgr->device_list, l, dev)
     {
        Eldbus_Message_Iter *struct_of_input;

        eldbus_message_iter_arguments_append(array_of_input, "("VALUE_TYPE_FOR_INPUTDEV")", &struct_of_input);

        eldbus_message_iter_arguments_append
                     (struct_of_input, VALUE_TYPE_FOR_INPUTDEV,
                      dev->name, dev->identifier, dev->clas);

        eldbus_message_iter_container_close(array_of_input, struct_of_input);
     }
   eldbus_message_iter_container_close(iter, array_of_input);
}


static Eldbus_Message *
_e_info_server_cb_input_device_info_get(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);

   _input_msg_clients_append(eldbus_message_iter_get(reply));

   return reply;
}

static void
_msg_connected_clients_append(Eldbus_Message_Iter *iter)
{
   Eldbus_Message_Iter *array_of_ec;
   E_Client *ec;
   Evas_Object *o;

   eldbus_message_iter_arguments_append(iter, "a(ss)", &array_of_ec);

   Eina_List *l;
   E_Comp_Connected_Client_Info *cinfo;


   Eldbus_Message_Iter* struct_of_ec;

#define __CONNECTED_CLIENTS_ARG_APPEND_TYPE(title, str, x...) ({                           \
                                                               char __temp[128] = {0,};                                                     \
                                                               snprintf(__temp, sizeof(__temp), str, ##x);                                  \
                                                               eldbus_message_iter_arguments_append(array_of_ec, "(ss)", &struct_of_ec);    \
                                                               eldbus_message_iter_arguments_append(struct_of_ec, "ss", (title), (__temp)); \
                                                               eldbus_message_iter_container_close(array_of_ec, struct_of_ec);})

   EINA_LIST_FOREACH(e_comp->connected_clients, l, cinfo)
     {
        __CONNECTED_CLIENTS_ARG_APPEND_TYPE("[Connected Clients]", "name:%20s pid:%3d uid:%3d gid:%3d", cinfo->name ?: "NO_NAME", cinfo->pid, cinfo->uid, cinfo->gid);
        for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
          {
             Ecore_Window win;
             uint32_t res_id = 0;
             pid_t pid = -1;

             ec = evas_object_data_get(o, "E_Client");
             if (!ec) continue;
             if (e_client_util_ignored_get(ec)) continue;

             win = e_client_util_win_get(ec);

             if (ec->pixmap)
               res_id = e_pixmap_res_id_get(ec->pixmap);
             if (ec->comp_data)
               {
                  E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)ec->comp_data;
                  if (cdata->surface)
                    wl_client_get_credentials(wl_resource_get_client(cdata->surface), &pid, NULL, NULL);
               }
             if (cinfo->pid == pid)
               {
                  __CONNECTED_CLIENTS_ARG_APPEND_TYPE("[E_Client Info]", "win:0x%08zx res_id:%5d, name:%20s, geo:(%4d, %4d, %4dx%4d), layer:%5d, visible:%d, argb:%d",
                                                      win, res_id, e_client_util_name_get(ec) ?: "NO_NAME", ec->x, ec->y, ec->w, ec->h, ec->layer, ec->visible, ec->argb);
               }
          }
     }

   eldbus_message_iter_container_close(iter, array_of_ec);
}

static Eldbus_Message *
_e_info_server_cb_connected_clients_get(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);

   _msg_connected_clients_append(eldbus_message_iter_get(reply));

   return reply;
}

#ifndef wl_client_for_each
#define wl_client_for_each(client, list)     \
   for (client = 0, client = wl_client_from_link((list)->next);   \
        wl_client_get_link(client) != (list);                     \
        client = wl_client_from_link(wl_client_get_link(client)->next))
#endif

static int resurceCnt = 0;

static enum wl_iterator_result
_e_info_server_get_resource(struct wl_resource *resource, void *data)
{
   Eldbus_Message_Iter* array_of_res= data;
   Eldbus_Message_Iter* struct_of_res;

   eldbus_message_iter_arguments_append(array_of_res, "("VALUE_TYPE_REPLY_RESLIST")", &struct_of_res);
   eldbus_message_iter_arguments_append(struct_of_res, VALUE_TYPE_REPLY_RESLIST, "[resource]", wl_resource_get_class(resource), wl_resource_get_id(resource));
   eldbus_message_iter_container_close(array_of_res, struct_of_res);
   resurceCnt++;

   return WL_ITERATOR_CONTINUE;
}

static void
_msg_clients_res_list_append(Eldbus_Message_Iter *iter, uint32_t mode, int id)
{
   Eldbus_Message_Iter *array_of_res;

   struct wl_list * client_list;
   struct wl_client *client;
   //E_Comp_Data *cdata;
   E_Comp_Wl_Data *cdata;
   int pid = -1;

   enum {
   DEFAULT_SUMMARY,
   TREE,
   PID} type = mode;

   eldbus_message_iter_arguments_append(iter, "a("VALUE_TYPE_REPLY_RESLIST")", &array_of_res);

   if (!e_comp) return;
   if (!(cdata = e_comp->wl_comp_data)) return;
   if (!cdata->wl.disp) return;

   client_list = wl_display_get_client_list(cdata->wl.disp);

   wl_client_for_each(client, client_list)
     {
        Eldbus_Message_Iter* struct_of_res;

        wl_client_get_credentials(client, &pid, NULL, NULL);

        if ((type == PID) && (pid != id)) continue;

        eldbus_message_iter_arguments_append(array_of_res, "("VALUE_TYPE_REPLY_RESLIST")", &struct_of_res);

        eldbus_message_iter_arguments_append(struct_of_res, VALUE_TYPE_REPLY_RESLIST, "[client]", "pid", pid);
        eldbus_message_iter_container_close(array_of_res, struct_of_res);

        resurceCnt = 0;
        wl_client_for_each_resource(client, _e_info_server_get_resource, array_of_res);

        eldbus_message_iter_arguments_append(array_of_res, "("VALUE_TYPE_REPLY_RESLIST")", &struct_of_res);
        eldbus_message_iter_arguments_append(struct_of_res, VALUE_TYPE_REPLY_RESLIST, "[count]", "resurceCnt", resurceCnt);
        eldbus_message_iter_container_close(array_of_res, struct_of_res);
     }
   eldbus_message_iter_container_close(iter, array_of_res);
}

static Eldbus_Message *
_e_info_server_cb_res_lists_get(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   uint32_t mode = 0;
   int pid = -1;

   if (!eldbus_message_arguments_get(msg, VALUE_TYPE_REQUEST_RESLIST, &mode, &pid))
     {
        ERR("Error getting arguments.");
        return reply;
     }

   _msg_clients_res_list_append(eldbus_message_iter_get(reply), mode, pid);

   return reply;
}

/*
 * behaves like strcat but also dynamically extends buffer when it's needed
 *
 * dst - the pointer to result string (dynamically allocated) will be stored to memory 'dst' points to
 *       '*dst' MUST either point nothing (nullptr) or point a !_dynamically allocated_! null-terminated string
 *       (e.g. returned by the previous call)
 * src - null-terminated string to concatenate (can be either statically or dynamically allocated string)
 *
 * *dst got to be freed by free() when it's no longer needed
 *
 * return -1 in case of an error, 0 otherwise
 */
static int
_astrcat(char **dst, const char *src)
{
   int new_size;
   char *res;

   if (!dst || !src)
     return -1;

   if (*dst)
     new_size = strlen(*dst) + strlen(src) + 1; /* + '/0' */
   else
     new_size = strlen(src) + 1; /* + '/0' */

   /* if *dst is nullptr realloc behaves like malloc */
   res = realloc(*dst, new_size);
   if (!res)
     return -1;

   /* if we were asked to concatenate to null string */
   if (!*dst)
     res[0] = '\0'; /* strncat looks for null-terminated string */

   *dst = res;
   strncat(*dst, src, new_size - strlen(*dst) - 1);

   return 0;
}

#define astrcat_(str, mod, x...) ({                                  \
                                  char *temp = NULL;                 \
                                  if (asprintf(&temp, mod, ##x) < 0) \
                                    goto fail;                      \
                                  if (_astrcat(str, temp) < 0)       \
                                    {                                \
                                       free(temp);                   \
                                       goto fail;                    \
                                    }                                \
                                  free(temp); })


static const char *
_get_win_prop_Input_region(const Evas_Object *evas_obj)
{
   Eina_List *list = NULL, *l;
   Eina_Rectangle *data;
   char *str = NULL;

   e_comp_object_input_rect_get((Evas_Object *)evas_obj, &list);
   if (!list)
     {
        astrcat_(&str, "No Input Region\n");
        return str;
     }

   EINA_LIST_FOREACH(list, l, data)
     {
        astrcat_(&str, "[(%d, %d) %dx%d]\n", data->x, data->y, data->w, data->h);
     }
   list = eina_list_free(list);

   return str;
fail:
   if (str) free(str);
   if (list) list = eina_list_free(list);
   return NULL;
}



static const char*
_get_win_prop_Rotation(const Evas_Object *evas_obj)
{
   const E_Client *ec;
   char *str = NULL;

   int i, count;

   ec = evas_object_data_get(evas_obj, "E_Client");
   count = ec->e.state.rot.count;

   astrcat_(&str, "Support(%d) Type(%s)\n", ec->e.state.rot.support,
           ec->e.state.rot.type == E_CLIENT_ROTATION_TYPE_NORMAL ? "normal" : "dependent");

   if (ec->e.state.rot.available_rots && count)
     {
        astrcat_(&str, "Availables[%d] ", count);

        for (i = 0; i < count; i++)
          astrcat_(&str, "%d ", ec->e.state.rot.available_rots[i]);
     }
   else
     astrcat_(&str, "Availables[%d] N/A", count);


   astrcat_(&str, "\nAngle prev(%d) curr(%d) next(%d) reserve(%d) preferred(%d)\n",
           ec->e.state.rot.ang.prev,
           ec->e.state.rot.ang.curr,
           ec->e.state.rot.ang.next,
           ec->e.state.rot.ang.reserve,
           ec->e.state.rot.preferred_rot);

   astrcat_(&str, "pending_change_request(%d) pending_show(%d) nopending_render(%d) wait_for_done(%d)\n",
           ec->e.state.rot.pending_change_request,
           ec->e.state.rot.pending_show,
           ec->e.state.rot.nopending_render,
           ec->e.state.rot.wait_for_done);

   if (ec->e.state.rot.geom_hint)
     for (i = 0; i < 4; i++)
       astrcat_(&str, "Geometry hint[%d] %d,%d   %dx%d\n",
               i,
               ec->e.state.rot.geom[i].x,
               ec->e.state.rot.geom[i].y,
               ec->e.state.rot.geom[i].w,
               ec->e.state.rot.geom[i].h);

   return str;

fail:
   free(str);
   return NULL;
}

static const char*
_get_win_prop_Transform(const Evas_Object *evas_obj)
{
   const E_Client *ec;
   char *str = NULL;

   int i, count;

   ec = evas_object_data_get(evas_obj, "E_Client");
   count = e_client_transform_core_transform_count_get((E_Client *)ec);

   astrcat_(&str, "transform count: %d\n", count);

   if (count <= 0)
     return str;

   astrcat_(&str, "[id] [move] [scale] [rotation] [keep_ratio] [viewport]\n");

   for (i = 0; i < count; ++i)
     {
        double dsx, dsy;
        int x = 0, y = 0, rz = 0;
        int view_port = 0;
        int vx = 0, vy = 0, vw = 0, vh = 0;
        E_Util_Transform *transform = NULL;

        transform = e_client_transform_core_transform_get((E_Client *)ec, i);
        if (!transform) continue;

        e_util_transform_move_round_get(transform, &x, &y, NULL);
        e_util_transform_scale_get(transform, &dsx, &dsy, NULL);
        e_util_transform_rotation_round_get(transform, NULL, NULL, &rz);
        view_port = e_util_transform_viewport_flag_get(transform);

        if (view_port)
          e_util_transform_viewport_get(transform, &vx, &vy, &vw, &vh);

        astrcat_(&str, "transform : [%d] [%d, %d] [%2.3f, %2.3f] [%d] [%d :%d, %d, %d, %d]\n",
                i, x, y, dsx, dsy, rz, view_port, vx, vy, vw, vh);

        if (e_util_transform_bg_transform_flag_get(transform))
          {
             e_util_transform_bg_move_round_get(transform, &x, &y, NULL);
             e_util_transform_bg_scale_get(transform, &dsx, &dsy, NULL);
             e_util_transform_bg_rotation_round_get(transform, NULL, NULL, &rz);

             astrcat_(&str, "transform_bg : --------- [%d] [%d, %d] [%2.3f, %2.3f] [%d]",
                     i, x, y, dsx, dsy, rz);
          }
     }

   return str;

fail:
   free(str);
   return NULL;
}

static const char*
_get_win_prop_Subsurface_Below_Child_List(const Evas_Object *evas_obj)
{
   const E_Comp_Wl_Client_Data *cdata;
   const E_Client *ec;
   char *str = NULL;

   const Eina_List *list;
   const Eina_List *l;
   const E_Client *child;

   ec = evas_object_data_get(evas_obj, "E_Client");

   if (!ec->comp_data)
     return strdup("None");

   cdata = (E_Comp_Wl_Client_Data*)ec->comp_data;
   list = cdata->sub.below_list;

   if (!list)
     return strdup("None");

   EINA_LIST_FOREACH(list, l, child)
     astrcat_(&str, "0x%zx, ", e_client_util_win_get(child));

   return str;

fail:
   free(str);
   return NULL;
}

static const char*
_get_win_prop_Subsurface_Child_List(const Evas_Object *evas_obj)
{
   const E_Comp_Wl_Client_Data *cdata;
   const E_Client *ec;
   char *str = NULL;

   const Eina_List *list;
   const Eina_List *l;
   const E_Client *child;

   ec = evas_object_data_get(evas_obj, "E_Client");

   if (!ec->comp_data)
     return strdup("None");

   cdata = (E_Comp_Wl_Client_Data*)ec->comp_data;
   list = cdata->sub.list;

   if (!list)
     return strdup("None");

   EINA_LIST_FOREACH(list, l, child)
     astrcat_(&str, "0x%zx, ", e_client_util_win_get(child));

   return str;

fail:
   free(str);
   return NULL;
}

static const char*
_get_win_prop_Subsurface_Parent(const Evas_Object *evas_obj)
{
   const E_Comp_Wl_Client_Data *cdata;
   const E_Client *ec;
   char *str = NULL;

   ec = evas_object_data_get(evas_obj, "E_Client");

   if (!ec->comp_data)
     return strdup("None");

   cdata = (E_Comp_Wl_Client_Data*)ec->comp_data;

   if (asprintf(&str, "0x%zx", cdata->sub.data ? e_client_util_win_get(cdata->sub.data->parent) : 0) < 0)
     return NULL;

   return str;
}

static const char*
_get_win_prop_Aux_Hint(const Evas_Object *evas_obj)
{
   const E_Comp_Wl_Client_Data *cdata;
   const E_Comp_Wl_Aux_Hint *hint;
   const Eina_List *l;

   const E_Client *ec;
   char *str = NULL;

   ec = evas_object_data_get(evas_obj, "E_Client");

   if (!ec->comp_data)
     return strdup("None");

   cdata = (E_Comp_Wl_Client_Data*)ec->comp_data;

   if (!cdata->aux_hint.hints)
     return strdup("None");

   EINA_LIST_FOREACH(cdata->aux_hint.hints, l, hint)
     astrcat_(&str, "[%d][%s][%s]\n", hint->id, hint->hint, hint->val);

   return str;

fail:
   free(str);
   return NULL;
}

static const char*
_get_win_prop_Video_Client(const Evas_Object *evas_obj)
{
   const E_Client *ec;
   char *str = NULL;

   ec = evas_object_data_get(evas_obj, "E_Client");

   if (asprintf(&str, "%d", ec->comp_data ? e_client_video_hw_composition_check((E_Client *)ec) : 0) < 0)
     return NULL;

   return str;
}

static const char*
_get_win_prop_Ignore_first_unmap(const Evas_Object *evas_obj)
{
   const E_Client *ec;
   char *str = NULL;

   ec = evas_object_data_get(evas_obj, "E_Client");

   if (asprintf(&str, "%c", ec->ignore_first_unmap) < 0)
     return NULL;

   return str;
}

static const char*
_get_win_prop_Transformed(const Evas_Object *evas_obj)
{
   const E_Client *ec = evas_object_data_get(evas_obj, "E_Client");

   return ec->transformed ? strdup("TRUE") : strdup("FALSE");
}

static const char*
_get_win_prop_Maximize_override(const Evas_Object *evas_obj)
{
   const E_Client *ec = evas_object_data_get(evas_obj, "E_Client");

   return ec->maximize_override ? strdup("TRUE") : strdup("FALSE");
}

static const char*
_get_win_prop_No_shape_cut(const Evas_Object *evas_obj)
{
   const E_Client *ec = evas_object_data_get(evas_obj, "E_Client");

   return ec->no_shape_cut ? strdup("TRUE") : strdup("FALSE");
}

static const char*
_set_win_prop_Ignored(Evas_Object *evas_obj, const char *prop_value)
{
   E_Client *ec = evas_object_data_get(evas_obj, "E_Client");

   if(strstr(prop_value, "TRUE"))
     ec->ignored = 1; /* TODO: is't right? */
   else if(strstr(prop_value, "FALSE"))
     e_client_unignore(ec);
   else
     return strdup("invalid property value");

   return NULL;
}

static const char*
_get_win_prop_Ignored(const Evas_Object *evas_obj)
{
   const E_Client *ec = evas_object_data_get(evas_obj, "E_Client");

   return ec->ignored ? strdup("TRUE") : strdup("FALSE");
}

static const char*
_get_win_prop_Layer_block(const Evas_Object *evas_obj)
{
   const E_Client *ec = evas_object_data_get(evas_obj, "E_Client");

   return ec->layer_block ? strdup("TRUE") : strdup("FALSE");
}

static const char*
_get_win_prop_Shape_changed(const Evas_Object *evas_obj)
{
   const E_Client *ec = evas_object_data_get(evas_obj, "E_Client");

   return ec->shape_changed ? strdup("TRUE") : strdup("FALSE");
}

static const char*
_get_win_prop_Unredirected_single(const Evas_Object *evas_obj)
{
   const E_Client *ec = evas_object_data_get(evas_obj, "E_Client");

   return ec->unredirected_single ? strdup("TRUE") : strdup("FALSE");
}

static const char*
_set_win_prop_Redirected(Evas_Object *evas_obj, const char *prop_value)
{
   E_Client *ec = evas_object_data_get(evas_obj, "E_Client");

   if(strstr(prop_value, "TRUE"))
     e_client_redirected_set(ec, EINA_TRUE);
   else if(strstr(prop_value, "FALSE"))
     e_client_redirected_set(ec, EINA_FALSE);
   else
     return strdup("invalid property value");

   return NULL;
}

static const char*
_get_win_prop_Redirected(const Evas_Object *evas_obj)
{
   const E_Client *ec = evas_object_data_get(evas_obj, "E_Client");

   return ec->redirected ? strdup("TRUE") : strdup("FALSE");
}

static const char*
_get_win_prop_Tooltip(const Evas_Object *evas_obj)
{
   const E_Client *ec = evas_object_data_get(evas_obj, "E_Client");

   return ec->tooltip ? strdup("TRUE") : strdup("FALSE");
}

static const char*
_get_win_prop_Dialog(const Evas_Object *evas_obj)
{
   const E_Client *ec = evas_object_data_get(evas_obj, "E_Client");

   return ec->dialog ? strdup("TRUE") : strdup("FALSE");
}

static const char*
_get_win_prop_Input_only(const Evas_Object *evas_obj)
{
   const E_Client *ec = evas_object_data_get(evas_obj, "E_Client");

   return ec->input_only ? strdup("TRUE") : strdup("FALSE");
}

static const char*
_get_win_prop_Override(const Evas_Object *evas_obj)
{
   const E_Client *ec = evas_object_data_get(evas_obj, "E_Client");

   return ec->override ? strdup("TRUE") : strdup("FALSE");
}

static const char*
_get_win_prop_E_Transient_Policy(const Evas_Object *evas_obj)
{
   const E_Client *ec;
   char *str = NULL;

   ec = evas_object_data_get(evas_obj, "E_Client");

   if (asprintf(&str, "%d", ec->transient_policy) < 0)
     return NULL;

   return str;
}

static const char*
_get_win_prop_E_FullScreen_Policy(const Evas_Object *evas_obj)
{
   const E_Client *ec;
   char *str = NULL;

   ec = evas_object_data_get(evas_obj, "E_Client");

   if (asprintf(&str, "%d", ec->fullscreen_policy) < 0)
     return NULL;

   return str;
}

static const char*
_get_win_prop_E_Maximize_Policy(const Evas_Object *evas_obj)
{
   const E_Client *ec;
   char *str = NULL;

   ec = evas_object_data_get(evas_obj, "E_Client");

   if (asprintf(&str, "0x%x", ec->maximized) < 0)
     return NULL;

   return str;
}

static const char*
_get_win_prop_Accept_focus(const Evas_Object *evas_obj)
{
   const E_Client *ec = evas_object_data_get(evas_obj, "E_Client");

   return ec->icccm.accepts_focus ? strdup("TRUE") : strdup("FALSE");
}

static const char*
_get_win_prop_Want_focus(const Evas_Object *evas_obj)
{
   const E_Client *ec = evas_object_data_get(evas_obj, "E_Client");

   return ec->want_focus ? strdup("TRUE") : strdup("FALSE");
}

static const char*
_get_win_prop_Take_focus(const Evas_Object *evas_obj)
{
   const E_Client *ec = evas_object_data_get(evas_obj, "E_Client");

   return ec->take_focus ? strdup("TRUE") : strdup("FALSE");
}

static const char*
_get_win_prop_Re_manage(const Evas_Object *evas_obj)
{
   const E_Client *ec = evas_object_data_get(evas_obj, "E_Client");

   return ec->re_manage ? strdup("TRUE") : strdup("FALSE");
}

static const char*
_set_win_prop_Fullscreen(Evas_Object *evas_obj, const char *prop_value)
{
   E_Client *ec = evas_object_data_get(evas_obj, "E_Client");

   if(strstr(prop_value, "TRUE"))
     e_client_fullscreen(ec, E_FULLSCREEN_RESIZE); /* TODO: what a policy to use? */
   else if(strstr(prop_value, "FALSE"))
     e_client_unfullscreen(ec);
   else
     return strdup("invalid property value");

   return NULL;
}

static const char*
_get_win_prop_Fullscreen(const Evas_Object *evas_obj)
{
   const E_Client *ec = evas_object_data_get(evas_obj, "E_Client");

   return ec->fullscreen ? strdup("TRUE") : strdup("FALSE");
}

static const char*
_set_win_prop_Urgent(Evas_Object *evas_obj, const char *prop_value)
{
   E_Client *ec = evas_object_data_get(evas_obj, "E_Client");

   if(strstr(prop_value, "TRUE"))
     e_client_urgent_set(ec, EINA_TRUE);
   else if(strstr(prop_value, "FALSE"))
     e_client_urgent_set(ec, EINA_FALSE);
   else
     return strdup("invalid property value");

   return NULL;
}

static const char*
_get_win_prop_Urgent(const Evas_Object *evas_obj)
{
   const E_Client *ec = evas_object_data_get(evas_obj, "E_Client");

   return ec->urgent ? strdup("TRUE") : strdup("FALSE");
}

static const char*
_set_win_prop_Sticky(Evas_Object *evas_obj, const char *prop_value)
{
   E_Client *ec = evas_object_data_get(evas_obj, "E_Client");

   if(strstr(prop_value, "TRUE"))
     e_client_stick(ec);
   else if(strstr(prop_value, "FALSE"))
     e_client_unstick(ec);
   else
     return strdup("invalid property value");

   return NULL;
}

static const char*
_get_win_prop_Sticky(const Evas_Object *evas_obj)
{
   const E_Client *ec = evas_object_data_get(evas_obj, "E_Client");

   return ec->sticky ? strdup("TRUE") : strdup("FALSE");
}

static const char*
_set_win_prop_Iconic(Evas_Object *evas_obj, const char *prop_value)
{
   E_Client *ec = evas_object_data_get(evas_obj, "E_Client");

   if(strstr(prop_value, "TRUE"))
     e_client_iconify(ec);
   else if(strstr(prop_value, "FALSE"))
     e_client_uniconify(ec);
   else
     return strdup("invalid property value");

   return NULL;
}

static const char*
_get_win_prop_Iconic(const Evas_Object *evas_obj)
{
   const E_Client *ec = evas_object_data_get(evas_obj, "E_Client");

   return ec->iconic ? strdup("TRUE") : strdup("FALSE");
}

static const char*
_set_win_prop_Focused(Evas_Object *evas_obj, const char *prop_value)
{
   E_Client *ec = evas_object_data_get(evas_obj, "E_Client");

   if(strstr(prop_value, "TRUE"))
     ec->focused = 1;
   else if(strstr(prop_value, "FALSE"))
     ec->focused = 0;
   else
     return strdup("invalid property value");

   return NULL;
}

static const char*
_get_win_prop_Focused(const Evas_Object *evas_obj)
{
   const E_Client *ec = evas_object_data_get(evas_obj, "E_Client");

   return ec->focused ? strdup("TRUE") : strdup("FALSE");
}

static const char*
_get_win_prop_Moving(const Evas_Object *evas_obj)
{
   const E_Client *ec = evas_object_data_get(evas_obj, "E_Client");

   return ec->moving ? strdup("TRUE") : strdup("FALSE");
}

static const char*
_set_win_prop_Hidden(Evas_Object *evas_obj, const char *prop_value)
{
   if(strstr(prop_value, "TRUE"))
     evas_object_hide(evas_obj);
   else if(strstr(prop_value, "FALSE"))
     evas_object_show(evas_obj);
   else
     return strdup("invalid property value");

   return NULL;
}

static const char*
_get_win_prop_Hidden(const Evas_Object *evas_obj)
{
   const E_Client *ec = evas_object_data_get(evas_obj, "E_Client");

   return ec->hidden ? strdup("TRUE") : strdup("FALSE");
}

static const char*
_get_win_prop_32bit(const Evas_Object *evas_obj)
{
   const E_Client *ec = evas_object_data_get(evas_obj, "E_Client");

   return ec->argb ? strdup("TRUE") : strdup("FALSE");
}

static const char*
_set_win_prop_Visible(Evas_Object *evas_obj, const char *prop_value)
{
   E_Client *ec = evas_object_data_get(evas_obj, "E_Client");

   if(strstr(prop_value, "TRUE"))
     ec->visible = 1;
   else if(strstr(prop_value, "FALSE"))
     ec->visible = 0;
   else
     return strdup("invalid property value");

   return NULL;
}

static const char*
_get_win_prop_Visible(const Evas_Object *evas_obj)
{
   const E_Client *ec = evas_object_data_get(evas_obj, "E_Client");

   return ec->visible ? strdup("TRUE") : strdup("FALSE");
}

/* this code looks awful but to make it sane some global changes are required,
 * but I'm not sure I'm allowed to do such changes which relate ONLY to e_info app */
static inline int
_check_layer_idx(const char *layer_name, int layer_idx)
{
   char tmp[64] = {0, };

   e_comp_layer_name_get(layer_idx, tmp, sizeof(tmp));

   return strncmp(tmp, layer_name, strlen(tmp));
}

static int
_e_comp_layer_idx_get(const char *layer_name)
{
   if (!layer_name) return E_LAYER_MAX + 1;

   if (!_check_layer_idx(layer_name, E_LAYER_BOTTOM))                     return E_LAYER_BOTTOM;
   if (!_check_layer_idx(layer_name, E_LAYER_BG))                         return E_LAYER_BG;
   if (!_check_layer_idx(layer_name, E_LAYER_DESKTOP))                    return E_LAYER_DESKTOP;
   if (!_check_layer_idx(layer_name, E_LAYER_DESKTOP_TOP))                return E_LAYER_DESKTOP_TOP;
   if (!_check_layer_idx(layer_name, E_LAYER_CLIENT_DESKTOP))             return E_LAYER_CLIENT_DESKTOP;
   if (!_check_layer_idx(layer_name, E_LAYER_CLIENT_BELOW))               return E_LAYER_CLIENT_BELOW;
   if (!_check_layer_idx(layer_name, E_LAYER_CLIENT_NORMAL))              return E_LAYER_CLIENT_NORMAL;
   if (!_check_layer_idx(layer_name, E_LAYER_CLIENT_ABOVE))               return E_LAYER_CLIENT_ABOVE;
   if (!_check_layer_idx(layer_name, E_LAYER_CLIENT_EDGE))                return E_LAYER_CLIENT_EDGE;
   if (!_check_layer_idx(layer_name, E_LAYER_CLIENT_FULLSCREEN))          return E_LAYER_CLIENT_FULLSCREEN;
   if (!_check_layer_idx(layer_name, E_LAYER_CLIENT_EDGE_FULLSCREEN))     return E_LAYER_CLIENT_EDGE_FULLSCREEN;
   if (!_check_layer_idx(layer_name, E_LAYER_CLIENT_POPUP))               return E_LAYER_CLIENT_POPUP;
   if (!_check_layer_idx(layer_name, E_LAYER_CLIENT_TOP))                 return E_LAYER_CLIENT_TOP;
   if (!_check_layer_idx(layer_name, E_LAYER_CLIENT_DRAG))                return E_LAYER_CLIENT_DRAG;
   if (!_check_layer_idx(layer_name, E_LAYER_CLIENT_PRIO))                return E_LAYER_CLIENT_PRIO;
   if (!_check_layer_idx(layer_name, E_LAYER_CLIENT_NOTIFICATION_LOW))    return E_LAYER_CLIENT_NOTIFICATION_LOW;
   if (!_check_layer_idx(layer_name, E_LAYER_CLIENT_NOTIFICATION_NORMAL)) return E_LAYER_CLIENT_NOTIFICATION_NORMAL;
   if (!_check_layer_idx(layer_name, E_LAYER_CLIENT_NOTIFICATION_HIGH))   return E_LAYER_CLIENT_NOTIFICATION_HIGH;
   if (!_check_layer_idx(layer_name, E_LAYER_CLIENT_NOTIFICATION_TOP))    return E_LAYER_CLIENT_NOTIFICATION_TOP;
   if (!_check_layer_idx(layer_name, E_LAYER_CLIENT_ALERT_LOW))           return E_LAYER_CLIENT_ALERT_LOW;
   if (!_check_layer_idx(layer_name, E_LAYER_CLIENT_ALERT))               return E_LAYER_CLIENT_ALERT;
   if (!_check_layer_idx(layer_name, E_LAYER_CLIENT_ALERT_HIGH))          return E_LAYER_CLIENT_ALERT_HIGH;
   if (!_check_layer_idx(layer_name, E_LAYER_CLIENT_CURSOR))              return E_LAYER_CLIENT_CURSOR;
   if (!_check_layer_idx(layer_name, E_LAYER_POPUP))                      return E_LAYER_POPUP;
   if (!_check_layer_idx(layer_name, E_LAYER_EFFECT))                     return E_LAYER_EFFECT;
   if (!_check_layer_idx(layer_name, E_LAYER_DESK_OBJECT_BELOW))          return E_LAYER_DESK_OBJECT_BELOW;
   if (!_check_layer_idx(layer_name, E_LAYER_DESK_OBJECT))                return E_LAYER_DESK_OBJECT;
   if (!_check_layer_idx(layer_name, E_LAYER_DESK_OBJECT_ABOVE))          return E_LAYER_DESK_OBJECT_ABOVE;
   if (!_check_layer_idx(layer_name, E_LAYER_MENU))                       return E_LAYER_MENU;
   if (!_check_layer_idx(layer_name, E_LAYER_DESKLOCK))                   return E_LAYER_DESKLOCK;
   if (!_check_layer_idx(layer_name, E_LAYER_MAX))                        return E_LAYER_MAX;

   return E_LAYER_MAX + 1;
}

static const char*
_set_win_prop_Layer(Evas_Object *evas_obj, const char *prop_value)
{
   E_Client *ec = evas_object_data_get(evas_obj, "E_Client");
   int layer_idx;

   layer_idx = _e_comp_layer_idx_get(prop_value);
   if (layer_idx == (E_LAYER_MAX + 1))
     return strdup("invalid property value");

   ec->layer = layer_idx;

   return NULL;
}

static const char*
_get_win_prop_Layer(const Evas_Object *evas_obj)
{
   const E_Client *ec;
   char *str = NULL;

   char layer_name[48] = {0,};

   ec = evas_object_data_get(evas_obj, "E_Client");
   e_comp_layer_name_get(ec->layer, layer_name, sizeof(layer_name));

   if (asprintf(&str, "[%d, %s]", ec->layer, layer_name) < 0)
     return NULL;

   return str;
}

static const char*
_get_win_prop_Shape_input(const Evas_Object *evas_obj)
{
   const E_Client *ec;
   char *str = NULL;
   int i = 0;

   ec = evas_object_data_get(evas_obj, "E_Client");

   if (!ec->shape_input_rects || ec->shape_input_rects_num <= 0)
     return strdup("None");

   for (i = 0 ; i < ec->shape_input_rects_num ; ++i)
     astrcat_(&str, "[%d,%d,%d,%d]\n", ec->shape_input_rects[i].x, ec->shape_input_rects[i].y,
             ec->shape_input_rects[i].w, ec->shape_input_rects[i].h);

   return str;

fail:
   free(str);
   return NULL;
}

static const char*
_get_win_prop_Shape_rects(const Evas_Object *evas_obj)
{
   const E_Client *ec;
   char *str = NULL;
   int i = 0;

   ec = evas_object_data_get(evas_obj, "E_Client");

   if (!ec->shape_rects || ec->shape_rects_num <= 0)
     return strdup("None");

   for (i = 0 ; i < ec->shape_rects_num ; ++i)
     astrcat_(&str, "[%d,%d,%d,%d]\n", ec->shape_rects[i].x, ec->shape_rects[i].y,
             ec->shape_rects[i].w, ec->shape_rects[i].h);

   return str;

fail:
   free(str);
   return NULL;
}

static const char*
_get_win_prop_Transients(const Evas_Object *evas_obj)
{
   const E_Client *ec;
   char *str = NULL;

   const E_Client *child;
   const Eina_List *l;

   ec = evas_object_data_get(evas_obj, "E_Client");

   if (!ec->transients)
     return strdup("None");

   EINA_LIST_FOREACH(ec->transients, l, child)
     astrcat_(&str, "0x%zx, ", e_client_util_win_get(child));

   return str;

fail:
   free(str);
   return NULL;
}

static const char*
_get_win_prop_ParentWindowID(const Evas_Object *evas_obj)
{
   const E_Client *ec;
   char *str = NULL;

   ec = evas_object_data_get(evas_obj, "E_Client");

   if (!ec->parent)
     return strdup("None");

   if (asprintf(&str, "0x%zx", e_client_util_win_get(ec->parent)) < 0)
     return NULL;

   return str;
}

static const char*
_set_win_prop_Geometry(Evas_Object *evas_obj, const char *prop_value)
{
   E_Client *ec = evas_object_data_get(evas_obj, "E_Client");
   int x = -1, y = -1, w = -1, h = -1;
   int ret;

   ret = sscanf(prop_value, "%d, %d %dx%d", &x, &y, &w, &h);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(ret == 4, (strdup("Invalid format")));

   if (x < 0 || y < 0 || w <= 0 || h <= 0)
     return strdup("invalid property value");

   /* TODO: I have no enough knowledges to say that it's a proper way
    *       to change e_client geometry */
   e_client_pos_set(ec, x, y);
   e_client_size_set(ec, w, h);

   return NULL;
}

static const char*
_get_win_prop_Geometry(const Evas_Object *evas_obj)
{
   const E_Client *ec;
   char *str = NULL;

   ec = evas_object_data_get(evas_obj, "E_Client");

   if (asprintf(&str, "[%d, %d %dx%d]", ec->x, ec->y, ec->w, ec->h) < 0)
     return NULL;

   return str;
}

static const char*
_set_win_prop_Role(Evas_Object *evas_obj, const char *prop_value)
{
   E_Client *ec = evas_object_data_get(evas_obj, "E_Client");

   e_client_window_role_set(ec, prop_value);

   return NULL;
}

static const char*
_get_win_prop_Role(const Evas_Object *evas_obj)
{
   const E_Client *ec;
   char *str = NULL;

   ec = evas_object_data_get(evas_obj, "E_Client");

   if (asprintf(&str, "%s", ec->icccm.window_role ?: "NO ROLE") < 0)
     return NULL;

   return str;
}

static const char*
_set_win_prop_Window_Name(Evas_Object *evas_obj, const char *prop_value)
{
   E_Client *ec = evas_object_data_get(evas_obj, "E_Client");

   /* TODO: I ain't sure it's a proper order */
   if (ec->netwm.name)
     eina_stringshare_replace(&ec->netwm.name, prop_value);
   else if (ec->icccm.title)
     eina_stringshare_replace(&ec->icccm.title, prop_value);
   else
     eina_stringshare_replace(&ec->netwm.name, prop_value);

   return NULL;
}

static const char*
_get_win_prop_Window_Name(const Evas_Object *evas_obj)
{
   const E_Client *ec;
   char *str = NULL;

   ec = evas_object_data_get(evas_obj, "E_Client");

   if (asprintf(&str, "%s", e_client_util_name_get(ec) ?: "NO NAME") < 0)
     return NULL;

   return str;
}

static const char*
_get_win_prop_ResourceID(const Evas_Object *evas_obj)
{
   const E_Client *ec;
   char *str = NULL;

   ec = evas_object_data_get(evas_obj, "E_Client");

   if (!ec->pixmap)
     return strdup("None");

   if (asprintf(&str, "%d", e_pixmap_res_id_get(ec->pixmap)) < 0)
     return NULL;

   return str;
}

static const char*
_get_win_prop_PID(const Evas_Object *evas_obj)
{
   const E_Client *ec;
   char *str = NULL;
   pid_t pid = -1;

   ec = evas_object_data_get(evas_obj, "E_Client");

   if (ec->comp_data)
     {
        const E_Comp_Wl_Client_Data *cdata = (const E_Comp_Wl_Client_Data*)ec->comp_data;
        if (cdata->surface)
          wl_client_get_credentials(wl_resource_get_client(cdata->surface), &pid, NULL, NULL);
     }

   if (asprintf(&str, "%d", pid) < 0)
     return NULL;

   return str;
}

static const char*
_get_win_prop_Window_ID(const Evas_Object *evas_obj)
{
   const E_Client *ec;
   char *str = NULL;

   ec = evas_object_data_get(evas_obj, "E_Client");

   if (asprintf(&str, "0x%zx", e_client_util_win_get(ec)) < 0)
     return NULL;

   return str;
}

typedef const char* (*get_prop_t)(const Evas_Object *evas_obj);
typedef const char* (*set_prop_t)(Evas_Object *evas_obj, const char *prop_value);

static struct property_manager
{
    const char* prop_name;

    /*
     * get one property
     *
     * evas_obj - an evas_obj (which is e_client) a property value has to be got for
     * return nullptr in case of an error, property value string otherwise
     *
     * property value string should be freed with free() when it's no longer needed
     *
     * can be nullptr if this property isn't getable */
    get_prop_t get_prop;

    /*
     * set one property
     *
     * evas_obj - an evas_obj (which is e_client) a property value has to be set for
     * prop_value - a value of property to set
     * return pointer to an error string in case of an error, nullptr otherwise
     *
     * error string should be freed with free() when it's no longer needed
     * it's this function responsibility to check property_value sanity
     *
     * can be nullptr if this property isn't setable */
    set_prop_t set_prop;
} win_properties[] =
{
    {
        "Window_ID",
        _get_win_prop_Window_ID,
        NULL
    },
    {
        "PID",
        _get_win_prop_PID,
        NULL
    },
    {
        "ResourceID",
        _get_win_prop_ResourceID,
        NULL
    },
    {
        "Window_Name",
        _get_win_prop_Window_Name,
        _set_win_prop_Window_Name
    },
    {
        "Role",
        _get_win_prop_Role,
        _set_win_prop_Role
    },
    {
        "Geometry",
        _get_win_prop_Geometry,
        _set_win_prop_Geometry
    },
    {
        "ParentWindowID",
        _get_win_prop_ParentWindowID,
        NULL
    },
    {
        "Transients",
        _get_win_prop_Transients,
        NULL
    },
    {
        "Shape_rects",
        _get_win_prop_Shape_rects,
        NULL
    },
    {
        "Shape_input",
        _get_win_prop_Shape_input,
        NULL
    },
    {
        "Layer",
        _get_win_prop_Layer,
        _set_win_prop_Layer
    },
    {
        "Visible",
        _get_win_prop_Visible,
        _set_win_prop_Visible
    },
    {
        "32bit",
        _get_win_prop_32bit,
        NULL
    },
    {
        "Hidden",
        _get_win_prop_Hidden,
        _set_win_prop_Hidden
    },
    {
        "Moving",
        _get_win_prop_Moving,
        NULL
    },
    {
        "Focused",
        _get_win_prop_Focused,
        _set_win_prop_Focused
    },
    {
        "Iconic",
        _get_win_prop_Iconic,
        _set_win_prop_Iconic
    },
    {
        "Sticky",
        _get_win_prop_Sticky,
        _set_win_prop_Sticky
    },
    {
        "Urgent",
        _get_win_prop_Urgent,
        _set_win_prop_Urgent
    },
    {
        "Fullscreen",
        _get_win_prop_Fullscreen,
        _set_win_prop_Fullscreen
    },
    {
        "Re_manage",
        _get_win_prop_Re_manage,
        NULL
    },
    {
       "Accept_focus",
       _get_win_prop_Accept_focus,
       NULL
    },
    {
        "Take_focus",
        _get_win_prop_Take_focus,
        NULL
    },
    {
        "Want_focus",
        _get_win_prop_Want_focus,
        NULL
    },
    {
        "E_Maximize_Policy",
        _get_win_prop_E_Maximize_Policy,
        NULL
    },
    {
        "E_FullScreen_Policy",
        _get_win_prop_E_FullScreen_Policy,
        NULL
    },
    {
        "E_Transient_Policy",
        _get_win_prop_E_Transient_Policy,
        NULL
    },
    {
        "Override",
        _get_win_prop_Override,
        NULL
    },
    {
        "Input_only",
        _get_win_prop_Input_only,
        NULL
    },
    {
        "Dialog",
        _get_win_prop_Dialog,
        NULL
    },
    {
        "Tooltip",
        _get_win_prop_Tooltip,
        NULL
    },
    {
        "Redirected",
        _get_win_prop_Redirected,
        _set_win_prop_Redirected
    },
    {
        "Unredirected_single",
        _get_win_prop_Unredirected_single,
        NULL
    },
    {
        "Shape_changed",
        _get_win_prop_Shape_changed,
        NULL
    },
    {
        "Layer_block",
        _get_win_prop_Layer_block,
        NULL
    },
    {
        "Ignored",
        _get_win_prop_Ignored,
        _set_win_prop_Ignored
    },
    {
        "No_shape_cut",
        _get_win_prop_No_shape_cut,
        NULL
    },
    {
        "Maximize_override",
        _get_win_prop_Maximize_override,
        NULL
    },
    {
        "Transformed",
        _get_win_prop_Transformed,
        NULL
    },
    {
        "Ignore_first_unmap",
        _get_win_prop_Ignore_first_unmap,
        NULL
    },
    {
        "Video Client",
        _get_win_prop_Video_Client,
        NULL
    },
    {
        "Aux_Hint Client",
        _get_win_prop_Aux_Hint,
        NULL
    },
    {
        "Subsurface Parent",
        _get_win_prop_Subsurface_Parent,
        NULL
    },
    {
        "Subsurface Child List",
        _get_win_prop_Subsurface_Child_List,
        NULL
    },
    {
        "Subsurface Below Child List",
        _get_win_prop_Subsurface_Below_Child_List,
        NULL
    },
    {
        "Transform",
        _get_win_prop_Transform,
        NULL
    },
    {
        "Rotation",
        _get_win_prop_Rotation,
        NULL
    },
    {
        "Input Region",
        _get_win_prop_Input_region,
        NULL
    }
};

#define __WINDOW_PROP_ARG_APPEND(title, value) ({                                    \
                                                eldbus_message_iter_arguments_append(iter, "(ss)", &struct_of_ec);    \
                                                eldbus_message_iter_arguments_append(struct_of_ec, "ss", (title), (value));  \
                                                eldbus_message_iter_container_close(iter, struct_of_ec);})

static Eldbus_Message*
_msg_fill_out_window_props(const Eldbus_Message *msg, Eldbus_Message_Iter *iter, Evas_Object *evas_obj,
        const char *property_name, const char *property_value)
{
   const int win_property_size = sizeof(win_properties)/sizeof(struct property_manager);
   Eldbus_Message_Iter* struct_of_ec;
   int idx;

   /* accordingly to -prop option rules (if user's provided some property name) */
   if (strlen(property_name))
     {
        /* check the property_name sanity */
        for (idx = 0; idx < win_property_size; ++idx)
          if (!strncmp(win_properties[idx].prop_name, property_name, sizeof(win_properties[idx])))
            break;

        if (idx == win_property_size)
          return eldbus_message_error_new(msg, INVALID_PROPERTY_NAME,
                  "get_window_prop: invalid property name");

        /* accordingly to -prop option rules (if user wanna set property) */
        if (strlen(property_value))
          {
             if (win_properties[idx].set_prop)
               {
                  /* in case of a success we just return an empty reply message */
                  const char* error_str = win_properties[idx].set_prop(evas_obj, property_value);
                  if (error_str)
                    {
                       Eldbus_Message* err_msg = eldbus_message_error_new(msg,
                               FAIL_TO_SET_PROPERTY, error_str);
                       free((void*)error_str);

                       return err_msg;
                    }
               }
             else
               return eldbus_message_error_new(msg, FAIL_TO_SET_PROPERTY,
                       "get_window_prop: this property isn't setable");
          }
        else /* if wanna get property */
          {
             if (win_properties[idx].get_prop)
               {
                  const char* res_str = win_properties[idx].get_prop(evas_obj);
                  if (res_str)
                    {
                       __WINDOW_PROP_ARG_APPEND(win_properties[idx].prop_name, res_str);
                       free((void*)res_str);
                    }
                  else
                    return eldbus_message_error_new(msg, FAIL_TO_GET_PROPERTY, "");
               }
             else
               return eldbus_message_error_new(msg, FAIL_TO_GET_PROPERTY,
                       "get_window_prop: this property isn't getable");
          }
     }
   else /* if user wanna get all properties */
     {
       /* to improve readability, if user wanna get properties for several windows, some
        * delimiter being used */
        __WINDOW_PROP_ARG_APPEND("delimiter", "");

        for (idx = 0; idx < win_property_size; ++idx)
          {
             if (win_properties[idx].get_prop)
               {
                  const char* res_str = win_properties[idx].get_prop(evas_obj);
                  if (res_str)
                    {
                       __WINDOW_PROP_ARG_APPEND(win_properties[idx].prop_name, res_str);
                       free((void*)res_str);
                    }
                  else
                    return eldbus_message_error_new(msg, FAIL_TO_GET_PROPERTY, "");
               }
          }
     }

   return NULL;

#undef __WINDOW_PROP_ARG_APPEND
}

/* create the reply message and look for window(s) an user wanna get/set property(ies) for */
static Eldbus_Message *
_msg_window_prop_append(const Eldbus_Message *msg, uint32_t mode, const char *value,
        const char *property_name, const char *property_value)
{
   const static int WINDOW_ID_MODE = 0;
   const static int WINDOW_PID_MODE = 1;
   const static int WINDOW_NAME_MODE = 2;

   Eldbus_Message_Iter *iter, *array_of_ec;
   Eldbus_Message *reply_msg, *error_msg = NULL;
   E_Client *ec;
   Evas_Object *o;
   unsigned long tmp = 0;
   uint64_t value_number = 0;
   Eina_Bool res = EINA_FALSE;
   Eina_Bool window_exists = EINA_FALSE;

   if (mode == WINDOW_ID_MODE || mode == WINDOW_PID_MODE)
     {
        if (!value) value_number = 0;
        else
          {
             if (strlen(value) >= 2 && value[0] == '0' && value[1] == 'x')
               res = e_util_string_to_ulong(value, &tmp, 16);
             else
               res = e_util_string_to_ulong(value, &tmp, 10);

             if (res == EINA_FALSE)
               {
                  ERR("get_window_prop: invalid input arguments");

                  return eldbus_message_error_new(msg, INVALID_ARGS,
                          "get_window_prop: invalid input arguments");
               }
             value_number = (uint64_t)tmp;
          }
     }

   /* msg - is a method call message */
   reply_msg = eldbus_message_method_return_new(msg);
   iter = eldbus_message_iter_get(reply_msg);
   eldbus_message_iter_arguments_append(iter, "a(ss)", &array_of_ec);

   for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
     {
        ec = evas_object_data_get(o, "E_Client");
        if (!ec) continue;

        /* here we're dealing with evas objects which are e_client */

        if (mode == WINDOW_ID_MODE)
          {
             Ecore_Window win = e_client_util_win_get(ec);

             if (win == value_number)
               {
                  window_exists = EINA_TRUE;
                  error_msg = _msg_fill_out_window_props(msg, array_of_ec, o, property_name, property_value);
                  break;
               }
          }
        else if (mode == WINDOW_PID_MODE)
          {
             pid_t pid = -1;
             if (ec->comp_data)
               {
                  E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)ec->comp_data;
                  if (cdata->surface)
                    {
                       wl_client_get_credentials(wl_resource_get_client(cdata->surface), &pid, NULL, NULL);
                    }
               }
             if (pid == value_number)
               {
                  window_exists = EINA_TRUE;
                  error_msg = _msg_fill_out_window_props(msg, array_of_ec, o, property_name, property_value);
                  if (error_msg)
                    break;
               }
          }
        else if (mode == WINDOW_NAME_MODE)
          {
             const char *name = e_client_util_name_get(ec) ?: "NO NAME";

             if (name != NULL && value != NULL)
               {
                  const char *find = strstr(name, value);

                  if (find)
                    {
                       window_exists = EINA_TRUE;
                       error_msg = _msg_fill_out_window_props(msg, array_of_ec, o, property_name, property_value);
                       if (error_msg)
                         break;
                    }
               }
          }
     }

   eldbus_message_iter_container_close(iter, array_of_ec);

   if (window_exists == EINA_TRUE && !error_msg)
     return reply_msg;

   /* TODO: I'm not sure we gotta do it. But, who's responsible for message freeing if we've not it
    *       returned to caller(eldbus)? */
   eldbus_message_unref(reply_msg);

   /* some error while filling out the reply message */
   if (error_msg)
     return error_msg;

   return eldbus_message_error_new(msg, WIN_NOT_EXIST, "get_window_prop: specified window(s) doesn't exist");
}

static Eldbus_Message *
_e_info_server_cb_scrsaver(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   E_Info_Cmd_Scrsaver cmd;
   double sec;
   Eina_Bool res, enabled;
   char result[1024];

   res = eldbus_message_arguments_get(msg,
                                      SIGNATURE_SCRSAVER_CLIENT,
                                      &cmd,
                                      &sec);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(res, reply);

   switch (cmd)
     {
      case E_INFO_CMD_SCRSAVER_INFO:
         sec = e_screensaver_timeout_get();
         enabled = e_screensaver_enabled_get();
         snprintf(result, sizeof(result),
                  "[Server] screen saver\n" \
                  "\tState: %s\n"           \
                  "\tTimeout period: %lf\n",
                  enabled ? "Enabled" : "Disabled",
                  sec);
         break;
      case E_INFO_CMD_SCRSAVER_ENABLE:
         e_screensaver_enable();
         snprintf(result, sizeof(result),
                  "[Server] Enabled the screen saver");
         break;
      case E_INFO_CMD_SCRSAVER_DISABLE:
         e_screensaver_disable();
         snprintf(result, sizeof(result),
                  "[Server] Disabled the screen saver");
         break;
      case E_INFO_CMD_SCRSAVER_TIMEOUT:
         e_screensaver_timeout_set(sec);
         snprintf(result, sizeof(result),
                  "[Server] Set timeout period of the screen saver: %lf",
                  sec);
         break;
      default:
         snprintf(result, sizeof(result),
                  "[Server] Error Unknown cmd(%d) for the screen saver",
                  cmd);
         break;
     }

   eldbus_message_arguments_append(reply,
                                   SIGNATURE_SCRSAVER_SERVER,
                                   result);

   return reply;
}

static Eldbus_Message *
_e_info_server_cb_window_prop_get(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   uint32_t mode = 0;
   const char *value = NULL;
   const char *property_name = NULL, *property_value = NULL;

   if (!eldbus_message_arguments_get(msg, "usss", &mode, &value, &property_name, &property_value))
     {
        ERR("Error getting arguments.");

        return eldbus_message_error_new(msg, GET_CALL_MSG_ARG_ERR,
                "get_window_prop: an attempt to get arguments from method call message failed");
     }

   /* TODO: it's guaranteed, by client logic, that 'value', 'property_name' and 'property_value'
    *       can be ONLY either empty string or string. Should I check this? <if( !property_name )> */
   return _msg_window_prop_append(msg, mode, value, property_name, property_value);
}

typedef struct {
     Eldbus_Message *reply;
     int num;
     char *result_str;
} Dump_Win_Data;

static void
_image_save_done_cb(void *data, E_Client* ec EINA_UNUSED, const Eina_Stringshare *dest, E_Capture_Save_State state)
{
   Dump_Win_Data *dump = (Dump_Win_Data *)data;

   dump->num --;

   if (state != E_CAPTURE_SAVE_STATE_DONE)
     astrcat_(&dump->result_str, "%s FAILED\n", dest ?: "Can't save the file(Already Exists?)");
   else
     astrcat_(&dump->result_str, "%s SAVED\n", dest);

   if (dump->num <= 0)
     {
        eldbus_message_arguments_append(dump->reply, "s", dump->result_str);
        eldbus_connection_send(e_info_server.edbus_conn, dump->reply, NULL, NULL, -1);
        free(dump->result_str);
        E_FREE(dump);
     }

   return;
fail:
   free(dump->result_str);

   if (dump->num <= 0)
     {
        eldbus_message_arguments_append(dump->reply, "s", "Failed to make log message...");
        eldbus_connection_send(e_info_server.edbus_conn, dump->reply, NULL, NULL, -1);
        E_FREE(dump);
     }
}

#undef astrcat_

static void _e_info_server_cb_wins_dump_topvwins(const char *dir, Eldbus_Message *reply)
{
   Evas_Object *o;
   E_Client *ec;
   Ecore_Window win;
   int rotation = 0;
   char fname[PATH_MAX];
   Eina_Stringshare *s_fname, *s_dir;
   Eina_List *topvwins = NULL;
   Dump_Win_Data *dump = NULL;
   E_Capture_Save_State state;

   for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
     {
        ec = evas_object_data_get(o, "E_Client");

        if (!ec) continue;
        if (e_client_util_ignored_get(ec)) continue;

        topvwins = eina_list_append(topvwins, ec);
     }

   if (topvwins)
     {
        dump = E_NEW(Dump_Win_Data, 1);
        EINA_SAFETY_ON_NULL_GOTO(dump, fail);

        dump->reply = reply;
        dump->num = eina_list_count(topvwins);

        s_dir = eina_stringshare_add(dir);

        EINA_LIST_FREE(topvwins, ec)
          {
             win = e_client_util_win_get(ec);
             if (ec->comp_data)
               rotation = ec->comp_data->scaler.buffer_viewport.buffer.transform * 90;
             snprintf(fname, sizeof(fname), "0x%08zx_%d", win, rotation);

             s_fname = eina_stringshare_add(fname);

             state = e_client_image_save(ec, s_dir, s_fname,
                                         _image_save_done_cb, dump, EINA_TRUE);

             if (state != E_CAPTURE_SAVE_STATE_START)
               dump->num --;

             eina_stringshare_del(s_fname);
          }
        eina_stringshare_del(s_dir);

        if (dump->num <= 0)
          E_FREE(dump);
     }

   //no available windows to dump
fail:
   if (!dump)
     {
        eldbus_message_arguments_append(reply, "s", "ERR: There are no topvwins.");
        eldbus_connection_send(e_info_server.edbus_conn, reply, NULL, NULL, -1);
     }

}

static void _e_info_server_cb_wins_dump_ns(const char *dir)
{
   Evas_Object *o;

   for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
     {
        Ecore_Window win;
        E_Client *ec;
        Evas_Native_Surface *ns = NULL;
        Evas_Object *co = NULL; // native surface set
        tbm_surface_h tbm_surface = NULL;
        char fname[PATH_MAX];
        const char *bltin_t = NULL;

        ec = evas_object_data_get(o, "E_Client");
        win = e_client_util_win_get(ec);

        // find obj which have native surface set
        bltin_t = evas_object_type_get(o);
        if (!e_util_strcmp(bltin_t, "image"))
          {
             // builtin types "image" could have cw->obj
             ns = evas_object_image_native_surface_get(o);
             if (ns) co = o;
          }

        if (!ns)
          {
             if (!co) co = evas_object_name_child_find(o, "cw->obj", -1);
             if (co) ns = evas_object_image_native_surface_get(co);
          }

        if (!ns)
          {
             Eina_List *ll;
             Evas_Object *c = NULL;

             if (evas_object_smart_data_get(o))
               {
                  //find smart obj members
                  EINA_LIST_REVERSE_FOREACH(evas_object_smart_members_get(o), ll, c)
                    {
                       if (!co) co = evas_object_name_child_find(c, "cw->obj", -1);
                       if (co) ns = evas_object_image_native_surface_get(co);
                       if (ns) break;
                    }
               }
          }

        if (!ns) continue;

        switch (ns->type)
          {
           case EVAS_NATIVE_SURFACE_WL:
              snprintf(fname, sizeof(fname), "%s/0x%08zx_wl_%p.png", dir, win, co);
              if (ns->data.wl.legacy_buffer)
                tbm_surface = wayland_tbm_server_get_surface(NULL, ns->data.wl.legacy_buffer);
              if (tbm_surface)
                tdm_helper_dump_buffer(tbm_surface, fname);
              break;
           case EVAS_NATIVE_SURFACE_TBM:
              snprintf(fname, sizeof(fname), "%s/0x%08zx_tbm_%p.png", dir, win, co);
              if (ns->data.tbm.buffer)
                tdm_helper_dump_buffer(ns->data.tbm.buffer, fname);
              break;
           default:
              break;
          }
     }
}

static void _e_info_server_cb_wins_dump_hwc_wins(const char *dir)
{
   E_Comp_Screen *e_comp_screen = NULL;
   E_Output *output = NULL;
   E_Hwc_Window *hwc_window = NULL;
   E_Hwc *hwc = NULL;
   Eina_List *o = NULL, *oo = NULL;
   Eina_List *l = NULL, *ll = NULL;

   e_comp_screen = e_comp->e_comp_screen;
   if (!e_comp_screen) return;

   EINA_LIST_FOREACH_SAFE(e_comp_screen->outputs, o, oo, output)
     {
        if (!output) continue;
        if (!output->config.enabled) continue;
        if (!output->tdm_hwc) continue;

         hwc = output->hwc;

         EINA_LIST_FOREACH_SAFE(hwc->hwc_windows, l, ll, hwc_window)
           {
              char fname[PATH_MAX];
              tbm_format fmt;

              if (!hwc_window) continue;
              if (!hwc_window->display.buffer.tsurface) continue;

              fmt = tbm_surface_get_format(hwc_window->display.buffer.tsurface);
              switch (fmt)
                {
                 case TBM_FORMAT_ARGB8888:
                 case TBM_FORMAT_XRGB8888:
                  if (hwc_window->is_target)
                    snprintf(fname, sizeof(fname), "compositor_hwin_%p_tbm_%p", hwc_window, hwc_window->display.buffer.tsurface);
                  else
                    snprintf(fname, sizeof(fname), "0x%08zx_hwin_%p_tbm_%p", e_client_util_win_get(hwc_window->ec),
                             hwc_window, hwc_window->display.buffer.tsurface);

                  tbm_surface_internal_capture_buffer(hwc_window->display.buffer.tsurface, dir, fname, "png");
                  break;
                 case TBM_FORMAT_YUV420:
                 case TBM_FORMAT_YVU420:
                 case TBM_FORMAT_NV12:
                 case TBM_FORMAT_NV21:
                 case TBM_FORMAT_YUYV:
                 case TBM_FORMAT_UYVY:
                  if (hwc_window->is_target)
                    snprintf(fname, sizeof(fname), "compositor_hwin_%p_tbm_%p", hwc_window, hwc_window->display.buffer.tsurface);
                  else
                    snprintf(fname, sizeof(fname), "0x%08zx_hwin_%p_tbm_%p", e_client_util_win_get(hwc_window->ec),
                             hwc_window, hwc_window->display.buffer.tsurface);

                  tbm_surface_internal_capture_buffer(hwc_window->display.buffer.tsurface, dir, fname, "yuv");
                  break;
                 default:
                  break;
                }
           }
     }
}

static Eldbus_Message *
_e_info_server_cb_wins_dump(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   const char *type;
   const char *dir;
   char log[2048];

   if (!eldbus_message_arguments_get(msg, SIGNATURE_DUMP_WINS, &type, &dir))
     {
        ERR("Error getting arguments.");
        return reply;
     }

   if (!e_util_strcmp(type, "topvwins"))
     {
        _e_info_server_cb_wins_dump_topvwins(dir, reply);
        return NULL;
     }
   else if (!e_util_strcmp(type, "ns"))
     _e_info_server_cb_wins_dump_ns(dir);
   else if (!e_util_strcmp(type, "hwc_wins"))
     _e_info_server_cb_wins_dump_hwc_wins(dir);

   snprintf(log, sizeof(log), "path:%s type:%s Dump Completed\n", dir, type);
   eldbus_message_arguments_append(reply, "s", log);
   return reply;
}

static Eldbus_Message *
_e_info_server_cb_force_visible(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   unsigned int obj;
   Eina_Bool visible;
   Evas_Object *o;
   E_Client *ec;

   if (!eldbus_message_arguments_get(msg, SIGNATURE_FORCE_VISIBLE_CLIENT, &obj, &visible))
     {
        ERR("Error getting arguments.");
        return reply;
     }

   o = (Evas_Object*)((uintptr_t)obj);

   ec = evas_object_data_get(o, "E_Client");
   if (ec && !e_pixmap_resource_get(ec->pixmap))
     {
        char msg[256];
        snprintf(msg, sizeof msg, "obj(%p) doesn't have valid wl_buffer", o);
        eldbus_message_arguments_append(reply, "s", msg);
        return reply;
     }

   if (visible)
     evas_object_show(o);
   else
     evas_object_hide(o);

   return reply;
}

/* Method Handlers */
static Eldbus_Message *
_e_info_server_cb_subsurface(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   Eldbus_Message_Iter *iter = eldbus_message_iter_get(reply);
   Eldbus_Message_Iter *array_of_ec;
   E_Client *ec;
   Evas_Object *o;

   eldbus_message_iter_arguments_append(iter, "a("SIGNATURE_SUBSURFACE")", &array_of_ec);

   // append clients.
   for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
     {
        Eldbus_Message_Iter* struct_of_ec;
        Ecore_Window win = 0, parent = 0;
        unsigned int buf_id = 0;
        int x = 0, y = 0, w = 0, h = 0;
        unsigned int transform = 0, visible = 0, alpha = 0, ignore = 0, maskobj = 0, video = 0, stand = 0;
        Ecore_Window bgrect = 0;
        const char *name = NULL;
        E_Comp_Wl_Buffer *buffer;
        const Evas_Map *map;

        ec = evas_object_data_get(o, "E_Client");
        if (!ec)
          {
             if (!evas_object_visible_get(o)) continue;

             name = evas_object_name_get(o);
             if (!name) continue;
             if (strncmp(name, "below_bg_rectangle", 18)) continue;
             win = (Ecore_Window)o;
             evas_object_geometry_get(o, &x, &y, &w, &h);
             visible = evas_object_visible_get(o);
          }
        else
          {
             if (e_object_is_del(E_OBJECT(ec)) || !ec->comp_data) continue;
             if (!ec->comp_data->sub.data &&
                 !ec->comp_data->sub.list && !ec->comp_data->sub.list_pending &&
                 !ec->comp_data->sub.below_list && !ec->comp_data->sub.below_list_pending)
               continue;
             win = e_client_util_win_get(ec);
             if (ec->comp_data->sub.data)
               parent = e_client_util_win_get(ec->comp_data->sub.data->parent);
             buffer = e_pixmap_resource_get(ec->pixmap);
             if (buffer)
               buf_id = (buffer->resource) ? wl_resource_get_id(buffer->resource) : (WAYLAND_SERVER_RESOURCE_ID_MASK & 99999);
             map = evas_object_map_get(ec->frame);
             if (map)
               {
                  Evas_Coord x1, x2, y1, y2;
                  E_Comp_Wl_Buffer_Viewport *vp = &ec->comp_data->scaler.buffer_viewport;
                  evas_map_point_coord_get(map, 0, &x1, &y1, NULL);
                  evas_map_point_coord_get(map, 2, &x2, &y2, NULL);
                  x = x1, y = y1, w = x2 - x1, h = y2 - y1;
                  transform = vp->buffer.transform;
               }
             else
               evas_object_geometry_get(ec->frame, &x, &y, &w, &h);
             visible = evas_object_visible_get(o);
             alpha = e_comp_object_alpha_get(ec->frame);
             ignore = e_client_util_ignored_get(ec);
             if (ec->comp_data->sub.below_obj)
               bgrect = (Ecore_Window)ec->comp_data->sub.below_obj;
             maskobj = e_comp_object_mask_has(ec->frame);
             video = (e_client_video_hw_composition_check(ec)) ? 1 : 0;
             if (ec->comp_data->sub.data)
               stand = ec->comp_data->sub.data->stand_alone;
             name = e_client_util_name_get(ec);
             if (!name)
               name = "NO NAME";
          }

        eldbus_message_iter_arguments_append(array_of_ec, "("SIGNATURE_SUBSURFACE")", &struct_of_ec);

        eldbus_message_iter_arguments_append
           (struct_of_ec, SIGNATURE_SUBSURFACE,
            win, parent, buf_id, x, y, w, h, transform, visible, alpha, ignore, maskobj, video, stand, bgrect, name);

        eldbus_message_iter_container_close(array_of_ec, struct_of_ec);
     }

   eldbus_message_iter_container_close(iter, array_of_ec);

   return reply;
}

static Eldbus_Message *
_e_info_server_cb_eina_log_levels(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   const char *start = NULL;
   int len = 0;

   if (!eldbus_message_arguments_get(msg, "s", &start) || !start)
     {
        ERR("Error getting arguments.");
        return reply;
     }

   while (1)
     {
        char module_name[256];
        char *end = NULL;
        char *tmp = NULL;
        int level;

        end = strchr(start, ':');
        if (!end)
           break;

        // Parse level, keep going if failed
        level = (int)strtol((char *)(end + 1), &tmp, 10);
        if (tmp == (end + 1))
           goto parse_end;

        // Parse name
        len = MIN(end - start, (sizeof module_name) - 1);
        strncpy(module_name, start, len);
        module_name[len] = '\0';

		  eina_log_domain_level_set((const char*)module_name, level);

parse_end:
        start = strchr(tmp, ',');
        if (start)
           start++;
        else
           break;
     }

   return reply;
}

static Eldbus_Message *
_e_info_server_cb_eina_log_path(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   const char *path = NULL;

   if (!eldbus_message_arguments_get(msg, "s", &path) || !path)
     {
        ERR("Error getting arguments.");
        return reply;
     }

   e_log_path_set(path);

   return reply;
}

#ifdef HAVE_DLOG
static Eldbus_Message *
_e_info_server_cb_dlog_switch(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   uint32_t onoff;

   if (!eldbus_message_arguments_get(msg, "i", &onoff))
     {
        ERR("Error getting arguments.");
        return reply;
     }

   if ((onoff == 1) || (onoff == 0))
     e_log_dlog_enable(onoff);

   return reply;
}
#endif

static Eldbus_Message *
_e_info_server_cb_rotation_query(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);

   /* TODO: need implementation */

   return reply;
}

static void
_e_info_event_rotation_free(void *data EINA_UNUSED, void *event)
{
   E_Event_Info_Rotation_Message *ev = event;

   e_object_unref(E_OBJECT(ev->zone));
   free(ev);
}

static Eldbus_Message *
_e_info_server_cb_rotation_message(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   E_Event_Info_Rotation_Message *ev;
   E_Info_Rotation_Message rot_msg;
   E_Zone *z;
   Eina_List *l;
   uint32_t zone_num;
   uint32_t rval;

   if (!eldbus_message_arguments_get(msg, "iii", &rot_msg, &zone_num, &rval))
     {
        ERR("Error getting arguments.");
        return reply;
     }

   if (rot_msg == E_INFO_ROTATION_MESSAGE_SET)
     {
        /* check if rval is valid */
        if ((rval > 270) || (rval % 90 != 0))
          return reply;
     }

   ev = E_NEW(E_Event_Info_Rotation_Message, 1);
   if (EINA_UNLIKELY(!ev))
     {
        ERR("Failed to allocate ""E_Event_Info_Rotation_Message""");
        return reply;
     }

   if (zone_num == -1)
     ev->zone = e_zone_current_get();
   else
     {
        EINA_LIST_FOREACH(e_comp->zones, l, z)
          {
             if (z->num == zone_num)
               ev->zone = z;
          }
     }

   if (!ev->zone)
     {
        ERR("Failed to found zone by given num: num %d", zone_num);
        free(ev);
        return reply;
     }

   e_object_ref(E_OBJECT(ev->zone));
   ev->message = rot_msg;
   ev->rotation = rval;

   ecore_event_add(E_EVENT_INFO_ROTATION_MESSAGE, ev, _e_info_event_rotation_free, NULL);

   return reply;
}

static void
protocol_cb_client_destroy(struct wl_listener *listener, void *data)
{
   struct wl_client *wc = (struct wl_client *)data;
   struct timespec tp;
   unsigned int time;
   pid_t client_pid = -1;
   const char *client_name = NULL;
   E_Comp_Connected_Client_Info *cinfo;
   Eina_List *l;
   char strbuf[512], *str_buff = strbuf;
   int str_r, str_l;

   str_buff[0] = '\0';
   str_r = sizeof(strbuf);

   wl_client_get_credentials(wc, &client_pid, NULL, NULL);

   clock_gettime(CLOCK_MONOTONIC, &tp);
   time = (tp.tv_sec * 1000000L) + (tp.tv_nsec / 1000);

   EINA_LIST_FOREACH(e_comp->connected_clients, l, cinfo)
     {
        if (cinfo->pid == client_pid)
          {
              client_name = cinfo->name;
              break;
          }
     }

   BUF_SNPRINTF("[%10.3f] Server           [PID:%d] client destroying", time / 1000.0, client_pid);
   BUF_SNPRINTF(", cmd: %s", client_name ? client_name : "cmd is NULL");

   if (log_fp_ptrace)
     fprintf(log_fp_ptrace, "%s\n", strbuf);
   else
     INF("%s", strbuf);

   wl_list_remove(&listener->link);
   E_FREE(listener);
}

static void
protocol_client_destroy_listener_reg(struct wl_client *client)
{
   struct wl_listener *destroy_listener;

   destroy_listener = wl_client_get_destroy_listener(client, protocol_cb_client_destroy);
   if (destroy_listener) return;

   destroy_listener = E_NEW(struct wl_listener, 1);
   EINA_SAFETY_ON_NULL_RETURN(destroy_listener);

   destroy_listener->notify = protocol_cb_client_destroy;
   wl_client_add_destroy_listener(client, destroy_listener);
}

/* wayland private function */
const char *
get_next_argument(const char *signature, struct argument_details *details)
{
   details->nullable = 0;
   for(; *signature; ++signature)
     {
        switch(*signature)
          {
           case 'i':
           case 'u':
           case 'f':
           case 's':
           case 'o':
           case 'n':
           case 'a':
           case 'h':
             details->type = *signature;
             return signature + 1;
           case '?':
             details->nullable = 1;
          }
     }
   details->type = '\0';
   return signature;
}

static void
_e_info_server_protocol_debug_func2(void *user_data, enum wl_protocol_logger_type direction, const struct wl_protocol_logger_message *message)
{
   int i;
   struct argument_details arg;
   struct wl_client *wc = wl_resource_get_client(message->resource);
   const char *signature = message->message->signature;
   struct timespec tp;
   unsigned int time;
   pid_t client_pid = -1;
   E_Comp_Connected_Client_Info *cinfo;
   Eina_List *l;
   char strbuf[512], *str_buff = strbuf;
   int str_r, str_l;

   str_buff[0] = '\0';
   str_r = sizeof(strbuf);

   if (wc)
     {
        protocol_client_destroy_listener_reg(wc);
        wl_client_get_credentials(wc, &client_pid, NULL, NULL);
     }

   clock_gettime(CLOCK_MONOTONIC, &tp);
   time = (tp.tv_sec * 1000000L) + (tp.tv_nsec / 1000);

   E_Info_Protocol_Log elog = {0,};
   elog.type = (direction == WL_PROTOCOL_LOGGER_EVENT)?1:0;
   elog.client_pid = client_pid;
   elog.target_id = wl_resource_get_id(message->resource);
   snprintf(elog.name, PATH_MAX, "%s:%s", wl_resource_get_class(message->resource), message->message->name);
   EINA_LIST_FOREACH(e_comp->connected_clients, l, cinfo)
     {
        if (cinfo->pid == client_pid)
          snprintf(elog.cmd, PATH_MAX, "%s", cinfo->name);
     }

   if (!e_info_protocol_rule_validate(&elog)) return;
   BUF_SNPRINTF("[%10.3f] %s%d%s%s@%u.%s(",
              time / 1000.0,
              elog.type ? "Server -> Client [PID:" : "Server <- Client [PID:",
              client_pid, "] ",
              wl_resource_get_class(message->resource),
              wl_resource_get_id(message->resource),
              message->message->name);

   for (i = 0; i < message->arguments_count; i++)
     {
        signature = get_next_argument(signature, &arg);
        if (i > 0) BUF_SNPRINTF(", ");

        switch (arg.type)
          {
           case 'u':
             BUF_SNPRINTF("%u", message->arguments[i].u);
             break;
           case 'i':
             BUF_SNPRINTF("%d", message->arguments[i].i);
             break;
           case 'f':
             BUF_SNPRINTF("%f",
             wl_fixed_to_double(message->arguments[i].f));
             break;
           case 's':
             BUF_SNPRINTF("\"%s\"", message->arguments[i].s);
             break;
           case 'o':
             if (message->arguments[i].o)
               BUF_SNPRINTF("%s@%u",
                        wl_resource_get_class((struct wl_resource*)message->arguments[i].o),
                        wl_resource_get_id((struct wl_resource*)message->arguments[i].o));
             else
               BUF_SNPRINTF("nil");
             break;
           case 'n':
             BUF_SNPRINTF("new id %s@", (message->message->types[i]) ? message->message->types[i]->name : "[unknown]");
             if (message->arguments[i].n != 0)
               BUF_SNPRINTF("%u", message->arguments[i].n);
             else
               BUF_SNPRINTF("nil");
             break;
           case 'a':
             BUF_SNPRINTF("array");
             break;
           case 'h':
             BUF_SNPRINTF("fd %d", message->arguments[i].h);
             break;
          }
     }

   BUF_SNPRINTF("), cmd: %s", elog.cmd ? elog.cmd : "cmd is NULL");

   if (log_fp_ptrace)
     fprintf(log_fp_ptrace, "%s\n", strbuf);
   else
     INF("%s", strbuf);
}

static Eldbus_Message *
_e_info_server_cb_protocol_trace(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   const char *path = NULL;

   if (!eldbus_message_arguments_get(msg, "s", &path) || !path)
     {
        ERR("Error getting arguments.");
        return reply;
     }

   if (log_fp_ptrace != NULL)
     {
        fclose(log_fp_ptrace);
        log_fp_ptrace = NULL;
     }

   if (!strncmp(path, "disable", 7))
     {
        if (e_info_protocol_logger)
          {
             wl_protocol_logger_destroy(e_info_protocol_logger);
             e_info_protocol_logger = NULL;
          }
        return reply;
     }

   /* if path's not elog, we open the new log file. Otherwise, the log will be printed via eina_log */
   if (strncmp(path, "elog", 4))
     {
        log_fp_ptrace = fopen(path, "a");
        if (!log_fp_ptrace)
          {
             ERR("failed: open file(%s)\n", path);
             return reply;
          }
        setvbuf(log_fp_ptrace, NULL, _IOLBF, 512);
     }

     if (e_info_protocol_logger)
       {
          wl_protocol_logger_destroy(e_info_protocol_logger);
          e_info_protocol_logger = NULL;
       }
     e_info_protocol_logger = wl_display_add_protocol_logger(e_comp->wl_comp_data->wl.disp, _e_info_server_protocol_debug_func2, NULL);

   return reply;
}

static Eldbus_Message *
_e_info_server_cb_protocol_rule(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply_msg = eldbus_message_method_return_new(msg);
   char reply[4096];
   int len = sizeof (reply);
   int argc = 3;
   char *argv[3];

   if (!eldbus_message_arguments_get(msg, "sss", &argv[0], &argv[1], &argv[2]) || !argv[0] || !argv[1] || !argv[2])
     {
        ERR("Error getting arguments.");
        return reply_msg;
     }

   if ((eina_streq(argv[0], "remove") || eina_streq(argv[0], "file")) && eina_streq(argv[2], "no_data"))
     argc--;
   if ((eina_streq(argv[0], "print") || eina_streq(argv[0], "help")) && eina_streq(argv[1], "no_data") && eina_streq(argv[2], "no_data"))
     argc = 1;

   e_info_protocol_rule_set(argc, (const char**)&(argv[0]), reply, &len);

   eldbus_message_arguments_append(reply_msg, "s", reply);

   return reply_msg;
}

static Eldbus_Message *
_e_info_server_cb_keymap_info_get(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);

   eldbus_message_arguments_append(reply, "hi", e_comp_wl->xkb.fd, e_comp_wl->xkb.size);
   return reply;
}

static void
_e_info_server_module_hook_call(const char *module_name, const char *log_path)
{
   Eina_List *l;
   E_Info_Hook *data;

   EINA_LIST_FOREACH(module_hook, l, data)
     {
        if (!strncmp(data->module_name, module_name, strlen(module_name)))
          {
             data->func(data->data, log_path);
             break;
          }
     }
}

static void
_e_info_server_module_hook_cleanup(void)
{
   E_Info_Hook *hdata;

   EINA_LIST_FREE(module_hook, hdata)
     {
        eina_stringshare_del(hdata->module_name);
        E_FREE(hdata);
     }
}

/* a hook with given name(module_name) is defined by plug-in modules*/
E_API void
e_info_server_hook_set(const char *module_name, E_Info_Hook_Cb func, void *data)
{
   Eina_List *l, *l_next;
   E_Info_Hook *hdata, *ndata;

   EINA_SAFETY_ON_NULL_RETURN(module_name);

   EINA_LIST_FOREACH_SAFE(module_hook, l, l_next, hdata)
     {
        if (!strncmp(hdata->module_name, module_name, strlen(module_name)))
          {
             if (!func)
               {
                  eina_stringshare_del(hdata->module_name);
                  E_FREE(hdata);
                  module_hook = eina_list_remove_list(module_hook, l);
               }
             else
               {
                  hdata->func = func;
                  hdata->data = data;
               }
             return;
          }
     }

   ndata = E_NEW(E_Info_Hook, 1);
   EINA_SAFETY_ON_NULL_RETURN(ndata);

   ndata->module_name = eina_stringshare_add(module_name);
   ndata->func = func;
   ndata->data = data;

   module_hook = eina_list_append(module_hook, ndata);
}

static Eldbus_Message *
_e_info_server_cb_module_info_get(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   const char *path = NULL, *module_name = NULL;

   if (!eldbus_message_arguments_get(msg, "ss", &module_name, &path) || !module_name || !path)
     {
        ERR("Error getting arguments.");
        return reply;
     }

   _e_info_server_module_hook_call(module_name, path);

   return reply;
}

static Eldbus_Message *
_e_info_server_cb_keygrab_status_get(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   const char *path = NULL;

   if (!eldbus_message_arguments_get(msg, "s", &path) || !path)
     {
        ERR("Error getting arguments.");
        return reply;
     }

   _e_info_server_module_hook_call("keygrab", path);

   return reply;
}

static Eldbus_Message *
_e_info_server_cb_bgcolor_set(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   int a, r, g, b;
   int pa, pr, pg, pb;
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);

   EINA_SAFETY_ON_NULL_RETURN_VAL(reply, NULL);

   if (!eldbus_message_arguments_get(msg, "iiii", &a, &r, &g, &b))
     {
        ERR("Error on getting argument from the given message.");
        return reply;
     }

   evas_object_color_get(e_comp->bg_blank_object, &pa, &pr, &pg, &pb);
   evas_object_color_set(e_comp->bg_blank_object, r, g, b, a);

   INF("The background color of bg_blank_object has been changed.");
   INF("(A, R, G, B) : %d, %d, %d, %d -> %d, %d, %d, %d", pa, pr, pg, pb, a, r, g, b);

   return reply;
}

static Eldbus_Message *
_e_info_server_cb_punch(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   int onoff = 0, x = 0, y = 0, w = 0, h = 0;
   int a = 0, r = 0, g = 0, b = 0;

   if (!eldbus_message_arguments_get(msg, "iiiiiiiii", &onoff, &x, &y, &w, &h, &a, &r, &g, &b))
     {
        ERR("Error getting arguments.");
        return reply;
     }

   if (onoff)
     e_video_debug_screen_punch_set(x, y, w, h, a, r, g, b);
   else
     e_video_debug_screen_punch_unset();

   return reply;
}

static Eldbus_Message *
e_info_server_cb_transform_message(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   uint32_t enable, transform_id;
   uint32_t x, y, sx, sy, degree;
   uint32_t background;
   unsigned long tmp = 0;
   const char *value = NULL;
   uint64_t value_number = 0;
   Evas_Object *o;
   E_Client *ec;
   Eina_Bool res = EINA_FALSE;

   if (!eldbus_message_arguments_get(msg, "siiiiiiii", &value, &transform_id, &enable, &x, &y, &sx, &sy, &degree, &background))
     {
        ERR("Error getting arguments.");
        return reply;
     }

   if (strlen(value) >= 2 && value[0] == '0' && value[1] == 'x')
     res = e_util_string_to_ulong(value, &tmp, 16);
   else
     res = e_util_string_to_ulong(value, &tmp, 10);

   EINA_SAFETY_ON_FALSE_RETURN_VAL(res, reply);

   value_number = (uint64_t)tmp;

   for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
     {
        ec = evas_object_data_get(o, "E_Client");
        Ecore_Window win;
        E_Info_Transform *transform_info;

        if (!ec) continue;

        win = e_client_util_win_get(ec);

        if (win != value_number) continue;
        transform_info = _e_info_transform_find(ec, transform_id);

        if (transform_info)
          {
             _e_info_transform_set(transform_info, enable, x, y, sx, sy, degree);

             if (!enable)
                _e_info_transform_del_with_id(ec, transform_id);
          }
        else
          {
             if (enable)
               {
                  _e_info_transform_new(ec, transform_id, enable, x, y, sx, sy, degree, background);
               }
          }

        break;
     }

   return reply;
}

static Eldbus_Message *
e_info_server_cb_slot_message(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   int32_t param[5];

   uint32_t slot_id = 0;
   uint32_t x, y, w, h, mode;
   int32_t win_id = 0;
   Eina_Bool start_split = EINA_FALSE;
   Evas_Object *o, *slot;
   E_Client *ec = NULL;
   Ecore_Window win;

   Eldbus_Message_Iter* struct_of_ec;
   Eldbus_Message_Iter *array_of_ec;
   Eldbus_Message_Iter *iter;

   if (!eldbus_message_arguments_get(msg, "iiiiii", &mode, &param[0], &param[1], &param[2], &param[3], &param[4]))
     {
        ERR("Error getting arguments.");
        return reply;
     }

   if (mode == E_INFO_CMD_MESSAGE_CREATE)
     {
        x = param[0];
        y = param[1];
        w = param[2];
        h = param[3];
     }
   else if (mode == E_INFO_CMD_MESSAGE_MODIFY)
     {
        slot_id = param[0];
        x = param[1];
        y = param[2];
        w = param[3];
        h = param[4];
     }
   else if (mode == E_INFO_CMD_MESSAGE_ADD_EC_TRANSFORM ||
            mode == E_INFO_CMD_MESSAGE_ADD_EC_RESIZE ||
            mode == E_INFO_CMD_MESSAGE_DEL_EC)
     {
        slot_id = param[0];
        win_id = param[1];
     }
   else if (mode == E_INFO_CMD_MESSAGE_START)
      {
         start_split = !!param[0];
      }
   else
     {
        slot_id = param[0];
     }

   iter = eldbus_message_iter_get(reply);
   eldbus_message_iter_arguments_append(iter, "a(ss)", &array_of_ec);
#define __SLOT_ARG_APPEND_TYPE(title, str, x...) ({                           \
                                                  char __temp[128] = {0,};                                                     \
                                                  snprintf(__temp, sizeof(__temp), str, ##x);                                  \
                                                  eldbus_message_iter_arguments_append(array_of_ec, "(ss)", &struct_of_ec);    \
                                                  eldbus_message_iter_arguments_append(struct_of_ec, "ss", (title), (__temp)); \
                                                  eldbus_message_iter_container_close(array_of_ec, struct_of_ec);})

   if (mode == E_INFO_CMD_MESSAGE_LIST)
     {
        Eina_List *slot_list;
        slot_list = e_slot_list_get();

        if (slot_list)
          {
             Eina_List *l;
             EINA_LIST_FOREACH(slot_list, l, slot)
               {
                  if (slot)
                    {
                       int id = e_slot_find_id(slot);
                       int eo_x, eo_y, eo_w, eo_h;
                       evas_object_geometry_get(slot,&eo_x,&eo_y,&eo_w,&eo_h);
                       __SLOT_ARG_APPEND_TYPE("[SLOT LIST]", "slot_id:%02d (%04d,%04d,%04dx%04d) \n", id, eo_x, eo_y, eo_w, eo_h);

                       if (id)
                         {
                            Eina_List *ll, *clist;
                            E_Client *ec = NULL;
                            clist = e_slot_client_list_get(slot);
                            EINA_LIST_FOREACH(clist, ll, ec)
                              {
                                 if (ec)
                                   __SLOT_ARG_APPEND_TYPE("[SLOT CLIENT]", "slot_client win:%08zx name:%s \n", e_client_util_win_get(ec), e_client_util_name_get(ec) ?: "NO NAME");
                              }
                         }
                    }
               }
          }
        else
          {
             __SLOT_ARG_APPEND_TYPE("[SLOT INFO]", "No slot.....\n");
          }
     }
   else if (mode == E_INFO_CMD_MESSAGE_CREATE)
     {
        E_Zone *zone = e_zone_current_get();
        E_Desk *desk = e_desk_current_get(zone);
        EINA_SAFETY_ON_NULL_GOTO(desk, finish);

        slot = e_slot_new(desk->layout);
        evas_object_move(slot, x, y);
        evas_object_resize(slot, w, h);
        __SLOT_ARG_APPEND_TYPE("[SLOT INFO]", "[SLOT CREATE]  slot_id:%02d (%04d,%04d,%04dx%04d)\n", e_slot_find_id(slot), x, y, w, h );
     }
   else if (mode == E_INFO_CMD_MESSAGE_MODIFY)
     {
        slot = e_slot_find_by_id(slot_id);
        if (!slot) __SLOT_ARG_APPEND_TYPE("[SLOT INFO]", "no such slot(id %d)\n",slot_id);
        else
          {
             evas_object_move(slot, x, y);
             evas_object_resize(slot, w, h);
             __SLOT_ARG_APPEND_TYPE("[SLOT INFO]", "[SLOT MODIFY]  slot_id:%02d (%04d,%04d,%04dx%04d)\n", slot_id, x, y, w, h );
          }
     }
   else if (mode == E_INFO_CMD_MESSAGE_DEL)
     {
        slot = e_slot_find_by_id(slot_id);
        if (!slot) __SLOT_ARG_APPEND_TYPE("[SLOT INFO]", "no such slot(id %d)\n", slot_id);
        else
          {
             //e_object_del(E_OBJECT(slot));
             e_slot_del(slot);
             __SLOT_ARG_APPEND_TYPE("[SLOT INFO]","[SLOT DEL]  slot_id:%02d\n", slot_id);
          }
     }
   else if (mode == E_INFO_CMD_MESSAGE_RAISE)
     {
        slot = e_slot_find_by_id(slot_id);
        if (!slot) __SLOT_ARG_APPEND_TYPE("[SLOT INFO]", "no such slot(id %d)\n", slot_id);
        else
          {
             e_slot_raise(slot);
             e_slot_update(slot);
             __SLOT_ARG_APPEND_TYPE("[SLOT INFO]","[SLOT RAISE]  slot_id:%02d\n", slot_id);
          }
     }
   else if (mode == E_INFO_CMD_MESSAGE_LOWER)
     {
        slot = e_slot_find_by_id(slot_id);
        if (!slot) __SLOT_ARG_APPEND_TYPE("[SLOT INFO]", "no such slot(id %d)\n", slot_id);
        else
          {
             e_slot_lower(slot);
             e_slot_update(slot);
             __SLOT_ARG_APPEND_TYPE("[SLOT INFO]","[SLOT LOWER]  slot_id:%02d\n", slot_id);
          }
     }
   else if (mode == E_INFO_CMD_MESSAGE_ADD_EC_TRANSFORM)
     {
        slot = e_slot_find_by_id(slot_id);
        for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
          {
             ec = evas_object_data_get(o, "E_Client");
             if (!ec) continue;
             win = e_client_util_win_get(ec);
             if (win != win_id) continue;
             break;
          }

        if (ec)
        {
          e_slot_client_add(slot, ec, 0);
          e_slot_client_update(ec);
          __SLOT_ARG_APPEND_TYPE("[SLOT INFO]", "[SLOT ADD EC as transform] slot_id:%02d (%08zx)\n", slot_id, win);
        }
     }
   else if (mode == E_INFO_CMD_MESSAGE_DEL_EC)
     {
        slot = e_slot_find_by_id(slot_id);
        for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
          {
             ec = evas_object_data_get(o, "E_Client");
             if (!ec) continue;
             win = e_client_util_win_get(ec);
             if (win != win_id) continue;
             break;
          }

        if (ec)
          {
             e_slot_client_remove(slot, ec);
             __SLOT_ARG_APPEND_TYPE("[SLOT INFO]", "[SLOT DEL EC] slot_id:%02d (%08zx)\n", slot_id, win);
          }
     }
   else if (mode == E_INFO_CMD_MESSAGE_ADD_EC_RESIZE)
     {
        slot = e_slot_find_by_id(slot_id);
        for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
          {
             ec = evas_object_data_get(o, "E_Client");
             if (!ec) continue;
             win = e_client_util_win_get(ec);
             if (win != win_id) continue;
             break;
          }

        if (ec)
        {
           e_slot_client_add(slot, ec, 1);
           e_slot_client_update(ec);
           __SLOT_ARG_APPEND_TYPE("[SLOT INFO]", "[SLOT ADD EC as resize] slot_id:%02d (%08zx)\n", slot_id, win);
        }
     }
   else if (mode == E_INFO_CMD_MESSAGE_FOCUS)
     {
        slot = e_slot_find_by_id(slot_id);
        if (!slot) __SLOT_ARG_APPEND_TYPE("[SLOT INFO]", "no such slot(id %d)\n", slot_id);
        else
          {
             e_slot_focus_set(slot);
             __SLOT_ARG_APPEND_TYPE("[SLOT INFO]", "[SLOT FOCUS SET]  slot_id:%02d\n", slot_id);
          }
     }
   else if (mode == E_INFO_CMD_MESSAGE_START)
     {
        E_Zone *zone = e_zone_current_get();
        E_Desk *desk = e_desk_current_get(zone);
        EINA_SAFETY_ON_NULL_GOTO(desk, finish);

        if (start_split) evas_object_raise(desk->layout);
        else evas_object_lower(desk->layout);
        //evas_object_show(desk->layout);
        __SLOT_ARG_APPEND_TYPE("[SLOT INFO]", "[SLOT %s]", start_split ? "START" : "STOP");
     }
   else
     {
        __SLOT_ARG_APPEND_TYPE("[SLOT INFO]", "Wrong command........\n");
     }

finish:
   eldbus_message_iter_container_close(iter, array_of_ec);
   return reply;
}

static Eldbus_Message *
_e_info_server_cb_desktop_geometry_set(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   E_Zone *zone;
   E_Desk *desk;
   int x, y, w, h;

   if (!eldbus_message_arguments_get(msg, "iiii", &x, &y, &w, &h))
     {
        ERR("Error getting arguments.");
        return reply;
     }

   if ((w < 0) || (h < 0))
     {
        ERR("Error: Invalid parameter w %d h %d", w, h);
        return reply;
     }

   zone = e_zone_current_get();
   desk = e_desk_current_get(zone);
   e_desk_geometry_set(desk, x, y, w, h);

   return reply;
}

static Eldbus_Message *
_e_info_server_cb_desk_zoom(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   E_Zone *zone;
   E_Desk *desk;
   double zx, zy;
   int cx, cy;

   if (!eldbus_message_arguments_get(msg, "ddii", &zx, &zy, &cx, &cy))
     {
        ERR("Error getting arguments.");
        return reply;
     }

   zone = e_zone_current_get();
   desk = e_desk_current_get(zone);

   if ((zx != 1.0) || (zy != 1.0))
     e_desk_zoom_set(desk, zx, zy, cx, cy);
   else
     e_desk_zoom_unset(desk);

   return reply;
}

static Eina_Bool
_e_info_server_cb_buffer_change(void *data, int type, void *event)
{
   E_Client *ec;
   E_Event_Client *ev = event;
   Ecore_Window event_win;
   char fname[PATH_MAX];
   E_Comp_Wl_Buffer *buffer;
   tbm_surface_h tbm_surface;
   struct wl_shm_buffer *shmbuffer = NULL;
   void *ptr;
   int stride, w, h, rotation, row, col;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ev, ECORE_CALLBACK_PASS_ON);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev->ec, ECORE_CALLBACK_PASS_ON);

   ec = ev->ec;
   if (e_object_is_del(E_OBJECT(ec)))
     {
        ERR("%s: e_object_is_del(E_OBJECT(ec) return\n", __func__);
        return ECORE_CALLBACK_PASS_ON;
     }
   if (e_client_util_ignored_get(ec))
     {
        if (!e_info_dump_remote_surface || !ec->remote_surface.provider)
          {
             ERR("%s: e_client_util_ignored_get(ec) true. return\n", __func__);
             return ECORE_CALLBACK_PASS_ON;
          }
     }

   buffer = e_pixmap_resource_get(ec->pixmap);
   if (!buffer) return ECORE_CALLBACK_PASS_ON;

   rotation = ec->comp_data->scaler.buffer_viewport.buffer.transform * 90;

   event_win = e_client_util_win_get(ec);
   switch (buffer->type)
     {
      case E_COMP_WL_BUFFER_TYPE_SHM:
        snprintf(fname, sizeof(fname), "buffer_commit_shm_0x%08zx_%d", event_win, rotation);
        break;
      case E_COMP_WL_BUFFER_TYPE_NATIVE:
        snprintf(fname, sizeof(fname), "buffer_commit_native_0x%08zx_%d", event_win, rotation);
        break;
      case E_COMP_WL_BUFFER_TYPE_VIDEO:
        snprintf(fname, sizeof(fname), "buffer_commit_video_0x%08zx_%d", event_win, rotation);
        break;
      case E_COMP_WL_BUFFER_TYPE_TBM:
        snprintf(fname, sizeof(fname), "buffer_commit_tbm_0x%08zx_%d", event_win, rotation);
        break;
      default:
        snprintf(fname, sizeof(fname), "buffer_commit_none_0x%08zx_%d", event_win, rotation);
        break;
     }

   switch (buffer->type)
     {
      case E_COMP_WL_BUFFER_TYPE_SHM:
        shmbuffer = wl_shm_buffer_get(buffer->resource);
        EINA_SAFETY_ON_NULL_RETURN_VAL(shmbuffer, ECORE_CALLBACK_PASS_ON);

        ptr = wl_shm_buffer_get_data(shmbuffer);
        EINA_SAFETY_ON_NULL_RETURN_VAL(ptr, ECORE_CALLBACK_PASS_ON);

        stride = wl_shm_buffer_get_stride(shmbuffer);
        w = stride / 4;
        h = wl_shm_buffer_get_height(shmbuffer);
        tbm_surface_internal_dump_shm_buffer(ptr, w, h, stride, fname);
        break;
      case E_COMP_WL_BUFFER_TYPE_NATIVE:
      case E_COMP_WL_BUFFER_TYPE_VIDEO:
      case E_COMP_WL_BUFFER_TYPE_TBM:
        tbm_surface = wayland_tbm_server_get_surface(NULL, buffer->resource);
        EINA_SAFETY_ON_NULL_RETURN_VAL(tbm_surface, ECORE_CALLBACK_PASS_ON);

        if (e_info_dump_mark)
          {
             unsigned int colors[5] = {0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFF00FFFF, 0xFFFF00FF};
             tdm_pos pos;
             int box_size = 20;
             int box = e_info_dump_mark_count * box_size;

             w = tbm_surface_get_width(tbm_surface);
             h = tbm_surface_get_height(tbm_surface);

             EINA_SAFETY_ON_FALSE_RETURN_VAL((w != 0), ECORE_CALLBACK_PASS_ON);
             EINA_SAFETY_ON_FALSE_RETURN_VAL((h != 0), ECORE_CALLBACK_PASS_ON);

             row = (((box / w) * box_size) % h);
             col = box % w;

             pos.x = col;
             pos.y = row;
             pos.w = box_size;
             pos.h = box_size;

             tdm_helper_clear_buffer_color(tbm_surface, &pos, colors[e_info_dump_mark_count % 5]);
             e_info_dump_mark_count++;
          }

        tbm_surface_internal_dump_buffer(tbm_surface, fname);
        break;
      default:
        DBG("Unknown type resource:%u", wl_resource_get_id(buffer->resource));
        break;
     }
   DBG("%s dump excute\n", fname);

   return ECORE_CALLBACK_PASS_ON;
}

static char *
_e_info_server_dump_directory_make(const char *path)
{
   char *fullpath;
   time_t timer;
   struct tm *t, *buf;

   timer = time(NULL);

   buf = calloc (1, sizeof (struct tm));
   EINA_SAFETY_ON_NULL_RETURN_VAL(buf, NULL);
   t = localtime_r(&timer, buf);
   if (!t)
     {
        free(buf);
        ERR("fail to get local time\n");
        return NULL;
     }

   fullpath = (char *)calloc(1, PATH_MAX * sizeof(char));
   if (!fullpath)
     {
        free(buf);
        ERR("fail to alloc pathname memory\n");
        return NULL;
     }

   snprintf(fullpath, PATH_MAX, "%s/dump_%04d%02d%02d.%02d%02d%02d", path,
            t->tm_year+1900, t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);

   free(buf);

   if ((mkdir(fullpath, 0755)) < 0)
     {
        ERR("%s: mkdir '%s' fail\n", __func__, fullpath);
        free(fullpath);
        return NULL;
     }

   return fullpath;
}

static Eldbus_Message *
_e_info_server_cb_buffer_dump(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply;
   int start = 0;
   int count = 0;
   const char *path = NULL;
   double scale;
   int ret = 0;

   if (!eldbus_message_arguments_get(msg, "iisdi", &start, &count, &path, &scale, &e_info_dump_mark))
     {
        return eldbus_message_error_new(msg, GET_CALL_MSG_ARG_ERR,
                                        "dump_buffers: an attempt to get arguments from method call message failed");
     }

   reply = eldbus_message_method_return_new(msg);

   if (start == 1)
     {
        if (e_info_dump_running == 1)
          {
             eldbus_message_arguments_append(reply, "is", ret, (e_info_dump_path ?: "nopath"));
             return reply;
          }
        e_info_dump_running = 1;
        e_info_dump_mark_count = 0;
        e_info_dump_count = 1;
        e_info_dump_path = _e_info_server_dump_directory_make(path);
        if (e_info_dump_path == NULL)
          {
             e_info_dump_running = 0;
             e_info_dump_count = 0;
             ERR("dump_buffers start fail\n");
             ret = -1;
          }
        else
          {
             /* start dump */
             if (scale > 0.0)
               tbm_surface_internal_dump_with_scale_start(e_info_dump_path,
                                                          e_comp->w,
                                                          e_comp->h,
                                                          count, scale);
             else
                tbm_surface_internal_dump_start(e_info_dump_path, e_comp->w, e_comp->h, count);
             tdm_helper_dump_start(e_info_dump_path, &e_info_dump_count);
             e_hwc_windows_dump_start();
             E_LIST_HANDLER_APPEND(e_info_dump_hdlrs, E_EVENT_CLIENT_BUFFER_CHANGE,
                               _e_info_server_cb_buffer_change, NULL);
          }
          eldbus_message_arguments_append(reply, "is", ret, (e_info_dump_path ?: "nopath"));
     }
   else
     {
        if (e_info_dump_running == 0)
          {
             eldbus_message_arguments_append(reply, "is", ret, (e_info_dump_path ?: "nopath"));
             return reply;
          }
        e_info_server_hook_call(E_INFO_SERVER_HOOK_BUFFER_DUMP_BEGIN);
        tdm_helper_dump_stop();
        e_hwc_windows_dump_stop();
        tbm_surface_internal_dump_end();

        eldbus_message_arguments_append(reply, "is", ret, (e_info_dump_path ?: "nopath"));

        E_FREE_LIST(e_info_dump_hdlrs, ecore_event_handler_del);
        e_info_dump_hdlrs = NULL;
        if (e_info_dump_path)
          {
             free(e_info_dump_path);
             e_info_dump_path = NULL;
          }
        e_info_dump_count = 0;
        e_info_dump_running = 0;
        e_info_dump_mark_count = 0;
        e_info_server_hook_call(E_INFO_SERVER_HOOK_BUFFER_DUMP_END);
     }

   return reply;
}

static Eldbus_Message *
_e_info_server_cb_selected_buffer_dump(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   const char *win_id_s = NULL;
   const char *path = NULL;
   int32_t win_id = 0;
   Evas_Object *o;
   int ret;

   Dump_Win_Data *dump = NULL;
   E_Capture_Save_State state;

   if (!eldbus_message_arguments_get(msg, "ss", &win_id_s, &path))
     {
        ERR("Error getting arguments.");
        return reply;
     }

    if (!win_id_s) win_id = 0;
    else
      {
         if (strlen(win_id_s) >= 2 && win_id_s[0] == '0' && win_id_s[1] == 'x')
           ret = sscanf(win_id_s, "%zx", (uintptr_t *)&win_id);
         else
           ret = sscanf(win_id_s, "%d", &win_id);
         EINA_SAFETY_ON_FALSE_GOTO(ret == 1, end);
      }

   for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
     {
        E_Client *ec = evas_object_data_get(o, "E_Client");
        Ecore_Window win;

        char fname[PATH_MAX];
        Eina_Stringshare *s_fname, *s_path;

        if (!ec) continue;
        if (e_client_util_ignored_get(ec)) continue;

        win = e_client_util_win_get(ec);

        if (win_id != win) continue;

        dump = E_NEW(Dump_Win_Data, 1);
        EINA_SAFETY_ON_NULL_RETURN_VAL(dump, reply);

        dump->num = 1;
        dump->reply = reply;

        snprintf(fname, sizeof(fname), "0x%08zx", win);

        s_fname = eina_stringshare_add(fname);
        s_path = eina_stringshare_add(path);

        state = e_client_image_save(ec, s_path, s_fname, _image_save_done_cb, dump, EINA_TRUE);

        eina_stringshare_del(s_path);
        eina_stringshare_del(s_fname);

        //creation of window dump job succeeded, reply will be sent after dump ends.
        if (state == E_CAPTURE_SAVE_STATE_START)
          return NULL;

        break;
     }

   if (dump)
     E_FREE(dump);

end:
   //send reply with error msg because dump job failed.
   eldbus_message_arguments_append(reply, "s", "ERR: Can't start dump job");
   return reply;
}

static void
_e_info_server_cb_screen_dump_cb(E_Output *eout, tbm_surface_h surface, void *user_data)
{
   char *path = (char *)user_data;

   tdm_helper_dump_buffer(surface, path);

   free(path);
   tbm_surface_destroy(surface);

   DBG("_e_info_server_cb_screen_dump_cb done");
}

static Eldbus_Message *
_e_info_server_cb_screen_dump(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   const char *path = NULL;
   tbm_surface_h surface = NULL;
   E_Output *eout = NULL;
   int w = 0, h = 0;
   Eina_Bool ret = EINA_FALSE;
   char *path_backup = NULL;

   if (!eldbus_message_arguments_get(msg, "s", &path))
     {
        ERR("Error getting arguments.");
        return reply;
     }

   eout = e_output_find_by_index(0);
   if (eout == NULL)
     {
        ERR("Error get main outpute.");
        return reply;
     }
   e_output_size_get(eout, &w, &h);

   surface = tbm_surface_create(w, h, TBM_FORMAT_ARGB8888);
   if (!surface)
     {
        ERR("Error create tbm_surface.");
        return reply;
     }

   path_backup = (char *)calloc(1, PATH_MAX * sizeof(char));
   if (path_backup == NULL)
     {
        ERR("Error alloc.");
        return reply;
     }
   strncpy(path_backup, path, PATH_MAX);

   ret = e_output_capture(eout, surface, EINA_FALSE, EINA_TRUE, _e_info_server_cb_screen_dump_cb, path_backup);
   if (ret)
     return reply;
   else
     ERR("Error fail capture.");

   free(path_backup);
   tbm_surface_destroy(surface);

   return reply;
}

static void
_output_mode_msg_clients_append(Eldbus_Message_Iter *iter, E_Comp_Screen *e_comp_screen, int gl, int mode, int mode_count)
{
   E_Output *primary_output = NULL;
   Eldbus_Message_Iter *array_of_mode;
   Eldbus_Message_Iter *struct_of_mode;
   E_Output_Mode *set_mode = NULL;
   int i, count;

   eldbus_message_iter_arguments_append(iter, "a("SIGNATURE_OUTPUT_MODE_SERVER")",
                                        &array_of_mode);

   if (gl == 0)
     {
        eldbus_message_iter_arguments_append(array_of_mode, "("SIGNATURE_OUTPUT_MODE_SERVER")",
                                             &struct_of_mode);
        eldbus_message_iter_arguments_append(struct_of_mode, SIGNATURE_OUTPUT_MODE_SERVER,
                                             0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "none",
                                             0, 0, 0, 0, TDM_OUTPUT_DPMS_OFF);
        eldbus_message_iter_container_close(array_of_mode, struct_of_mode);

        eldbus_message_iter_container_close(iter, array_of_mode);

        return;
     }

   if (mode == E_INFO_CMD_OUTPUT_MODE_SET)
     {
        E_Output_Mode *emode = NULL;
        Eina_List *modelist = NULL, *l = NULL;
        int num;

        primary_output = e_comp_screen_primary_output_get(e_comp->e_comp_screen);

        modelist = e_output_mode_list_get(primary_output);
        if (modelist)
          {
             num = eina_list_count(modelist);
             if (mode_count >= 0 && mode_count < num)
               {
                  count = 0;
                  EINA_LIST_FOREACH(modelist, l, emode)
                    {
                       if (count == mode_count)
                         {
                            set_mode = emode;
                            break;
                         }
                       count++;
                    }
               }

             if (set_mode)
               e_output_mode_change(primary_output, set_mode);
          }
     }

   count = e_comp_screen->num_outputs;

   for (i = 0; i < count; i++)
     {
        E_Output *eout = e_output_find_by_index(i);
        E_Output_Mode *current_mode = NULL;
        E_Output_Mode *emode = NULL;
        Eina_List *modelist = NULL, *l = NULL;
        const tdm_output_mode *tmode = NULL;
        int current;
        unsigned int preferred;
        int dpms;

        if (eout == NULL) continue;

        if (e_output_connected(eout) == EINA_FALSE)
          {
             eldbus_message_iter_arguments_append(array_of_mode, "("SIGNATURE_OUTPUT_MODE_SERVER")",
                                                  &struct_of_mode);
             eldbus_message_iter_arguments_append(struct_of_mode, SIGNATURE_OUTPUT_MODE_SERVER,
                                                  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "none",
                                                  0, i, 0, 1, TDM_OUTPUT_DPMS_OFF);
             eldbus_message_iter_container_close(array_of_mode, struct_of_mode);

             continue;
          }

        current_mode = e_output_current_mode_get(eout);
        modelist = e_output_mode_list_get(eout);
        if (modelist == NULL) continue;

        EINA_LIST_FOREACH(modelist, l, emode)
          {
             if (emode == NULL) continue;

             tmode = emode->tmode;

             if (tmode->type & TDM_OUTPUT_MODE_TYPE_PREFERRED) preferred = 1;
             else preferred = 0;

             if (emode == current_mode) current = 1;
             else current = 0;

             dpms = e_output_dpms_get(eout);

             eldbus_message_iter_arguments_append(array_of_mode, "("SIGNATURE_OUTPUT_MODE_SERVER")",
                                                  &struct_of_mode);
             eldbus_message_iter_arguments_append(struct_of_mode, SIGNATURE_OUTPUT_MODE_SERVER,
                                                  tmode->hdisplay, tmode->hsync_start, tmode->hsync_end, tmode->htotal,
                                                  tmode->vdisplay, tmode->vsync_start, tmode->vsync_end, tmode->vtotal,
                                                  tmode->vrefresh, tmode->vscan, tmode->clock, preferred, tmode->name,
                                                  current, i, 1, 1, dpms);
             eldbus_message_iter_container_close(array_of_mode, struct_of_mode);
          }
     }

   eldbus_message_iter_container_close(iter, array_of_mode);
}

static Eldbus_Message *
_e_info_server_cb_output_mode(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   E_Comp_Screen *e_comp_screen = NULL;
   tdm_display *tdpy = NULL;
   int mode = 0;
   int count = 0;

   if (!eldbus_message_arguments_get(msg, SIGNATURE_OUTPUT_MODE_CLIENT, &mode, &count))
     {
        ERR("Error getting arguments.");
        return reply;
     }

   if ((mode == E_INFO_CMD_OUTPUT_MODE_GET) ||
       (mode == E_INFO_CMD_OUTPUT_MODE_SET))
     {
        e_comp_screen = e_comp->e_comp_screen;
        tdpy = e_comp_screen->tdisplay;

        if (tdpy != NULL)
          _output_mode_msg_clients_append(eldbus_message_iter_get(reply), e_comp_screen, 1, mode, count);
        else
          _output_mode_msg_clients_append(eldbus_message_iter_get(reply), e_comp_screen, 0, 0, 0);
     }

   return reply;
}

static Eldbus_Message *
e_info_server_cb_hwc_trace_message(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   uint32_t onoff;

   if (!eldbus_message_arguments_get(msg, "i", &onoff))
     {
        ERR("Error getting arguments.");
        return reply;
     }

   if (onoff == 0 || onoff == 1)
     {
        e_plane_hwc_trace_debug(onoff);
        e_hwc_windows_trace_debug(onoff);
     }

   if (onoff == 2)
     e_comp_screen_hwc_info_debug();

   return reply;
}

static Eldbus_Message *
e_info_server_cb_serial_trace_message(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   uint32_t on;

   if (!eldbus_message_arguments_get(msg, "i", &on))
     {
        ERR("Error getting arguments.");
        return reply;
     }

   if (on == 0 || on == 1)
     e_comp_wl_trace_serial_debug(on);

   return reply;
}

static Eldbus_Message *
e_info_server_cb_hwc(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   uint32_t info;

   if (!eldbus_message_arguments_get(msg, "i", &info))
     {
        ERR("Error getting arguments.");
        return reply;
     }

   if (!e_comp->hwc)
     {
        ERR("Error HWC is not initialized.");
        return reply;
     }

   switch (info)
     {
      case 0:
        e_comp_hwc_deactive_set(EINA_TRUE);
        break;

      case 1:
        e_comp_hwc_deactive_set(EINA_FALSE);
        break;

      default:
      case 2:
        e_comp_screen_hwc_info_debug();
        break;
     }

   return reply;
}

static Eldbus_Message *
e_info_server_cb_show_plane_state(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eina_List *output_l, *plane_l;
   E_Comp_Screen *e_comp_screen = NULL;
   E_Output *output = NULL;
   E_Plane *plane = NULL;
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);

   e_comp_screen = e_comp->e_comp_screen;

   EINA_LIST_FOREACH(e_comp_screen->outputs, output_l, output)
     {
        if (!output) continue;

        EINA_LIST_FOREACH(output->planes, plane_l, plane)
          {
             if (!plane) continue;

             e_plane_show_state(plane);
          }
     }

   return reply;
}

static void
_msg_show_pending_commit_append(Eldbus_Message_Iter *iter)
{
   Eina_List *output_l, *plane_l, *data_l;
   Eldbus_Message_Iter *array_of_pending_commit;
   E_Comp_Screen *e_comp_screen = NULL;
   E_Output *output = NULL;
   E_Plane *plane = NULL;
   E_Plane_Commit_Data *data = NULL;

   eldbus_message_iter_arguments_append(iter, "a("VALUE_TYPE_FOR_PENDING_COMMIT")", &array_of_pending_commit);

   e_comp_screen = e_comp->e_comp_screen;

   EINA_LIST_FOREACH(e_comp_screen->outputs, output_l, output)
     {
        if (!output) continue;

        EINA_LIST_FOREACH(output->planes, plane_l, plane)
          {
             if (!plane) continue;

             EINA_LIST_FOREACH(plane->commit_data_list, data_l, data)
               {
                  Eldbus_Message_Iter* struct_of_pending_commit;

                  if (!data) continue;

                  eldbus_message_iter_arguments_append(array_of_pending_commit, "("VALUE_TYPE_FOR_PENDING_COMMIT")", &struct_of_pending_commit);

                  eldbus_message_iter_arguments_append
                    (struct_of_pending_commit, VALUE_TYPE_FOR_PENDING_COMMIT,
                      (uintptr_t)plane,
                      plane->zpos,
                      (uintptr_t)data,
                      (uintptr_t)data->tsurface);

                  eldbus_message_iter_container_close(array_of_pending_commit, struct_of_pending_commit);
               }
          }
     }

   eldbus_message_iter_container_close(iter, array_of_pending_commit);
}

static Eldbus_Message *
e_info_server_cb_show_pending_commit(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);

   _msg_show_pending_commit_append(eldbus_message_iter_get(reply));

   return reply;
}

static void
_msg_fps_append(Eldbus_Message_Iter *iter)
{
   Eina_List *output_l, *plane_l, *hwc_l;
   Eldbus_Message_Iter *array_of_fps;
   E_Comp_Screen *e_comp_screen = NULL;
   E_Hwc_Window *hwc_window = NULL;
   E_Output *output = NULL;
   E_Plane *plane = NULL;
   double fps = 0.0;
   char output_name[30];

   eldbus_message_iter_arguments_append(iter, "a("VALUE_TYPE_FOR_FPS")", &array_of_fps);

   e_comp_screen = e_comp->e_comp_screen;

   EINA_LIST_FOREACH(e_comp_screen->outputs, output_l, output)
     {
        if (!output) continue;

        strncpy(output_name, output->id, sizeof(char)*29);

        if (output->hwc)
          {
             if (e_hwc_policy_get(output->hwc) == E_HWC_POLICY_WINDOWS)
               {
                  if (e_hwc_windows_fps_get(output->hwc, &fps))
                    {
                       Eldbus_Message_Iter* struct_of_fps;

                       eldbus_message_iter_arguments_append(array_of_fps, "("VALUE_TYPE_FOR_FPS")", &struct_of_fps);

                       eldbus_message_iter_arguments_append
                           (struct_of_fps, VALUE_TYPE_FOR_FPS,
                             E_INFO_FPS_TYPE_OUTPUT,
                             output_name,
                             -999,
                             0,
                             fps);

                       eldbus_message_iter_container_close(array_of_fps, struct_of_fps);
                    }

                  EINA_LIST_FOREACH(output->hwc->hwc_windows, hwc_l, hwc_window)
                    {
                       E_Hwc_Window_State state;
                       Eldbus_Message_Iter* struct_of_fps;

                       if(!hwc_window) continue;

                       state = e_hwc_window_accepted_state_get(hwc_window);
                       if ((state == E_HWC_WINDOW_STATE_CLIENT) || (state == E_HWC_WINDOW_STATE_NONE)) continue;
                       if (!e_hwc_window_fps_get(hwc_window, &fps)) continue;

                       eldbus_message_iter_arguments_append(array_of_fps, "("VALUE_TYPE_FOR_FPS")", &struct_of_fps);

                       eldbus_message_iter_arguments_append
                           (struct_of_fps, VALUE_TYPE_FOR_FPS,
                             hwc_window->is_target ? E_INFO_FPS_TYPE_HWC_COMP : E_INFO_FPS_TYPE_HWC_WIN,
                             output_name,
                             hwc_window->zpos,
                             hwc_window->is_target ? 0 : e_client_util_win_get(hwc_window->ec),
                             fps);

                       eldbus_message_iter_container_close(array_of_fps, struct_of_fps);
                    }
               }
             else
               {
                  EINA_LIST_FOREACH(output->planes, plane_l, plane)
                    {
                        if (!plane) continue;
                        if (!e_plane_fps_get(plane, &fps)) continue;

                        Eldbus_Message_Iter* struct_of_fps;

                        eldbus_message_iter_arguments_append(array_of_fps, "("VALUE_TYPE_FOR_FPS")", &struct_of_fps);

                        eldbus_message_iter_arguments_append
                          (struct_of_fps, VALUE_TYPE_FOR_FPS,
                            E_INFO_FPS_TYPE_LAYER,
                            output_name,
                            plane->zpos,
                            0,
                            plane->fps);

                        eldbus_message_iter_container_close(array_of_fps, struct_of_fps);
                    }
               }
          }
        else
          continue;

        memset(output_name, 0x0, sizeof(char)*30);
     }

   eldbus_message_iter_container_close(iter, array_of_fps);
}

static Eldbus_Message *
_e_info_server_cb_fps_info_get(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);

   if (!e_comp->calc_fps)
     {
        e_comp->calc_fps = 1;
     }

   _msg_fps_append(eldbus_message_iter_get(reply));

   return reply;
}

static Eldbus_Message *
e_info_server_cb_effect_control(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   uint32_t onoff;
   E_Module *m;

   if (!eldbus_message_arguments_get(msg, "i", &onoff))
     {
        ERR("Error getting arguments.");
        return reply;
     }

   m = e_module_find("e-mod-tizen-effect");

   if (onoff == 1)
     {
        if (!m)
          m = e_module_new("e-mod-tizen-effect");
        if (m)
          e_module_enable(m);
     }
   else if (onoff == 0)
     {
        if (m)
          {
             e_module_disable(m);
             e_object_del(E_OBJECT(m));
          }
     }

   return reply;
}

static Eldbus_Message *
e_info_server_cb_magnifier(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   uint32_t opcode;

   if (!eldbus_message_arguments_get(msg, "i", &opcode))
     {
        ERR("Error getting arguments.");
        return reply;
     }

   switch (opcode)
     {
      case 0: // magnifier off test
         e_magnifier_stand_alone_mode_set(EINA_FALSE);
         e_magnifier_hide(NULL);
         e_magnifier_del();
         break;

      case 1: // magnifier on test
         e_magnifier_new();
         e_magnifier_stand_alone_mode_set(EINA_TRUE);
         e_magnifier_show(NULL);
         break;

      case 2: // magnifier new
         e_magnifier_new();
         break;

      case 3: // magnifier del
         e_magnifier_del();
         break;

      case 4: // set stand_alone
         e_magnifier_stand_alone_mode_set(EINA_TRUE);
         break;

      case 5: // unset stand_alone
         e_magnifier_stand_alone_mode_set(EINA_FALSE);
         break;

      case 6: // magnifier show
         e_magnifier_show(NULL);
         break;

      case 7: // magnifier hide
         e_magnifier_hide(NULL);
         break;

      default:
         break;
     }

   return reply;
}

static Eldbus_Message *
e_info_server_cb_aux_message(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   Eldbus_Message_Iter *opt_iter;
   const char *win_str, *key, *val, *opt;
   Eina_List *options = NULL;
   unsigned long tmp = 0;
   uint64_t win_id = 0;
   E_Client *ec;
   Evas_Object *o;
   Eina_Bool res = EINA_FALSE;

   if (!e_policy)
     {
        ERR("e_policy is not initialized!");
        return reply;
     }

   if (!eldbus_message_arguments_get(msg, "sssa(s)", &win_str, &key, &val, &opt_iter))
     {
        ERR("Error getting arguments.");
        return reply;
     }

   while (eldbus_message_iter_get_and_next(opt_iter, 's', &opt))
     {
        const char *str;

        str = eina_stringshare_add(opt);
        options = eina_list_append(options, str);
     }

   res = e_util_string_to_ulong(win_str, &tmp, 16);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(res, reply);

   win_id = (uint64_t)tmp;

   for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
     {
        ec = evas_object_data_get(o, "E_Client");
        if (!ec) continue;

        Ecore_Window win = e_client_util_win_get(ec);

        if (win == win_id)
          {
             e_policy_aux_message_send(ec, key, val, options);
             break;
          }
     }

   EINA_LIST_FREE(options, opt)
      eina_stringshare_del(opt);

   return reply;
}

static Eldbus_Message *
_e_info_server_cb_force_render(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   E_Info_Cmd_Force_Render cmd;
   Eina_Bool res;
   char result[1024];
   E_Client *ec = NULL;

   res = eldbus_message_arguments_get(msg,
                                      "i",
                                      &cmd);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(res, reply);

   switch (cmd)
     {
      case E_INFO_CMD_FRENDER_ALL:
         E_CLIENT_FOREACH(ec)
           {
              if (ec->visible && (!ec->input_only))
                e_comp_object_damage(ec->frame, 0, 0, ec->w, ec->h);
           }
         evas_damage_rectangle_add(e_comp->evas, 0, 0, e_comp->w, e_comp->h);
         e_comp_render_queue();
         snprintf(result, sizeof(result),
                  "[Server] force rendered all clients and canvas\n");
         break;
      case E_INFO_CMD_FRENDER_CLS:
         E_CLIENT_FOREACH(ec)
           {
              if (ec->visible && (!ec->input_only))
                e_comp_object_damage(ec->frame, 0, 0, ec->w, ec->h);
           }
         e_comp_render_queue();
         snprintf(result, sizeof(result),
                  "[Server] updated clients' surface");
         break;
      case E_INFO_CMD_FRENDER_CANVAS:
         evas_damage_rectangle_add(e_comp->evas, 0, 0, e_comp->w, e_comp->h);
         snprintf(result, sizeof(result),
                  "[Server] updated canvas");
         break;
      default:
         snprintf(result, sizeof(result),
                  "[Server] Error Unknown cmd(%d) for the render force",
                  cmd);
         break;
     }

   eldbus_message_arguments_append(reply,
                                   "s",
                                   result);

   return reply;
}

static Eldbus_Message *
_e_info_server_cb_screen_rotation_pre(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   int rotation_pre;

   if (!eldbus_message_arguments_get(msg, "i", &rotation_pre))
     {
        ERR("Error getting arguments.");
        return reply;
     }

   if (!e_comp || !e_comp->e_comp_screen)
     {
        ERR("Error no screen.");
        return reply;
     }

   e_comp_screen_rotation_pre_set(e_comp->e_comp_screen, rotation_pre);

   return reply;
}

static Eldbus_Message *
_e_info_server_cb_screen_rotation(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   int rotation;

   if (!eldbus_message_arguments_get(msg, "i", &rotation))
     {
        ERR("Error getting arguments.");
        return reply;
     }

   if (!e_comp || !e_comp->e_comp_screen)
     {
        ERR("Error no screen.");
        return reply;
     }

   e_comp_screen_rotation_setting_set(e_comp->e_comp_screen, rotation);

   return reply;
}

static Eldbus_Message *
_e_info_server_cb_remote_surface(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   int dump_request, info_query;
   Eina_Bool res;
   Eldbus_Message_Iter *iter, *line_array;

   res = eldbus_message_arguments_get(msg,
                                      "ii", &dump_request, &info_query);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(res, reply);

   if (info_query)
     {
        e_comp_wl_remote_surface_debug_info_get(eldbus_message_iter_get(reply));
     }
   else if (dump_request != -1)
     {
        char reply_msg[1024] = "";

        e_info_dump_remote_surface = dump_request;

        snprintf(reply_msg, sizeof(reply_msg), "Switch %s remote surface dump",
                 dump_request? "ON":"OFF");

        iter = eldbus_message_iter_get(reply);
        eldbus_message_iter_arguments_append(iter, "as", &line_array);
        eldbus_message_iter_basic_append(line_array, 's', reply_msg);
        eldbus_message_iter_container_close(iter, line_array);
     }

   return reply;
}

static Ecore_Window
_e_info_server_top_win_at_xy_get(int x, int y)
{
   Evas_Object *o;
   E_Client *ec;

   o = evas_object_top_at_xy_get(e_comp->evas, x, y, EINA_FALSE, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(o, 0);

   ec = evas_object_data_get(o, "E_Client");
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, 0);

   return e_client_util_win_get(ec);
}

static Eina_Bool
_e_info_server_cb_ecore_event_filter(void *data, void *loop_data EINA_UNUSED, int type, void *event)
{
   Ecore_Event_Mouse_Button *e;
   Ecore_Window win;
   Ecore_Event_Filter **event_filter;

   if (type != ECORE_EVENT_MOUSE_BUTTON_DOWN && type != ECORE_EVENT_MOUSE_BUTTON_UP
       && type != ECORE_EVENT_MOUSE_MOVE && type != ECORE_EVENT_MOUSE_WHEEL
       && type != ECORE_EVENT_MOUSE_IN && type != ECORE_EVENT_MOUSE_OUT)
     return EINA_TRUE;

   if (type == ECORE_EVENT_MOUSE_BUTTON_DOWN)
     {
        e = event;
        event_filter = data;

        win = _e_info_server_top_win_at_xy_get(e->x, e->y);
        EINA_SAFETY_ON_FALSE_RETURN_VAL(win, EINA_FALSE);

        ecore_event_filter_del(*event_filter);
        free(event_filter);

        eldbus_service_signal_emit(e_info_server.iface, E_INFO_SERVER_SIGNAL_WIN_UNDER_TOUCH, (uint64_t)win);
     }

   return EINA_FALSE;
}

static Eldbus_Message *
_e_info_server_cb_get_win_under_touch(const Eldbus_Service_Interface *iface EINA_UNUSED,
                                      const Eldbus_Message *msg)
{
   int result = 0;
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   Ecore_Event_Filter **event_filter;;

   event_filter = calloc(1, sizeof(Ecore_Event_Filter *));
   EINA_SAFETY_ON_NULL_GOTO(event_filter, fail);

   *event_filter = ecore_event_filter_add(NULL, _e_info_server_cb_ecore_event_filter,
                                          NULL, event_filter);
   EINA_SAFETY_ON_NULL_GOTO(*event_filter, fail);

   goto finish;

fail:
   result = -1;
   if (event_filter)
     free(event_filter);

finish:
   eldbus_message_arguments_append(reply, "i", result);

   return reply;
}

static E_Client *
_e_info_server_ec_find_by_win(Ecore_Window win)
{
   E_Client *ec;
   Evas_Object *o;

   for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
     {
        Ecore_Window w;

        ec = evas_object_data_get(o, "E_Client");
        if (!ec) continue;

        w = e_client_util_win_get(ec);
        if (w == win)
          return ec;
     }

   return NULL;
}

const static int KILL_ID_MODE = 1;
const static int KILL_NAME_MODE = 2;
const static int KILL_PID_MODE = 3;
const static int KILL_PID_FORCE_MODE = 5;

static int
_e_info_server_pid_kill(pid_t id, Eldbus_Message_Iter *array_of_string)
{
   int cnt = 0;
   pid_t pid = -1;
   char result[128];

   E_Comp_Wl_Data *cdata;
   struct wl_list * client_list;
   struct wl_client *client;

   if (!e_comp) return 0;
   if (!(cdata = e_comp->wl_comp_data)) return 0;
   if (!cdata->wl.disp) return 0;

   client_list = wl_display_get_client_list(cdata->wl.disp);

   wl_client_for_each(client, client_list)
     {
        if (!client) continue;
        wl_client_get_credentials(client, &pid, NULL, NULL);
        if (pid != id) continue;

        INF("[%s] client(%p, pid:%d) has been destroyed !", __FUNCTION__, client, pid);
        wl_client_destroy(client);

        snprintf(result, sizeof(result), "[Server] A client (PID:%d) has been destroyed !", pid);
        eldbus_message_iter_arguments_append(array_of_string, VALUE_TYPE_REPLY_KILL, result);
        cnt++;
        break;
     }

   return cnt;
}

static int
_e_info_server_ec_kill(uint32_t mode, void *value, Eldbus_Message_Iter *array_of_string)
{
   E_Client *ec;
   Evas_Object *o;
   int count = 0;
   char result[1024];

   for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
     {
        const char *ec_name, *find;

        ec = evas_object_data_get(o, "E_Client");
        if (!ec) continue;
        if (e_client_util_ignored_get(ec)) continue;

        ec_name = e_client_util_name_get(ec) ?: "NO NAME";

        if (mode == KILL_NAME_MODE)
          {
             find = strstr(ec_name, (const char *)value);

             if (!find)
               continue;
          }
        else if (mode == KILL_PID_MODE)
          {
             pid_t pid = -1;
             pid = ec->netwm.pid;
             if (pid <= 0)
               {
                  if (ec->comp_data)
                    {
                       E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)ec->comp_data;
                       if (cdata->surface)
                       wl_client_get_credentials(wl_resource_get_client(cdata->surface), &pid, NULL, NULL);
                    }
               }
             if (pid != *(pid_t *)value)
               continue;
          }

        count++;
        e_client_act_kill_begin(ec);

        snprintf(result, sizeof(result), "[Server] killing creator(%s) of resource 0x%zx",
                 ec_name, e_client_util_win_get(ec));
        eldbus_message_iter_arguments_append(array_of_string, VALUE_TYPE_REPLY_KILL, result);
     }

   return count;
}

static Eldbus_Message *
_e_info_server_cb_kill_client(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   Eldbus_Message_Iter *iter = eldbus_message_iter_get(reply);
   Eina_Bool res;
   char result[1024];
   E_Client *ec;
   uint64_t uint64_value;
   uint32_t mode;
   const char *str_value;
   int count;
   Eldbus_Message_Iter *array_of_string = NULL;

   res = eldbus_message_arguments_get(msg, VALUE_TYPE_REQUEST_FOR_KILL,
                                      &mode, &uint64_value, &str_value);
   if (res != EINA_TRUE)
     {
        snprintf(result, sizeof(result),
                "[Server] Error: cannot get the arguments from an Eldbus_Message");
        goto finish;
     }

   eldbus_message_iter_arguments_append(iter, "a"VALUE_TYPE_REPLY_KILL, &array_of_string);

   if (mode == KILL_ID_MODE)
     {
        Ecore_Window win = uint64_value;

        ec = _e_info_server_ec_find_by_win(win);
        if (!ec)
          {
             snprintf(result, sizeof(result),
                     "[Server] Error: cannot find the E_Client.");
             goto finish;
          }

        e_client_act_kill_begin(ec);

        snprintf(result, sizeof(result),
                "[Server] killing creator(%s) of resource 0x%zx",
                e_client_util_name_get(ec) ?: "NO NAME", win);
     }
   else if (mode >= KILL_NAME_MODE && mode <= KILL_PID_FORCE_MODE)
     {
        if (mode == KILL_NAME_MODE)
          count = _e_info_server_ec_kill(mode, (void *)str_value, array_of_string);
        else if (mode == KILL_PID_FORCE_MODE)
          count = _e_info_server_pid_kill((pid_t)uint64_value, array_of_string);
        else
          count = _e_info_server_ec_kill(mode, (void *)&uint64_value, array_of_string);

        snprintf(result, sizeof(result), "\n[Server] killed %d client(s)", count);
     }
   else
     {
        snprintf(result, sizeof(result), "[Server] Error: wrong mode.");
     }

finish:
   eldbus_message_iter_arguments_append(array_of_string, VALUE_TYPE_REPLY_KILL, result);
   eldbus_message_iter_container_close(iter, array_of_string);

   return reply;
}

static Eldbus_Message *
_e_info_server_cb_get_windows(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   const static int _E_GET_WINDOWS_NAME_MODE = 1;
   const static int _E_GET_WINDOWS_PID_MODE = 2;
   Eldbus_Message *reply;
   Eldbus_Message_Iter *iter;
   Eina_Bool res;
   E_Client *ec;
   char *value;
   uint32_t mode;
   int count = 0;
   Eldbus_Message_Iter *array_of_windows;
   Evas_Object *o;
   pid_t pid;

   res = eldbus_message_arguments_get(msg, "is", &mode, &value);
   if (res != EINA_TRUE)
     {
        return eldbus_message_error_new(msg, GET_CALL_MSG_ARG_ERR,
                      "get_windows: an attempt to get arguments from method call message failed");
     }

   if (mode == _E_GET_WINDOWS_PID_MODE)
     {
        if (strlen(value) >= 2 && value[0] == '0' && value[1] == 'x')
          res = e_util_string_to_int(value, &pid, 16);
        else
          res = e_util_string_to_int(value, &pid, 10);

       if (res == EINA_FALSE)
         return eldbus_message_error_new(msg, INVALID_ARGS,
                                       "get_windows: invalid input arguments");
     }

   reply = eldbus_message_method_return_new(msg);
   iter = eldbus_message_iter_get(reply);

   eldbus_message_iter_arguments_append(iter, "at", &array_of_windows);

   for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
     {
        const char *ec_name, *find;
        Ecore_Window win;

        ec = evas_object_data_get(o, "E_Client");
        if (!ec) continue;

        ec_name = e_client_util_name_get(ec) ?: "NO NAME";

        if (mode == _E_GET_WINDOWS_NAME_MODE)
          {
             find = strstr(ec_name, (const char *)value);

             if (!find)
               continue;
          }
        else if (mode == _E_GET_WINDOWS_PID_MODE)
          {
             pid_t ec_pid = -1;

             ec_pid = ec->netwm.pid;
             if (ec_pid <= 0)
               {
                  if (ec->comp_data)
                    {
                       E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)ec->comp_data;
                       if (cdata->surface)
                       wl_client_get_credentials(wl_resource_get_client(cdata->surface), &ec_pid, NULL, NULL);
                    }
               }
             if (ec_pid != pid)
               continue;
          }

        win = e_client_util_win_get(ec);

        count++;

        eldbus_message_iter_arguments_append(array_of_windows, "t", win);
     }

   eldbus_message_iter_container_close(iter, array_of_windows);

   if (count)
     return reply;

   eldbus_message_unref(reply);

   return eldbus_message_error_new(msg, WIN_NOT_EXIST,
                              "get_windows: specified window(s) doesn't exist");
}

static Eldbus_Message *
_e_info_server_cb_get_window_name(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply;
   Eina_Bool res;
   E_Client *ec;
   uint64_t win;

   res = eldbus_message_arguments_get(msg, VALUE_TYPE_REQUEST_FOR_WININFO,
                                      &win);
   if (res != EINA_TRUE)
     {
        return eldbus_message_error_new(msg, GET_CALL_MSG_ARG_ERR,
                      "get_window_name: an attempt to get arguments from method call message failed");
     }

   ec = _e_info_server_ec_find_by_win(win);
   if (!ec)
     {
        return eldbus_message_error_new(msg, WIN_NOT_EXIST, "get_window_name: specified window doesn't exist");
     }

   reply = eldbus_message_method_return_new(msg);

   eldbus_message_arguments_append(reply, "s",
                                   e_client_util_name_get(ec) ?: "NO NAME");

   return reply;
}

static void
_e_info_server_wininfo_tree_info_add(E_Client *ec, Eldbus_Message_Iter *iter,
                                 int recurse, int level)
{
   Eldbus_Message_Iter *struct_of_child;

   if (ec->transients)
     {
        E_Client *child;
        const Eina_List *l;

        EINA_LIST_FOREACH(ec->transients, l, child)
          {
             uint64_t win;
             int num_child = -1;
             int hwc = -1, pl_zpos = -999;

             if (recurse)
               num_child = eina_list_count(child->transients);

             if ((!child->iconic) && (!child->visibility.obscured) &&
                 evas_object_visible_get(ec->frame))
               _e_info_server_ec_hwc_info_get(child, &hwc, &pl_zpos);

             win = e_client_util_win_get(child);
             eldbus_message_iter_arguments_append(iter, "(tsiiiiiiii)", &struct_of_child);
             eldbus_message_iter_arguments_append
                (struct_of_child, "tsiiiiiiii", win, e_client_util_name_get(child) ?: "NO NAME",
                         num_child, level, child->x, child->y, child->w, child->h, hwc, pl_zpos);
             eldbus_message_iter_container_close(iter, struct_of_child);

             if (recurse)
                _e_info_server_wininfo_tree_info_add(child, iter, 1, level + 1);
          }
     }
}

static Eldbus_Message *
_e_info_server_cb_wininfo_tree(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply;
   Eldbus_Message_Iter *iter, *array_of_child;
   Eina_Bool res;
   E_Client *ec;
   uint64_t win;
   int recurse;

   res = eldbus_message_arguments_get(msg, VALUE_TYPE_REQUEST_FOR_WININFO_TREE,
                                      &win, &recurse);
   if (res != EINA_TRUE)
     {
        return eldbus_message_error_new(msg, GET_CALL_MSG_ARG_ERR,
                      "wininfo: an attempt to get arguments from method call message failed");
     }

   ec = _e_info_server_ec_find_by_win(win);
   if (!ec)
     {
        return eldbus_message_error_new(msg, WIN_NOT_EXIST, "wininfo: specified window(s) doesn't exist");
     }

   reply = eldbus_message_method_return_new(msg);
   iter = eldbus_message_iter_get(reply);

   eldbus_message_iter_basic_append(iter, 't', (uint64_t)e_client_util_win_get(ec->parent));
   eldbus_message_iter_basic_append(iter, 's', e_client_util_name_get(ec->parent) ?: "NO NAME");
   eldbus_message_iter_basic_append(iter, 'i', eina_list_count(ec->transients));

   array_of_child = eldbus_message_iter_container_new(iter, 'a', "(tsiiiiiiii)");
   _e_info_server_wininfo_tree_info_add(ec, array_of_child, recurse, 1);
   eldbus_message_iter_container_close(iter, array_of_child);

   return reply;
}

static Eldbus_Message *
_e_info_server_cb_wininfo(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply;
   Eina_Bool res;
   E_Client *ec;
   uint64_t win;
   Ecore_Window pwin;
   uint32_t res_id = 0;
   pid_t pid = -1;
   char layer_name[64];
   int hwc = -1, pl_zpos = -999, dw, dh, xright, ybelow;;

   res = eldbus_message_arguments_get(msg, VALUE_TYPE_REQUEST_FOR_WININFO,
                                      &win);
   if (res != EINA_TRUE)
     {
        return eldbus_message_error_new(msg, GET_CALL_MSG_ARG_ERR,
                      "wininfo: an attempt to get arguments from method call message failed");
     }

   ec = _e_info_server_ec_find_by_win(win);
   if (!ec)
     {
        return eldbus_message_error_new(msg, WIN_NOT_EXIST, "wininfo: specified window(s) doesn't exist");
     }

   e_comp_layer_name_get(ec->layer, layer_name, sizeof(layer_name));

   pwin = e_client_util_win_get(ec->parent);

   if (ec->pixmap)
     res_id = e_pixmap_res_id_get(ec->pixmap);

   pid = ec->netwm.pid;
   if (pid <= 0)
     {
        if (ec->comp_data)
          {
             E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)ec->comp_data;
             if (cdata->surface)
               wl_client_get_credentials(wl_resource_get_client(cdata->surface), &pid, NULL, NULL);
          }
     }

   _e_info_server_ec_hwc_info_get(ec, &hwc, &pl_zpos);

   ecore_evas_screen_geometry_get(e_comp->ee, NULL, NULL, &dw, &dh);

   xright = dw - ec->x - ec->border_size * 2 - ec->w;
   ybelow = dh - ec->y - ec->border_size * 2 - ec->h;

   reply = eldbus_message_method_return_new(msg);

   eldbus_message_arguments_append(reply, VALUE_TYPE_REPLY_WININFO, res_id, pid,
                                   ec->x, ec->y, ec->w, ec->h, ec->layer, ec->visible,
                                   ec->argb, ec->visibility.opaque, ec->visibility.obscured,
                                   ec->iconic, evas_object_visible_get(ec->frame),
                                   ec->focused, hwc, pl_zpos, (uint64_t)pwin,
                                   layer_name, xright, ybelow, ec->border_size,
                                   ec->redirected);

   return reply;
}

static void
_e_info_server_cb_wininfo_size_hints_append(E_Client *ec, Eldbus_Message_Iter *array_of_hints)
{
   char temp[512] = {0};
   Evas_Coord w, h, l, r, t, b;
   double x, y;

   evas_object_size_hint_min_get(ec->frame, &w, &h);
   snprintf(temp, sizeof(temp), "   min: h(%d), v(%d)", w, h);
   eldbus_message_iter_arguments_append(array_of_hints, "s", temp);

   evas_object_size_hint_max_get(ec->frame, &w, &h);
   snprintf(temp, sizeof(temp), "   max: h(%d), v(%d)", w, h);
   eldbus_message_iter_arguments_append(array_of_hints, "s", temp);

   evas_object_size_hint_request_get(ec->frame, &w, &h);
   snprintf(temp, sizeof(temp), "   request: h(%d), v(%d)", w, h);
   eldbus_message_iter_arguments_append(array_of_hints, "s", temp);

   evas_object_size_hint_align_get(ec->frame, &x, &y);
   snprintf(temp, sizeof(temp), "   align: x(%f), y(%f)", x, y);
   eldbus_message_iter_arguments_append(array_of_hints, "s", temp);

   evas_object_size_hint_weight_get(ec->frame, &x, &y);
   snprintf(temp, sizeof(temp), "   weight: x(%f), y(%f)", x, y);
   eldbus_message_iter_arguments_append(array_of_hints, "s", temp);

   evas_object_size_hint_padding_get(ec->frame, &l, &r, &t, &b);
   snprintf(temp, sizeof(temp), "   padding: l(%d), r(%d), t(%d), b(%d)",
            l, r, t, b);
   eldbus_message_iter_arguments_append(array_of_hints, "s", temp);
}

static void
_e_info_server_cb_wininfo_wm_hints_append(E_Client *ec, Eldbus_Message_Iter *array_of_hints)
{
   Eina_List *l;
   E_Comp_Wl_Aux_Hint *hint;
   char temp[512] = {0};

   if (!ec->comp_data)
     return;

   EINA_LIST_FOREACH(ec->comp_data->aux_hint.hints, l, hint)
     {
        snprintf(temp, sizeof(temp), "%s: %s", hint->hint, hint->val);
        eldbus_message_iter_arguments_append(array_of_hints, "s", temp);
     }
}

static Eldbus_Message *
_e_info_server_cb_wininfo_hints(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply;
   Eldbus_Message_Iter *iter, *array_of_hints;
   Eina_Bool res;
   E_Client *ec;
   uint64_t win;
   int wm_mode;

   res = eldbus_message_arguments_get(msg, "it", &wm_mode, &win);
   if (res != EINA_TRUE)
     {
        return eldbus_message_error_new(msg, GET_CALL_MSG_ARG_ERR,
                      "wininfo_hints: an attempt to get arguments from method call message failed");
     }

   ec = _e_info_server_ec_find_by_win(win);
   if (!ec)
     {
        return eldbus_message_error_new(msg, WIN_NOT_EXIST,
                      "wininfo_hints: specified window(s) doesn't exist");
     }

   reply = eldbus_message_method_return_new(msg);
   iter = eldbus_message_iter_get(reply);

   eldbus_message_iter_arguments_append(iter, "as", &array_of_hints);
   if (wm_mode)
      _e_info_server_cb_wininfo_wm_hints_append(ec, array_of_hints);
   else
      _e_info_server_cb_wininfo_size_hints_append(ec, array_of_hints);
   eldbus_message_iter_container_close(iter, array_of_hints);

   return reply;
}

static Eldbus_Message *
_e_info_server_cb_wininfo_shape(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply;
   Eldbus_Message_Iter *iter, *array_of_shape, *struct_of_shape;
   Eina_Bool res;
   E_Client *ec;
   uint64_t win;
   int i;

   res = eldbus_message_arguments_get(msg, "t", &win);
   if (res != EINA_TRUE)
     {
        return eldbus_message_error_new(msg, GET_CALL_MSG_ARG_ERR,
                      "wininfo_shape: an attempt to get arguments from method call message failed");
     }

   ec = _e_info_server_ec_find_by_win(win);
   if (!ec)
     {
        return eldbus_message_error_new(msg, WIN_NOT_EXIST, "wininfo_shape: specified window(s) doesn't exist");
     }

   reply = eldbus_message_method_return_new(msg);
   iter = eldbus_message_iter_get(reply);

   eldbus_message_iter_basic_append(iter, 'i', ec->shape_rects_num);
   array_of_shape = eldbus_message_iter_container_new(iter, 'a', "(iiii)");
   for(i = 0; i < ec->shape_rects_num; ++i)
     {
        eldbus_message_iter_arguments_append(iter, "(iiii)", &struct_of_shape);
        eldbus_message_iter_arguments_append
           (struct_of_shape, "iiii",
            ec->shape_rects[i].x, ec->shape_rects[i].y,
            ec->shape_rects[i].w, ec->shape_rects[i].h);
        eldbus_message_iter_container_close(iter, struct_of_shape);
     }
   eldbus_message_iter_container_close(iter, array_of_shape);

   eldbus_message_iter_basic_append(iter, 'i', ec->shape_input_rects_num);
   array_of_shape = eldbus_message_iter_container_new(iter, 'a', "(iiii)");
   for(i = 0; i < ec->shape_input_rects_num; ++i)
     {
        eldbus_message_iter_arguments_append(iter, "(iiii)", &struct_of_shape);
        eldbus_message_iter_arguments_append
           (struct_of_shape, "iiii",
            ec->shape_input_rects[i].x, ec->shape_input_rects[i].y,
            ec->shape_input_rects[i].w, ec->shape_input_rects[i].h);
        eldbus_message_iter_container_close(iter, struct_of_shape);
     }
   eldbus_message_iter_container_close(iter, array_of_shape);

   return reply;
}

static Eldbus_Message *
_e_info_server_cb_version_get(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);

   eldbus_message_arguments_append(reply, "ss", VERSION, TIZEN_REL_VERSION);

   return reply;
}

static Eldbus_Message *
_e_info_server_cb_module_list_get(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eina_List *module_list = NULL, *l = NULL;
   E_Module *mod = NULL;
   Eldbus_Message *reply = NULL;
   Eldbus_Message_Iter *iter = NULL, *module_array = NULL;
   Eldbus_Message_Iter *inner_module_array = NULL;

   module_list = e_module_list();
   if (module_list == NULL)
     {
        ERR("cannot get module list");
        return eldbus_message_error_new(msg, FAIL_TO_GET_PROPERTY,
                                        "module list: e_module_list() returns NULL");
     }

   // init message
   reply = eldbus_message_method_return_new(msg);
   iter = eldbus_message_iter_get(reply);

   // get module count
   eldbus_message_iter_basic_append(iter, 'i', eina_list_count(module_list));

   // get module list
   eldbus_message_iter_arguments_append(iter, "a(si)", &module_array);
   EINA_LIST_FOREACH(module_list, l, mod)
     {
        char module_name[128] = {0};
        int isonoff = 0;
        snprintf(module_name, sizeof(module_name), "%s", mod->name);
        isonoff = e_module_enabled_get(mod);
        eldbus_message_iter_arguments_append(module_array, "(si)", &inner_module_array);
        eldbus_message_iter_arguments_append(inner_module_array, "si", module_name, isonoff);
        eldbus_message_iter_container_close(module_array, inner_module_array);
     }
   eldbus_message_iter_container_close(iter, module_array);

   return reply;
}

static Eldbus_Message *
_e_info_server_cb_module_load(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = NULL;
   E_Module *module = NULL;
   const char *module_name = NULL;
   char msg_to_client[128] = {0};
   int res = 0;

   if (eldbus_message_arguments_get(msg, "s", &module_name) == EINA_FALSE || module_name == NULL)
     {
        return eldbus_message_error_new(msg, GET_CALL_MSG_ARG_ERR,
                                        "module load: an attempt to get arguments from method call message failed");
     }

   // find module & enable
   module = e_module_find(module_name);
   if (module == NULL)
     {
        module = e_module_new(module_name);
     }
   if (module == NULL || module->error)
     {
        snprintf(msg_to_client, sizeof(msg_to_client), "module load: cannot find module name : %s", module_name);
        if(module != NULL)
          e_object_del(E_OBJECT(module));
     }
   else
     {
        if (e_module_enabled_get(module))
          {
             snprintf(msg_to_client, sizeof(msg_to_client), "enlightenment module[ %s ] is already loaded", module_name);
          }
        else
          {
             res = e_module_enable(module);
             snprintf(msg_to_client, sizeof(msg_to_client), "enlightenment module[ %s ] load %s", module_name, res?"succeed":"failed");
          }
     }

   // return message to client
   reply = eldbus_message_method_return_new(msg);
   eldbus_message_arguments_append(reply, "s", msg_to_client);

   return reply;
}

static Eldbus_Message *
_e_info_server_cb_module_unload(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = NULL;
   E_Module *module = NULL;
   const char *module_name = NULL;
   char msg_to_client[128] = {0};
   int res = 0;

   if (eldbus_message_arguments_get(msg, "s", &module_name) == EINA_FALSE || module_name == NULL)
     {
        return eldbus_message_error_new(msg, GET_CALL_MSG_ARG_ERR,
                                        "module unload: an attempt to get arguments from method call message failed");
     }

   module = e_module_find(module_name);
   if (module == NULL)
     {
        snprintf(msg_to_client, sizeof(msg_to_client), "module unload: cannot find module name : %s", module_name);
        goto finish;
     }
   else
     {
        if (e_module_enabled_get(module))
          {
             res = e_module_disable(module);
             snprintf(msg_to_client, sizeof(msg_to_client), "enlightenment module[ %s ] unload %s", module_name, res?"succeed":"failed");
          }
        else
          {
             snprintf(msg_to_client, sizeof(msg_to_client), "enlightenment module[ %s ] is already unloaded", module_name);
          }
        goto finish;
     }

finish:
   // return message to client
   reply = eldbus_message_method_return_new(msg);
   eldbus_message_arguments_append(reply, "s", msg_to_client);

   return reply;
}

static Eldbus_Message *
_e_info_server_cb_shutdown(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = NULL;
   char msg_to_client[128] = {0};

   snprintf(msg_to_client, sizeof(msg_to_client), "Enlightenment will be shutdown");
   reply = eldbus_message_method_return_new(msg);
   eldbus_message_arguments_append(reply, "s", msg_to_client);

   ecore_main_loop_quit();

   return reply;

}

static Eldbus_Message *
_e_info_server_cb_buffer_flush(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = NULL;
   int msg_from_client = 0;
   char msg_to_client[128] = {0};
   E_Client *ec = NULL;
   Ecore_Window win = 0;

   if (!eldbus_message_arguments_get(msg, "it", &msg_from_client, &win))
     {
        snprintf(msg_to_client, sizeof(msg_to_client), "Error occured while get message");
        goto finish;
     }

   if (win)
     {
        // find ec
        ec = _e_info_server_ec_find_by_win(win);
        if (ec == NULL)
          {
             snprintf(msg_to_client, sizeof(msg_to_client), "Cannot find win 0x%08zx!", win);
             goto finish;
          }
     }

   switch (msg_from_client)
     {
      case 0:
      case 1:
         if (ec)
           {
              // set buffer_flush to specified window
              ec->exp_iconify.buffer_flush = msg_from_client;
              snprintf(msg_to_client, sizeof(msg_to_client),
                       "Successfully changed!\n"
                       "win(0x%08zx/%s)->buffer_flush : %s",
                       win,
                       ec->icccm.name,
                       ec->exp_iconify.buffer_flush ? "on" : "off");
           }
         else
           {
              // set buffer_flush to all window
              e_config->use_buffer_flush = msg_from_client;
              for (ec = e_client_top_get(); ec; ec = e_client_below_get(ec))
                {
                   ec->exp_iconify.buffer_flush = msg_from_client;
                }

              snprintf(msg_to_client, sizeof(msg_to_client),
                       "Successfully changed!\n"
                       "e_config->use_buffer_flush : %s",
                       e_config->use_buffer_flush ? "on" : "off");
           }
         break;
      default:
         snprintf(msg_to_client, sizeof(msg_to_client), "Current option: e_config->use_buffer_flush : %s",
                  e_config->use_buffer_flush ? "on" : "off");
         if (ec)
           {
              snprintf(msg_to_client + strlen(msg_to_client),
                       sizeof(msg_to_client) - strlen(msg_to_client),
                       "\n\t\twin(0x%08zx)->buffer_flush : %s",
                       win,
                       ec->exp_iconify.buffer_flush ? "on" : "off");
           }
         break;
     }

finish:
   reply = eldbus_message_method_return_new(msg);
   eldbus_message_arguments_append(reply, "s", msg_to_client);

   return reply;
}

static Eldbus_Message *
_e_info_server_cb_deiconify_approve(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = NULL;
   int msg_from_client = 0;
   char msg_to_client[128] = {0};
   E_Client *ec = NULL;
   Ecore_Window win = 0;

   if (!eldbus_message_arguments_get(msg, "it", &msg_from_client, &win))
     {
        snprintf(msg_to_client, sizeof(msg_to_client), "Error occured while get message");
        goto finish;
     }

   if (win)
     {
        // find ec
        ec = _e_info_server_ec_find_by_win(win);
        if (ec == NULL)
          {
             snprintf(msg_to_client, sizeof(msg_to_client), "Cannot find win 0x%08zx!", win);
             goto finish;
          }
     }

   switch (msg_from_client)
     {
      case 0:
      case 1:
         if (ec)
           {
              // set deiconify_approve to specified window
              ec->exp_iconify.deiconify_update = msg_from_client;
              snprintf(msg_to_client, sizeof(msg_to_client),
                       "Successfully changed!\n"
                       "win(0x%08zx/%s)->deiconify_update : %s",
                       win,
                       ec->icccm.name,
                       ec->exp_iconify.deiconify_update ? "on" : "off");
           }
         else
           {
              // set deiconify_approve to all window
              e_config->deiconify_approve = msg_from_client;
              for (ec = e_client_top_get(); ec; ec = e_client_below_get(ec))
                {
                   ec->exp_iconify.deiconify_update = msg_from_client;
                }

              snprintf(msg_to_client, sizeof(msg_to_client),
                       "Successfully changed!\n"
                       "e_config->deiconify_approve : %s",
                       e_config->deiconify_approve ? "on" : "off");
           }
         break;
      default:
         snprintf(msg_to_client, sizeof(msg_to_client), "Current option: e_config->deiconify_approve : %s",
                  e_config->deiconify_approve ? "on" : "off");
         if (ec)
           {
              snprintf(msg_to_client + strlen(msg_to_client),
                       sizeof(msg_to_client) - strlen(msg_to_client),
                       "\n\t\twin(0x%08zx)->deiconify_update : %s",
                       win,
                       ec->exp_iconify.deiconify_update ? "on" : "off");
           }
         break;
     }

finish:
   reply = eldbus_message_method_return_new(msg);
   eldbus_message_arguments_append(reply, "s", msg_to_client);

   return reply;
}

static Eldbus_Message *
_e_info_server_cb_key_repeat(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   const char *path = NULL;
   int rate = 0, delay = 0;
   FILE *log_fp;

   if (!eldbus_message_arguments_get(msg, "sii", &path, &delay, &rate))
     {
        ERR("Error getting arguments.");
        return reply;
     }
   if (!e_comp_wl) return reply;

   if (path && strlen(path) > 0)
     {
        log_fp = fopen(path, "a");
        EINA_SAFETY_ON_NULL_RETURN_VAL(log_fp, reply);

        fprintf(log_fp, "\tkeyboard repeat info\n");
        fprintf(log_fp, "\t\trate: %d (ms), delay: %d (ms)\n", e_comp_wl->kbd.repeat_rate, e_comp_wl->kbd.repeat_delay);
        fclose(log_fp);
        log_fp = NULL;
     }
   else
     {
        if (delay <= 0) delay = e_comp_wl->kbd.repeat_delay;
        if (rate <= 0) rate = e_comp_wl->kbd.repeat_rate;

        e_comp_wl_input_keyboard_repeat_set(delay, rate);
     }

   return reply;
}

static Eldbus_Message *
_e_info_server_cb_memchecker(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);

   if (e_comp->func_memory_dump)
     {
        e_comp->func_memory_dump();
     }
   else
     {
        e_comp->func_memory_dump = dlsym(RTLD_NEXT, "e_memcheck_dump");
        if (e_comp->func_memory_dump)
          e_comp->func_memory_dump();
        else
          ERR("Not available to dump memory");

     }

   return reply;
}

static Eina_Bool
_input_rect_timer(void *data)
{
   Evas_Object *rect = (Evas_Object *)data;

   evas_object_hide(rect);
   evas_object_del(rect);

   e_comp_render_queue();

   return ECORE_CALLBACK_CANCEL;;
}

static void
_input_rect_draw(int x, int y, int w, int h, int time, int color_r, int color_g, int color_b)
{
   Evas_Object *rect;
   EINA_SAFETY_ON_NULL_RETURN(e_comp->evas);

   rect = evas_object_rectangle_add(e_comp->evas);
   EINA_SAFETY_ON_NULL_RETURN(rect);

   evas_object_color_set(rect, color_r, color_g, color_b, 150);
   evas_object_resize(rect, w, h);
   evas_object_move(rect, x, y);

   evas_object_layer_set(rect, E_LAYER_DESK_OBJECT_ABOVE);

   evas_object_show(rect);

   e_comp_render_queue();

   ecore_timer_add((double)time, _input_rect_timer, rect);
}

static void
_input_region_msg_clients_append(Eldbus_Message_Iter *iter, Evas_Object *obj, int time, int color_r, int color_g, int color_b)
{
   Eldbus_Message_Iter *array_of_ec;
   Eina_List *list = NULL, *l;
   Eina_Rectangle *data;

   e_comp_object_input_rect_get(obj, &list);
   if (!list) return;

   eldbus_message_iter_arguments_append(iter, "a(iiii)", &array_of_ec);

   EINA_LIST_FOREACH(list, l, data)
     {
        Eldbus_Message_Iter* struct_of_ec;

        eldbus_message_iter_arguments_append(array_of_ec, "(iiii)", &struct_of_ec);

        eldbus_message_iter_arguments_append(struct_of_ec, "iiii", data->x, data->y, data->w, data->h);
        eldbus_message_iter_container_close(array_of_ec, struct_of_ec);

        _input_rect_draw(data->x, data->y, data->w, data->h, time, color_r, color_g, color_b);
     }
   eldbus_message_iter_container_close(iter, array_of_ec);

   list = eina_list_free(list);
}

static Eldbus_Message *
_e_info_server_cb_input_region(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   Eldbus_Message_Iter *iter = eldbus_message_iter_get(reply);

   Evas_Object *o;
   E_Client *ec;
   Ecore_Window win;
   uint64_t win_id_value = 0;
   const char *win_id_str = NULL;
   unsigned long tmp = 0;
   int time = 0, color_r = 0, color_g = 0, color_b = 0;
   Eina_Bool res = EINA_FALSE;

   if (!eldbus_message_arguments_get(msg, "siiii", &win_id_str, &time, &color_r, &color_g, &color_b))
     {
        ERR("Error getting arguments.");
        return reply;
     }
   if (!e_comp) return reply;

   if (strlen(win_id_str) >= 2 && win_id_str[0] == '0' && win_id_str[1] == 'x')
     res = e_util_string_to_ulong(win_id_str, &tmp, 16);
   else
     res = e_util_string_to_ulong(win_id_str, &tmp, 10);
   if (res == EINA_FALSE)
     {
        ERR("input_region: invalid input arguments");
        return eldbus_message_error_new(msg, INVALID_ARGS,
        "input_region: invalid input arguments");
     }
   win_id_value = (uint64_t)tmp;

   for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
     {
        ec = evas_object_data_get(o, "E_Client");
        if (!ec) continue;
        if (e_client_util_ignored_get(ec)) continue;

        win = e_client_util_win_get(ec);
        if (!win || win != win_id_value) continue;

        _input_region_msg_clients_append(iter, o, time, color_r, color_g, color_b);
        break;
     }

   return reply;
}

static Eldbus_Message *
_e_info_server_cb_hwc_wins_info_get(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   E_Hwc_Wins_Debug_Cmd cmd;

   if (!eldbus_message_arguments_get(msg, "i", &cmd))
     {
        return eldbus_message_error_new(msg, GET_CALL_MSG_ARG_ERR,
                                        "hwc_wins: an attempt to get arguments from method call message failed");
     }

   e_hwc_windows_debug_info_get(eldbus_message_iter_get(reply), cmd);

   return reply;
}

//{ "method_name", arguments_from_client, return_values_to_client, _method_cb, ELDBUS_METHOD_FLAG },
static const Eldbus_Method methods[] = {
   { "get_window_info", NULL, ELDBUS_ARGS({"iiiiisa("VALUE_TYPE_FOR_TOPVWINS")", "array of ec"}), _e_info_server_cb_window_info_get, 0 },
   { "get_ec_info", NULL, ELDBUS_ARGS({"iiiiisa("VALUE_TYPE_FOR_TOPVWINS")", "array of ec"}), _e_info_server_cb_ec_info_get, 0 },
   { "get_all_window_info", NULL, ELDBUS_ARGS({"a("VALUE_TYPE_FOR_TOPVWINS")", "array of ec"}), _e_info_server_cb_all_window_info_get, 0 },
   { "compobjs", NULL, ELDBUS_ARGS({"a("SIGNATURE_COMPOBJS_CLIENT")", "array of comp objs"}), _e_info_server_cb_compobjs, 0 },
   { "subsurface", NULL, ELDBUS_ARGS({"a("SIGNATURE_SUBSURFACE")", "array of ec"}), _e_info_server_cb_subsurface, 0 },
   { "dump_wins", ELDBUS_ARGS({SIGNATURE_DUMP_WINS, "directory"}), ELDBUS_ARGS({"s", "result of dump"}), _e_info_server_cb_wins_dump, 0 },
   { "set_force_visible", ELDBUS_ARGS({SIGNATURE_FORCE_VISIBLE_CLIENT, "obj"}), ELDBUS_ARGS({SIGNATURE_FORCE_VISIBLE_SERVER, "msg"}), _e_info_server_cb_force_visible, 0 },
   { "eina_log_levels", ELDBUS_ARGS({"s", "eina log levels"}), NULL, _e_info_server_cb_eina_log_levels, 0 },
   { "eina_log_path", ELDBUS_ARGS({"s", "eina log path"}), NULL, _e_info_server_cb_eina_log_path, 0 },
#ifdef HAVE_DLOG
   { "dlog", ELDBUS_ARGS({"i", "using dlog"}), NULL, _e_info_server_cb_dlog_switch, 0},
#endif
   { "get_window_prop", ELDBUS_ARGS({"usss", "prop_manage_request"}), ELDBUS_ARGS({"a(ss)", "array_of_ec"}), _e_info_server_cb_window_prop_get, 0},
   { "get_connected_clients", NULL, ELDBUS_ARGS({"a(ss)", "array of ec"}), _e_info_server_cb_connected_clients_get, 0 },
   { "rotation_query", ELDBUS_ARGS({"i", "query_rotation"}), NULL, _e_info_server_cb_rotation_query, 0},
   { "rotation_message", ELDBUS_ARGS({"iii", "rotation_message"}), NULL, _e_info_server_cb_rotation_message, 0},
   { "get_res_lists", ELDBUS_ARGS({VALUE_TYPE_REQUEST_RESLIST, "client resource"}), ELDBUS_ARGS({"a("VALUE_TYPE_REPLY_RESLIST")", "array of client resources"}), _e_info_server_cb_res_lists_get, 0 },
   { "get_input_devices", NULL, ELDBUS_ARGS({"a("VALUE_TYPE_FOR_INPUTDEV")", "array of input"}), _e_info_server_cb_input_device_info_get, 0},
   { "protocol_trace", ELDBUS_ARGS({"s", "protocol_trace"}), NULL, _e_info_server_cb_protocol_trace, 0},
   { "protocol_rule", ELDBUS_ARGS({"sss", "protocol_rule"}), ELDBUS_ARGS({"s", "rule request"}), _e_info_server_cb_protocol_rule, 0},
   { "bgcolor_set", ELDBUS_ARGS({"iiii", "bgcolor_set"}), NULL, _e_info_server_cb_bgcolor_set, 0},
   { "punch", ELDBUS_ARGS({"iiiiiiiii", "punch_geometry"}), NULL, _e_info_server_cb_punch, 0},
   { "transform_message", ELDBUS_ARGS({"siiiiiiii", "transform_message"}), NULL, e_info_server_cb_transform_message, 0},
   { "dump_buffers", ELDBUS_ARGS({"iisdi", "dump_buffers"}), ELDBUS_ARGS({"is", "dump_buffers reply"}), _e_info_server_cb_buffer_dump, 0 },
   { "dump_selected_buffers", ELDBUS_ARGS({"ss", "dump_selected_buffers"}), ELDBUS_ARGS({"s", "result of dump"}), _e_info_server_cb_selected_buffer_dump, 0 },
   { "dump_screen", ELDBUS_ARGS({"s", "dump_screen"}), NULL, _e_info_server_cb_screen_dump, 0 },
   { "output_mode", ELDBUS_ARGS({SIGNATURE_OUTPUT_MODE_CLIENT, "output mode"}), ELDBUS_ARGS({"a("SIGNATURE_OUTPUT_MODE_SERVER")", "array of ec"}), _e_info_server_cb_output_mode, 0 },
   { "trace_message_hwc", ELDBUS_ARGS({"i", "trace_message_hwc"}), NULL, e_info_server_cb_hwc_trace_message, 0},
   { "trace_message_serial", ELDBUS_ARGS({"i", "trace_message_serial"}), NULL, e_info_server_cb_serial_trace_message, 0},
   { "hwc", ELDBUS_ARGS({"i", "hwc"}), NULL, e_info_server_cb_hwc, 0},
   { "show_plane_state", NULL, NULL, e_info_server_cb_show_plane_state, 0},
   { "show_pending_commit", NULL, ELDBUS_ARGS({"a("VALUE_TYPE_FOR_PENDING_COMMIT")", "array of pending commit"}), e_info_server_cb_show_pending_commit, 0},
   { "get_fps_info", NULL, ELDBUS_ARGS({"a("VALUE_TYPE_FOR_FPS")", "array of fps"}), _e_info_server_cb_fps_info_get, 0},
   { "get_keymap", NULL, ELDBUS_ARGS({"hi", "keymap fd"}), _e_info_server_cb_keymap_info_get, 0},
   { "effect_control", ELDBUS_ARGS({"i", "effect_control"}), NULL, e_info_server_cb_effect_control, 0},
   { "get_keygrab_status", ELDBUS_ARGS({"s", "get_keygrab_status"}), NULL, _e_info_server_cb_keygrab_status_get, 0},
   { "get_module_info", ELDBUS_ARGS({"ss", "get_module_info"}), NULL, _e_info_server_cb_module_info_get, 0},
   { "aux_msg", ELDBUS_ARGS({"s","window id" }, {"s", "key"}, {"s", "value"}, {"as", "options"}), NULL, e_info_server_cb_aux_message, 0},
   { "scrsaver", ELDBUS_ARGS({SIGNATURE_SCRSAVER_CLIENT, "scrsaver_params"}), ELDBUS_ARGS({SIGNATURE_SCRSAVER_SERVER, "scrsaver_result"}), _e_info_server_cb_scrsaver, 0},
   { "slot_message", ELDBUS_ARGS({"iiiiii", "slot_message"}), ELDBUS_ARGS({"a(ss)", "array of ec"}), e_info_server_cb_slot_message, 0},
   { "desktop_geometry_set", ELDBUS_ARGS({"iiii", "Geometry"}), NULL, _e_info_server_cb_desktop_geometry_set, 0},
   { "desk_zoom", ELDBUS_ARGS({"ddii", "Zoom"}), NULL, _e_info_server_cb_desk_zoom, 0},
   { "frender", ELDBUS_ARGS({"i", "frender"}), ELDBUS_ARGS({"s", "force_render_result"}), _e_info_server_cb_force_render, 0},
   { "screen_rotation_pre", ELDBUS_ARGS({"i", "value"}), NULL, _e_info_server_cb_screen_rotation_pre, 0},
   { "screen_rotation", ELDBUS_ARGS({"i", "value"}), NULL, _e_info_server_cb_screen_rotation, 0},
   { "remote_surface", ELDBUS_ARGS({"ii", "remote surface query"}), ELDBUS_ARGS({"as", "remote surfac information"}), _e_info_server_cb_remote_surface, 0},
   { "get_win_under_touch", NULL, ELDBUS_ARGS({"i", "result"}), _e_info_server_cb_get_win_under_touch, 0 },
   { "kill_client", ELDBUS_ARGS({VALUE_TYPE_REQUEST_FOR_KILL, "window"}), ELDBUS_ARGS({"a"VALUE_TYPE_REPLY_KILL, "kill result"}), _e_info_server_cb_kill_client, 0 },
   { "get_window_name", ELDBUS_ARGS({"t", "window"}), ELDBUS_ARGS({"s", "window name"}), _e_info_server_cb_get_window_name, 0 },
   { "get_windows", ELDBUS_ARGS({"is", "mode, value"}), ELDBUS_ARGS({"at", "array_of_windows"}), _e_info_server_cb_get_windows, 0 },
   { "wininfo", ELDBUS_ARGS({VALUE_TYPE_REQUEST_FOR_WININFO, "window"}), ELDBUS_ARGS({VALUE_TYPE_REPLY_WININFO, "window info"}), _e_info_server_cb_wininfo, 0 },
   { "wininfo_tree", ELDBUS_ARGS({VALUE_TYPE_REQUEST_FOR_WININFO_TREE, "wininfo_tree"}), ELDBUS_ARGS({VALUE_TYPE_REPLY_WININFO_TREE, "window tree info"}), _e_info_server_cb_wininfo_tree, 0 },
   { "wininfo_hints", ELDBUS_ARGS({"it", "mode, window"}), ELDBUS_ARGS({"as", "window hints"}), _e_info_server_cb_wininfo_hints, 0 },
   { "wininfo_shape", ELDBUS_ARGS({"t", "window"}), ELDBUS_ARGS({"ia(iiii)ia(iiii)", "window shape"}), _e_info_server_cb_wininfo_shape, 0 },
   { "get_version", NULL, ELDBUS_ARGS({"ss", "version of E20"}), _e_info_server_cb_version_get, 0 },
   { "module_list_get", NULL, ELDBUS_ARGS({"ia(si)", "module list"}), _e_info_server_cb_module_list_get, 0 },
   { "module_load", ELDBUS_ARGS({"s", "target module"}), ELDBUS_ARGS({"s", "load result"}), _e_info_server_cb_module_load, 0 },
   { "module_unload", ELDBUS_ARGS({"s", "target module"}), ELDBUS_ARGS({"s", "unload result"}), _e_info_server_cb_module_unload, 0 },
   { "shutdown", NULL, ELDBUS_ARGS({"s", "shutdown result"}), _e_info_server_cb_shutdown, 0 },
   { "buffer_flush", ELDBUS_ARGS({"it", "option"}), ELDBUS_ARGS({"s", "buffer_flush status"}), _e_info_server_cb_buffer_flush, 0},
   { "deiconify_approve", ELDBUS_ARGS({"it", "option"}), ELDBUS_ARGS({"s", "deiconify_approve status"}), _e_info_server_cb_deiconify_approve, 0},
   { "key_repeat", ELDBUS_ARGS({"sii", "option"}), NULL, _e_info_server_cb_key_repeat, 0},
   { "dump_memchecker", NULL, NULL, _e_info_server_cb_memchecker, 0},
   { "magnifier", ELDBUS_ARGS({"i", "magnifier"}), NULL, e_info_server_cb_magnifier, 0},
   { "input_region", ELDBUS_ARGS({"siiii", "options"}), ELDBUS_ARGS({"a(iiii)", "path"}), _e_info_server_cb_input_region, 0},
   { "hwc_wins", ELDBUS_ARGS({"i", "option"}), ELDBUS_ARGS({"as", "hwc wins info"}), _e_info_server_cb_hwc_wins_info_get, 0 },
   { NULL, NULL, NULL, NULL, 0 }
};

static const Eldbus_Signal signals[] = {
   [E_INFO_SERVER_SIGNAL_WIN_UNDER_TOUCH] = {"win_under_touch", ELDBUS_ARGS({ "t", "win_under_touch" }), 0},
   { }
};

static const Eldbus_Service_Interface_Desc iface_desc = {
     IFACE, methods, signals, NULL, NULL, NULL
};

Eina_Bool
e_info_server_protocol_rule_path_init(char *rule_path)
{
    char reply[4096];
    int len = sizeof (reply);
    char *argv[2];
    int argc = 2;

    if (!rule_path || strlen(rule_path) <= 0)
        return EINA_FALSE;

    argv[0] = "file";
    argv[1] = rule_path;

    e_info_protocol_rule_set(argc, (const char**)&(argv[0]), reply, &len);

    INF("%s: rule_path : %s\n", __func__, rule_path);
    INF("%s\n", reply);

    return EINA_TRUE;
}

Eina_Bool
e_info_server_protocol_trace_path_init(char *trace_path)
{
   if (!trace_path || strlen(trace_path) <= 0)
     return EINA_FALSE;

   INF("%s: trace_path : %s\n", __func__, trace_path);

   log_fp_ptrace = fopen(trace_path, "a");

   if (!log_fp_ptrace)
     {
        ERR("failed: open file(%s)\n", trace_path);
        return EINA_FALSE;
     }

   setvbuf(log_fp_ptrace, NULL, _IOLBF, 512);
   if (e_info_protocol_logger)
     {
        wl_protocol_logger_destroy(e_info_protocol_logger);
        e_info_protocol_logger = NULL;
     }

   e_info_protocol_logger = wl_display_add_protocol_logger(e_comp->wl_comp_data->wl.disp, _e_info_server_protocol_debug_func2, NULL);

   return EINA_TRUE;
}

static Eina_Bool
_e_info_server_dbus_init(void *data EINA_UNUSED)
{
   char *s = NULL;

   e_info_server.iface = eldbus_service_interface_register(e_info_server.edbus_conn,
                                                           PATH,
                                                           &iface_desc);
   EINA_SAFETY_ON_NULL_GOTO(e_info_server.iface, err);

   E_EVENT_INFO_ROTATION_MESSAGE = ecore_event_type_new();

   e_info_protocol_init();

   s = e_util_env_get("E_INFO_RULE_FILE");
   e_info_server_protocol_rule_path_init(s);
   E_FREE(s);

   s = e_util_env_get("E_INFO_TRACE_FILE");
   e_info_server_protocol_trace_path_init(s);
   E_FREE(s);

   e_main_hook_call(E_MAIN_HOOK_E_INFO_READY);

   return ECORE_CALLBACK_CANCEL;

err:
   e_info_server_shutdown();

   return ECORE_CALLBACK_CANCEL;
}

static Eina_Bool
_e_info_server_cb_dbus_init_done(void *data, int type, void *event)
{
   E_DBus_Conn_Init_Done_Event *e = event;

   if (e->status == E_DBUS_CONN_INIT_SUCCESS && e->conn_type == e_info_server.edbus_conn_type)
     {
        e_info_server.edbus_conn = e_dbus_conn_connection_ref(e_info_server.edbus_conn_type);

        if (e_info_server.edbus_conn)
          _e_info_server_dbus_init(NULL);
     }

   ecore_event_handler_del(e_info_server.dbus_init_done_handler);
   e_info_server.dbus_init_done_handler = NULL;

   return ECORE_CALLBACK_PASS_ON;
}


EINTERN int
e_info_server_init(void)
{
   e_info_server.edbus_conn = NULL;
   e_info_server.edbus_conn_type = ELDBUS_CONNECTION_TYPE_SYSTEM;
   e_info_server.dbus_init_done_handler = NULL;

   if (e_dbus_conn_init() > 0)
     {
        e_info_server.dbus_init_done_handler = ecore_event_handler_add(E_EVENT_DBUS_CONN_INIT_DONE, _e_info_server_cb_dbus_init_done, NULL);
        e_dbus_conn_dbus_init(e_info_server.edbus_conn_type);
     }

   return 1;
}

EINTERN int
e_info_server_shutdown(void)
{
   if (e_info_server.dbus_init_done_handler)
     {
         ecore_event_handler_del(e_info_server.dbus_init_done_handler);
         e_info_server.dbus_init_done_handler = NULL;
     }

   if (e_info_server.iface)
     {
        eldbus_service_interface_unregister(e_info_server.iface);
        e_info_server.iface = NULL;
     }

   if (e_info_server.edbus_conn)
     {
        eldbus_name_release(e_info_server.edbus_conn, BUS, NULL, NULL);
        e_dbus_conn_connection_unref(e_info_server.edbus_conn);
        e_info_server.edbus_conn = NULL;
     }

   e_dbus_conn_shutdown();

   if (e_info_transform_list)
     {
        E_Info_Transform *info;
        Eina_List *l, *l_next;

        EINA_LIST_FOREACH_SAFE(e_info_transform_list, l, l_next, info)
          {
             _e_info_transform_del(info);
          }

        eina_list_free(e_info_transform_list);
        e_info_transform_list = NULL;
     }

   if (e_info_dump_running == 1)
     {
        tdm_helper_dump_stop();
        tbm_surface_internal_dump_end();
     }
   if (e_info_dump_hdlrs)
     {
        E_FREE_LIST(e_info_dump_hdlrs, ecore_event_handler_del);
        e_info_dump_hdlrs = NULL;
     }
   if (e_info_dump_path)
     {
        free(e_info_dump_path);
        e_info_dump_path = NULL;
     }
   e_info_dump_count = 0;
   e_info_dump_running = 0;

   if (module_hook) _e_info_server_module_hook_cleanup();

   e_info_protocol_shutdown();

   eldbus_shutdown();

   return 1;
}

EINTERN void
e_info_server_dump_client(E_Client *ec, char *fname)
{
   void *data = NULL;
   int w = 0, h = 0;
   Ecore_Evas *ee = NULL;
   Evas_Object *img = NULL;

   if (!ec) return;
   if (e_client_util_ignored_get(ec)) return;

   struct wl_shm_buffer *shmbuffer = NULL;
   E_Comp_Wl_Buffer *buffer = e_pixmap_resource_get(ec->pixmap);
   if (!buffer) return;

   if (buffer->type == E_COMP_WL_BUFFER_TYPE_SHM)
     {
        shmbuffer = wl_shm_buffer_get(buffer->resource);
        if (shmbuffer)
          {
             data = wl_shm_buffer_get_data(shmbuffer);
             w = wl_shm_buffer_get_stride(shmbuffer) / 4;
             h = wl_shm_buffer_get_height(shmbuffer);
          }
     }
   else if (buffer->type == E_COMP_WL_BUFFER_TYPE_NATIVE)
     {
        tbm_surface_info_s surface_info;
        tbm_surface_h tbm_surface = wayland_tbm_server_get_surface(NULL, buffer->resource);

        EINA_SAFETY_ON_NULL_RETURN(tbm_surface);
        memset(&surface_info, 0, sizeof(tbm_surface_info_s));
        tbm_surface_map(tbm_surface, TBM_SURF_OPTION_READ, &surface_info);

        data = surface_info.planes[0].ptr;
        w = surface_info.planes[0].stride / 4;
        h = surface_info.height;
     }
   else if (buffer->type == E_COMP_WL_BUFFER_TYPE_TBM)
     {
        tbm_surface_info_s surface_info;
        tbm_surface_h tbm_surface = buffer->tbm_surface;

        EINA_SAFETY_ON_NULL_RETURN(tbm_surface);
        memset(&surface_info, 0, sizeof(tbm_surface_info_s));
        tbm_surface_map(tbm_surface, TBM_SURF_OPTION_READ, &surface_info);

        data = surface_info.planes[0].ptr;
        w = surface_info.planes[0].stride / 4;
        h = surface_info.height;
     }
   else
     {
        ERR("Invalid resource:%u", wl_resource_get_id(buffer->resource));
     }

   EINA_SAFETY_ON_NULL_GOTO(data, err);

   ee = ecore_evas_buffer_new(1, 1);
   EINA_SAFETY_ON_NULL_GOTO(ee, err);

   img = evas_object_image_add(ecore_evas_get(ee));
   EINA_SAFETY_ON_NULL_GOTO(img, err);

   evas_object_image_alpha_set(img, EINA_TRUE);
   evas_object_image_size_set(img, w, h);
   evas_object_image_data_set(img, data);

   if (!evas_object_image_save(img, fname, NULL, "compress=1 quality=100"))
     ERR("Cannot save window to '%s'", fname);

err:
   if (data)
     {
        if (buffer->type == E_COMP_WL_BUFFER_TYPE_NATIVE)
          {
             tbm_surface_h tbm_surface = wayland_tbm_server_get_surface(NULL, buffer->resource);
             tbm_surface_unmap(tbm_surface);
          }
        else if (buffer->type == E_COMP_WL_BUFFER_TYPE_TBM)
          {
             tbm_surface_h tbm_surface = buffer->tbm_surface;
             tbm_surface_unmap(tbm_surface);
          }
     }

   if (img) evas_object_del(img);
   if (ee) ecore_evas_free(ee);
}


static E_Info_Transform*
_e_info_transform_new(E_Client *ec, int id, int enable, int x, int y, int sx, int sy, int degree, int background)
{
   E_Info_Transform *result = NULL;
   result = _e_info_transform_find(ec, id);

   if (!result)
     {
        result = (E_Info_Transform*)malloc(sizeof(E_Info_Transform));
        EINA_SAFETY_ON_NULL_RETURN_VAL(result, NULL);
        memset(result, 0, sizeof(E_Info_Transform));
        result->id = id;
        result->ec = ec;
        result->transform = e_util_transform_new();
        result->background = background;
        result->enable = 0;
        _e_info_transform_set(result, enable, x, y, sx, sy, degree);
        e_info_transform_list = eina_list_append(e_info_transform_list, result);
     }

   return result;
}

static E_Info_Transform*
_e_info_transform_find(E_Client *ec, int id)
{
   Eina_List *l;
   E_Info_Transform *transform;
   E_Info_Transform *result = NULL;

   EINA_LIST_FOREACH(e_info_transform_list, l, transform)
     {
        if (transform->ec == ec && transform->id == id)
          {
             result =  transform;
             break;
          }
     }

   return result;
}

static void
_e_info_transform_set(E_Info_Transform *transform, int enable, int x, int y, int sx, int sy, int degree)
{
   if (!transform) return;
   if (!transform->transform) return;

   if (transform->background)
     {
        e_util_transform_bg_move(transform->transform, (double)x, (double)y, 0.0);
        e_util_transform_bg_scale(transform->transform, (double)sx / 100.0, (double)sy / 100.0, 1.0);
        e_util_transform_bg_rotation(transform->transform, 0.0, 0.0, degree);
     }
   else
     {
        e_util_transform_move(transform->transform, (double)x, (double)y, 0.0);
        e_util_transform_scale(transform->transform, (double)sx / 100.0, (double)sy / 100.0, 1.0);
        e_util_transform_rotation(transform->transform, 0.0, 0.0, degree);
     }

   if (enable != transform->enable)
     {
        if (enable)
          e_client_transform_core_add(transform->ec, transform->transform);
        else
          e_client_transform_core_remove(transform->ec, transform->transform);

        transform->enable = enable;
     }

   e_client_transform_core_update(transform->ec);
}

static void
_e_info_transform_del(E_Info_Transform *transform)
{
   if (!transform) return;

   e_info_transform_list = eina_list_remove(e_info_transform_list, transform);

   if (transform->enable)
     {
        e_client_transform_core_remove(transform->ec, transform->transform);
     }

   e_util_transform_del(transform->transform);
   free(transform);
}

static void
_e_info_transform_del_with_id(E_Client *ec, int id)
{
   E_Info_Transform *transform = NULL;
   if (!ec) return;

   transform = _e_info_transform_find(ec, id);

   if (transform)
      _e_info_transform_del(transform);
}
