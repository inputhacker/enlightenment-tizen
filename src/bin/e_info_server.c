#include "e.h"
#include "e_info_server.h"
#include <tbm_bufmgr.h>
#include <tbm_surface.h>
#include <tbm_surface_internal.h>
#include <tdm_helper.h>
#include <wayland-tbm-server.h>
#include "e_comp_wl.h"
#include "e_info_protocol.h"

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

void wl_map_for_each(struct wl_map *map, void *func, void *data);

#define BUS "org.enlightenment.wm"
#define PATH "/org/enlightenment/wm"
#define IFACE "org.enlightenment.wm.info"

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
} E_Info_Transform;

static E_Info_Server e_info_server;
static Eina_List    *e_info_transform_list = NULL;

static Eina_List    *e_info_dump_hdlrs;
static char         *e_info_dump_path;
static int           e_info_dump_running;
static int           e_info_dump_count;

//FILE pointer for protocol_trace
static FILE *log_fp_ptrace = NULL;

// Module list for module info
static Eina_List *module_hook = NULL;

#define BUF_SNPRINTF(fmt, ARG...) do { \
   str_l = snprintf(str_buff, str_r, fmt, ##ARG); \
   str_buff += str_l; \
   str_r -= str_l; \
} while(0)

#define VALUE_TYPE_FOR_TOPVWINS "uuisiiiiibbiibbbiis"
#define VALUE_TYPE_REQUEST_RESLIST "ui"
#define VALUE_TYPE_REPLY_RESLIST "ssi"
#define VALUE_TYPE_FOR_INPUTDEV "ssi"

static E_Info_Transform *_e_info_transform_new(E_Client *ec, int id, int enable, int x, int y, int sx, int sy, int degree, int keep_ratio);
static E_Info_Transform *_e_info_transform_find(E_Client *ec, int id);
static void              _e_info_transform_set(E_Info_Transform *transform, int enable, int x, int y, int sx, int sy, int degree, int keep_ratio);
static void              _e_info_transform_del(E_Info_Transform *transform);
static void              _e_info_transform_del_with_id(E_Client *ec, int id);

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
        uint32_t res_id = 0;
        pid_t pid = -1;
        char layer_name[32];
        int hwc = -1, pl_zpos = -999;

        ec = evas_object_data_get(o, "E_Client");
        if (!ec) continue;
        if (e_client_util_ignored_get(ec)) continue;

        win = e_client_util_win_get(ec);
        e_comp_layer_name_get(ec->layer, layer_name, sizeof(layer_name));

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
        if (e_comp->hwc && e_comp->hwc_fs)
          {
             Eina_List *l;
             E_Plane *ep;

             hwc = 0;

             E_Output *eout = e_output_find(ec->zone->output_id);
             EINA_LIST_FOREACH(eout->planes, l, ep)
               {
                  if (e_plane_is_fb_target(ep))
                    pl_zpos = ep->zpos;

                  if (ep->ec == ec)
                    {
                       hwc = 1;
                       pl_zpos = ep->zpos;
                       break;
                    }
               }
          }
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
            evas_object_visible_get(ec->frame), ec->focused, hwc, pl_zpos, layer_name);

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

