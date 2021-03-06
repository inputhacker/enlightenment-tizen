#include "e.h"
#include "e_policy_wl.h"
#ifdef HAVE_REMOTE_SURFACE
 #include <tizen-remote-surface-server-protocol.h>
#endif /* HAVE_REMOTE_SURFACE */
#include <tbm_surface.h>
#include <tbm_surface_internal.h>
#include <wayland-tbm-server.h>

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
typedef struct _E_Comp_Wl_Remote_Provider E_Comp_Wl_Remote_Provider;
typedef struct _E_Comp_Wl_Remote_Source E_Comp_Wl_Remote_Source;
typedef struct _E_Comp_Wl_Remote_Surface E_Comp_Wl_Remote_Surface;
typedef struct _E_Comp_Wl_Remote_Region E_Comp_Wl_Remote_Region;
typedef struct _E_Comp_Wl_Remote_Buffer E_Comp_Wl_Remote_Buffer;

struct _E_Comp_Wl_Remote_Manager{
   struct wl_global *global;

   Eina_Hash *provider_hash;
   Eina_Hash *surface_hash;
   Eina_Hash *source_hash;
   Eina_List *event_hdlrs;
   Eina_List *client_hooks;

   int dummy_fd; /* tizen_remote_surface@chagned_buffer need valid fd when it send tbm surface */
};

struct _E_Comp_Wl_Remote_Provider
{
     struct wl_resource *resource;

     E_Client *ec;

     Eina_List *surfaces;
     E_Comp_Wl_Remote_Surface *onscreen_parent;

     Eina_Bool visible;
     int vis_ref;
     uint32_t input_event_filter;

     Eina_Bool is_offscreen;
};

struct _E_Comp_Wl_Remote_Source {
     E_Client *ec;
     Eina_List *surfaces;

     E_Comp_Wl_Buffer_Ref buffer_ref;
     const char *image_path;
     Ecore_Thread *th;
     Eina_Bool deleted;

     int offscreen_ref;
     Eina_Bool is_offscreen;
};

struct _E_Comp_Wl_Remote_Surface {
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

     int version;
};

struct _E_Comp_Wl_Remote_Region {
     struct wl_resource *resource;

     E_Comp_Wl_Remote_Surface *remote_surface;
     Eina_Rectangle geometry;
     Evas_Object *mirror;
};

struct _E_Comp_Wl_Remote_Buffer {
     E_Comp_Wl_Buffer_Ref ref;
     struct wl_resource *resource;
     struct wl_listener destroy_listener;
};

static E_Comp_Wl_Remote_Manager *_rsm = NULL;

static void _e_comp_wl_remote_surface_state_buffer_set(E_Comp_Wl_Surface_State *state, E_Comp_Wl_Buffer *buffer);
static void _e_comp_wl_remote_buffer_cb_destroy(struct wl_listener *listener, void *data);
static E_Comp_Wl_Remote_Buffer *_e_comp_wl_remote_buffer_get(struct wl_resource *remote_buffer_resource);
static void _remote_surface_region_clear(E_Comp_Wl_Remote_Surface *remote_surface);

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

static void
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

static void
_remote_provider_rect_clear(E_Comp_Wl_Remote_Provider *provider)
{
   E_Client *ec;

   ec = provider->ec;
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

static void
_remote_provider_onscreen_parent_calculate(E_Comp_Wl_Remote_Provider *provider)
{
   Evas_Object *o;
   E_Client *ec, *_ec, *parent = NULL;
   E_Comp_Wl_Remote_Surface *surface;
   E_Comp_Wl_Client_Data *cdata;

   if (!provider) return;

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
          NULL, source->ec, "SOURCE", source, fd, source->image_path, (intmax_t)image_size);

   EINA_LIST_FOREACH(source->surfaces, l, remote_surface)
     {
        if (remote_surface->version < TIZEN_REMOTE_SURFACE_CHANGED_BUFFER_SINCE_VERSION)
          continue;

        tizen_remote_surface_send_changed_buffer(remote_surface->resource,
                                                 TIZEN_REMOTE_SURFACE_BUFFER_TYPE_IMAGE_FILE,
                                                 NULL,
                                                 fd,
                                                 (unsigned int)image_size,
                                                 ecore_time_get() * 1000,
                                                 NULL);
     }

   close(fd);
}

