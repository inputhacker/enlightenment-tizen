#include "e.h"

#include "e_comp_wl.h"
#include <wayland-tbm-server.h>
#include <tizen-extension-server-protocol.h>
#include <tizen-surface-server-protocol.h>
#ifndef EGL_TEXTURE_RGBA
# define EGL_TEXTURE_RGBA 0x305E
#endif

#include <uuid.h>

static Eina_Hash *pixmaps[E_PIXMAP_TYPE_MAX] = {NULL};
static Eina_Hash *res_ids = NULL;
static uint32_t res_id = 0;
static Eina_Hash *aliases[E_PIXMAP_TYPE_MAX] = {NULL};
static uint32_t dummy_pixmap_id = 1; // dummy id starts from 1

struct _E_Pixmap
{
   unsigned int refcount;
   unsigned int failures;

   E_Client *client;
   E_Pixmap_Type type;

   Ecore_Window win;
   uint32_t res_id;
   Ecore_Window parent;

   int w, h;

   E_Comp_Wl_Buffer *buffer;
   E_Comp_Wl_Buffer_Ref buffer_ref;
   struct wl_listener buffer_destroy_listener;
   void *data;
   struct wl_shm_buffer *shm_buffer;
   struct wl_shm_pool *data_pool;
   Eina_Rectangle opaque;
   uuid_t uuid;

   struct wl_resource *shm_flusher;

   E_Comp_Wl_Client_Data *cdata;
   Eina_Bool own_cdata : 1;

   Eina_Bool usable : 1;
   Eina_Bool dirty : 1;
   Eina_Bool image_argb : 1;
};

static int _e_pixmap_hooks_delete = 0;
static int _e_pixmap_hooks_walking = 0;

static Eina_Inlist *_e_pixmap_hooks[] =
{
   [E_PIXMAP_HOOK_NEW] = NULL,
   [E_PIXMAP_HOOK_DEL] = NULL,
   [E_PIXMAP_HOOK_USABLE] = NULL,
   [E_PIXMAP_HOOK_UNUSABLE] = NULL,
};

static void
_e_pixmap_hooks_clean(void)
{
   Eina_Inlist *l;
   E_Pixmap_Hook *ph;
   unsigned int x;

   for (x = 0; x < E_PIXMAP_HOOK_LAST; x++)
     EINA_INLIST_FOREACH_SAFE(_e_pixmap_hooks[x], l, ph)
       {
          if (!ph->delete_me) continue;
          _e_pixmap_hooks[x] = eina_inlist_remove(_e_pixmap_hooks[x],
                                                  EINA_INLIST_GET(ph));
          free(ph);
       }
}

static void
_e_pixmap_hook_call(E_Pixmap_Hook_Point hookpoint, E_Pixmap *cp)
{
   E_Pixmap_Hook *ph;

   _e_pixmap_hooks_walking++;
   EINA_INLIST_FOREACH(_e_pixmap_hooks[hookpoint], ph)
     {
        if (ph->delete_me) continue;
        ph->func(ph->data, cp);
     }
   _e_pixmap_hooks_walking--;
   if ((_e_pixmap_hooks_walking == 0) && (_e_pixmap_hooks_delete > 0))
     _e_pixmap_hooks_clean();
}

static void
_e_pixmap_cb_buffer_destroy(struct wl_listener *listener, void *data)
{
   E_Pixmap *cp;

   cp = container_of(listener, E_Pixmap, buffer_destroy_listener);
   if ((E_Comp_Wl_Buffer *)data != cp->buffer)
     return;

   cp->buffer = NULL;
   cp->data = NULL;
   cp->shm_buffer = NULL;
}

static void
_e_pixmap_clear(E_Pixmap *cp, Eina_Bool cache)
{
   cp->w = cp->h = 0;
   cp->image_argb = EINA_FALSE;

   if (cp->type != E_PIXMAP_TYPE_WL) return;
   e_pixmap_image_clear(cp, cache);
   ELOGF("PIXMAP", "PIXMAP CLEAR. cp:%p", cp->client, cp);
}

