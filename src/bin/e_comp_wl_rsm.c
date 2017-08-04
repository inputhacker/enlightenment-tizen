#include "e.h"
#include "e_policy_wl.h"
#ifdef HAVE_REMOTE_SURFACE
 #include <tizen-remote-surface-server-protocol.h>
#endif /* HAVE_REMOTE_SURFACE */
#include <tbm_surface.h>
#include <tbm_surface_internal.h>
#include <wayland-tbm-server.h>

#include <pixman.h>

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

#define container_of(ptr, type, member) \
   ({ \
    const __typeof__( ((type *)0)->member ) *__mptr = (ptr); \
    (type *)( (char *)__mptr - offsetof(type,member) ); \
    })

E_API int E_EVENT_REMOTE_SURFACE_PROVIDER_VISIBILITY_CHANGE = -1;

#ifdef HAVE_REMOTE_SURFACE
typedef struct _E_Comp_Wl_Remote_Manager E_Comp_Wl_Remote_Manager;
typedef struct _E_Comp_Wl_Remote_Common E_Comp_Wl_Remote_Common;
typedef struct _E_Comp_Wl_Remote_Provider E_Comp_Wl_Remote_Provider;
typedef struct _E_Comp_Wl_Remote_Source E_Comp_Wl_Remote_Source;
typedef struct _E_Comp_Wl_Remote_Surface E_Comp_Wl_Remote_Surface;
typedef struct _E_Comp_Wl_Remote_Region E_Comp_Wl_Remote_Region;
typedef struct _E_Comp_Wl_Remote_Buffer E_Comp_Wl_Remote_Buffer;

struct _E_Comp_Wl_Remote_Manager
{
   struct wl_global *global;

   Eina_Hash *provider_hash;
   Eina_Hash *surface_hash;
   Eina_Hash *source_hash;
   Eina_List *event_hdlrs;
   Eina_List *client_hooks;

   int dummy_fd; /* tizen_remote_surface@chagned_buffer need valid fd when it send tbm surface */
};

/* common structure of provider and source */
struct _E_Comp_Wl_Remote_Common
{
   E_Client *ec;
   Eina_List *surfaces;

   Eina_Bool is_offscreen;
   Eina_Bool ignore_output_transform;
};

/* widget client */
struct _E_Comp_Wl_Remote_Provider
{
   E_Comp_Wl_Remote_Common common;

   struct wl_resource *resource;

   E_Comp_Wl_Remote_Surface *onscreen_parent;

   Eina_Bool visible;
   int vis_ref;
   uint32_t input_event_filter;
   int buffer_mode;
};

/* normal UI client */
struct _E_Comp_Wl_Remote_Source
{
   E_Comp_Wl_Remote_Common common;

   E_Comp_Wl_Buffer_Ref buffer_ref;
   const char *image_path;
   Ecore_Thread *th;
   Eina_Bool deleted;

   int offscreen_ref;

   Eina_Bool defer_img_save;
};

/* widget viewer or task-manager client */
struct _E_Comp_Wl_Remote_Surface
{
   struct wl_resource *resource;
   struct wl_resource *wl_tbm;
   struct wl_listener tbm_destroy_listener;

   E_Comp_Wl_Remote_Provider *provider;
   E_Comp_Wl_Remote_Source *source;
   E_Client *bind_ec;

   E_Client *owner;
   Eina_List *regions;

   Eina_Bool redirect;
   Eina_Bool visible;

   Eina_Bool remote_render;

   Eina_Bool valid;

   struct
   {
      Eina_Bool use;
      enum tizen_remote_surface_changed_buffer_event_filter filter;
   } changed_buff_ev_filter;

   struct
   {
      Eina_Bool set;
      enum tizen_remote_surface_buffer_type type;
      uint32_t serial;
   } req_curr_buff;

   int version;
};

struct _E_Comp_Wl_Remote_Region
{
   struct wl_resource *resource;

   E_Comp_Wl_Remote_Surface *remote_surface;
   Eina_Rectangle geometry;
   Evas_Object *mirror;
};

struct _E_Comp_Wl_Remote_Buffer
{
   E_Comp_Wl_Buffer_Ref ref;
   struct wl_resource *resource;
   struct wl_listener destroy_listener;
};

static E_Comp_Wl_Remote_Manager *_rsm = NULL;

static void _e_comp_wl_remote_surface_state_buffer_set(E_Comp_Wl_Surface_State *state, E_Comp_Wl_Buffer *buffer);
static void _e_comp_wl_remote_buffer_cb_destroy(struct wl_listener *listener, void *data);
static E_Comp_Wl_Remote_Buffer *_e_comp_wl_remote_buffer_get(struct wl_resource *remote_buffer_resource);
static void _remote_surface_region_clear(E_Comp_Wl_Remote_Surface *remote_surface);
static void _remote_surface_ignore_output_transform_send(E_Comp_Wl_Remote_Common *common);
static void _remote_source_save_start(E_Comp_Wl_Remote_Source *source);

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

static void
_remote_region_mirror_clear(E_Comp_Wl_Remote_Region *region)
{
   if (!region) return;
   if (!region->mirror) return;

   evas_object_del(region->mirror);
}

static void
_remote_provider_rect_add(E_Comp_Wl_Remote_Provider *provider, Eina_Rectangle *rect)
{
   E_Client *ec;

   ec = provider->common.ec;
   if (!ec) return;
   if (!ec->comp_data) return;

   ec->comp_data->remote_surface.regions =
      eina_list_remove(ec->comp_data->remote_surface.regions,
                       rect);
   ec->comp_data->remote_surface.regions =
      eina_list_append(ec->comp_data->remote_surface.regions,
                       rect);
}

static void
_remote_provider_rect_del(E_Comp_Wl_Remote_Provider *provider, Eina_Rectangle *rect)
{
   E_Client *ec;

   ec = provider->common.ec;
   if (!ec) return;
   if (!ec->comp_data) return;

   ec->comp_data->remote_surface.regions =
      eina_list_remove(ec->comp_data->remote_surface.regions,
                       rect);
}

static void
_remote_provider_rect_clear(E_Comp_Wl_Remote_Provider *provider)
{
   E_Client *ec;

   ec = provider->common.ec;
   if (!ec) return;
   if (!ec->comp_data) return;

   /* TODO : remove it from here after supporting multiple onscreen surface */
   _remote_surface_region_clear(provider->onscreen_parent);

   ec->comp_data->remote_surface.regions =
      eina_list_remove_list(ec->comp_data->remote_surface.regions,
                            ec->comp_data->remote_surface.regions);
}

static void
_remote_provider_onscreen_parent_set(E_Comp_Wl_Remote_Provider *provider, E_Comp_Wl_Remote_Surface *parent)
{
   E_Comp_Wl_Remote_Region *region;
   Eina_List *l;

   if (!provider) return;
   if ((parent) && !(parent->owner)) return;
   if (provider->onscreen_parent == parent) return;

   _remote_provider_rect_clear(provider);

   provider->onscreen_parent = parent;
   provider->common.ec->comp_data->remote_surface.onscreen_parent = NULL;

   RSMDBG("set onscreen_parent %p(ec:%p)",
          provider->common.ec->pixmap, provider->common.ec,
          "PROVIDER", provider,
          parent, parent? parent->owner:NULL);

   if (parent)
     {
        EINA_LIST_FOREACH(provider->onscreen_parent->regions, l, region)
          {
             _remote_provider_rect_add(provider, &region->geometry);
          }

        provider->common.ec->comp_data->remote_surface.onscreen_parent = parent->owner;
     }
}

static void
_remote_provider_onscreen_parent_calculate(E_Comp_Wl_Remote_Provider *provider)
{
   Evas_Object *o;
   E_Client *ec, *_ec, *parent = NULL;
   E_Comp_Wl_Remote_Surface *surface;
   E_Comp_Wl_Client_Data *cdata;

   if (!provider) return;

   ec = provider->common.ec;
   if (!ec) return;
   if (!ec->comp_data) return;
   if (!provider->common.surfaces) return;

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
   EINA_SAFETY_ON_NULL_RETURN(provider->common.ec);

   ec = provider->common.ec;

   if (e_object_is_del(E_OBJECT(ec))) return;

   if (set)
     {
        provider->common.is_offscreen = set;
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
        provider->common.is_offscreen = set;
        ec->icccm.accepts_focus = ec->icccm.take_focus = ec->want_focus = EINA_TRUE;
        ec->placed = EINA_FALSE;

        _remote_provider_onscreen_parent_set(provider, NULL);

        e_comp_wl_surface_commit(ec);
     }

   _remote_surface_ignore_output_transform_send(&provider->common);

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

   if (e_object_is_del(E_OBJECT(provider->common.ec))) return;

   ev = E_NEW(E_Event_Remote_Surface_Provider, 1);
   if (!ev) return;

   ev->ec = provider->common.ec;
   e_object_ref(E_OBJECT(provider->common.ec));
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
               provider->common.ec->pixmap, provider->common.ec,
               "PROVIDER", provider, provider->vis_ref);

        if (provider->vis_ref == 1)
          {
             provider->common.ec->visibility.obscured = E_VISIBILITY_UNOBSCURED;

             _remote_provider_visible_event_send(provider);
             e_policy_client_visibility_send(provider->common.ec);

             tizen_remote_surface_provider_send_visibility
                (provider->resource,
                 TIZEN_REMOTE_SURFACE_PROVIDER_VISIBILITY_TYPE_VISIBLE);
          }
     }
   else
     {
        provider->vis_ref --;
        RSMDBG("Count down vis_ref:%d",
               provider->common.ec->pixmap, provider->common.ec,
               "PROVIDER", provider, provider->vis_ref);

        if (provider->vis_ref == 0)
          {
             provider->common.ec->visibility.obscured = E_VISIBILITY_FULLY_OBSCURED;

             _remote_provider_visible_event_send(provider);
             e_policy_client_visibility_send(provider->common.ec);

             tizen_remote_surface_provider_send_visibility
                (provider->resource,
                 TIZEN_REMOTE_SURFACE_PROVIDER_VISIBILITY_TYPE_INVISIBLE);

             if (provider->buffer_mode)
               e_pixmap_buffer_clear(provider->common.ec->pixmap, EINA_TRUE);
          }
     }

   _remote_provider_onscreen_parent_calculate(provider);
}

