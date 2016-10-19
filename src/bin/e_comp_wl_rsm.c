#include "e.h"
#ifdef HAVE_REMOTE_SURFACE
 #include <tizen-remote-surface-server-protocol.h>
#endif /* HAVE_REMOTE_SURFACE */

#define RSMINF(f, cp, ec, obj, ptr, x...)                            \
   do                                                                \
     {                                                               \
        if ((!cp) && (!ec))                                          \
          INF("EWL|%20.20s|              |             |%10.10s|%p|"f,\
              "RSM", (obj), (ptr), ##x);                             \
        else                                                         \
          INF("EWL|%20.20s|win:0x%08x|ec:0x%08x|%10.10s|%p|"f,       \
              "RSM",                                                 \
              (unsigned int)(cp ? e_pixmap_window_get(cp) : 0),      \
              (unsigned int)(ec),                                    \
              (obj), (ptr),                                          \
              ##x);                                                  \
     }                                                               \
   while (0)

#define RSMDBG(f, cp, ec, obj, ptr, x...)                            \
   do                                                                \
     {                                                               \
        if ((!cp) && (!ec))                                          \
          DBG("EWL|%20.20s|              |             |%10.10s|%p|"f,\
              "RSM", (obj), (ptr), ##x);                             \
        else                                                         \
          DBG("EWL|%20.20s|win:0x%08x|ec:0x%08x|%10.10s|%p|"f,       \
              "RSM",                                                 \
              (unsigned int)(cp ? e_pixmap_window_get(cp) : 0),      \
              (unsigned int)(ec),                                    \
              (obj), (ptr),                                          \
              ##x);                                                  \
     }                                                               \
   while (0)

E_API int E_EVENT_REMOTE_SURFACE_PROVIDER_VISIBILITY_CHANGE = -1;

#ifdef HAVE_REMOTE_SURFACE
typedef struct _E_Comp_Wl_Remote_Manager E_Comp_Wl_Remote_Manager;
typedef struct _E_Comp_Wl_Remote_Provider E_Comp_Wl_Remote_Provider;
typedef struct _E_Comp_Wl_Remote_Surface E_Comp_Wl_Remote_Surface;
typedef struct _E_Comp_Wl_Remote_Region E_Comp_Wl_Remote_Region;

struct _E_Comp_Wl_Remote_Manager{
   struct wl_global *global;

   Eina_Hash *provider_hash;
   Eina_Hash *surface_hash;
   Eina_List *event_hdlrs;
};

struct _E_Comp_Wl_Remote_Provider
{
     struct wl_resource *resource;

     E_Client *ec;

     Eina_List *surfaces;
     E_Comp_Wl_Remote_Surface *onscreen_parent;

     Eina_Bool visible;
     int vis_ref;

     Eina_Bool is_offscreen;
};

struct _E_Comp_Wl_Remote_Surface{
     struct wl_resource *resource;
     struct wl_resource *wl_tbm;

     E_Comp_Wl_Remote_Provider *provider;
     E_Client *bind_ec;

     E_Client *owner;
     Eina_List *regions;

     Eina_Bool redirect;
     Eina_Bool visible;
};

struct _E_Comp_Wl_Remote_Region {
     struct wl_resource *resource;

     E_Comp_Wl_Remote_Surface *remote_surface;
     Eina_Rectangle geometry;
};

static E_Comp_Wl_Remote_Manager *_rsm = NULL;

static void _e_comp_wl_remote_surface_state_buffer_set(E_Comp_Wl_Surface_State *state, E_Comp_Wl_Buffer *buffer);

static Ecore_Device *
_device_get_by_identifier(const char *identifier)
{
   Ecore_Device *dev = NULL;
   const Eina_List *devices, *l;

   devices = ecore_device_list();
   EINA_LIST_FOREACH(devices, l, dev)
     {
        if (!e_util_strcmp(identifier, ecore_device_identifier_get(dev)))
          return dev;
     }

   return NULL;
}

void
_remote_provider_rect_add(E_Comp_Wl_Remote_Provider *provider, Eina_Rectangle *rect)
{
   E_Client *ec;

   ec = provider->ec;
   if (!ec) return;
   if (!ec->comp_data) return;

   ec->comp_data->remote_surface.regions =
      eina_list_remove(ec->comp_data->remote_surface.regions,
                       rect);
   ec->comp_data->remote_surface.regions =
      eina_list_append(ec->comp_data->remote_surface.regions,
                       rect);
}

void
_remote_provider_rect_del(E_Comp_Wl_Remote_Provider *provider, Eina_Rectangle *rect)
{
   E_Client *ec;

   ec = provider->ec;
   if (!ec) return;
   if (!ec->comp_data) return;

   ec->comp_data->remote_surface.regions =
      eina_list_remove(ec->comp_data->remote_surface.regions,
                       rect);
}

void
_remote_provider_rect_clear(E_Comp_Wl_Remote_Provider *provider)
{
   E_Client *ec;

   ec = provider->ec;
   if (!ec) return;
   if (!ec->comp_data) return;

   ec->comp_data->remote_surface.regions =
      eina_list_remove_list(ec->comp_data->remote_surface.regions,
                            ec->comp_data->remote_surface.regions);
}

void
_remote_provider_onscreen_parent_set(E_Comp_Wl_Remote_Provider *provider, E_Comp_Wl_Remote_Surface *parent)
{
   E_Comp_Wl_Remote_Region *region;
   Eina_List *l;

   if (!provider) return;
   if ((parent) && !(parent->owner)) return;
   if (provider->onscreen_parent == parent) return;

   _remote_provider_rect_clear(provider);

   provider->onscreen_parent = parent;
   provider->ec->comp_data->remote_surface.onscreen_parent = NULL;

   RSMDBG("set onscreen_parent %p(ec:%p)",
          provider->ec->pixmap, provider->ec,
          "PROVIDER", provider,
          parent, parent? parent->owner:NULL);

   if (parent)
     {
        EINA_LIST_FOREACH(provider->onscreen_parent->regions, l, region)
          {
             _remote_provider_rect_add(provider, &region->geometry);
          }

        provider->ec->comp_data->remote_surface.onscreen_parent = parent->owner;
     }
}

void
_remote_provider_onscreen_parent_calculate(E_Comp_Wl_Remote_Provider *provider)
{
   Evas_Object *o;
   E_Client *ec, *_ec, *parent = NULL;
   E_Comp_Wl_Remote_Surface *surface;
   E_Comp_Wl_Client_Data *cdata;

   ec = provider->ec;
   if (!ec) return;
   if (!ec->comp_data) return;
   if (!provider->surfaces) return;

   o = evas_object_top_get(e_comp->evas);
   for (; o; o = evas_object_below_get(o))
     {
        _ec = evas_object_data_get(o, "E_Client");
        if (!_ec) continue;
        if (_ec == ec) continue;

        if ((surface = eina_hash_find(_rsm->surface_hash, &_ec)))
          {
             if (surface->provider != provider) continue;
             if (!surface->visible) continue;

             if (e_object_is_del(E_OBJECT(_ec))) continue;
             if (e_client_util_ignored_get(_ec)) continue;
             if (_ec->zone != ec->zone) continue;
             if (!_ec->frame) continue;
             if (!_ec->visible) continue;
             if (_ec->visibility.skip) continue;
             if ((_ec->visibility.obscured != E_VISIBILITY_UNOBSCURED) &&
                 (_ec->visibility.obscured != E_VISIBILITY_PARTIALLY_OBSCURED))
               continue;

             /* if _ec is subsurface, skip this */
             cdata = (E_Comp_Wl_Client_Data *)_ec->comp_data;
             if (cdata && cdata->sub.data) continue;

             if (!E_INTERSECTS(_ec->x, _ec->y, _ec->w, _ec->h, ec->zone->x, ec->zone->y, ec->zone->w, ec->zone->h))
               continue;

             parent = _ec;
             break;
          }
     }

   surface = NULL;
   if (parent)
     surface = eina_hash_find(_rsm->surface_hash, &parent);

   _remote_provider_onscreen_parent_set(provider, surface);
}

static void
_remote_provider_offscreen_set(E_Comp_Wl_Remote_Provider* provider, Eina_Bool set)
{
   E_Client *ec;

   EINA_SAFETY_ON_NULL_RETURN(provider);
   EINA_SAFETY_ON_NULL_RETURN(provider->ec);

   ec = provider->ec;

   if (e_object_is_del(E_OBJECT(ec))) return;

   if (set)
     {
        provider->is_offscreen = set;
        ec->ignored = EINA_TRUE;

        //TODO: consider what happens if it's not normal client such as subsurface client
        //TODO: save original values
        if ((ec->comp_data->shell.surface) && (ec->comp_data->shell.unmap))
          ec->comp_data->shell.unmap(ec->comp_data->shell.surface);
        else
          {
             ec->visible = EINA_FALSE;
             evas_object_hide(ec->frame);
             ec->comp_data->mapped = 0;
          }

        ec->icccm.accepts_focus = ec->icccm.take_focus = ec->want_focus = EINA_FALSE;
        ec->placed = EINA_TRUE;
        e_client_visibility_skip_set(ec, EINA_TRUE);

        _remote_provider_onscreen_parent_calculate(provider);
     }
   else
     {
        e_client_visibility_skip_set(ec, EINA_FALSE);
        provider->is_offscreen = set;
        ec->icccm.accepts_focus = ec->icccm.take_focus = ec->want_focus = EINA_TRUE;
        ec->placed = EINA_FALSE;

        _remote_provider_onscreen_parent_set(provider, NULL);

        e_comp_wl_surface_commit(ec);
     }

   RSMINF("%s offscreen",
          ec->pixmap, ec,
          "PROVIDER", provider, set? "Set":"Unset");
}

static void
_remote_provider_visible_event_free(void *data EINA_UNUSED, E_Event_Remote_Surface_Provider *ev)
{
   e_object_unref(E_OBJECT(ev->ec));
   free(ev);
}

static void
_remote_provider_visible_event_send(E_Comp_Wl_Remote_Provider *provider)
{
   E_Event_Remote_Surface_Provider *ev;

   if (e_object_is_del(E_OBJECT(provider->ec))) return;

   ev = E_NEW(E_Event_Remote_Surface_Provider, 1);
   if (!ev) return;

   ev->ec = provider->ec;
   e_object_ref(E_OBJECT(provider->ec));
   ecore_event_add(E_EVENT_REMOTE_SURFACE_PROVIDER_VISIBILITY_CHANGE, ev, (Ecore_End_Cb)_remote_provider_visible_event_free, NULL);
}

static void
_remote_provider_visible_set(E_Comp_Wl_Remote_Provider *provider, Eina_Bool set)
{
   EINA_SAFETY_ON_NULL_RETURN(provider);

   if (set)
     {
        provider->vis_ref ++;
        RSMDBG("Count up vis_ref:%d",
               provider->ec->pixmap, provider->ec,
               "PROVIDER", provider, provider->vis_ref);

        if (provider->vis_ref == 1)
          {
             provider->ec->visibility.obscured = E_VISIBILITY_UNOBSCURED;

             _remote_provider_visible_event_send(provider);
             e_policy_client_visibility_send(provider->ec);

             tizen_remote_surface_provider_send_visibility
                (provider->resource,
                 TIZEN_REMOTE_SURFACE_PROVIDER_VISIBILITY_TYPE_VISIBLE);
          }
     }
   else
     {
        provider->vis_ref --;
        RSMDBG("Count down vis_ref:%d",
               provider->ec->pixmap, provider->ec,
               "PROVIDER", provider, provider->vis_ref);

        if (provider->vis_ref == 0)
          {
             provider->ec->visibility.obscured = E_VISIBILITY_FULLY_OBSCURED;

             _remote_provider_visible_event_send(provider);
             e_policy_client_visibility_send(provider->ec);

             tizen_remote_surface_provider_send_visibility
                (provider->resource,
                 TIZEN_REMOTE_SURFACE_PROVIDER_VISIBILITY_TYPE_INVISIBLE);
          }
     }

   _remote_provider_onscreen_parent_calculate(provider);
}

static E_Comp_Wl_Remote_Provider *
_remote_provider_find(E_Client *ec)
{
   E_Comp_Wl_Remote_Provider *provider;

   EINA_SAFETY_ON_NULL_RETURN_VAL(_rsm, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(_rsm->provider_hash, NULL);

   provider = eina_hash_find(_rsm->provider_hash, &ec);
   return provider;
}

static void
_remote_surface_visible_set(E_Comp_Wl_Remote_Surface *remote_surface, Eina_Bool set)
{
   E_Comp_Wl_Remote_Provider *provider;

   if (remote_surface->visible == set) return;

   remote_surface->visible = set;

   RSMDBG("Switch visible:%d",
          NULL, NULL,
          "SURFACE", remote_surface, remote_surface->visible);

   provider = remote_surface->provider;
   if (!provider) return;

   _remote_provider_visible_set(provider, set);
}

static void
_remote_surface_bind_client(E_Comp_Wl_Remote_Surface *remote_surface, E_Client *ec)
{
   if (!remote_surface) return;
   if ((ec) && (remote_surface->bind_ec == ec)) return;

   /* clear previous binding */
   if (remote_surface->bind_ec)
     {
        RSMINF("Clear previous bind_ec:%p",
               NULL, NULL,
               "SURFACE", remote_surface, remote_surface->bind_ec);

        /* do NULL buffer commit for binded ec */
        _e_comp_wl_remote_surface_state_buffer_set(&remote_surface->bind_ec->comp_data->pending, NULL);

        remote_surface->bind_ec->comp_data->pending.sx = 0;
        remote_surface->bind_ec->comp_data->pending.sy = 0;
        remote_surface->bind_ec->comp_data->pending.new_attach = EINA_TRUE;

        e_comp_wl_surface_commit(remote_surface->bind_ec);

        remote_surface->bind_ec = NULL;
     }

   if (ec)
     {
        if (e_object_is_del(E_OBJECT(ec)))
          {
             ERR("Trying to bind with deleted EC(%p)", ec);
             return;
          }

        RSMINF("Set bind_ec:%p",
               NULL, NULL,
               "SURFACE", remote_surface, ec);

        /* TODO: enable user geometry? */
        e_policy_allow_user_geometry_set(ec, EINA_TRUE);
        remote_surface->bind_ec = ec;
     }
}

static void
_remote_region_cb_resource_destroy(struct wl_resource *resource)
{
   E_Comp_Wl_Remote_Region *region;

   region = wl_resource_get_user_data(resource);
   if (!region) return;

   if (region->remote_surface)
     {
        if (region->remote_surface->provider)
          {
             _remote_provider_rect_del(region->remote_surface->provider,
                                       &region->geometry);
          }
        region->remote_surface->regions = eina_list_remove(region->remote_surface->regions,
                                                           region);
     }

   E_FREE(region);
}

static void
_remote_region_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void
_remote_region_cb_geometry_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, int32_t x, int32_t y, int32_t w, int32_t h)
{
   E_Comp_Wl_Remote_Region *region;

   region = wl_resource_get_user_data(resource);
   if (!region) return;

   region->geometry.x = x;
   region->geometry.y = y;
   region->geometry.w = w;
   region->geometry.h = h;

   RSMDBG("Region %p geometry set (%d, %d) %dx%d",
          NULL, NULL,
          "SURFACE", region->remote_surface, region, x, y, w, h);
}

static const struct tizen_remote_surface_region_interface _remote_region_interface =
{
   _remote_region_cb_destroy,
   _remote_region_cb_geometry_set,
};

static void
_remote_provider_cb_resource_destroy(struct wl_resource *resource)
{
   E_Comp_Wl_Remote_Provider *provider;
   E_Comp_Wl_Remote_Surface *remote_surface;

   provider = wl_resource_get_user_data(resource);
   if (!provider) return;

   if (_rsm)
     eina_hash_del(_rsm->provider_hash, &provider->ec, provider);

   EINA_LIST_FREE(provider->surfaces, remote_surface)
     {
        if (remote_surface->provider == provider)
          {
             /* unset remote buffer from provider */
             if (remote_surface->bind_ec)
               _remote_surface_bind_client(remote_surface, NULL);

             remote_surface->provider = NULL;
             //notify of this ejection to remote surface_resource
             tizen_remote_surface_send_missing(remote_surface->resource);
          }
     }
   _remote_provider_offscreen_set(provider, EINA_FALSE);
   E_FREE(provider);
}

static void
_remote_provider_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void
_remote_provider_cb_offscreen_set(struct wl_client *client EINA_UNUSED, struct wl_resource *provider_resource, uint32_t offscreen)
{
   E_Comp_Wl_Remote_Provider *provider;

   provider = wl_resource_get_user_data(provider_resource);
   if (!provider) return;

   if (provider->is_offscreen == offscreen) return;
   _remote_provider_offscreen_set(provider, offscreen);
}

static const struct tizen_remote_surface_provider_interface _remote_provider_interface =
{
   _remote_provider_cb_destroy,
   _remote_provider_cb_offscreen_set,
};

static void
_remote_surface_cb_resource_destroy(struct wl_resource *resource)
{
   E_Comp_Wl_Remote_Surface *remote_surface;
   E_Comp_Wl_Remote_Provider *provider;
   E_Comp_Wl_Remote_Region *region;

   remote_surface = wl_resource_get_user_data(resource);
   if (!remote_surface) return;

   provider = remote_surface->provider;
   if (provider)
     {
        _remote_surface_visible_set(remote_surface, EINA_FALSE);
        if (provider->onscreen_parent == remote_surface)
          _remote_provider_onscreen_parent_set(provider, NULL);

        provider->surfaces = eina_list_remove(provider->surfaces,
                                              remote_surface);
        remote_surface->provider = NULL;
     }

   EINA_LIST_FREE(remote_surface->regions, region)
     {
        region->remote_surface = NULL;
        wl_resource_destroy(region->resource);
     }

   if (remote_surface->bind_ec)
     _remote_surface_bind_client(remote_surface, NULL);
   if (remote_surface->owner)
     eina_hash_del_by_key(_rsm->surface_hash, &remote_surface->owner);

   E_FREE(remote_surface);
}

static void
_remote_surface_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void
_remote_surface_cb_redirect(struct wl_client *client, struct wl_resource *resource)
{
   E_Comp_Wl_Buffer *buffer;
   E_Comp_Wl_Remote_Surface *remote_surface;
   struct wl_resource *remote_buffer;

   remote_surface = wl_resource_get_user_data(resource);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface->provider);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface->provider->ec);

   RSMINF("Redirect surface provider:%p(ec:%p)",
          NULL, NULL,
          "SURFACE", remote_surface,
          remote_surface->provider, remote_surface->provider->ec);

   remote_surface->redirect = EINA_TRUE;

   buffer = e_pixmap_resource_get(remote_surface->provider->ec->pixmap);
   EINA_SAFETY_ON_NULL_RETURN(buffer);

   remote_buffer = e_comp_wl_tbm_remote_buffer_get(remote_surface->wl_tbm, buffer->resource);
   EINA_SAFETY_ON_NULL_RETURN(remote_buffer);

   tizen_remote_surface_send_update_buffer(resource,
                                           remote_buffer,
                                           ecore_time_get());
}

static void
_remote_surface_cb_unredirect(struct wl_client *client, struct wl_resource *resource)
{
   E_Comp_Wl_Remote_Surface *remote_surface;

   remote_surface = wl_resource_get_user_data(resource);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface);

   remote_surface->redirect = EINA_FALSE;
//   _remote_surface_visible_set(remote_surface, EINA_FALSE);

   RSMINF("Unredirect surface provider:%p(ec:%p)",
          NULL, NULL,
          "SURFACE", remote_surface,
          remote_surface->provider, remote_surface->provider? remote_surface->provider->ec: NULL);
}

static void
_remote_surface_cb_mouse_event_transfer(struct wl_client *client, struct wl_resource *resource, uint32_t event_type, int32_t device, int32_t button, int32_t x, int32_t y, wl_fixed_t radius_x, wl_fixed_t radius_y, wl_fixed_t pressure, wl_fixed_t angle, uint32_t clas, uint32_t subclas EINA_UNUSED, const char *identifier, uint32_t time)
{
   E_Comp_Wl_Remote_Provider *provider;
   E_Comp_Wl_Remote_Surface *remote_surface;
   E_Client *ec;

   Ecore_Device *edev = NULL;
   Ecore_Device_Class eclas = ECORE_DEVICE_CLASS_NONE;
   double eradx, erady, epressure, eangle;

   remote_surface = wl_resource_get_user_data(resource);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface->provider);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface->provider->ec);

   provider = remote_surface->provider;
   ec = provider->ec;

   if (e_object_is_del(E_OBJECT(ec))) return;

   /* identify class */
   if (clas == TIZEN_INPUT_DEVICE_CLAS_MOUSE)
     eclas = ECORE_DEVICE_CLASS_MOUSE;
   else if (clas == TIZEN_INPUT_DEVICE_CLAS_TOUCHSCREEN)
     eclas = ECORE_DEVICE_CLASS_TOUCH;
   else
     {
        ERR("Not supported device clas(%d) subclas(%d) identifier(%s)",
            clas, subclas, identifier);
     }

   /* find ecore device*/
   edev = _device_get_by_identifier(identifier);
   if (edev)
     {
        eclas = ecore_device_class_get(edev);
     }

   /* fixed to */
   eradx = wl_fixed_to_double(radius_x);
   erady = wl_fixed_to_double(radius_y);
   epressure = wl_fixed_to_double(pressure);
   eangle = wl_fixed_to_double(angle);

   if (remote_surface->visible)
     {
        if (eclas == ECORE_DEVICE_CLASS_MOUSE)
          {
             switch (event_type)
               {
                case TIZEN_REMOTE_SURFACE_EVENT_TYPE_MOUSE_DOWN:
                   e_client_mouse_button_send(ec,
                                              button,
                                              EINA_TRUE,
                                              edev,
                                              time);
                   break;
                case TIZEN_REMOTE_SURFACE_EVENT_TYPE_MOUSE_UP:
                   e_client_mouse_button_send(ec,
                                              button,
                                              EINA_FALSE,
                                              edev,
                                              time);
                   break;
                case TIZEN_REMOTE_SURFACE_EVENT_TYPE_MOUSE_MOVE:
                   e_client_mouse_move_send(ec,
                                            x, y,
                                            edev,
                                            time);
                   break;
                default:
                   ERR("Not supported event_type(%d)", event_type);
                   break;
               }
          }
        else if (eclas == ECORE_DEVICE_CLASS_TOUCH)
          {
             switch (event_type)
               {
                case TIZEN_REMOTE_SURFACE_EVENT_TYPE_MOUSE_DOWN:
                   /* FIXME: temporary fix for first touch down w/o move event */
                   e_client_touch_update_send(ec,
                                              device,
                                              x, y,
                                              edev,
                                              eradx, erady, epressure, eangle,
                                              time);
                   e_client_touch_send(ec,
                                       device,
                                       x, y,
                                       EINA_TRUE,
                                       edev,
                                       eradx, erady, epressure, eangle,
                                       time);
                   break;
                case TIZEN_REMOTE_SURFACE_EVENT_TYPE_MOUSE_UP:
                   e_client_touch_send(ec,
                                       device,
                                       x, y,
                                       EINA_FALSE,
                                       edev,
                                       eradx, erady, epressure, eangle,
                                       time);
                   break;
                case TIZEN_REMOTE_SURFACE_EVENT_TYPE_MOUSE_MOVE:
                   e_client_touch_update_send(ec,
                                              device,
                                              x, y,
                                              edev,
                                              eradx, erady, epressure, eangle,
                                              time);
                   break;
                default:
                   ERR("Not supported event_type(%d)", event_type);
                   break;
               }
          }
     }
}

