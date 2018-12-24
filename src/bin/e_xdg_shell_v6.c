#include "e.h"
#include <xdg-shell-unstable-v6-server-protocol.h>

#ifdef LOG
#undef LOG
#endif
#ifdef ERR
#undef ERR
#endif

#define LOG(f, e, x...)  ELOGF("XDG6 <LOG>", f, e, ##x)
#define ERR(f, e, x...)  ELOGF("XDG6 <ERR>", f, e, ##x)

#define e_xdg_surface_role_biggest_struct E_Xdg_Toplevel
#define E_XDG_SURFACE_V6_TYPE (int)0xE0b06000

typedef enum   _E_Xdg_Surface_Role        E_Xdg_Surface_Role;
typedef struct _E_Xdg_Size                E_Xdg_Size;
typedef struct _E_Xdg_Shell               E_Xdg_Shell;
typedef struct _E_Xdg_Surface             E_Xdg_Surface;
typedef struct _E_Xdg_Toplevel            E_Xdg_Toplevel;
typedef struct _E_Xdg_Popup               E_Xdg_Popup;
typedef struct _E_Xdg_Positioner          E_Xdg_Positioner;
typedef struct _E_Xdg_Toplevel_State      E_Xdg_Toplevel_State;
typedef struct _E_Xdg_Surface_Configure   E_Xdg_Surface_Configure;

enum _E_Xdg_Surface_Role
{
   E_XDG_SURFACE_ROLE_NONE,
   E_XDG_SURFACE_ROLE_TOPLEVEL,
   E_XDG_SURFACE_ROLE_POPUP,
};

struct _E_Xdg_Size
{
   int w, h;
};

struct _E_Xdg_Shell
{
   struct wl_client     *wclient;
   struct wl_resource   *resource;     /* xdg_shell resource */
   Eina_List            *surfaces;     /* list of all E_Xdg_Surface belonging to shell */
   Eina_List            *positioners;  /* list of E_Xdg_Positioner */
   uint32_t              ping_serial;
};

struct _E_Xdg_Toplevel_State
{
   Eina_Bool maximized;
   Eina_Bool fullscreen;
   Eina_Bool resizing;
   Eina_Bool activated;
};

struct _E_Xdg_Surface
{
   E_Object e_obj_inherit;
   struct wl_resource   *resource;        /* wl_resource for Zxdg_Surface_V6 */
   E_Client             *ec;              /* E_Client corresponding Xdg_Surface */
   E_Xdg_Shell          *shell;           /* Xdg_Shell created Xdg_Surface */
   Eina_List            *configure_list;  /* list of data being appended whenever configure send and remove by ack_configure */

   Ecore_Idle_Enterer   *configure_idle;  /* Idle_Enterer for sending configure */
   Ecore_Event_Handler  *commit_handler;  /* Handler raised when wl_buffer is committed. */

   E_Xdg_Surface_Role    role;
   Eina_Rectangle        configured_geometry;   /* configured geometry by compositor */
   Eina_Rectangle        window_geometry;       /* window geometry set by client */

   Eina_Bool             configured :1;
   Eina_Bool             has_window_geometry: 1;
   Eina_Bool             wait_next_commit;
};

struct _E_Xdg_Toplevel
{
   E_Xdg_Surface         base;
   struct wl_resource   *resource;

   struct
   {
      E_Xdg_Toplevel_State state;
      E_Xdg_Size           size;
      uint32_t             edges;
   } pending;
   struct
   {
      E_Xdg_Toplevel_State state;
      E_Xdg_Size           size;
      E_Xdg_Size           min_size, max_size;
   } next;
   struct
   {
      E_Xdg_Toplevel_State state;
      E_Xdg_Size           min_size, max_size;
   } current;
};

struct _E_Xdg_Popup
{
   E_Xdg_Surface         base;

   struct wl_resource   *resource;
   E_Xdg_Surface        *parent;

   Eina_Rectangle        geometry;
   Eina_Bool             committed;
};

struct _E_Xdg_Surface_Configure
{
   uint32_t serial;
   E_Xdg_Toplevel_State state;
   E_Xdg_Size size;
};

struct _E_Xdg_Positioner
{
   E_Xdg_Shell          *shell;
   struct wl_resource   *resource;     /* xdg_positioner_v6 resources */
   E_Xdg_Size            size;
   Eina_Rectangle        anchor_rect;
   enum zxdg_positioner_v6_anchor                  anchor;
   enum zxdg_positioner_v6_gravity                 gravity;
   enum zxdg_positioner_v6_constraint_adjustment   constraint_adjustment;
   struct
   {
      int x, y;
   } offset;
};

static struct wl_global *global_resource = NULL;

static void             _e_xdg_shell_surface_add(E_Xdg_Shell *shell, E_Xdg_Surface *exsurf);
static void             _e_xdg_shell_surface_remove(E_Xdg_Shell *shell, E_Xdg_Surface *exsurf);
static void             _e_xdg_shell_ping(E_Xdg_Shell *shell);
static Eina_Bool        _e_xdg_surface_cb_configure_send(void *data);
static Eina_Rectangle   _e_xdg_positioner_geometry_get(E_Xdg_Positioner *p);

/**********************************************************
 * Implementation for Utility
 **********************************************************/