static void
_remote_provider_client_set(E_Client *ec, Eina_Bool set)
{
   if (!ec) return;
   if ((e_object_is_del(E_OBJECT(ec)))) return;

   ec->remote_surface.provider = set;
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

/* true : given buffer type can be delivered to the client
 * false: the client wants to filter given buffer type, thus changed_buffer
 *        event for this type of buffer will not be sent to the client.
 */
static Eina_Bool
_remote_surface_changed_buff_ev_filter_check(E_Comp_Wl_Remote_Surface *rs,
                                             enum tizen_remote_surface_buffer_type buff_type)
{
   Eina_Bool res = EINA_TRUE;

   if (rs->changed_buff_ev_filter.use)
     {
        switch (buff_type)
          {
           case TIZEN_REMOTE_SURFACE_BUFFER_TYPE_TBM:
              if (rs->changed_buff_ev_filter.filter & TIZEN_REMOTE_SURFACE_CHANGED_BUFFER_EVENT_FILTER_TBM)
                res = EINA_FALSE;
              break;
           case TIZEN_REMOTE_SURFACE_BUFFER_TYPE_IMAGE_FILE:
              if (rs->changed_buff_ev_filter.filter & TIZEN_REMOTE_SURFACE_CHANGED_BUFFER_EVENT_FILTER_IMAGE_FILE)
                res = EINA_FALSE;
              break;
           default:
              break;
          }
     }

   return res;
}

static Eina_Bool
_remote_surface_changed_buff_protocol_send(E_Comp_Wl_Remote_Surface *rs,
                                           enum tizen_remote_surface_buffer_type buff_type,
                                           E_Comp_Wl_Remote_Buffer *rbuff,
                                           int img_file_fd,
                                           unsigned int img_file_size,
                                           Eina_Bool ref_set,
                                           E_Comp_Wl_Buffer *buff)
{
   struct wl_resource *tbm = NULL;
   Eina_Bool send = EINA_FALSE;
   struct wl_array opts;
   Eina_Bool add_opts = EINA_FALSE;
   char *p, tmp[16];
   int len;

   if (rbuff)
     tbm = rbuff->resource;

   send = _remote_surface_changed_buff_ev_filter_check(rs, buff_type);
   if (send)
     {
        if ((ref_set) &&
            (buff) &&
            (rs->version >= 2)) /* WORKAROUND for 3.0: old version wayland-scanner can't generation since macro. TIZEN_REMOTE_SURFACE_RELEASE_SINCE_VERSION */
          e_comp_wl_buffer_reference(&rbuff->ref, buff);

        if (rs->version >= TIZEN_REMOTE_SURFACE_CHANGED_BUFFER_SINCE_VERSION)
          {
             if (rs->req_curr_buff.set)
               {
                  /* example of option list:
                   *  [0] "curr_buff_req_serial"
                   *  [1] "257"
                   *  [2] "opt_none"
                   */
                  wl_array_init(&opts);
                  p = wl_array_add(&opts, 21);
                  if (p) strncpy(p, "curr_buff_req_serial", 21);

                  snprintf(tmp, sizeof(tmp), "%u", rs->req_curr_buff.serial);
                  len = strlen(tmp) + 1;
                  p = wl_array_add(&opts, len);
                  if (p) strncpy(p, tmp, len);

                  p = wl_array_add(&opts, 9);
                  if (p) strncpy(p, "opt_none", 9);

                  rs->req_curr_buff.set = EINA_FALSE;
                  add_opts = EINA_TRUE;
               }

             RSMINF("CHANGED_BUFF send:%d type:%u tbm:%p fd:%d(%d) add_opts:%d EV_FILTER(%d):%u",
                    NULL, NULL, "SURFACE", rs,
                    send, buff_type, tbm, img_file_fd, img_file_size, add_opts,
                    rs->changed_buff_ev_filter.use,
                    rs->changed_buff_ev_filter.filter);

             tizen_remote_surface_send_changed_buffer(rs->resource,
                                                      buff_type,
                                                      tbm,
                                                      img_file_fd,
                                                      img_file_size,
                                                      ecore_time_get() * 1000,
                                                      add_opts ? &opts : NULL);
          }
        else
          tizen_remote_surface_send_update_buffer(rs->resource,
                                                  tbm,
                                                  ecore_time_get() * 1000);
     }

   if (add_opts)
     wl_array_release(&opts);

   return send;
}

static Eina_Bool
_remote_surface_buff_send(E_Comp_Wl_Remote_Surface *rs)
{
   enum tizen_remote_surface_buffer_type buff_type;
   E_Comp_Wl_Remote_Provider *provider;
   E_Comp_Wl_Remote_Source *source;
   E_Comp_Wl_Buffer *buff = NULL;
   struct wl_resource *rbuff_res = NULL;
   E_Comp_Wl_Remote_Buffer *rbuff = NULL;
   char *img_path;
   int fd = _rsm->dummy_fd;
   off_t img_size = 0;
   Eina_Bool res = EINA_FALSE;

   E_Client *src_ec = NULL;

   source = rs->source;
   provider = rs->provider;

   if (provider)
     {
        src_ec = provider->common.ec;
        img_path = NULL;
     }
   else if (source)
     {
        src_ec = source->common.ec;
        img_path = (char *)source->image_path;
     }

   EINA_SAFETY_ON_NULL_RETURN_VAL(src_ec, EINA_FALSE);

   buff = e_pixmap_resource_get(src_ec->pixmap);
   if (buff)
     {
        rbuff_res = e_comp_wl_tbm_remote_buffer_get(rs->wl_tbm, buff->resource);
        EINA_SAFETY_ON_NULL_RETURN_VAL(rbuff_res, EINA_FALSE);

        rbuff = _e_comp_wl_remote_buffer_get(rbuff_res);
        EINA_SAFETY_ON_NULL_RETURN_VAL(rbuff, EINA_FALSE);

        buff_type = TIZEN_REMOTE_SURFACE_BUFFER_TYPE_TBM;
        res = EINA_TRUE;

        /* TODO: if client wants to receive image file for the remote_surfac_provider,
         * then makes image file from tbm buffer and sends information for that file.
         * otherwise, just sends tbm buffer to the client.
         */
        res = _remote_surface_changed_buff_protocol_send(rs,
                                                         buff_type,
                                                         rbuff,
                                                         _rsm->dummy_fd,
                                                         (unsigned int)img_size,
                                                         EINA_FALSE,
                                                         NULL);
     }
   else
     {
        EINA_SAFETY_ON_NULL_RETURN_VAL(img_path, EINA_FALSE);

        fd = open(img_path, O_RDONLY);
        EINA_SAFETY_ON_FALSE_RETURN_VAL(fd != -1, EINA_FALSE);

        img_size = lseek(fd, 0, SEEK_END);
        EINA_SAFETY_ON_FALSE_GOTO(img_size > 0, close_fd);

        buff_type = TIZEN_REMOTE_SURFACE_BUFFER_TYPE_IMAGE_FILE;
        res = EINA_TRUE;

        /* TODO: if client wants to receive image file for the remote_surfac_provider,
         * then makes image file from tbm buffer and sends information for that file.
         * otherwise, just sends tbm buffer to the client.
         */
        res = _remote_surface_changed_buff_protocol_send(rs,
                                                         buff_type,
                                                         rbuff,
                                                         fd,
                                                         (unsigned int)img_size,
                                                         EINA_FALSE,
                                                         NULL);
        close(fd);
     }


   return res;

close_fd:
   close(fd);
   return EINA_FALSE;
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

        /* try to send latest buffer of the provider to the consumer when unbinding
         * the remote surface to avoid showing old buffer on consumer's window for a while.
         */
        if (remote_surface->provider)
          {
             E_Comp_Wl_Buffer *buffer;
             struct wl_resource *remote_buffer_resource;
             E_Comp_Wl_Remote_Buffer *remote_buffer;

             RSMINF("Try to send latest buffer of provider:%p(ec:%p)",
                    NULL, NULL,
                    "SURFACE", remote_surface,
                    remote_surface->provider,
                    remote_surface->provider->common.ec);

             EINA_SAFETY_ON_NULL_GOTO(remote_surface->provider->common.ec, bind_ec_set);

             buffer = e_pixmap_resource_get(remote_surface->provider->common.ec->pixmap);
             EINA_SAFETY_ON_NULL_GOTO(buffer, bind_ec_set);

             remote_buffer_resource = e_comp_wl_tbm_remote_buffer_get(remote_surface->wl_tbm, buffer->resource);
             EINA_SAFETY_ON_NULL_GOTO(remote_buffer_resource, bind_ec_set);

             remote_buffer = _e_comp_wl_remote_buffer_get(remote_buffer_resource);
             EINA_SAFETY_ON_NULL_GOTO(remote_buffer, bind_ec_set);

             _remote_surface_changed_buff_protocol_send(remote_surface,
                                                        TIZEN_REMOTE_SURFACE_BUFFER_TYPE_TBM,
                                                        remote_buffer,
                                                        _rsm->dummy_fd,
                                                        0,
                                                        EINA_TRUE,
                                                        buffer);
          }
     }

bind_ec_set:
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

        /* try to set latest buffer of the provider to bind_ec */
        if (remote_surface->provider && remote_surface->provider->common.ec)
          {
             E_Comp_Wl_Buffer *buffer;

             buffer = e_pixmap_resource_get(remote_surface->provider->common.ec->pixmap);
             EINA_SAFETY_ON_NULL_RETURN(buffer);

             _e_comp_wl_remote_surface_state_buffer_set(&remote_surface->bind_ec->comp_data->pending, buffer);

             remote_surface->bind_ec->comp_data->pending.sx = 0;
             remote_surface->bind_ec->comp_data->pending.sy = 0;
             remote_surface->bind_ec->comp_data->pending.new_attach = EINA_TRUE;

             e_comp_wl_surface_commit(remote_surface->bind_ec);
          }
     }
}

static void
_remote_surface_ignore_output_transform_send(E_Comp_Wl_Remote_Common *common)
{
   E_Comp_Wl_Remote_Surface *remote_surface;
   const char *msg;

   EINA_SAFETY_ON_NULL_RETURN(common);

   if (eina_list_count(common->surfaces) != 1)
     {
        msg = "remote surface count = 0 or over 1";
        goto ignore;
     }

   remote_surface = eina_list_nth(common->surfaces, 0);
   if (remote_surface && remote_surface->bind_ec)
     {
        msg = "1 binding remote surface";
        goto no_ignore;
     }

   if (common->is_offscreen)
     {
        msg = "offscreen";
        goto ignore;
     }
   else
     {
        msg = "not offscreen";
        goto no_ignore;
     }

ignore:
   if (common->ignore_output_transform != EINA_TRUE)
     {
        RSMINF("ignore output transform: %s", NULL, common->ec, "common", NULL, msg);
        e_comp_screen_rotation_ignore_output_transform_send(common->ec, EINA_TRUE);
        common->ignore_output_transform = EINA_TRUE;
     }
   return;

no_ignore:
   if (common->ignore_output_transform != EINA_FALSE)
     {
        RSMINF("not ignore output transform: %s", NULL, common->ec, "common", NULL, msg);
        e_comp_screen_rotation_ignore_output_transform_send(common->ec, EINA_FALSE);
        common->ignore_output_transform = EINA_FALSE;
     }
   return;
}

