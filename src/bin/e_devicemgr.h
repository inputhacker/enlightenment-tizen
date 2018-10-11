#ifdef E_TYPEDEFS

typedef enum _E_Devicemgr_Intercept_Hook_Point
{
   E_DEVICEMGR_INTERCEPT_HOOK_DETENT,
   E_DEVICEMGR_INTERCEPT_HOOK_LAST
} E_Devicemgr_Intercept_Hook_Point;

typedef Eina_Bool (*E_Devicemgr_Intercept_Hook_Cb) (void *data, int point, void *event);
typedef struct _E_Devicemgr_Intercept_Hook E_Devicemgr_Intercept_Hook;

typedef struct _E_Devicemgr_Conf_Edd E_Devicemgr_Conf_Edd;
typedef struct _E_Devicemgr_Config_Data E_Devicemgr_Config_Data;
typedef struct _E_Devicemgr_Wl_Data E_Devicemgr_Wl_Data;

typedef struct _E_Devicemgr_Input_Device
{
   Eina_List *resources;
   const char *name;
   const char *identifier;
   Ecore_Device_Class clas;
   Ecore_Device_Subclass subclas;
} E_Devicemgr_Input_Device;

typedef struct _E_Devicemgr_Input_Device_Multi
{
   double radius_x;
   double radius_y;
   double pressure;
   double angle;
} E_Devicemgr_Input_Device_Multi;

typedef struct _E_Devicemgr E_Devicemgr;

#else
#ifndef E_DEVICEMGR_H
#define E_DEVICEMGR_H

extern E_API E_Devicemgr *e_devicemgr;

struct _E_Devicemgr
{
   E_Devicemgr_Config_Data *dconfig;
   E_Devicemgr_Wl_Data *wl_data;

   Ecore_Event_Filter *ev_filter;
   Eina_List *handlers;

   Eina_List *device_list;
   E_Devicemgr_Input_Device *last_device_ptr;
   E_Devicemgr_Input_Device *last_device_touch;
   E_Devicemgr_Input_Device *last_device_kbd;
   E_Devicemgr_Input_Device_Multi multi[E_COMP_WL_TOUCH_MAX];

   struct
     {
        unsigned int devtype;
        struct wl_client *client;
        Ecore_Timer *duration_timer;
     } block;

   Eina_List *pressed_keys;
   unsigned int pressed_button;
   unsigned int pressed_finger;

   int virtual_key_device_fd;
   int virtual_mouse_device_fd;

   struct
   {
      Eina_List *kbd_list;
      Eina_List *ptr_list;
      Eina_List *touch_list;

      Eina_List *resource_list;
   } inputgen;

   struct
   {
      char *identifier;
      int wheel_click_angle;
   } detent;

   Eina_List *watched_clients;
};

struct _E_Devicemgr_Intercept_Hook
{
   EINA_INLIST;
   E_Devicemgr_Intercept_Hook_Point hookpoint;
   E_Devicemgr_Intercept_Hook_Cb func;
   void *data;
   unsigned char delete_me : 1;
};

E_API E_Devicemgr_Intercept_Hook *e_devicemgr_intercept_hook_add(E_Devicemgr_Intercept_Hook_Point hookpoint, E_Devicemgr_Intercept_Hook_Cb func, const void *data);
E_API void e_devicemgr_intercept_hook_del(E_Devicemgr_Intercept_Hook *ch);
E_API Eina_Bool e_devicemgr_intercept_hook_call(E_Devicemgr_Intercept_Hook_Point hookpoint, void *event);
E_API Eina_Bool e_devicemgr_is_blocking_event(Ecore_Device_Class clas);

E_API int e_devicemgr_init(void);
E_API int e_devicemgr_shutdown(void);

Eina_Bool e_devicemgr_detent_is_detent(const char *name);


#endif
#endif