static Eina_Bool
_e_client_shsurface_assignable_check(E_Client *ec)
{
   if (!e_shell_e_client_shell_assignable_check(ec))
     {
        ERR("Could not assign shell", ec);
        wl_resource_post_error(ec->comp_data->surface,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "Could not assign shell surface to wl_surface");
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

static Eina_Bool
_e_client_xdg_shell_v6_assigned_check(E_Client *ec)
{
   return !!ec->comp_data->sh_v6.res_role;
}

static void
_e_client_xdg_shell_v6_assign(E_Client *ec,
                              struct wl_resource *resource,
                              E_Comp_Wl_Sh_Surf_Role role)
{
   if ((!ec) || (!ec->comp_data) || (e_object_is_del(E_OBJECT(ec))))
     return;
   ec->comp_data->sh_v6.res_role = resource;
   ec->comp_data->sh_v6.role = role;
}

static void
_e_client_xdg_shell_v6_role_assingment_unset(E_Client *ec)
{
   if ((!ec) || (!ec->comp_data) || (e_object_is_del(E_OBJECT(ec))))
     return;
   _e_client_xdg_shell_v6_assign(ec, NULL, E_COMP_WL_SH_SURF_ROLE_NONE);
}

static void
_validate_size(struct wl_resource *resource, int32_t value)
{
   if (value <= 0)
     wl_resource_post_error(resource, ZXDG_POSITIONER_V6_ERROR_INVALID_INPUT, "Invalid size passed");
}
/* End of utility */

/**********************************************************
 * Implementation for Xdg_popup
 **********************************************************/
static void
_e_xdg_popup_positioner_apply(E_Xdg_Popup *popup, E_Xdg_Positioner *pos)
{
   popup->geometry = _e_xdg_positioner_geometry_get(pos);

   /* TODO apply geometry to popup */

}

static void
_e_xdg_popup_parent_set(E_Xdg_Popup *popup, E_Xdg_Surface *parent)
{
   popup->parent = parent;
   /* set this client as a transient for parent */
   e_shell_e_client_parent_set(popup->base.ec, parent->ec->comp_data->surface);
}

static void
_e_xdg_popup_set(E_Xdg_Popup *popup, struct wl_resource *resource)
{
   popup->resource = resource;
   _e_client_xdg_shell_v6_assign(popup->base.ec, resource, E_COMP_WL_SH_SURF_ROLE_POPUP);
   e_shell_e_client_popup_set(popup->base.ec);
}

static void
_e_xdg_popup_committed(E_Xdg_Popup *popup)
{
   if (!popup->committed)
     {
        if (!popup->base.configure_idle)
          {
             popup->base.configure_idle =
                ecore_idle_enterer_add(_e_xdg_surface_cb_configure_send, popup);
          }
     }

   popup->committed = EINA_TRUE;

   /* TODO: Weston does update the position of popup here */
}

static void
_e_xdg_popup_configure_send(E_Xdg_Popup *popup)
{
   if (!popup) return;
   if (!popup->resource) return;

   zxdg_popup_v6_send_configure(popup->resource,
                                popup->geometry.x,
                                popup->geometry.y,
                                popup->geometry.w,
                                popup->geometry.h);
}

static void
_e_xdg_popup_cb_resource_destroy(struct wl_resource *resource)
{
   E_Xdg_Popup *popup;

   popup = wl_resource_get_user_data(resource);
   popup->resource = NULL;
   _e_client_xdg_shell_v6_role_assingment_unset(popup->base.ec);
   e_object_unref(E_OBJECT(popup));
}

static void
_e_xdg_popup_cb_destroy(struct wl_client *client, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void
_e_xdg_popup_cb_grab(struct wl_client *client,
                     struct wl_resource *resource,
                     struct wl_resource *res_seat,
                     uint32_t serial)
{
   /* TODO no op */
}

static const struct zxdg_popup_v6_interface _e_xdg_popup_interface =
{
   _e_xdg_popup_cb_destroy,
   _e_xdg_popup_cb_grab
};

/* End of Xdg_Popup */

/**********************************************************
 * Implementation for Xdg_toplevel
 ***********************************************************/
static void
_e_xdg_toplevel_set(E_Xdg_Toplevel *toplevel, struct wl_resource *resource)
{
   toplevel->resource = resource;
   _e_client_xdg_shell_v6_assign(toplevel->base.ec, resource, E_COMP_WL_SH_SURF_ROLE_TOPLV);
   e_shell_e_client_toplevel_set(toplevel->base.ec);
   e_comp_wl_shell_surface_ready(toplevel->base.ec);
}

static void
_e_xdg_toplevel_committed(E_Xdg_Toplevel *toplevel)
{
   E_Client *ec;
   int pw, ph;

   ec = toplevel->base.ec;
   if (!ec)
     {
        ERR("E_Xdg_Toplevel must have E_Client", NULL);
        return;
     }

   if (!ec->comp_data)
     {
        ERR("E_Client must have E_Comp_Client_Data", ec);
        return;
     }

   if (!toplevel->resource)
     {
        ERR("E_Client must have xdg_toplevel instance", ec);
        return;
     }

   if (!e_pixmap_usable_get(ec->pixmap))
     {
        ERR("E_Pixmap should be valid here", ec);
        return;
     }

   e_pixmap_size_get(ec->pixmap, &pw, &ph);

   if ((toplevel->next.state.maximized || toplevel->next.state.fullscreen) &&
        (toplevel->next.size.w != ec->comp_data->shell.window.w ||
        toplevel->next.size.h != ec->comp_data->shell.window.h ||
        toplevel->next.size.w != pw ||
        toplevel->next.size.h != ph))
     {
        ERR("Xdg_surface buffer does not match the configured state\nmaximized: "
            "%d fullscreen: %d, size:expected(%d %d) shell.request(%d %d) pixmap(%d %d)",
            ec,
            toplevel->next.state.maximized,
            toplevel->next.state.fullscreen,
            toplevel->next.size.w, toplevel->next.size.h,
            ec->comp_data->shell.window.w, ec->comp_data->shell.window.h,
            pw, ph);
        /* TODO Disable this part for now, but need to consider enabling it later.
         * To enable this part, we first need to ensure that do not send configure
         * with argument of size as 0, and make client ensure that set
         * window geometry correctly so that match its commit buffer size, if
         * its state is maximize or fullscreen.
         */
        /*
           wl_resource_post_error(toplevel->base.shell->resource,
           ZXDG_SHELL_V6_ERROR_INVALID_SURFACE_STATE,
           "xdg_surface buffer does not match the configured state");
           return;
           */
     }
   toplevel->current.state = toplevel->next.state;
   toplevel->current.min_size = toplevel->next.min_size;
   toplevel->current.max_size = toplevel->next.max_size;

   /* Now we can adjust size of its composite object corresponding client */
}

static void
_e_xdg_toplevel_configure_ack(E_Xdg_Toplevel *toplevel,
                              E_Xdg_Surface_Configure *configure)
{
   LOG("Ack configure TOPLEVEL size (%d %d) "
       "state (f %d m %d r %d a %d)",
       toplevel->base.ec,
       configure->size.w, configure->size.h,
       configure->state.fullscreen, configure->state.maximized,
       configure->state.resizing, configure->state.activated);

   toplevel->next.state = configure->state;
   toplevel->next.size = configure->size;
   toplevel->base.wait_next_commit = EINA_TRUE;
}

static void
_e_xdg_toplevel_configure_send(E_Xdg_Toplevel *toplevel,
                               E_Xdg_Surface_Configure *configure)
{
   uint32_t *s;
   struct wl_array states;

   if (!toplevel->resource) return;

   configure->state = toplevel->pending.state;
   configure->size = toplevel->pending.size;

   wl_array_init(&states);
   if (toplevel->pending.state.maximized)
     {
        s = wl_array_add(&states, sizeof(uint32_t));
        *s = ZXDG_TOPLEVEL_V6_STATE_MAXIMIZED;
     }
   if (toplevel->pending.state.fullscreen)
     {
        s = wl_array_add(&states, sizeof(uint32_t));
        *s = ZXDG_TOPLEVEL_V6_STATE_FULLSCREEN;
     }
   if (toplevel->pending.state.resizing)
     {
        s = wl_array_add(&states, sizeof(uint32_t));
        *s = ZXDG_TOPLEVEL_V6_STATE_RESIZING;
     }
   if (toplevel->pending.state.activated)
     {
        s = wl_array_add(&states, sizeof(uint32_t));
        *s = ZXDG_TOPLEVEL_V6_STATE_ACTIVATED;
     }

   LOG("Send configure: Topevel size (%d %d) "
       "state (f %d m %d r %d a %d)",
       toplevel->base.ec,
       toplevel->pending.size.w, toplevel->pending.size.h,
       toplevel->pending.state.fullscreen, toplevel->pending.state.maximized,
       toplevel->pending.state.resizing, toplevel->pending.state.activated);

   zxdg_toplevel_v6_send_configure(toplevel->resource,
                                   toplevel->pending.size.w,
                                   toplevel->pending.size.h,
                                   &states);

   wl_array_release(&states);
}

static void
_e_xdg_toplevel_configure_pending_set(E_Xdg_Toplevel *toplevel,
                                      uint32_t edges,
                                      int32_t width,
                                      int32_t height)
{
   E_Client *ec, *focused;

   ec = toplevel->base.ec;

   toplevel->pending.state.fullscreen = ec->fullscreen;
   toplevel->pending.state.maximized = !!ec->maximized;
   toplevel->pending.state.resizing = !!edges;
   toplevel->pending.edges = edges;
   toplevel->pending.size.w = width;
   toplevel->pending.size.h = height;

   if ((toplevel->pending.state.maximized) ||
       (toplevel->pending.state.fullscreen))
     {
        /* NOTE
         * DO NOT configure maximized or fullscreen surface to (0x0) size.
         * If the width or height arguments are zero, it means the client should
         * decide its own window dimension. See xdg-shell-v6.xml
         */
        LOG("FORCELY STAY current size (%d %d) of E_Client, requested size "
            "is (%d %d), the state (maximize %d, fullscreen %d)",
            ec, ec->w, ec->h, width, height,
            toplevel->pending.state.maximized,
            toplevel->pending.state.fullscreen);
        toplevel->pending.size.w = ec->w;
        toplevel->pending.size.h = ec->h;
     }

   focused = e_client_focused_get();
   toplevel->pending.state.activated = (ec == focused);

   LOG("Set pending state: edges %d size (%d %d) "
       "state (f %d m %d r %d a %d)",
       ec,
       toplevel->pending.edges,
       toplevel->pending.size.w, toplevel->pending.size.h,
       toplevel->pending.state.fullscreen, toplevel->pending.state.maximized,
       toplevel->pending.state.resizing, toplevel->pending.state.activated);
}

static Eina_Bool
_e_xdg_toplevel_pending_state_compare(E_Xdg_Toplevel *toplevel)
{
   E_Xdg_Surface_Configure *configure;
   int pw, ph;
   int cw, ch;

   struct {
        E_Xdg_Toplevel_State state;
        E_Xdg_Size size;
   } configured;

   /* must send configure at least once */
   if (!toplevel->base.configured)
     return EINA_FALSE;

   if (!toplevel->base.configure_list)
     {
        /* if configure_list is empty, last configure is actually the current
         * state */
        e_pixmap_size_get(toplevel->base.ec->pixmap, &pw, &ph);
        e_client_geometry_get(toplevel->base.ec, NULL, NULL, &cw, &ch);

        if ((pw != cw) || (ph != ch))
          {
             ERR("The size of buffer is different with expected "
                 "client size. So, here, let it compare with buffer size.",
                 toplevel->base.ec);
          }

        configured.state = toplevel->current.state;
        configured.size.w = pw;
        configured.size.h = ph;
     }
   else
     {
        configure = eina_list_last_data_get(toplevel->base.configure_list);
        configured.state = configure->state;
        configured.size = configure->size;
     }

   if (toplevel->pending.state.activated != configured.state.activated)
     return EINA_FALSE;
   if (toplevel->pending.state.fullscreen != configured.state.fullscreen)
     return EINA_FALSE;
   if (toplevel->pending.state.maximized != configured.state.maximized)
     return EINA_FALSE;
   if (toplevel->pending.state.resizing != configured.state.resizing)
     return EINA_FALSE;

   if ((toplevel->pending.size.w == configured.size.w) &&
       (toplevel->pending.size.h == configured.size.h))
     return EINA_TRUE;

   if ((toplevel->pending.size.w == 0) &&
       (toplevel->pending.size.h == 0))
     return EINA_TRUE;

   return EINA_FALSE;
}

static void
_e_xdg_toplevel_cb_resource_destroy(struct wl_resource *resource)
{
   E_Xdg_Toplevel *toplevel;

   toplevel = wl_resource_get_user_data(resource);
   toplevel->resource = NULL;
   _e_client_xdg_shell_v6_role_assingment_unset(toplevel->base.ec);
   e_object_unref(E_OBJECT(toplevel));
}

static void
_e_xdg_toplevel_cb_destroy(struct wl_client *client, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void
_e_xdg_toplevel_cb_parent_set(struct wl_client *client,
                              struct wl_resource *resource,
                              struct wl_resource *res_parent)
{
   E_Xdg_Toplevel *toplevel, *parent;
   E_Client *pc;
   struct wl_resource *parent_wsurface = NULL;

   toplevel = wl_resource_get_user_data(resource);
   if (!toplevel)
     return;

   if (res_parent)
     {
        parent = wl_resource_get_user_data(res_parent);
        if (!parent)
          {
             ERR("No E_Xdg_Toplevel data in wl_resource", NULL);
             wl_resource_post_error(res_parent,
                                    WL_DISPLAY_ERROR_INVALID_OBJECT,
                                    "No E_Xdg_Toplevel data in wl_resource");

             return;
          }

        pc = parent->base.ec;
        if (!pc)
          {
             ERR("Toplevel must have E_Client", NULL);
             wl_resource_post_error(res_parent,
                                    WL_DISPLAY_ERROR_INVALID_OBJECT,
                                    "No E_Client data in wl_resource");
             return;
          }

        if (!pc->comp_data) return;

        parent_wsurface = pc->comp_data->surface;
     }

   /* set this client as a transient for parent */
   e_shell_e_client_parent_set(toplevel->base.ec, parent_wsurface);
}

static void
_e_xdg_toplevel_cb_title_set(struct wl_client *client,
                             struct wl_resource *resource,
                             const char *title)
{
   E_Xdg_Toplevel *toplevel;

   toplevel = wl_resource_get_user_data(resource);
   if (!toplevel)
     return;

   e_shell_e_client_name_title_set(toplevel->base.ec, title, title);
}

static void
_e_xdg_toplevel_cb_app_id_set(struct wl_client *client,
                              struct wl_resource *resource,
                              const char *app_id)
{
   E_Xdg_Toplevel *toplevel;
   E_Client *ec;

   toplevel = wl_resource_get_user_data(resource);
   if (!toplevel)
     return;

   ec = toplevel->base.ec;
   if (!ec)
     {
        ERR("Toplevel must have E_Client", NULL);
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No E_Client data in wl_resource");
        return;
     }

   /* use the wl_client to get the pid * and set it in the netwm props */
   wl_client_get_credentials(client, &ec->netwm.pid, NULL, NULL);

   e_shell_e_client_app_id_set(ec, app_id);
}

static void
_e_xdg_toplevel_cb_win_menu_show(struct wl_client *client,
                                 struct wl_resource *resource,
                                 struct wl_resource *res_seat,
                                 uint32_t serial,
                                 int32_t x,
                                 int32_t y)
{
   /* TODO no op */
}

static void
_e_xdg_toplevel_cb_move(struct wl_client *client,
                        struct wl_resource *resource,
                        struct wl_resource *res_seat,
                        uint32_t serial)
{
   E_Xdg_Toplevel *toplevel;

   toplevel = wl_resource_get_user_data(resource);
   if (!toplevel)
     return;

   if (!e_shell_e_client_interactive_move(toplevel->base.ec, res_seat))
     {
        ERR("Failed to move this Toplevel", NULL);
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "Can't move this surface");
        return;
     }
}

static void
_e_xdg_toplevel_cb_resize(struct wl_client *client,
                          struct wl_resource *resource,
                          struct wl_resource *res_seat,
                          uint32_t serial,
                          uint32_t edges)
{
   E_Xdg_Toplevel *toplevel;

   toplevel = wl_resource_get_user_data(resource);
   if (!toplevel)
     return;

   if (!e_shell_e_client_interactive_resize(toplevel->base.ec, resource, res_seat, edges))
     {
        ERR("Failed to resize this Toplevel", NULL);
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "Can't resize this surface");
        return;
     }
}

static void
_e_xdg_toplevel_cb_max_size_set(struct wl_client *client,
                                struct wl_resource *resource,
                                int32_t w,
                                int32_t h)
{
   E_Xdg_Toplevel *toplevel;

   toplevel = wl_resource_get_user_data(resource);
   if (!toplevel)
     return;

   toplevel->next.max_size.w = w;
   toplevel->next.max_size.h = h;
}

static void
_e_xdg_toplevel_cb_min_size_set(struct wl_client *client,
                                struct wl_resource *resource,
                                int32_t w,
                                int32_t h)
{
   E_Xdg_Toplevel *toplevel;

   toplevel = wl_resource_get_user_data(resource);
   if (!toplevel)
     return;

   toplevel->next.min_size.w = w;
   toplevel->next.min_size.h = h;
}

static void
_e_xdg_toplevel_cb_maximized_set(struct wl_client *client, struct wl_resource *resource)
{
   E_Xdg_Toplevel *toplevel;
   E_Client *ec;
   E_Maximize max;

   toplevel = wl_resource_get_user_data(resource);
   if (!toplevel)
     return;

   ec = toplevel->base.ec;
   if (!ec)
     {
        ERR("Toplevel must have E_Client", NULL);
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No E_Client data in wl_resource");
        return;
     }

   if (!ec->lock_user_maximize)
     {
        max = (e_config->maximize_policy & E_MAXIMIZE_TYPE) | E_MAXIMIZE_BOTH;
        e_client_maximize(ec, max);
     }
}

static void
_e_xdg_toplevel_cb_maximized_unset(struct wl_client *client, struct wl_resource *resource)
{
   E_Xdg_Toplevel *toplevel;
   E_Client *ec;

   toplevel = wl_resource_get_user_data(resource);
   if (!toplevel)
     return;

   ec = toplevel->base.ec;
   if (!ec)
     {
        ERR("Toplevel must have E_Client", NULL);
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No E_Client data in wl_resource");
        return;
     }

   /* it's doubtful */
   e_client_unmaximize(ec, E_MAXIMIZE_BOTH);
}

static void
_e_xdg_toplevel_cb_fullscreen_set(struct wl_client *client,
                                  struct wl_resource *resource,
                                  struct wl_resource *res_output)
{
   E_Xdg_Toplevel *toplevel;
   E_Client *ec;

   toplevel = wl_resource_get_user_data(resource);
   if (!toplevel)
     return;

   ec = toplevel->base.ec;
   if (!ec)
     {
        ERR("Toplevel must have E_Client", NULL);
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No E_Client data in wl_resource");
        return;
     }

   if (!ec->lock_user_fullscreen)
     e_client_fullscreen(ec, e_config->fullscreen_policy);
}

static void
_e_xdg_toplevel_cb_fullscreen_unset(struct wl_client *client, struct wl_resource *resource)
{
   E_Xdg_Toplevel *toplevel;
   E_Client *ec;

   toplevel = wl_resource_get_user_data(resource);
   if (!toplevel)
     return;

   ec = toplevel->base.ec;
   if (!ec)
     {
        ERR("Toplevel must have E_Client", NULL);
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No E_Client data in wl_resource");
        return;
     }

   if (!ec->lock_user_fullscreen)
     e_client_unfullscreen(ec);
}

static void
_e_xdg_toplevel_cb_minimized_set(struct wl_client *client, struct wl_resource *resource)
{
   E_Xdg_Toplevel *toplevel;
   E_Client *ec;

   toplevel = wl_resource_get_user_data(resource);
   if (!toplevel)
     return;

   ec = toplevel->base.ec;
   if (!ec)
     {
        ERR("Toplevel must have E_Client", NULL);
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No E_Client data in wl_resource");
        return;
     }

   if (!ec->lock_client_iconify)
     e_client_iconify(ec);
}

static const struct zxdg_toplevel_v6_interface _e_xdg_toplevel_interface =
{
   _e_xdg_toplevel_cb_destroy,
   _e_xdg_toplevel_cb_parent_set,
   _e_xdg_toplevel_cb_title_set,
   _e_xdg_toplevel_cb_app_id_set,
   _e_xdg_toplevel_cb_win_menu_show,
   _e_xdg_toplevel_cb_move,
   _e_xdg_toplevel_cb_resize,
   _e_xdg_toplevel_cb_max_size_set,
   _e_xdg_toplevel_cb_min_size_set,
   _e_xdg_toplevel_cb_maximized_set,
   _e_xdg_toplevel_cb_maximized_unset,
   _e_xdg_toplevel_cb_fullscreen_set,
   _e_xdg_toplevel_cb_fullscreen_unset,
   _e_xdg_toplevel_cb_minimized_set
};

/* End of Xdg_toplevel */

/**********************************************************
 * Implementation for Xdg_Positioner
 **********************************************************/
static Eina_Rectangle
_e_xdg_positioner_geometry_get(E_Xdg_Positioner *p)
{
   Eina_Rectangle geometry = {
        .x = p->offset.x,
        .y = p->offset.y,
        .w = p->size.w,
        .h = p->size.h,
   };

   if (p->anchor & ZXDG_POSITIONER_V6_ANCHOR_TOP)
     geometry.y += p->anchor_rect.y;
   else if (p->anchor & ZXDG_POSITIONER_V6_ANCHOR_BOTTOM)
     geometry.y += p->anchor_rect.y + p->anchor_rect.h;
   else
     geometry.y += p->anchor_rect.y + p->anchor_rect.h / 2;

   if (p->anchor & ZXDG_POSITIONER_V6_ANCHOR_LEFT)
     geometry.x += p->anchor_rect.x;
   else if (p->anchor & ZXDG_POSITIONER_V6_ANCHOR_RIGHT)
     geometry.x += p->anchor_rect.x + p->anchor_rect.w;
   else
     geometry.x += p->anchor_rect.x + p->anchor_rect.w / 2;

   if (p->gravity & ZXDG_POSITIONER_V6_GRAVITY_TOP)
     geometry.y -= geometry.h;
   else if (!(p->gravity & ZXDG_POSITIONER_V6_GRAVITY_BOTTOM))
     geometry.y = geometry.h / 2;

   if (p->gravity & ZXDG_POSITIONER_V6_GRAVITY_LEFT)
     geometry.x -= geometry.w;
   else if (!(p->gravity & ZXDG_POSITIONER_V6_GRAVITY_RIGHT))
     geometry.x = geometry.w / 2;

   if (p->constraint_adjustment == ZXDG_POSITIONER_V6_CONSTRAINT_ADJUSTMENT_NONE)
     return geometry;

   /* TODO: According to weston, add compositor policy configuration and the
    * code here
    */

   return geometry;
}

static Eina_Bool
_e_xdg_positioner_validation_check(E_Xdg_Positioner *p)
{
   return ((p->size.w != 0) && (p->anchor_rect.w != 0));
}

static void
_e_xdg_positioner_cb_resource_destroy(struct wl_resource *resource)
{
   E_Xdg_Positioner *p;

   p = wl_resource_get_user_data(resource);
   EINA_SAFETY_ON_NULL_RETURN(p);

   if (!p->shell)
     goto finish;

   p->shell->positioners = eina_list_remove(p->shell->positioners, p);

finish:
   free(p);
}

static void
_e_xdg_positioner_cb_destroy(struct wl_client *client, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void
_e_xdg_positioner_cb_size_set(struct wl_client *client,
                              struct wl_resource *resource,
                              int32_t w, int32_t h)
{
   E_Xdg_Positioner *p;

   _validate_size(resource, w);
   _validate_size(resource, h);

   p = wl_resource_get_user_data(resource);
   p->size.w = w;
   p->size.h = h;
}

static void
_e_xdg_positioner_cb_anchor_rect_set(struct wl_client *client,
                                     struct wl_resource *resource,
                                     int32_t x, int32_t y, int32_t w, int32_t h)
{
   E_Xdg_Positioner *p;

   _validate_size(resource, w);
   _validate_size(resource, h);

   p = wl_resource_get_user_data(resource);
   EINA_RECTANGLE_SET(&p->anchor_rect, x, y, w, h);
}

static void
_e_xdg_positioner_cb_anchor_set(struct wl_client *client,
                                struct wl_resource *resource,
                                enum zxdg_positioner_v6_anchor anchor)
{
   E_Xdg_Positioner *p;

   if ((anchor & (ZXDG_POSITIONER_V6_ANCHOR_TOP | ZXDG_POSITIONER_V6_ANCHOR_BOTTOM)) ==
       (ZXDG_POSITIONER_V6_ANCHOR_TOP | ZXDG_POSITIONER_V6_ANCHOR_BOTTOM))
     {
        wl_resource_post_error(resource, ZXDG_POSITIONER_V6_ERROR_INVALID_INPUT,
                               "Invalid anchor values passed");
     }
   else if ((anchor & (ZXDG_POSITIONER_V6_ANCHOR_LEFT | ZXDG_POSITIONER_V6_ANCHOR_RIGHT)) ==
            (ZXDG_POSITIONER_V6_ANCHOR_LEFT | ZXDG_POSITIONER_V6_ANCHOR_RIGHT))
     {
        wl_resource_post_error(resource, ZXDG_POSITIONER_V6_ERROR_INVALID_INPUT,
                               "Invalid anchor values passed");
     }
   else
     {
        p = wl_resource_get_user_data(resource);
        p->anchor = anchor;
     }
}

static void
_e_xdg_positioner_cb_gravity_set(struct wl_client *client,
                                 struct wl_resource *resource,
                                 enum zxdg_positioner_v6_gravity gravity)
{
   E_Xdg_Positioner *p;

   if ((gravity & (ZXDG_POSITIONER_V6_GRAVITY_TOP | ZXDG_POSITIONER_V6_GRAVITY_BOTTOM)) ==
       (ZXDG_POSITIONER_V6_GRAVITY_TOP | ZXDG_POSITIONER_V6_GRAVITY_BOTTOM))
     {
        wl_resource_post_error(resource, ZXDG_POSITIONER_V6_ERROR_INVALID_INPUT,
                               "Invalid gravity values passed");
     }
   else if ((gravity & (ZXDG_POSITIONER_V6_GRAVITY_LEFT | ZXDG_POSITIONER_V6_GRAVITY_RIGHT)) ==
            (ZXDG_POSITIONER_V6_GRAVITY_LEFT | ZXDG_POSITIONER_V6_GRAVITY_RIGHT))
     {
        wl_resource_post_error(resource, ZXDG_POSITIONER_V6_ERROR_INVALID_INPUT,
                               "Invalid gravity values passed");
     }
   else
     {
        p = wl_resource_get_user_data(resource);
        p->gravity = gravity;
     }
}

static void
_e_xdg_positioner_cb_constraint_adjustment_set(struct wl_client *client,
                                               struct wl_resource *resource,
                                               enum zxdg_positioner_v6_constraint_adjustment constraint_adjustment)
{
   E_Xdg_Positioner *p;

   p = wl_resource_get_user_data(resource);
   p->constraint_adjustment = constraint_adjustment;
}

static void
_e_xdg_positioner_cb_offset_set(struct wl_client *client,
                                struct wl_resource *resource,
                                int32_t x, int32_t y)
{
   E_Xdg_Positioner *p;

   p = wl_resource_get_user_data(resource);
   p->offset.x = x;
   p->offset.y = y;
}

static const struct zxdg_positioner_v6_interface _e_xdg_positioner_interface =
{
   _e_xdg_positioner_cb_destroy,
   _e_xdg_positioner_cb_size_set,
   _e_xdg_positioner_cb_anchor_rect_set,
   _e_xdg_positioner_cb_anchor_set,
   _e_xdg_positioner_cb_gravity_set,
   _e_xdg_positioner_cb_constraint_adjustment_set,
   _e_xdg_positioner_cb_offset_set,
};

/* End of Xdg_positioner */

/**********************************************************
 * Implementation for Xdg_Surface
 **********************************************************/
static const char *
_e_xdg_surface_util_role_string_get(E_Xdg_Surface *exsurf)
{
   switch (exsurf->role)
     {
      case E_XDG_SURFACE_ROLE_TOPLEVEL:
         return "TOPLEVEL";
      case E_XDG_SURFACE_ROLE_POPUP:
         return "POPUP";
      default:
      case E_XDG_SURFACE_ROLE_NONE:
         return "NONE";
     }
}

static Eina_Bool
_e_xdg_surface_cb_configure_send(void *data)
{
   E_Xdg_Surface *exsurf;
   E_Xdg_Surface_Configure *configure;

   exsurf = data;

   EINA_SAFETY_ON_NULL_GOTO(exsurf, end);
   EINA_SAFETY_ON_NULL_GOTO(exsurf->ec, end);
   EINA_SAFETY_ON_NULL_GOTO(exsurf->ec->comp_data, end);
   EINA_SAFETY_ON_NULL_GOTO(exsurf->resource, end);

   if (e_object_is_del(E_OBJECT(exsurf->ec)))
     goto end;

   /* Make configure */
   configure = E_NEW(E_Xdg_Surface_Configure, 1);
   if (!configure)
     {
        ERR("Failed to allocate memory: E_Xdg_Surface_Configure", NULL);
        goto end;
     }

   exsurf->configure_list = eina_list_append(exsurf->configure_list, configure);
   configure->serial = wl_display_next_serial(e_comp_wl->wl.disp);

   switch (exsurf->role)
     {
      case E_XDG_SURFACE_ROLE_NONE:
         ERR("Cannot reach here", NULL);
         break;
      case E_XDG_SURFACE_ROLE_POPUP:
         _e_xdg_popup_configure_send((E_Xdg_Popup *)exsurf);
         break;
      case E_XDG_SURFACE_ROLE_TOPLEVEL:
         _e_xdg_toplevel_configure_send((E_Xdg_Toplevel *)exsurf, configure);
         break;
     }

   zxdg_surface_v6_send_configure(exsurf->resource, configure->serial);

   LOG("Send configure: %s serial %d", exsurf->ec,
       _e_xdg_surface_util_role_string_get(exsurf), configure->serial);

end:
   exsurf->configure_idle = NULL;
   return ECORE_CALLBACK_DONE;
}

static void
_e_xdg_surface_configure_send(struct wl_resource *resource,
                              uint32_t edges,
                              int32_t width,
                              int32_t height)
{
   E_Xdg_Surface *exsurf;
   Eina_Bool pending_same = EINA_FALSE;

   EINA_SAFETY_ON_NULL_RETURN(resource);

   exsurf = wl_resource_get_user_data(resource);
   if (!exsurf)
     {
        ERR("Invalid wl_resource", NULL);
        return;
     }

   LOG("Scheduling task to send configure %s edges %d w %d h %d",
       exsurf->ec,
       _e_xdg_surface_util_role_string_get(exsurf), edges, width, height);

   switch (exsurf->role)
     {
      case E_XDG_SURFACE_ROLE_NONE:
      default:
         ERR("Cannot reach here", exsurf->ec);
         break;
      case E_XDG_SURFACE_ROLE_TOPLEVEL:
         _e_xdg_toplevel_configure_pending_set((E_Xdg_Toplevel *)exsurf,
                                               edges, width, height);

         pending_same = _e_xdg_toplevel_pending_state_compare((E_Xdg_Toplevel *)exsurf);
         if (pending_same)
           {
              LOG("\tSKIP Configuring state is same with current state",
                  exsurf->ec);
           }
         break;
      case E_XDG_SURFACE_ROLE_POPUP:
         break;
     }

   if (exsurf->configure_idle)
     {
        if (!pending_same)
          return;

        LOG("\tRemove configure idler", exsurf->ec);

        E_FREE_FUNC(exsurf->configure_idle, ecore_idle_enterer_del);
     }
   else
     {
        if (pending_same)
          return;

        exsurf->configure_idle =
           ecore_idle_enterer_add(_e_xdg_surface_cb_configure_send, exsurf);

        LOG("\tAdd configure idler %p",
            exsurf->ec, exsurf->configure_idle);
     }
}

static void
_e_xdg_surface_configure(struct wl_resource *resource,
                         Evas_Coord x, Evas_Coord y,
                         Evas_Coord w, Evas_Coord h)
{
   E_Xdg_Surface *exsurf;

   EINA_SAFETY_ON_NULL_RETURN(resource);

   /* get the client for this resource */
   exsurf = wl_resource_get_user_data(resource);
   if (!exsurf)
     {
        ERR("No E_Xdg_Surface data in wl_resource", NULL);
        return;
     }

   if (!exsurf->configured)
     {
        // any attempts by a client to attach or manipulate a buffer prior to the first xdg_surface.configure call must
        // be treated as errors.
        ERR("Could not handle %s prior to the first xdg_surface.configure",
            exsurf->ec,
            _e_xdg_surface_util_role_string_get(exsurf));
        return;
     }

   EINA_RECTANGLE_SET(&exsurf->configured_geometry, x, y, w, h);

   e_client_util_move_resize_without_frame(exsurf->ec, x, y, w, h);
}

static void
_e_xdg_surface_ping(struct wl_resource *resource)
{
   E_Xdg_Surface *exsurf;

   exsurf = wl_resource_get_user_data(resource);
   if (!exsurf)
     {
        ERR("No E_Xdg_Surface data in wl_resource", NULL);
        return;
     }

   if (e_object_is_del(E_OBJECT(exsurf->ec)))
     return;

   _e_xdg_shell_ping(exsurf->shell);
}

static void
_e_xdg_surface_map(struct wl_resource *resource)
{
   E_Xdg_Surface *exsurf;

   exsurf = wl_resource_get_user_data(resource);
   if (!exsurf)
     {
        ERR("No E_Xdg_Surface in wl_resource", NULL);
        return;
     }

   e_shell_e_client_map(exsurf->ec);
}

static void
_e_xdg_surface_unmap(struct wl_resource *resource)
{
   E_Xdg_Surface *exsurf;

   exsurf = wl_resource_get_user_data(resource);
   if (!exsurf)
     {
        ERR("No E_Xdg_Surface in wl_resource", NULL);
        return;
     }

   e_shell_e_client_unmap(exsurf->ec);
}

static Eina_Bool
_e_xdg_surface_role_assign(E_Xdg_Surface *exsurf,
                           struct wl_resource *resource,
                           E_Xdg_Surface_Role role)
{
   E_Shell_Surface_Api api = {
        .configure_send = _e_xdg_surface_configure_send,
        .configure = _e_xdg_surface_configure,
        .ping = _e_xdg_surface_ping,
        .map = _e_xdg_surface_map,
        .unmap = _e_xdg_surface_unmap,
   };

   if (_e_client_xdg_shell_v6_assigned_check(exsurf->ec))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "Client already has shell resource");
        return EINA_FALSE;
     }

   exsurf->role = role;

   e_shell_e_client_shsurface_api_set(exsurf->ec, &api);

   switch (role)
     {
      case E_XDG_SURFACE_ROLE_NONE:
      default:
         ERR("Cannot reach here", exsurf->ec);
         return EINA_FALSE;

      case E_XDG_SURFACE_ROLE_TOPLEVEL:
         _e_xdg_toplevel_set((E_Xdg_Toplevel *)exsurf, resource);
         break;

      case E_XDG_SURFACE_ROLE_POPUP:
         _e_xdg_popup_set((E_Xdg_Popup *)exsurf, resource);
         break;
     }

   return EINA_TRUE;
}

static void
_e_xdg_surface_cb_destroy(struct wl_client *client, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void
_e_xdg_surface_cb_toplevel_get(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
   E_Xdg_Surface *exsurf;
   struct wl_resource *toplevel_resource;

   exsurf = wl_resource_get_user_data(resource);
   if (!exsurf)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No E_Xdg_Surface data in wl_resource");
        return;
     }

   toplevel_resource = wl_resource_create(client, &zxdg_toplevel_v6_interface, 1, id);
   if (!toplevel_resource)
     {
        ERR("Could not create xdg toplevel resource", NULL);
        wl_resource_post_no_memory(resource);
        return;
     }

   wl_resource_set_implementation(toplevel_resource,
                                  &_e_xdg_toplevel_interface,
                                  exsurf,
                                  _e_xdg_toplevel_cb_resource_destroy);

   if (!_e_xdg_surface_role_assign(exsurf, toplevel_resource, E_XDG_SURFACE_ROLE_TOPLEVEL))
     {
        ERR("Failed to assign TOPLEVEL role", exsurf->ec);
        wl_resource_destroy(toplevel_resource);
        return;
     }

   e_object_ref(E_OBJECT(exsurf));
}

static void
_e_xdg_surface_cb_popup_get(struct wl_client *client,
                            struct wl_resource *resource,
                            uint32_t id,
                            struct wl_resource *res_parent,
                            struct wl_resource *res_pos)
{
   struct wl_resource *popup_resource;
   E_Xdg_Surface *exsurf, *parent;
   E_Xdg_Positioner *p;

   if (!res_parent)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "Popup requires a parent shell surface");
        return;
     }

   parent = wl_resource_get_user_data(res_parent);
   if (!parent)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "xdg_popup must have parent");
        return;
     }

   p = wl_resource_get_user_data(res_pos);
   if (!p)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "invalid positioner");
        return;
     }

   if (!_e_xdg_positioner_validation_check(p))
     {
        wl_resource_post_error(resource,
                               ZXDG_SHELL_V6_ERROR_INVALID_POSITIONER,
                               "invalid positioner");
        return;
     }

   exsurf = wl_resource_get_user_data(resource);
   if (!exsurf)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No E_Xdg_Popup data in wl_resource");
        return;
     }

   popup_resource = wl_resource_create(client, &zxdg_popup_v6_interface, 1, id);
   if (!popup_resource)
     {
        ERR("Could not create xdg popup resource", NULL);
        wl_resource_post_no_memory(resource);
        return;
     }

   wl_resource_set_implementation(popup_resource,
                                  &_e_xdg_popup_interface,
                                  exsurf,
                                  _e_xdg_popup_cb_resource_destroy);


   if (!_e_xdg_surface_role_assign(exsurf, popup_resource, E_XDG_SURFACE_ROLE_POPUP))
     {
        ERR("Failed to assign role to surface", exsurf->ec);
        wl_resource_destroy(popup_resource);
        return;
     }

   _e_xdg_popup_parent_set((E_Xdg_Popup *)exsurf, parent);
   _e_xdg_popup_positioner_apply((E_Xdg_Popup *)exsurf, p);

   e_object_ref(E_OBJECT(exsurf));
}