static void
_e_pixmap_free(E_Pixmap *cp)
{
   if (cp->own_cdata)
     {
        E_FREE(cp->cdata);
        cp->own_cdata = EINA_FALSE;
     }
   if (cp->data_pool)
     {
        if (cp->client && cp->client->frame)
          e_comp_object_clear(cp->client->frame);

        wl_shm_pool_unref(cp->data_pool);
        cp->data_pool = NULL;
     }
   _e_pixmap_clear(cp, 1);
   ELOGF("PIXMAP", "PIXMAP FREE. cp:%p", cp->client, cp);

   if (cp->shm_flusher)
     {
        wl_resource_set_user_data(cp->shm_flusher, NULL);
        cp->shm_flusher = NULL;
     }

   if (cp->buffer)
     wl_list_remove(&cp->buffer_destroy_listener.link);

   free(cp);
}

static E_Pixmap *
_e_pixmap_new(E_Pixmap_Type type)
{
   E_Pixmap *cp;

   cp = E_NEW(E_Pixmap, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(cp, NULL);
   cp->type = type;
   cp->w = cp->h = 0;
   cp->refcount = 1;
   cp->dirty = 1;
   cp->cdata = E_NEW(E_Comp_Wl_Client_Data, 1);
   cp->buffer_destroy_listener.notify = _e_pixmap_cb_buffer_destroy;
   if (!cp->cdata)
     {
        E_FREE(cp);
        return NULL;
     }
   cp->cdata->pending.buffer_viewport.buffer.transform = WL_OUTPUT_TRANSFORM_NORMAL;
   cp->cdata->pending.buffer_viewport.buffer.scale = 1;
   cp->cdata->pending.buffer_viewport.buffer.src_width = wl_fixed_from_int(-1);
   cp->cdata->pending.buffer_viewport.surface.width = -1;
   cp->cdata->pending.buffer_viewport.changed = 0;
   cp->cdata->accepts_focus = 1;
   cp->own_cdata = EINA_TRUE;
   return cp;
}

static E_Pixmap *
_e_pixmap_find(E_Pixmap_Type type, va_list *l)
{
   uintptr_t id;
   E_Pixmap *cp;

   if (type == E_PIXMAP_TYPE_X) return NULL;
   if (!pixmaps[type]) return NULL;

   id = va_arg(*l, uintptr_t);
   cp = eina_hash_find(aliases[type], &id);
   if (!cp) cp = eina_hash_find(pixmaps[type], &id);
   return cp;
}

// --------------------------------------------------------
// tizen_surface_shm
// --------------------------------------------------------
static void
_e_pixmap_tzsurf_shm_flusher_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static const struct tizen_surface_shm_flusher_interface _tzsurf_shm_flusher_iface =
{
   _e_pixmap_tzsurf_shm_flusher_cb_destroy,
};

static void
_e_pixmap_tzsurf_shm_flusher_cb_res_destroy(struct wl_resource *resource)
{
   E_Pixmap *cp;

   cp = wl_resource_get_user_data(resource);
   if (cp)
     cp->shm_flusher = NULL;
}

static void
_e_pixmap_tzsurf_shm_cb_flusher_get(struct wl_client *client, struct wl_resource *flusher, uint32_t id, struct wl_resource *surface)
{
   E_Client *ec;
   struct wl_resource *res;

   ec = wl_resource_get_user_data(surface);
   if ((!ec) || (!ec->pixmap))
     {
        wl_resource_post_error(flusher, WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "tizen_shm_flusher failed: wrong wl_surface@%d resource",
                               wl_resource_get_id(surface));
        return;
     }

   res = wl_resource_create(client, &tizen_surface_shm_flusher_interface,
                            wl_resource_get_version(flusher), id);
   if (!res)
     {
        wl_resource_post_no_memory(flusher);
        return;
     }

   if (ec->pixmap->shm_flusher)
      wl_resource_set_user_data(ec->pixmap->shm_flusher, NULL);

   wl_resource_set_implementation(res, &_tzsurf_shm_flusher_iface, ec->pixmap,
                                  _e_pixmap_tzsurf_shm_flusher_cb_res_destroy);

   ec->pixmap->shm_flusher = res;
}

static void
_e_pixmap_tzsurf_shm_cb_destroy(struct wl_client *client, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static const struct tizen_surface_shm_interface _tzsurf_shm_iface =
{
   _e_pixmap_tzsurf_shm_cb_flusher_get,
   _e_pixmap_tzsurf_shm_cb_destroy,
};

static void
_e_pixmap_tzsurf_shm_cb_bind(struct wl_client *client, void *data EINA_UNUSED, uint32_t ver, uint32_t id)
{
   struct wl_resource *res;

   res = wl_resource_create(client, &tizen_surface_shm_interface, ver, id);
   if (!res)
     {
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(res, &_tzsurf_shm_iface, NULL, NULL);
}

E_API int
e_pixmap_free(E_Pixmap *cp)
{
   if (!cp) return 0;
   if (--cp->refcount) return cp->refcount;
   ELOGF("PIXMAP", "PIXMAP DEL. cp:%p", NULL, cp);
   if (cp->usable)
     e_pixmap_usable_set(cp, 0);

   _e_pixmap_hook_call(E_PIXMAP_HOOK_DEL, cp);
   e_pixmap_image_clear(cp, EINA_FALSE);
   eina_hash_del_by_key(res_ids, &cp->res_id);

   if (cp->win)
     eina_hash_del_by_key(pixmaps[cp->type], &cp->win);

   _e_pixmap_free(cp);

   return 0;
}

E_API void
e_pixmap_win_id_del(E_Pixmap *cp)
{
   if (!cp) return;
   if (cp->type == E_PIXMAP_TYPE_X) return;
   if (!cp->win) return;

   eina_hash_del_by_key(pixmaps[cp->type], &cp->win);
   cp->win = 0;
}

E_API E_Pixmap *
e_pixmap_ref(E_Pixmap *cp)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(cp, NULL);
   cp->refcount++;
   return cp;
}

E_API E_Pixmap *
e_pixmap_new(E_Pixmap_Type type, ...)
{
   E_Pixmap *cp = NULL;
   va_list l;
   uintptr_t id;

   EINA_SAFETY_ON_TRUE_RETURN_VAL(type == E_PIXMAP_TYPE_X, NULL);

   va_start(l, type);

   id = va_arg(l, uintptr_t);

   if ((type == E_PIXMAP_TYPE_NONE) ||
       (type == E_PIXMAP_TYPE_EXT_OBJECT))
     {
        id = dummy_pixmap_id++;
     }

   if (pixmaps[type])
     {
        cp = eina_hash_find(pixmaps[type], &id);
        if (cp)
          {
             cp->refcount++;
             goto end;
          }
     }
   else
     pixmaps[type] = eina_hash_pointer_new(NULL);

   cp = _e_pixmap_new(type);
   if (!cp)
     {
        va_end(l);
        ELOGF("PIXMAP", "NEW failed id:%p", NULL, (void *)id);
        return NULL;
     }

   cp->win = id;
   eina_hash_add(pixmaps[type], &id, cp);
   uuid_generate(cp->uuid);

   if (!res_ids)
     res_ids = eina_hash_int32_new(NULL);

   cp->res_id = ++res_id;
   eina_hash_add(res_ids, &res_id, cp);
   ELOGF("PIXMAP", "PIXMAP NEW. cp:%p, id:%d", NULL, cp, cp->res_id);

end:
   va_end(l);

   _e_pixmap_hook_call(E_PIXMAP_HOOK_NEW, cp);

   return cp;
}

E_API E_Pixmap_Type
e_pixmap_type_get(const E_Pixmap *cp)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(cp, 9999);
   return cp->type;
}

E_API void
e_pixmap_parent_window_set(E_Pixmap *cp, Ecore_Window win)
{
   EINA_SAFETY_ON_NULL_RETURN(cp);
   EINA_SAFETY_ON_FALSE_RETURN(cp->type == E_PIXMAP_TYPE_WL);

   if (cp->parent == win) return;

   e_pixmap_usable_set(cp, 0);
   e_pixmap_clear(cp);

   cp->parent = win;
}

E_API void
e_pixmap_usable_set(E_Pixmap *cp, Eina_Bool set)
{
   Eina_Bool tmp = !!set;
   EINA_SAFETY_ON_NULL_RETURN(cp);

   if (cp->usable != tmp)
     {
        cp->usable = tmp;

        if (cp->usable)
          _e_pixmap_hook_call(E_PIXMAP_HOOK_USABLE, cp);
        else
          {
             // if we are received NULL Buffer by client while a surface waiting in the render queue,
             // we canceled rendering and we doesn't send frame callback event.
             // in this case e_pixmap_iamge_clear function send frame callback.
             e_pixmap_image_clear(cp, 1);
             _e_pixmap_hook_call(E_PIXMAP_HOOK_UNUSABLE, cp);
          }
     }
}

E_API Eina_Bool
e_pixmap_usable_get(const E_Pixmap *cp)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(cp, EINA_FALSE);
   return cp->usable;
}

