#ifndef _E_CLIENT_VIDEO_H_
#define _E_CLIENT_VIDEO_H_

#include <tdm.h>
#include <tdm_helper.h>
#include <tbm_surface.h>
#include <values.h>

typedef struct _E_Client_Video_Info E_Client_Video_Info;

struct _E_Client_Video_Info
{
   tdm_info_config src_config;
   tdm_pos dst_pos;
   tdm_transform transform;
};

EINTERN Eina_Bool    e_client_video_set(E_Client *ec);
EINTERN void         e_client_video_unset(E_Client *ec);

EINTERN Eina_Bool    e_client_video_topmost_visibility_follow(E_Client *ec);
EINTERN Eina_Bool    e_client_video_topmost_visibility_unfollow(E_Client *ec);
EINTERN Eina_Bool    e_client_video_property_allow(E_Client *ec);
EINTERN Eina_Bool    e_client_video_property_disallow(E_Client *ec);

EINTERN Eina_Bool    e_client_video_available_properties_get(E_Client *ec, const tdm_prop **props, int *count);
EINTERN Eina_Bool    e_client_video_property_get(E_Client *ec, unsigned int id, tdm_value *value);
EINTERN Eina_Bool    e_client_video_property_set(E_Client *ec, unsigned int id, tdm_value value);
EINTERN Eina_Bool    e_client_video_property_delay_set(E_Client *ec, unsigned int id, tdm_value value);

EINTERN Eina_Bool    e_client_video_info_get(E_Client *ec, E_Client_Video_Info *info);
EINTERN Eina_Bool    e_client_video_commit_data_release(E_Client *ec, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec);

EINTERN tbm_surface_h  e_client_video_tbm_surface_get(E_Client *ec);

#endif