static void
_e_xdg_surface_cb_win_geometry_set(struct wl_client *client,
                                   struct wl_resource *resource,
                                   int32_t x,
                                   int32_t y,
                                   int32_t w,
                                   int32_t h)
{
   E_Xdg_Surface *exsurf;

   exsurf = wl_resource_get_user_data(resource);
   if (!exsurf)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No E_Xdg_Surface data in wl_resource");
        return;
     }

   LOG("Set window geometry: geometry(%d %d %d %d)",
       exsurf->ec, x, y, w, h);

   exsurf->has_window_geometry = EINA_TRUE;
   EINA_RECTANGLE_SET(&exsurf->window_geometry, x, y, w, h);

   exsurf->wait_next_commit = EINA_TRUE;
}

static void
_e_xdg_surface_cb_configure_ack(struct wl_client *client, struct wl_resource *resource, uint32_t serial)
{
   E_Xdg_Surface *exsurf;
   E_Xdg_Surface_Configure *configure;
   Eina_List *l, *ll;
   Eina_Bool found = EINA_FALSE;

   exsurf = wl_resource_get_user_data(resource);
   if (!exsurf)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No E_Xdg_Surface data in wl_surface");
        return;
     }

   LOG("Ack configure", exsurf->ec);

   if ((exsurf->role != E_XDG_SURFACE_ROLE_TOPLEVEL) &&
       (exsurf->role != E_XDG_SURFACE_ROLE_POPUP))
     {
        wl_resource_post_error(resource,
                               ZXDG_SURFACE_V6_ERROR_NOT_CONSTRUCTED,
                               "xdg_surface must have a role");
        return;
     }

   EINA_LIST_FOREACH_SAFE(exsurf->configure_list, l, ll, configure)
     {
        if (configure->serial < serial)
          {
             exsurf->configure_list =
                eina_list_remove_list(exsurf->configure_list, l);
             free(configure);
          }
        else if (configure->serial == serial)
          {
             exsurf->configure_list =
                eina_list_remove_list(exsurf->configure_list, l);
             found = EINA_TRUE;
             break;
          }
        else
          break;
     }

   LOG("Ack configure %s first %d serial %d found %d",
       exsurf->ec,
       _e_xdg_surface_util_role_string_get(exsurf),
       !exsurf->configured,
       serial, found);

   if (!found)
     {
        wl_resource_post_error(exsurf->shell->resource,
                               ZXDG_SHELL_V6_ERROR_INVALID_SURFACE_STATE,
                               "Wrong configure serial: %u", serial);
        return;
     }

   exsurf->configured = EINA_TRUE;

   switch (exsurf->role)
     {
      case E_XDG_SURFACE_ROLE_NONE:
         ERR("Cannot reach here", exsurf->ec);
         break;
      case E_XDG_SURFACE_ROLE_TOPLEVEL:
         _e_xdg_toplevel_configure_ack((E_Xdg_Toplevel *)exsurf, configure);
         break;
      case E_XDG_SURFACE_ROLE_POPUP:
         break;
     }

   E_FREE_FUNC(configure, free);
}

