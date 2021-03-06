#include "e.h"
#include <xdg-shell-server-protocol.h>
#include <tizen-extension-server-protocol.h>
#include "e_scaler.h"

#define XDG_SERVER_VERSION 5

typedef struct _E_Xdg_Shell E_Xdg_Shell;

struct _E_Xdg_Shell
{
   struct wl_client   *wc;
   struct wl_resource *res;      /* xdg_shell resource */
   Eina_List          *ping_ecs; /* list of all ec which are waiting for pong response */
};

static Eina_Hash *xdg_sh_hash = NULL;

static void
_e_shell_surface_parent_set(E_Client *ec, struct wl_resource *parent_resource)
{
   E_Client *pc;
   Ecore_Window pwin = 0;

   if (!parent_resource)
     {
        ec->icccm.fetch.transient_for = EINA_FALSE;
        ec->icccm.transient_for = 0;
        if (ec->parent)
          {
             ec->parent->transients =
                eina_list_remove(ec->parent->transients, ec);
             if (ec->parent->modal == ec) ec->parent->modal = NULL;
             ec->parent = NULL;
          }
        return;
     }

   pc = wl_resource_get_user_data(parent_resource);
   if (!pc)
     {
        ERR("Could not get parent resource client");
        return;
     }

   pwin = e_pixmap_window_get(pc->pixmap);

   e_pixmap_parent_window_set(ec->pixmap, pwin);

   /* If we already have a parent, remove it */
   if (ec->parent)
     {
        if (pc != ec->parent)
          {
             ec->parent->transients =
                eina_list_remove(ec->parent->transients, ec);
             if (ec->parent->modal == ec) ec->parent->modal = NULL;
             ec->parent = NULL;
          }
     }

   if ((pc != ec) &&
       (eina_list_data_find(pc->transients, ec) != ec))
     {
        pc->transients = eina_list_append(pc->transients, ec);
        ec->parent = pc;
     }

   ec->icccm.fetch.transient_for = EINA_TRUE;
   ec->icccm.transient_for = pwin;
}

static void
_e_shell_surface_mouse_down_helper(E_Client *ec, E_Binding_Event_Mouse_Button *ev, Eina_Bool move, int resize_edges)
{
   E_Pointer_Mode resize_mode = E_POINTER_RESIZE_NONE;

   if (move)
     {
        /* tell E to start moving the client */
        e_client_act_move_begin(ec, ev);

        /* we have to get a reference to the window_move action here, or else
         * when e_client stops the move we will never get notified */
        ec->cur_mouse_action = e_action_find("window_move");
        if (ec->cur_mouse_action)
          e_object_ref(E_OBJECT(ec->cur_mouse_action));
     }
   else
     {
        /* convert value to E's pointer mode from wayland resize edge */
        switch (resize_edges)
          {
           case  1: resize_mode = E_POINTER_RESIZE_T;  break;
           case  2: resize_mode = E_POINTER_RESIZE_B;  break;
           case  4: resize_mode = E_POINTER_RESIZE_L;  break;
           case  5: resize_mode = E_POINTER_RESIZE_TL; break;
           case  6: resize_mode = E_POINTER_RESIZE_BL; break;
           case  8: resize_mode = E_POINTER_RESIZE_R;  break;
           case  9: resize_mode = E_POINTER_RESIZE_TR; break;
           case 10: resize_mode = E_POINTER_RESIZE_BR; break;
           default: resize_mode = E_POINTER_RESIZE_NONE; break;
          }

        /* tell E to start resizing the client */
        e_client_act_resize_begin(ec, ev, resize_mode);

        /* we have to get a reference to the window_resize action here,
         * or else when e_client stops the resize we will never get notified */
        ec->cur_mouse_action = e_action_find("window_resize");
        if (ec->cur_mouse_action)
          e_object_ref(E_OBJECT(ec->cur_mouse_action));
     }

   e_focus_event_mouse_down(ec);
}

static void
_e_shell_surface_destroy(struct wl_resource *resource)
{
   E_Client *ec;

   /* get the client for this resource */
   if ((ec = wl_resource_get_user_data(resource)))
     {
        if ((e_object_unref(E_OBJECT(ec))) &&
            (!e_object_is_del(E_OBJECT(ec))))
          {
             if (ec->comp_data->mapped)
               {
                  if ((ec->comp_data->shell.surface) &&
                      (ec->comp_data->shell.unmap))
                    ec->comp_data->shell.unmap(ec->comp_data->shell.surface);
               }
             if (ec->parent)
               {
                  ec->parent->transients =
                     eina_list_remove(ec->parent->transients, ec);
                  if (ec->parent->modal == ec) ec->parent->modal = NULL;
                  ec->parent = NULL;
               }
             /* wl_resource_destroy(ec->comp_data->shell.surface); */
          }
        if (ec->comp_data)
          ec->comp_data->shell.surface = NULL;
     }

   e_policy_client_unmap(ec);
}

static void
_e_shell_surface_cb_destroy(struct wl_resource *resource)
{
   _e_shell_surface_destroy(resource);
}

static void
_e_shell_surface_cb_pong(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, uint32_t serial EINA_UNUSED)
{
   E_Client *ec;

   if ((ec = wl_resource_get_user_data(resource)))
     {
        ec->ping_ok = EINA_TRUE;
        ec->hung = EINA_FALSE;
     }
}

static void
_e_shell_surface_cb_move(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *seat_resource EINA_UNUSED, uint32_t serial EINA_UNUSED)
{
   E_Client *ec;
   E_Binding_Event_Mouse_Button ev;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   if ((ec->maximized) || (ec->fullscreen)) return;

   switch (e_comp_wl->ptr.button)
     {
      case BTN_LEFT:   ev.button = 1; break;
      case BTN_MIDDLE: ev.button = 2; break;
      case BTN_RIGHT:  ev.button = 3; break;
      default: ev.button = e_comp_wl->ptr.button; break;
     }

   e_comp_object_frame_xy_unadjust(ec->frame,
                                   wl_fixed_to_int(e_comp_wl->ptr.x),
                                   wl_fixed_to_int(e_comp_wl->ptr.y),
                                   &ev.canvas.x, &ev.canvas.y);

   _e_shell_surface_mouse_down_helper(ec, &ev, EINA_TRUE, 0);
}