E_API Eina_Bool
e_pixmap_dirty_get(E_Pixmap *cp)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(cp, EINA_FALSE);
   return cp->dirty;
}

E_API void
e_pixmap_clear(E_Pixmap *cp)
{
   EINA_SAFETY_ON_NULL_RETURN(cp);
   _e_pixmap_clear(cp, 0);
   cp->dirty = EINA_TRUE;
}

E_API void
e_pixmap_dirty(E_Pixmap *cp)
{
   EINA_SAFETY_ON_NULL_RETURN(cp);
   cp->dirty = 1;
}

E_API Eina_Bool
e_pixmap_refresh(E_Pixmap *cp)
{
   E_Comp_Wl_Buffer *buffer;
   struct wl_shm_buffer *shm_buffer;
   int format;
   Eina_Bool success = EINA_FALSE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(cp, EINA_FALSE);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(cp->type == E_PIXMAP_TYPE_WL, EINA_FALSE);

   if (!cp->usable)
     {
        cp->failures++;
        return EINA_FALSE;
     }
   if (!cp->dirty) return EINA_TRUE;

   cp->w = cp->h = 0;
   cp->image_argb = EINA_FALSE;

   buffer = cp->buffer;
   if (!buffer) return EINA_FALSE;

   shm_buffer = buffer->shm_buffer;
   cp->w = buffer->w;
   cp->h = buffer->h;

   if (shm_buffer)
     format = wl_shm_buffer_get_format(shm_buffer);
   else if (buffer->tbm_surface)
     format = tbm_surface_get_format(buffer->tbm_surface);
   else if (e_comp->gl && buffer->resource)
     e_comp_wl->wl.glapi->evasglQueryWaylandBuffer(e_comp_wl->wl.gl,
                                                   buffer->resource,
                                                   EVAS_GL_TEXTURE_FORMAT,
                                                   &format);
   else if (buffer->type == E_COMP_WL_BUFFER_TYPE_VIDEO)
      format = 0;
   else
      return EINA_FALSE;

   switch (format)
     {
      case WL_SHM_FORMAT_ARGB8888:
      case EGL_TEXTURE_RGBA:
      case TBM_FORMAT_ARGB8888:
         cp->image_argb = EINA_TRUE;
         break;
      default:
         cp->image_argb = EINA_FALSE;
         break;
     }

   success = ((cp->w > 0) && (cp->h > 0));
   if (success)
     {
        cp->dirty = 0;
        cp->failures = 0;
     }
   else
     cp->failures++;

   return success;
}

