#include "e.h"
#include "e_info_server.h"
#include <tbm_bufmgr.h>
#include <tbm_surface.h>
#include <tbm_surface_internal.h>
#include <tdm_helper.h>
#include <wayland-tbm-server.h>
#include "e_comp_wl.h"
#include "e_info_protocol.h"

#define EDJE_EDIT_IS_UNSTABLE_AND_I_KNOW_ABOUT_IT
#include <Edje_Edit.h>

#define USE_WAYLAND_LOG_TRACE
#define USE_WAYLAND_LOGGER ((WAYLAND_VERSION_MAJOR == 1) && (WAYLAND_VERSION_MINOR > 11))

#if !USE_WAYLAND_LOGGER
struct wl_object
{
   const struct wl_interface *interface;
   const void *implementation;
   uint32_t id;
};

struct wl_resource
{
   struct wl_object object;
   wl_resource_destroy_func_t destroy;
   struct wl_list link;
   struct wl_signal destroy_signal;
   struct wl_client *client;
   void *data;
};
#endif

void wl_map_for_each(struct wl_map *map, void *func, void *data);

#define BUS "org.enlightenment.wm"
#define PATH "/org/enlightenment/wm"
#define IFACE "org.enlightenment.wm.info"

#define ERR_BASE "org.enlightenment.wm.Error."
#define INVALID_ARGS         ERR_BASE"InvalidArguments"
#define GET_CALL_MSG_ARG_ERR ERR_BASE"GetCallMsgArgFailed"
#define WIN_NOT_EXIST        ERR_BASE"WindowNotExist"

E_API int E_EVENT_INFO_ROTATION_MESSAGE = -1;

typedef struct _E_Info_Server
{
   Eldbus_Connection *conn;
   Eldbus_Service_Interface *iface;
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

//FILE pointer for protocol_trace
static FILE *log_fp_ptrace = NULL;

#if USE_WAYLAND_LOGGER
//wayland protocol logger
static struct wl_protocol_logger *e_info_protocol_logger;
#endif

// Module list for module info
static Eina_List *module_hook = NULL;

#define BUF_SNPRINTF(fmt, ARG...) do { \
   str_l = snprintf(str_buff, str_r, fmt, ##ARG); \
   str_buff += str_l; \
   str_r -= str_l; \
} while(0)

#define VALUE_TYPE_FOR_TOPVWINS "uuisiiiiibbiibbbiius"
#define VALUE_TYPE_REQUEST_RESLIST "ui"
#define VALUE_TYPE_REPLY_RESLIST "ssi"
#define VALUE_TYPE_FOR_INPUTDEV "ssi"
#define VALUE_TYPE_FOR_PENDING_COMMIT "uiuu"
#define VALUE_TYPE_REQUEST_FOR_KILL "uts"
#define VALUE_TYPE_REPLY_KILL "s"
#define VALUE_TYPE_REQUEST_FOR_WININFO "t"
#define VALUE_TYPE_REPLY_WININFO "tuisiiiiibbiibbbiitsiiib"

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
   if ((hookpoint < 0) || (hookpoint >= E_INFO_SERVER_HOOK_LAST)) return;

   _e_info_server_hook_call(hookpoint, NULL);
}

#ifdef ENABLE_HWC_MULTI
static void
_e_info_server_ec_hwc_info_get(E_Client *ec, int *hwc, int *pl_zpos)
{
   Eina_List *l;
   E_Output *eout;
   E_Plane *ep;

   *hwc = -1;
   *pl_zpos = -999;

   if ((!e_comp->hwc) || (e_comp->hwc_deactive))
     return;

   *hwc = 0;

   eout = e_output_find(ec->zone->output_id);
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
#endif

static void
_msg_clients_append(Eldbus_Message_Iter *iter)
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

        ec = evas_object_data_get(o, "E_Client");
        if (!ec) continue;
        if (e_client_util_ignored_get(ec)) continue;

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


#ifdef ENABLE_HWC_MULTI
        _e_info_server_ec_hwc_info_get(ec, &hwc, &pl_zpos);
#endif

        eldbus_message_iter_arguments_append(array_of_ec, "("VALUE_TYPE_FOR_TOPVWINS")", &struct_of_ec);

        eldbus_message_iter_arguments_append
           (struct_of_ec, VALUE_TYPE_FOR_TOPVWINS,
            win,
            res_id,
            pid,
            e_client_util_name_get(ec) ?: "NO NAME",
            ec->x, ec->y, ec->w, ec->h, ec->layer,
            ec->visible, ec->argb, ec->visibility.opaque, ec->visibility.obscured, ec->iconic,
            evas_object_visible_get(ec->frame), ec->focused, hwc, pl_zpos, pwin, layer_name);

        eldbus_message_iter_container_close(array_of_ec, struct_of_ec);
     }

   eldbus_message_iter_container_close(iter, array_of_ec);
}