static void
_e_shell_surface_cb_resize(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *seat_resource EINA_UNUSED, uint32_t serial EINA_UNUSED, uint32_t edges)
{
   E_Client *ec;
   E_Binding_Event_Mouse_Button ev;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   if ((edges == 0) || (edges > 15) ||
       ((edges & 3) == 3) || ((edges & 12) == 12)) return;

   if ((ec->maximized) || (ec->fullscreen)) return;

   e_comp_wl->resize.resource = resource;
   e_comp_wl->resize.edges = edges;
   e_comp_wl->ptr.grab_x = e_comp_wl->ptr.x - wl_fixed_from_int(ec->client.x);
   e_comp_wl->ptr.grab_y = e_comp_wl->ptr.y - wl_fixed_from_int(ec->client.y);

   switch (e_comp_wl->ptr.button)
     {
      case BTN_LEFT:   ev.button = 1; break;
      case BTN_MIDDLE: ev.button = 2; break;
      case BTN_RIGHT:  ev.button = 3; break;
      default: ev.button = e_comp_wl->ptr.button; break;
     }

   e_comp_object_frame_xy_unadjust(ec->frame,
                                   wl_fixed_to_int(e_comp_wl->ptr.x),
                                   wl_fixed_to_int(e_comp_wl->ptr.y),
                                   &ev.canvas.x, &ev.canvas.y);

   _e_shell_surface_mouse_down_helper(ec, &ev, EINA_FALSE, edges);
}

static void
_e_shell_surface_cb_toplevel_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   E_Client *ec;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   /* set toplevel client properties */
   ec->icccm.accepts_focus = 1;
   if (!ec->internal)
     ec->borderless = !ec->internal;

   ec->lock_border = EINA_TRUE;
   if (!ec->internal)
     ec->border.changed = ec->changes.border = !ec->borderless;
   ec->netwm.type = E_WINDOW_TYPE_NORMAL;
   ec->comp_data->set_win_type = EINA_TRUE;
   if ((!ec->lock_user_maximize) && (ec->maximized))
     e_client_unmaximize(ec, E_MAXIMIZE_BOTH);
   if ((!ec->lock_user_fullscreen) && (ec->fullscreen))
     e_client_unfullscreen(ec);
   EC_CHANGED(ec);
}

static void
_e_shell_surface_cb_transient_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *parent_resource, int32_t x EINA_UNUSED, int32_t y EINA_UNUSED, uint32_t flags EINA_UNUSED)
{
   E_Client *ec;

   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   /* set this client as a transient for parent */
   _e_shell_surface_parent_set(ec, parent_resource);

   EC_CHANGED(ec);
}

static void
_e_shell_surface_cb_fullscreen_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, uint32_t method EINA_UNUSED, uint32_t framerate EINA_UNUSED, struct wl_resource *output_resource EINA_UNUSED)
{
   E_Client *ec;

   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   if (!ec->lock_user_fullscreen)
     e_client_fullscreen(ec, e_config->fullscreen_policy);
}

static void
_e_shell_surface_cb_popup_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *seat_resource EINA_UNUSED, uint32_t serial EINA_UNUSED, struct wl_resource *parent_resource, int32_t x, int32_t y, uint32_t flags EINA_UNUSED)
{
   E_Client *ec;

   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   if (ec->comp_data)
     {
        ec->comp_data->popup.x = x;
        ec->comp_data->popup.y = y;
     }

   if (!ec->internal)
     ec->borderless = !ec->internal_elm_win;
   ec->lock_border = EINA_TRUE;
   if (!ec->internal)
     ec->border.changed = ec->changes.border = !ec->borderless;
   ec->changes.icon = !!ec->icccm.class;
   ec->netwm.type = E_WINDOW_TYPE_POPUP_MENU;
   if (ec->comp_data)
     ec->comp_data->set_win_type = EINA_TRUE;
   ec->layer = E_LAYER_CLIENT_POPUP;

   /* set this client as a transient for parent */
   _e_shell_surface_parent_set(ec, parent_resource);

   EC_CHANGED(ec);
}

static void
_e_shell_surface_cb_maximized_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *output_resource EINA_UNUSED)
{
   E_Client *ec;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   /* tell E to maximize this client */
   if (!ec->lock_user_maximize)
     {
        unsigned int edges = 0;

        e_client_maximize(ec, ((e_config->maximize_policy & E_MAXIMIZE_TYPE) |
                               E_MAXIMIZE_BOTH));

        edges = (WL_SHELL_SURFACE_RESIZE_TOP | WL_SHELL_SURFACE_RESIZE_LEFT);
        wl_shell_surface_send_configure(resource, edges, ec->w, ec->h);
     }
}

static void
_e_shell_surface_cb_title_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, const char *title)
{
   E_Client *ec;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   /* set title */
   eina_stringshare_replace(&ec->icccm.title, title);
   if (ec->frame) e_comp_object_frame_title_set(ec->frame, title);
}

static void
_e_shell_surface_cb_class_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, const char *clas)
{
   E_Client *ec;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   /* use the wl_client to get the pid * and set it in the netwm props */
   wl_client_get_credentials(client, &ec->netwm.pid, NULL, NULL);

   /* set class */
   eina_stringshare_replace(&ec->icccm.class, clas);
   ec->changes.icon = !!ec->icccm.class;
   EC_CHANGED(ec);
}

