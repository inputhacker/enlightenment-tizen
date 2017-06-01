#ifdef E_TYPEDEFS


#else
#ifndef E_COMP_SCREEN_H
#define E_COMP_SCREEN_H

#include <tdm.h>

typedef struct _E_Comp_Screen   E_Comp_Screen;
typedef struct _E_Screen        E_Screen;

struct _E_Comp_Screen
{
   Eina_List     *outputs; // available screens
   int            w, h; // virtual resolution (calculated)
   unsigned char  ignore_hotplug_events;
   unsigned char  ignore_acpi_events;
   Eina_List     *e_screens;

   int            num_outputs;
   tdm_display   *tdisplay;

   /* for sw compositing */
   const Eina_List *devices;

   /* for screen_rotation */
   int rotation;
};


struct _E_Screen
{
   int screen, escreen;
   int x, y, w, h;
   char *id; // this is the same id we get from _E_Output so look it up there
};

extern E_API int E_EVENT_SCREEN_CHANGE;

E_API   Eina_Bool         e_comp_screen_init(void);
E_API   void              e_comp_screen_shutdown(void);
EINTERN Eina_Bool         e_comp_screen_setup(E_Comp_Screen *e_comp_screen);
EINTERN void              e_comp_screen_hwc_info_debug(void);

EINTERN void              e_comp_screen_e_screens_setup(E_Comp_Screen *e_comp_screen, int rw, int rh);
EINTERN const Eina_List * e_comp_screen_e_screens_get(E_Comp_Screen *e_comp_screen);
E_API   Eina_Bool         e_comp_screen_rotation_setting_set(E_Comp_Screen *e_comp_screen, int rotation);

EINTERN void              e_comp_screen_rotation_ignore_output_transform_send(E_Client *ec, Eina_Bool ignore);
EINTERN E_Output        * e_comp_screen_primary_output_get(E_Comp_Screen *e_comp_screen);


#endif /*E_COMP_SCREEN_H*/

#endif