/* Method Handlers */
static Eldbus_Message *
_e_info_server_cb_window_info_get(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);

   _msg_clients_append(eldbus_message_iter_get(reply));

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

   cobj->obj = (unsigned int)o;
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

             snprintf(buf, sizeof(buf), "%x %d %s",
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
                   cobj->img.data = (unsigned int)ns->data.wl.legacy_buffer;
                   break;
                case EVAS_NATIVE_SURFACE_TBM:
                   cobj->img.native_type = eina_stringshare_add("TBM");
                   cobj->img.data = (unsigned int)ns->data.tbm.buffer;
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

             cobj->img.data = (unsigned int)evas_object_image_data_get(o, 0);
          }

        evas_object_image_size_get(o, &cobj->img.w, &cobj->img.h);
        evas_object_image_load_size_get(o, &cobj->img.lw, &cobj->img.lh);
        evas_object_image_fill_get(o, &cobj->img.fx, &cobj->img.fy, &cobj->img.fw, &cobj->img.fh);
        cobj->img.alpha = evas_object_image_alpha_get(o);
        cobj->img.dirty = evas_object_image_pixels_dirty_get(o);
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
        stack = eina_list_append(stack, info);
     }

   while (1)
     {
        /* 2. pop */
        info = eina_list_last_data_get(stack);
        if (!info) break;

        /* store data */
        cobj = _compobj_info_get(info->po, info->o, info->depth);
        queue = eina_list_append(queue, cobj);

        /* 3. push : child objects */
        if (evas_object_smart_data_get(info->o))
          {
             EINA_LIST_REVERSE_FOREACH(evas_object_smart_members_get(info->o), ll, c)
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
                                             cobj->img.dirty);

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
   E_Comp_Wl_Data *cdata;
   E_Comp_Wl_Input_Device *dev;

   eldbus_message_iter_arguments_append(iter, "a("VALUE_TYPE_FOR_INPUTDEV")", &array_of_input);

   cdata = e_comp->wl_comp_data;
   EINA_LIST_FOREACH(cdata->input_device_manager.device_list, l, dev)
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
                  __CONNECTED_CLIENTS_ARG_APPEND_TYPE("[E_Client Info]", "win:0x%08x res_id:%5d, name:%20s, geo:(%4d, %4d, %4dx%4d), layer:%5d, visible:%d, argb:%d",
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

static void
_e_info_server_get_resource(void *element, void *data)
{
   struct wl_resource *resource = element;
   Eldbus_Message_Iter* array_of_res= data;
   Eldbus_Message_Iter* struct_of_res;

   eldbus_message_iter_arguments_append(array_of_res, "("VALUE_TYPE_REPLY_RESLIST")", &struct_of_res);
   eldbus_message_iter_arguments_append(struct_of_res, VALUE_TYPE_REPLY_RESLIST, "[resource]", wl_resource_get_name(resource), wl_resource_get_id(resource));
   eldbus_message_iter_container_close(array_of_res, struct_of_res);
   resurceCnt++;
}

static void
_msg_clients_res_list_append(Eldbus_Message_Iter *iter, uint32_t mode, int id)
{
   Eldbus_Message_Iter *array_of_res;

   struct wl_list * client_list;
   struct wl_client *client;
   struct wl_map *res_objs;
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
        res_objs = wl_client_get_resources(client);
        wl_map_for_each(res_objs, _e_info_server_get_resource, array_of_res);

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

#define __WINDOW_PROP_ARG_APPEND(title, value) ({                                    \
                                                eldbus_message_iter_arguments_append(iter, "(ss)", &struct_of_ec);    \
                                                eldbus_message_iter_arguments_append(struct_of_ec, "ss", (title), (value));  \
                                                eldbus_message_iter_container_close(iter, struct_of_ec);})

#define __WINDOW_PROP_ARG_APPEND_TYPE(title, str, x...) ({                           \
                                                         char __temp[128] = {0,};                                                     \
                                                         snprintf(__temp, sizeof(__temp), str, ##x);                                  \
                                                         eldbus_message_iter_arguments_append(iter, "(ss)", &struct_of_ec);    \
                                                         eldbus_message_iter_arguments_append(struct_of_ec, "ss", (title), (__temp)); \
                                                         eldbus_message_iter_container_close(iter, struct_of_ec);})