static const struct zxdg_surface_v6_interface _e_xdg_surface_interface =
{
   _e_xdg_surface_cb_destroy,
   _e_xdg_surface_cb_toplevel_get,
   _e_xdg_surface_cb_popup_get,
   _e_xdg_surface_cb_win_geometry_set,
   _e_xdg_surface_cb_configure_ack
};

static Eina_Bool
_e_xdg_surface_cb_commit(void *data, int type, void *event)
{
   E_Xdg_Surface *exsurf;
   E_Event_Client *ev;

   exsurf = (E_Xdg_Surface *)data;
   ev = (E_Event_Client *)event;

   if (exsurf->ec != ev->ec)
     goto end;

   if (!exsurf->wait_next_commit)
     goto end;

   LOG("Wl_Surface Commit, Update Xdg_Surface state %s",
       exsurf->ec,
       _e_xdg_surface_util_role_string_get(exsurf));

   exsurf->wait_next_commit = EINA_FALSE;

   if (exsurf->has_window_geometry)
     {
        exsurf->has_window_geometry = EINA_FALSE;
        EINA_RECTANGLE_SET(&exsurf->ec->comp_data->shell.window,
                           exsurf->window_geometry.x,
                           exsurf->window_geometry.y,
                           exsurf->window_geometry.w,
                           exsurf->window_geometry.h);
     }

   switch (exsurf->role)
     {
      case E_XDG_SURFACE_ROLE_NONE:
         wl_resource_post_error(exsurf->resource,
                                ZXDG_SURFACE_V6_ERROR_NOT_CONSTRUCTED,
                                "xdg_surface must have a role");
         break;
      case E_XDG_SURFACE_ROLE_TOPLEVEL:
         _e_xdg_toplevel_committed((E_Xdg_Toplevel *)exsurf);
         break;
      case E_XDG_SURFACE_ROLE_POPUP:
         _e_xdg_popup_committed((E_Xdg_Popup *)exsurf);
         break;
     }

end:
   return ECORE_CALLBACK_PASS_ON;
}