#define wl_client_for_each(client, list)     \
   for (client = 0, client = wl_client_from_link((list)->next);   \
        wl_client_get_link(client) != (list);                     \
        client = wl_client_from_link(wl_client_get_link(client)->next))

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
   __WINDOW_PROP_ARG_APPEND_TYPE("Transform_count", "%d", e_client_transform_core_transform_count_get(target_ec));
   if (e_client_transform_core_transform_count_get(target_ec) > 0)
     {
        int i;
        int count = e_client_transform_core_transform_count_get(target_ec);

        __WINDOW_PROP_ARG_APPEND(" ", "[id] [move] [scale] [rotation] [keep_ratio] [viewport]");
        for (i = 0 ; i < count ; ++i)
          {
             double dx, dy, dsx, dsy, drz;
             int x, y, rz;
             int keep_ratio;
             int view_port;
             int vx, vy, vw, vh;

             E_Util_Transform *transform = e_client_transform_core_transform_get(target_ec, i);
             if (!transform) continue;

             e_util_transform_move_get(transform, &dx, &dy, NULL);
             e_util_transform_scale_get(transform, &dsx, &dsy, NULL);
             e_util_transform_rotation_get(transform, NULL, NULL, &drz);
             keep_ratio = e_util_transform_keep_ratio_get(transform);
             view_port = e_util_transform_viewport_flag_get(transform);

             x = (int)(dx + 0.5);
             y = (int)(dy + 0.5);
             rz = (int)(drz + 0.5);

             if (view_port)
               {
                  e_util_transform_viewport_get(transform, &vx, &vy, &vw, &vh);
               }
             else
               {
                  vx = 0;
                  vy = 0;
                  vw = 0;
                  vh = 0;
               }

             __WINDOW_PROP_ARG_APPEND_TYPE("Transform", "[%d] [%d, %d] [%2.1f, %2.1f] [%d] [%d] [%d :%d, %d, %d, %d]",
                                           i, x, y, dsx, dsy, rz, keep_ratio, view_port, vx, vy, vw, vh);
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
   int32_t value_number = 0;

   eldbus_message_iter_arguments_append(iter, "a(ss)", &array_of_ec);

   if (mode == WINDOW_ID_MODE || mode == WINDOW_PID_MODE)
     {
        if (!value) value_number = 0;
        else
          {
             if (strlen(value) >= 2 && value[0] == '0' && value[1] == 'x')
                sscanf(value, "%x", &value_number);
             else
                sscanf(value, "%d", &value_number);
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

        if (!ec) continue;
        if (e_client_util_ignored_get(ec)) continue;

        win = e_client_util_win_get(ec);
        snprintf(fname, sizeof(fname), "%s/0x%08x.png", dir, win);

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
        unsigned int transform = 0, visible = 0, alpha = 0, ignore = 0, maskobj = 0, video = 0;
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
             name = e_client_util_name_get(ec);
             if (!name)
               name = "NO NAME";
          }

        eldbus_message_iter_arguments_append(array_of_ec, "("SIGNATURE_SUBSURFACE")", &struct_of_ec);

        eldbus_message_iter_arguments_append
           (struct_of_ec, SIGNATURE_SUBSURFACE,
            win, parent, buf_id, x, y, w, h, transform, visible, alpha, ignore, maskobj, video, bgrect, name);

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
   static int old_stderr = -1;
   int  log_fd = -1;
   FILE *log_fl;

   if (!eldbus_message_arguments_get(msg, "s", &path) || !path)
     {
        ERR("Error getting arguments.");
        return reply;
     }

   if (old_stderr == -1)
     old_stderr = dup(STDOUT_FILENO);

   log_fl = fopen(path, "a");
   if (!log_fl)
     {
        ERR("failed: open file(%s)\n", path);
        return reply;
     }

   fflush(stderr);
   close(STDOUT_FILENO);

   setvbuf(log_fl, NULL, _IOLBF, 512);
   log_fd = fileno(log_fl);

   dup2(log_fd, STDOUT_FILENO);
   fclose(log_fl);

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

   if (!log_fp_ptrace) return;
   if (wc) wl_client_get_credentials(wc, &client_pid, NULL, NULL);

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
   fprintf(log_fp_ptrace, "[%10.3f] %s%d%s%s@%u.%s(",
              time / 1000.0,
              send ? "Server -> Client [PID:" : "Server <- Client [PID:",
              client_pid, "] ",
              target->interface->name, target->id,
              closure->message->name);

   for (i = 0; i < closure->count; i++)
     {
        signature = get_next_argument(signature, &arg);
        if (i > 0) fprintf(log_fp_ptrace, ", ");

        switch (arg.type)
          {
           case 'u':
             fprintf(log_fp_ptrace, "%u", closure->args[i].u);
             break;
           case 'i':
             fprintf(log_fp_ptrace, "%d", closure->args[i].i);
             break;
           case 'f':
             fprintf(log_fp_ptrace, "%f",
             wl_fixed_to_double(closure->args[i].f));
             break;
           case 's':
             fprintf(log_fp_ptrace, "\"%s\"", closure->args[i].s);
             break;
           case 'o':
             if (closure->args[i].o)
               fprintf(log_fp_ptrace, "%s@%u", closure->args[i].o->interface->name, closure->args[i].o->id);
             else
               fprintf(log_fp_ptrace, "nil");
             break;
           case 'n':
             fprintf(log_fp_ptrace, "new id %s@", (closure->message->types[i]) ? closure->message->types[i]->name : "[unknown]");
             if (closure->args[i].n != 0)
               fprintf(log_fp_ptrace, "%u", closure->args[i].n);
             else
               fprintf(log_fp_ptrace, "nil");
             break;
           case 'a':
             fprintf(log_fp_ptrace, "array");
             break;
           case 'h':
             fprintf(log_fp_ptrace, "fd %d", closure->args[i].h);
             break;
          }
     }

   fprintf(log_fp_ptrace, "), cmd: %s\n", elog.cmd? : "cmd is NULL");
}

static void
_e_info_server_protocol_debug_func_elog(struct wl_closure *closure, struct wl_resource *resource, int send)
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

   if (wc) wl_client_get_credentials(wc, &client_pid, NULL, NULL);

   clock_gettime(CLOCK_REALTIME, &tp);
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
        wl_debug_server_debug_func_set(NULL);
        return reply;
     }

   if (!strncmp(path, "elog", 4))
     {
        wl_debug_server_debug_func_set((wl_server_debug_func_ptr)_e_info_server_protocol_debug_func_elog);
        return reply;
     }

   log_fp_ptrace = fopen(path, "a");

   if (!log_fp_ptrace)
     {
        ERR("failed: open file(%s)\n", path);
        return reply;
     }

   setvbuf(log_fp_ptrace, NULL, _IOLBF, 512);
   wl_debug_server_debug_func_set((wl_server_debug_func_ptr)_e_info_server_protocol_debug_func);

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
_e_info_server_hook_call(const char *module_name, const char *log_path)
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

   _e_info_server_hook_call(module_name, path);

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

   _e_info_server_hook_call("keygrab", path);

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
        if(e_comp->nocomp && e_comp->nocomp_ec)
          snprintf(buf, sizeof(buf), "... FPS %3.1f(by 0x%x : %s)", e_comp->fps, e_client_util_win_get(e_comp->nocomp_ec), e_client_util_name_get(e_comp->nocomp_ec) ?: " ");
        else
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
   uint32_t keep_ratio;
   const char *value = NULL;
   int32_t value_number;
   Evas_Object *o;
   E_Client *ec;

   if (!eldbus_message_arguments_get(msg, "siiiiiiii", &value, &transform_id, &enable, &x, &y, &sx, &sy, &degree, &keep_ratio))
     {
        ERR("Error getting arguments.");
        return reply;
     }

   if (strlen(value) >= 2 && value[0] == '0' && value[1] == 'x')
      sscanf(value, "%x", &value_number);
   else
      sscanf(value, "%d", &value_number);

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
             _e_info_transform_set(transform_info, enable, x, y, sx, sy, degree, keep_ratio);

             if (!enable)
                _e_info_transform_del_with_id(ec, transform_id);
          }
        else
          {
             if (enable)
               {
                  _e_info_transform_new(ec, transform_id, enable, x, y, sx, sy, degree, keep_ratio);
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
   int stride;
   int w;
   int h;

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

   event_win = e_client_util_win_get(ec);
   switch (buffer->type)
     {
      case E_COMP_WL_BUFFER_TYPE_SHM:
        snprintf(fname, sizeof(fname), "buffer_commit_shm_0x%08x", event_win);
        break;
      case E_COMP_WL_BUFFER_TYPE_NATIVE:
        snprintf(fname, sizeof(fname), "buffer_commit_native_0x%08x", event_win);
        break;
      case E_COMP_WL_BUFFER_TYPE_VIDEO:
        snprintf(fname, sizeof(fname), "buffer_commit_video_0x%08x", event_win);
        break;
      case E_COMP_WL_BUFFER_TYPE_TBM:
        snprintf(fname, sizeof(fname), "buffer_commit_tbm_0x%08x", event_win);
        break;
      default:
        snprintf(fname, sizeof(fname), "buffer_commit_none_0x%08x", event_win);
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
        e_comp->hwc_fs = EINA_TRUE;
     }
   else if (onoff == 0)
     {
        e_comp_hwc_end("in runtime by e_info..");
        e_comp->hwc_fs = EINA_FALSE;
     }

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
   int32_t win_id = 0;
   E_Client *ec;
   Evas_Object *o;

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

   sscanf(win_str, "%x", &win_id);
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

static const Eldbus_Method methods[] = {
   { "get_window_info", NULL, ELDBUS_ARGS({"a("VALUE_TYPE_FOR_TOPVWINS")", "array of ec"}), _e_info_server_cb_window_info_get, 0 },
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
#ifdef ENABLE_HWC_MULTI
   { "hwc_trace_message", ELDBUS_ARGS({"i", "hwc_trace_message"}), NULL, e_info_server_cb_hwc_trace_message, 0},
   { "hwc", ELDBUS_ARGS({"i", "hwc"}), NULL, e_info_server_cb_hwc, 0},
#endif
   { "get_keymap", NULL, ELDBUS_ARGS({"hi", "keymap fd"}), _e_info_server_cb_keymap_info_get, 0},
   { "effect_control", ELDBUS_ARGS({"i", "effect_control"}), NULL, e_info_server_cb_effect_control, 0},
   { "get_keygrab_status", ELDBUS_ARGS({"s", "get_keygrab_status"}), NULL, _e_info_server_cb_keygrab_status_get, 0},
   { "get_module_info", ELDBUS_ARGS({"ss", "get_module_info"}), NULL, _e_info_server_cb_module_info_get, 0},
   { "aux_msg", ELDBUS_ARGS({"s","window id" }, {"s", "key"}, {"s", "value"}, {"as", "options"}), NULL, e_info_server_cb_aux_message, 0},
   { "scrsaver", ELDBUS_ARGS({SIGNATURE_SCRSAVER_CLIENT, "scrsaver_params"}), ELDBUS_ARGS({SIGNATURE_SCRSAVER_SERVER, "scrsaver_result"}), _e_info_server_cb_scrsaver, 0},
   { "slot_message", ELDBUS_ARGS({"iiiiii", "slot_message"}), ELDBUS_ARGS({"a(ss)", "array of ec"}), e_info_server_cb_slot_message, 0},
   { "desktop_geometry_set", ELDBUS_ARGS({"iiii", "Geometry"}), NULL, _e_info_server_cb_desktop_geometry_set, 0},
   { NULL, NULL, NULL, NULL, 0 }
};

static const Eldbus_Service_Interface_Desc iface_desc = {
     IFACE, methods, NULL, NULL, NULL, NULL
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
_e_info_transform_new(E_Client *ec, int id, int enable, int x, int y, int sx, int sy, int degree, int keep_ratio)
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
        _e_info_transform_set(result, enable, x, y, sx, sy, degree, keep_ratio);
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
_e_info_transform_set(E_Info_Transform *transform, int enable, int x, int y, int sx, int sy, int degree, int keep_ratio)
{
   if (!transform) return;
   if (!transform->transform) return;

   e_util_transform_move(transform->transform, (double)x, (double)y, 0.0);
   e_util_transform_scale(transform->transform, (double)sx / 100.0, (double)sy / 100.0, 1.0);
   e_util_transform_rotation(transform->transform, 0.0, 0.0, degree);
   e_util_transform_keep_ratio_set(transform->transform, keep_ratio);

   if (enable)
      e_client_transform_core_add(transform->ec, transform->transform);
   else
      e_client_transform_core_remove(transform->ec, transform->transform);

   e_client_transform_core_update(transform->ec);
}

static void
_e_info_transform_del(E_Info_Transform *transform)
{
   if (!transform) return;

   e_info_transform_list = eina_list_remove(e_info_transform_list, transform);
   e_client_transform_core_remove(transform->ec, transform->transform);
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