static void
_msg_window_prop_client_append(Eldbus_Message_Iter *iter, E_Client *target_ec)
{
   Eldbus_Message_Iter* struct_of_ec;
   pid_t pid = -1;
   char win_resid[16] = {0,};
   char char_True[] = "TRUE";
   char char_False[] = "FALSE";
   char layer_name[48] = {0,};
   char layer[64] = {0,};
   char transients[128] = {0,};
   char shape_rects[128] = {0,};
   char shape_input[128] = {0,};

   if (!target_ec) return;

   if (target_ec->pixmap)
      snprintf(win_resid, sizeof(win_resid), "%d", e_pixmap_res_id_get(target_ec->pixmap));

   e_comp_layer_name_get(target_ec->layer, layer_name, sizeof(layer_name));
   snprintf(layer, sizeof(layer), "[%d, %s]",  target_ec->layer, layer_name);

   if (target_ec->transients)
     {
        E_Client *child;
        const Eina_List *l;

        EINA_LIST_FOREACH(target_ec->transients, l, child)
          {
             char temp[16];
             snprintf(temp, sizeof(temp), "0x%x", e_client_util_win_get(child));
             strncat(transients, temp, sizeof(transients) - strlen(transients));
          }
     }

   if (target_ec->shape_rects && target_ec->shape_rects_num > 0)
     {
        int i = 0;
        for (i = 0 ; i < target_ec->shape_rects_num ; ++i)
          {
             char temp[32];
             snprintf(temp, sizeof(temp), "[%d,%d,%d,%d] ", target_ec->shape_rects[i].x, target_ec->shape_rects[i].y,
                      target_ec->shape_rects[i].w, target_ec->shape_rects[i].h);
             strncat(shape_rects, temp, sizeof(shape_rects) - strlen(shape_rects));
          }
     }

   if (target_ec->shape_input_rects && target_ec->shape_input_rects_num > 0)
     {
        int i = 0;
        for (i = 0 ; i < target_ec->shape_input_rects_num ; ++i)
          {
             char temp[32];
             snprintf(temp, sizeof(temp), "[%d,%d,%d,%d] ", target_ec->shape_input_rects[i].x, target_ec->shape_input_rects[i].y,
                      target_ec->shape_input_rects[i].w, target_ec->shape_input_rects[i].h);
             strncat(shape_input, temp, sizeof(shape_input) - strlen(shape_input));
          }
     }

   if (target_ec->comp_data)
     {

        E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)target_ec->comp_data;
        if (cdata->surface)
          {
             wl_client_get_credentials(wl_resource_get_client(cdata->surface), &pid, NULL, NULL);
          }
     }

   __WINDOW_PROP_ARG_APPEND("[WINDOW PROP]", "[WINDOW PROP]");
   __WINDOW_PROP_ARG_APPEND_TYPE("Window_ID", "0x%x", e_client_util_win_get(target_ec));
   __WINDOW_PROP_ARG_APPEND_TYPE("PID", "%d", pid);
   __WINDOW_PROP_ARG_APPEND("ResourceID", win_resid);
   __WINDOW_PROP_ARG_APPEND("Window_Name", e_client_util_name_get(target_ec) ?: "NO NAME");
   __WINDOW_PROP_ARG_APPEND("Role", target_ec->icccm.window_role ?: "NO ROLE");
   __WINDOW_PROP_ARG_APPEND_TYPE("Geometry", "[%d, %d, %d, %d]", target_ec->x, target_ec->y, target_ec->w, target_ec->h);
   __WINDOW_PROP_ARG_APPEND_TYPE("ParentWindowID", "0x%x", target_ec->parent ? e_client_util_win_get(target_ec->parent) : 0);
   __WINDOW_PROP_ARG_APPEND("Transients", transients);
   __WINDOW_PROP_ARG_APPEND("Shape_rects", shape_rects);
   __WINDOW_PROP_ARG_APPEND("Shape_input", shape_input);
   __WINDOW_PROP_ARG_APPEND("Layer", layer);
   __WINDOW_PROP_ARG_APPEND("Visible",  target_ec->visible ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("32bit",  target_ec->argb ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Hidden", target_ec->hidden ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Moving", target_ec->moving ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Focused", target_ec->focused ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Iconic", target_ec->iconic ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Sticky", target_ec->sticky ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Urgent", target_ec->urgent ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Fullscreen", target_ec->fullscreen ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Re_manage", target_ec->re_manage ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Take_focus", target_ec->take_focus ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Want_focus", target_ec->want_focus ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND_TYPE("E_Maximize_Policy", "0x%x", target_ec->maximized);
   __WINDOW_PROP_ARG_APPEND_TYPE("E_FullScreen_Policy", "%d", target_ec->fullscreen_policy);
   __WINDOW_PROP_ARG_APPEND_TYPE("E_Transient_Policy", "%d", target_ec->transient_policy);
   __WINDOW_PROP_ARG_APPEND("Override", target_ec->override ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Input_only", target_ec->input_only ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Dialog", target_ec->dialog ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Tooltip", target_ec->tooltip ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Redirected", target_ec->redirected ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Unredirected_single", target_ec->unredirected_single ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Shape_changed", target_ec->shape_changed ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Layer_block", target_ec->layer_block ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Ignored", target_ec->ignored ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("No_shape_cut", target_ec->no_shape_cut ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Maximize_override", target_ec->maximize_override ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Transformed", target_ec->transformed ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND_TYPE("Ignore_first_unmap", "%c", target_ec->ignore_first_unmap);
   __WINDOW_PROP_ARG_APPEND_TYPE("Video Client", "%d",target_ec->comp_data ? target_ec->comp_data->video_client : 0);

   if (target_ec->comp_data)
     {
        E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)target_ec->comp_data;
        Eina_List *l;
        E_Comp_Wl_Aux_Hint *hint;

        EINA_LIST_FOREACH(cdata->aux_hint.hints, l, hint)
          {
             __WINDOW_PROP_ARG_APPEND_TYPE("Aux_Hint", "[%d][%s][%s]", hint->id, hint->hint, hint->val);
          }
     }

   if (target_ec->comp_data)
     {
        int i;
        E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)target_ec->comp_data;

        if (cdata->sub.data)
          {
             __WINDOW_PROP_ARG_APPEND_TYPE("Subsurface Parent", "0x%x", e_client_util_win_get(cdata->sub.data->parent));
          }
        else
          {
             __WINDOW_PROP_ARG_APPEND_TYPE("Subsurface Parent", "0x%x", 0);
          }

        for ( i = 0 ; i < 2 ; ++i)
          {
             Eina_List *list;
             Eina_List *l;
             E_Client *child;
             char buffer[256] = {0,};

             if (i == 0) list = cdata->sub.list;
             else        list = cdata->sub.below_list;


             EINA_LIST_FOREACH(list, l, child)
               {
                  snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer) - 1, "0x%x, ", e_client_util_win_get(child));
               }

             if (i == 0) __WINDOW_PROP_ARG_APPEND("Subsurface Child List", buffer);
             else        __WINDOW_PROP_ARG_APPEND("Subsurface Below Child List", buffer);
          }
     }

   __WINDOW_PROP_ARG_APPEND_TYPE("Transform_count", "%d", e_client_transform_core_transform_count_get(target_ec));
   if (e_client_transform_core_transform_count_get(target_ec) > 0)
     {
        int i;
        int count = e_client_transform_core_transform_count_get(target_ec);

        __WINDOW_PROP_ARG_APPEND(" ", "[id] [move] [scale] [rotation] [keep_ratio] [viewport]");
        for (i = 0 ; i < count ; ++i)
          {
             double dsx, dsy;
             int x = 0, y = 0, rz = 0;
             int view_port = 0;
             int vx = 0, vy = 0, vw = 0, vh = 0;
             E_Util_Transform *transform = NULL;

             transform = e_client_transform_core_transform_get(target_ec, i);
             if (!transform) continue;

             e_util_transform_move_round_get(transform, &x, &y, NULL);
             e_util_transform_scale_get(transform, &dsx, &dsy, NULL);
             e_util_transform_rotation_round_get(transform, NULL, NULL, &rz);
             view_port = e_util_transform_viewport_flag_get(transform);

             if (view_port)
               {
                  e_util_transform_viewport_get(transform, &vx, &vy, &vw, &vh);
               }

             __WINDOW_PROP_ARG_APPEND_TYPE("Transform", "[%d] [%d, %d] [%2.1f, %2.1f] [%d] [%d :%d, %d, %d, %d]",
                                           i, x, y, dsx, dsy, rz, view_port, vx, vy, vw, vh);

             if (e_util_transform_bg_transform_flag_get(transform))
               {
                  e_util_transform_bg_move_round_get(transform, &x, &y, NULL);
                  e_util_transform_bg_scale_get(transform, &dsx, &dsy, NULL);
                  e_util_transform_bg_rotation_round_get(transform, NULL, NULL, &rz);

                  __WINDOW_PROP_ARG_APPEND_TYPE("Transform_BG", "--------- [%d] [%d, %d] [%2.1f, %2.1f] [%d]",
                                                i, x, y, dsx, dsy, rz);
               }
          }
     }

   /* Rotation info */
   __WINDOW_PROP_ARG_APPEND_TYPE("Rotation", "Support(%d) Type(%s)",
                                 target_ec->e.state.rot.support,
                                 target_ec->e.state.rot.type == E_CLIENT_ROTATION_TYPE_NORMAL ? "normal" : "dependent");

   if ((target_ec->e.state.rot.available_rots) &&
       (target_ec->e.state.rot.count))
     {
        int i = 0;
        char availables[256] = { 0, };

        for (i = 0; i < target_ec->e.state.rot.count; i++)
          {
             char tmp[16];
             snprintf(tmp, sizeof(tmp), "%d ", target_ec->e.state.rot.available_rots[i]);
             strncat(availables, tmp, sizeof(availables) - strlen(availables));
          }

        __WINDOW_PROP_ARG_APPEND_TYPE(" ", "Availables[%d] %s", target_ec->e.state.rot.count, availables);
     }
   else
     {
        __WINDOW_PROP_ARG_APPEND_TYPE(" ", "Availables[%d] N/A", target_ec->e.state.rot.count);
     }


   __WINDOW_PROP_ARG_APPEND_TYPE(" ", "Angle prev(%d) curr(%d) next(%d) reserve(%d) preferred(%d)",
                                 target_ec->e.state.rot.ang.prev,
                                 target_ec->e.state.rot.ang.curr,
                                 target_ec->e.state.rot.ang.next,
                                 target_ec->e.state.rot.ang.reserve,
                                 target_ec->e.state.rot.preferred_rot);

   __WINDOW_PROP_ARG_APPEND_TYPE(" ", "pending_change_request(%d) pending_show(%d) nopending_render(%d) wait_for_done(%d)",
                                 target_ec->e.state.rot.pending_change_request,
                                 target_ec->e.state.rot.pending_show,
                                 target_ec->e.state.rot.nopending_render,
                                 target_ec->e.state.rot.wait_for_done);

   if (target_ec->e.state.rot.geom_hint)
     {
        int i = 0;
        for (i = 0; i < 4; i++)
          {
             __WINDOW_PROP_ARG_APPEND_TYPE(" ", "Geometry hint[%d] %d,%d   %dx%d",
                                           i,
                                           target_ec->e.state.rot.geom[i].x,
                                           target_ec->e.state.rot.geom[i].y,
                                           target_ec->e.state.rot.geom[i].w,
                                           target_ec->e.state.rot.geom[i].h);
          }
     }
