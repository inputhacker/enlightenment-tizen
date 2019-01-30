#ifdef E_TYPEDEFS
#else
#ifndef E_HWC_WINDOWS_H
#define E_HWC_WINDOWS_H

typedef enum
{
   E_HWC_WINS_DEBUG_CMD_NONE,
   E_HWC_WINS_DEBUG_CMD_VIS,
   E_HWC_WINS_DEBUG_CMD_ALL,
   E_HWC_WINS_DEBUG_CMD_CL,
   E_HWC_WINS_DEBUG_CMD_DV,
   E_HWC_WINS_DEBUG_CMD_CS,
   E_HWC_WINS_DEBUG_CMD_VD,
   E_HWC_WINS_DEBUG_CMD_NO,
   E_HWC_WINS_DEBUG_CMD_QUEUE,
} E_Hwc_Wins_Debug_Cmd;

EINTERN Eina_Bool            e_hwc_windows_init(E_Hwc *hwc);
EINTERN void                 e_hwc_windows_deinit(E_Hwc *hwc);
EINTERN Eina_Bool            e_hwc_windows_render(E_Hwc *hwc);
EINTERN Eina_Bool            e_hwc_windows_commit(E_Hwc *hwc);
EINTERN void                 e_hwc_windows_rendered_window_add(E_Hwc_Window *hwc_window);
EINTERN Eina_Bool            e_hwc_windows_get_available_properties(E_Hwc *hwc, const tdm_prop **props, int *count);
EINTERN Eina_Bool            e_hwc_windows_get_video_available_properties(E_Hwc *hwc, const tdm_prop **props, int *count);

EINTERN Eina_Bool            e_hwc_windows_pp_commit_possible_check(E_Hwc *hwc);
EINTERN Eina_Bool            e_hwc_windows_zoom_set(E_Hwc *hwc, Eina_Rectangle *rect);
EINTERN void                 e_hwc_windows_zoom_unset(E_Hwc *hwc);
EINTERN Eina_Bool            e_hwc_windows_fps_get(E_Hwc *hwc, double *fps);

EINTERN void                 e_hwc_windows_trace_debug(Eina_Bool onoff);
EINTERN void                 e_hwc_windows_dump_start(void);
EINTERN void                 e_hwc_windows_dump_stop(void);
EINTERN void                 e_hwc_windows_debug_info_get(Eldbus_Message_Iter *iter, E_Hwc_Wins_Debug_Cmd cmd);

#endif
#endif
