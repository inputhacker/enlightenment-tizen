#include "e.h"

EINTERN Eina_Bool
e_input_libinput_init()
{
}

EINTERN void
e_input_libinput_shutdown()\
{
}

EINTERN Eina_Bool
e_input_libinput_devices_create()
{
   struct libinput_device *device;
   int devices_num;
   char *env;
   Eina_Stringshare *path;

   TRACE_INPUT_BEGIN(__FUNCTION__);

   if ((env = getenv("PATH_DEVICES_NUM")))
     devices_num = atoi(env);
   if (!env || devices_num == 0)
     {
        TRACE_INPUT_END();
        return EINA_TRUE;
     }

   //INF("PATH_DEVICES_NUM : %d", devices_num);

   /* try to create libinput context */
   input->libinput =
     libinput_path_create_context(&_input_interface, input);
   if (!input->libinput)
     {
        ERR("Could not create libinput path context: %m");
        goto err;
     }

   /* set libinput log priority */
   libinput_log_set_priority(input->libinput, LIBINPUT_LOG_PRIORITY_INFO);

   for (int i = 0; i < devices_num; i++)
     {
        char buf[1024] = "PATH_DEVICE_";
        eina_convert_itoa(i + 1, buf + 12);
        env = getenv(buf);
        if (env)
          {
             path = eina_stringshare_add(env);
             device = libinput_path_add_device(input->libinput, path);
             if (!device)
               ERR("Failed to initialized device %s", path);
             else
               INF("libinput_path created input device %s", path);
          }
     }

   /* process pending events */
   _input_events_process(input);

   /* enable this input */
   if (!ecore_drm_inputs_enable(input))
     {
        ERR("Failed to enable input");
        goto err;
     }

   /* append this input */
   dev->inputs = eina_list_append(dev->inputs, input);

   TRACE_EFL_END();
   TRACE_INPUT_END();
   return EINA_TRUE;

err:
   if (input->libinput) libinput_unref(input->libinput);
   free(input);
   TRACE_EFL_END();
   TRACE_INPUT_END();
   return EINA_FALSE;
}