#undef __WINDOW_PROP_ARG_APPEND
#undef __WINDOW_PROP_ARG_APPEND_TYPE
}

static void
_msg_window_prop_append(Eldbus_Message_Iter *iter, uint32_t mode, const char *value)
{
   const static int WINDOW_ID_MODE = 0;
   const static int WINDOW_PID_MODE = 1;
   const static int WINDOW_NAME_MODE = 2;

   Eldbus_Message_Iter *array_of_ec;
   E_Client *ec;
   Evas_Object *o;
   uint64_t value_number = 0;
   Eina_Bool res = EINA_FALSE;

   eldbus_message_iter_arguments_append(iter, "a(ss)", &array_of_ec);

   if (mode == WINDOW_ID_MODE || mode == WINDOW_PID_MODE)
     {
        if (!value) value_number = 0;
        else
          {
             if (strlen(value) >= 2 && value[0] == '0' && value[1] == 'x')
               res = e_util_string_to_ulong(value, (unsigned long *)&value_number, 16);
             else
               res = e_util_string_to_ulong(value, (unsigned long *)&value_number, 10);

             EINA_SAFETY_ON_FALSE_GOTO(res, finish);
          }
     }

   for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
     {
        ec = evas_object_data_get(o, "E_Client");
        if (!ec) continue;

        if (mode == WINDOW_ID_MODE)
          {
             Ecore_Window win = e_client_util_win_get(ec);

             if (win == value_number)
               {
                  _msg_window_prop_client_append(array_of_ec, ec);
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
                  _msg_window_prop_client_append(array_of_ec, ec);
               }
          }
        else if (mode == WINDOW_NAME_MODE)
          {
             const char *name = e_client_util_name_get(ec) ?: "NO NAME";

             if (name != NULL && value != NULL)
               {
                  const char *find = strstr(name, value);

                  if (find)
                     _msg_window_prop_client_append(array_of_ec, ec);
               }
          }
     }

finish:
   eldbus_message_iter_container_close(iter, array_of_ec);
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
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   uint32_t mode = 0;
   const char *value = NULL;

   if (!eldbus_message_arguments_get(msg, "us", &mode, &value))
     {
        ERR("Error getting arguments.");
        return reply;
     }

   _msg_window_prop_append(eldbus_message_iter_get(reply), mode, value);
   return reply;
}