E_API Eina_Bool
e_pixmap_size_changed(E_Pixmap *cp, int w, int h)
{
   E_Client *ec;
   EINA_SAFETY_ON_NULL_RETURN_VAL(cp, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(cp->client, EINA_FALSE);
   if (cp->dirty) return EINA_TRUE;

   ec = cp->client;
   if (!ec->comp_data || e_object_is_del(E_OBJECT(ec)))
     return (w != cp->w) || (h != cp->h);

   return (w != ec->comp_data->width_from_buffer) || (h != ec->comp_data->height_from_buffer);
}

E_API Eina_Bool
e_pixmap_size_get(E_Pixmap *cp, int *w, int *h)
{
   E_Client *ec;
   if (w) *w = 0;
   if (h) *h = 0;
   EINA_SAFETY_ON_NULL_RETURN_VAL(cp, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(cp->client, EINA_FALSE);

   ec = cp->client;
   if (!ec->comp_data || e_object_is_del(E_OBJECT(ec)))
     {
        if (w) *w = cp->w;
        if (h) *h = cp->h;
        return (cp->w > 0) && (cp->h > 0);
     }

   if (w) *w = ec->comp_data->width_from_buffer;
   if (h) *h = ec->comp_data->height_from_buffer;
   return (ec->comp_data->width_from_buffer > 0) && (ec->comp_data->height_from_buffer > 0);
}

E_API unsigned int
e_pixmap_failures_get(const E_Pixmap *cp)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(cp, 0);
   return cp->failures;
}

E_API void
e_pixmap_client_set(E_Pixmap *cp, E_Client *ec)
{
   EINA_SAFETY_ON_NULL_RETURN(cp);
   if (cp->client && ec) CRI("ACK!");
   ELOGF("PIXMAP", "PIXMAP CLIENT SET. cp:%p", ec, cp);
   cp->client = ec;
}

E_API E_Client *
e_pixmap_client_get(E_Pixmap *cp)
{
   if (!cp) return NULL;
   return cp->client;
}

E_API E_Pixmap *
e_pixmap_find(E_Pixmap_Type type, ...)
{
   va_list l;
   E_Pixmap *cp;

   va_start(l, type);
   cp = _e_pixmap_find(type, &l);
   va_end(l);
   return cp;
}

E_API E_Client *
e_pixmap_find_client(E_Pixmap_Type type, ...)
{
   va_list l;
   E_Pixmap *cp;

   va_start(l, type);
   cp = _e_pixmap_find(type, &l);
   va_end(l);
   return (!cp) ? NULL : cp->client;
}

E_API E_Client *
e_pixmap_find_client_by_res_id(uint32_t res_id)
{
   E_Pixmap *cp;

   if (!res_ids) return NULL;
   cp = eina_hash_find(res_ids, &res_id);

   return (!cp) ? NULL : cp->client;
}

E_API uint32_t
e_pixmap_res_id_get(E_Pixmap *cp)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(cp, 0);
   return cp->res_id;
}

