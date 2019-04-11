#ifndef _E_VIDEO_HWC_H_
#define _E_VIDEO_HWC_H_

#include "e_video_internal.h"

#define BUFFER_MAX_COUNT   5

#ifndef CLEAR
#define CLEAR(x) memset(&(x), 0, sizeof(x))
#endif

EINTERN E_Video_Comp_Iface  *e_video_hwc_planes_iface_create(E_Client *ec);
EINTERN E_Video_Comp_Iface  *e_video_hwc_windows_iface_create(E_Client *ec);

#endif
