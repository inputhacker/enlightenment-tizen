#ifndef _E_COMP_WL_SUBSURFACE_H_
#define _E_COMP_WL_SUBSURFACE_H_

#include "e.h"

#ifndef WL_HIDE_DEPRECATED
# define WL_HIDE_DEPRECATED
#endif
#include <wayland-server.h>

E_API Eina_Bool       e_comp_wl_subsurface_create(E_Client *ec, E_Client *epc, uint32_t id, struct wl_resource *surface_resource);
E_API void            e_comp_wl_subsurface_stack_update(E_Client *ec);

EINTERN Eina_Bool     e_comp_wl_subsurfaces_init(E_Comp_Wl_Data *wl_comp);
EINTERN void          e_comp_wl_subsurfaces_shutdown(void);
EINTERN void          e_comp_wl_subsurface_parent_commit(E_Client *ec, Eina_Bool parent_synchronized);
EINTERN Eina_Bool     e_comp_wl_subsurface_order_commit(E_Client *ec);
EINTERN Eina_Bool     e_comp_wl_subsurface_commit(E_Client *ec);
EINTERN Eina_Bool     e_comp_wl_subsurface_can_show(E_Client *ec);
EINTERN void          e_comp_wl_subsurface_show(E_Client *ec);
EINTERN void          e_comp_wl_subsurface_hide(E_Client *ec);
EINTERN void          e_comp_wl_subsurface_restack_bg_rectangle(E_Client *ec);
EINTERN void          e_comp_wl_subsurface_restack(E_Client *ec);
EINTERN Eina_Bool     e_comp_wl_video_subsurface_has(E_Client *ec);
EINTERN Eina_Bool     e_comp_wl_normal_subsurface_has(E_Client *ec);
EINTERN void          e_comp_wl_subsurface_check_below_bg_rectangle(E_Client *ec);

#endif