static void
_remote_source_send_image_update(E_Comp_Wl_Remote_Source *source)
{
   int fd = -1;
   off_t image_size;
   Eina_List *l;
   E_Comp_Wl_Remote_Surface *remote_surface;

   if (!source->image_path) return;

   fd = open(source->image_path, O_RDONLY);
   if (fd == -1)
     {
        ERR("%m");
        return;
     }

   image_size = lseek(fd, 0, SEEK_END);
   if (image_size <= 0)
     {
        close(fd);
        return;
     }

   RSMDBG("send image fd(%d) path(%s) size(%jd)",
          NULL, source->common.ec, "SOURCE", source, fd, source->image_path, (intmax_t)image_size);

   EINA_LIST_FOREACH(source->common.surfaces, l, remote_surface)
     {
        if (remote_surface->version < TIZEN_REMOTE_SURFACE_CHANGED_BUFFER_SINCE_VERSION)
          continue;

        _remote_surface_changed_buff_protocol_send(remote_surface,
                                                   TIZEN_REMOTE_SURFACE_BUFFER_TYPE_IMAGE_FILE,
                                                   NULL,
                                                   fd,
                                                   (unsigned int)image_size,
                                                   EINA_FALSE,
                                                   NULL);
     }

   close(fd);
}

typedef struct {
     struct wl_shm_buffer *shm_buffer;
     struct wl_shm_pool *shm_pool;
     tbm_surface_h tbm_surface;

     uint32_t transform;

     const char *image_path;
     E_Client *ec;
} Thread_Data;