static void
_remote_surface_cb_mouse_wheel_transfer(struct wl_client *client, struct wl_resource *resource, uint32_t direction, int32_t z, uint32_t clas, uint32_t subclas, const char *identifier, uint32_t time)
{
   E_Comp_Wl_Remote_Provider *provider;
   E_Comp_Wl_Remote_Surface *remote_surface;
   E_Client *ec;

   Ecore_Device *edev = NULL;

   remote_surface = wl_resource_get_user_data(resource);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface->provider);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface->provider->ec);

   provider = remote_surface->provider;
   ec = provider->ec;

   if (e_object_is_del(E_OBJECT(ec))) return;

   /* identify class */
   edev = _device_get_by_identifier(identifier);

   if (remote_surface->visible)
     e_client_mouse_wheel_send(ec, direction, z, edev, time);
}

static void
_remote_surface_cb_touch_event_transfer(struct wl_client *client, struct wl_resource *resource, uint32_t event_type, int32_t device, int32_t button, int32_t x, int32_t y, wl_fixed_t radius_x, wl_fixed_t radius_y, wl_fixed_t pressure, wl_fixed_t angle, uint32_t clas, uint32_t subclas, const char *identifier, uint32_t time)
{
   E_Comp_Wl_Remote_Provider *provider;
   E_Comp_Wl_Remote_Surface *remote_surface;
   E_Client *ec;

   Ecore_Device *edev = NULL;
   Ecore_Device_Class eclas;
   double eradx, erady, epressure, eangle;

   remote_surface = wl_resource_get_user_data(resource);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface->provider);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface->provider->ec);

   provider = remote_surface->provider;
   ec = provider->ec;

   if (e_object_is_del(E_OBJECT(ec))) return;

   /* identify class */
   if (clas == TIZEN_INPUT_DEVICE_CLAS_TOUCHSCREEN)
     eclas = ECORE_DEVICE_CLASS_TOUCH;
   else
     {
        ERR("Not supported device clas(%d) subclas(%d identifier(%s)",
            clas, subclas, identifier);
        return;
     }

   /* find ecore device*/
   edev = _device_get_by_identifier(identifier);
   if (edev)
     {
        eclas = ecore_device_class_get(edev);
     }

   /* fixed to */
   eradx = wl_fixed_to_double(radius_x);
   erady = wl_fixed_to_double(radius_y);
   epressure = wl_fixed_to_double(pressure);
   eangle = wl_fixed_to_double(angle);

   if (remote_surface->visible)
     {
        if (eclas == ECORE_DEVICE_CLASS_TOUCH)
          {
             switch (event_type)
               {
                case TIZEN_REMOTE_SURFACE_EVENT_TYPE_TOUCH_DOWN:
                   e_client_touch_update_send(ec,
                                              device,
                                              x, y,
                                              edev,
                                              eradx, erady, epressure, eangle,
                                              time);
                   e_client_touch_send(ec,
                                       device,
                                       x, y,
                                       EINA_TRUE,
                                       edev,
                                       eradx, erady, epressure, eangle,
                                       time);
                   break;
                case TIZEN_REMOTE_SURFACE_EVENT_TYPE_TOUCH_UP:
                   e_client_touch_send(ec,
                                       device,
                                       x, y,
                                       EINA_FALSE,
                                       edev,
                                       eradx, erady, epressure, eangle,
                                       time);
                   break;
                case TIZEN_REMOTE_SURFACE_EVENT_TYPE_TOUCH_MOVE:
                   e_client_touch_update_send(ec,
                                              device,
                                              x, y,
                                              edev,
                                              eradx, erady, epressure, eangle,
                                              time);
                   break;
                default:
                   ERR("Not supported event_type(%d)", event_type);
                   break;
               }
          }
     }
}

