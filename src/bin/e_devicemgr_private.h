#include "e.h"
#include "e_devicemgr.h"
#ifdef HAVE_CYNARA
#include <cynara-session.h>
#include <cynara-client.h>
#include <cynara-creds-socket.h>
#include <sys/smack.h>
#endif
#include <tizen-extension-server-protocol.h>
#include <linux/uinput.h>

#ifdef TRACE_INPUT_BEGIN
#undef TRACE_INPUT_BEGIN
#endif
#ifdef TRACE_INPUT_END
#undef TRACE_INPUT_END
#endif

#ifdef ENABLE_TTRACE
#include <ttrace.h>

#define TRACE_INPUT_BEGIN(NAME) traceBegin(TTRACE_TAG_INPUT, "INPUT:DEVMGR:"#NAME)
#define TRACE_INPUT_END() traceEnd(TTRACE_TAG_INPUT)
#else
#define TRACE_INPUT_BEGIN(NAME)
#define TRACE_INPUT_END()
#endif

#define DMERR(msg, ARG...) ERR("[%s:%d] "msg, __FUNCTION__, __LINE__, ##ARG)
#define DMWRN(msg, ARG...) WRN("[%s:%d] "msg, __FUNCTION__, __LINE__, ##ARG)
#define DMINF(msg, ARG...) INF("[%s:%d] "msg, __FUNCTION__, __LINE__, ##ARG)
#define DMDBG(msg, ARG...) DBG("[%s:%d] "msg, __FUNCTION__, __LINE__, ##ARG)

#ifdef HAVE_CYNARA
#include <cynara-session.h>
#include <cynara-client.h>
#include <cynara-creds-socket.h>
#endif

#define INPUT_GENERATOR_DEVICE "Input Generator"
#define DETENT_DEVICE_NAME "tizen_detent"
#define INPUTGEN_MAX_TOUCH 10
#define INPUTGEN_MAX_BTN 16

typedef struct _E_Devicemgr_Input_Device_User_Data E_Devicemgr_Input_Device_User_Data;
typedef struct _E_Devicemgr_Coords E_Devicemgr_Coords;
typedef struct _E_Devicemgr_Inputgen_Client_Data E_Devicemgr_Inputgen_Client_Data;
typedef struct _E_Devicemgr_Inputgen_Client_Global_Data E_Devicemgr_Inputgen_Client_Global_Data;
typedef struct _E_Devicemgr_Inputgen_Device_Data E_Devicemgr_Inputgen_Device_Data;
typedef struct _E_Devicemgr_Inputgen_Resource_Data E_Devicemgr_Inputgen_Resource_Data;

struct _E_Devicemgr_Input_Device_User_Data
{
   E_Devicemgr_Input_Device *dev;
   struct wl_resource *dev_mgr_res;
   struct wl_resource *seat_res;
};

struct _E_Devicemgr_Coords
{
   int x, y;
};

struct _E_Devicemgr_Inputgen_Client_Data
{
   struct wl_client *client;
   int ref;
};

struct _E_Devicemgr_Inputgen_Client_Global_Data
{
   struct wl_client *client;
   unsigned int clas;
};

struct _E_Devicemgr_Inputgen_Device_Data
{
   int uinp_fd;
   char *identifier;
   char name[UINPUT_MAX_NAME_SIZE];
   Ecore_Device_Class clas;
   struct
     {
        unsigned int pressed;
        E_Devicemgr_Coords coords[INPUTGEN_MAX_TOUCH];
     } touch;
   struct
     {
        unsigned int pressed;
        E_Devicemgr_Coords coords;
     } mouse;
   struct
     {
        Eina_List *pressed;
     } key;

   Eina_List *clients;
};

struct _E_Devicemgr_Inputgen_Resource_Data
{
   struct wl_resource *resource;
   char name[UINPUT_MAX_NAME_SIZE];
};

struct _E_Devicemgr_Conf_Edd
{
   struct
   {
      Eina_Bool button_remap_enable;          // enable feature of remap mouse right button to back key
      Eina_Bool virtual_key_device_enable;    // create a virtual keyboard device
      Eina_Bool virtual_mouse_device_enable;  // create a virtual mouse device
      int back_keycode;                       // keycode of back key
   } input;
};

struct _E_Devicemgr_Config_Data
{
   E_Config_DD *conf_edd;
   E_Devicemgr_Conf_Edd *conf;
};

struct _E_Devicemgr_Wl_Data
{
   struct wl_global *global;
   Eina_List *resources;

#ifdef HAVE_CYNARA
   cynara *p_cynara;
   Eina_Bool cynara_initialized;
#endif
};

void e_devicemgr_conf_init(E_Devicemgr_Config_Data *dconfig);
void e_devicemgr_conf_fini(E_Devicemgr_Config_Data *dconfig);

Eina_Bool e_devicemgr_wl_init(void);
void e_devicemgr_wl_shutdown(void);
void e_devicemgr_wl_device_add(E_Devicemgr_Input_Device *dev);
void e_devicemgr_wl_device_del(E_Devicemgr_Input_Device *dev);
void e_devicemgr_wl_device_update(E_Devicemgr_Input_Device *dev);

Eina_Bool e_devicemgr_block_check_keyboard(Ecore_Event_Key *ev, Eina_Bool pressed);
Eina_Bool e_devicemgr_block_check_move(Ecore_Event_Mouse_Move *ev);
Eina_Bool e_devicemgr_block_check_button(Ecore_Event_Mouse_Button *ev, Eina_Bool pressed);
void e_devicemgr_wl_block_send_expired(struct wl_resource *resource);
int e_devicemgr_block_add(struct wl_client *client, struct wl_resource *resource, uint32_t clas, uint32_t duration);
int e_devicemgr_block_remove(struct wl_client *client);

int e_devicemgr_inputgen_add(struct wl_client *client, struct wl_resource *resource, uint32_t clas, const char *name);
void e_devicemgr_inputgen_remove(struct wl_client *client, struct wl_resource *resource, uint32_t clas);
int e_devicemgr_inputgen_generate_key(struct wl_client *client, struct wl_resource *resource, const char *keyname, Eina_Bool pressed);
int e_devicemgr_inputgen_generate_pointer(struct wl_client *client, struct wl_resource *resource, uint32_t type, uint32_t x, uint32_t y, uint32_t button);
int e_devicemgr_inputgen_generate_touch(struct wl_client *client, struct wl_resource *resource, uint32_t type, uint32_t x, uint32_t y, uint32_t finger);
void e_devicemgr_inputgen_get_device_info(E_Devicemgr_Input_Device *dev);

int e_devicemgr_create_virtual_device(Ecore_Device_Class clas, const char *name);
void e_devicemgr_destroy_virtual_device(int uinp_fd);

Eina_Bool e_devicemgr_strcmp(const char *dst, const char *src);
int e_devicemgr_keycode_from_string(const char *keyname);
int e_devicemgr_input_pointer_warp(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface, wl_fixed_t x, wl_fixed_t y);
void e_devicemgr_wl_detent_send_event(int detent);
Eina_Bool e_devicemgr_input_init(void);
void e_devicemgr_input_shutdown(void);