static const struct wl_shell_surface_interface _e_shell_surface_interface =
{
   _e_shell_surface_cb_pong,
   _e_shell_surface_cb_move,
   _e_shell_surface_cb_resize,
   _e_shell_surface_cb_toplevel_set,
   _e_shell_surface_cb_transient_set,
   _e_shell_surface_cb_fullscreen_set,
   _e_shell_surface_cb_popup_set,
   _e_shell_surface_cb_maximized_set,
   _e_shell_surface_cb_title_set,
   _e_shell_surface_cb_class_set,
};

static void
_e_shell_surface_configure_send(struct wl_resource *resource, uint32_t edges, int32_t width, int32_t height)
{
   if (!resource)
     return;

   wl_shell_surface_send_configure(resource, edges, width, height);
}

static void
_e_shell_surface_configure(struct wl_resource *resource, Evas_Coord x, Evas_Coord y, Evas_Coord w, Evas_Coord h)
{
   E_Client *ec;

   if (!resource) return;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   if (ec->parent)
     {
        if ((ec->netwm.type == E_WINDOW_TYPE_MENU) ||
            (ec->netwm.type == E_WINDOW_TYPE_POPUP_MENU) ||
            (ec->netwm.type == E_WINDOW_TYPE_DROPDOWN_MENU))
          {
             x = E_CLAMP(ec->parent->client.x + ec->comp_data->popup.x,
                         ec->parent->client.x,
                         ec->parent->client.x +
                         ec->parent->client.w - ec->client.w);
             y = E_CLAMP(ec->parent->client.y + ec->comp_data->popup.y,
                         ec->parent->client.y,
                         ec->parent->client.y +
                         ec->parent->client.h - ec->client.h);
          }
     }

   e_client_util_move_resize_without_frame(ec, x, y, w, h);
}

static void
_e_shell_surface_ping(struct wl_resource *resource)
{
   E_Client *ec;
   uint32_t serial;

   if (!resource)
     return;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   serial = wl_display_next_serial(e_comp_wl->wl.disp);
   wl_shell_surface_send_ping(ec->comp_data->shell.surface, serial);
}

static void
_e_shell_surface_map(struct wl_resource *resource)
{
   E_Client *ec;

   if (!resource)
     return;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   /* map this surface if needed */
   if ((!ec->comp_data->mapped) && (e_pixmap_usable_get(ec->pixmap)))
     {
        ELOGF("SHELL",
              "Map window  |win:0x%08x|ec_size:%d,%d",
              ec->pixmap, ec,
              (unsigned int)e_client_util_win_get(ec),
              ec->w, ec->h);

        /* unset previous content */
        e_comp_object_content_unset(ec->frame);

        ec->visible = EINA_TRUE;
        evas_object_geometry_set(ec->frame, ec->x, ec->y, ec->w, ec->h);
        evas_object_show(ec->frame);
        ec->comp_data->mapped = EINA_TRUE;

        if ((!ec->first_mapped) && (!ec->iconic) && (!e_client_util_ignored_get(ec)))
          {
             if (!ec->comp_data->sub.data)
               {
                  if (ec->post_lower)
                     evas_object_lower(ec->frame);
                  else if (ec->post_raise)
                     evas_object_raise(ec->frame);

                  ec->post_lower = EINA_FALSE;
                  ec->post_raise = EINA_FALSE;
               }
          }

        ec->first_mapped = 1;
     }
}

static void
_e_shell_surface_unmap(struct wl_resource *resource)
{
   E_Client *ec;

   if (!resource)
     return;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   if (ec->comp_data->mapped)
     {
        ec->visible = EINA_FALSE;
        evas_object_hide(ec->frame);
        ec->comp_data->mapped = EINA_FALSE;

        ELOGF("SHELL",
              "Unmap window  |win:0x%08x|ec_size:%d,%d",
              ec->pixmap, ec,
              (unsigned int)e_client_util_win_get(ec),
              ec->w, ec->h);
     }
}

static void
_e_shell_cb_shell_surface_get(struct wl_client *client, struct wl_resource *resource EINA_UNUSED, uint32_t id, struct wl_resource *surface_resource)
{
   E_Client *ec;
   E_Comp_Client_Data *cdata;

   /* get the pixmap from this surface so we can find the client */
   if (!(ec = wl_resource_get_user_data(surface_resource)))
     {
        wl_resource_post_error(surface_resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Pixmap Set On Surface");
        return;
     }
   ec->netwm.ping = 1;
   /* get the client data */
   if (!(cdata = ec->comp_data))
     {
        wl_resource_post_error(surface_resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Data For Client");
        return;
     }

   /* check for existing shell surface */
   if (cdata->shell.surface)
     {
        wl_resource_post_error(surface_resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "Client already has shell surface");
        return;
     }

   /* try to create a shell surface */
   if (!(cdata->shell.surface =
         wl_resource_create(client, &wl_shell_surface_interface, 1, id)))
     {
        wl_resource_post_no_memory(surface_resource);
        return;
     }

   wl_resource_set_implementation(cdata->shell.surface,
                                  &_e_shell_surface_interface,
                                  ec,
                                  _e_shell_surface_cb_destroy);

   e_object_ref(E_OBJECT(ec));

   cdata->shell.configure_send = _e_shell_surface_configure_send;
   cdata->shell.configure = _e_shell_surface_configure;
   cdata->shell.ping = _e_shell_surface_ping;
   cdata->shell.map = _e_shell_surface_map;
   cdata->shell.unmap = _e_shell_surface_unmap;

   e_comp_wl_shell_surface_ready(ec);
}

static void
_e_xdg_surface_state_add(struct wl_resource *resource, struct wl_array *states, uint32_t state)
{
   uint32_t *s;

   s = wl_array_add(states, sizeof(*s));
   if (s)
     *s = state;
   else
     wl_resource_post_no_memory(resource);
}

static void
_e_xdg_shell_surface_configure_send(struct wl_resource *resource, uint32_t edges, int32_t width, int32_t height)
{
   E_Client *ec;
   struct wl_array states;
   uint32_t serial;

   if (!resource) return;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   wl_array_init(&states);

   if (ec->fullscreen)
     {
        _e_xdg_surface_state_add(resource, &states, XDG_SURFACE_STATE_FULLSCREEN);

        // send fullscreen size
        if ((width == 0) && (height == 0))
          {
             width = ec->client.w && ec->client.h? ec->client.w : ec->w;
             height = ec->client.w && ec->client.h? ec->client.h : ec->h;
          }
     }
   else if (ec->maximized)
     {
        _e_xdg_surface_state_add(resource, &states, XDG_SURFACE_STATE_MAXIMIZED);

        // send maximized size
        if ((width == 0) && (height == 0))
          {
             width = ec->client.w && ec->client.h? ec->client.w : ec->w;
             height = ec->client.w && ec->client.h? ec->client.h : ec->h;
          }
     }
   if (edges != 0)
     _e_xdg_surface_state_add(resource, &states, XDG_SURFACE_STATE_RESIZING);
   if (ec->focused)
     _e_xdg_surface_state_add(resource, &states, XDG_SURFACE_STATE_ACTIVATED);

   if (ec->netwm.type != E_WINDOW_TYPE_POPUP_MENU)
     {
        serial = wl_display_next_serial(e_comp_wl->wl.disp);
        xdg_surface_send_configure(resource, width, height, &states, serial);
     }

   wl_array_release(&states);
}

static void
_e_xdg_shell_surface_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void
_e_xdg_shell_surface_cb_parent_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *parent_resource)
{
   E_Client *ec, *pc;

   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   if (parent_resource)
     {
        if (!(pc = wl_resource_get_user_data(parent_resource)))
          {
             ERR("Could not get parent resource clinet");
             return;
          }
        if (!pc->comp_data) return;
        parent_resource = pc->comp_data->surface;
     }

   /* set this client as a transient for parent */
   _e_shell_surface_parent_set(ec, parent_resource);
}

static void
_e_xdg_shell_surface_cb_title_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, const char *title)
{
   E_Client *ec;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   /* set title */
   eina_stringshare_replace(&ec->icccm.title, title);
   eina_stringshare_replace(&ec->icccm.name, title);
   if (ec->frame) e_comp_object_frame_title_set(ec->frame, title);
}