static void
_remote_surface_cb_touch_cancel_transfer(struct wl_client *client, struct wl_resource *resource)
{
   E_Comp_Wl_Remote_Provider *provider;
   E_Comp_Wl_Remote_Surface *remote_surface;
   E_Client *ec;

   remote_surface = wl_resource_get_user_data(resource);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface->provider);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface->provider->ec);

   provider = remote_surface->provider;
   ec = provider->ec;

   if (e_object_is_del(E_OBJECT(ec))) return;
   e_client_touch_cancel_send(ec);
}

static void
_remote_surface_cb_key_event_transfer(struct wl_client *client, struct wl_resource *resource, uint32_t event_type, int32_t keycode, uint32_t clas, uint32_t subclas, const char *identifier, uint32_t time)
{
   E_Comp_Wl_Remote_Provider *provider;
   E_Comp_Wl_Remote_Surface *remote_surface;
   E_Client *ec;

   Ecore_Device *edev = NULL;
   Ecore_Device_Class eclas;

   remote_surface = wl_resource_get_user_data(resource);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface->provider);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface->provider->ec);

   provider = remote_surface->provider;
   ec = provider->ec;

   if (e_object_is_del(E_OBJECT(ec))) return;

   /* identify class */
   if (clas == TIZEN_INPUT_DEVICE_CLAS_KEYBOARD)
     eclas = ECORE_DEVICE_CLASS_KEYBOARD;
   else
     {
        ERR("Not supported device class(%d) subclass(%d identifier(%s)",
            clas, subclas, identifier);
        return;
     }

   /* find ecore device*/
   edev = _device_get_by_identifier(identifier);
   if (edev)
     {
        eclas = ecore_device_class_get(edev);
     }

   if (remote_surface->visible)
     {
        if (eclas == ECORE_DEVICE_CLASS_KEYBOARD)
          {
             switch (event_type)
               {
                case TIZEN_REMOTE_SURFACE_EVENT_TYPE_KEY_DOWN:
                   e_client_key_send(ec,
                                     keycode,
                                     EINA_TRUE,
                                     edev,
                                     time);
                   break;
                case TIZEN_REMOTE_SURFACE_EVENT_TYPE_KEY_UP:
                   e_client_key_send(ec,
                                     keycode,
                                     EINA_FALSE,
                                     edev,
                                     time);
                   break;
                default:
                   ERR("Not supported event_type(%d)", event_type);
                   break;
               }
          }
     }
}