E_API uint64_t
e_pixmap_window_get(E_Pixmap *cp)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(cp, 0);
   return cp->win;
}

E_API void *
e_pixmap_resource_get(E_Pixmap *cp)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(cp, NULL);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(cp->type == E_PIXMAP_TYPE_WL, NULL);
   return cp->buffer;
}

E_API void
e_pixmap_resource_set(E_Pixmap *cp, void *resource)
{
   if ((!cp) || (cp->type != E_PIXMAP_TYPE_WL)) return;

   if (cp->buffer)
     {
        if (cp->buffer == resource)
          return;

        wl_list_remove(&cp->buffer_destroy_listener.link);
     }

   cp->buffer = resource;
   if (cp->buffer)
     wl_signal_add(&cp->buffer->destroy_signal, &cp->buffer_destroy_listener);
}

E_API Ecore_Window
e_pixmap_parent_window_get(E_Pixmap *cp)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(cp, 0);
   return cp->parent;
}

E_API Eina_Bool
e_pixmap_native_surface_init(E_Pixmap *cp, Evas_Native_Surface *ns)
{
   Eina_Bool ret = EINA_FALSE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(cp, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ns, EINA_FALSE);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(cp->type == E_PIXMAP_TYPE_WL, EINA_FALSE);

   ns->version = EVAS_NATIVE_SURFACE_VERSION;
   if (cp->buffer && cp->buffer->type == E_COMP_WL_BUFFER_TYPE_NATIVE)
     {
        ns->type = EVAS_NATIVE_SURFACE_WL;
        ns->version = EVAS_NATIVE_SURFACE_VERSION;
        ns->data.wl.legacy_buffer = cp->buffer->resource;
        ret = !cp->buffer->shm_buffer;
     }
   else if (cp->buffer && cp->buffer->type == E_COMP_WL_BUFFER_TYPE_TBM)
     {
        ns->type = EVAS_NATIVE_SURFACE_TBM;
        ns->version = EVAS_NATIVE_SURFACE_VERSION;
        ns->data.tbm.buffer = cp->buffer->tbm_surface;
        if (cp->buffer->tbm_surface)
          ret = EINA_TRUE;
     }
   else /* SHM buffer or VIDEO buffer */
     {
        ns->type = EVAS_NATIVE_SURFACE_NONE;
        ret = EINA_FALSE;
     }

   return ret;
}

E_API void
e_pixmap_image_clear(E_Pixmap *cp, Eina_Bool cache)
{
   EINA_SAFETY_ON_NULL_RETURN(cp);
   EINA_SAFETY_ON_FALSE_RETURN(cp->type == E_PIXMAP_TYPE_WL);

   if (!cache)
     {
        if (!cp->buffer_ref.buffer) return;
     }

   cp->failures = 0;
   if (cache)
     {
        E_Comp_Wl_Client_Data *cd;
        struct wl_resource *cb;
        Eina_List *l, *ll;

        if ((!cp->client) || (!cp->client->comp_data)) return;
        cd = (E_Comp_Wl_Client_Data *)cp->client->comp_data;
        EINA_LIST_FOREACH_SAFE(cd->frames, l, ll, cb)
          {
             wl_callback_send_done(cb, ecore_time_unix_get() * 1000);
             wl_resource_destroy(cb);
          }
     }
   e_comp_wl_buffer_reference(&cp->buffer_ref, NULL);
   cp->data = NULL;
   cp->shm_buffer = NULL;
}