static void
_e_xdg_shell_surface_cb_app_id_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, const char *id)
{
   E_Client *ec;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   /* use the wl_client to get the pid * and set it in the netwm props */
   wl_client_get_credentials(client, &ec->netwm.pid, NULL, NULL);

   /* set class */
   eina_stringshare_replace(&ec->icccm.class, id);
   /* eina_stringshare_replace(&ec->netwm.name, id); */
   ec->changes.icon = !!ec->icccm.class;
   EC_CHANGED(ec);
}

static void
_e_xdg_shell_surface_cb_window_menu_show(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *seat_resource EINA_UNUSED, uint32_t serial EINA_UNUSED, int32_t x, int32_t y)
{
   E_Client *ec;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   //double timestamp;
   //timestamp = ecore_loop_time_get();
   //e_int_client_menu_show(ec, x, y, 0, timestamp);
}

static void
_e_xdg_shell_surface_cb_move(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *seat_resource EINA_UNUSED, uint32_t serial EINA_UNUSED)
{
   E_Client *ec;
   E_Binding_Event_Mouse_Button ev;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   if ((ec->maximized) || (ec->fullscreen)) return;

   TRACE_DS_BEGIN(SHELL:SURFACE MOVE REQUEST CB);

   switch (e_comp_wl->ptr.button)
     {
      case BTN_LEFT:   ev.button = 1; break;
      case BTN_MIDDLE: ev.button = 2; break;
      case BTN_RIGHT:  ev.button = 3; break;
      default:         ev.button = e_comp_wl->ptr.button; break;
     }

   e_comp_object_frame_xy_unadjust(ec->frame,
                                   wl_fixed_to_int(e_comp_wl->ptr.x),
                                   wl_fixed_to_int(e_comp_wl->ptr.y),
                                   &ev.canvas.x,
                                   &ev.canvas.y);

   _e_shell_surface_mouse_down_helper(ec, &ev, EINA_TRUE, 0);

   TRACE_DS_END();
}

static void
_e_xdg_shell_surface_cb_resize(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *seat_resource EINA_UNUSED, uint32_t serial EINA_UNUSED, uint32_t edges)
{
   E_Client *ec;
   E_Binding_Event_Mouse_Button ev;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   if ((edges == 0) ||
       (edges > 15) ||
       ((edges & 3) == 3) ||
       ((edges & 12) == 12))
     return;

   if ((ec->maximized) || (ec->fullscreen)) return;

   TRACE_DS_BEGIN(SHELL:SURFACE RESIZE REQUEST CB);

   e_comp_wl->resize.resource = resource;
   e_comp_wl->resize.edges = edges;
   e_comp_wl->ptr.grab_x = e_comp_wl->ptr.x - wl_fixed_from_int(ec->client.x);
   e_comp_wl->ptr.grab_y = e_comp_wl->ptr.y - wl_fixed_from_int(ec->client.y);

   switch (e_comp_wl->ptr.button)
     {
      case BTN_LEFT:   ev.button = 1; break;
      case BTN_MIDDLE: ev.button = 2; break;
      case BTN_RIGHT:  ev.button = 3; break;
      default:         ev.button = e_comp_wl->ptr.button; break;
     }

   e_comp_object_frame_xy_unadjust(ec->frame,
                                   wl_fixed_to_int(e_comp_wl->ptr.x),
                                   wl_fixed_to_int(e_comp_wl->ptr.y),
                                   &ev.canvas.x,
                                   &ev.canvas.y);

   _e_shell_surface_mouse_down_helper(ec, &ev, EINA_FALSE, edges);

   TRACE_DS_END();
}

static void
_e_xdg_shell_surface_cb_ack_configure(struct wl_client *client EINA_UNUSED, struct wl_resource *resource EINA_UNUSED, uint32_t serial EINA_UNUSED)
{
   /* No-Op */
}