static void
_e_xdg_surface_free(E_Xdg_Surface *exsurf)
{
   free(exsurf);
}

static void
_e_xdg_surface_del(E_Xdg_Surface *exsurf)
{
   _e_xdg_shell_surface_remove(exsurf->shell, exsurf);

   E_FREE_LIST(exsurf->configure_list, free);
   if (exsurf->configure_idle)
     ecore_idle_enterer_del(exsurf->configure_idle);
   if (exsurf->commit_handler)
     ecore_event_handler_del(exsurf->commit_handler);
}

static void
_e_xdg_surface_cb_resource_destroy(struct wl_resource *resource)
{
   E_Xdg_Surface *exsurf;

   exsurf = wl_resource_get_user_data(resource);
   if (!exsurf)
     {
        ERR("No E_Xdg_Surface data in wl_resource", NULL);
        return;

     }

   LOG("Destroy resource of Xdg_Surface %s",
       exsurf->ec,
       _e_xdg_surface_util_role_string_get(exsurf));

   /* Although zxdg_toplevel_v6 or zxdg_popup_v6 are still existed, unset
    * assignment at here anyway. once zxdg_surface_v6 is destroyed, the
    * attribute 'toplevel and popup' is no longer meaningful. */
   _e_client_xdg_shell_v6_role_assingment_unset(exsurf->ec);

   e_shell_e_client_destroy(exsurf->ec);
   /* set null after destroying shell of e_client, ec will be freed */
   exsurf->ec = NULL;

   e_object_del(E_OBJECT(exsurf));
}

