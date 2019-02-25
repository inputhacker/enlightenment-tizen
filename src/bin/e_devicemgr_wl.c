#include "e_devicemgr_private.h"

#ifdef HAVE_CYNARA
#define E_DEVMGR_CYNARA_ERROR_CHECK_GOTO(func_name, ret, label) \
  do \
    { \
       if (EINA_UNLIKELY(CYNARA_API_SUCCESS != ret)) \
          { \
             _e_devicemgr_util_cynara_log(func_name, ret); \
             goto label; \
          } \
    } \
  while (0)

static void
_e_devicemgr_util_cynara_log(const char *func_name, int err)
{
#define CYNARA_BUFSIZE 128
   char buf[CYNARA_BUFSIZE] = "\0";
   int ret;

   ret = cynara_strerror(err, buf, CYNARA_BUFSIZE);
   if (ret != CYNARA_API_SUCCESS)
     {
        DMDBG("Failed to cynara_strerror: %d (error log about %s: %d)\n", ret, func_name, err);
        return;
     }
   DMDBG("%s is failed: %s\n", func_name, buf);
}

static Eina_Bool
_e_devicemgr_util_do_privilege_check(struct wl_client *client, int socket_fd, const char *rule)
{
   int ret, pid;
   char *clientSmack=NULL, *uid=NULL, *client_session=NULL;
   Eina_Bool res = EINA_FALSE;

   /* If initialization of cynara has been failed, let's not to do further permission checks. */
   if (e_devicemgr->wl_data->p_cynara == NULL && e_devicemgr->wl_data->cynara_initialized) return EINA_TRUE;

   ret = cynara_creds_socket_get_client(socket_fd, CLIENT_METHOD_SMACK, &clientSmack);
   E_DEVMGR_CYNARA_ERROR_CHECK_GOTO("cynara_creds_socket_get_client", ret, finish);

   ret = cynara_creds_socket_get_user(socket_fd, USER_METHOD_UID, &uid);
   E_DEVMGR_CYNARA_ERROR_CHECK_GOTO("cynara_creds_socket_get_user", ret, finish);

   ret = cynara_creds_socket_get_pid(socket_fd, &pid);
   E_DEVMGR_CYNARA_ERROR_CHECK_GOTO("cynara_creds_socket_get_pid", ret, finish);

   client_session = cynara_session_from_pid(pid);

   ret = cynara_check(e_devicemgr->wl_data->p_cynara, clientSmack, client_session, uid, rule);

   if (CYNARA_API_ACCESS_ALLOWED == ret)
        res = EINA_TRUE;

finish:
   E_FREE(client_session);
   E_FREE(clientSmack);
   E_FREE(uid);

   return res;
}
#endif

static void
_e_devicemgr_wl_device_cb_axes_select(struct wl_client *client, struct wl_resource *resource, struct wl_array *axes)
{
   return;
}