static void
_remote_surface_cb_visibility_transfer(struct wl_client *client, struct wl_resource *resource, uint32_t visibility_type)
{
   E_Comp_Wl_Remote_Surface *remote_surface;

   remote_surface = wl_resource_get_user_data(resource);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface);

   if (visibility_type == TIZEN_REMOTE_SURFACE_VISIBILITY_TYPE_INVISIBLE)
     {
        _remote_surface_visible_set(remote_surface, EINA_FALSE);
     }
   else if (visibility_type == TIZEN_REMOTE_SURFACE_VISIBILITY_TYPE_VISIBLE)
     {
        _remote_surface_visible_set(remote_surface, EINA_TRUE);
     }
}

static void
_remote_surface_cb_owner_set(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface_resource)
{
   E_Comp_Wl_Remote_Surface *remote_surface;
   E_Client *owner = NULL;

   remote_surface = wl_resource_get_user_data(resource);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface);

   if (surface_resource)
     owner = wl_resource_get_user_data(surface_resource);

   remote_surface->owner = owner;
   eina_hash_del_by_data(_rsm->surface_hash, remote_surface);

   if (owner)
     eina_hash_add(_rsm->surface_hash, &owner, remote_surface);

   if (remote_surface->provider)
     _remote_provider_onscreen_parent_calculate(remote_surface->provider);
}

