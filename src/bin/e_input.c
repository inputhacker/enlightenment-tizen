#include "e.h"
#include "e_input_private.h"
#include "e_comp_wl_input.h"
#include <Ecore_Input_Evas.h>

int _e_input_init_count;
int _e_input_log_dom = -1;

E_API int E_INPUT_EVENT_INPUT_DEVICE_ADD = -1;
E_API int E_INPUT_EVENT_INPUT_DEVICE_DEL = -1;
E_API int E_INPUT_EVENT_SEAT_ADD = -1;
E_API int E_EVENT_INPUT_ENABLED = -1;
E_API int E_EVENT_INPUT_DISABLED = -1;

E_API E_Input *e_input = NULL;

EINTERN const char *
e_input_base_dir_get(void)
{
   return e_input->input_base_dir;
}

EINTERN Eina_Bool
e_input_thread_enabled_get(void)
{
   return e_input->use_thread;
}

EINTERN int
e_input_init(Ecore_Evas *ee)
{
   char *env = NULL;
   unsigned int seat_caps = 0;

   E_Input_Device *dev;

   Eina_Bool use_udev_backend = EINA_FALSE;
   Eina_Bool use_path_backend = EINA_FALSE;
   Eina_Bool skip_udev_enumeration = EINA_FALSE;

   TRACE_INPUT_BEGIN(e_input_init);

   if (++_e_input_init_count != 1) return _e_input_init_count;
   if (!ecore_event_evas_init()) goto ecore_event_evas_err;
   if (!eeze_init()) goto eeze_err;

   _e_input_log_dom = eina_log_domain_register("e_input", EINA_COLOR_GREEN);
   if (!_e_input_log_dom)
     {
        EINA_LOG_ERR("Could not create logging domain for E_Input");
        goto log_err;
     }

   E_INPUT_EVENT_INPUT_DEVICE_ADD = ecore_event_type_new();
   E_INPUT_EVENT_INPUT_DEVICE_DEL = ecore_event_type_new();
   E_INPUT_EVENT_SEAT_ADD = ecore_event_type_new();
   E_EVENT_INPUT_ENABLED = ecore_event_type_new();
   E_EVENT_INPUT_DISABLED = ecore_event_type_new();

   ecore_event_add(E_EVENT_INPUT_ENABLED, NULL, NULL, NULL);

   ecore_evas_input_event_register_with_multi2(ee);

   if (!e_input)
     {
        e_input = (E_Input *)calloc(1, sizeof(E_Input));
     }

   if (!e_input)
     {
        EINA_LOG_ERR("Failed to alloc memory for e_input\n");
        goto log_err;
     }

   // TODO : make this variable configurable e.g. e.cfg
   e_input->input_base_dir = eina_stringshare_add("/dev/input");
   e_input->use_thread = EINA_FALSE;

   dev = e_input_device_open();

   if (!dev)
     {
        EINA_LOG_ERR("Failed to open device\n");
        goto log_err;
     }

   e_input->window = ecore_evas_window_get(ee);
   e_input_device_window_set(dev, e_input->window);

   env = e_util_env_get("E_INPUT_USE_THREAD_INIT");

   if (env)
     {
        e_input->use_thread = EINA_TRUE;
        E_FREE(env);
     }

   env = e_util_env_get("LIBINPUT_UDEV_SKIP_INITIAL_ENUMERATION");
   if (env)
     {
        skip_udev_enumeration = EINA_TRUE;
        E_FREE(env);
     }

   env = e_util_env_get("E_INPUT_USE_LIBINPUT_UDEV_BACKEND");
   if (env)
     {
        use_udev_backend = EINA_TRUE;
        E_FREE(env);
     }

   env = e_util_env_get("E_INPUT_USE_LIBINPUT_PATH_BACKEND");
   if (env)
     {
        use_path_backend = EINA_TRUE;
        E_FREE(env);
     }

   TRACE_INPUT_BEGIN(e_input_device_input_backend_create);

   if ((use_udev_backend) &&
       (!e_input_device_input_backend_create(dev, E_INPUT_LIBINPUT_BACKEND_UDEV)))
     {
        EINA_LOG_ERR("Failed to create e_input_device\n");
        TRACE_INPUT_END();
        goto device_create_err;
     }

   if ((use_path_backend) && (!use_udev_backend || skip_udev_enumeration) &&
       (!e_input_device_input_backend_create(dev, E_INPUT_LIBINPUT_BACKEND_PATH)))
     {
        EINA_LOG_ERR("Failed to create e_input_device\n");
        TRACE_INPUT_END();
        goto device_create_err;
     }

   TRACE_INPUT_END();

   if (use_udev_backend && skip_udev_enumeration && !use_path_backend)
     {
        /* Enable some of keyboard, touch devices temporarily */
        /* FIXME : get seat caps from e_input configuration or env */
        seat_caps = E_INPUT_SEAT_KEYBOARD | E_INPUT_SEAT_TOUCH;
        e_comp_wl_input_seat_caps_set(seat_caps);
     }

   e_input->dev = dev;

   TRACE_INPUT_END();

   return _e_input_init_count;

device_create_err:
   e_input_device_close(dev);

log_err:
   if (e_input && e_input->input_base_dir)
     {
        eina_stringshare_del(e_input->input_base_dir);
        e_input->input_base_dir = NULL;
     }

   eeze_shutdown();

eeze_err:
   ecore_event_evas_shutdown();

ecore_event_evas_err:

   TRACE_INPUT_END();

   return --_e_input_init_count;
}

EINTERN int
e_input_shutdown(void)
{
   if (_e_input_init_count < 1) return 0;
   if (--_e_input_init_count != 0) return _e_input_init_count;

   ecore_event_add(E_EVENT_INPUT_DISABLED, NULL, NULL, NULL);

   E_INPUT_EVENT_INPUT_DEVICE_ADD = -1;
   E_INPUT_EVENT_INPUT_DEVICE_DEL = -1;
   E_INPUT_EVENT_SEAT_ADD = -1;
   E_EVENT_INPUT_ENABLED = -1;
   E_EVENT_INPUT_DISABLED = -1;

   if (e_input->input_base_dir)
     eina_stringshare_del(e_input->input_base_dir);
   e_input_device_close(e_input->dev);
   free(e_input);

   ecore_event_evas_shutdown();
   eeze_shutdown();
   ecore_event_shutdown();
   ecore_shutdown();
   eina_shutdown();

   return _e_input_init_count;
}