static E_Xdg_Surface *
_e_xdg_surface_create(E_Xdg_Shell *shell,
                      struct wl_resource *wsurface,
                      uint32_t id)
{
   E_Xdg_Surface *exsurf;
   E_Client *ec;

   ec = wl_resource_get_user_data(wsurface);
   if (!ec)
     {
        ERR("No E_Client data in wl_resource", NULL);
        wl_resource_post_error(wsurface,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No data in wl_resource");
        return NULL;
     }

   LOG("Create Xdg_Surface", ec);

   if (!_e_client_shsurface_assignable_check(ec))
     {
        ERR("Cannot get xdg_surface with this wl_surface", ec);
        return NULL;
     }

   exsurf = E_OBJECT_ALLOC(e_xdg_surface_role_biggest_struct,
                           E_XDG_SURFACE_V6_TYPE,
                           _e_xdg_surface_free);
   if (!exsurf)
     {
        wl_client_post_no_memory(shell->wclient);
        return NULL;
     }
   e_object_del_func_set(E_OBJECT(exsurf), E_OBJECT_CLEANUP_FUNC(_e_xdg_surface_del));

   exsurf->resource = wl_resource_create(shell->wclient,
                                         &zxdg_surface_v6_interface,
                                         1,
                                         id);
   if (!exsurf->resource)
     {
        ERR("Could not create wl_resource for xdg surface", ec);
        wl_client_post_no_memory(shell->wclient);
        e_object_del(E_OBJECT(exsurf));
        return NULL;
     }

   wl_resource_set_implementation(exsurf->resource,
                                  &_e_xdg_surface_interface,
                                  exsurf,
                                  _e_xdg_surface_cb_resource_destroy);

   e_shell_e_client_shsurface_assign(ec, exsurf->resource, NULL);

   exsurf->shell = shell;
   exsurf->ec = ec;
   exsurf->configured = EINA_FALSE;
   exsurf->commit_handler =
      ecore_event_handler_add(E_EVENT_CLIENT_BUFFER_CHANGE,
                              _e_xdg_surface_cb_commit,
                              exsurf);

   _e_xdg_shell_surface_add(shell, exsurf);

   return exsurf;
}
/* End of Xdg_surface */

/**********************************************************
 * Implementation for Xdg_Shell
 **********************************************************/
static void
_e_xdg_shell_surface_add(E_Xdg_Shell *shell, E_Xdg_Surface *exsurf)
{
   if (!shell) return;
   shell->surfaces = eina_list_append(shell->surfaces, exsurf);
}

static void
_e_xdg_shell_surface_remove(E_Xdg_Shell *shell, E_Xdg_Surface *exsurf)
{
   if (!shell) return;
   shell->surfaces = eina_list_remove(shell->surfaces, exsurf);
}

static void
_e_xdg_shell_ping(E_Xdg_Shell *shell)
{
   EINA_SAFETY_ON_NULL_RETURN(shell);
   EINA_SAFETY_ON_NULL_RETURN(shell->resource);

   if (shell->ping_serial != 0)
     return;

   shell->ping_serial = wl_display_next_serial(e_comp_wl->wl.disp);
   zxdg_shell_v6_send_ping(shell->resource, shell->ping_serial);
}

static void
_e_xdg_shell_cb_destroy(struct wl_client *client, struct wl_resource *resource)
{
   LOG("Destroy Xdg_Shell", NULL);

   wl_resource_destroy(resource);
}

static void
_e_xdg_shell_cb_positioner_create(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
   E_Xdg_Shell *shell;
   E_Xdg_Positioner *p;
   struct wl_resource *new_res;

   LOG("Create Positioner", NULL);

   shell = wl_resource_get_user_data(resource);
   if (!shell)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No resource for xdg_shell_v6");
        return;
     }

   new_res = wl_resource_create(client, &zxdg_positioner_v6_interface, 1, id);
   if (!new_res)
     {
        wl_resource_post_no_memory(resource);
        return;
     }

   p = E_NEW(E_Xdg_Positioner, 1);
   if (!p)
     {
        wl_resource_destroy(new_res);
        wl_resource_post_no_memory(resource);
        return;
     }
   p->shell = shell;
   p->resource = new_res;

   shell->positioners = eina_list_append(shell->positioners, p);

   wl_resource_set_implementation(new_res,
                                  &_e_xdg_positioner_interface,
                                  p,
                                  _e_xdg_positioner_cb_resource_destroy);
}