static void
_remote_surface_cb_region_create(struct wl_client *client, struct wl_resource *remote_surface_resource, uint32_t id)
{
   struct wl_resource *resource;
   E_Comp_Wl_Remote_Surface *remote_surface;
   E_Comp_Wl_Remote_Region *region;
   E_Comp_Wl_Remote_Provider *provider;

   remote_surface = wl_resource_get_user_data(remote_surface_resource);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface);

   resource = wl_resource_create(client,
                                 &tizen_remote_surface_region_interface,
                                 1, id);

   if (!resource)
     {
        ERR("Could not create tizen remote region resource: %m");
        wl_client_post_no_memory(client);
        return;
     }

   region = E_NEW(E_Comp_Wl_Remote_Region, 1);
   region->remote_surface = remote_surface;
   region->resource = resource;
   region->geometry.x = -1;
   region->geometry.y = -1;
   region->geometry.w = -1;
   region->geometry.h = -1;
   remote_surface->regions = eina_list_append(remote_surface->regions, region);

   wl_resource_set_implementation(resource,
                                  &_remote_region_interface,
                                  region,
                                  _remote_region_cb_resource_destroy);

   //update provider's region rect list
   provider = remote_surface->provider;
   if ((provider) && (provider->onscreen_parent == remote_surface))
     {
        _remote_provider_rect_add(provider, &region->geometry);
     }
}

