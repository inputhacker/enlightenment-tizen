#include "e.h"

#include <tbm_surface.h>
#include <tbm_surface_internal.h>
#include <wayland-tbm-server.h>
#include <pixman.h>

#define CAPINF(f, ec, str, obj, x...)                                \
   do                                                                \
     {                                                               \
        if ((!ec))                                                   \
          INF("EWL|%20.20s|            |             |%10.10s|%8p|"f,\
              "CAPTURE", (str), (obj), ##x);                         \
        else                                                         \
          INF("EWL|%20.20s|w:0x%08zx|ec:%8p|%10.10s|%p|"f,           \
              "CAPTURE",                                             \
              (e_client_util_win_get(ec)),                           \
              (ec),                                                  \
              (str), (obj),                                          \
              ##x);                                                  \
     }                                                               \
   while (0)

#define CAPDBG(f, ec, str, obj, x...)                                \
   do                                                                \
     {                                                               \
        if (!ec)                                                     \
          DBG("EWL|%20.20s|            |             |%10.10s|%8p|"f,\
              "CAPTURE", (str), (obj), ##x);                         \
        else                                                         \
          DBG("EWL|%20.20s|w:0x%08zx|ec:%8p|%10.10s|%p|"f,           \
              "CAPTURE",                                             \
              (e_client_util_win_get(ec)),                           \
              (ec),                                                  \
              (str), (obj),                                          \
              ##x);                                                  \
     }                                                               \
   while (0)

typedef struct _E_Capture_Client E_Capture_Client;

static Eina_Hash *_client_hash;

struct _E_Capture_Client
{
   E_Client *ec;

   E_Comp_Wl_Buffer_Ref buffer_ref;
   Ecore_Thread *th;

   int ref_as_child;
};

typedef struct
{
   void *shm_buffer_ptr;
   int shm_buffer_stride;
   int shm_buffer_h;
   unsigned int shm_buffer_format;
   struct wl_shm_pool *shm_pool;

   tbm_surface_h tbm_surface;

   uint32_t transform;

   int x, y, w, h;
   E_Client *ec;
   E_Client *parent;
} Capture_Data;

typedef struct {
     void *shm_buffer_ptr;
     int shm_buffer_stride;
     int shm_buffer_h;
     unsigned int shm_buffer_format;
     struct wl_shm_pool *shm_pool;

     tbm_surface_h tbm_surface;

     uint32_t transform;

     const char *image_path;
     E_Client *ec;

     Capture_Data *child_data;
     Eina_Stringshare *image_dir;
     Eina_Stringshare *image_name;

     E_Capture_Client_Save_End_Cb func_end;
     void *data;
} Thread_Data;

static pixman_format_code_t
_e_capture_image_data_pixman_format_get_from_tbm_surface(tbm_format format)
{
   switch(format)
     {
      case TBM_FORMAT_ARGB8888: return PIXMAN_a8r8g8b8;
      case TBM_FORMAT_XRGB8888: return PIXMAN_x8r8g8b8;
      default:                  return PIXMAN_x8r8g8b8;
     }
}

static pixman_format_code_t
_e_capture_image_data_pixman_format_get_from_shm_buffer(uint32_t format)
{
   switch(format)
     {
      case WL_SHM_FORMAT_ARGB8888: return PIXMAN_a8r8g8b8;
      case WL_SHM_FORMAT_XRGB8888: return PIXMAN_x8r8g8b8;
      default:                     return PIXMAN_x8r8g8b8;
     }
}

static E_Capture_Client *
_e_capture_client_find(E_Client *ec)
{
   E_Capture_Client *ecc;

   EINA_SAFETY_ON_NULL_RETURN_VAL(_client_hash, NULL);

   ecc = eina_hash_find(_client_hash, &ec);
   return ecc;
}

static E_Capture_Client *
_e_capture_client_get(E_Client *ec)
{
   E_Capture_Client *ecc = NULL;

   ecc = _e_capture_client_find(ec);
   if (!ecc)
     {
        if (e_object_is_del(E_OBJECT(ec)))
          return NULL;

        ecc = E_NEW(E_Capture_Client, 1);
        if (!ecc) return NULL;

        ecc->ec = ec;
        eina_hash_add(_client_hash, &ec, ecc);
     }

   return ecc;
}

static void
_e_capture_client_destroy(E_Capture_Client *ecc)
{
   if (!ecc) return;

   CAPDBG("capture client destroy", ecc->ec, "ECC", ecc);

   if (ecc->th)
     {
        CAPDBG("IMG save is cancelled. th:%p pending destroy",
               ecc->ec, "ECC", ecc, ecc->th);
        ecore_thread_cancel(ecc->th);
        return;
     }

   if (ecc->ref_as_child > 0)
     {
        CAPDBG("Parent IMG save is running. ref_as_child:%d pending destroy",
               ecc->ec, "ECC", ecc, ecc->ref_as_child);
        return;
     }

   if (_client_hash)
     eina_hash_del_by_data(_client_hash, ecc);

   E_FREE(ecc);
}

static tbm_surface_h
_e_capture_image_data_transform(Thread_Data *td, int w, int h)
{
   // for base
   tbm_surface_h transform_surface = NULL;
   tbm_surface_info_s info;
   int tw = 0, th = 0;
   pixman_image_t *src_img = NULL, *dst_img = NULL;
   pixman_format_code_t src_format, dst_format;
   pixman_transform_t t;
   struct pixman_f_transform ft;
   unsigned char *src_ptr = NULL, *dst_ptr = NULL;
   int c = 0, s = 0, tx = 0, ty = 0;

   // for child
   tbm_surface_h c_transform_surface = NULL;
   tbm_surface_info_s c_info;
   pixman_image_t *c_src_img = NULL, *c_dst_img = NULL;
   pixman_format_code_t c_src_format, c_dst_format;
   pixman_transform_t c_t;
   struct pixman_f_transform c_ft;
   unsigned char *c_src_ptr = NULL, *c_dst_ptr = NULL;
   int c_x, c_y, c_w, c_h;
   int c_tx, c_ty, c_tw, c_th;

   EINA_SAFETY_ON_NULL_RETURN_VAL(td, NULL);

   if (td->transform > WL_OUTPUT_TRANSFORM_270) return NULL;

   if (td->tbm_surface)
     {
        src_format = _e_capture_image_data_pixman_format_get_from_tbm_surface(tbm_surface_get_format(td->tbm_surface));
        dst_format = src_format;

        tbm_surface_map(td->tbm_surface, TBM_SURF_OPTION_READ, &info);
        src_ptr = info.planes[0].ptr;

        src_img = pixman_image_create_bits(src_format, w, h, (uint32_t*)src_ptr, info.planes[0].stride);
        EINA_SAFETY_ON_NULL_GOTO(src_img, clean_up);
     }
   else if (td->shm_buffer_ptr)
     {
        src_format = _e_capture_image_data_pixman_format_get_from_shm_buffer(td->shm_buffer_format);
        dst_format = src_format;

        src_ptr = td->shm_buffer_ptr;
        src_img = pixman_image_create_bits(src_format, w, h, (uint32_t*)src_ptr, w * 4);
        EINA_SAFETY_ON_NULL_GOTO(src_img, clean_up);
     }
   else
     {
        ERR("invalid buffer");
        return NULL;
     }

   if (td->transform == WL_OUTPUT_TRANSFORM_90)
     {
        c = 0, s = -1, tx = -h;
        tw = h, th = w;
     }
   else if (td->transform == WL_OUTPUT_TRANSFORM_180)
     {
        c = -1, s = 0, tx = -w, ty = -h;
        tw = w, th = h;
     }
   else if (td->transform == WL_OUTPUT_TRANSFORM_270)
     {
        c = 0, s = 1, ty = -w;
        tw = h, th = w;
     }
   else
     {
        c = 1, s = 0;
        tw = w, th = h;
     }

   transform_surface = tbm_surface_create(tw, th, tbm_surface_get_format(td->tbm_surface));
   EINA_SAFETY_ON_NULL_GOTO(transform_surface, clean_up);

   tbm_surface_map(transform_surface, TBM_SURF_OPTION_WRITE, &info);
   dst_ptr = info.planes[0].ptr;

   dst_img = pixman_image_create_bits(dst_format, tw, th, (uint32_t*)dst_ptr, info.planes[0].stride);
   EINA_SAFETY_ON_NULL_GOTO(dst_img, clean_up);

   pixman_f_transform_init_identity(&ft);
   pixman_f_transform_translate(&ft, NULL, tx, ty);
   pixman_f_transform_rotate(&ft, NULL, c, s);

   pixman_transform_from_pixman_f_transform(&t, &ft);
   pixman_image_set_transform(src_img, &t);

   pixman_image_composite(PIXMAN_OP_SRC, src_img, NULL, dst_img, 0, 0, 0, 0, 0, 0, tw, th);

   // for child data
   if (td->child_data)
     {
        c_x = td->child_data->x;
        c_y = td->child_data->y;
        c_w = td->child_data->w;
        c_h = td->child_data->h;

        c_tx = 0;
        c_ty = 0;
        c_tw = c_w;
        c_th = c_h;

        if (td->child_data->tbm_surface)
          {
             c_src_format = _e_capture_image_data_pixman_format_get_from_tbm_surface(tbm_surface_get_format(td->child_data->tbm_surface));
             c_dst_format = c_src_format;

             tbm_surface_map(td->child_data->tbm_surface, TBM_SURF_OPTION_READ, &c_info);
             c_src_ptr = c_info.planes[0].ptr;

             c_src_img = pixman_image_create_bits(c_src_format, c_w, c_h, (uint32_t*)c_src_ptr, c_info.planes[0].stride);
             EINA_SAFETY_ON_NULL_GOTO(c_src_img, clean_up);
          }
        else if (td->child_data->shm_buffer_ptr)
          {
             c_src_format = _e_capture_image_data_pixman_format_get_from_shm_buffer(td->child_data->shm_buffer_format);
             c_dst_format = c_src_format;

             c_src_ptr = td->child_data->shm_buffer_ptr;
             c_src_img = pixman_image_create_bits(c_src_format, c_w, c_h, (uint32_t*)c_src_ptr, c_w * 4);
             EINA_SAFETY_ON_NULL_GOTO(c_src_img, clean_up);
          }
        else
          {
             ERR("invalid buffer");
             goto clean_up;
          }

        if (td->child_data->transform == WL_OUTPUT_TRANSFORM_90)
          {
             c = 0, s = -1, c_tx = -c_h;
             c_tw = c_h, c_th = c_w;
          }
        else if (td->child_data->transform == WL_OUTPUT_TRANSFORM_180)
          {
             c = -1, s = 0, c_tx = -c_w, c_ty = -c_h;
             c_tw = c_w, c_th = c_h;
          }
        else if (td->child_data->transform == WL_OUTPUT_TRANSFORM_270)
          {
             c = 0, s = 1, c_ty = -c_w;
             c_tw = c_h, c_th = c_w;
          }
        else
          {
             c = 1, s = 0;
             c_tw = c_w, c_th = c_h;
          }

        c_transform_surface = tbm_surface_create(c_tw, c_th, tbm_surface_get_format(td->child_data->tbm_surface));
        EINA_SAFETY_ON_NULL_GOTO(c_transform_surface, clean_up);

        tbm_surface_map(c_transform_surface, TBM_SURF_OPTION_WRITE, &c_info);
        c_dst_ptr = c_info.planes[0].ptr;

        c_dst_img = pixman_image_create_bits(c_dst_format, c_tw, c_th, (uint32_t*)c_dst_ptr, c_info.planes[0].stride);
        EINA_SAFETY_ON_NULL_GOTO(c_dst_img, clean_up);

        pixman_f_transform_init_identity(&c_ft);
        pixman_f_transform_translate(&c_ft, NULL, c_tx, c_ty);
        pixman_f_transform_rotate(&c_ft, NULL, c, s);

        pixman_transform_from_pixman_f_transform(&c_t, &c_ft);
        pixman_image_set_transform(c_src_img, &c_t);

        pixman_image_composite(PIXMAN_OP_SRC, c_src_img, NULL, c_dst_img, 0, 0, 0, 0, 0, 0, c_tw, c_th);

        CAPDBG("image composite with child. child(win:%zx, ec:%p)",
               td->ec, "ECC", NULL,
               e_client_util_win_get(td->child_data->ec), td->child_data->ec);

        pixman_image_composite(PIXMAN_OP_OVER, c_dst_img, NULL, dst_img, 0, 0, 0, 0, c_x, c_y, tw, th);
     }

clean_up:
   if (td->child_data)
     {
        if (c_src_ptr && td->child_data->tbm_surface) tbm_surface_unmap(td->child_data->tbm_surface);
        if (c_dst_ptr) tbm_surface_unmap(c_transform_surface);

        if (c_transform_surface)
          {
             tbm_surface_destroy(c_transform_surface);
             c_transform_surface = NULL;
          }

        if (c_src_img) pixman_image_unref(c_src_img);
        if (c_dst_img) pixman_image_unref(c_dst_img);
     }

   if (src_ptr && td->tbm_surface) tbm_surface_unmap(td->tbm_surface);
   if (dst_ptr) tbm_surface_unmap(transform_surface);

   // if dst_img is null, then trasform is failed. So we should destroy transform_surface.
   if (!dst_img)
     {
        tbm_surface_destroy(transform_surface);
        transform_surface = NULL;
     }

   if (src_img) pixman_image_unref(src_img);
   if (dst_img) pixman_image_unref(dst_img);

   return transform_surface;
}

static Eina_Bool
_e_capture_client_image_data_save(Thread_Data *td)
{
   void *shm_buffer_ptr = NULL;
   tbm_surface_h tbm_surface = NULL, transform_surface = NULL;
   int w, h, stride;
   void *ptr;
   int ret = 0;

   EINA_SAFETY_ON_NULL_RETURN_VAL(td, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(td->image_dir, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(td->image_name, EINA_FALSE);

   shm_buffer_ptr = td->shm_buffer_ptr;
   tbm_surface = td->tbm_surface;

   if (shm_buffer_ptr)
     {
         stride = td->shm_buffer_stride;
         w = stride / 4;
         h = td->shm_buffer_h;

         transform_surface = _e_capture_image_data_transform(td, w, h);
         if (transform_surface)
           {
              tbm_surface_info_s info;
              tbm_surface_map(transform_surface, TBM_SURF_OPTION_READ|TBM_SURF_OPTION_WRITE, &info);
              ptr = info.planes[0].ptr;
           }
         else
           {
              ptr = shm_buffer_ptr;
              EINA_SAFETY_ON_NULL_RETURN_VAL(ptr, EINA_FALSE);
           }

         ret = tbm_surface_internal_capture_shm_buffer(ptr, w, h, stride, td->image_dir, td->image_name, "png");

         if (transform_surface)
           {
              tbm_surface_unmap(transform_surface);
              tbm_surface_destroy(transform_surface);
           }

         if (!ret)
           {
              CAPDBG("IMG save fail: %s/%s.png",
                     td->ec, "ECC", NULL, td->image_dir, td->image_name);
              return EINA_FALSE;
           }
     }
   else if (tbm_surface)
     {
         w = tbm_surface_get_width(tbm_surface);
         EINA_SAFETY_ON_FALSE_RETURN_VAL(w > 0, EINA_FALSE);
         h = tbm_surface_get_height(tbm_surface);
         EINA_SAFETY_ON_FALSE_RETURN_VAL(h > 0, EINA_FALSE);

         transform_surface = _e_capture_image_data_transform(td, w, h);
         if (transform_surface)
           tbm_surface = transform_surface;

         CAPDBG("IMG save. transform_surface=%p transform=%d",
                td->ec, "ECC", NULL, transform_surface, td->transform);

         ret = tbm_surface_internal_capture_buffer(tbm_surface, td->image_dir, td->image_name, "png");

         if (transform_surface)
           tbm_surface_destroy(transform_surface);

         if (!ret)
           {
              CAPDBG("IMG save fail: %s/%s.png",
                     td->ec, "ECC", NULL, td->image_dir, td->image_name);
              return EINA_FALSE;
           }
     }
   else
     {
         CAPDBG("IMG save fail: %s/%s.png",
                td->ec, "ECC", NULL, td->image_dir, td->image_name);
         return EINA_FALSE;
     }

   td->image_path = eina_stringshare_printf("%s/%s.png",td->image_dir, td->image_name);
   return EINA_TRUE;
}

static void
_e_capture_client_child_data_release(Thread_Data *td)
{
   E_Capture_Client *ecc;
   E_Client *ec;

   if (!td) return;
   if (!td->child_data) return;

   if (td->child_data->tbm_surface)
     tbm_surface_internal_unref(td->child_data->tbm_surface);

   if (td->child_data->shm_pool)
     wl_shm_pool_unref(td->child_data->shm_pool);

   ec = td->child_data->ec;
   if (!ec) return;

   ecc = _e_capture_client_find(ec);
   if (!ecc) return;

   e_comp_wl_buffer_reference(&ecc->buffer_ref, NULL);
   ecc->ref_as_child--;
   CAPDBG("Child data release. ref_as_child:%d", ec, "ECC", ecc, ecc->ref_as_child);

   _e_capture_client_destroy(ecc);

   e_object_unref(E_OBJECT(ec));

   E_FREE(td->child_data);
}

static Eina_Bool
_e_capture_client_child_data_create(Thread_Data *td, E_Client *ec)
{
   E_Capture_Client *ecc;
   Capture_Data *capture_data;

   E_Comp_Wl_Buffer *buffer = NULL;
   struct wl_shm_buffer *shm_buffer;
   struct wl_shm_pool *shm_pool;
   void *shm_buffer_ptr = NULL;
   int shm_buffer_stride, shm_buffer_h;
   tbm_surface_h tbm_surface;

   if (!td) return EINA_FALSE;
   if (!ec) return EINA_FALSE;

   ecc = _e_capture_client_get(ec);
   if (!ecc) return EINA_FALSE;

   if (!(buffer = e_pixmap_resource_get(ec->pixmap))) return EINA_FALSE;

   if (td->child_data)
     {
        // how do we handle this case?
        _e_capture_client_child_data_release(td);
     }

   capture_data = E_NEW(Capture_Data, 1);
   if (!capture_data) return EINA_FALSE;

   e_object_ref(E_OBJECT(ec));
   capture_data->ec = ec;

   capture_data->x = ec->x;
   capture_data->y = ec->y;
   capture_data->w = ec->w;
   capture_data->h = ec->h;

   capture_data->transform = e_comp_wl_output_buffer_transform_get(ec);

   e_comp_wl_buffer_reference(&ecc->buffer_ref, buffer);
   switch (buffer->type)
     {
      case E_COMP_WL_BUFFER_TYPE_SHM:
         shm_buffer = wl_shm_buffer_get(buffer->resource);
         if (!shm_buffer) goto end;

         shm_buffer_ptr = wl_shm_buffer_get_data(shm_buffer);
         if (!shm_buffer_ptr) goto end;

         shm_buffer_stride = wl_shm_buffer_get_stride(shm_buffer);
         if (shm_buffer_stride <= 0) goto end;

         shm_buffer_h = wl_shm_buffer_get_height(shm_buffer);
         if (shm_buffer_h <= 0) goto end;

         shm_pool = wl_shm_buffer_ref_pool(shm_buffer);
         if (!shm_pool) goto end;

         capture_data->shm_buffer_format = wl_shm_buffer_get_format(shm_buffer);
         capture_data->shm_buffer_ptr = shm_buffer_ptr;
         capture_data->shm_buffer_stride = shm_buffer_stride;
         capture_data->shm_buffer_h = shm_buffer_h;
         capture_data->shm_pool = shm_pool;
         break;

      case E_COMP_WL_BUFFER_TYPE_NATIVE:
      case E_COMP_WL_BUFFER_TYPE_VIDEO:
         tbm_surface = wayland_tbm_server_get_surface(e_comp_wl->tbm.server, buffer->resource);
         if (!tbm_surface) goto end;

         tbm_surface_internal_ref(tbm_surface);
         capture_data->tbm_surface = tbm_surface;
         break;

      case E_COMP_WL_BUFFER_TYPE_TBM:
         tbm_surface = buffer->tbm_surface;
         if (!tbm_surface) goto end;

         tbm_surface_internal_ref(tbm_surface);
         capture_data->tbm_surface = tbm_surface;
         break;

      default:
         goto end;
     }

   td->child_data = capture_data;
   ecc->ref_as_child++;
   CAPDBG("Child data create. ref_as_child:%d", ec, "ecc", ecc, ecc->ref_as_child);
   return EINA_TRUE;

end:
   e_comp_wl_buffer_reference(&ecc->buffer_ref, NULL);
   e_object_unref(E_OBJECT(ec));
   E_FREE(capture_data);

   return EINA_FALSE;
}

static Eina_Bool
_e_capture_client_child_is_placed_above(E_Client *child, E_Client *ec)
{
   E_Client *above = NULL;

   for (above = e_client_above_get(ec); above; above = e_client_above_get(above))
     {
        if (above == child)
          return EINA_TRUE;
     }

   return EINA_FALSE;
}

static Eina_Bool
_e_capture_client_child_data_check(Thread_Data *td)
{
   Eina_List *list, *l;
   E_Client *ec = NULL;
   E_Client *child_ec = NULL;

   ec = td->ec;
   if (!ec) return EINA_FALSE;

   list = eina_list_clone(ec->transients);

   EINA_LIST_FOREACH(list, l, child_ec)
     {
        if (!child_ec->comp_data) continue;
        if (!child_ec->comp_data->mapped) continue;

        if (child_ec->iconic && child_ec->exp_iconify.by_client)
          continue;

        if (child_ec->bg_state)
          continue;

        if (!e_policy_client_is_keyboard(child_ec))
          continue;

        if (!_e_capture_client_child_is_placed_above(child_ec, ec))
          continue;

        _e_capture_client_child_data_create(td, child_ec);
        break;
     }

   eina_list_free(list);

   return EINA_TRUE;
}

 void
_e_capture_client_save_thread_data_free(Thread_Data *td)
{
   if (td->child_data)
      {
         _e_capture_client_child_data_release(td);
      }

    if (td->ec)
      e_object_unref(E_OBJECT(td->ec));
    if (td->tbm_surface)
      tbm_surface_internal_unref(td->tbm_surface);
    if (td->shm_pool)
      wl_shm_pool_unref(td->shm_pool);

    eina_stringshare_del(td->image_path);
    eina_stringshare_del(td->image_dir);
    eina_stringshare_del(td->image_name);

    E_FREE(td);
}

static void
_e_capture_thread_client_save_run(void *data, Ecore_Thread *th)
{
   Thread_Data *td;
   E_Client *ec;

   if (!(td = data)) return;

   ec = td->ec;
   if (!ec) return;
   if (ecore_thread_check(th)) return;

   if (_e_capture_client_image_data_save(td))
     {
        CAPDBG("IMG save success. th:%p to %s", ec, "ECC", NULL, th, td->image_path);
     }
   else
     {
        CAPDBG("IMG save failure. th:%p to %s/%s.png", ec, "ECC", NULL, th, td->image_dir, td->image_name);
     }
}

static void
_e_capture_thread_client_save_done(void *data, Ecore_Thread *th)
{
   Thread_Data *td = data;
   E_Client *ec;
   E_Capture_Client *ecc = NULL;
   E_Capture_Save_State st = E_CAPTURE_SAVE_STATE_DONE;

   if (!td) return;

   ec = td->ec;
   if (!ec) goto end;

   ecc = _e_capture_client_find(ec);
   if (!ecc) goto end;

   CAPDBG("IMG save DONE. th:%p", ec, "ECC", ecc, th);

   if (th == ecc->th)
     {
        ecc->th = NULL;
        e_comp_wl_buffer_reference(&ecc->buffer_ref, NULL);
     }
   else
     {
        CAPDBG("IMG not matched. del. td:%s",
               ec, "ECC", ecc, td->image_path);
        ecore_file_remove(td->image_path);
        eina_stringshare_del(td->image_path);
        td->image_path = NULL;
     }

   if (td->image_path)
     {
        CAPDBG("Client save Thread::DONE path(%s)",
               ecc->ec, "ECC", ecc, td->image_path);
     }
   else
     {
        st = E_CAPTURE_SAVE_STATE_INVALID;
     }

   /*TODO: How can I do, if ecc was deleted or ec was del*/
   if (td->func_end)
     td->func_end(td->data, ec, td->image_path, st);

   _e_capture_client_destroy(ecc);

end:
   _e_capture_client_save_thread_data_free(td);
}

static void
_e_capture_thread_client_save_cancel(void *data, Ecore_Thread *th)
{
   Thread_Data *td = data;
   E_Client *ec;
   E_Capture_Client *ecc = NULL;
   E_Capture_Save_State st = E_CAPTURE_SAVE_STATE_CANCEL;

   if (!td) return;

   ec = td->ec;
   if (!ec) goto end;

   ecc = _e_capture_client_find(ec);
   if (!ecc) goto end;

   CAPDBG("IMG save CANCELED. th:%p %s",
          ecc->ec, "ECC", ecc, th, td->image_path);

   if (th == ecc->th)
     {
        ecc->th = NULL;
        e_comp_wl_buffer_reference(&ecc->buffer_ref, NULL);

        if (td->child_data)
          {
             _e_capture_client_child_data_release(td);
          }
     }

   if (td->image_path)
     {
        CAPDBG("Client save Thread::CANCEL path(%s)",
               ecc->ec, "ECC", ecc, td->image_path);
        st = E_CAPTURE_SAVE_STATE_DONE;
     }

   /*TODO: How can I do, if ecc was deleted or ec was del*/
   if (td->func_end)
     td->func_end(td->data, ec, td->image_path, st);

   _e_capture_client_destroy(ecc);

end:
   _e_capture_client_save_thread_data_free(td);
}

/* Stop capture job when the window is uniconified while capturing
 * on another thread.
 *
 * If a commit event occurs for iconified window, then does cancellation
 * for capture thread.
 *
 * It can be using ecore_thread_check API to check whether the capture
 * job is done.
 */
static E_Capture_Save_State
_e_capture_client_save(E_Capture_Client *ecc,
                             Eina_Stringshare *dir,
                             Eina_Stringshare *name,
                             E_Capture_Client_Save_End_Cb func_end,
                             void *data,
                             Eina_Bool skip_child)
{
   E_Client *ec;
   E_Comp_Wl_Buffer *buffer = NULL;
   Thread_Data *td;
   struct wl_shm_buffer *shm_buffer;
   struct wl_shm_pool *shm_pool;
   void *shm_buffer_ptr = NULL;
   int shm_buffer_stride, shm_buffer_h;
   tbm_surface_h tbm_surface;

   if (!(ec = ecc->ec)) return E_CAPTURE_SAVE_STATE_INVALID;
   if (!(buffer = e_pixmap_resource_get(ec->pixmap))) return E_CAPTURE_SAVE_STATE_INVALID;

   if (ecc->th)
     {
        CAPDBG("ALREADY doing capture", ecc->ec, "ECC", ecc);
        return E_CAPTURE_SAVE_STATE_BUSY;
     }

   td = E_NEW(Thread_Data, 1);
   if (!td) return E_CAPTURE_SAVE_STATE_INVALID;

   e_object_ref(E_OBJECT(ec));
   td->ec = ec;

   td->transform = e_comp_wl_output_buffer_transform_get(ec);
   td->image_dir = eina_stringshare_ref(dir);
   td->image_name = eina_stringshare_ref(name);
   td->func_end = func_end;
   td->data = data;

   e_comp_wl_buffer_reference(&ecc->buffer_ref, buffer);
   switch (buffer->type)
     {
      case E_COMP_WL_BUFFER_TYPE_SHM:
         shm_buffer = wl_shm_buffer_get(buffer->resource);
         if (!shm_buffer) goto end;

         shm_buffer_ptr = wl_shm_buffer_get_data(shm_buffer);
         if (!shm_buffer_ptr) goto end;

         shm_buffer_stride = wl_shm_buffer_get_stride(shm_buffer);
         if (shm_buffer_stride <= 0) goto end;

         shm_buffer_h = wl_shm_buffer_get_height(shm_buffer);
         if (shm_buffer_h <= 0) goto end;

         shm_pool = wl_shm_buffer_ref_pool(shm_buffer);
         if (!shm_pool) goto end;

         td->shm_buffer_format = wl_shm_buffer_get_format(shm_buffer);
         td->shm_buffer_ptr = shm_buffer_ptr;
         td->shm_buffer_stride = shm_buffer_stride;
         td->shm_buffer_h = shm_buffer_h;
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

   if (!skip_child)
      _e_capture_client_child_data_check(td);

   ecc->th = ecore_thread_run(_e_capture_thread_client_save_run,
                              _e_capture_thread_client_save_done,
                              _e_capture_thread_client_save_cancel,
                              td);
   CAPDBG("IMG save START. th:%p to %s/%s.png",
          ec, "ECC", ecc, ecc->th, td->image_dir, td->image_name);
   return E_CAPTURE_SAVE_STATE_START;

end:
   e_comp_wl_buffer_reference(&ecc->buffer_ref, NULL);
   e_object_unref(E_OBJECT(ec));

   _e_capture_client_child_data_release(td);
   E_FREE(td);

   _e_capture_client_destroy(ecc);

   return E_CAPTURE_SAVE_STATE_INVALID;
}

static void
_e_capture_client_save_cancel(E_Capture_Client *ecc)
{
   if (!ecc) return;

   if (ecc->th)
     {
        CAPDBG("IMG save could be cancelled. UNICONIFY th:%p(cancel:%d) iconic:%d ec_del:%d",
               ecc->ec, "ECC", ecc,
               ecc->th, ecore_thread_check(ecc->th),
               ecc->ec ? ecc->ec->iconic : 0,
               e_object_is_del(E_OBJECT(ecc->ec)));
        if (!ecore_thread_check(ecc->th) &&
            !e_object_is_del(E_OBJECT(ecc->ec)))
          {
             CAPDBG("IMG save CANCELLED.", ecc->ec, "ECC", ecc);
             ecore_thread_cancel(ecc->th);
          }
     }
}

/* NOTE :
 * e_comp_wl_capture is NOT in charge of manage created image file,
 * except for failure case to capture a client image.
 * The caller SHOULD manage the created file.
 */

E_API E_Capture_Save_State
e_comp_wl_capture_client_image_save(E_Client *ec,
                            const char* dir, const char* name,
                            E_Capture_Client_Save_End_Cb func_end, void *data,
                            Eina_Bool skip_child)
{
   E_Capture_Client *ecc;
   Eina_Stringshare *_dir, *_name;
   E_Capture_Save_State ret;

   EINA_SAFETY_ON_NULL_RETURN_VAL(dir, E_CAPTURE_SAVE_STATE_INVALID);
   EINA_SAFETY_ON_NULL_RETURN_VAL(name,E_CAPTURE_SAVE_STATE_INVALID);

   if (e_object_is_del(E_OBJECT(ec))) return E_CAPTURE_SAVE_STATE_INVALID;

   ecc = _e_capture_client_get(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ecc, E_CAPTURE_SAVE_STATE_INVALID);

   _dir = eina_stringshare_add(dir);
   _name = eina_stringshare_add(name);

   ret = _e_capture_client_save(ecc, _dir, _name, func_end, data, skip_child);

   eina_stringshare_del(dir);
   eina_stringshare_del(name);

   return ret;
}

E_API void
e_comp_wl_capture_client_image_save_cancel(E_Client *ec)
{
   E_Capture_Client *ecc;

   ecc = _e_capture_client_find(ec);
   _e_capture_client_save_cancel(ecc);
}

EINTERN void
e_comp_wl_capture_init(void)
{
   Eina_Hash *client_hash;

   client_hash = eina_hash_pointer_new(NULL);
   EINA_SAFETY_ON_NULL_RETURN(client_hash);

   _client_hash = client_hash;
}

EINTERN void
e_comp_wl_capture_shutdown(void)
{
   if (!_client_hash) return;

   E_FREE_FUNC(_client_hash, eina_hash_free);
}