static void
_e_xdg_shell_cb_surface_get(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *wsurface)
{
   E_Xdg_Shell *shell;
   E_Xdg_Surface *exsurf;

   shell = wl_resource_get_user_data(resource);
   if (!shell)
     {
        ERR("No E_Xdg_Shell data in wl_resource", NULL);
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No shell data in wl_resource");
        return;
     }

   exsurf = _e_xdg_surface_create(shell, wsurface, id);
   if (!exsurf)
     {
        ERR("Failed to create E_Xdg_Surface", NULL);
        return;
     }
}

static void
_e_xdg_shell_cb_pong(struct wl_client *client, struct wl_resource *resource, uint32_t serial)
{
   E_Xdg_Shell *shell;
   E_Xdg_Surface *exsurf;
   Eina_List *l;

   LOG("Pong", NULL);

   shell = wl_resource_get_user_data(resource);
   if (!shell)
     {
        ERR("No E_Xdg_Shell data in wl_resource", NULL);
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No E_Xdg_Shell data in wl_resource");
        return;
     }

   EINA_LIST_FOREACH(shell->surfaces, l, exsurf)
      e_shell_e_client_pong(exsurf->ec);

   shell->ping_serial = 0;
}

static const struct zxdg_shell_v6_interface _e_xdg_shell_interface =
{
   _e_xdg_shell_cb_destroy,
   _e_xdg_shell_cb_positioner_create,
   _e_xdg_shell_cb_surface_get,
   _e_xdg_shell_cb_pong
};

