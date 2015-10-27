#ifdef E_TYPEDEFS
#else
# ifndef E_COMP_WL_DATA_H
#  define E_COMP_WL_DATA_H

#  undef NEED_X
#  include "e_comp_wl.h"

#  define CLIPBOARD_CHUNK 1024

typedef struct _E_Comp_Wl_Data_Source E_Comp_Wl_Data_Source;
typedef struct _E_Comp_Wl_Data_Offer E_Comp_Wl_Data_Offer;
typedef struct _E_Comp_Wl_Clipboard_Source E_Comp_Wl_Clipboard_Source;
typedef struct _E_Comp_Wl_Clipboard_Offer E_Comp_Wl_Clipboard_Offer;

struct _E_Comp_Wl_Data_Source
{
   struct wl_resource *resource; //resource of wl_data_source

   Eina_List *mime_types; //mime_type list to offer from source
   struct wl_signal destroy_signal; //signal to emit when wl_data_source resource is destroyed

   void (*target) (E_Comp_Wl_Data_Source *source, uint32_t serial, const char* mime_type);
   void (*send) (E_Comp_Wl_Data_Source *source, const char* mime_type, int32_t fd);
   void (*cancelled) (E_Comp_Wl_Data_Source *source);
};

struct _E_Comp_Wl_Data_Offer
{
   struct wl_resource *resource; //resource of wl_data_offer

   E_Comp_Wl_Data_Source *source; //indicates source client data
   struct wl_listener source_destroy_listener; //listener for destroy of source
};

struct _E_Comp_Wl_Clipboard_Source
{
   E_Comp_Wl_Data_Source data_source;
   Ecore_Fd_Handler *fd_handler;
   uint32_t serial;

   struct wl_array contents; //for extendable buffer
   int ref;
   int fd;
};

struct _E_Comp_Wl_Clipboard_Offer
{
   E_Comp_Wl_Clipboard_Source *source;
   Ecore_Fd_Handler *fd_handler;
   size_t offset;
};

EINTERN void e_comp_wl_data_device_keyboard_focus_set(E_Comp_Data *cdata);
EINTERN Eina_Bool e_comp_wl_data_manager_init(E_Comp_Data *cdata);
EINTERN void e_comp_wl_data_manager_shutdown(E_Comp_Data *cdata);
EINTERN Eina_Bool e_comp_wl_data_dnd_focus(E_Client *ec);
EINTERN void e_comp_wl_data_dnd_motion(E_Client *ec, unsigned int time, int x, int y);
EINTERN void e_comp_wl_data_dnd_drop(E_Client *ec, unsigned int time, uint32_t btn, uint32_t state);

# endif
#endif
