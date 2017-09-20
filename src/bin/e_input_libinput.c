#include "e.h"
#include "e_input_private.h"

static Eina_Hash *_fd_hash = NULL;

EINTERN Eina_Bool
e_input_libinput_init()
{
   return EINA_TRUE;
}

EINTERN void
e_input_libinput_shutdown()\
{
}

static int
_device_open_no_pending(const char *device, int flags)
{
   int fd = -1;
   struct stat s;

   fd = open(device, flags | O_CLOEXEC);

   if (fd < 0) return fd;
   if (fstat(fd, &s) == -1)
     {
        close(fd);
        return -1;
     }

   return fd;
}

static void
_device_close(const char *device, int fd)
{
   if (fd >= 0)
     close(fd);
}

/* local functions */
static int
_cb_open_restricted(const char *path, int flags, void *data)
{
   E_Input_Backend *input;
   int fd = -1;

   if (!(input = data)) return -1;

   /* try to open the device */
   fd = _device_open_no_pending(path, flags);
   if (fd < 0) ERR("Could not open device");
   if (_fd_hash)
     eina_hash_add(_fd_hash, path, (void *)(intptr_t)fd);

   return fd;
}

static void
_cb_close_restricted(int fd, void *data)
{
   E_Input_Backend *input;
   E_Input_Seat *seat;
   E_Input_Evdev *edev;
   Eina_List *l, *ll;

   if (!(input = data)) return;

   EINA_LIST_FOREACH(input->dev->seats, l, seat)
     {
        EINA_LIST_FOREACH(seat->devices, ll, edev)
          {
             if (edev->fd == fd)
               {
                  _device_close(edev->path, fd);

                  /* re-initialize fd after closing */
                  edev->fd = -1;
                  return;
               }
          }
     }

   if (fd >= 0) close(fd);
}

const struct libinput_interface _libinput_interface =
{
   _cb_open_restricted,
   _cb_close_restricted,
};

EINTERN Eina_Bool
e_input_libinput_devices_create(E_Input_Device *dev)
{
   E_Input_Backend *input;
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

   if (!(input = calloc(1, sizeof(E_Input_Backend))))
     {
        TRACE_INPUT_END();
        return EINA_FALSE;
     }

   /* set reference for parent device */
   input->dev = dev;

   /* try to create libinput context */
   input->libinput =
     libinput_path_create_context(&_libinput_interface, input);
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
   if (!e_input_inputs_enable(input))
     {
        ERR("Failed to enable input");
        goto err;
     }

   /* append this input */
   dev->inputs = eina_list_append(dev->inputs, input);

   TRACE_INPUT_END();
   return EINA_TRUE;

err:
   if (input->libinput) libinput_unref(input->libinput);
   free(input);
   TRACE_INPUT_END();
   return EINA_FALSE;
}