static Eldbus_Message *
_e_info_server_cb_topvwins_dump(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   const char *dir;
   Evas_Object *o;

   if (!eldbus_message_arguments_get(msg, "s", &dir))
     {
        ERR("Error getting arguments.");
        return reply;
     }

   for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
     {
        E_Client *ec = evas_object_data_get(o, "E_Client");
        char fname[PATH_MAX];
        Ecore_Window win;
        int rotation = 0;

        if (!ec) continue;
        if (e_client_util_ignored_get(ec)) continue;

        win = e_client_util_win_get(ec);
        if (ec->comp_data)
          rotation = ec->comp_data->scaler.buffer_viewport.buffer.transform * 90;
        snprintf(fname, sizeof(fname), "%s/0x%08x_%d.png", dir, win, rotation);

        e_info_server_dump_client(ec, fname);
     }

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
             video = (ec->comp_data->video_client) ? 1 : 0;
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
        strncpy(module_name, start, MIN(end - start, (sizeof module_name) - 1));
        module_name[end - start] = '\0';

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

#if !USE_WAYLAND_LOGGER
static void
_e_info_server_protocol_debug_func(struct wl_closure *closure, struct wl_resource *resource, int send)
{
   int i;
   struct argument_details arg;
   struct wl_object *target = &resource->object;
   struct wl_client *wc = resource->client;
   const char *signature = closure->message->signature;
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
   elog.type = send;
   elog.client_pid = client_pid;
   elog.target_id = target->id;
   snprintf(elog.name, PATH_MAX, "%s:%s", target->interface->name, closure->message->name);
   EINA_LIST_FOREACH(e_comp->connected_clients, l, cinfo)
     {
        if (cinfo->pid == client_pid)
          snprintf(elog.cmd, PATH_MAX, "%s", cinfo->name);
     }

   if (!e_info_protocol_rule_validate(&elog)) return;
   BUF_SNPRINTF("[%10.3f] %s%d%s%s@%u.%s(",
              time / 1000.0,
              send ? "Server -> Client [PID:" : "Server <- Client [PID:",
              client_pid, "] ",
              target->interface->name, target->id,
              closure->message->name);

   for (i = 0; i < closure->count; i++)
     {
        signature = get_next_argument(signature, &arg);
        if (i > 0) BUF_SNPRINTF(", ");

        switch (arg.type)
          {
           case 'u':
             BUF_SNPRINTF("%u", closure->args[i].u);
             break;
           case 'i':
             BUF_SNPRINTF("%d", closure->args[i].i);
             break;
           case 'f':
             BUF_SNPRINTF("%f",
             wl_fixed_to_double(closure->args[i].f));
             break;
           case 's':
             BUF_SNPRINTF("\"%s\"", closure->args[i].s);
             break;
           case 'o':
             if (closure->args[i].o)
               BUF_SNPRINTF("%s@%u", closure->args[i].o->interface->name, closure->args[i].o->id);
             else
               BUF_SNPRINTF("nil");
             break;
           case 'n':
             BUF_SNPRINTF("new id %s@", (closure->message->types[i]) ? closure->message->types[i]->name : "[unknown]");
             if (closure->args[i].n != 0)
               BUF_SNPRINTF("%u", closure->args[i].n);
             else
               BUF_SNPRINTF("nil");
             break;
           case 'a':
             BUF_SNPRINTF("array");
             break;
           case 'h':
             BUF_SNPRINTF("fd %d", closure->args[i].h);
             break;
          }
     }

   BUF_SNPRINTF("), cmd: %s", elog.cmd ? elog.cmd : "cmd is NULL");

   if (log_fp_ptrace)
     fprintf(log_fp_ptrace, "%s\n", strbuf);
   else
     INF("%s", strbuf);
}

#else

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
   snprintf(elog.name, PATH_MAX, "%s:%s", wl_resource_get_name(message->resource), message->message->name);
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
              wl_resource_get_name(message->resource),
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
                        wl_resource_get_name((struct wl_resource*)message->arguments[i].o),
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
#endif

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
#if !USE_WAYLAND_LOGGER
        wl_debug_server_debug_func_set(NULL);
#else
        if (e_info_protocol_logger)
          {
             wl_protocol_logger_destroy(e_info_protocol_logger);
             e_info_protocol_logger = NULL;
          }
#endif
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

#if !USE_WAYLAND_LOGGER
   wl_debug_server_debug_func_set((wl_server_debug_func_ptr)_e_info_server_protocol_debug_func);
#else
     if (e_info_protocol_logger)
       {
          wl_protocol_logger_destroy(e_info_protocol_logger);
          e_info_protocol_logger = NULL;
       }
     e_info_protocol_logger = wl_display_add_protocol_logger(e_comp->wl_comp_data->wl.disp, _e_info_server_protocol_debug_func2, NULL);
#endif

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
_e_info_server_cb_fps_info_get(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   static double old_fps = 0;

   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   char buf[128] = {};

   if (!e_comp->calc_fps)
     {
        e_comp->calc_fps = 1;
     }

   if (old_fps == e_comp->fps)
     {
        snprintf(buf, sizeof(buf), "no_update");
     }
   else if (e_comp->fps > 0.0)
     {
        snprintf(buf, sizeof(buf), "... FPS %3.1f", e_comp->fps);
        old_fps = e_comp->fps;
     }
   else
     {
        snprintf(buf, sizeof(buf), "... FPS N/A");
     }

   eldbus_message_arguments_append(reply, "s", buf);
   return reply;
}

static Eldbus_Message *
_e_info_server_cb_punch(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   int onoff = 0, x = 0, y = 0, w = 0, h = 0;
   int a = 0, r = 0, g = 0, b = 0;
   static Evas_Object *punch_obj = NULL;

   if (!eldbus_message_arguments_get(msg, "iiiiiiiii", &onoff, &x, &y, &w, &h, &a, &r, &g, &b))
     {
        ERR("Error getting arguments.");
        return reply;
     }

  if (!onoff)
    {
       if (punch_obj)
         evas_object_del(punch_obj);
       punch_obj = NULL;
       return reply;
    }

  if (!punch_obj)
    {
       punch_obj = evas_object_rectangle_add(e_comp->evas);
       evas_object_render_op_set(punch_obj, EVAS_RENDER_COPY);
    }

   evas_object_color_set(punch_obj, r, g, b, a);

   if (w == 0 || h == 0)
     evas_output_size_get(e_comp->evas, &w, &h);

   evas_object_move(punch_obj, x, y);
   evas_object_resize(punch_obj, w, h);
   evas_object_layer_set(punch_obj, EVAS_LAYER_MAX);
   evas_object_show(punch_obj);

   return reply;
}

static Eldbus_Message *
e_info_server_cb_transform_message(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   uint32_t enable, transform_id;
   uint32_t x, y, sx, sy, degree;
   uint32_t background;
   const char *value = NULL;
   uint64_t value_number;
   Evas_Object *o;
   E_Client *ec;
   Eina_Bool res = EINA_FALSE;

   if (!eldbus_message_arguments_get(msg, "siiiiiiii", &value, &transform_id, &enable, &x, &y, &sx, &sy, &degree, &background))
     {
        ERR("Error getting arguments.");
        return reply;
     }

   if (strlen(value) >= 2 && value[0] == '0' && value[1] == 'x')
     res = e_util_string_to_ulong(value, (unsigned long *)&value_number, 16);
   else
     res = e_util_string_to_ulong(value, (unsigned long *)&value_number, 10);

   EINA_SAFETY_ON_FALSE_RETURN_VAL(res, reply);

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
                                   __SLOT_ARG_APPEND_TYPE("[SLOT CLIENT]", "slot_client win:%08x name:%s \n", e_client_util_win_get(ec), e_client_util_name_get(ec) ?: "NO NAME");
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
          __SLOT_ARG_APPEND_TYPE("[SLOT INFO]", "[SLOT ADD EC as transform] slot_id:%02d (%08x)\n", slot_id, win);
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
             __SLOT_ARG_APPEND_TYPE("[SLOT INFO]", "[SLOT DEL EC] slot_id:%02d (%08x)\n", slot_id, win);
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
           __SLOT_ARG_APPEND_TYPE("[SLOT INFO]", "[SLOT ADD EC as resize] slot_id:%02d (%08x)\n", slot_id, win);
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
        if (start_split) evas_object_raise(desk->layout);
        else evas_object_lower(desk->layout);
        //evas_object_show(desk->layout);
        __SLOT_ARG_APPEND_TYPE("[SLOT INFO]", "[SLOT %s]", start_split ? "START" : "STOP");
     }
   else
     {
        __SLOT_ARG_APPEND_TYPE("[SLOT INFO]", "Wrong command........\n");
     }

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
   int stride, w, h, rotation;

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
        ERR("%s: e_client_util_ignored_get(ec) true. return\n", __func__);
        return ECORE_CALLBACK_PASS_ON;
     }

   buffer = e_pixmap_resource_get(ec->pixmap);
   if (!buffer) return ECORE_CALLBACK_PASS_ON;

   rotation = ec->comp_data->scaler.buffer_viewport.buffer.transform * 90;

   event_win = e_client_util_win_get(ec);
   switch (buffer->type)
     {
      case E_COMP_WL_BUFFER_TYPE_SHM:
        snprintf(fname, sizeof(fname), "buffer_commit_shm_0x%08x_%d", event_win, rotation);
        break;
      case E_COMP_WL_BUFFER_TYPE_NATIVE:
        snprintf(fname, sizeof(fname), "buffer_commit_native_0x%08x_%d", event_win, rotation);
        break;
      case E_COMP_WL_BUFFER_TYPE_VIDEO:
        snprintf(fname, sizeof(fname), "buffer_commit_video_0x%08x_%d", event_win, rotation);
        break;
      case E_COMP_WL_BUFFER_TYPE_TBM:
        snprintf(fname, sizeof(fname), "buffer_commit_tbm_0x%08x_%d", event_win, rotation);
        break;
      default:
        snprintf(fname, sizeof(fname), "buffer_commit_none_0x%08x_%d", event_win, rotation);
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
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   int start = 0;
   int count = 0;
   const char *path = NULL;

   if (!eldbus_message_arguments_get(msg, "iis", &start, &count, &path))
     {
        ERR("Error getting arguments.");
        return reply;
     }

   if (start == 1)
     {
        if (e_info_dump_running == 1)
          return reply;
        e_info_dump_running = 1;
        e_info_dump_count = 1;
        e_info_dump_path = _e_info_server_dump_directory_make(path);
        if (e_info_dump_path == NULL)
          {
             e_info_dump_running = 0;
             e_info_dump_count = 0;
             ERR("dump_buffers start fail\n");
          }
        else
          {
             /* start dump */
             tbm_surface_internal_dump_start(e_info_dump_path, e_comp->w, e_comp->h, count);
             tdm_helper_dump_start(e_info_dump_path, &e_info_dump_count);
             E_LIST_HANDLER_APPEND(e_info_dump_hdlrs, E_EVENT_CLIENT_BUFFER_CHANGE,
                               _e_info_server_cb_buffer_change, NULL);
          }
     }
   else
     {
        if (e_info_dump_running == 0)
          return reply;

        e_info_server_hook_call(E_INFO_SERVER_HOOK_BUFFER_DUMP_BEGIN);
        tdm_helper_dump_stop();
        tbm_surface_internal_dump_end();

        E_FREE_LIST(e_info_dump_hdlrs, ecore_event_handler_del);
        e_info_dump_hdlrs = NULL;
        if (e_info_dump_path)
          {
             free(e_info_dump_path);
             e_info_dump_path = NULL;
          }
        e_info_dump_count = 0;
        e_info_dump_running = 0;
        e_info_server_hook_call(E_INFO_SERVER_HOOK_BUFFER_DUMP_END);
     }

   return reply;
}