E_API Eina_Bool
e_pixmap_image_refresh(E_Pixmap *cp)
{
   E_Comp_Wl_Buffer *buffer = NULL;
   struct wl_shm_buffer *shm_buffer = NULL;
   int bw, bh;

   EINA_SAFETY_ON_NULL_RETURN_VAL(cp, EINA_FALSE);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(cp->dirty, EINA_FALSE);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(cp->type == E_PIXMAP_TYPE_WL, EINA_FALSE);

   buffer = cp->buffer;
   if (!buffer) return EINA_FALSE;

   shm_buffer = buffer->shm_buffer;
   if (cp->buffer_ref.buffer && (cp->buffer_ref.buffer != buffer))
     {
        /* FIXME: wtf? */
     }
   else if (cp->buffer_ref.buffer)
     {
        return EINA_TRUE;
     }

   e_comp_wl_buffer_reference(&cp->buffer_ref, buffer);

   cp->w = cp->h = 0;
   cp->image_argb = EINA_FALSE;

   if (buffer->type == E_COMP_WL_BUFFER_TYPE_SHM)
     {
        shm_buffer = wl_shm_buffer_get(buffer->resource);
        if (!shm_buffer)
          {
             ERR("No shm_buffer resource:%u", wl_resource_get_id(buffer->resource));
             e_comp_wl_buffer_reference(&cp->buffer_ref, NULL);
             return EINA_FALSE;
          }

        buffer->shm_buffer = shm_buffer;
        cp->w = buffer->w;
        cp->h = buffer->h;

        switch (wl_shm_buffer_get_format(shm_buffer))
          {
           case WL_SHM_FORMAT_ARGB8888:
              cp->image_argb = EINA_TRUE;
              break;
           default:
              cp->image_argb = EINA_FALSE;
              break;
          }

        cp->data = wl_shm_buffer_get_data(shm_buffer);
        cp->shm_buffer = shm_buffer;
     }
   else if (buffer->type == E_COMP_WL_BUFFER_TYPE_NATIVE)
     {
        buffer->shm_buffer = NULL;
        cp->w = buffer->w;
        cp->h = buffer->h;
        cp->image_argb = EINA_FALSE; /* TODO: format */
        cp->data = NULL;

        /* TODO: Current buffer management process doesn't ensure
         * to render all committed buffer, it means there are buffers
         * never rendered. New attached buffer resources should be
         * managed and be pending if previous buffer is not rendered yet. */
        /* set size of image object to new buffer size */
        e_pixmap_size_get(cp, &bw, &bh);
        e_comp_object_size_update(cp->client->frame, bw, bh);
     }
   else if (buffer->type == E_COMP_WL_BUFFER_TYPE_TBM)
     {
        buffer->shm_buffer = NULL;
        cp->w = buffer->w;
        cp->h = buffer->h;
        cp->image_argb = EINA_FALSE; /* TODO: format */
        cp->data = NULL;
        cp->shm_buffer = NULL;

        /* TODO: Current buffer management process doesn't ensure
         * to render all committed buffer, it means there are buffers
         * never rendered. New attached buffer resources should be
         * managed and be pending if previous buffer is not rendered yet. */
        /* set size of image object to new buffer size */
        e_pixmap_size_get(cp, &bw, &bh);
        e_comp_object_size_update(cp->client->frame, bw, bh);

        /* buffer has no client resources */
        return EINA_TRUE;
     }
   else if (buffer->type == E_COMP_WL_BUFFER_TYPE_VIDEO)
     {
        buffer->shm_buffer = NULL;
        cp->w = buffer->w;
        cp->h = buffer->h;
        cp->image_argb = EINA_FALSE;
        cp->data = NULL;
        cp->shm_buffer = NULL;
     }
   else
     {
        ERR("Invalid resource:%u", wl_resource_get_id(buffer->resource));
        e_comp_wl_buffer_reference(&cp->buffer_ref, NULL);
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

E_API Eina_Bool
e_pixmap_image_exists(const E_Pixmap *cp)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(cp, EINA_FALSE);
   return (!!cp->data) || (e_comp->gl && (!cp->buffer->shm_buffer));
}

E_API Eina_Bool
e_pixmap_image_is_argb(const E_Pixmap *cp)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(cp, EINA_FALSE);
   switch (cp->type)
     {
      case E_PIXMAP_TYPE_WL:
        return ((cp->buffer_ref.buffer != NULL) && (cp->image_argb));
        default: break;
     }
   return EINA_FALSE;
}