static E_Comp_Wl_Remote_Source *
_remote_source_find(E_Client *ec)
{
   E_Comp_Wl_Remote_Source *source;

   EINA_SAFETY_ON_NULL_RETURN_VAL(_rsm, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(_rsm->source_hash, NULL);

   source = eina_hash_find(_rsm->source_hash, &ec);
   return source;
}

static void
_remote_source_destroy(E_Comp_Wl_Remote_Source *source)
{
   E_Comp_Wl_Remote_Surface *remote_surface;
   if (!source) return;

   RSMDBG("remote source destroy", NULL, source->common.ec,"SOURCE", source);

   if (source->th)
     {
        RSMDBG("IMG save is cancelled. th:%p pending destroy",
               source->common.ec->pixmap, source->common.ec, "SOURCE",
               source, source->th);
        ecore_thread_cancel(source->th);
        source->deleted = EINA_TRUE;
        return;
     }

   if (_rsm)
     eina_hash_del_by_data(_rsm->source_hash, source);

   EINA_LIST_FREE(source->common.surfaces, remote_surface)
     {
        if (remote_surface->source == source)
          {
             remote_surface->source = NULL;
             tizen_remote_surface_send_missing(remote_surface->resource);
          }
     }

   /* is it ok without client's ack ?*/
   if (source->image_path)
     {
        if (!e_config->hold_prev_win_img)
          {
             RSMDBG("IMG del %s", NULL, source->common.ec, "SOURCE", source, source->image_path);
             ecore_file_remove(source->image_path);
          }
        eina_stringshare_del(source->image_path);
     }

   E_FREE(source);
}

static pixman_format_code_t
_remote_source_image_data_pixman_format_get_from_tbm_surface(tbm_format format)
{
   switch(format)
     {
      case TBM_FORMAT_ARGB8888: return PIXMAN_a8r8g8b8;
      case TBM_FORMAT_XRGB8888: return PIXMAN_x8r8g8b8;
      default:                  return PIXMAN_x8r8g8b8;
     }
   return PIXMAN_x8r8g8b8;
}

static pixman_format_code_t
_remote_source_image_data_pixman_format_get_from_shm_buffer(uint32_t format)
{
   switch(format)
     {
      case WL_SHM_FORMAT_ARGB8888: return PIXMAN_a8r8g8b8;
      case WL_SHM_FORMAT_XRGB8888: return PIXMAN_x8r8g8b8;
      default:                     return PIXMAN_x8r8g8b8;
     }
   return PIXMAN_x8r8g8b8;
}

static tbm_surface_h
_remote_source_image_data_transform(Thread_Data *td, int w, int h)
{
   tbm_surface_h transform_surface = NULL;
   tbm_surface_info_s info;
   int tw = 0, th = 0;
   pixman_image_t *src_img = NULL, *dst_img = NULL;
   pixman_format_code_t src_format, dst_format;
   pixman_transform_t t;
   struct pixman_f_transform ft;
   unsigned char *src_ptr = NULL, *dst_ptr = NULL;
   int c = 0, s = 0, tx = 0, ty = 0;

   EINA_SAFETY_ON_NULL_RETURN_VAL(td, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(td->tbm_surface, NULL);

   if (td->transform == WL_OUTPUT_TRANSFORM_NORMAL || td->transform > WL_OUTPUT_TRANSFORM_270) return NULL;

   if (td->tbm_surface)
     {
        src_format = _remote_source_image_data_pixman_format_get_from_tbm_surface(tbm_surface_get_format(td->tbm_surface));
        dst_format = src_format;

        tbm_surface_map(td->tbm_surface, TBM_SURF_OPTION_READ|TBM_SURF_OPTION_WRITE, &info);
        src_ptr = info.planes[0].ptr;

        src_img = pixman_image_create_bits(src_format, w, h, (uint32_t*)src_ptr, info.planes[0].stride);
        EINA_SAFETY_ON_NULL_GOTO(src_img, error_case);
     }
   else if (td->shm_buffer)
     {
        src_format = _remote_source_image_data_pixman_format_get_from_shm_buffer(wl_shm_buffer_get_format(td->shm_buffer));
        dst_format = src_format;

        src_ptr = wl_shm_buffer_get_data(td->shm_buffer);
        src_img = pixman_image_create_bits(src_format, w, h, (uint32_t*)src_ptr, w * 4);
        EINA_SAFETY_ON_NULL_GOTO(src_img, error_case);
     }

   if (td->transform == WL_OUTPUT_TRANSFORM_90)
     {
        c = 0, s = -1, tx = -w;
        tw = h, th = w;
     }
   else if (td->transform == WL_OUTPUT_TRANSFORM_180)
     {
        c = -1, s = 0, tx = -w, ty = -h;
        tw = w, th = h;
     }
   else if (td->transform == WL_OUTPUT_TRANSFORM_270)
     {
        c = 0, s = 1, ty = -h;
        tw = h, th = w;
     }

   transform_surface = tbm_surface_create(tw, th, tbm_surface_get_format(td->tbm_surface));
   EINA_SAFETY_ON_NULL_GOTO(transform_surface, error_case);

   tbm_surface_map(transform_surface, TBM_SURF_OPTION_READ|TBM_SURF_OPTION_WRITE, &info);
   dst_ptr = info.planes[0].ptr;

   dst_img = pixman_image_create_bits(dst_format, tw, th, (uint32_t*)dst_ptr, info.planes[0].stride);
   EINA_SAFETY_ON_NULL_GOTO(dst_img, error_case);

   pixman_f_transform_init_identity(&ft);
   pixman_f_transform_translate(&ft, NULL, tx, ty);
   pixman_f_transform_rotate(&ft, NULL, c, s);

   pixman_transform_from_pixman_f_transform(&t, &ft);
   pixman_image_set_transform(src_img, &t);

   pixman_image_composite(PIXMAN_OP_SRC, src_img, NULL, dst_img, 0, 0, 0, 0, 0, 0, w, h);

error_case:

   if (src_img) pixman_image_unref(src_img);
   if (dst_img) pixman_image_unref(dst_img);

   if (src_ptr && td->tbm_surface) tbm_surface_unmap(td->tbm_surface);
   if (dst_ptr) tbm_surface_unmap(transform_surface);

   return transform_surface;
}

static const char *
_remote_source_image_data_save(Thread_Data *td, const char *path, const char *name)
{
   struct wl_shm_buffer *shm_buffer = NULL;
   tbm_surface_h tbm_surface = NULL, transform_surface = NULL;
   int w, h, stride;
   void *ptr;
   char dest[2048], fname[2048];
   const char *dupname;
   int id = 0, ret = 0;

   EINA_SAFETY_ON_NULL_RETURN_VAL(td, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(path, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(name, NULL);

   snprintf(fname, sizeof(fname), "%s-%d", name, id);
   snprintf(dest, sizeof(dest), "%s/%s.png", path, fname);
   while (ecore_file_exists(dest))
     {
        snprintf(fname, sizeof(fname), "%s-%d", name, ++id);
        snprintf(dest, sizeof(dest), "%s/%s.png", path, fname);
     }

   shm_buffer = td->shm_buffer;
   tbm_surface = td->tbm_surface;

   if (shm_buffer)
     {
         stride = wl_shm_buffer_get_stride(shm_buffer);
         w = stride / 4;
         h = wl_shm_buffer_get_height(shm_buffer);

         transform_surface = _remote_source_image_data_transform(td, w, h);
         if (transform_surface)
           {
              tbm_surface_info_s info;
              tbm_surface_map(transform_surface, TBM_SURF_OPTION_READ|TBM_SURF_OPTION_WRITE, &info);
              ptr = info.planes[0].ptr;
           }
         else
           {
              ptr = wl_shm_buffer_get_data(shm_buffer);
              EINA_SAFETY_ON_NULL_RETURN_VAL(ptr, NULL);
           }

         dupname = strdup(fname);
         ret = tbm_surface_internal_capture_shm_buffer(ptr, w, h, stride, path, dupname, "png");

         if (transform_surface)
           {
              tbm_surface_unmap(transform_surface);
              tbm_surface_destroy(transform_surface);
           }

         free((void*)dupname);
         if (!ret)
           return NULL;
     }
   else if (tbm_surface)
     {
         transform_surface = _remote_source_image_data_transform(td, tbm_surface_get_width(tbm_surface), tbm_surface_get_height(tbm_surface));
         if (transform_surface)
           tbm_surface = transform_surface;

         RSMDBG("image save. transform_surface=%p transform=%d", td->ec->pixmap, td->ec, "SOURCE", NULL, transform_surface, td->transform);

         w = tbm_surface_get_width(tbm_surface);
         h = tbm_surface_get_height(tbm_surface);

         dupname = strdup(fname);
         ret = tbm_surface_internal_capture_buffer(tbm_surface, path, dupname, "png");

         if (transform_surface)
           tbm_surface_destroy(transform_surface);

         free((void*)dupname);
         if (!ret)
           return NULL;
     }
   else
     {
         return NULL;
     }

   return strdup(dest);
}

static void
_remote_source_save(void *data, Ecore_Thread *th)
{
   Thread_Data *td;
   E_Client *ec;
   char name[1024];
   char dest_dir[1024];
   const char *dest_path, *run_dir, *dupname, *dupdir;

   if (!(td = data)) return;

   ec = td->ec;
   if (!ec) return;
   if (ecore_thread_check(th)) return;

   if (!(run_dir = getenv("XDG_RUNTIME_DIR")))
     return;

   snprintf(dest_dir, sizeof(dest_dir), "%s/.e-img", run_dir);
   if (!ecore_file_exists(dest_dir))
     ecore_file_mkdir(dest_dir);
   dupdir = strdup(dest_dir);

   snprintf(name, sizeof(name),
            "win_%d_%u",
            ec->netwm.pid,
            e_pixmap_res_id_get(ec->pixmap));

   dupname = strdup(name);

   dest_path = _remote_source_image_data_save(td, dupdir, dupname);
   if (dest_path)
     {
        RSMDBG("IMG save success. th:%p file:%s", NULL, ec, "SOURCE", NULL, th, dest_path);
        td->image_path = eina_stringshare_add(dest_path);
        free((void*)dest_path);
     }
   else
     {
        RSMDBG("IMG save failure. th:%p file:%s", NULL, ec, "SOURCE", NULL, th, dest_path);
     }
   free((void*)dupname);
   free((void*)dupdir);
}

static void
_remote_source_save_done(void *data, Ecore_Thread *th)
{
   Thread_Data *td = data;
   E_Client *ec;
   E_Comp_Wl_Remote_Source *source = NULL;

   if (!td) return;

   ec = td->ec;
   if (!ec) goto end;

   source = _remote_source_find(ec);
   if (!source) goto end;

   RSMDBG("IMG save DONE. th:%p", NULL, ec, "SOURCE", source, th);

   if (th == source->th)
     {
        source->th = NULL;
        e_comp_wl_buffer_reference(&source->buffer_ref, NULL);

        if ((source->deleted) || (e_object_is_del(E_OBJECT(ec))))
          {
             _remote_source_destroy(source);
             goto end;
          }

        if (!td->image_path) goto end;

        RSMDBG("Source save DONE path(%s)", source->common.ec->pixmap, source->common.ec,
               "SOURCE", source, td->image_path);

        /* remove previous file */
        if ((source->image_path) && (e_util_strcmp(source->image_path, td->image_path)))
          {
             if (!e_config->hold_prev_win_img)
               {
                  RSMDBG("IMG del %s", ec->pixmap, ec, "SOURCE", source, source->image_path);
                  ecore_file_remove(source->image_path);
               }
             eina_stringshare_del(source->image_path);
          }
        source->image_path = eina_stringshare_add(td->image_path);
        _remote_source_send_image_update(source);

        ec->saved_img = EINA_TRUE;
     }
   else
     {
        RSMDBG("IMG not matched. del. src:%s td:%s", ec->pixmap, ec, "SOURCE",
               source, source->image_path, td->image_path);
        ecore_file_remove(td->image_path);
     }
end:
   if (ec)
     e_object_unref(E_OBJECT(ec));
   if (td->tbm_surface)
     tbm_surface_internal_unref(td->tbm_surface);
   if (td->shm_pool)
     wl_shm_pool_unref(td->shm_pool);

   eina_stringshare_del(td->image_path);
   E_FREE(td);
}

static void
_remote_source_save_cancel(void *data, Ecore_Thread *th)
{
   Thread_Data *td = data;
   E_Client *ec;
   E_Comp_Wl_Remote_Source *source = NULL;
   Eina_Bool del = EINA_FALSE;

   if (!td) return;

   ec = td->ec;
   if (!ec) goto end;

   source = _remote_source_find(ec);
   if (!source) goto end;

   RSMDBG("IMG save CANCELED. th:%p %s", source->common.ec->pixmap, source->common.ec,
          "SOURCE", source, th, td->image_path);

   if (th == source->th)
     {
        source->th = NULL;
        e_comp_wl_buffer_reference(&source->buffer_ref, NULL);
     }

   if (!e_config->hold_prev_win_img)
     {
        if (td->image_path)
          {
             RSMDBG("IMG del %s", NULL, source->common.ec, "SOURCE", source, td->image_path);
             ecore_file_remove(td->image_path);
          }
     }

   if (source->deleted)
     {
        _remote_source_destroy(source);
        del = EINA_TRUE;
     }
end:
   if (ec)
     e_object_unref(E_OBJECT(ec));
   if (td->tbm_surface)
     tbm_surface_internal_unref(td->tbm_surface);
   if (td->shm_pool)
     wl_shm_pool_unref(td->shm_pool);

   eina_stringshare_del(td->image_path);
   E_FREE(td);

   if ((!del) && (source))
     {
        if (source->defer_img_save)
          {
             RSMDBG("IMG save retry", NULL, source->common.ec, "SOURCE", source);
             _remote_source_save_start(source);
             source->defer_img_save = EINA_FALSE;
          }
     }
}

static void
_remote_source_save_start(E_Comp_Wl_Remote_Source *source)
{
   E_Client *ec;
   E_Comp_Wl_Buffer *buffer = NULL;
   Thread_Data *td;
   struct wl_shm_buffer *shm_buffer;
   struct wl_shm_pool *shm_pool;
   tbm_surface_h tbm_surface;

   if (!(ec = source->common.ec)) return;
   if (!(buffer = e_pixmap_resource_get(ec->pixmap))) return;
   if (!e_config->save_win_buffer) return;

   td = E_NEW(Thread_Data, 1);
   if (!td) return;

   e_object_ref(E_OBJECT(ec));
   td->ec = ec;

   if (source->th)
     {
        RSMDBG("IMG prev save is cancelled. th:%p",
               source->common.ec->pixmap, source->common.ec->pixmap, "SOURCE",
               source, source->th);
        ecore_thread_cancel(source->th);
     }

   if (ec->comp_data)
     {
        E_Comp_Wl_Buffer_Viewport *vp = &ec->comp_data->scaler.buffer_viewport;
        td->transform = vp->buffer.transform;
     }
   else
     td->transform = 0;

   e_comp_wl_buffer_reference(&source->buffer_ref, buffer);
   switch (buffer->type)
     {
      case E_COMP_WL_BUFFER_TYPE_SHM:
         shm_buffer = wl_shm_buffer_get(buffer->resource);
         if (!shm_buffer) goto end;

         shm_pool = wl_shm_buffer_ref_pool(shm_buffer);
         if (!shm_pool) goto end;

         td->shm_buffer = shm_buffer;
         td->shm_pool = shm_pool;
         break;
      case E_COMP_WL_BUFFER_TYPE_NATIVE:
      case E_COMP_WL_BUFFER_TYPE_VIDEO:
         tbm_surface = wayland_tbm_server_get_surface(e_comp_wl->tbm.server, buffer->resource);
         if (!tbm_surface) goto end;

         tbm_surface_internal_ref(tbm_surface);
         td->tbm_surface = tbm_surface;
         break;
      case E_COMP_WL_BUFFER_TYPE_TBM:
         tbm_surface = buffer->tbm_surface;
         if (!tbm_surface) goto end;

         tbm_surface_internal_ref(tbm_surface);
         td->tbm_surface = tbm_surface;
         break;
      default:
         goto end;
     }

   source->th = ecore_thread_run(_remote_source_save,
                                 _remote_source_save_done,
                                 _remote_source_save_cancel,
                                 td);
   RSMDBG("IMG save START. th:%p", ec->pixmap, ec, "SOURCE", source, source->th);
   return;
end:
   e_comp_wl_buffer_reference(&source->buffer_ref, NULL);
   e_object_unref(E_OBJECT(ec));
   E_FREE(td);
}

static void
_remote_source_offscreen_set(E_Comp_Wl_Remote_Source *source, Eina_Bool set)
{
   EINA_SAFETY_ON_NULL_RETURN(source);
   if (set)
     {
        source->offscreen_ref++;
        RSMDBG("Set offscreen offscreen_ref:%d",
               source->common.ec->pixmap, source->common.ec,
               "SOURCE", source, source->offscreen_ref);

        if (source->offscreen_ref == 1)
          {
             _remote_surface_ignore_output_transform_send(&source->common);
             source->common.is_offscreen = EINA_TRUE;

             source->common.ec->exp_iconify.not_raise = 1;
             if (!source->common.ec->exp_iconify.by_client)
               e_policy_wl_iconify_state_change_send(source->common.ec, 0);

             RSMINF("Un-Set ICONIFY BY Remote_Surface", source->common.ec->pixmap, source->common.ec,
                    "SOURCE", source);
             e_client_uniconify(source->common.ec);

             source->common.ec->exp_iconify.by_client = 0;
             source->common.ec->exp_iconify.skip_by_remote = 1;

             EC_CHANGED(source->common.ec);
          }
     }
   else
     {
        if (!source->common.is_offscreen)
          return;

        source->offscreen_ref--;
        RSMDBG("Unset offscreen offscreen_ref:%d",
               source->common.ec->pixmap, source->common.ec,
               "SOURCE", source, source->offscreen_ref);

        if (source->offscreen_ref == 0)
          {
             _remote_surface_ignore_output_transform_send(&source->common);
             source->common.is_offscreen = EINA_FALSE;
             source->common.ec->exp_iconify.skip_by_remote = 0;
             EC_CHANGED(source->common.ec);
          }
     }
}

static void
_remote_surface_region_clear(E_Comp_Wl_Remote_Surface *remote_surface)
{
   Eina_List *l;
   E_Comp_Wl_Remote_Region *region;
   if (!remote_surface) return;

   EINA_LIST_FOREACH(remote_surface->regions, l, region)
     {
        _remote_region_mirror_clear(region);
     }
}

static void
_remote_surface_client_set(E_Client *ec, Eina_Bool set)
{
   if (!ec) return;
   if ((e_object_is_del(E_OBJECT(ec)))) return;

   ec->remote_surface.consumer = set;
}

static void
_remote_region_cb_mirror_del(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
   E_Comp_Wl_Remote_Region *region = data;
   if (!region->mirror) return;

   region->mirror = NULL;
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

   if (region->mirror)
     {
        evas_object_event_callback_del_full(region->mirror, EVAS_CALLBACK_DEL, _remote_region_cb_mirror_del, region);
        evas_object_del(region->mirror);
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
     eina_hash_del(_rsm->provider_hash, &provider->common.ec, provider);

   EINA_LIST_FREE(provider->common.surfaces, remote_surface)
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

   _remote_provider_client_set(provider->common.ec, EINA_FALSE);
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

   if (provider->common.is_offscreen == offscreen) return;
   _remote_provider_offscreen_set(provider, offscreen);
}

static void
_remote_provider_cb_input_event_filter_set(struct wl_client *client EINA_UNUSED, struct wl_resource *provider_resource, uint32_t event_filter)
{
   E_Comp_Wl_Remote_Provider *provider;
   E_Comp_Wl_Remote_Surface *remote_surface;
   Eina_List *l;

   provider = wl_resource_get_user_data(provider_resource);
   if (!provider) return;

   provider->input_event_filter = event_filter;
   RSMDBG("set input event filter 0x%08x",
          provider->common.ec->pixmap, provider->common.ec,
          "PROVIDER", provider, event_filter);

   if (!event_filter) return;

   EINA_LIST_FOREACH(provider->common.surfaces, l, remote_surface)
     {
        if (remote_surface->version >= TIZEN_REMOTE_SURFACE_INPUT_EVENT_FILTER_SINCE_VERSION)
          tizen_remote_surface_send_input_event_filter(remote_surface->resource, event_filter);
     }
}

static const struct tizen_remote_surface_provider_interface _remote_provider_interface =
{
   _remote_provider_cb_destroy,
   _remote_provider_cb_offscreen_set,
   _remote_provider_cb_input_event_filter_set,
};

static void
_remote_surface_cb_tbm_destroy(struct wl_listener *listener, void *data)
{
   E_Comp_Wl_Remote_Surface *remote_surface;

   remote_surface = container_of(listener, E_Comp_Wl_Remote_Surface, tbm_destroy_listener);
   if (!remote_surface) return;

   remote_surface->wl_tbm = NULL;
}

static void
_remote_surface_cb_resource_destroy(struct wl_resource *resource)
{
   E_Comp_Wl_Remote_Surface *remote_surface;
   E_Comp_Wl_Remote_Provider *provider;
   E_Comp_Wl_Remote_Source *source;
   E_Comp_Wl_Remote_Region *region;

   remote_surface = wl_resource_get_user_data(resource);
   if (!remote_surface) return;

   provider = remote_surface->provider;
   if (provider)
     {
        _remote_surface_visible_set(remote_surface, EINA_FALSE);
        if (provider->onscreen_parent == remote_surface)
          _remote_provider_onscreen_parent_set(provider, NULL);

        provider->common.surfaces = eina_list_remove(provider->common.surfaces,
                                              remote_surface);
        remote_surface->provider = NULL;
     }

   source = remote_surface->source;
   if (source)
     {
        source->common.surfaces = eina_list_remove(source->common.surfaces, remote_surface);
        remote_surface->source = NULL;
     }

   EINA_LIST_FREE(remote_surface->regions, region)
     {
        region->remote_surface = NULL;
        wl_resource_destroy(region->resource);
     }

   if (remote_surface->bind_ec)
     _remote_surface_bind_client(remote_surface, NULL);
   if (remote_surface->owner)
     {
        eina_hash_del_by_key(_rsm->surface_hash, &remote_surface->owner);
        _remote_surface_client_set(remote_surface->owner, EINA_FALSE);
     }

   if (remote_surface->wl_tbm)
     wl_list_remove(&remote_surface->tbm_destroy_listener.link);

   if (provider)
     _remote_surface_ignore_output_transform_send(&provider->common);
   if (source)
     _remote_surface_ignore_output_transform_send(&source->common);

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
   E_Comp_Wl_Remote_Buffer *remote_buffer;
   struct wl_resource *remote_buffer_resource;

   EINA_SAFETY_ON_NULL_RETURN(_rsm);

   remote_surface = wl_resource_get_user_data(resource);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface);
   EINA_SAFETY_ON_FALSE_RETURN(remote_surface->valid);

   if (remote_surface->provider)
     {
        EINA_SAFETY_ON_NULL_RETURN(remote_surface->provider->common.ec);

        RSMINF("Redirect surface provider:%p(ec:%p)",
               NULL, NULL,
               "SURFACE", remote_surface,
               remote_surface->provider, remote_surface->provider->common.ec);

        remote_surface->redirect = EINA_TRUE;

        /* Send input event filter of provider */
        if ((remote_surface->provider->input_event_filter) &&
            (remote_surface->version >= TIZEN_REMOTE_SURFACE_INPUT_EVENT_FILTER_SINCE_VERSION))
          tizen_remote_surface_send_input_event_filter(resource,
                                                       remote_surface->provider->input_event_filter);

        buffer = e_pixmap_resource_get(remote_surface->provider->common.ec->pixmap);
        EINA_SAFETY_ON_NULL_RETURN(buffer);

        remote_buffer_resource = e_comp_wl_tbm_remote_buffer_get(remote_surface->wl_tbm, buffer->resource);
        EINA_SAFETY_ON_NULL_RETURN(remote_buffer_resource);

        remote_buffer = _e_comp_wl_remote_buffer_get(remote_buffer_resource);
        EINA_SAFETY_ON_NULL_RETURN(remote_buffer);

        _remote_surface_changed_buff_protocol_send(remote_surface,
                                                   TIZEN_REMOTE_SURFACE_BUFFER_TYPE_TBM,
                                                   remote_buffer,
                                                   _rsm->dummy_fd,
                                                   0,
                                                   EINA_TRUE,
                                                   buffer);
     }
   else if (remote_surface->source)
     {
        EINA_SAFETY_ON_NULL_RETURN(remote_surface->source->common.ec);
        RSMINF("Redirect surface source:%p(ec:%p)",
               NULL, NULL,
               "SURFACE", remote_surface,
               remote_surface->source, remote_surface->source->common.ec);

        if (remote_surface->version < TIZEN_REMOTE_SURFACE_CHANGED_BUFFER_SINCE_VERSION)
          return;

        remote_surface->redirect = EINA_TRUE;

        if ((buffer = e_pixmap_resource_get(remote_surface->source->common.ec->pixmap)))
          {

             remote_buffer_resource = e_comp_wl_tbm_remote_buffer_get(remote_surface->wl_tbm, buffer->resource);
             EINA_SAFETY_ON_NULL_RETURN(remote_buffer_resource);

             remote_buffer = _e_comp_wl_remote_buffer_get(remote_buffer_resource);
             EINA_SAFETY_ON_NULL_RETURN(remote_buffer);

             _remote_surface_changed_buff_protocol_send(remote_surface,
                                                        TIZEN_REMOTE_SURFACE_BUFFER_TYPE_TBM,
                                                        remote_buffer,
                                                        _rsm->dummy_fd,
                                                        0,
                                                        EINA_TRUE,
                                                        buffer);
          }
        else
          {
             _remote_source_send_image_update(remote_surface->source);
          }
     }
}

static void
_remote_surface_cb_unredirect(struct wl_client *client, struct wl_resource *resource)
{
   E_Comp_Wl_Remote_Surface *remote_surface;

   remote_surface = wl_resource_get_user_data(resource);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface);
   EINA_SAFETY_ON_FALSE_RETURN(remote_surface->valid);

   remote_surface->redirect = EINA_FALSE;
//   _remote_surface_visible_set(remote_surface, EINA_FALSE);

   RSMINF("Unredirect surface provider:%p(ec:%p)",
          NULL, NULL,
          "SURFACE", remote_surface,
          remote_surface->provider, remote_surface->provider? remote_surface->provider->common.ec: NULL);
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
   EINA_SAFETY_ON_FALSE_RETURN(remote_surface->valid);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface->provider);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface->provider->common.ec);

   provider = remote_surface->provider;
   ec = provider->common.ec;

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
           case TIZEN_REMOTE_SURFACE_EVENT_TYPE_MOUSE_IN:
              e_client_mouse_in_send(ec,
                                     x, y,
                                     edev,
                                     time);
              break;
           case TIZEN_REMOTE_SURFACE_EVENT_TYPE_MOUSE_OUT:
              e_client_mouse_out_send(ec,
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

static void
_remote_surface_cb_mouse_wheel_transfer(struct wl_client *client, struct wl_resource *resource, uint32_t direction, int32_t z, uint32_t clas, uint32_t subclas, const char *identifier, uint32_t time)
{
   E_Comp_Wl_Remote_Provider *provider;
   E_Comp_Wl_Remote_Surface *remote_surface;
   E_Client *ec;

   Ecore_Device *edev = NULL;

   remote_surface = wl_resource_get_user_data(resource);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface);
   EINA_SAFETY_ON_FALSE_RETURN(remote_surface->valid);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface->provider);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface->provider->common.ec);

   provider = remote_surface->provider;
   ec = provider->common.ec;

   if (e_object_is_del(E_OBJECT(ec))) return;

   /* identify class */
   edev = _device_get_by_identifier(identifier);

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
   EINA_SAFETY_ON_FALSE_RETURN(remote_surface->valid);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface->provider);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface->provider->common.ec);

   provider = remote_surface->provider;
   ec = provider->common.ec;

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