static E_Xdg_Shell *
_e_xdg_shell_create(struct wl_client *client, struct wl_resource *resource)
{
   E_Xdg_Shell *shell;

   shell = E_NEW(E_Xdg_Shell, 1);
   if (!shell)
     return NULL;

   shell->wclient = client;
   shell->resource = resource;

   return shell;
}

static void
_e_xdg_shell_destroy(E_Xdg_Shell *shell)
{
   E_Xdg_Surface *exsurf;
   E_Xdg_Positioner *p;

   EINA_LIST_FREE(shell->surfaces, exsurf)
     {
        /* Do we need to do it even though shell is just about to be destroyed? */
        e_shell_e_client_pong(exsurf->ec);
        exsurf->shell = NULL;
     }

   EINA_LIST_FREE(shell->positioners, p)
      p->shell = NULL;

   free(shell);
}

static void
_e_xdg_shell_cb_unbind(struct wl_resource *resource)
{
   E_Xdg_Shell *shell;

   LOG("Unbind Xdg_Shell", NULL);

   shell = wl_resource_get_user_data(resource);
   if (!shell)
     {
        ERR("No E_Xdg_Shell in wl_resource", NULL);
        return;
     }

   _e_xdg_shell_destroy(shell);
}

static void
_e_xdg_shell_cb_bind(struct wl_client *client, void *data EINA_UNUSED, uint32_t version, uint32_t id)
{
   E_Xdg_Shell *shell;
   struct wl_resource *resource;

   LOG("Bind Xdg_Shell", NULL);

   /* Create resource for zxdg_shell_v6 */
   resource = wl_resource_create(client,
                                 &zxdg_shell_v6_interface,
                                 version,
                                 id);
   if (!resource)
     goto err_res;


   shell = _e_xdg_shell_create(client, resource);
   if (!shell)
     {
        ERR("Failed to create E_Xdg_Shell", NULL);
        goto err_shell;
     }

   wl_resource_set_implementation(resource, &_e_xdg_shell_interface,
                                  shell, _e_xdg_shell_cb_unbind);

   return;
err_shell:
   wl_resource_destroy(resource);
err_res:
   wl_client_post_no_memory(client);
}

EINTERN Eina_Bool
e_xdg_shell_v6_init(void)
{
   LOG("Initializing Xdg_Shell_V6", NULL);

   /* try to create global xdg_shell interface */
   global_resource = wl_global_create(e_comp_wl->wl.disp,
                                      &zxdg_shell_v6_interface,
                                      1,
                                      e_comp->wl_comp_data,
                                      _e_xdg_shell_cb_bind);
   if (!global_resource)
     {
        ERR("Could not create zxdg_shell_v6 global: %m", NULL);
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

EINTERN void
e_xdg_shell_v6_shutdown(void)
{
   E_FREE_FUNC(global_resource, wl_global_destroy);
}
/* End of Xdg_shell */
