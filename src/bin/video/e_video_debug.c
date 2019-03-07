#include "e_video_internal.h"

static Eina_Bool video_to_primary = EINA_FALSE;
static Eina_Bool video_punch = EINA_FALSE;

E_API Eina_Bool
e_video_debug_display_primary_plane_value_get(void)
{
   return video_to_primary;
}

E_API void
e_video_debug_display_primary_plane_set(Eina_Bool set)
{
   video_to_primary = set;
}

EINTERN Eina_Bool
e_video_debug_punch_value_get(void)
{
   return video_punch;
}

EINTERN void
e_video_debug_punch_set(Eina_Bool set)
{
   video_punch = set;
}