static void
_remote_surface_cb_touch_cancel_transfer(struct wl_client *client, struct wl_resource *resource)
{
   E_Comp_Wl_Remote_Provider *provider;
   E_Comp_Wl_Remote_Surface *remote_surface;
   E_Client *ec;

   remote_surface = wl_resource_get_user_data(resource);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface);
   EINA_SAFETY_ON_FALSE_RETURN(remote_surface->valid);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface->provider);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface->provider->common.ec);

   provider = remote_surface->provider;
   ec = provider->common.ec;

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
   EINA_SAFETY_ON_FALSE_RETURN(remote_surface->valid);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface->provider);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface->provider->common.ec);

   provider = remote_surface->provider;
   ec = provider->common.ec;

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

static void
_remote_surface_cb_visibility_transfer(struct wl_client *client, struct wl_resource *resource, uint32_t visibility_type)
{
   E_Comp_Wl_Remote_Surface *remote_surface;

   remote_surface = wl_resource_get_user_data(resource);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface);
   EINA_SAFETY_ON_FALSE_RETURN(remote_surface->valid);

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
   EINA_SAFETY_ON_FALSE_RETURN(remote_surface->valid);

   if (surface_resource)
     owner = wl_resource_get_user_data(surface_resource);

   if (remote_surface->owner)
     _remote_surface_client_set(remote_surface->owner, EINA_FALSE);

   remote_surface->owner = owner;
   eina_hash_del_by_data(_rsm->surface_hash, remote_surface);

   if ((owner) && (remote_surface->provider))
     {
        eina_hash_add(_rsm->surface_hash, &owner, remote_surface);
        _remote_surface_client_set(remote_surface->owner, EINA_TRUE);
     }

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
   EINA_SAFETY_ON_FALSE_RETURN(remote_surface->valid);

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
   if (!region)
     {
        wl_client_post_no_memory(client);
        wl_resource_destroy(resource);
        return;
     }
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

static void
_remote_surface_cb_release(struct wl_client *client, struct wl_resource *resource, struct wl_resource *remote_buffer_resource)
{
   E_Comp_Wl_Remote_Surface *remote_surface;
   E_Comp_Wl_Remote_Buffer *remote_buffer;

   remote_surface = wl_resource_get_user_data(resource);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface);
   EINA_SAFETY_ON_FALSE_RETURN(remote_surface->valid);

   remote_buffer = _e_comp_wl_remote_buffer_get(remote_buffer_resource);
   EINA_SAFETY_ON_NULL_RETURN(remote_buffer);

   if (remote_surface->version >= 2)
     {
        E_Comp_Wl_Buffer *buf = NULL;

        if (remote_buffer->ref.buffer &&
            remote_buffer->ref.buffer->resource)
           buf = remote_buffer->ref.buffer;

        e_comp_wl_buffer_reference(&remote_buffer->ref, NULL);

        /*Send release event to provider*/
        if (remote_surface->provider &&
            remote_surface->provider->buffer_mode &&
            buf && buf->busy == 0)
          {
             if (remote_surface->provider->buffer_mode == 1 ||
                 (remote_surface->provider->buffer_mode == 2 &&
                  remote_surface->provider->vis_ref == 0))
               {
                  E_Client *ec = remote_surface->provider->common.ec;
                  e_pixmap_buffer_clear(ec->pixmap, EINA_TRUE);
               }
          }
     }
}