typedef struct {
     struct wl_shm_buffer *shm_buffer;
     struct wl_shm_pool *shm_pool;
     tbm_surface_h tbm_surface;

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

   RSMDBG("remote source destroy", NULL, source->ec,"SOURCE", source);

   if (source->th)
     {
        RSMDBG("thread is running. pending destroy", NULL, source->ec, "SOURCE", source);
        ecore_thread_cancel(source->th);
        source->deleted = EINA_TRUE;
        return;
     }

   if (_rsm)
     eina_hash_del_by_data(_rsm->source_hash, source);

   EINA_LIST_FREE(source->surfaces, remote_surface)
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
        RSMDBG("delete image %s", NULL, source->ec, "SOURCE", source, source->image_path);
        ecore_file_remove(source->image_path);
        eina_stringshare_del(source->image_path);
     }

   E_FREE(source);
}

static const char *
_remote_source_image_data_save(Thread_Data *td, const char *path, const char *name)
{
   struct wl_shm_buffer *shm_buffer = NULL;
   tbm_surface_h tbm_surface = NULL;
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
   dupname = strdup(fname);

   shm_buffer = td->shm_buffer;
   tbm_surface = td->tbm_surface;

   if (shm_buffer)
     {
         ptr = wl_shm_buffer_get_data(shm_buffer);
         EINA_SAFETY_ON_NULL_RETURN_VAL(ptr, NULL);

         stride = wl_shm_buffer_get_stride(shm_buffer);
         w = stride / 4;
         h = wl_shm_buffer_get_height(shm_buffer);

         ret = tbm_surface_internal_capture_shm_buffer(ptr, w, h, stride, path, dupname, "png");
         free((void*)dupname);
         if (!ret)
           return NULL;
     }
   else if (tbm_surface)
     {
         w = tbm_surface_get_width(tbm_surface);
         h = tbm_surface_get_height(tbm_surface);

         ret = tbm_surface_internal_capture_buffer(tbm_surface, path, dupname, "png");
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

   snprintf(dest_dir, sizeof(dest_dir), "%s/.enlightenment", run_dir);
   if (!ecore_file_exists(dest_dir))
     ecore_file_mkdir(dest_dir);
   dupdir = strdup(dest_dir);

   snprintf(name, sizeof(name), "e-window-image_0x%08x", (unsigned int)ec);
   dupname = strdup(name);

   dest_path = _remote_source_image_data_save(td, dupdir, dupname);
   if (dest_path)
     {
        td->image_path = eina_stringshare_add(dest_path);
        free((void*)dest_path);
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

        RSMDBG("Source save DONE path(%s)", source->ec->pixmap, source->ec,
               "SOURCE", source, td->image_path);

        /* remove previous file */
        if ((source->image_path) && (e_util_strcmp(source->image_path, td->image_path)))
          {
             ecore_file_remove(source->image_path);
             eina_stringshare_del(source->image_path);
          }
        source->image_path = eina_stringshare_add(td->image_path);
        _remote_source_send_image_update(source);
     }
   else
     ecore_file_remove(td->image_path);
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

   if (!td) return;

   ec = td->ec;
   if (!ec) goto end;

   source = _remote_source_find(ec);
   if (!source) goto end;

   RSMDBG("Source save CANCELED", source->ec->pixmap, source->ec,
          "SOURCE", source);

   if (th == source->th)
     {
        source->th = NULL;
        e_comp_wl_buffer_reference(&source->buffer_ref, NULL);
     }

   if (td->image_path)
     ecore_file_remove(td->image_path);

   if (source->deleted)
     _remote_source_destroy(source);
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
_remote_source_save_start(E_Comp_Wl_Remote_Source *source)
{
   E_Client *ec;
   E_Comp_Wl_Buffer *buffer = NULL;
   Thread_Data *td;
   struct wl_shm_buffer *shm_buffer;
   struct wl_shm_pool *shm_pool;
   tbm_surface_h tbm_surface;

   if (!(ec = source->ec)) return;
   if (!(buffer = e_pixmap_resource_get(ec->pixmap))) return;
   if (!e_config->save_win_buffer) return;

   td = E_NEW(Thread_Data, 1);
   if (!td) return;

   e_object_ref(E_OBJECT(ec));
   td->ec = ec;

   if (source->th)
     ecore_thread_cancel(source->th);

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
               source->ec->pixmap, source->ec,
               "SOURCE", source, source->offscreen_ref);

        if (source->offscreen_ref == 1)
          {
             source->is_offscreen = EINA_TRUE;

             source->ec->exp_iconify.not_raise = 1;
             if (!source->ec->exp_iconify.by_client)
               e_policy_wl_iconify_state_change_send(source->ec, 0);

             RSMINF("Un-Set ICONIFY BY Remote_Surface", source->ec->pixmap, source->ec,
                    "SOURCE", source);
             e_client_uniconify(source->ec);

             source->ec->exp_iconify.by_client = 0;
             source->ec->exp_iconify.skip_by_remote = 1;

             EC_CHANGED(source->ec);
          }
     }
   else
     {
        if (!source->is_offscreen)
          return;

        source->offscreen_ref--;
        RSMDBG("Unset offscreen offscreen_ref:%d",
               source->ec->pixmap, source->ec,
               "SOURCE", source, source->offscreen_ref);

        if (source->offscreen_ref == 0)
          {
             source->is_offscreen = EINA_FALSE;
             source->ec->exp_iconify.skip_by_remote = 0;
             EC_CHANGED(source->ec);
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

   _remote_provider_client_set(provider->ec, EINA_FALSE);
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
          provider->ec->pixmap, provider->ec,
          "PROVIDER", provider, event_filter);

   if (!event_filter) return;

   EINA_LIST_FOREACH(provider->surfaces, l, remote_surface)
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

        provider->surfaces = eina_list_remove(provider->surfaces,
                                              remote_surface);
        remote_surface->provider = NULL;
     }

   source = remote_surface->source;
   if (source)
     {
        source->surfaces = eina_list_remove(source->surfaces, remote_surface);
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
   if (!remote_surface) return;
   if (!remote_surface->valid) return;

   if (remote_surface->provider)
     {
        EINA_SAFETY_ON_NULL_RETURN(remote_surface->provider->ec);

        RSMINF("Redirect surface provider:%p(ec:%p)",
               NULL, NULL,
               "SURFACE", remote_surface,
               remote_surface->provider, remote_surface->provider->ec);

        remote_surface->redirect = EINA_TRUE;

        /* Send input event filter of provider */
        if ((remote_surface->provider->input_event_filter) &&
            (remote_surface->version >= TIZEN_REMOTE_SURFACE_INPUT_EVENT_FILTER_SINCE_VERSION))
          tizen_remote_surface_send_input_event_filter(resource,
                                                       remote_surface->provider->input_event_filter);

        buffer = e_pixmap_resource_get(remote_surface->provider->ec->pixmap);
        EINA_SAFETY_ON_NULL_RETURN(buffer);

        remote_buffer_resource = e_comp_wl_tbm_remote_buffer_get(remote_surface->wl_tbm, buffer->resource);
        EINA_SAFETY_ON_NULL_RETURN(remote_buffer_resource);

        remote_buffer = _e_comp_wl_remote_buffer_get(remote_buffer_resource);
        EINA_SAFETY_ON_NULL_RETURN(remote_buffer);

        if (remote_surface->version >= 2)
          e_comp_wl_buffer_reference(&remote_buffer->ref, buffer);

        if (remote_surface->version >= TIZEN_REMOTE_SURFACE_CHANGED_BUFFER_SINCE_VERSION)
          tizen_remote_surface_send_changed_buffer(resource,
                                                   TIZEN_REMOTE_SURFACE_BUFFER_TYPE_TBM,
                                                   remote_buffer->resource,
                                                   _rsm->dummy_fd,
                                                   0,
                                                   ecore_time_get() * 1000,
                                                   NULL);
        else
          tizen_remote_surface_send_update_buffer(resource,
                                                  remote_buffer->resource,
                                                  ecore_time_get() * 1000);
     }
   else if (remote_surface->source)
     {
        EINA_SAFETY_ON_NULL_RETURN(remote_surface->source->ec);
        RSMINF("Redirect surface source:%p(ec:%p)",
               NULL, NULL,
               "SURFACE", remote_surface,
               remote_surface->source, remote_surface->source->ec);

        if (remote_surface->version < TIZEN_REMOTE_SURFACE_CHANGED_BUFFER_SINCE_VERSION)
          return;

        remote_surface->redirect = EINA_TRUE;

        if ((buffer = e_pixmap_resource_get(remote_surface->source->ec->pixmap)))
          {

             remote_buffer_resource = e_comp_wl_tbm_remote_buffer_get(remote_surface->wl_tbm, buffer->resource);
             EINA_SAFETY_ON_NULL_RETURN(remote_buffer_resource);

             remote_buffer = _e_comp_wl_remote_buffer_get(remote_buffer_resource);
             EINA_SAFETY_ON_NULL_RETURN(remote_buffer);

             if (remote_surface->version >= 2)
               e_comp_wl_buffer_reference(&remote_buffer->ref, buffer);

             tizen_remote_surface_send_changed_buffer(remote_surface->resource,
                                                      TIZEN_REMOTE_SURFACE_BUFFER_TYPE_TBM,
                                                      remote_buffer->resource,
                                                      _rsm->dummy_fd,
                                                      0,
                                                      ecore_time_get() * 1000,
                                                      NULL);
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
   if (!remote_surface) return;
   if (!remote_surface->valid) return;

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
   if (!remote_surface) return;
   if (!remote_surface->provider) return;
   if (!remote_surface->provider->ec) return;
   if (!remote_surface->valid) return;

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

static void
_remote_surface_cb_mouse_wheel_transfer(struct wl_client *client, struct wl_resource *resource, uint32_t direction, int32_t z, uint32_t clas, uint32_t subclas, const char *identifier, uint32_t time)
{
   E_Comp_Wl_Remote_Provider *provider;
   E_Comp_Wl_Remote_Surface *remote_surface;
   E_Client *ec;

   Ecore_Device *edev = NULL;

   remote_surface = wl_resource_get_user_data(resource);
   if (!remote_surface) return;
   if (!remote_surface->provider) return;
   if (!remote_surface->provider->ec) return;
   if (!remote_surface->valid) return;

   provider = remote_surface->provider;
   ec = provider->ec;

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
   if (!remote_surface) return;
   if (!remote_surface->provider) return;
   if (!remote_surface->provider->ec) return;
   if (!remote_surface->valid) return;

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
   if (!remote_surface) return;
   if (!remote_surface->provider) return;
   if (!remote_surface->provider->ec) return;
   if (!remote_surface->valid) return;

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
   if (!remote_surface) return;
   if (!remote_surface->provider) return;
   if (!remote_surface->provider->ec) return;
   if (!remote_surface->valid) return;

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
   if (!remote_surface) return;
   if (!remote_surface->valid) return;

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
   if (!remote_surface) return;
   if (!remote_surface->valid) return;

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
   if (!remote_surface) return;
   if (!remote_surface->valid) return;

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

static void
_remote_surface_cb_release(struct wl_client *client, struct wl_resource *resource, struct wl_resource *remote_buffer_resource)
{
   E_Comp_Wl_Remote_Surface *remote_surface;
   E_Comp_Wl_Remote_Buffer *remote_buffer;

   remote_surface = wl_resource_get_user_data(resource);
   if (!remote_surface) return;
   if (!remote_surface->valid) return;

   remote_buffer = _e_comp_wl_remote_buffer_get(remote_buffer_resource);
   EINA_SAFETY_ON_NULL_RETURN(remote_buffer);

   if (remote_surface->version >= 2)
     e_comp_wl_buffer_reference(&remote_buffer->ref, NULL);
}

static void
_remote_surface_cb_remote_render_set(struct wl_client *client, struct wl_resource *resource, uint32_t set)
{
   E_Comp_Wl_Remote_Surface *remote_surface;
   E_Comp_Wl_Remote_Source *source = NULL;

   remote_surface = wl_resource_get_user_data(resource);
   if (!remote_surface) return;
   if (!remote_surface->valid) return;

   source = remote_surface->source;
   if (!source) return;

   if (remote_surface->remote_render == set)
     return;

   remote_surface->remote_render = set;
   _remote_source_offscreen_set(source, set);
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

   _remote_provider_client_set(ec, EINA_TRUE);
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
   E_Comp_Wl_Remote_Provider *provider = NULL;
   E_Comp_Wl_Remote_Source *source = NULL;
   E_Client *ec;
   int version;
   pid_t pid;

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

   wl_client_get_credentials(client, &pid, NULL, NULL);
   provider = _remote_provider_find(ec);
   if (!provider)
     {
        if (version >= TIZEN_REMOTE_SURFACE_CHANGED_BUFFER_SINCE_VERSION)
          {
             /* TODO: privilege check */
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

                  source->ec = ec;
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
     provider->surfaces = eina_list_append(provider->surfaces, remote_surface);
   else if (source)
     source->surfaces = eina_list_append(source->surfaces, remote_surface);

   RSMINF("Created resource(%p) ec(%p) provider(%p) source(%p) version(%d)",
          NULL, NULL,
          "SURFACE", remote_surface, resource, ec, provider, source, remote_surface->version);

   remote_surface->valid = EINA_TRUE;
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
   if (!remote_surface) return;
   if (!remote_surface->valid) return;

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
_e_comp_wl_remote_cb_client_iconify(void *data, E_Client *ec)
{
   E_Comp_Wl_Remote_Source *source;

   if (!(source = _remote_source_find(ec)))
     {
        if (ec->ignored) return;
        if (ec->parent) return;
        if ((e_policy_client_is_home_screen(ec)) ||
            (e_policy_client_is_lockscreen(ec)) ||
            (e_policy_client_is_volume(ec)) ||
            (e_policy_client_is_volume_tv(ec)) ||
            (e_policy_client_is_cbhm(ec)))
          return;

        source = E_NEW(E_Comp_Wl_Remote_Source, 1);
        if (!source) return;

        source->ec = ec;
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
     ecore_thread_cancel(source->th);

   EINA_LIST_FOREACH(source->surfaces, l, remote_surface)
     {
        if (remote_surface->version < TIZEN_REMOTE_SURFACE_CHANGED_BUFFER_SINCE_VERSION)
          continue;

        remote_buffer_resource = e_comp_wl_tbm_remote_buffer_get(remote_surface->wl_tbm, buffer->resource);
        if (!remote_buffer_resource) continue;

        remote_buffer = _e_comp_wl_remote_buffer_get(remote_buffer_resource);
        if (!remote_buffer) continue;

        if (!remote_surface->redirect) continue;

        if (remote_surface->version >= 2)
          e_comp_wl_buffer_reference(&remote_buffer->ref, buffer);

        tizen_remote_surface_send_changed_buffer(remote_surface->resource,
                                                 TIZEN_REMOTE_SURFACE_BUFFER_TYPE_TBM,
                                                 remote_buffer->resource,
                                                 _rsm->dummy_fd,
                                                 0,
                                                 ecore_time_get() * 1000,
                                                 NULL);
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
             EINA_LIST_FOREACH(provider->surfaces, l, surface)
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
                    }
                  else
                    {
                       if (surface->version >= 2)
                         e_comp_wl_buffer_reference(&remote_buffer->ref, buffer);

                       if (surface->version >= TIZEN_REMOTE_SURFACE_CHANGED_BUFFER_SINCE_VERSION)
                         tizen_remote_surface_send_changed_buffer(surface->resource,
                                                                  TIZEN_REMOTE_SURFACE_BUFFER_TYPE_TBM,
                                                                  remote_buffer->resource,
                                                                  _rsm->dummy_fd,
                                                                  0,
                                                                  ecore_time_get() * 1000,
                                                                  NULL);
                       else
                         tizen_remote_surface_send_update_buffer(surface->resource,
                                                                 remote_buffer->resource,
                                                                 ecore_time_get()  * 1000);
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

   EINA_LIST_FOREACH(parent_provider->ec->comp_data->remote_surface.regions, l, rect)
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
   if (e_object_is_del(E_OBJECT(ec)) || !ec->comp_data) return EINA_FALSE;

   if ((source = _remote_source_find(ec)))
     {
        if (source->is_offscreen)
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
                                         4,
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
        EINA_LIST_FREE(provider->surfaces, remote_surface)
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
        EINA_LIST_FREE(source->surfaces, remote_surface)
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
