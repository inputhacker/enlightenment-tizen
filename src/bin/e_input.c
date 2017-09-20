#include "e.h"
//#include <Eeze.h>
#include "e_input_private.h"

int _e_input_init_count;
int _e_input_log_dom = -1;

//TODO : event declaration
E_API int E_INPUT_EVENT_INPUT_DEVICE_ADD = -1;
E_API int E_INPUT_EVENT_INPUT_DEVICE_DEL = -1;
E_API int E_INPUT_EVENT_SEAT_ADD = -1;
E_API int E_EVENT_INPUT_ENABLED = -1;
E_API int E_EVENT_INPUT_DISABLED = -1;

E_API E_Input *e_input = NULL;

static Eina_Bool
_e_input_cb_key_down(void *data, int type EINA_UNUSED, void *event)
{
//   Ecore_Event_Key *ev;
//   int code = 0;

//   ev = event;
//   code = (ev->keycode - 8);

   //TODO : do some global actions for key combinations

   return ECORE_CALLBACK_RENEW;
}
#if 0
static void
_e_input_event_generic_free(void *data EINA_UNUSED, void *ev)
{
   struct _E_Event_Input_Generic *e;

   e = ev;
   e_object_unref(E_OBJECT(e->input));
   free(e);
}
#endif

EINTERN int
e_input_init(Ecore_Evas *ee)
{
   E_Input_Device *dev;

   if (++_e_input_init_count != 1) return _e_input_init_count;

   if (!eina_init()) goto eina_err;
   if (!ecore_init()) goto ecore_err;
   if (!ecore_event_init()) goto ecore_event_err;
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
   ecore_event_handler_add(ECORE_EVENT_KEY_DOWN, _e_input_cb_key_down, NULL);

   if (!e_input)
     {
        e_input = (E_Input *)calloc(1, sizeof(E_Input));
     }

   if (!e_input)
     {
        EINA_LOG_ERR("Failed to alloc memory for e_input\n");
        goto log_err;
     }

   _e_input_inputs_init();

   dev = (E_Input_Device *)calloc(1, sizeof(E_Input_Device));

   if (!dev)
     {
        EINA_LOG_ERR("Failed to alloc memory for E_Input_Device\n");
        goto input_err;
     }

   dev->seat = eina_stringshare_add("seat0");
   dev->xkb_ctx = _e_input_device_cached_context_get(0);
   if (!dev->xkb_ctx)
     {
        EINA_LOG_ERR("Failed to get xkb cached context\n");
        goto xkb_ctx_err;
     }

   if (!e_input_inputs_create(dev))
     {
        EINA_LOG_ERR("Failed to create device\n");
        goto input_create_err;
     }

   e_input->dev = dev;

   ecore_evas_input_event_register_with_multi(ee);
   ecore_evas_input_event_register_with_multi2(ee);

   e_input->window = ecore_evas_window_get(ee);
   e_input_device_window_set(dev, e_input->window);

   return _e_input_init_count;

input_create_err:
   eina_stringshare_del(dev->seat);
   free(dev);

xkb_ctx_err:
   xkb_context_unref(dev->xkb_ctx);

input_err:
   _e_input_inputs_shutdown();

log_err:
   eeze_shutdown();

eeze_err:
   ecore_event_shutdown();

ecore_event_err:
   ecore_shutdown();

ecore_err:
   eina_shutdown();

eina_err:
   return --_e_input_init_count;
}

#if 0
E_API E_Input *
e_input_new(void)
{
   E_Input *einput;

   //einput = E_OBJECT_ALLOC(E_Input, E_INPUT_TYPE, _e_input_free);
   einput = calloc(1, sizeof(E_Input));

   if (!einput) return NULL;

   return einput;
}
#endif

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

   eeze_shutdown();
   ecore_event_shutdown();
   ecore_shutdown();
   eina_shutdown();

   return _e_input_init_count;
}
