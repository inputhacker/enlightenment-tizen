#include "e.h"
#include <tbm_bufmgr.h>

#define BUS "org.enlightenment.wm"
#define PATH "/org/enlightenment/wm"
#define IFACE "org.enlightenment.wm.info"

typedef struct _E_Info_Server
{
   Eldbus_Connection *conn;
   Eldbus_Service_Interface *iface;
} E_Info_Server;

static E_Info_Server e_info_server;

struct wl_drm;

struct wl_drm_buffer
{
   struct wl_resource *resource;
   struct wl_drm *drm;
   int32_t width, height;
   uint32_t format;
   const void *driver_format;
   int32_t offset[3];
   int32_t stride[3];
   void *driver_buffer;
};

static void
_msg_clients_append(Eldbus_Message_Iter *iter)
{
   Eldbus_Message_Iter *array_of_ec;
   E_Client *ec;
   Evas_Object *o;

   eldbus_message_iter_arguments_append(iter, "a(uuisiiiiibb)", &array_of_ec);

   // append clients.
   for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
     {
        Eldbus_Message_Iter* struct_of_ec;
        Ecore_Window win;
        uint32_t res_id;
        pid_t pid;

        ec = evas_object_data_get(o, "E_Client");
        if (!ec) continue;
        if (e_client_util_ignored_get(ec)) continue;

        win = e_client_util_win_get(ec);

        if (ec->pixmap)
          res_id = e_pixmap_res_id_get(ec->pixmap);
#ifdef HAVE_WAYLAND_ONLY
        if (ec->comp_data)
          {
             E_Comp_Wl_Client_Data *cdata = ec->comp_data;
             if (cdata->surface)
               wl_client_get_credentials(wl_resource_get_client(cdata->surface), &pid, NULL, NULL);
          }
#endif
        eldbus_message_iter_arguments_append(array_of_ec, "(uuisiiiiibb)", &struct_of_ec);

        eldbus_message_iter_arguments_append
           (struct_of_ec, "uuisiiiiibb",
            win,
            res_id,
            pid,
            e_client_util_name_get(ec) ?: "NO NAME",
            ec->x, ec->y, ec->w, ec->h, ec->layer,
            ec->visible, ec->argb);

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
        void *data = NULL;
        int w = 0, h = 0;
        Ecore_Evas *ee = NULL;
        Evas_Object *img = NULL;

        if (!ec) continue;
        if (e_client_util_ignored_get(ec)) continue;

        win = e_client_util_win_get(ec);
        snprintf(fname, sizeof(fname), "%s/0x%08x.png", dir, win);

#ifdef HAVE_WAYLAND_ONLY
        E_Comp_Wl_Buffer *buffer = e_pixmap_resource_get(ec->pixmap);
        if (!buffer) continue;

        if (buffer->type == E_COMP_WL_BUFFER_TYPE_SHM)
          {
             data = wl_shm_buffer_get_data(wl_shm_buffer_get(buffer->resource));
             w = wl_shm_buffer_get_stride(wl_shm_buffer_get(buffer->resource))/4;
             h = wl_shm_buffer_get_height(wl_shm_buffer_get(buffer->resource));
          }
        else if (buffer->type == E_COMP_WL_BUFFER_TYPE_NATIVE)
          {
             struct wl_drm_buffer *drm_buffer = wl_resource_get_user_data(buffer->resource);
             data = tbm_bo_map((tbm_bo)drm_buffer->driver_buffer, TBM_DEVICE_CPU, TBM_OPTION_READ).ptr;
             w = drm_buffer->stride[0]/4;
             h = drm_buffer->height;
          }
        else
          {
             ERR("Invalid resource:%u", wl_resource_get_id(buffer->resource));
          }
#endif

        EINA_SAFETY_ON_NULL_GOTO(data, err);

        ee = ecore_evas_buffer_new(1, 1);
        EINA_SAFETY_ON_NULL_GOTO(ee, err);

        img = evas_object_image_add(ecore_evas_get(ee));
        EINA_SAFETY_ON_NULL_GOTO(img, err);

        evas_object_image_alpha_set(img, ec->argb);
        evas_object_image_size_set(img, w, h);
        evas_object_image_data_set(img, data);

        if (!evas_object_image_save(img, fname, NULL, "compress=1 quality=100"))
          ERR("Cannot save window to '%s'", fname);

err:
#ifdef HAVE_WAYLAND_ONLY
        if (data && buffer->type == E_COMP_WL_BUFFER_TYPE_NATIVE)
          {
             struct wl_drm_buffer *drm_buffer = wl_resource_get_user_data(buffer->resource);
             tbm_bo_unmap((tbm_bo)(drm_buffer->driver_buffer));
          }
#endif

        if (img) evas_object_del(img);
        if (ee) ecore_evas_free(ee);
     }

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

static const Eldbus_Method methods[] = {
   { "get_window_info", NULL, ELDBUS_ARGS({"a(uuisiiiiibb)", "array of ec"}), _e_info_server_cb_window_info_get, 0 },
   { "dump_topvwins", ELDBUS_ARGS({"s", "directory"}), NULL, _e_info_server_cb_topvwins_dump, 0 },
   { "eina_log_levels", ELDBUS_ARGS({"s", "eina log levels"}), NULL, _e_info_server_cb_eina_log_levels, 0 },
   { "eina_log_path", ELDBUS_ARGS({"s", "eina log path"}), NULL, _e_info_server_cb_eina_log_path, 0 },
   { NULL, NULL, NULL, NULL, 0 }
};

static const Eldbus_Service_Interface_Desc iface_desc = {
     IFACE, methods, NULL, NULL, NULL, NULL
};

EINTERN int
e_info_server_init(void)
{
   eldbus_init();

   e_info_server.conn = eldbus_connection_get(ELDBUS_CONNECTION_TYPE_SYSTEM);
   EINA_SAFETY_ON_NULL_GOTO(e_info_server.conn, err);

   e_info_server.iface = eldbus_service_interface_register(e_info_server.conn,
                                                           PATH,
                                                           &iface_desc);
   EINA_SAFETY_ON_NULL_GOTO(e_info_server.iface, err);

   return 1;

err:
   e_info_server_shutdown();
   return 0;
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

   eldbus_shutdown();

   return 1;
}