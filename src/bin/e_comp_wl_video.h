#ifdef E_TYPEDEFS

#else
#ifndef E_COMP_WL_VIDEO_H
#define E_COMP_WL_VIDEO_H

#define E_COMP_WL
#include "e_comp_wl_video_buffer.h"

typedef struct _E_Video E_Video;

int e_comp_wl_video_init(void);
void e_comp_wl_video_shutdown(void);

Eina_List* e_comp_wl_video_list_get(void);
E_Comp_Wl_Video_Buf* e_comp_wl_video_fb_get(E_Video *video);
void e_comp_wl_video_pos_get(E_Video *video, int *x, int *y);
Ecore_Drm_Output* e_comp_wl_video_drm_output_get(E_Video *video);
tdm_layer* e_comp_wl_video_layer_get(tdm_output *output);



#include <tdm.h>
#include <tbm_bufmgr.h>
#include <tbm_surface.h>
#include <tbm_surface_internal.h>

#define C(b,m)              (((b) >> (m)) & 0xFF)
#define B(c,s)              ((((unsigned int)(c)) & 0xff) << (s))
#define FOURCC(a,b,c,d)     (B(d,24) | B(c,16) | B(b,8) | B(a,0))
#define FOURCC_STR(id)      C(id,0), C(id,8), C(id,16), C(id,24)
#define FOURCC_ID(str)      FOURCC(((char*)str)[0],((char*)str)[1],((char*)str)[2],((char*)str)[3])

#define IS_RGB(f)           ((f) == TBM_FORMAT_XRGB8888 || (f) == TBM_FORMAT_ARGB8888)
#define ROUNDUP(s,c)        (((s) + (c-1)) & ~(c-1))
#define ALIGN_TO_16B(x)     ((((x) + (1 <<  4) - 1) >>  4) <<  4)
#define ALIGN_TO_32B(x)     ((((x) + (1 <<  5) - 1) >>  5) <<  5)
#define ALIGN_TO_128B(x)    ((((x) + (1 <<  7) - 1) >>  7) <<  7)
#define ALIGN_TO_2KB(x)     ((((x) + (1 << 11) - 1) >> 11) << 11)
#define ALIGN_TO_8KB(x)     ((((x) + (1 << 13) - 1) >> 13) << 13)
#define ALIGN_TO_64KB(x)    ((((x) + (1 << 16) - 1) >> 16) << 16)

#endif
#endif