static void
_output_mode_msg_clients_append(Eldbus_Message_Iter *iter, E_Comp_Screen *e_comp_screen, int gl)
{
   Eldbus_Message_Iter *array_of_mode;
   Eldbus_Message_Iter *struct_of_mode;
   tdm_display *tdpy;
   tdm_output *output = NULL;
   tdm_output_conn_status status;
   const tdm_output_mode *mode = NULL;
   const tdm_output_mode *modes = NULL;
   tdm_error ret = TDM_ERROR_NONE;
   int i, j, count, mode_count, current;
   unsigned int preferred;

   eldbus_message_iter_arguments_append(iter, "a("SIGNATURE_OUTPUT_MODE_SERVER")",
                                        &array_of_mode);

   if (gl == 0)
     {
        eldbus_message_iter_arguments_append(array_of_mode, "("SIGNATURE_OUTPUT_MODE_SERVER")",
                                             &struct_of_mode);
        eldbus_message_iter_arguments_append(struct_of_mode, SIGNATURE_OUTPUT_MODE_SERVER,
                                             0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "none",
                                             0, 0, 0, 0);
        eldbus_message_iter_container_close(array_of_mode, struct_of_mode);

        eldbus_message_iter_container_close(iter, array_of_mode);

        return;
     }

   count = e_comp_screen->num_outputs;
   tdpy = e_comp_screen->tdisplay;

   for (i = 0; i < count; i++)
     {
        output = tdm_display_get_output(tdpy, i, &ret);
        if (ret != TDM_ERROR_NONE || output == NULL)
          continue;

        ret = tdm_output_get_conn_status(output, &status);
        if (ret != TDM_ERROR_NONE)
          continue;

        if (status == TDM_OUTPUT_CONN_STATUS_DISCONNECTED)
          {
             eldbus_message_iter_arguments_append(array_of_mode, "("SIGNATURE_OUTPUT_MODE_SERVER")",
                                                  &struct_of_mode);
             eldbus_message_iter_arguments_append(struct_of_mode, SIGNATURE_OUTPUT_MODE_SERVER,
                                                  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "none",
                                                  0, i, 0, 1);
             eldbus_message_iter_container_close(array_of_mode, struct_of_mode);

             continue;
          }

        ret = tdm_output_get_mode(output, &mode);
        if (ret != TDM_ERROR_NONE)
          continue;

        ret = tdm_output_get_available_modes(output, &modes, &mode_count);
        if (ret != TDM_ERROR_NONE)
          continue;

        for (j = 0; j < mode_count; j++)
          {
             eldbus_message_iter_arguments_append(array_of_mode, "("SIGNATURE_OUTPUT_MODE_SERVER")",
                                                  &struct_of_mode);
             current = 0;
             if (mode == modes + j) current = 1;

             preferred = 0;
             if (modes[j].type & TDM_OUTPUT_MODE_TYPE_PREFERRED) preferred = 1;

             eldbus_message_iter_arguments_append(struct_of_mode, SIGNATURE_OUTPUT_MODE_SERVER,
                                                  modes[j].hdisplay, modes[j].hsync_start, modes[j].hsync_end, modes[j].htotal,
                                                  modes[j].vdisplay, modes[j].vsync_start, modes[j].vsync_end, modes[j].vtotal,
                                                  modes[j].vrefresh, modes[j].vscan, modes[j].clock, preferred, modes[j].name,
                                                  current, i, 1, 1);
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

   if (mode == E_INFO_CMD_OUTPUT_MODE_GET)
     {
        e_comp_screen = e_comp->e_comp_screen;
        tdpy = e_comp_screen->tdisplay;

        if (tdpy != NULL)
          _output_mode_msg_clients_append(eldbus_message_iter_get(reply), e_comp_screen, 1);
        else
          _output_mode_msg_clients_append(eldbus_message_iter_get(reply), e_comp_screen, 0);
     }

   return reply;
}

#ifdef ENABLE_HWC_MULTI
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
     e_plane_hwc_trace_debug(onoff);

   if (onoff == 2)
     e_comp_screen_hwc_info_debug();

   return reply;
}

static Eldbus_Message *
e_info_server_cb_hwc(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   uint32_t onoff;

   if (!eldbus_message_arguments_get(msg, "i", &onoff))
     {
        ERR("Error getting arguments.");
        return reply;
     }

   if (!e_comp->hwc)
     {
        ERR("Error HWC is not initialized.");
        return reply;
     }

   if (onoff == 1)
     {
        e_comp->hwc_deactive = EINA_FALSE;
     }
   else if (onoff == 0)
     {
        e_comp_hwc_end("in runtime by e_info..");
        e_comp->hwc_deactive = EINA_TRUE;
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

             EINA_LIST_FOREACH(plane->pending_commit_data_list, data_l, data)
               {
                  Eldbus_Message_Iter* struct_of_pending_commit;

                  if (!data) continue;

                  eldbus_message_iter_arguments_append(array_of_pending_commit, "("VALUE_TYPE_FOR_PENDING_COMMIT")", &struct_of_pending_commit);

                  eldbus_message_iter_arguments_append
                    (struct_of_pending_commit, VALUE_TYPE_FOR_PENDING_COMMIT,
                      (unsigned int)plane,
                      plane->zpos,
                      (unsigned int)data,
                      (unsigned int)data->tsurface);

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
#endif

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
e_info_server_cb_aux_message(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   Eldbus_Message_Iter *opt_iter;
   const char *win_str, *key, *val, *opt;
   Eina_List *options = NULL;
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

   res = e_util_string_to_ulong(win_str, (unsigned long *)&win_id, 16);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(res, reply);

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
        if (e_client_util_ignored_get(ec)) continue;

        w = e_client_util_win_get(ec);
        if (w == win)
          return ec;
     }

   return NULL;
}

const static int KILL_ID_MODE = 1;
const static int KILL_NAME_MODE = 2;
const static int KILL_PID_MODE = 3;
const static int KILL_ALL_MODE = 4;

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

        snprintf(result, sizeof(result),
                 "[Server] killing creator(%s) of resource 0x%lx",
                 ec_name, (unsigned long)e_client_util_win_get(ec));
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
   Eldbus_Message_Iter *array_of_string;

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
                "[Server] killing creator(%s) of resource 0x%lx",
                e_client_util_name_get(ec) ?: "NO NAME", (unsigned long)win);
     }
   else if (mode >= KILL_NAME_MODE && mode <= KILL_ALL_MODE)
     {
        if (mode == KILL_NAME_MODE)
          count = _e_info_server_ec_kill(mode, (void *)str_value, array_of_string);
        else
          count = _e_info_server_ec_kill(mode, (void *)&uint64_value, array_of_string);

        snprintf(result, sizeof(result),
                 "\n[Server] killed %d client(s)", count);
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
_e_info_server_cb_wininfo(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply;
   Eina_Bool res;
   E_Client *ec;
   uint64_t win;
   Ecore_Window pwin;
   uint32_t res_id = 0;
   pid_t pid = -1;
   char layer_name[32];
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

#ifdef ENABLE_HWC_MULTI
   _e_info_server_ec_hwc_info_get(ec, &hwc, &pl_zpos);
#endif

   ecore_evas_screen_geometry_get(e_comp->ee, NULL, NULL, &dw, &dh);

   xright = dw - ec->x - ec->border_size * 2 - ec->w;
   ybelow = dh - ec->y - ec->border_size * 2 - ec->h;

   reply = eldbus_message_method_return_new(msg);

   eldbus_message_arguments_append(reply, VALUE_TYPE_REPLY_WININFO, (uint64_t)win, res_id,
                                   pid, e_client_util_name_get(ec) ?: "NO NAME",
                                   ec->x, ec->y, ec->w, ec->h, ec->layer, ec->visible,
                                   ec->argb, ec->visibility.opaque, ec->visibility.obscured,
                                   ec->iconic, evas_object_visible_get(ec->frame),
                                   ec->focused, hwc, pl_zpos, (uint64_t)pwin,
                                   layer_name, xright, ybelow, ec->border_size,
                                   ec->redirected);

   return reply;
}

static const Eldbus_Method methods[] = {
   { "get_window_info", NULL, ELDBUS_ARGS({"a("VALUE_TYPE_FOR_TOPVWINS")", "array of ec"}), _e_info_server_cb_window_info_get, 0 },
   { "compobjs", NULL, ELDBUS_ARGS({"a("SIGNATURE_COMPOBJS_CLIENT")", "array of comp objs"}), _e_info_server_cb_compobjs, 0 },
   { "subsurface", NULL, ELDBUS_ARGS({"a("SIGNATURE_SUBSURFACE")", "array of ec"}), _e_info_server_cb_subsurface, 0 },
   { "dump_topvwins", ELDBUS_ARGS({"s", "directory"}), NULL, _e_info_server_cb_topvwins_dump, 0 },
   { "eina_log_levels", ELDBUS_ARGS({"s", "eina log levels"}), NULL, _e_info_server_cb_eina_log_levels, 0 },
   { "eina_log_path", ELDBUS_ARGS({"s", "eina log path"}), NULL, _e_info_server_cb_eina_log_path, 0 },
#ifdef HAVE_DLOG
   { "dlog", ELDBUS_ARGS({"i", "using dlog"}), NULL, _e_info_server_cb_dlog_switch, 0},
#endif
   { "get_window_prop", ELDBUS_ARGS({"us", "query_mode_value"}), ELDBUS_ARGS({"a(ss)", "array_of_ec"}), _e_info_server_cb_window_prop_get, 0},
   { "get_connected_clients", NULL, ELDBUS_ARGS({"a(ss)", "array of ec"}), _e_info_server_cb_connected_clients_get, 0 },
   { "rotation_query", ELDBUS_ARGS({"i", "query_rotation"}), NULL, _e_info_server_cb_rotation_query, 0},
   { "rotation_message", ELDBUS_ARGS({"iii", "rotation_message"}), NULL, _e_info_server_cb_rotation_message, 0},
   { "get_res_lists", ELDBUS_ARGS({VALUE_TYPE_REQUEST_RESLIST, "client resource"}), ELDBUS_ARGS({"a("VALUE_TYPE_REPLY_RESLIST")", "array of client resources"}), _e_info_server_cb_res_lists_get, 0 },
   { "get_input_devices", NULL, ELDBUS_ARGS({"a("VALUE_TYPE_FOR_INPUTDEV")", "array of input"}), _e_info_server_cb_input_device_info_get, 0},
   { "protocol_trace", ELDBUS_ARGS({"s", "protocol_trace"}), NULL, _e_info_server_cb_protocol_trace, 0},
   { "protocol_rule", ELDBUS_ARGS({"sss", "protocol_rule"}), ELDBUS_ARGS({"s", "rule request"}), _e_info_server_cb_protocol_rule, 0},
   { "get_fps_info", NULL, ELDBUS_ARGS({"s", "fps request"}), _e_info_server_cb_fps_info_get, 0},
   { "punch", ELDBUS_ARGS({"iiiiiiiii", "punch_geometry"}), NULL, _e_info_server_cb_punch, 0},
   { "transform_message", ELDBUS_ARGS({"siiiiiiii", "transform_message"}), NULL, e_info_server_cb_transform_message, 0},
   { "dump_buffers", ELDBUS_ARGS({"iis", "start"}), NULL, _e_info_server_cb_buffer_dump, 0 },
   { "output_mode", ELDBUS_ARGS({SIGNATURE_OUTPUT_MODE_CLIENT, "output mode"}), ELDBUS_ARGS({"a("SIGNATURE_OUTPUT_MODE_SERVER")", "array of ec"}), _e_info_server_cb_output_mode, 0 },
#ifdef ENABLE_HWC_MULTI
   { "hwc_trace_message", ELDBUS_ARGS({"i", "hwc_trace_message"}), NULL, e_info_server_cb_hwc_trace_message, 0},
   { "hwc", ELDBUS_ARGS({"i", "hwc"}), NULL, e_info_server_cb_hwc, 0},
   { "show_plane_state", NULL, NULL, e_info_server_cb_show_plane_state, 0},
   { "show_pending_commit", NULL, ELDBUS_ARGS({"a("VALUE_TYPE_FOR_PENDING_COMMIT")", "array of pending commit"}), e_info_server_cb_show_pending_commit, 0},
#endif
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
   { "screen_rotation", ELDBUS_ARGS({"i", "value"}), NULL, _e_info_server_cb_screen_rotation, 0},
   { "get_win_under_touch", NULL, ELDBUS_ARGS({"i", "result"}), _e_info_server_cb_get_win_under_touch, 0 },
   { "kill_client", ELDBUS_ARGS({VALUE_TYPE_REQUEST_FOR_KILL, "window"}), ELDBUS_ARGS({"a"VALUE_TYPE_REPLY_KILL, "kill result"}), _e_info_server_cb_kill_client, 0 },
   { "wininfo", ELDBUS_ARGS({VALUE_TYPE_REQUEST_FOR_WININFO, "window"}), ELDBUS_ARGS({VALUE_TYPE_REPLY_WININFO, "window info"}), _e_info_server_cb_wininfo, 0 },
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
#if !USE_WAYLAND_LOGGER
   wl_debug_server_debug_func_set((wl_server_debug_func_ptr)_e_info_server_protocol_debug_func);
#else
   if (e_info_protocol_logger)
     {
        wl_protocol_logger_destroy(e_info_protocol_logger);
        e_info_protocol_logger = NULL;
     }

   e_info_protocol_logger = wl_display_add_protocol_logger(e_comp->wl_comp_data->wl.disp, _e_info_server_protocol_debug_func2, NULL);
#endif
    return EINA_TRUE;
}

static Eina_Bool
_e_info_server_dbus_init(void *data EINA_UNUSED)
{
   if (e_info_server.conn) return ECORE_CALLBACK_CANCEL;

   if (!e_info_server.conn)
     e_info_server.conn = eldbus_connection_get(ELDBUS_CONNECTION_TYPE_SYSTEM);

   if(!e_info_server.conn)
     {
        ecore_timer_add(1, _e_info_server_dbus_init, NULL);
        return ECORE_CALLBACK_CANCEL;
     }

   e_info_server.iface = eldbus_service_interface_register(e_info_server.conn,
                                                           PATH,
                                                           &iface_desc);
   EINA_SAFETY_ON_NULL_GOTO(e_info_server.iface, err);

   E_EVENT_INFO_ROTATION_MESSAGE = ecore_event_type_new();

   e_info_protocol_init();
   e_info_server_protocol_rule_path_init(getenv("E_INFO_RULE_FILE"));
   e_info_server_protocol_trace_path_init(getenv("E_INFO_TRACE_FILE"));
   e_main_hook_call(E_MAIN_HOOK_E_INFO_READY);

   return ECORE_CALLBACK_CANCEL;

err:
   e_info_server_shutdown();

   if (e_info_server.conn)
     {
        eldbus_name_release(e_info_server.conn, BUS, NULL, NULL);
        eldbus_connection_unref(e_info_server.conn);
        e_info_server.conn = NULL;
     }

   return ECORE_CALLBACK_CANCEL;
}

EINTERN int
e_info_server_init(void)
{
   if (eldbus_init() == 0) return 0;

   _e_info_server_dbus_init(NULL);

   return 1;
}

EINTERN int
e_info_server_shutdown(void)
{
   if (e_info_server.iface)
     {
        eldbus_service_interface_unregister(e_info_server.iface);
        e_info_server.iface = NULL;
     }

   if (e_info_server.conn)
     {
        eldbus_connection_unref(e_info_server.conn);
        e_info_server.conn = NULL;
     }

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