static const struct tizen_remote_surface_interface _remote_surface_interface =
{
   _remote_surface_cb_destroy,
   _remote_surface_cb_redirect,
   _remote_surface_cb_unredirect,
   _remote_surface_cb_mouse_event_transfer,
   _remote_surface_cb_mouse_wheel_transfer,
   _remote_surface_cb_touch_event_transfer,
   _remote_surface_cb_touch_cancel_transfer,
   _remote_surface_cb_key_event_transfer,
   _remote_surface_cb_visibility_transfer,
   _remote_surface_cb_owner_set,
   _remote_surface_cb_region_create,
};

static void
_remote_manager_cb_provider_create(struct wl_client *client, struct wl_resource *res_remote_manager, uint32_t id, struct wl_resource *surface_resource)
{
   struct wl_resource *resource;
   E_Comp_Wl_Remote_Provider *provider;
   E_Client *ec;
   uint32_t res_id;

   EINA_SAFETY_ON_NULL_RETURN(_rsm);

   ec = wl_resource_get_user_data(surface_resource);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   if (e_object_is_del(E_OBJECT(ec))) return;

   resource = wl_resource_create(client,
                                 &tizen_remote_surface_provider_interface,
                                 1, id);
   if (!resource)
     {
        ERR("Could not create tizen remote surface provider resource: %m");
        wl_client_post_no_memory(client);
        return;
     }

   provider = E_NEW(E_Comp_Wl_Remote_Provider, 1);
   provider->ec = ec;
   provider->resource = resource;

   wl_resource_set_implementation(resource,
                                  &_remote_provider_interface,
                                  provider,
                                  _remote_provider_cb_resource_destroy);

   eina_hash_add(_rsm->provider_hash, &ec, provider);

   RSMINF("Created resource(%p)",
          ec->pixmap, ec,
          "PROVIDER", provider, resource);

   _remote_provider_offscreen_set(provider, EINA_TRUE);

   /* send resource id */
   res_id = e_pixmap_res_id_get(ec->pixmap);
   tizen_remote_surface_provider_send_resource_id(resource, res_id);
}