static void
_e_xdg_shell_surface_cb_window_geometry_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, int32_t x, int32_t y, int32_t w, int32_t h)
{
   E_Client *ec;

   ec = wl_resource_get_user_data(resource);
   if (!ec)
     {
        wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }
   EINA_RECTANGLE_SET(&ec->comp_data->shell.window, x, y, w, h);
}

static void
_e_xdg_shell_surface_cb_maximized_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   E_Client *ec;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   if (!ec->lock_user_maximize)
     {
        e_client_maximize(ec,
                          ((e_config->maximize_policy & E_MAXIMIZE_TYPE) | E_MAXIMIZE_BOTH));
     }
}

static void
_e_xdg_shell_surface_cb_maximized_unset(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   E_Client *ec;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   e_client_unmaximize(ec, E_MAXIMIZE_BOTH);
   _e_xdg_shell_surface_configure_send(resource, 0, ec->w, ec->h);
}

static void
_e_xdg_shell_surface_cb_fullscreen_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *output_resource EINA_UNUSED)
{
   E_Client *ec;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   if (!ec->lock_user_fullscreen)
     e_client_fullscreen(ec, e_config->fullscreen_policy);
}

static void
_e_xdg_shell_surface_cb_fullscreen_unset(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   E_Client *ec;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   if (!ec->lock_user_fullscreen)
     e_client_unfullscreen(ec);
}

static void
_e_xdg_shell_surface_cb_minimized_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   E_Client *ec;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   if (!ec->lock_client_iconify)
     e_client_iconify(ec);
}

static const struct xdg_surface_interface _e_xdg_surface_interface =
{
   _e_xdg_shell_surface_cb_destroy,
   _e_xdg_shell_surface_cb_parent_set,
   _e_xdg_shell_surface_cb_title_set,
   _e_xdg_shell_surface_cb_app_id_set,
   _e_xdg_shell_surface_cb_window_menu_show,
   _e_xdg_shell_surface_cb_move,
   _e_xdg_shell_surface_cb_resize,
   _e_xdg_shell_surface_cb_ack_configure,
   _e_xdg_shell_surface_cb_window_geometry_set,
   _e_xdg_shell_surface_cb_maximized_set,
   _e_xdg_shell_surface_cb_maximized_unset,
   _e_xdg_shell_surface_cb_fullscreen_set,
   _e_xdg_shell_surface_cb_fullscreen_unset,
   _e_xdg_shell_surface_cb_minimized_set,
};

static void
_e_xdg_shell_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void
_e_xdg_shell_cb_unstable_version(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, int32_t version)
{
   if (version > 1)
     wl_resource_post_error(resource, 1, "XDG Version Not Implemented Yet");
}

static void
_e_xdg_shell_surface_configure(struct wl_resource *resource, Evas_Coord x, Evas_Coord y, Evas_Coord w, Evas_Coord h)
{
   E_Client *ec;

   if (!resource) return;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   if (ec->parent)
     {
        if ((ec->netwm.type == E_WINDOW_TYPE_MENU) ||
            (ec->netwm.type == E_WINDOW_TYPE_POPUP_MENU) ||
            (ec->netwm.type == E_WINDOW_TYPE_DROPDOWN_MENU))
          {
             x = E_CLAMP(ec->parent->client.x + ec->comp_data->popup.x,
                         ec->parent->client.x,
                         ec->parent->client.x + 
                         ec->parent->client.w - ec->client.w);
             y = E_CLAMP(ec->parent->client.y + ec->comp_data->popup.y,
                         ec->parent->client.y,
                         ec->parent->client.y + 
                         ec->parent->client.h - ec->client.h);
          }
     }

   e_client_util_move_resize_without_frame(ec, x, y, w, h);

   /* TODO: ack configure ?? */
}

static void
_e_xdg_shell_surface_ping(struct wl_resource *resource)
{
   E_Client *ec;
   uint32_t serial;
   struct wl_client *client;
   E_Xdg_Shell *esh;

   if (!resource)
     return;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   client = wl_resource_get_client(resource);

   esh = eina_hash_find(xdg_sh_hash, &client);
   EINA_SAFETY_ON_NULL_RETURN(esh);
   EINA_SAFETY_ON_NULL_RETURN(esh->res);

   if (!eina_list_data_find(esh->ping_ecs, ec))
     esh->ping_ecs = eina_list_append(esh->ping_ecs, ec);

   serial = wl_display_next_serial(e_comp_wl->wl.disp);
   xdg_shell_send_ping(esh->res, serial);
}

static Eina_Bool
_e_xdg_shell_surface_map_cb_timer(void *data)
{
   E_Client *ec = data;

   if (!ec) return ECORE_CALLBACK_CANCEL;
   if (e_object_is_del(E_OBJECT(ec))) return ECORE_CALLBACK_CANCEL;

   if ((!ec->comp_data->mapped) && (e_pixmap_usable_get(ec->pixmap)))
     {
        ELOGF("SHELL",
              "Map window by map_timer |win:0x%08x|ec_size:%d,%d",
              ec->pixmap, ec,
              (unsigned int)e_client_util_win_get(ec),
              ec->w, ec->h);

        if (ec->use_splash)
          {
             ELOGF("LAUNCH", "SHOW real win after splash effect by map_timer", ec->pixmap, ec);
             e_comp_object_signal_emit(ec->frame, "e,action,launch_real,done", "e");
          }

        /* unset previous content */
        e_comp_object_content_unset(ec->frame);

        /* map this surface if needed */
        ec->visible = EINA_TRUE;
        evas_object_show(ec->frame);
        ec->comp_data->mapped = EINA_TRUE;

        /* force update */
        e_comp_object_damage(ec->frame, 0, 0, ec->w, ec->h);
        e_comp_object_dirty(ec->frame);
        e_comp_object_render(ec->frame);

        e_comp_wl_surface_commit(ec);

        /* FIXME: sometimes popup surfaces Do Not raise above their
         * respective parents... */
        /* if (ec->netwm.type == E_WINDOW_TYPE_POPUP_MENU) */
        /*   e_client_raise_latest_set(ec); */

        if ((!ec->first_mapped) && (!ec->iconic) && (!e_client_util_ignored_get(ec)))
          {
             if (!ec->comp_data->sub.data)
               {
                  if (ec->post_lower)
                     evas_object_lower(ec->frame);
                  else if (ec->post_raise)
                     evas_object_raise(ec->frame);

                  ec->post_lower = EINA_FALSE;
                  ec->post_raise = EINA_FALSE;
               }
          }

        ec->first_mapped = 1;
        EC_CHANGED(ec);
     }
   ec->map_timer = NULL;
   return ECORE_CALLBACK_CANCEL;
}

