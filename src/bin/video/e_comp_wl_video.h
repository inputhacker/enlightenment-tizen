#ifdef E_TYPEDEFS

#else
#ifndef E_COMP_WL_VIDEO_H
#define E_COMP_WL_VIDEO_H

#define E_COMP_WL

EINTERN int e_comp_wl_video_init(void);
EINTERN void e_comp_wl_video_shutdown(void);

EINTERN void          e_comp_wl_video_hwc_window_commit_data_release(E_Hwc_Window *hwc_window, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec);
EINTERN tbm_surface_h e_comp_wl_video_hwc_widow_surface_get(E_Hwc_Window *hwc_window);
EINTERN Eina_Bool     e_comp_wl_video_hwc_window_info_get(E_Hwc_Window *hwc_window, tdm_hwc_window_info *hwc_win_info);

#define C(b,m)              (((b) >> (m)) & 0xFF)
#define FOURCC_STR(id)      C(id,0), C(id,8), C(id,16), C(id,24)
#define IS_RGB(f)           ((f) == TBM_FORMAT_XRGB8888 || (f) == TBM_FORMAT_ARGB8888)
#define ROUNDUP(s,c)        (((s) + (c-1)) & ~(c-1))

#endif
#endif
