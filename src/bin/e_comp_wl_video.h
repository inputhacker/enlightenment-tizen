#ifdef E_TYPEDEFS

#else
#ifndef E_COMP_WL_VIDEO_H
#define E_COMP_WL_VIDEO_H

#define E_COMP_WL

typedef struct _E_Video E_Video;

EINTERN int e_comp_wl_video_init(void);
EINTERN void e_comp_wl_video_shutdown(void);

EINTERN tdm_layer* e_comp_wl_video_layer_get(tdm_output *output);

#define C(b,m)              (((b) >> (m)) & 0xFF)
#define FOURCC_STR(id)      C(id,0), C(id,8), C(id,16), C(id,24)
#define IS_RGB(f)           ((f) == TBM_FORMAT_XRGB8888 || (f) == TBM_FORMAT_ARGB8888)
#define ROUNDUP(s,c)        (((s) + (c-1)) & ~(c-1))

#endif
#endif