static void
_e_xdg_shell_surface_map(struct wl_resource *resource)
{
   E_Client *ec;

   if (!resource) return;

   TRACE_DS_BEGIN(SHELL:MAP);

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        TRACE_DS_END();
        return;
     }

   if ((!ec->comp_data->mapped) && (e_pixmap_usable_get(ec->pixmap)))
     {
        int pw = 0;
        int ph = 0;
        int cw = ec->w;
        int ch = ec->h;
        e_pixmap_size_get(ec->pixmap, &pw, &ph);
        evas_object_geometry_get(ec->frame, NULL, NULL, &cw, &ch);
        if (cw == 0 && ch == 0)
          {
             cw = ec->w;
             ch = ec->h;
          }

        if (pw != cw || ph != ch)
          {
             if ((ec->changes.need_maximize) ||
                 ((ec->maximized & E_MAXIMIZE_BOTH) == E_MAXIMIZE_BOTH))
               {
                  // skip. because the pixmap's size doesnot same to ec's size
                  ELOGF("SHELL",
                        "Deny Map |win:0x%08x|ec_size:%d,%d|get_size:%d,%d|pix_size:%d,%d",
                        ec->pixmap, ec,
                        (unsigned int)e_client_util_win_get(ec),
                        ec->w, ec->h, cw, ch, pw, ph);

                  if (!ec->map_timer)
                    ec->map_timer = ecore_timer_add(3.0, _e_xdg_shell_surface_map_cb_timer, ec);

                  TRACE_DS_END();
                  return;
               }
          }
        E_FREE_FUNC(ec->map_timer, ecore_timer_del);

        ELOGF("SHELL",
              "Map window  |win:0x%08x|ec_size:%d,%d",
              ec->pixmap, ec,
              (unsigned int)e_client_util_win_get(ec),
              ec->w, ec->h);

        if (ec->use_splash)
          {
             ELOGF("LAUNCH", "SHOW real win after splash effect", ec->pixmap, ec);
             e_comp_object_signal_emit(ec->frame, "e,action,launch_real,done", "e");
          }

        /* unset previous content */
        e_comp_object_content_unset(ec->frame);

        /* map this surface if needed */
        ec->visible = EINA_TRUE;
        evas_object_show(ec->frame);
        ec->comp_data->mapped = EINA_TRUE;

        /* FIXME: sometimes popup surfaces Do Not raise above their
         * respective parents... */
        /* if (ec->netwm.type == E_WINDOW_TYPE_POPUP_MENU) */
        /*   e_client_raise_latest_set(ec); */

        if ((!ec->first_mapped) && (!ec->iconic) && (!e_client_util_ignored_get(ec)))
          {
             if (!ec->comp_data->sub.data)
               {
                  if (ec->post_lower)
                     evas_object_lower(ec->frame);
                  else if (ec->post_raise)
                     evas_object_raise(ec->frame);

                  ec->post_lower = EINA_FALSE;
                  ec->post_raise = EINA_FALSE;
               }
          }

        ec->first_mapped = 1;
        EC_CHANGED(ec);
     }

   TRACE_DS_END();
}

static void
_e_xdg_shell_surface_unmap(struct wl_resource *resource)
{
   E_Client *ec;

   if (!resource) return;

   TRACE_DS_BEGIN(SHELL:UNMAP);

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        TRACE_DS_END();
        return;
     }

   E_FREE_FUNC(ec->map_timer, ecore_timer_del);

   if (ec->comp_data->mapped)
     {
        ec->visible = EINA_FALSE;
        evas_object_hide(ec->frame);
        ec->comp_data->mapped = EINA_FALSE;

        ELOGF("SHELL",
              "Unmap window  |win:0x%08x|ec_size:%d,%d",
              ec->pixmap, ec,
              (unsigned int)e_client_util_win_get(ec),
              ec->w, ec->h);
     }

   TRACE_DS_END();
}

