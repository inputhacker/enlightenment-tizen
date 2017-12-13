#include "e.h"
#include "e_input_private.h"
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

EINTERN int
e_input_init(Ecore_Evas *ee)
{
   char *env = NULL;
   E_Input_Device *dev;
   E_Input_Libinput_Backend backend = E_INPUT_LIBINPUT_BACKEND_UDEV;

   TRACE_INPUT_BEGIN(e_input_init);

   if (++_e_input_init_count != 1) return _e_input_init_count;

   if (!eina_init()) goto eina_err;
   if (!ecore_init()) goto ecore_err;
   if (!ecore_event_init()) goto ecore_event_err;
   if (!eeze_init()) goto eeze_err;
   if (!ecore_event_evas_init()) goto ecore_event_evas_err;

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

   dev = e_input_device_open();

   if (!dev)
     {
        EINA_LOG_ERR("Failed to open device\n");
        goto log_err;
     }

   e_input->window = ecore_evas_window_get(ee);
   e_input_device_window_set(dev, e_input->window);

   TRACE_INPUT_BEGIN(e_input_device_input_backend_create);

   env = e_util_env_get("E_INPUT_USE_LIBINPUT_PATH_BACKEND");
   if (env)
     {
        backend = E_INPUT_LIBINPUT_BACKEND_PATH;
        E_FREE(env);
     }

   if (!e_input_device_input_backend_create(dev, backend))
     {
        EINA_LOG_ERR("Failed to create device\n");
        TRACE_INPUT_END();
        goto device_create_err;
     }
   TRACE_INPUT_END();

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
   ecore_event_evas_shutdown();

ecore_event_evas_err:
   eeze_shutdown();

eeze_err:
   ecore_event_shutdown();

ecore_event_err:
   ecore_shutdown();

ecore_err:
   eina_shutdown();

eina_err:

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