E_API void
e_pixmap_image_data_ref(E_Pixmap *cp)
{
   EINA_SAFETY_ON_NULL_RETURN(cp);

   switch (cp->type)
     {
      case E_PIXMAP_TYPE_WL:
         if (cp->shm_buffer)
           {
              if (cp->data_pool) wl_shm_pool_unref(cp->data_pool);
              cp->data_pool = wl_shm_buffer_ref_pool(cp->shm_buffer);
           }
         break;
      default:
         break;
     }
   return;
}

E_API void *
e_pixmap_image_data_get(E_Pixmap *cp)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(cp, NULL);

   switch (cp->type)
     {
      case E_PIXMAP_TYPE_WL:
        return cp->data;
        break;
      default:
        break;
     }
   return NULL;
}

E_API Eina_Bool
e_pixmap_image_data_argb_convert(E_Pixmap *cp, void *pix, void *ipix, Eina_Rectangle *r, int stride)
{
   struct wl_shm_buffer *shm_buffer;
   uint32_t format;
   int i, x, y;
   unsigned int *src, *dst;

   EINA_SAFETY_ON_NULL_RETURN_VAL(cp, EINA_FALSE);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(cp->type == E_PIXMAP_TYPE_WL, EINA_FALSE);

   if (cp->image_argb) return EINA_TRUE;
   if (!cp->buffer_ref.buffer) return EINA_FALSE;

   shm_buffer = cp->buffer_ref.buffer->shm_buffer;
   if (!shm_buffer) return EINA_FALSE;

   format = wl_shm_buffer_get_format(shm_buffer);
   if (format == WL_SHM_FORMAT_XRGB8888)
     {
        dst = (unsigned int *)pix;
        src = (unsigned int *)ipix;

        for (y = 0; y < r->h; y++)
          {
             i = (r->y + y) * stride / 4 + r->x;
             for (x = 0; x < r->w; x++)
               dst[i+x] = 0xff000000 | src[i+x];
          }
        pix = (void *)dst;
     }

   return EINA_TRUE;
}

E_API void
e_pixmap_image_opaque_set(E_Pixmap *cp, int x, int y, int w, int h)
{
   EINA_SAFETY_ON_NULL_RETURN(cp);
   EINA_RECTANGLE_SET(&cp->opaque, x, y, w, h);
}

E_API void
e_pixmap_image_opaque_get(E_Pixmap *cp, int *x, int *y, int *w, int *h)
{
   EINA_SAFETY_ON_NULL_RETURN(cp);
   if (x) *x = cp->opaque.x;
   if (y) *y = cp->opaque.y;
   if (w) *w = cp->opaque.w;
   if (h) *h = cp->opaque.h;
}

E_API E_Comp_Client_Data *
e_pixmap_cdata_get(E_Pixmap *cp)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(cp, NULL);
   return (E_Comp_Client_Data*)cp->cdata;
}

E_API void
e_pixmap_cdata_set(E_Pixmap *cp, E_Comp_Client_Data *cdata)
{
   E_Comp_Wl_Client_Data *cd = (E_Comp_Wl_Client_Data*)cdata;

   EINA_SAFETY_ON_NULL_RETURN(cp);

   if (cp->cdata)
     {
        if (cp->own_cdata)
          {
             if (cd)
               {
                  cd->wl_surface = cp->cdata->wl_surface;
                  cd->scaler.viewport = cp->cdata->scaler.viewport;
                  cd->pending.buffer_viewport = cp->cdata->pending.buffer_viewport;
                  cd->opaque_state = cp->cdata->opaque_state;
               }

             E_FREE(cp->cdata);
             cp->own_cdata = EINA_FALSE;
          }
     }

   cp->cdata = cd;
}

E_API E_Pixmap_Hook *
e_pixmap_hook_add(E_Pixmap_Hook_Point hookpoint, E_Pixmap_Hook_Cb func, const void *data)
{
   E_Pixmap_Hook *ph;

   EINA_SAFETY_ON_TRUE_RETURN_VAL(hookpoint >= E_PIXMAP_HOOK_LAST, NULL);

   ph = E_NEW(E_Pixmap_Hook, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ph, NULL);
   ph->hookpoint = hookpoint;
   ph->func = func;
   ph->data = (void*)data;
   _e_pixmap_hooks[hookpoint] = eina_inlist_append(_e_pixmap_hooks[hookpoint],
                                                   EINA_INLIST_GET(ph));
   return ph;
}