static void
_remote_manager_cb_surface_create(struct wl_client *client, struct wl_resource *res_remote_manager, uint32_t id, uint32_t res_id, struct wl_resource *wl_tbm)
{
   struct wl_resource *resource;
   E_Comp_Wl_Remote_Surface *remote_surface;
   E_Comp_Wl_Remote_Provider *provider;
   E_Client *ec;

   EINA_SAFETY_ON_NULL_RETURN(_rsm);

   ec = e_pixmap_find_client_by_res_id(res_id);
   if (!ec)
     {
        ERR("Could not find client by res_id(%u)", res_id);
        return;
     }

   if (!(provider = _remote_provider_find(ec)))
     {
        ERR("EC(%p) res_id(%u) is not provider client", ec, res_id);
        return;
     }

   resource = wl_resource_create(client,
                                 &tizen_remote_surface_interface,
                                 1, id);
   if (!resource)
     {
        ERR("Could not create tizen remote surface resource: %m");
        wl_client_post_no_memory(client);
        return;
     }

   remote_surface = E_NEW(E_Comp_Wl_Remote_Surface, 1);
   remote_surface->resource = resource;
   remote_surface->wl_tbm = wl_tbm;
   remote_surface->provider = provider;
   remote_surface->redirect = EINA_FALSE;
   provider->surfaces = eina_list_append(provider->surfaces, remote_surface);

   wl_resource_set_implementation(resource,
                                  &_remote_surface_interface,
                                  remote_surface,
                                  _remote_surface_cb_resource_destroy);

   RSMINF("Created resource(%p) provider(%p)",
          NULL, NULL,
          "SURFACE", remote_surface, resource, provider);
}


static void
_remote_manager_cb_surface_bind(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface_resource, struct wl_resource *remote_surface_resource)
{
   E_Comp_Wl_Remote_Surface *remote_surface;
   E_Comp_Wl_Remote_Provider *provider;
   E_Client *ec = NULL;

   remote_surface = wl_resource_get_user_data(remote_surface_resource);
   if (!remote_surface) return;

   provider = remote_surface->provider;
   if (!provider) return;

   if (surface_resource)
     ec = wl_resource_get_user_data(surface_resource);

   _remote_surface_bind_client(remote_surface, ec);
}

static const struct tizen_remote_surface_manager_interface _remote_manager_interface =
{
   _remote_manager_cb_provider_create,
   _remote_manager_cb_surface_create,
   _remote_manager_cb_surface_bind,
};

static void
_remote_manager_cb_unbind(struct wl_resource *res_remote_manager)
{
   //nothing to do yet.
}

static void
_remote_manager_cb_bind(struct wl_client *client, void *data EINA_UNUSED, uint32_t ver, uint32_t id)
{
   struct wl_resource *res_remote_manager;

   res_remote_manager = wl_resource_create(client,
                                        &tizen_remote_surface_manager_interface,
                                        ver,
                                        id);
   EINA_SAFETY_ON_NULL_GOTO(res_remote_manager, err);

   wl_resource_set_implementation(res_remote_manager,
                                  &_remote_manager_interface,
                                  NULL,
                                  _remote_manager_cb_unbind);
   return;

err:
   ERR("Could not create tizen_remote_surface_manager_interface res: %m");
   wl_client_post_no_memory(client);
}

static void
_e_comp_wl_remote_cb_client_del(void *data, E_Client *ec)
{
   E_Comp_Wl_Remote_Provider *provider;
   E_Comp_Wl_Remote_Surface *remote_surface;

   if ((provider = eina_hash_find(_rsm->provider_hash, &ec)))
     {
        eina_hash_del(_rsm->provider_hash, &ec, provider);
        EINA_LIST_FREE(provider->surfaces, remote_surface)
          {
             if (remote_surface->provider == provider)
               {
                  /* unset remote buffer from provider */
                  if (remote_surface->bind_ec)
                    _remote_surface_bind_client(remote_surface, NULL);

                  remote_surface->provider = NULL;
                  //notify of this ejection to remote surface_resource
                  tizen_remote_surface_send_missing(remote_surface->resource);
               }
          }
        _remote_provider_offscreen_set(provider, EINA_FALSE);
        wl_resource_set_user_data(provider->resource, NULL);
        E_FREE(provider);
     }

   if ((remote_surface = eina_hash_find(_rsm->surface_hash, &ec)))
     {
        eina_hash_del(_rsm->surface_hash, &ec, remote_surface);
        if (remote_surface->owner == ec)
          remote_surface->owner = NULL;
        if (remote_surface->provider)
          _remote_provider_onscreen_parent_calculate(remote_surface->provider);
     }
}

static Eina_Bool
_e_comp_wl_remote_cb_visibility_change(void *data, int type, void *event)
{
   E_Event_Client *ev = event;
   E_Client *ec;
   E_Comp_Wl_Remote_Surface *remote_surface;

   EINA_SAFETY_ON_NULL_RETURN_VAL(_rsm, ECORE_CALLBACK_PASS_ON);

   ec = ev->ec;
   if (!ec) return ECORE_CALLBACK_PASS_ON;

   if (e_object_is_del(E_OBJECT(ec))) return ECORE_CALLBACK_PASS_ON;

   if ((remote_surface = eina_hash_find(_rsm->surface_hash, &ec)))
     {
        _remote_provider_onscreen_parent_calculate(remote_surface->provider);
     }

   return ECORE_CALLBACK_PASS_ON;
}

static void
_e_comp_wl_remote_surface_state_buffer_set(E_Comp_Wl_Surface_State *state, E_Comp_Wl_Buffer *buffer)
{
   if (state->buffer == buffer) return;
   if (state->buffer)
     wl_list_remove(&state->buffer_destroy_listener.link);
   state->buffer = buffer;
   if (state->buffer)
     wl_signal_add(&state->buffer->destroy_signal,
                   &state->buffer_destroy_listener);
}

