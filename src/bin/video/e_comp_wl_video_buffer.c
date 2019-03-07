#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "e.h"
#include <wayland-tbm-server.h>
#include <sys/mman.h>
#include <pixman.h>
#include <tdm_helper.h>

//#define DEBUG_LIFECYCLE

#define BER(fmt,arg...)   ERR("%d: "fmt, vbuf ? vbuf->stamp : 0, ##arg)
#define BWR(fmt,arg...)   WRN("%d: "fmt, vbuf ? vbuf->stamp : 0, ##arg)
#define BIN(fmt,arg...)   INF("%d: "fmt, vbuf ? vbuf->stamp : 0, ##arg)
#define BDB(fmt,arg...)   DBG("%d: "fmt, vbuf ? vbuf->stamp : 0, ##arg)

#define VBUF_RETURN_IF_FAIL(cond) \
   {if (!(cond)) { BER("'%s' failed", #cond); return; }}
#define VBUF_RETURN_VAL_IF_FAIL(cond, val) \
   {if (!(cond)) { BER("'%s' failed", #cond); return val; }}

#ifdef DEBUG_LIFECYCLE
#undef BDB
#define BDB BIN
#endif

typedef struct _VBufFreeFuncInfo
{
   VBuf_Free_Func func;
   void *data;
} VBufFreeFuncInfo;

static void _e_comp_wl_video_buffer_cb_destroy(struct wl_listener *listener, void *data);
static void _e_comp_wl_video_buffer_free(E_Comp_Wl_Video_Buf *vbuf);
#define e_comp_wl_video_buffer_free(b) _e_comp_wl_video_buffer_free(b)

static Eina_List *vbuf_lists;

static E_Comp_Wl_Video_Buf*
_find_vbuf(uint stamp)
{
   E_Comp_Wl_Video_Buf *vbuf;
   Eina_List *l;

   if (!vbuf_lists)
     return NULL;

   EINA_LIST_FOREACH(vbuf_lists, l, vbuf)
     {
        if (vbuf->stamp == stamp)
          return vbuf;
     }

   return NULL;
}

static Eina_Bool
_e_comp_wl_video_buffer_access_data_begin(E_Comp_Wl_Video_Buf *vbuf)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(vbuf, EINA_FALSE);

   vbuf->ptrs[0] = vbuf->ptrs[1] = vbuf->ptrs[2] = NULL;

   if (vbuf->type == E_COMP_WL_VIDEO_BUF_TYPE_SHM)
     {
        struct wl_shm_buffer *shm_buffer = wl_shm_buffer_get(vbuf->resource);
        EINA_SAFETY_ON_NULL_RETURN_VAL(shm_buffer, EINA_FALSE);
        vbuf->ptrs[0] = wl_shm_buffer_get_data(shm_buffer);
        EINA_SAFETY_ON_NULL_RETURN_VAL(vbuf->ptrs[0], EINA_FALSE);
        return EINA_TRUE;
     }
   else if (vbuf->type == E_COMP_WL_VIDEO_BUF_TYPE_TBM)
     {
        int i, j;
        tbm_bo bos[4] = {0,};

        for (i = 0; i < 3; i++)
          {
             tbm_bo_handle bo_handles;

             bos[i] = tbm_surface_internal_get_bo(vbuf->tbm_surface, i);
             if (!bos[i]) continue;

             bo_handles = tbm_bo_map(bos[i], TBM_DEVICE_CPU, TBM_OPTION_READ);
             if (!bo_handles.ptr)
               {
                  for (j = 0; j < i; j++)
                    tbm_bo_unmap(bos[j]);
                  return EINA_FALSE;
               }

             vbuf->ptrs[i] = bo_handles.ptr;
          }

        EINA_SAFETY_ON_NULL_RETURN_VAL(vbuf->ptrs[0], EINA_FALSE);

        switch(vbuf->tbmfmt)
          {
           case TBM_FORMAT_YVU420:
           case TBM_FORMAT_YUV420:
              if (!vbuf->ptrs[1])
                vbuf->ptrs[1] = vbuf->ptrs[0];
              if (!vbuf->ptrs[2])
                vbuf->ptrs[2] = vbuf->ptrs[1];
              break;
           case TBM_FORMAT_NV12:
           case TBM_FORMAT_NV21:
              if (!vbuf->ptrs[1])
                vbuf->ptrs[1] = vbuf->ptrs[0];
              break;
           default:
              break;
          }

        return EINA_TRUE;
     }

   return EINA_FALSE;
}

static void
_e_comp_wl_video_buffer_access_data_end(E_Comp_Wl_Video_Buf *vbuf)
{
   EINA_SAFETY_ON_NULL_RETURN(vbuf);

   if (vbuf->type == E_COMP_WL_VIDEO_BUF_TYPE_SHM)
     {
        vbuf->ptrs[0] = NULL;
     }
   else if (vbuf->type == E_COMP_WL_VIDEO_BUF_TYPE_TBM)
     {
        int i;
        tbm_bo bos[4] = {0,};

        for (i = 0; i < 3; i++)
          {
             bos[i] = tbm_surface_internal_get_bo(vbuf->tbm_surface, i);
             if (!bos[i]) continue;

             tbm_bo_unmap(bos[i]);
             vbuf->ptrs[i] = NULL;
          }
     }
}

static E_Comp_Wl_Video_Buf*
_e_comp_wl_video_buffer_create_res(struct wl_resource *resource)
{
   E_Comp_Wl_Video_Buf *vbuf = NULL;
   struct wl_shm_buffer *shm_buffer;
   tbm_surface_h tbm_surface;

   EINA_SAFETY_ON_NULL_RETURN_VAL(resource, NULL);

   vbuf = calloc(1, sizeof(E_Comp_Wl_Video_Buf));
   EINA_SAFETY_ON_FALSE_GOTO(vbuf != NULL, create_fail);

   vbuf->ref_cnt = 1;
   vbuf->stamp = e_comp_wl_video_buffer_get_mills();
   while (_find_vbuf(vbuf->stamp))
     vbuf->stamp++;

   vbuf->resource = resource;

   if ((shm_buffer = wl_shm_buffer_get(resource)))
     {
        uint32_t tbmfmt = wl_shm_buffer_get_format(shm_buffer);

        vbuf->type = E_COMP_WL_VIDEO_BUF_TYPE_SHM;

        if (tbmfmt == WL_SHM_FORMAT_ARGB8888)
          vbuf->tbmfmt = TBM_FORMAT_ARGB8888;
        else if (tbmfmt == WL_SHM_FORMAT_XRGB8888)
          vbuf->tbmfmt = TBM_FORMAT_XRGB8888;
        else
          vbuf->tbmfmt = tbmfmt;

        vbuf->width = wl_shm_buffer_get_width(shm_buffer);
        vbuf->height = wl_shm_buffer_get_height(shm_buffer);
        vbuf->pitches[0] = wl_shm_buffer_get_stride(shm_buffer);

        vbuf->width_from_pitch = vbuf->width;
        vbuf->height_from_size = vbuf->height;;
     }
   else if ((tbm_surface = wayland_tbm_server_get_surface(e_comp->wl_comp_data->tbm.server, resource)))
     {
        int i;

        vbuf->type = E_COMP_WL_VIDEO_BUF_TYPE_TBM;
        vbuf->tbm_surface = tbm_surface;
        tbm_surface_internal_ref(tbm_surface);

        vbuf->tbmfmt = tbm_surface_get_format(tbm_surface);
        vbuf->width = tbm_surface_get_width(tbm_surface);
        vbuf->height = tbm_surface_get_height(tbm_surface);

        /* NOTE Some format such a NVxx and a YUVxxx can have multiple handles */
        for (i = 0; i < 3; i++)
          {
             uint32_t size = 0, offset = 0, pitch = 0;
             tbm_bo bo;

             bo = tbm_surface_internal_get_bo(tbm_surface, i);
             if (bo)
               {
                  vbuf->handles[i] = tbm_bo_get_handle(bo, TBM_DEVICE_DEFAULT).u32;
                  EINA_SAFETY_ON_FALSE_GOTO(vbuf->handles[i] > 0, create_fail);

                  vbuf->names[i] = tbm_bo_export(bo);
                  EINA_SAFETY_ON_FALSE_GOTO(vbuf->names[i] > 0, create_fail);
               }

             tbm_surface_internal_get_plane_data(tbm_surface, i, &size, &offset, &pitch);
             vbuf->pitches[i] = pitch;
             vbuf->offsets[i] = offset;
          }

        tdm_helper_get_buffer_full_size(tbm_surface, &vbuf->width_from_pitch, &vbuf->height_from_size);
     }
   else
     {
        ERR("unknown buffer resource");
        goto create_fail;
     }

   vbuf_lists = eina_list_append(vbuf_lists, vbuf);

   BDB("type(%d) %dx%d(%dx%d), %c%c%c%c, name(%d,%d,%d) hnd(%d,%d,%d), pitch(%d,%d,%d), offset(%d,%d,%d)",
       vbuf->type, vbuf->width_from_pitch, vbuf->height_from_size,
       vbuf->width, vbuf->height, FOURCC_STR(vbuf->tbmfmt),
       vbuf->names[0], vbuf->names[1], vbuf->names[2],
       vbuf->handles[0], vbuf->handles[1], vbuf->handles[2],
       vbuf->pitches[0], vbuf->pitches[1], vbuf->pitches[2],
       vbuf->offsets[0], vbuf->offsets[1], vbuf->offsets[2]);

   return vbuf;

create_fail:
   _e_comp_wl_video_buffer_free(vbuf);

   return NULL;
}

EINTERN E_Comp_Wl_Video_Buf*
e_comp_wl_video_buffer_create(struct wl_resource *resource)
{
   E_Comp_Wl_Video_Buf *vbuf = _e_comp_wl_video_buffer_create_res(resource);
   EINA_SAFETY_ON_NULL_RETURN_VAL(vbuf, NULL);

   vbuf->destroy_listener.notify = _e_comp_wl_video_buffer_cb_destroy;
   wl_resource_add_destroy_listener(resource, &vbuf->destroy_listener);

   return vbuf;
}

EINTERN E_Comp_Wl_Video_Buf*
e_comp_wl_video_buffer_create_comp(E_Comp_Wl_Buffer *comp_buffer)
{
   E_Comp_Wl_Video_Buf *vbuf;

   EINA_SAFETY_ON_NULL_RETURN_VAL(comp_buffer, NULL);

   vbuf = _e_comp_wl_video_buffer_create_res(comp_buffer->resource);
   EINA_SAFETY_ON_NULL_RETURN_VAL(vbuf, NULL);

   vbuf->comp_buffer = comp_buffer;

   vbuf->destroy_listener.notify = _e_comp_wl_video_buffer_cb_destroy;
   wl_resource_add_destroy_listener(comp_buffer->resource, &vbuf->destroy_listener);

   return vbuf;
}

EINTERN E_Comp_Wl_Video_Buf*
e_comp_wl_video_buffer_create_tbm(tbm_surface_h tbm_surface)
{
   E_Comp_Wl_Video_Buf *vbuf;
   int i;

   EINA_SAFETY_ON_NULL_RETURN_VAL(tbm_surface, NULL);

   vbuf = calloc(1, sizeof(E_Comp_Wl_Video_Buf));
   EINA_SAFETY_ON_FALSE_GOTO(vbuf != NULL, create_fail);

   vbuf->ref_cnt = 1;
   vbuf->stamp = e_comp_wl_video_buffer_get_mills();
   while (_find_vbuf(vbuf->stamp))
     vbuf->stamp++;

   vbuf->type = E_COMP_WL_VIDEO_BUF_TYPE_TBM;
   vbuf->tbm_surface = tbm_surface;
   tbm_surface_internal_ref(tbm_surface);

   vbuf->tbmfmt = tbm_surface_get_format(tbm_surface);
   vbuf->width = tbm_surface_get_width(tbm_surface);
   vbuf->height = tbm_surface_get_height(tbm_surface);

   for (i = 0; i < 3; i++)
     {
        uint32_t size = 0, offset = 0, pitch = 0;
        tbm_bo bo;

        bo = tbm_surface_internal_get_bo(tbm_surface, i);
        if (bo)
          {
             vbuf->handles[i] = tbm_bo_get_handle(bo, TBM_DEVICE_DEFAULT).u32;
             EINA_SAFETY_ON_FALSE_GOTO(vbuf->handles[i] > 0, create_fail);

             vbuf->names[i] = tbm_bo_export(bo);
             EINA_SAFETY_ON_FALSE_GOTO(vbuf->names[i] > 0, create_fail);
          }

        tbm_surface_internal_get_plane_data(tbm_surface, i, &size, &offset, &pitch);
        vbuf->pitches[i] = pitch;
        vbuf->offsets[i] = offset;
     }

   tdm_helper_get_buffer_full_size(tbm_surface, &vbuf->width_from_pitch, &vbuf->height_from_size);

   vbuf_lists = eina_list_append(vbuf_lists, vbuf);

   BDB("type(%d) %dx%d(%dx%d), %c%c%c%c, name(%d,%d,%d) hnd(%d,%d,%d), pitch(%d,%d,%d), offset(%d,%d,%d)",
       vbuf->type, vbuf->width_from_pitch, vbuf->height_from_size,
       vbuf->width, vbuf->height, FOURCC_STR(vbuf->tbmfmt),
       vbuf->names[0], vbuf->names[1], vbuf->names[2],
       vbuf->handles[0], vbuf->handles[1], vbuf->handles[2],
       vbuf->pitches[0], vbuf->pitches[1], vbuf->pitches[2],
       vbuf->offsets[0], vbuf->offsets[1], vbuf->offsets[2]);

   return vbuf;

create_fail:
   _e_comp_wl_video_buffer_free(vbuf);

   return NULL;
}

EINTERN E_Comp_Wl_Video_Buf*
e_comp_wl_video_buffer_alloc(int width, int height, tbm_format tbmfmt, Eina_Bool scanout)
{
   E_Comp_Wl_Video_Buf *vbuf = NULL;
   tbm_surface_h tbm_surface = NULL;
   int i;

   EINA_SAFETY_ON_FALSE_RETURN_VAL(width > 0, NULL);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(height > 0, NULL);

   vbuf = calloc(1, sizeof(E_Comp_Wl_Video_Buf));
   EINA_SAFETY_ON_FALSE_GOTO(vbuf != NULL, alloc_fail);

   vbuf->ref_cnt = 1;
   vbuf->stamp = e_comp_wl_video_buffer_get_mills();
   while (_find_vbuf(vbuf->stamp))
     vbuf->stamp++;

   if (scanout)
     tbm_surface = tbm_surface_internal_create_with_flags(width, height, tbmfmt, TBM_BO_SCANOUT);
   else
     tbm_surface = tbm_surface_internal_create_with_flags(width, height, tbmfmt, TBM_BO_DEFAULT);
   EINA_SAFETY_ON_NULL_GOTO(tbm_surface, alloc_fail);

   vbuf->type = E_COMP_WL_VIDEO_BUF_TYPE_TBM;
   vbuf->tbm_surface = tbm_surface;
   tbm_surface_internal_ref(tbm_surface);

   vbuf->tbmfmt = tbmfmt;
   vbuf->width = width;
   vbuf->height = height;

   for (i = 0; i < 3; i++)
     {
        uint32_t size = 0, offset = 0, pitch = 0;
        tbm_bo bo;

        bo = tbm_surface_internal_get_bo(tbm_surface, i);
        if (bo)
          {
             vbuf->handles[i] = tbm_bo_get_handle(bo, TBM_DEVICE_DEFAULT).u32;
             EINA_SAFETY_ON_FALSE_GOTO(vbuf->handles[i] > 0, alloc_fail);

             vbuf->names[i] = tbm_bo_export(bo);
             EINA_SAFETY_ON_FALSE_GOTO(vbuf->names[i] > 0, alloc_fail);
          }

        tbm_surface_internal_get_plane_data(tbm_surface, i, &size, &offset, &pitch);
        vbuf->pitches[i] = pitch;
        vbuf->offsets[i] = offset;
     }

   tdm_helper_get_buffer_full_size(tbm_surface, &vbuf->width_from_pitch, &vbuf->height_from_size);

   tbm_surface_internal_unref(tbm_surface);

   vbuf_lists = eina_list_append(vbuf_lists, vbuf);

   BDB("type(%d) %dx%d(%dx%d) %c%c%c%c nm(%d,%d,%d) hnd(%d,%d,%d) pitch(%d,%d,%d) offset(%d,%d,%d)",
       vbuf->type, vbuf->width_from_pitch, vbuf->height_from_size,
       vbuf->width, vbuf->height, FOURCC_STR(vbuf->tbmfmt),
       vbuf->names[0], vbuf->names[1], vbuf->names[2],
       vbuf->handles[0], vbuf->handles[1], vbuf->handles[2],
       vbuf->pitches[0], vbuf->pitches[1], vbuf->pitches[2],
       vbuf->offsets[0], vbuf->offsets[1], vbuf->offsets[2]);

   return vbuf;

alloc_fail:
   if (tbm_surface)
     tbm_surface_internal_unref(tbm_surface);
   _e_comp_wl_video_buffer_free(vbuf);
   return NULL;
}

EINTERN E_Comp_Wl_Video_Buf*
e_comp_wl_video_buffer_ref(E_Comp_Wl_Video_Buf *vbuf)
{
   if (!vbuf)
     return NULL;

   EINA_SAFETY_ON_FALSE_RETURN_VAL(VBUF_IS_VALID(vbuf), NULL);

   vbuf->ref_cnt++;
   BDB("count(%d) ref", vbuf->ref_cnt);

   return vbuf;
}

EINTERN void
e_comp_wl_video_buffer_unref(E_Comp_Wl_Video_Buf *vbuf)
{
   if (!vbuf)
     return;

   VBUF_RETURN_IF_FAIL(e_comp_wl_video_buffer_valid(vbuf));

   vbuf->ref_cnt--;
   BDB("count(%d) unref", vbuf->ref_cnt);

   if (!vbuf->buffer_destroying && vbuf->ref_cnt == 0)
     _e_comp_wl_video_buffer_free(vbuf);
}

static void
_e_comp_wl_video_buffer_free(E_Comp_Wl_Video_Buf *vbuf)
{
   VBufFreeFuncInfo *info;
   Eina_List *l, *ll;

   if (!vbuf)
     return;

   VBUF_RETURN_IF_FAIL(e_comp_wl_video_buffer_valid(vbuf));

   BDB("vbuf(%p) tbm_surface(%p) freed", vbuf, vbuf->tbm_surface);

   vbuf->buffer_destroying = EINA_TRUE;

   if (vbuf->destroy_listener.notify)
     {
        wl_list_remove(&vbuf->destroy_listener.link);
        vbuf->destroy_listener.notify = NULL;
     }

   EINA_LIST_FOREACH_SAFE(vbuf->free_funcs, l, ll, info)
     {
        /* call before tmb_bo_unref */
        vbuf->free_funcs = eina_list_remove_list(vbuf->free_funcs, l);
        if (info->func)
          info->func(vbuf, info->data);
        free(info);
     }

#if 0
   /* DO not check ref_count here. Even if ref_count is not 0, vbuf can be
    * be destroyed by wl_buffer_destroy forcely. video or screenmirror should add
    * the vbuf free function and handle the destroying vbuf situation.
    */
   if (!vbuf->buffer_destroying)
     VBUF_RETURN_IF_FAIL(vbuf->ref_cnt == 0);
#endif

   /* make sure all operation is done */
   VBUF_RETURN_IF_FAIL(vbuf->in_use == EINA_FALSE);

   if (vbuf->type == E_COMP_WL_VIDEO_BUF_TYPE_TBM && vbuf->tbm_surface)
     {
        tbm_surface_internal_unref(vbuf->tbm_surface);
        vbuf->tbm_surface = NULL;
     }

   vbuf_lists = eina_list_remove(vbuf_lists, vbuf);

   vbuf->stamp = 0;

   free(vbuf);
}

static void
_e_comp_wl_video_buffer_cb_destroy(struct wl_listener *listener, void *data)
{
   E_Comp_Wl_Video_Buf *vbuf = container_of(listener, E_Comp_Wl_Video_Buf, destroy_listener);

   if (!vbuf) return;

   vbuf->comp_buffer = NULL;

   if (vbuf->buffer_destroying == EINA_FALSE)
     {
        vbuf->destroy_listener.notify = NULL;
        _e_comp_wl_video_buffer_free(vbuf);
     }
}

EINTERN void
e_comp_wl_video_buffer_clear(E_Comp_Wl_Video_Buf *vbuf)
{
   EINA_SAFETY_ON_NULL_RETURN(vbuf);

   if (!_e_comp_wl_video_buffer_access_data_begin(vbuf))
     {
        BER("can't access ptr");
        return;
     }

   switch(vbuf->tbmfmt)
     {
      case TBM_FORMAT_ARGB8888:
      case TBM_FORMAT_XRGB8888:
         memset(vbuf->ptrs[0], 0, vbuf->pitches[0] * vbuf->height);
         break;
      case TBM_FORMAT_YVU420:
      case TBM_FORMAT_YUV420:
         memset((char*)vbuf->ptrs[0] + vbuf->offsets[0], 0x10, vbuf->pitches[0] * vbuf->height);
         memset((char*)vbuf->ptrs[1] + vbuf->offsets[1], 0x80, vbuf->pitches[1] * (vbuf->height >> 1));
         memset((char*)vbuf->ptrs[2] + vbuf->offsets[2], 0x80, vbuf->pitches[2] * (vbuf->height >> 1));
         break;
      case TBM_FORMAT_NV12:
      case TBM_FORMAT_NV21:
         memset((char*)vbuf->ptrs[0] + vbuf->offsets[0], 0x10, vbuf->pitches[0] * vbuf->height);
         memset((char*)vbuf->ptrs[1] + vbuf->offsets[1], 0x80, vbuf->pitches[1] * (vbuf->height >> 1));
         break;
      case TBM_FORMAT_YUYV:
           {
              int *ibuf = (int*)vbuf->ptrs[0];
              int i, size = vbuf->pitches[0] * vbuf->height / 4;

              for (i = 0 ; i < size ; i++)
                ibuf[i] = 0x10801080;
           }
         break;
      case TBM_FORMAT_UYVY:
           {
              int *ibuf = (int*)vbuf->ptrs[0];
              int i, size = vbuf->pitches[0] * vbuf->height / 4;

              for (i = 0 ; i < size ; i++)
                ibuf[i] = 0x80108010; /* YUYV -> 0xVYUY */
           }
         break;
      default:
         BWR("can't clear %c%c%c%c buffer", FOURCC_STR(vbuf->tbmfmt));
         break;
     }

   _e_comp_wl_video_buffer_access_data_end(vbuf);
}

EINTERN Eina_Bool
e_comp_wl_video_buffer_valid(E_Comp_Wl_Video_Buf *vbuf)
{
   E_Comp_Wl_Video_Buf *temp;
   Eina_List *l;

   VBUF_RETURN_VAL_IF_FAIL(vbuf != NULL, EINA_FALSE);
   VBUF_RETURN_VAL_IF_FAIL(vbuf->stamp != 0, EINA_FALSE);

   EINA_LIST_FOREACH(vbuf_lists, l, temp)
     {
        if (temp->stamp == vbuf->stamp)
          return EINA_TRUE;
     }

   BDB("vbuf(%p) invalid", vbuf);

   return EINA_FALSE;
}

static VBufFreeFuncInfo*
_e_comp_wl_video_buffer_free_func_find(E_Comp_Wl_Video_Buf *vbuf, VBuf_Free_Func func, void *data)
{
   VBufFreeFuncInfo *info;
   Eina_List *l;

   EINA_LIST_FOREACH(vbuf->free_funcs, l, info)
     {
        if (info->func == func && info->data == data)
          return info;
     }

   return NULL;
}

EINTERN void
e_comp_wl_video_buffer_free_func_add(E_Comp_Wl_Video_Buf *vbuf, VBuf_Free_Func func, void *data)
{
   VBufFreeFuncInfo *info;

   EINA_SAFETY_ON_FALSE_RETURN(VBUF_IS_VALID(vbuf));
   EINA_SAFETY_ON_NULL_RETURN(func);

   info = _e_comp_wl_video_buffer_free_func_find(vbuf, func, data);
   if (info)
     return;

   info = calloc(1, sizeof(VBufFreeFuncInfo));
   EINA_SAFETY_ON_NULL_RETURN(info);

   info->func = func;
   info->data = data;

   vbuf->free_funcs = eina_list_append(vbuf->free_funcs, info);
}

EINTERN void
e_comp_wl_video_buffer_free_func_del(E_Comp_Wl_Video_Buf *vbuf, VBuf_Free_Func func, void *data)
{
   VBufFreeFuncInfo *info;

   EINA_SAFETY_ON_FALSE_RETURN(VBUF_IS_VALID(vbuf));
   EINA_SAFETY_ON_NULL_RETURN(func);

   info = _e_comp_wl_video_buffer_free_func_find(vbuf, func, data);
   if (!info)
     return;

   vbuf->free_funcs = eina_list_remove(vbuf->free_funcs, info);

   free(info);
}

static pixman_format_code_t
_e_comp_wl_video_buffer_pixman_format_get(E_Comp_Wl_Video_Buf *vbuf)
{
   switch(vbuf->tbmfmt)
     {
      case TBM_FORMAT_ARGB8888: return PIXMAN_a8r8g8b8;
      case TBM_FORMAT_XRGB8888: return PIXMAN_x8r8g8b8;
      default:                  return 0;
     }
   return 0;
}

EINTERN void
e_comp_wl_video_buffer_convert(E_Comp_Wl_Video_Buf *srcbuf, E_Comp_Wl_Video_Buf *dstbuf,
                               int sx, int sy, int sw, int sh,
                               int dx, int dy, int dw, int dh,
                               Eina_Bool over, int rotate, int hflip, int vflip)
{
   pixman_image_t *src_img = NULL, *dst_img = NULL;
   pixman_format_code_t src_format, dst_format;
   double scale_x, scale_y;
   int rotate_step;
   pixman_transform_t t;
   struct pixman_f_transform ft;
   pixman_op_t op;
   int src_stride, dst_stride;
   int buf_width;

   EINA_SAFETY_ON_FALSE_RETURN(VBUF_IS_VALID(srcbuf));
   EINA_SAFETY_ON_FALSE_RETURN(VBUF_IS_VALID(dstbuf));

   if (!_e_comp_wl_video_buffer_access_data_begin(srcbuf))
     return;
   if (!_e_comp_wl_video_buffer_access_data_begin(dstbuf))
     {
        _e_comp_wl_video_buffer_access_data_end(srcbuf);
        return;
     }

   /* not handle buffers which have 2 more gem handles */
   EINA_SAFETY_ON_NULL_GOTO(srcbuf->ptrs[0], cant_convert);
   EINA_SAFETY_ON_NULL_GOTO(dstbuf->ptrs[0], cant_convert);
   EINA_SAFETY_ON_FALSE_RETURN(!srcbuf->ptrs[1]);
   EINA_SAFETY_ON_FALSE_RETURN(!dstbuf->ptrs[1]);

   src_format = _e_comp_wl_video_buffer_pixman_format_get(srcbuf);
   EINA_SAFETY_ON_FALSE_GOTO(src_format > 0, cant_convert);
   dst_format = _e_comp_wl_video_buffer_pixman_format_get(dstbuf);
   EINA_SAFETY_ON_FALSE_GOTO(dst_format > 0, cant_convert);

   buf_width = IS_RGB(srcbuf->tbmfmt)?(srcbuf->pitches[0]/4):srcbuf->pitches[0];
   src_stride = IS_RGB(srcbuf->tbmfmt)?(srcbuf->pitches[0]):buf_width * (PIXMAN_FORMAT_BPP(src_format) / 8);
   src_img = pixman_image_create_bits(src_format, buf_width, srcbuf->height,
                                      (uint32_t*)srcbuf->ptrs[0], src_stride);
   EINA_SAFETY_ON_NULL_GOTO(src_img, cant_convert);

   buf_width = IS_RGB(dstbuf->tbmfmt)?(dstbuf->pitches[0]/4):dstbuf->pitches[0];
   dst_stride = IS_RGB(srcbuf->tbmfmt)?(dstbuf->pitches[0]):buf_width * (PIXMAN_FORMAT_BPP(dst_format) / 8);
   dst_img = pixman_image_create_bits(dst_format, buf_width, dstbuf->height,
                                      (uint32_t*)dstbuf->ptrs[0], dst_stride);
   EINA_SAFETY_ON_NULL_GOTO(dst_img, cant_convert);

   pixman_f_transform_init_identity(&ft);

   if (hflip)
     {
        pixman_f_transform_scale(&ft, NULL, -1, 1);
        pixman_f_transform_translate(&ft, NULL, dw, 0);
     }

   if (vflip)
     {
        pixman_f_transform_scale(&ft, NULL, 1, -1);
        pixman_f_transform_translate(&ft, NULL, 0, dh);
     }

   rotate_step = (rotate + 360) / 90 % 4;
   if (rotate_step > 0)
     {
        int c, s, tx = 0, ty = 0;
        switch (rotate_step)
          {
           case 1:
              c = 0, s = -1, tx = -dw;
              break;
           case 2:
              c = -1, s = 0, tx = -dw, ty = -dh;
              break;
           case 3:
              c = 0, s = 1, ty = -dh;
              break;
          }
        pixman_f_transform_translate(&ft, NULL, tx, ty);
        pixman_f_transform_rotate(&ft, NULL, c, s);
     }

   if (rotate_step % 2 == 0)
     {
        scale_x = (double)sw / dw;
        scale_y = (double)sh / dh;
     }
   else
     {
        scale_x = (double)sw / dh;
        scale_y = (double)sh / dw;
     }

   pixman_f_transform_scale(&ft, NULL, scale_x, scale_y);
   pixman_f_transform_translate(&ft, NULL, sx, sy);
   pixman_transform_from_pixman_f_transform(&t, &ft);
   pixman_image_set_transform(src_img, &t);

   if (!over) op = PIXMAN_OP_SRC;
   else op = PIXMAN_OP_OVER;

   pixman_image_composite(op, src_img, NULL, dst_img, 0, 0, 0, 0,
                          dx, dy, dw, dh);
cant_convert:
   if (src_img) pixman_image_unref(src_img);
   if (dst_img) pixman_image_unref(dst_img);

   _e_comp_wl_video_buffer_access_data_end(srcbuf);
   _e_comp_wl_video_buffer_access_data_end(dstbuf);
}

EINTERN Eina_Bool
e_comp_wl_video_buffer_copy(E_Comp_Wl_Video_Buf *srcbuf, E_Comp_Wl_Video_Buf *dstbuf)
{
   int i, j, c_height;
   unsigned char *s, *d;

   EINA_SAFETY_ON_FALSE_RETURN_VAL(VBUF_IS_VALID(srcbuf), EINA_FALSE);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(VBUF_IS_VALID(dstbuf), EINA_FALSE);

   if (!_e_comp_wl_video_buffer_access_data_begin(srcbuf))
     return EINA_FALSE;
   if (!_e_comp_wl_video_buffer_access_data_begin(dstbuf))
     {
        _e_comp_wl_video_buffer_access_data_end(srcbuf);
        return EINA_FALSE;
     }

   switch (srcbuf->tbmfmt)
     {
      case TBM_FORMAT_ARGB8888:
      case TBM_FORMAT_XRGB8888:
      case TBM_FORMAT_YUV422:
      case TBM_FORMAT_YVU422:
         s = (unsigned char*)srcbuf->ptrs[0];
         d = (unsigned char*)dstbuf->ptrs[0];
         for (i = 0; i < srcbuf->height; i++)
           {
              memcpy(d, s, srcbuf->pitches[0]);
              s += srcbuf->pitches[0];
              d += dstbuf->pitches[0];
           }
         break;
      case TBM_FORMAT_YUV420:
      case TBM_FORMAT_YVU420:
         for (i = 0; i < 3; i++)
           {
              s = (unsigned char*)srcbuf->ptrs[i] + srcbuf->offsets[i];
              d = (unsigned char*)dstbuf->ptrs[i] + dstbuf->offsets[i];
              c_height = (i == 0) ? srcbuf->height : srcbuf->height / 2;
              for (j = 0; j < c_height; j++)
                {
                   memcpy(d, s, srcbuf->pitches[i]);
                   s += srcbuf->pitches[i];
                   d += dstbuf->pitches[i];
                }
           }
         break;
      case TBM_FORMAT_NV12:
      case TBM_FORMAT_NV21:
         for (i = 0; i < 2; i++)
           {
              s = (unsigned char*)srcbuf->ptrs[i] + srcbuf->offsets[i];
              d = (unsigned char*)dstbuf->ptrs[i] + dstbuf->offsets[i];
              c_height = (i == 0) ? srcbuf->height : srcbuf->height / 2;
              for (j = 0; j < c_height; j++)
                {
                   memcpy(d, s, srcbuf->pitches[i]);
                   s += srcbuf->pitches[i];
                   d += dstbuf->pitches[i];
                }
           }
         break;
      default:
         ERR("not implemented for %c%c%c%c", FOURCC_STR(srcbuf->tbmfmt));
         _e_comp_wl_video_buffer_access_data_end(srcbuf);
         _e_comp_wl_video_buffer_access_data_end(dstbuf);

         return EINA_FALSE;
     }

   _e_comp_wl_video_buffer_access_data_end(srcbuf);
   _e_comp_wl_video_buffer_access_data_end(dstbuf);

   return EINA_TRUE;
}

EINTERN uint
e_comp_wl_video_buffer_get_mills(void)
{
   struct timespec tp;

   clock_gettime(CLOCK_MONOTONIC, &tp);

   return (tp.tv_sec * 1000) + (tp.tv_nsec / 1000000L);
}

EINTERN int
e_comp_wl_video_buffer_list_length(void)
{
   return eina_list_count(vbuf_lists);
}

E_API void
e_comp_wl_video_buffer_list_print(const char *log_path)
{
   E_Comp_Wl_Video_Buf *vbuf;
   Eina_List *l;
   FILE *log_fl;

   log_fl = fopen(log_path, "a");
   if (!log_fl)
     {
        ERR("failed: open file(%s)", log_path);
        return;
     }

   setvbuf(log_fl, NULL, _IOLBF, 512);

   fprintf(log_fl, "* Video Buffers:\n");
   fprintf(log_fl, "stamp\tsize\tformat\thandles\tpitches\toffsets\tin_use\n");
   EINA_LIST_FOREACH(vbuf_lists, l, vbuf)
     {
        fprintf(log_fl, "%d\t%dx%d\t%c%c%c%c\t%d,%d,%d\t%d,%d,%d\t%d,%d,%d\t%d\n",
                vbuf->stamp, vbuf->width, vbuf->height, FOURCC_STR(vbuf->tbmfmt),
                vbuf->handles[0], vbuf->handles[1], vbuf->handles[2],
                vbuf->pitches[0], vbuf->pitches[1], vbuf->pitches[2],
                vbuf->offsets[0], vbuf->offsets[1], vbuf->offsets[2],
                vbuf->in_use);
     }

   fclose(log_fl);
}

EINTERN void
e_comp_wl_video_buffer_size_get(E_Client *ec, int *bw, int *bh)
{
   E_Comp_Wl_Buffer *buffer = e_pixmap_resource_get(ec->pixmap);

   if (bw) *bw = 0;
   if (bh) *bh = 0;

   if (!buffer)
     {
        INF("no buffer. using ec(%p) size(%dx%d)", ec, ec->w, ec->h);
        if (bw) *bw = ec->w;
        if (bh) *bh = ec->h;
        return;
     }

   if (buffer->type == E_COMP_WL_BUFFER_TYPE_VIDEO)
     {
        tbm_surface_h tbm_surface = wayland_tbm_server_get_surface(NULL, buffer->resource);

        if (bw) *bw = tbm_surface_get_width(tbm_surface);
        if (bh) *bh = tbm_surface_get_height(tbm_surface);
     }
   else
     {
        if (bw) *bw = buffer->w;
        if (bh) *bh = buffer->h;
     }
}

EINTERN void
e_comp_wl_video_buffer_transform_scale_size_get(E_Client *ec, int *bw, int *bh)
{
   E_Comp_Wl_Buffer *buffer = e_pixmap_resource_get(ec->pixmap);
   E_Comp_Wl_Buffer_Viewport *vp = &ec->comp_data->scaler.buffer_viewport;
   int w, h, transform;

   if (bw) *bw = 0;
   if (bh) *bh = 0;

   if (!buffer)
     {
        INF("no buffer. using ec(%p) size(%dx%d)", ec, ec->w, ec->h);
        if (bw) *bw = ec->w;
        if (bh) *bh = ec->h;
        return;
     }

   if (buffer->type == E_COMP_WL_BUFFER_TYPE_VIDEO)
     {
        tbm_surface_h tbm_surface = wayland_tbm_server_get_surface(NULL, buffer->resource);

        w = tbm_surface_get_width(tbm_surface);
        h = tbm_surface_get_height(tbm_surface);
     }
   else
     {
        w = buffer->w;
        h = buffer->h;
     }

   transform = e_comp_wl_output_buffer_transform_get(ec);

   switch (transform)
     {
      case WL_OUTPUT_TRANSFORM_90:
      case WL_OUTPUT_TRANSFORM_270:
      case WL_OUTPUT_TRANSFORM_FLIPPED_90:
      case WL_OUTPUT_TRANSFORM_FLIPPED_270:
         if (bw) *bw = h / vp->buffer.scale;
         if (bh) *bh = w / vp->buffer.scale;
         break;
      default:
         if (bw) *bw = w / vp->buffer.scale;
         if (bh) *bh = h / vp->buffer.scale;
         break;
     }
}