static void
_e_devicemgr_wl_device_cb_release(struct wl_client *client, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static const struct tizen_input_device_interface _e_devicemgr_wl_device_interface =
{
   _e_devicemgr_wl_device_cb_axes_select,
   _e_devicemgr_wl_device_cb_release,
};

static void
_e_devicemgr_wl_device_cb_unbind(struct wl_resource *resource)
{
   E_Devicemgr_Input_Device *dev;
   E_Devicemgr_Input_Device_User_Data *device_user_data;

   if (!(device_user_data = wl_resource_get_user_data(resource))) return;

   dev = device_user_data->dev;

   device_user_data->dev = NULL;
   device_user_data->dev_mgr_res = NULL;
   device_user_data->seat_res = NULL;
   E_FREE(device_user_data);

   if (!dev) return;

   dev->resources = eina_list_remove(dev->resources, resource);
}

void
e_devicemgr_wl_device_update(E_Devicemgr_Input_Device *dev)
{
   struct wl_array axes;
   Eina_List *l;
   struct wl_resource *res;

   wl_array_init(&axes);

   EINA_LIST_FOREACH(dev->resources, l, res)
     {
        tizen_input_device_send_device_info(res, dev->name, dev->clas, dev->subclas, &axes);
     }
}

void
e_devicemgr_wl_device_add(E_Devicemgr_Input_Device *dev)
{
   struct wl_resource *res, *seat_res, *dev_mgr_res;
   Eina_List *l, *ll;
   uint32_t serial;
   struct wl_client *wc;
   E_Devicemgr_Input_Device_User_Data *device_user_data;
   struct wl_array axes;

   /* TODO: find the seat corresponding to event */
   serial = wl_display_next_serial(e_comp_wl->wl.disp);
   wl_array_init(&axes);

   EINA_LIST_FOREACH(e_comp_wl->seat.resources, l, seat_res)
     {
        wc = wl_resource_get_client(seat_res);

        EINA_LIST_FOREACH(e_devicemgr->wl_data->resources, ll, dev_mgr_res)
          {
             if (wl_resource_get_client(dev_mgr_res) != wc) continue;
             res = wl_resource_create(wc, &tizen_input_device_interface, 1, 0);
             if (!res)
                 {
                  DMERR("Could not create tizen_input_device resource");
                  break;
                }

             device_user_data = E_NEW(E_Devicemgr_Input_Device_User_Data, 1);
             if (!device_user_data)
               {
                  DMERR("Failed to allocate memory for input device user data\n");
                  break;
               }
             device_user_data->dev = dev;
             device_user_data->dev_mgr_res = dev_mgr_res;
             device_user_data->seat_res = seat_res;

             dev->resources = eina_list_append(dev->resources, res);
             wl_resource_set_implementation(res, &_e_devicemgr_wl_device_interface, device_user_data,
                                            _e_devicemgr_wl_device_cb_unbind);
             tizen_input_device_manager_send_device_add(dev_mgr_res, serial, dev->identifier, res, seat_res);
             tizen_input_device_send_device_info(res, dev->name, dev->clas, dev->subclas, &axes);
          }
     }
}

void
e_devicemgr_wl_device_del(E_Devicemgr_Input_Device *dev)
{
   struct wl_client *wc;
   Eina_List *l, *ll, *lll;
   struct wl_resource *res, *seat_res, *dev_mgr_res;
   uint32_t serial;
   E_Devicemgr_Input_Device_User_Data *device_user_data;

   serial = wl_display_next_serial(e_comp_wl->wl.disp);

   /* TODO: find the seat corresponding to event */
   EINA_LIST_FOREACH(e_comp_wl->seat.resources, l, seat_res)
     {
        wc = wl_resource_get_client(seat_res);
        EINA_LIST_FOREACH(e_devicemgr->wl_data->resources, ll, dev_mgr_res)
          {
             if (wl_resource_get_client(dev_mgr_res) != wc) continue;
             EINA_LIST_FOREACH(dev->resources, lll, res)
               {
                  if (wl_resource_get_client(res) != wc) continue;
                  device_user_data = wl_resource_get_user_data(res);
                  if (!device_user_data) continue;
                  if (device_user_data->dev_mgr_res != dev_mgr_res)
                    continue;
                  if (device_user_data->seat_res != seat_res)
                    continue;

                  tizen_input_device_manager_send_device_remove(dev_mgr_res, serial, dev->identifier, res, seat_res);
               }
          }
     }

   EINA_LIST_FREE(dev->resources, res)
     {
        device_user_data = wl_resource_get_user_data(res);
        if (device_user_data)
          {
             device_user_data->dev = NULL;
             device_user_data->dev_mgr_res = NULL;
             device_user_data->seat_res = NULL;
             E_FREE(device_user_data);
          }

        wl_resource_set_user_data(res, NULL);
     }
}

void
e_devicemgr_wl_detent_send_event(int detent)
{
   E_Devicemgr_Input_Device *input_dev;
   struct wl_resource *dev_res;
   struct wl_client *wc;
   Eina_List *l, *ll;
   wl_fixed_t f_value;
   E_Client *ec;

   ec = e_client_focused_get();

   if (!ec) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (ec->ignored) return;
   if (!ec->comp_data || !ec->comp_data->surface) return;

   f_value = wl_fixed_from_double(detent * 1.0);
   wc = wl_resource_get_client(ec->comp_data->surface);

   EINA_LIST_FOREACH(e_devicemgr->device_list, l, input_dev)
     {
        if (!strncmp(input_dev->name, DETENT_DEVICE_NAME, sizeof(DETENT_DEVICE_NAME)))
          {
             EINA_LIST_FOREACH(input_dev->resources, ll, dev_res)
               {
                  if (wl_resource_get_client(dev_res) != wc) continue;
                  tizen_input_device_send_axis(dev_res, TIZEN_INPUT_DEVICE_AXIS_TYPE_DETENT, f_value);
               }
          }
     }
}

void
e_devicemgr_wl_block_send_expired(struct wl_resource *resource)
{
   if (!resource) return;
   tizen_input_device_manager_send_block_expired(resource);
}

static void
_e_devicemgr_wl_cb_block_events(struct wl_client *client, struct wl_resource *resource, uint32_t serial, uint32_t clas, uint32_t duration)
{
   int ret = TIZEN_INPUT_DEVICE_MANAGER_ERROR_NONE;

#ifdef HAVE_CYNARA
   if (EINA_FALSE == _e_devicemgr_util_do_privilege_check(client, wl_client_get_fd(client),
                                                          "http://tizen.org/privilege/internal/inputdevice.block"))
     {
        DMERR("block_events request:priv check failed");
        tizen_input_device_manager_send_error(resource, TIZEN_INPUT_DEVICE_MANAGER_ERROR_NO_PERMISSION);
        return;
     }
#endif

   ret = e_devicemgr_block_add(client, resource, clas, duration);
   tizen_input_device_manager_send_error(resource, ret);
}

static void
_e_devicemgr_wl_cb_unblock_events(struct wl_client *client, struct wl_resource *resource, uint32_t serial)
{
   int ret;

#ifdef HAVE_CYNARA
   if (EINA_FALSE == _e_devicemgr_util_do_privilege_check(client, wl_client_get_fd(client),
                                                          "http://tizen.org/privilege/internal/inputdevice.block"))
     {
        DMERR("unblock_events request:priv check failed");
        tizen_input_device_manager_send_error(resource, TIZEN_INPUT_DEVICE_MANAGER_ERROR_NO_PERMISSION);
        return;
     }
#endif

   ret = e_devicemgr_block_remove(client);
   tizen_input_device_manager_send_error(resource, ret);
}

static void
_e_devicemgr_wl_cb_init_generator(struct wl_client *client, struct wl_resource *resource, uint32_t clas)
{
   int ret = TIZEN_INPUT_DEVICE_MANAGER_ERROR_NONE;

#ifdef HAVE_CYNARA
   if (EINA_FALSE == _e_devicemgr_util_do_privilege_check(client, wl_client_get_fd(client),
                                                          "http://tizen.org/privilege/inputgenerator"))
     {
        DMERR("init_generator request:priv check failed");
        tizen_input_device_manager_send_error(resource, TIZEN_INPUT_DEVICE_MANAGER_ERROR_NO_PERMISSION);
        return;
     }
#endif

   ret = e_devicemgr_inputgen_add(client, resource, clas, INPUT_GENERATOR_DEVICE);
   tizen_input_device_manager_send_error(resource, ret);
}

static void
_e_devicemgr_wl_cb_init_generator_with_name(struct wl_client *client, struct wl_resource *resource, uint32_t clas, const char *name)
{
   int ret = TIZEN_INPUT_DEVICE_MANAGER_ERROR_NONE;

#ifdef HAVE_CYNARA
   if (EINA_FALSE == _e_devicemgr_util_do_privilege_check(client, wl_client_get_fd(client),
                                                          "http://tizen.org/privilege/inputgenerator"))
     {
        DMERR("init_generator_with_name request:priv check failed");
        tizen_input_device_manager_send_error(resource, TIZEN_INPUT_DEVICE_MANAGER_ERROR_NO_PERMISSION);
        return;
     }
#endif

   ret = e_devicemgr_inputgen_add(client, resource, clas, name);
   tizen_input_device_manager_send_error(resource, ret);
}

static void
_e_devicemgr_wl_cb_deinit_generator(struct wl_client *client, struct wl_resource *resource, uint32_t clas)
{
   int ret = TIZEN_INPUT_DEVICE_MANAGER_ERROR_NONE;

#ifdef HAVE_CYNARA
   if (EINA_FALSE == _e_devicemgr_util_do_privilege_check(client, wl_client_get_fd(client),
                                                          "http://tizen.org/privilege/inputgenerator"))
     {
        DMERR("deinit_generator request:priv check failed");
        tizen_input_device_manager_send_error(resource, TIZEN_INPUT_DEVICE_MANAGER_ERROR_NO_PERMISSION);
        return;
     }
#endif

   e_devicemgr_inputgen_remove(client, resource, clas);
   tizen_input_device_manager_send_error(resource, ret);
}

static void
_e_devicemgr_wl_cb_generate_key(struct wl_client *client, struct wl_resource *resource, const char *keyname, uint32_t pressed)
{
   int ret = TIZEN_INPUT_DEVICE_MANAGER_ERROR_NONE;

#ifdef HAVE_CYNARA
   if (EINA_FALSE == _e_devicemgr_util_do_privilege_check(client, wl_client_get_fd(client),
                                                          "http://tizen.org/privilege/inputgenerator"))
     {
        DMERR("generate_key request:priv check failed");
        tizen_input_device_manager_send_error(resource, TIZEN_INPUT_DEVICE_MANAGER_ERROR_NO_PERMISSION);
        return;
     }
#endif

   ret = e_devicemgr_inputgen_generate_key(client, resource, keyname, (Eina_Bool)!!pressed);
   tizen_input_device_manager_send_error(resource, ret);
}

static void
_e_devicemgr_wl_cb_generate_pointer(struct wl_client *client, struct wl_resource *resource, uint32_t type, uint32_t x, uint32_t y, uint32_t button)
{
   int ret = TIZEN_INPUT_DEVICE_MANAGER_ERROR_NONE;

#ifdef HAVE_CYNARA
   if (EINA_FALSE == _e_devicemgr_util_do_privilege_check(client, wl_client_get_fd(client),
                                                          "http://tizen.org/privilege/inputgenerator"))
     {
        DMERR("_generate_pointer request:priv check failed");
        tizen_input_device_manager_send_error(resource, TIZEN_INPUT_DEVICE_MANAGER_ERROR_NO_PERMISSION);
        return;
     }
#endif

   ret = e_devicemgr_inputgen_generate_pointer(client, resource, type, x, y, button);
   tizen_input_device_manager_send_error(resource, ret);
}

static void
_e_devicemgr_wl_cb_generate_touch(struct wl_client *client, struct wl_resource *resource, uint32_t type, uint32_t x, uint32_t y, uint32_t finger)
{
   int ret = TIZEN_INPUT_DEVICE_MANAGER_ERROR_NONE;

#ifdef HAVE_CYNARA
   if (EINA_FALSE == _e_devicemgr_util_do_privilege_check(client, wl_client_get_fd(client), "http://tizen.org/privilege/inputgenerator"))
     {
        DMERR("_e_input_devmgr_cb_generate_touch:priv check failed");
        tizen_input_device_manager_send_error(resource, TIZEN_INPUT_DEVICE_MANAGER_ERROR_NO_PERMISSION);
        return;
     }
#endif

   ret = e_devicemgr_inputgen_generate_touch(client, resource, type, x, y, finger);
   tizen_input_device_manager_send_error(resource, ret);
}

static void
_e_devicemgr_wl_cb_pointer_warp(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface, wl_fixed_t x, wl_fixed_t y)
{
   int ret = TIZEN_INPUT_DEVICE_MANAGER_ERROR_NONE;

   ret = e_devicemgr_input_pointer_warp(client, resource, surface, x, y);

   tizen_input_device_manager_send_error(resource, ret);
}

static void
_e_devicemgr_wl_cb_destroy(struct wl_client *client, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void
_e_devicemgr_wl_cb_generate_axis(struct wl_client *client, struct wl_resource *resource, uint32_t type, wl_fixed_t value)
{
   int ret = TIZEN_INPUT_DEVICE_MANAGER_ERROR_NONE;

#ifdef HAVE_CYNARA
   if (EINA_FALSE == _e_devicemgr_util_do_privilege_check(client, wl_client_get_fd(client),
                                                          "http://tizen.org/privilege/inputgenerator"))
     {
        DMERR("_generate_pointer request:priv check failed");
        tizen_input_device_manager_send_error(resource, TIZEN_INPUT_DEVICE_MANAGER_ERROR_NO_PERMISSION);
        return;
     }
#endif

   if (type == TIZEN_INPUT_DEVICE_MANAGER_AXIS_TYPE_WHEEL ||
       type == TIZEN_INPUT_DEVICE_MANAGER_AXIS_TYPE_HWHEEL)
     ret = e_devicemgr_inputgen_generate_wheel(client, resource, type, (int)wl_fixed_to_double(value));
   else
     ret = e_devicemgr_inputgen_touch_axis_store(client, resource, type, wl_fixed_to_double(value));
   tizen_input_device_manager_send_error(resource, ret);
}

static const struct tizen_input_device_manager_interface _e_devicemgr_wl_implementation = {
   _e_devicemgr_wl_cb_block_events,
   _e_devicemgr_wl_cb_unblock_events,
   _e_devicemgr_wl_cb_init_generator,
   _e_devicemgr_wl_cb_deinit_generator,
   _e_devicemgr_wl_cb_generate_key,
   _e_devicemgr_wl_cb_generate_pointer,
   _e_devicemgr_wl_cb_generate_touch,
   _e_devicemgr_wl_cb_pointer_warp,
   _e_devicemgr_wl_cb_init_generator_with_name,
   _e_devicemgr_wl_cb_destroy,
   _e_devicemgr_wl_cb_generate_axis
};

static void
_e_devicemgr_wl_cb_unbind(struct wl_resource *resource)
{
   if(!e_comp_wl) return;

   e_devicemgr->wl_data->resources = eina_list_remove(e_devicemgr->wl_data->resources, resource);
}

static void
_e_devicemgr_wl_cb_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   struct wl_resource *res, *seat_res, *device_res;
   Eina_List *l;
   uint32_t serial;
   E_Devicemgr_Input_Device *dev;
   struct wl_array axes;
   E_Devicemgr_Input_Device_User_Data *device_user_data;

   if (!(res = wl_resource_create(client, &tizen_input_device_manager_interface, version, id)))
     {
        DMERR("Could not create tizen_input_device_manager_interface resource: %m");
        wl_client_post_no_memory(client);
        return;
     }

   e_devicemgr->wl_data->resources = eina_list_append(e_devicemgr->wl_data->resources, res);

   wl_resource_set_implementation(res, &_e_devicemgr_wl_implementation, NULL,
                                  _e_devicemgr_wl_cb_unbind);

   EINA_LIST_FOREACH(e_comp_wl->seat.resources, l, seat_res)
     {
        if (wl_resource_get_client(seat_res) != client) continue;

        wl_array_init(&axes);
        serial = wl_display_next_serial(e_comp_wl->wl.disp);

        EINA_LIST_FOREACH(e_devicemgr->device_list, l, dev)
          {
             device_res = wl_resource_create(client, &tizen_input_device_interface, 1, 0);
             if (!device_res)
               {
                  DMERR("Could not create tizen_input_device resource: %m");
                  return;
               }
             device_user_data = E_NEW(E_Devicemgr_Input_Device_User_Data, 1);
             if (!device_user_data)
               {
                  DMERR("Failed to allocate memory for input device user data\n");
                  return;
               }
             device_user_data->dev = dev;
             device_user_data->dev_mgr_res = res;
             device_user_data->seat_res = seat_res;

             dev->resources = eina_list_append(dev->resources, device_res);

             wl_resource_set_implementation(device_res, &_e_devicemgr_wl_device_interface, device_user_data,
                                            _e_devicemgr_wl_device_cb_unbind);

             tizen_input_device_manager_send_device_add(res, serial, dev->identifier, device_res, seat_res);
             tizen_input_device_send_device_info(device_res, dev->name, dev->clas, dev->subclas, &axes);
          }
     }
}

Eina_Bool
e_devicemgr_wl_init(void)
{
   if (!e_comp_wl) return EINA_FALSE;
   if (!e_comp_wl->wl.disp) return EINA_FALSE;

   if (e_devicemgr->wl_data) return EINA_TRUE;

   e_devicemgr->wl_data = E_NEW(E_Devicemgr_Wl_Data, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_devicemgr->wl_data, EINA_FALSE);

   /* try to add tizen_input_device_manager to wayland globals */
   e_devicemgr->wl_data->global = wl_global_create(e_comp_wl->wl.disp,
                                                   &tizen_input_device_manager_interface, 3,
                                                   NULL, _e_devicemgr_wl_cb_bind);
   if (!e_devicemgr->wl_data->global)
     {
        DMERR("Could not add tizen_input_device_manager to wayland globals");
        return EINA_FALSE;
     }
   e_devicemgr->wl_data->resources = NULL;

   /* initialization of cynara for checking privilege */
#ifdef HAVE_CYNARA
   int ret;

   ret = cynara_initialize(&e_devicemgr->wl_data->p_cynara, NULL);
   if (EINA_UNLIKELY(CYNARA_API_SUCCESS != ret))
     {
        _e_devicemgr_util_cynara_log("cynara_initialize", ret);
        e_devicemgr->wl_data->p_cynara = NULL;
     }
   e_devicemgr->wl_data->cynara_initialized = EINA_TRUE;
#endif

   return EINA_TRUE;
}

void
e_devicemgr_wl_shutdown(void)
{
   if (!e_devicemgr->wl_data) return;
   /* destroy the global seat resource */
   if (e_devicemgr->wl_data->global)
     wl_global_destroy(e_devicemgr->wl_data->global);
   e_devicemgr->wl_data->global = NULL;

   /* deinitialization of cynara if it has been initialized */
#ifdef HAVE_CYNARA
   if (e_devicemgr->wl_data->p_cynara) cynara_finish(e_devicemgr->wl_data->p_cynara);
   e_devicemgr->wl_data->cynara_initialized = EINA_FALSE;
#endif

   E_FREE(e_devicemgr->wl_data);
}

