#ifdef E_TYPEDEFS

typedef enum _E_Comp_Wl_Video_Buf_Type
{
   E_COMP_WL_VIDEO_BUF_TYPE_SHM,
   E_COMP_WL_VIDEO_BUF_TYPE_TBM,
} E_Comp_Wl_Video_Buf_Type;

typedef struct _E_Comp_Wl_Video_Buf E_Comp_Wl_Video_Buf;

#else
#ifndef E_COMP_WL_VIDEO_BUFFER_H
#define E_COMP_WL_VIDEO_BUFFER_H

#include "e_comp_wl_tbm.h"

struct _E_Comp_Wl_Video_Buf
{
   /* to manage lifecycle */
   uint ref_cnt;

   /* to check valid */
   uint stamp;

   /* to manage wl_resource */
   struct wl_resource *resource;
   struct wl_listener destroy_listener;

   Eina_Bool buffer_destroying;

   E_Comp_Wl_Video_Buf_Type type;
   tbm_surface_h tbm_surface;

   /* pitch contains the full buffer width.
    * width indicates the content area width.
    */
   tbm_format tbmfmt;
   int width;
   int height;
   uint handles[4];
   uint pitches[4];
   uint offsets[4];
   int names[4];
   void *ptrs[4];

   int width_from_pitch;
   int height_from_size;

   /* to avoid reading & write at same time */
   Eina_Bool in_use;

   Eina_List *free_funcs;

   /* for wl_buffer.release event */
   E_Comp_Wl_Buffer *comp_buffer;
   E_Comp_Wl_Buffer_Ref buffer_ref;
   Eina_Rectangle content_r;        /* content rect */
   unsigned int content_t;          /* content transform */
};

EINTERN E_Comp_Wl_Video_Buf* e_comp_wl_video_buffer_create(struct wl_resource *resource);
EINTERN E_Comp_Wl_Video_Buf* e_comp_wl_video_buffer_create_comp(E_Comp_Wl_Buffer *comp_buffer);
EINTERN E_Comp_Wl_Video_Buf* e_comp_wl_video_buffer_create_tbm(tbm_surface_h tbm_surface);
EINTERN E_Comp_Wl_Video_Buf* e_comp_wl_video_buffer_alloc(int width, int height, tbm_format tbmfmt, Eina_Bool scanout);
EINTERN E_Comp_Wl_Video_Buf* e_comp_wl_video_buffer_ref(E_Comp_Wl_Video_Buf *vbuf);
EINTERN void          e_comp_wl_video_buffer_unref(E_Comp_Wl_Video_Buf *vbuf);
EINTERN Eina_Bool     e_comp_wl_video_buffer_valid(E_Comp_Wl_Video_Buf *vbuf);

#define VBUF_IS_VALID(b)   e_comp_wl_video_buffer_valid(b)
#define MSTAMP(b)          ((b)?(b)->stamp:0)

#define e_comp_wl_video_buffer_set_use(b, v)    \
   do { \
      if (b) b->in_use = v; \
   } while (0)

typedef void (*VBuf_Free_Func) (E_Comp_Wl_Video_Buf *vbuf, void *data);
EINTERN void e_comp_wl_video_buffer_free_func_add(E_Comp_Wl_Video_Buf *vbuf, VBuf_Free_Func func, void *data);
EINTERN void e_comp_wl_video_buffer_free_func_del(E_Comp_Wl_Video_Buf *vbuf, VBuf_Free_Func func, void *data);

EINTERN void e_comp_wl_video_buffer_clear(E_Comp_Wl_Video_Buf *vbuf);
EINTERN Eina_Bool e_comp_wl_video_buffer_copy(E_Comp_Wl_Video_Buf *srcbuf, E_Comp_Wl_Video_Buf *dstbuf);
EINTERN void e_comp_wl_video_buffer_convert(E_Comp_Wl_Video_Buf *srcbuf, E_Comp_Wl_Video_Buf *dstbuf,
                                            int sx, int sy, int sw, int sh,
                                            int dx, int dy, int dw, int dh,
                                            Eina_Bool over, int rotate, int hflip, int vflip);

EINTERN uint e_comp_wl_video_buffer_get_mills(void);
EINTERN int  e_comp_wl_video_buffer_list_length(void);
EINTERN void e_comp_wl_video_buffer_list_print(const char *log_path);

EINTERN void e_comp_wl_video_buffer_size_get(E_Client *ec, int *bw, int *bh);
EINTERN void e_comp_wl_video_buffer_transform_scale_size_get(E_Client *ec, int *bw, int *bh);

#endif
#endif