static void
_remote_surface_cb_remote_render_set(struct wl_client *client, struct wl_resource *resource, uint32_t set)
{
   E_Comp_Wl_Remote_Surface *remote_surface;
   E_Comp_Wl_Remote_Source *source = NULL;

   remote_surface = wl_resource_get_user_data(resource);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface);
   EINA_SAFETY_ON_FALSE_RETURN(remote_surface->valid);

   source = remote_surface->source;
   if (!source) return;

   if (remote_surface->remote_render == set)
     return;

   remote_surface->remote_render = set;
   _remote_source_offscreen_set(source, set);
}

static void
_remote_surface_cb_changed_buffer_event_filter_set(struct wl_client *client,
                                                   struct wl_resource *rsurf_res,
                                                   enum tizen_remote_surface_changed_buffer_event_filter filter)
{
   E_Comp_Wl_Remote_Surface *rs;

   rs = wl_resource_get_user_data(rsurf_res);
   EINA_SAFETY_ON_NULL_RETURN(rs);
   EINA_SAFETY_ON_FALSE_RETURN(rs->valid);

   if (filter == TIZEN_REMOTE_SURFACE_CHANGED_BUFFER_EVENT_FILTER_NONE)
     rs->changed_buff_ev_filter.use = EINA_FALSE;
   else
     rs->changed_buff_ev_filter.use = EINA_TRUE;

   rs->changed_buff_ev_filter.filter = filter;

   RSMINF("use:%d filter:%u", NULL, NULL, "SURFACE", rs,
          rs->changed_buff_ev_filter.use,
          rs->changed_buff_ev_filter.filter);
}

static void
_remote_surface_cb_curr_buff_get(struct wl_client *client,
                                 struct wl_resource *rsurf_res,
                                 enum tizen_remote_surface_buffer_type buff_type,
                                 uint32_t req_serial)
{
   E_Comp_Wl_Remote_Surface *rs;
   Eina_Bool res;

   rs = wl_resource_get_user_data(rsurf_res);
   EINA_SAFETY_ON_NULL_RETURN(rs);
   EINA_SAFETY_ON_FALSE_RETURN(rs->valid);

   RSMINF("buff_type:%u req_serial:%u", NULL, NULL, "SURFACE", rs,
          buff_type, req_serial);

   /* compare buffer type with filter value of changed_buffer event */
   res = _remote_surface_changed_buff_ev_filter_check(rs, buff_type);
   EINA_SAFETY_ON_FALSE_RETURN(res);

   /* setup request info before sending current buffer */
   rs->req_curr_buff.set = EINA_TRUE;
   rs->req_curr_buff.type = buff_type;
   rs->req_curr_buff.serial = req_serial;

   RSMINF("buff_type:%u req_serial:%u", NULL, NULL, "SURFACE", rs,
          buff_type, req_serial);

   /* send current buffer to the requesting client */
   res = _remote_surface_buff_send(rs);
   EINA_SAFETY_ON_FALSE_GOTO(res, err_cleanup);

   return;

err_cleanup:
   rs->req_curr_buff.set = EINA_FALSE;
   rs->req_curr_buff.type = 0;
   rs->req_curr_buff.serial = 0;
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
   _remote_surface_cb_release,
   _remote_surface_cb_remote_render_set,
   _remote_surface_cb_changed_buffer_event_filter_set,
   _remote_surface_cb_curr_buff_get,
};

static void
_remote_manager_cb_provider_create(struct wl_client *client, struct wl_resource *res_remote_manager, uint32_t id, struct wl_resource *surface_resource)
{
   struct wl_resource *resource;
   E_Comp_Wl_Remote_Provider *provider;
   E_Client *ec;
   uint32_t res_id;
   int version;

   EINA_SAFETY_ON_NULL_RETURN(_rsm);

   ec = wl_resource_get_user_data(surface_resource);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   if (e_object_is_del(E_OBJECT(ec))) return;

   version = wl_resource_get_version(res_remote_manager);
   resource = wl_resource_create(client,
                                 &tizen_remote_surface_provider_interface,
                                 version, id);
   if (!resource)
     {
        ERR("Could not create tizen remote surface provider resource: %m");
        wl_client_post_no_memory(client);
        return;
     }

   provider = E_NEW(E_Comp_Wl_Remote_Provider, 1);
   if (!provider)
     {
        wl_client_post_no_memory(client);
        wl_resource_destroy(resource);
        return;
     }
   provider->common.ec = ec;
   provider->resource = resource;

   wl_resource_set_implementation(resource,
                                  &_remote_provider_interface,
                                  provider,
                                  _remote_provider_cb_resource_destroy);

   eina_hash_add(_rsm->provider_hash, &ec, provider);

   RSMINF("Created resource(%p)",
          ec->pixmap, ec,
          "PROVIDER", provider, resource);

   _remote_provider_client_set(ec, EINA_TRUE);
   _remote_provider_offscreen_set(provider, EINA_TRUE);

   /* send resource id */
   res_id = e_pixmap_res_id_get(ec->pixmap);
   tizen_remote_surface_provider_send_resource_id(resource, res_id);

   /* set buffer mode */
   provider->buffer_mode = e_config->rsm_buffer_release_mode;
}

static void
_remote_manager_cb_surface_create(struct wl_client *client,
                                  struct wl_resource *res_remote_manager,
                                  uint32_t id,
                                  uint32_t res_id,
                                  struct wl_resource *wl_tbm)
{
   struct wl_resource *resource;
   E_Comp_Wl_Remote_Surface *remote_surface;
   E_Comp_Wl_Remote_Provider *provider = NULL;
   E_Comp_Wl_Remote_Source *source = NULL;
   E_Client *ec;
   int version;
   pid_t pid = 0;
   uid_t uid = 0;
   Eina_Bool res;

   EINA_SAFETY_ON_NULL_RETURN(_rsm);

   version = wl_resource_get_version(res_remote_manager);
   resource = wl_resource_create(client,
                                 &tizen_remote_surface_interface,
                                 version, id);
   if (!resource)
     {
        ERR("Could not create tizen remote surface resource: %m");
        wl_client_post_no_memory(client);
        return;
     }

   remote_surface = E_NEW(E_Comp_Wl_Remote_Surface, 1);
   if (!remote_surface)
     {
        wl_client_post_no_memory(client);
        wl_resource_destroy(resource);
        return;
     }
   remote_surface->resource = resource;
   remote_surface->version = wl_resource_get_version(resource);
   remote_surface->redirect = EINA_FALSE;
   remote_surface->valid = EINA_FALSE;

   wl_resource_set_implementation(resource,
                                  &_remote_surface_interface,
                                  remote_surface,
                                  _remote_surface_cb_resource_destroy);

   ec = e_pixmap_find_client_by_res_id(res_id);
   if (!ec)
     {
        ERR("Could not find client by res_id(%u)", res_id);
        goto fail;
     }

   if (!wl_tbm)
     {
        ERR("wayland_tbm resource is NULL");
        goto fail;
     }

   provider = _remote_provider_find(ec);
   if (!provider)
     {
        /* check the privilege for the client which wants to be the remote surface of normal UI client */
        wl_client_get_credentials(client, &pid, &uid, NULL);
        res = e_security_privilege_check(pid, uid, E_PRIVILEGE_INTERNAL_DEFAULT_PLATFORM);
        if (!res)
          {
             ELOGF("TRS",
                   "Privilege Check Failed! DENY creating tizen_remote_surface pid:%d",
                   NULL, NULL, pid);
             goto fail;
          }

        if (version >= TIZEN_REMOTE_SURFACE_CHANGED_BUFFER_SINCE_VERSION)
          {
             if (ec->comp_data->sub.data)
               {
                  ERR("Subsurface could not be source client");
                  goto fail;
               }

             /* if passed */
             source = _remote_source_find(ec);
             if (!source)
               {
                  source = E_NEW(E_Comp_Wl_Remote_Source, 1);
                  if (!source) goto fail;

                  source->common.ec = ec;
                  eina_hash_add(_rsm->source_hash, &ec, source);
               }
          }
        else
          {
             ERR("Could not support tizen_remote_surface to client :%d", pid);
             goto fail;
          }
     }

   remote_surface->provider = provider;
   remote_surface->source = source;
   remote_surface->wl_tbm = wl_tbm;

   /* Add destroy listener for wl_tbm resource */
   remote_surface->tbm_destroy_listener.notify = _remote_surface_cb_tbm_destroy;
   wl_resource_add_destroy_listener((struct wl_resource *)wl_tbm, &remote_surface->tbm_destroy_listener);

   if (provider)
     provider->common.surfaces = eina_list_append(provider->common.surfaces, remote_surface);
   else if (source)
     source->common.surfaces = eina_list_append(source->common.surfaces, remote_surface);

   RSMINF("Created resource(%p) ec(%p) provider(%p) source(%p) version(%d)",
          NULL, NULL,
          "SURFACE", remote_surface, resource, ec, provider, source, remote_surface->version);

   remote_surface->valid = EINA_TRUE;

   if (provider)
     _remote_surface_ignore_output_transform_send(&provider->common);
   else if (source)
     _remote_surface_ignore_output_transform_send(&source->common);

   return;

fail:
   tizen_remote_surface_send_missing(resource);
}


