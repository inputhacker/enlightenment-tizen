#ifndef _E_VIDEO_DEBUG_H_
#define _E_VIDEO_DEBUG_H_

#include <Eina.h>

EINTERN Eina_Bool   e_video_debug_display_primary_plane_value_get(void);
EINTERN void        e_video_debug_display_primary_plane_set(Eina_Bool set);
EINTERN Eina_Bool   e_video_debug_punch_value_get(void);
EINTERN void        e_video_debug_punch_set(Eina_Bool set);

#endif