E_API void
e_pixmap_hook_del(E_Pixmap_Hook *ph)
{
   ph->delete_me = 1;
   if (_e_pixmap_hooks_walking == 0)
     {
        _e_pixmap_hooks[ph->hookpoint] = eina_inlist_remove(_e_pixmap_hooks[ph->hookpoint],
                                                            EINA_INLIST_GET(ph));
        free(ph);
     }
   else
     _e_pixmap_hooks_delete++;
}

E_API Eina_Bool
e_pixmap_init(void)
{
   struct wl_global *global;

   if (!e_comp_wl)
     return EINA_FALSE;

   if (!e_comp_wl->wl.disp)
     return EINA_FALSE;

   global = wl_global_create(e_comp_wl->wl.disp, &tizen_surface_shm_interface,
                             2, NULL, _e_pixmap_tzsurf_shm_cb_bind);
   if (!global)
     return EINA_FALSE;

   return EINA_TRUE;
}

E_API void
e_pixmap_shutdown(void)
{
}

/* By this calling, client will destroy their buffer by this feature
 * implementation. the implementation for shared memory is in evas_shm engine.
 */
E_API void
e_pixmap_buffer_clear(E_Pixmap *cp, Eina_Bool only_free)
{
   E_Comp_Wl_Client_Data *cdata;
   struct wl_resource *cb;
   Eina_List *l, *ll;

   EINA_SAFETY_ON_NULL_RETURN(cp);

   /* disabled this feature */
   if (!(cp->client && cp->client->exp_iconify.buffer_flush))
     return;

   DBG("PIXMAP: Buffer Flush(%s) '%s'(%p) only_free:%d", cp->shm_flusher ? "SHM" : "NATIVE",
       cp->client->icccm.name?:"", cp->client, only_free);

   if (cp->shm_flusher)
     {
        if (only_free == EINA_TRUE)
          {
            if (TIZEN_SURFACE_SHM_FLUSHER_FREE_FLUSH_SINCE_VERSION <= wl_resource_get_version(cp->shm_flusher))
              {
                 tizen_surface_shm_flusher_send_free_flush(cp->shm_flusher);
              }
            else
              {
                 /*free_flush was supported from version 2*/
                 return;
              }
           }
         else
           {
              if (e_comp_is_on_overlay(cp->client))
                {
                   e_comp_hwc_client_end(cp->client, __FUNCTION__);

                   //TODO: this is temporary code..
                   if (cp->client->hwc_window)
                      e_hwc_window_deactivate(cp->client->hwc_window);
                }

              tizen_surface_shm_flusher_send_flush(cp->shm_flusher);
           }
     }
   else if (cp->buffer)
     {
        if (only_free)
          return;

        switch (cp->buffer->type)
          {
           case E_COMP_WL_BUFFER_TYPE_TBM:
           case E_COMP_WL_BUFFER_TYPE_NATIVE:
                {
                   struct wayland_tbm_client_queue *cqueue;

                   cqueue =
                      wayland_tbm_server_client_queue_get(e_comp_wl->tbm.server,
                                                          cp->client->comp_data->wl_surface);
                   if (!cqueue)
                     return;

                   wayland_tbm_server_client_queue_flush(cqueue);
                }
              break;
           default:
              /* Do nothing */
              break;
          }
     }
   else
     return;

   if (only_free == EINA_FALSE)
     {
        /* release the helded buffer by e_client */
        if (cp->client->comp_data)
          e_comp_wl_buffer_reference(&cp->client->comp_data->buffer_ref, NULL);

        /* composite object clear */
        e_comp_object_clear(cp->client->frame);

        /* pending frame event callback*/
        if (!cp->client->comp_data) return;
        cdata = (E_Comp_Wl_Client_Data *)cp->client->comp_data;
        EINA_LIST_FOREACH_SAFE(cdata->frames, l, ll, cb)
          {
             wl_callback_send_done(cb, ecore_time_unix_get() * 1000);
             wl_resource_destroy(cb);
          }
     }

}

EINTERN void *
e_pixmap_ref_resource_get(E_Pixmap *cp)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(cp, NULL);

   return cp->buffer_ref.buffer;
}