static void
_remote_manager_cb_surface_bind(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface_resource, struct wl_resource *remote_surface_resource)
{
   E_Comp_Wl_Remote_Surface *remote_surface;
   E_Comp_Wl_Remote_Provider *provider;
   E_Client *ec = NULL;

   remote_surface = wl_resource_get_user_data(remote_surface_resource);
   EINA_SAFETY_ON_NULL_RETURN(remote_surface);
   EINA_SAFETY_ON_FALSE_RETURN(remote_surface->valid);

   provider = remote_surface->provider;
   if (!provider) return;

   if (surface_resource)
     ec = wl_resource_get_user_data(surface_resource);

   _remote_surface_bind_client(remote_surface, ec);

   _remote_surface_ignore_output_transform_send(&provider->common);
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

static Eina_Bool
_image_save_type_check(E_Client *ec)
{
   if (e_policy_client_is_lockscreen(ec) ||
       e_policy_client_is_home_screen(ec) ||
       e_policy_client_is_quickpanel(ec) ||
       e_policy_client_is_volume(ec) ||
       e_policy_client_is_volume_tv(ec) ||
       e_policy_client_is_floating(ec) ||
       e_policy_client_is_cursor(ec) ||
       e_policy_client_is_subsurface(ec) ||
       e_policy_client_is_cbhm(ec) ||
       e_policy_client_is_toast_popup(ec) ||
       e_policy_client_is_keyboard(ec) ||
       e_policy_client_is_keyboard_sub(ec) ||
       e_policy_client_is_keyboard_magnifier(ec))
     return EINA_FALSE;

   return EINA_TRUE;
}

static void
_e_comp_wl_remote_cb_client_iconify(void *data, E_Client *ec)
{
   E_Comp_Wl_Remote_Source *source;

   if (!(source = _remote_source_find(ec)))
     {
        if (ec->ignored) return;
        if (!_image_save_type_check(ec)) return;

        source = E_NEW(E_Comp_Wl_Remote_Source, 1);
        EINA_SAFETY_ON_NULL_RETURN(source);

        source->common.ec = ec;
        eina_hash_add(_rsm->source_hash, &ec, source);
     }

   _remote_source_save_start(source);
}

static void
_e_comp_wl_remote_cb_client_del(void *data, E_Client *ec)
{
   E_Comp_Wl_Remote_Provider *provider;
   E_Comp_Wl_Remote_Source *source;
   E_Comp_Wl_Remote_Surface *remote_surface;

   if ((provider = eina_hash_find(_rsm->provider_hash, &ec)))
     {
        eina_hash_del(_rsm->provider_hash, &ec, provider);
        EINA_LIST_FREE(provider->common.surfaces, remote_surface)
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

   if ((source = _remote_source_find(ec)))
     {
        _remote_source_destroy(source);
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
_e_comp_wl_remote_buffer_cb_destroy(struct wl_listener *listener, void *data)
{
   E_Comp_Wl_Remote_Buffer *remote_buffer;

   remote_buffer = container_of(listener, E_Comp_Wl_Remote_Buffer, destroy_listener);
   if (!remote_buffer) return;

   e_comp_wl_buffer_reference(&remote_buffer->ref, NULL);
   free(remote_buffer);
}

static E_Comp_Wl_Remote_Buffer *
_e_comp_wl_remote_buffer_get(struct wl_resource *remote_buffer_resource)
{
   E_Comp_Wl_Remote_Buffer *remote_buffer = NULL;
   struct wl_listener *listener;

   listener = wl_resource_get_destroy_listener(remote_buffer_resource, _e_comp_wl_remote_buffer_cb_destroy);
   if (listener)
     return container_of(listener, E_Comp_Wl_Remote_Buffer, destroy_listener);

   if (!(remote_buffer = E_NEW(E_Comp_Wl_Remote_Buffer, 1))) return NULL;

   remote_buffer->resource = remote_buffer_resource;
   remote_buffer->destroy_listener.notify = _e_comp_wl_remote_buffer_cb_destroy;
   wl_resource_add_destroy_listener(remote_buffer->resource, &remote_buffer->destroy_listener);

   return remote_buffer;
}

static void
_e_comp_wl_remote_surface_source_update(E_Comp_Wl_Remote_Source *source, E_Comp_Wl_Buffer *buffer)
{
   E_Comp_Wl_Remote_Surface *remote_surface;
   E_Comp_Wl_Remote_Buffer *remote_buffer;
   struct wl_resource *remote_buffer_resource;
   Eina_List *l;

   if ((!source) || (!buffer)) return;

   if (source->th)
     {
        RSMDBG("IMG save is cancelled. th:%p Save job will be triggered later.",
               source->common.ec->pixmap, source->common.ec, "SOURCE",
               source, source->th);
        ecore_thread_cancel(source->th);
        source->defer_img_save = EINA_TRUE;
     }

   EINA_LIST_FOREACH(source->common.surfaces, l, remote_surface)
     {
        if (remote_surface->version < TIZEN_REMOTE_SURFACE_CHANGED_BUFFER_SINCE_VERSION)
          continue;

        if (!remote_surface->redirect) continue;

        remote_buffer_resource = e_comp_wl_tbm_remote_buffer_get(remote_surface->wl_tbm, buffer->resource);
        if (!remote_buffer_resource) continue;

        remote_buffer = _e_comp_wl_remote_buffer_get(remote_buffer_resource);
        if (!remote_buffer) continue;

        _remote_surface_changed_buff_protocol_send(remote_surface,
                                                   TIZEN_REMOTE_SURFACE_BUFFER_TYPE_TBM,
                                                   remote_buffer,
                                                   _rsm->dummy_fd,
                                                   0,
                                                   EINA_TRUE,
                                                   buffer);
     }
}

static int
_e_comp_wl_remote_surface_dummy_fd_get(void)
{
   int fd = 0, blen = 0, len = 0;
   const char *path;
   char tmp[PATH_MAX];

   blen = sizeof(tmp) - 1;

   if (!(path = getenv("XDG_RUNTIME_DIR")))
     return -1;

   len = strlen(path);
   if (len < blen)
     {
        strncpy(tmp, path, len + 1);
        strncat(tmp, "/enlightenment_rsm_dummy_fdXXXXXX", 34);
     }
   else
     return -1;

   if ((fd = mkstemp(tmp)) < 0)
     return -1;

   unlink(tmp);

   return fd;
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
_e_comp_wl_remote_surface_state_commit(E_Client *ec, E_Comp_Wl_Surface_State *state)
{
   E_Comp_Wl_Remote_Provider *provider;
   E_Comp_Wl_Remote_Source *source;
   E_Comp_Wl_Remote_Surface *surface;
   struct wl_resource *cb;
   Eina_Rectangle *dmg;
   int x = 0, y = 0, sx = 0, sy = 0;
   E_Comp_Wl_Buffer *buffer;
   E_Comp_Wl_Remote_Buffer *remote_buffer;
   struct wl_resource *remote_buffer_resource;
   Eina_List *l, *ll;

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
        if ((provider = _remote_provider_find(ec)))
          {
             EINA_LIST_FOREACH(provider->common.surfaces, l, surface)
               {
                  remote_buffer_resource = e_comp_wl_tbm_remote_buffer_get(surface->wl_tbm, buffer->resource);
                  if (!remote_buffer_resource) continue;

                  remote_buffer = _e_comp_wl_remote_buffer_get(remote_buffer_resource);
                  if (!remote_buffer) continue;

                  if (!surface->redirect) continue;
                  if (surface->bind_ec)
                    {
                       E_Comp_Wl_Buffer *buffer;

                       buffer = e_comp_wl_buffer_get(remote_buffer->resource, surface->bind_ec);
                       _e_comp_wl_remote_surface_state_buffer_set(&surface->bind_ec->comp_data->pending, buffer);
                       surface->bind_ec->comp_data->pending.sx = sx;
                       surface->bind_ec->comp_data->pending.sy = sy;
                       surface->bind_ec->comp_data->pending.new_attach = EINA_TRUE;

                       e_comp_wl_surface_commit(surface->bind_ec);

                       /* need to prepare hwc whenever buffer changed */
                       e_comp_render_queue();
                    }
                  else
                    {
                       _remote_surface_changed_buff_protocol_send(surface,
                                                                  TIZEN_REMOTE_SURFACE_BUFFER_TYPE_TBM,
                                                                  remote_buffer,
                                                                  _rsm->dummy_fd,
                                                                  0,
                                                                  EINA_TRUE,
                                                                  buffer);
                    }
               }
          }
        else if ((source = _remote_source_find(ec)))
          {
             _e_comp_wl_remote_surface_source_update(source, buffer);
          }

        /* send frame done */
        e_pixmap_image_clear(ec->pixmap, 1);
     }
}

static Eina_Bool
_e_comp_wl_remote_surface_subsurface_commit(E_Comp_Wl_Remote_Provider *parent_provider,
                                            E_Client *ec)
{
   E_Comp_Wl_Subsurf_Data *sdata;
   E_Comp_Wl_Remote_Surface *onscreen_parent;
   Eina_List *l;
   Eina_Rectangle *rect;
   int fx, fy, fw, fh;
   int x, y, w, h;
   Eina_Bool first_skip = EINA_TRUE;
   E_Comp_Wl_Buffer *buffer;

   if (!e_comp_wl_subsurface_commit(ec)) return EINA_FALSE;

   buffer = e_pixmap_resource_get(ec->pixmap);
   if (!buffer) return EINA_TRUE;

   if (buffer->type != E_COMP_WL_BUFFER_TYPE_SHM) return EINA_TRUE;

   /* TODO : store and use multiple onscreen_parent for geometry calculation */
   onscreen_parent = parent_provider->onscreen_parent;
   if (!onscreen_parent) return EINA_TRUE;

   if (!evas_object_visible_get(ec->frame)) return EINA_TRUE;

   sdata = ec->comp_data->sub.data;
   evas_object_geometry_get(ec->frame, &fx, &fy, &fw, &fh);

   EINA_LIST_FOREACH(parent_provider->common.ec->comp_data->remote_surface.regions, l, rect)
     {
        E_Comp_Wl_Remote_Region *region;

        region = container_of(rect, E_Comp_Wl_Remote_Region, geometry);

        x = sdata->position.x + rect->x;
        y = sdata->position.y + rect->y;
        if (onscreen_parent->owner)
          {
             x += onscreen_parent->owner->x;
             y += onscreen_parent->owner->y;
          }

        if ((fx == x) && (fy == y) && (first_skip))
          {
             first_skip = EINA_FALSE;
             continue;
          }

        w = ec->comp_data->width_from_viewport;
        h = ec->comp_data->height_from_viewport;

        /* consider scale?
         * w = (int) (w * ((double)rect->w / parent_provider->ec->w));
         * h = (int) (h * ((double)rect->h / parent_provider->ec->h));
         */

        if (!region->mirror)
          {
             region->mirror = e_comp_object_util_mirror_add(ec->frame);
             evas_object_layer_set(region->mirror, ec->layer);
             evas_object_stack_below(region->mirror, ec->frame);
             evas_object_show(region->mirror);
             evas_object_event_callback_add(region->mirror, EVAS_CALLBACK_DEL, _remote_region_cb_mirror_del, region);
          }
        if (!region->mirror) continue;

        evas_object_move(region->mirror, x, y);
        evas_object_resize(region->mirror, w, h);
     }

   return EINA_TRUE;
}
#endif /* HAVE_REMOTE_SURFACE */

EINTERN Eina_Bool
e_comp_wl_remote_surface_commit(E_Client *ec)
{
#ifdef HAVE_REMOTE_SURFACE
   E_Comp_Wl_Remote_Provider *provider;
   E_Comp_Wl_Remote_Source *source = NULL;
   E_Comp_Wl_Subsurf_Data *sdata, *ssdata;
   E_Client *offscreen_parent;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(e_object_is_del(E_OBJECT(ec)), EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec->comp_data, EINA_FALSE);

   source = _remote_source_find(ec);
   if (source)
     {
        if (source->common.is_offscreen)
          {
             _e_comp_wl_remote_surface_state_commit(ec, &ec->comp_data->pending);
             return EINA_TRUE;
          }

        //send update to remote_surface of source client
        _e_comp_wl_remote_surface_source_update(source, ec->comp_data->pending.buffer);

        //do normal commit callback process
        return EINA_FALSE;
     }

   /* subsurface case */
   if ((sdata = ec->comp_data->sub.data))
     {
        /* check for valid subcompositor data */
        if (!sdata->parent)
          return EINA_FALSE;

        if (!(ssdata = sdata->parent->comp_data->sub.data))
          return EINA_FALSE;

        if (!ssdata->remote_surface.offscreen_parent)
          return EINA_FALSE;

        offscreen_parent = ssdata->remote_surface.offscreen_parent;

        provider = _remote_provider_find(offscreen_parent);
        if (!provider) return EINA_FALSE;

        if (!_e_comp_wl_remote_surface_subsurface_commit(provider, ec))
          return EINA_FALSE;
        return EINA_TRUE;
     }

   if (!(provider = _remote_provider_find(ec)))
     return EINA_FALSE;

   _e_comp_wl_remote_surface_state_commit(ec, &ec->comp_data->pending);

   return EINA_TRUE;
#else
   return EINA_FALSE;
#endif /* HAVE_REMOTE_SURFACE */
}

EAPI void
e_comp_wl_remote_surface_image_save(E_Client *ec)
{
#ifdef HAVE_REMOTE_SURFACE
   E_Comp_Wl_Remote_Source *src;

   if (!e_config->save_win_buffer) return;
   if (!e_config->hold_prev_win_img) return;
   if (ec->saved_img) return;
   if (ec->ignored) return;
   if (!_image_save_type_check(ec)) return;

   src = _remote_source_find(ec);
   if (!src)
     {
        src = E_NEW(E_Comp_Wl_Remote_Source, 1);
        EINA_SAFETY_ON_NULL_GOTO(src, end);

        src->common.ec = ec;
        eina_hash_add(_rsm->source_hash, &ec, src);
     }

   _remote_source_save_start(src);

end:
   return;
#endif /* HAVE_REMOTE_SURFACE */
}


EINTERN void
e_comp_wl_remote_surface_debug_info_get(Eldbus_Message_Iter *iter)
{
#ifdef HAVE_REMOTE_SURFACE
   Eldbus_Message_Iter *line_array;
   Eina_Iterator *hash_iter;
   E_Comp_Wl_Remote_Provider *provider;
   E_Comp_Wl_Remote_Source *source;
   int idx = 0;
   char info_str[1024];

   eldbus_message_iter_arguments_append(iter, "as", &line_array);
   if (!_rsm)
     {
        eldbus_message_iter_basic_append(line_array,
                                         's',
                                         "Remote Surface not initialized..");
        eldbus_message_iter_container_close(iter, line_array);
        return;
     }

   /* PROVIDER */
   hash_iter = eina_hash_iterator_data_new(_rsm->provider_hash);
   EINA_ITERATOR_FOREACH(hash_iter, provider)
     {
        E_Client *ec = provider->common.ec;
        E_Comp_Wl_Remote_Surface *remote_surface;
        Eina_List *l;

        if (!ec) continue;

        snprintf(info_str, sizeof(info_str),
                 "%10s [%d] 0x%08x win(0x%08x) res(%d) pid(%d) vis(%d) name(%s)",
                 "PROVIDER", idx++, (unsigned int)provider,
                 e_client_util_win_get(ec),
                 e_pixmap_res_id_get(ec->pixmap),
                 ec->netwm.pid,
                 provider->vis_ref,
                 e_client_util_name_get(ec)?:ec->icccm.class?:"NO NAME");
        eldbus_message_iter_basic_append(line_array, 's', info_str);

        if (provider->common.surfaces)
          {
             snprintf(info_str, sizeof(info_str), "%7s", "│");
             eldbus_message_iter_basic_append(line_array, 's', info_str);
          }

        EINA_LIST_FOREACH(provider->common.surfaces, l, remote_surface)
          {
             struct wl_client *wc = NULL;
             E_Client *owner = NULL;
             pid_t pid = -1;
             int s_idx = 0;
             Eina_Bool is_last = 0;

             if (!remote_surface->resource) continue;

             owner = remote_surface->owner;
             if (!owner)
               owner = remote_surface->bind_ec;

             wc = wl_resource_get_client(remote_surface->resource);
             if (wc)
               wl_client_get_credentials(wc, &pid, NULL, NULL);

             if ((eina_list_last(provider->common.surfaces) == l))
                 is_last = EINA_TRUE;

             snprintf(info_str, sizeof(info_str),
                      "%10s CONSUMER [%d] 0x%08x ec(0x%08x) win(0x%08x) pid(%d) vis(%d) redirected(%d) name(%s)",
                      is_last? "└─" : "├─", s_idx++, (unsigned int)remote_surface,
                      (unsigned int)owner?:0,
                      owner?e_client_util_win_get(owner):0,
                      pid,
                      remote_surface->visible,
                      remote_surface->redirect,
                      owner? e_client_util_name_get(owner)?:owner->icccm.class?:"NO NAME":"NO OWNER"
                      );
             eldbus_message_iter_basic_append(line_array, 's', info_str);
          }
        eldbus_message_iter_basic_append(line_array, 's', "");
     }
   eina_iterator_free(hash_iter);

   /* SOURCE */
   idx = 0;
   hash_iter = eina_hash_iterator_data_new(_rsm->source_hash);
   EINA_ITERATOR_FOREACH(hash_iter, source)
     {
        E_Client *ec = source->common.ec;
        E_Comp_Wl_Remote_Surface *remote_surface;
        Eina_List *l;

        if (!ec) continue;
        snprintf(info_str, sizeof(info_str),
                 "%10s [%d] 0x%08x win(0x%08x) res(%d) pid(%d) offscreen(%d) name(%s)",
                 "SOURCE", idx++, (unsigned int)source,
                 e_client_util_win_get(ec),
                 e_pixmap_res_id_get(ec->pixmap),
                 ec->netwm.pid,
                 source->offscreen_ref,
                 e_client_util_name_get(ec)?:ec->icccm.class?:"NO NAME");
        eldbus_message_iter_basic_append(line_array, 's', info_str);

        if (source->common.surfaces)
          {
             snprintf(info_str, sizeof(info_str), "%7s", "│");
             eldbus_message_iter_basic_append(line_array, 's', info_str);
          }

        EINA_LIST_FOREACH(source->common.surfaces, l, remote_surface)
          {
             struct wl_client *wc = NULL;
             E_Client *owner = NULL;
             pid_t pid = -1;
             int s_idx = 0;
             Eina_Bool is_last = 0;

             if (!remote_surface->resource) continue;

             owner = remote_surface->owner;
             if (!owner)
               owner = remote_surface->bind_ec;

             wc = wl_resource_get_client(remote_surface->resource);
             if (wc)
               wl_client_get_credentials(wc, &pid, NULL, NULL);

             if ((eina_list_last(source->common.surfaces) == l))
               is_last = EINA_TRUE;

             snprintf(info_str, sizeof(info_str),
                      "%10s CONSUMER [%d] 0x%08x ec(0x%08x) win(0x%08x) pid(%d) vis(%d) redirected(%d) name(%s)",
                      is_last? "└─" : "├─", s_idx++, (unsigned int)remote_surface,
                      (unsigned int)owner?:0,
                      owner?e_client_util_win_get(owner):0,
                      pid,
                      remote_surface->visible,
                      remote_surface->redirect,
                      owner? e_client_util_name_get(owner)?:owner->icccm.class?:"NO NAME":"NO OWNER"
                     );
             eldbus_message_iter_basic_append(line_array, 's', info_str);
          }
        eldbus_message_iter_basic_append(line_array, 's', "");
     }
   eina_iterator_free(hash_iter);

   eldbus_message_iter_container_close(iter, line_array);
#else
   Eldbus_Message_Iter *line_array;

   eldbus_message_iter_arguments_append(iter, "as", &line_array);
   eldbus_message_iter_basic_append(line_array,
                                    's',
                                    "Enlightenment doesn't support remote surface");
   eldbus_message_iter_container_close(iter, line_array);
#endif
}

#undef E_CLIENT_HOOK_APPEND
#define E_CLIENT_HOOK_APPEND(l, t, cb, d) \
  do                                      \
    {                                     \
       E_Client_Hook *_h;                 \
       _h = e_client_hook_add(t, cb, d);  \
       assert(_h);                        \
       l = eina_list_append(l, _h);       \
    }                                     \
  while (0)

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
                                         5,
                                         NULL,
                                         _remote_manager_cb_bind);

   /* client hook */
   E_CLIENT_HOOK_APPEND(rs_manager->client_hooks, E_CLIENT_HOOK_DEL, _e_comp_wl_remote_cb_client_del, NULL);
   if (e_config->save_win_buffer)
     E_CLIENT_HOOK_APPEND(rs_manager->client_hooks, E_CLIENT_HOOK_ICONIFY, _e_comp_wl_remote_cb_client_iconify, NULL);

   /* client event */
   E_LIST_HANDLER_APPEND(rs_manager->event_hdlrs,
                         E_EVENT_CLIENT_VISIBILITY_CHANGE,
                         _e_comp_wl_remote_cb_visibility_change, rs_manager);

   rs_manager->provider_hash = eina_hash_pointer_new(NULL);
   rs_manager->surface_hash = eina_hash_pointer_new(NULL);
   rs_manager->source_hash = eina_hash_pointer_new(NULL);
   rs_manager->dummy_fd = _e_comp_wl_remote_surface_dummy_fd_get();

   if (rs_manager->dummy_fd == -1)
     {
        ERR("it's FATAL error, remote surface can't send remote buffer without dummy_fd...");
        _rsm = rs_manager;
        e_comp_wl_remote_surface_shutdown();
        return;
     }

   RSMINF("dummy_fd created %d", NULL, NULL, "MANAGER", rs_manager, rs_manager->dummy_fd);

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
   E_Comp_Wl_Remote_Source *source;
   E_Comp_Wl_Remote_Surface *remote_surface;
   Eina_Iterator *it;

   if (!_rsm) return;

   rsm = _rsm;
   _rsm = NULL;

   it = eina_hash_iterator_data_new(rsm->provider_hash);
   EINA_ITERATOR_FOREACH(it, provider)
     {
        EINA_LIST_FREE(provider->common.surfaces, remote_surface)
          {
             remote_surface->provider = NULL;
             wl_resource_destroy(remote_surface->resource);
          }
        wl_resource_destroy(provider->resource);
     }
   eina_iterator_free(it);

   it = eina_hash_iterator_data_new(rsm->source_hash);
   EINA_ITERATOR_FOREACH(it, source)
     {
        EINA_LIST_FREE(source->common.surfaces, remote_surface)
          {
             remote_surface->source = NULL;
             wl_resource_destroy(remote_surface->resource);
          }
        _remote_source_destroy(source);
     }
   eina_iterator_free(it);

   if (rsm->dummy_fd != -1)
     close(rsm->dummy_fd);

   E_FREE_FUNC(rsm->provider_hash, eina_hash_free);
   E_FREE_FUNC(rsm->surface_hash, eina_hash_free);
   E_FREE_FUNC(rsm->source_hash, eina_hash_free);

   E_FREE_LIST(rsm->client_hooks, e_client_hook_del);
   E_FREE_LIST(rsm->event_hdlrs, ecore_event_handler_del);
   wl_global_destroy(rsm->global);
   E_FREE(rsm);
#endif /* HAVE_REMOTE_SURFACE */
}