static void
_e_xdg_shell_cb_surface_get(struct wl_client *client, struct wl_resource *resource EINA_UNUSED, uint32_t id, struct wl_resource *surface_resource)
{
   E_Client *ec;
   E_Comp_Client_Data *cdata;

   /* get the pixmap from this surface so we can find the client */
   if (!(ec = wl_resource_get_user_data(surface_resource)))
     {
        wl_resource_post_error(surface_resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Pixmap Set On Surface");
        return;
     }

   ec->netwm.ping = 1;

   /* get the client data */
   if (!(cdata = ec->comp_data))
     {
        wl_resource_post_error(surface_resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Data For Client");
        return;
     }

   /* check for existing shell surface */
   if (cdata->shell.surface)
     {
        wl_resource_post_error(surface_resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "Client already has XDG shell surface");
        return;
     }

   /* try to create a shell surface */
   if (!(cdata->shell.surface =
         wl_resource_create(client, &xdg_surface_interface, 1, id)))
     {
        ERR("Could not create xdg shell surface");
        wl_resource_post_no_memory(surface_resource);
        return;
     }

   wl_resource_set_implementation(cdata->shell.surface,
                                  &_e_xdg_surface_interface,
                                  ec,
                                  _e_shell_surface_cb_destroy);

   e_object_ref(E_OBJECT(ec));

   cdata->shell.configure_send = _e_xdg_shell_surface_configure_send;
   cdata->shell.configure = _e_xdg_shell_surface_configure;
   cdata->shell.ping = _e_xdg_shell_surface_ping;
   cdata->shell.map = _e_xdg_shell_surface_map;
   cdata->shell.unmap = _e_xdg_shell_surface_unmap;

   /* set toplevel client properties */
   ec->icccm.accepts_focus = 1;
   if (!ec->internal)
     ec->borderless = 1;
   ec->lock_border = EINA_TRUE;
   if ((!ec->internal) || (!ec->borderless))
     ec->border.changed = ec->changes.border = !ec->borderless;
   if (ec->netwm.type == E_WINDOW_TYPE_UNKNOWN)
     ec->netwm.type = E_WINDOW_TYPE_NORMAL;
   ec->comp_data->set_win_type = EINA_TRUE;

   e_comp_wl_shell_surface_ready(ec);
}

static void
_e_xdg_shell_popup_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   _e_shell_surface_destroy(resource);
}

static const struct xdg_popup_interface _e_xdg_popup_interface =
{
   _e_xdg_shell_popup_cb_destroy,
};

static void
_e_xdg_shell_cb_popup_get(struct wl_client *client, struct wl_resource *resource EINA_UNUSED, uint32_t id, struct wl_resource *surface_resource, struct wl_resource *parent_resource, struct wl_resource *seat_resource EINA_UNUSED, uint32_t serial EINA_UNUSED, int32_t x, int32_t y)
{
   E_Client *ec;
   E_Comp_Client_Data *cdata;

   /* get the pixmap from this surface so we can find the client */
   if (!(ec = wl_resource_get_user_data(surface_resource)))
     {
        wl_resource_post_error(surface_resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Pixmap Set On Surface");
        return;
     }

   /* get the client data */
   if (!(cdata = ec->comp_data))
     {
        wl_resource_post_error(surface_resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Data For Client");
        return;
     }

   /* check for existing shell surface */
   if (cdata->shell.surface)
     {
        wl_resource_post_error(surface_resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "Client already has shell popup surface");
        return;
     }

   /* check for the parent surface */
   if (!parent_resource)
     {
        wl_resource_post_error(surface_resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "Popup requires a parent shell surface");
        return;
     }

   /* try to create a shell surface */
   if (!(cdata->shell.surface =
         wl_resource_create(client, &xdg_popup_interface, 1, id)))
     {
        ERR("Could not create xdg shell surface");
        wl_resource_post_no_memory(surface_resource);
        return;
     }

   wl_resource_set_implementation(cdata->shell.surface,
                                  &_e_xdg_popup_interface,
                                  ec,
                                  NULL);

   e_object_ref(E_OBJECT(ec));

   cdata->shell.configure_send = _e_xdg_shell_surface_configure_send;
   cdata->shell.configure = _e_xdg_shell_surface_configure;
   cdata->shell.ping = _e_xdg_shell_surface_ping;
   cdata->shell.map = _e_xdg_shell_surface_map;
   cdata->shell.unmap = _e_xdg_shell_surface_unmap;

   EC_CHANGED(ec);
   ec->new_client = ec->override = 1;
   e_client_unignore(ec);
   e_comp->new_clients++;
   if (!ec->internal)
     ec->borderless = !ec->internal_elm_win;
   ec->lock_border = EINA_TRUE;
   if (!ec->internal)
     ec->border.changed = ec->changes.border = !ec->borderless;
   ec->changes.icon = !!ec->icccm.class;
   ec->netwm.type = E_WINDOW_TYPE_POPUP_MENU;
   ec->comp_data->set_win_type = EINA_TRUE;
   evas_object_layer_set(ec->frame, E_LAYER_CLIENT_POPUP);

   /* set this client as a transient for parent */
   _e_shell_surface_parent_set(ec, parent_resource);

   if (ec->parent)
     {
        cdata->popup.x = E_CLAMP(x, 0, ec->parent->client.w);
        cdata->popup.y = E_CLAMP(y, 0, ec->parent->client.h);
     }
   else
     {
        cdata->popup.x = x;
        cdata->popup.y = y;
     }
}

static void
_e_xdg_shell_cb_pong(struct wl_client *client, struct wl_resource *resource, uint32_t serial EINA_UNUSED)
{
   E_Client *ec;
   E_Xdg_Shell *esh;

   esh = eina_hash_find(xdg_sh_hash, &client);
   EINA_SAFETY_ON_NULL_RETURN(esh);
   EINA_SAFETY_ON_NULL_RETURN(esh->res);
   EINA_SAFETY_ON_FALSE_RETURN(esh->res == resource);

   EINA_LIST_FREE(esh->ping_ecs, ec)
     {
        if (e_object_is_del(E_OBJECT(ec))) continue;

        ec->ping_ok = EINA_TRUE;
        ec->hung = EINA_FALSE;
     }
}

static void
_e_tz_res_cb_destroy(struct wl_client *client, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static const struct tizen_resource_interface _e_tz_res_interface =
{
   _e_tz_res_cb_destroy
};

static const struct wl_shell_interface _e_shell_interface =
{
   _e_shell_cb_shell_surface_get
};

static const struct xdg_shell_interface _e_xdg_shell_interface =
{
   _e_xdg_shell_cb_destroy,
   _e_xdg_shell_cb_unstable_version,
   _e_xdg_shell_cb_surface_get,
   _e_xdg_shell_cb_popup_get,
   _e_xdg_shell_cb_pong
};

static void
_e_xdg_shell_cb_unbind(struct wl_resource *resource)
{
   E_Xdg_Shell *esh;
   E_Client *ec;
   struct wl_client *client;

   client = wl_resource_get_client(resource);
   EINA_SAFETY_ON_NULL_RETURN(client);

   esh = eina_hash_find(xdg_sh_hash, &client);
   EINA_SAFETY_ON_NULL_RETURN(esh);

   EINA_LIST_FREE(esh->ping_ecs, ec)
     {
        if (e_object_is_del(E_OBJECT(ec))) continue;
        ec->ping_ok = EINA_TRUE;
        ec->hung = EINA_FALSE;
     }

   eina_hash_del_by_key(xdg_sh_hash, &client);

   E_FREE(esh);
}

static int
_e_xdg_shell_cb_dispatch(const void *implementation EINA_UNUSED, void *target, uint32_t opcode, const struct wl_message *message EINA_UNUSED, union wl_argument *args)
{
   struct wl_resource *res;

   if (!(res = target)) return 0;

   if (opcode != 1)
     {
        wl_resource_post_error(res, WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "Must call use_unstable_version first");
        return 0;
     }

   if (args[0].i != XDG_SERVER_VERSION)
     {
        wl_resource_post_error(res, WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "Incompatible versions. "
                               "Server: %d, Client: %d",
                               XDG_SERVER_VERSION, args[0].i);
        return 0;
     }

   wl_resource_set_implementation(res,
                                  &_e_xdg_shell_interface,
                                  NULL,
                                  _e_xdg_shell_cb_unbind);

   return 1;
}

static void
_e_shell_cb_unbind(struct wl_resource *resource EINA_UNUSED)
{
   /* no op */
}

static void
_e_shell_cb_bind(struct wl_client *client, void *data EINA_UNUSED, uint32_t version, uint32_t id)
{
   struct wl_resource *res;

   if (!(res = wl_resource_create(client, &wl_shell_interface, version, id)))
     {
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(res,
                                  &_e_shell_interface,
                                  e_comp->wl_comp_data,
                                  _e_shell_cb_unbind);
}

static void
_e_xdg_shell_cb_bind(struct wl_client *client, void *data EINA_UNUSED, uint32_t version, uint32_t id)
{
   E_Xdg_Shell *esh;
   struct wl_resource *res;

   res = wl_resource_create(client, &xdg_shell_interface, version, id);
   EINA_SAFETY_ON_NULL_GOTO(res, err);

   esh = E_NEW(E_Xdg_Shell, 1);
   EINA_SAFETY_ON_NULL_GOTO(esh, err);

   esh->wc = client;
   esh->res = res;
   eina_hash_add(xdg_sh_hash, &client, esh);

   wl_resource_set_dispatcher(res,
                              _e_xdg_shell_cb_dispatch,
                              NULL,
                              e_comp->wl_comp_data,
                              NULL);

   return;

err:
   wl_client_post_no_memory(client);
}

static void
_e_tz_surf_cb_tz_res_get(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *surface)
{
   struct wl_resource *res;
   E_Client *ec;
   uint32_t res_id;

   /* get the pixmap from this surface so we can find the client */
   if (!(ec = wl_resource_get_user_data(surface)))
     {
        wl_resource_post_error(surface,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Pixmap Set On Surface");
        return;
     }

   /* find the window id for this pixmap */
   res_id = e_pixmap_res_id_get(ec->pixmap);

   /* try to create a tizen_gid */
   if (!(res = wl_resource_create(client,
                                  &tizen_resource_interface,
                                  wl_resource_get_version(resource),
                                  id)))
     {
        wl_resource_post_no_memory(resource);
        return;
     }

   wl_resource_set_implementation(res,
                                  &_e_tz_res_interface,
                                  ec,
                                  NULL);

   tizen_resource_send_resource_id(res, res_id);
}

static const struct tizen_surface_interface _e_tz_surf_interface =
{
   _e_tz_surf_cb_tz_res_get,
};

static void
_e_tz_surf_cb_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   struct wl_resource *res;

   if (!(res = wl_resource_create(client,
                                  &tizen_surface_interface,
                                  MIN(version, 1),
                                  id)))
     {
        ERR("Could not create tizen_surface resource: %m");
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(res,
                                  &_e_tz_surf_interface,
                                  e_comp->wl_comp_data,
                                  NULL);
}

E_API E_Module_Api e_modapi = { E_MODULE_API_VERSION, "Wl_Desktop_Shell" };

E_API void *
e_modapi_init(E_Module *m)
{
   /* try to get the current compositor */
   if (!e_comp) return NULL;

   /* try to get the compositor data */
   if (!e_comp->wl_comp_data) return NULL;

   /* try to create global shell interface */
   if (!wl_global_create(e_comp_wl->wl.disp,
                         &wl_shell_interface,
                         1,
                         e_comp->wl_comp_data,
                         _e_shell_cb_bind))
     {
        ERR("Could not create shell global: %m");
        return NULL;
     }

   /* try to create global xdg_shell interface */
   if (!wl_global_create(e_comp_wl->wl.disp,
                         &xdg_shell_interface,
                         1,
                         e_comp->wl_comp_data,
                         _e_xdg_shell_cb_bind))
     {
        ERR("Could not create xdg_shell global: %m");
        return NULL;
     }

   e_scaler_init();

   if (!wl_global_create(e_comp_wl->wl.disp,
                         &tizen_surface_interface,
                         1,
                         e_comp->wl_comp_data,
                         _e_tz_surf_cb_bind))
     {
        ERR("Could not create tizen_surface to wayland globals: %m");
        return NULL;
     }

#ifdef HAVE_WL_TEXT_INPUT
   if (!e_input_panel_init())
     {
        ERR("Could not init input panel");
        return NULL;
     }
#endif

   xdg_sh_hash = eina_hash_pointer_new(NULL);

   return m;
}

E_API int
e_modapi_shutdown(E_Module *m EINA_UNUSED)
{
#ifdef HAVE_WL_TEXT_INPUT
   e_input_panel_shutdown();
#endif
   eina_hash_free(xdg_sh_hash);

   return 1;
}

E_API int
e_modapi_save(E_Module *m EINA_UNUSED)
{
   // do nothing
   return 1;
}