static void
_e_comp_wl_remote_surface_state_commit(E_Comp_Wl_Remote_Provider *provider, E_Comp_Wl_Surface_State *state)
{
   E_Comp_Wl_Remote_Surface *surface;
   E_Client *ec;
   struct wl_resource *remote_buffer;
   struct wl_resource *cb;
   Eina_Rectangle *dmg;
   int x = 0, y = 0, sx = 0, sy = 0;
   E_Comp_Wl_Buffer *buffer;
   Eina_List *l, *ll;

   ec = provider->ec;
   if (e_object_is_del(E_OBJECT(ec))) return;

   if (state->new_attach)
     e_comp_wl_surface_attach(ec, state->buffer);

   _e_comp_wl_remote_surface_state_buffer_set(state, NULL);

   if (state->new_attach)
     {
        x = ec->client.x, y = ec->client.y;

        ec->w = ec->client.w = state->bw;
        ec->h = ec->client.h = state->bh;

        if ((ec->comp_data->shell.surface) && (ec->comp_data->shell.configure))
          ec->comp_data->shell.configure(ec->comp_data->shell.surface,
                                         x, y, ec->w, ec->h);
     }

   sx = state->sx;
   sy = state->sy;
   state->sx = 0;
   state->sy = 0;
   state->new_attach = EINA_FALSE;

   /* send previous frame done */
   EINA_LIST_FOREACH_SAFE(ec->comp_data->frames, l, ll, cb)
     {
         wl_callback_send_done(cb, ecore_time_unix_get() * 1000);
         wl_resource_destroy(cb);
     }

   ec->comp_data->frames = eina_list_merge(ec->comp_data->frames,
                                           state->frames);
   state->frames = NULL;

   /* clear stored damages... */
   EINA_LIST_FREE(state->buffer_damages, dmg)
      eina_rectangle_free(dmg);

   EINA_LIST_FREE(state->damages, dmg)
      eina_rectangle_free(dmg);

   state->buffer_viewport.changed = 0;

   /* send remote buffer to remote surfaces */
   buffer = e_pixmap_resource_get(ec->pixmap);
   if (buffer)
     {
        EINA_LIST_FOREACH(provider->surfaces, l, surface)
          {
             remote_buffer = e_comp_wl_tbm_remote_buffer_get(surface->wl_tbm, buffer->resource);
             if (!remote_buffer) continue;
             if (!surface->redirect) continue;
             if (surface->bind_ec)
               {
                 E_Comp_Wl_Buffer *buffer;

                 buffer = e_comp_wl_buffer_get(remote_buffer, surface->bind_ec);
                 _e_comp_wl_remote_surface_state_buffer_set(&surface->bind_ec->comp_data->pending, buffer);
                 surface->bind_ec->comp_data->pending.sx = sx;
                 surface->bind_ec->comp_data->pending.sy = sy;
                 surface->bind_ec->comp_data->pending.new_attach = EINA_TRUE;

                 e_comp_wl_surface_commit(surface->bind_ec);
               }
             else
               {
                  tizen_remote_surface_send_update_buffer(surface->resource,
                                                          remote_buffer,
                                                          ecore_time_get());
               }
          }

        /* send frame done */
        e_pixmap_image_clear(ec->pixmap, 1);
     }
}
#endif /* HAVE_REMOTE_SURFACE */

EINTERN Eina_Bool
e_comp_wl_remote_surface_commit(E_Client *ec)
{
#ifdef HAVE_REMOTE_SURFACE
   E_Comp_Wl_Remote_Provider *provider;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   if (!(provider = _remote_provider_find(ec)))
     return EINA_FALSE;

   _e_comp_wl_remote_surface_state_commit(provider, &ec->comp_data->pending);

   return EINA_TRUE;
#else
   return EINA_FALSE;
#endif /* HAVE_REMOTE_SURFACE */
}

EINTERN void
e_comp_wl_remote_surface_init(void)
{
#ifdef HAVE_REMOTE_SURFACE
   E_Comp_Wl_Remote_Manager *rs_manager = NULL;

   EINA_SAFETY_ON_NULL_RETURN(e_comp_wl);
   EINA_SAFETY_ON_NULL_RETURN(e_comp_wl->wl.disp);
   EINA_SAFETY_ON_NULL_RETURN(e_comp->wl_comp_data->tbm.server);

   rs_manager = E_NEW(E_Comp_Wl_Remote_Manager, 1);
   EINA_SAFETY_ON_NULL_RETURN(rs_manager);

   rs_manager->global = wl_global_create(e_comp_wl->wl.disp,
                                         &tizen_remote_surface_manager_interface,
                                         1,
                                         NULL,
                                         _remote_manager_cb_bind);

   /* client hook */
   e_client_hook_add(E_CLIENT_HOOK_DEL, _e_comp_wl_remote_cb_client_del, NULL);

   /* client event */
   E_LIST_HANDLER_APPEND(rs_manager->event_hdlrs,
                         E_EVENT_CLIENT_VISIBILITY_CHANGE,
                         _e_comp_wl_remote_cb_visibility_change, rs_manager);

   rs_manager->provider_hash = eina_hash_pointer_new(NULL);
   rs_manager->surface_hash = eina_hash_pointer_new(NULL);

   _rsm = rs_manager;

   E_EVENT_REMOTE_SURFACE_PROVIDER_VISIBILITY_CHANGE = ecore_event_type_new();
#endif /* HAVE_REMOTE_SURFACE */
}

EINTERN void
e_comp_wl_remote_surface_shutdown(void)
{
#ifdef HAVE_REMOTE_SURFACE
   E_Comp_Wl_Remote_Manager *rsm;
   E_Comp_Wl_Remote_Provider *provider;
   E_Comp_Wl_Remote_Surface *remote_surface;
   Eina_Iterator *it;

   if (!_rsm) return;

   rsm = _rsm;
   _rsm = NULL;

   it = eina_hash_iterator_data_new(rsm->provider_hash);
   EINA_ITERATOR_FOREACH(it, provider)
     {
        EINA_LIST_FREE(provider->surfaces, remote_surface)
          {
             remote_surface->provider = NULL;
             wl_resource_destroy(remote_surface->resource);
          }
        wl_resource_destroy(provider->resource);
     }
   eina_iterator_free(it);

   E_FREE_FUNC(rsm->provider_hash, eina_hash_free);
   E_FREE_FUNC(rsm->surface_hash, eina_hash_free);

   E_FREE_LIST(rsm->event_hdlrs, ecore_event_handler_del);
   wl_global_destroy(rsm->global);
   E_FREE(rsm);
#endif /* HAVE_REMOTE_SURFACE */
}
