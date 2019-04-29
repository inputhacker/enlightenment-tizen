#include "e_video_internal.h"

#define EO_DATA_KEY  "E_Client_Video"

#define INTERNAL_DATA_GET                             \
   E_Client_Video *ecv;                               \
   ecv = evas_object_data_get(ec->frame, EO_DATA_KEY)

#define API_ENTRY                                     \
   INTERNAL_DATA_GET;                                 \
   EINA_SAFETY_ON_NULL_RETURN(ecv)

#define IFACE_CHECK_RET(iname, ret)                   \
   INTERNAL_DATA_GET;                                 \
   EINA_SAFETY_ON_NULL_RETURN_VAL(ecv, ret);          \
   VIN(#iname, ec);                                   \
   if (!ecv->iface->iname)                            \
      return ret

struct _E_Client_Video
{
   /* Composite interface ( hwc planes / hwc windows / fallback ) */
   E_Video_Comp_Iface *iface;

   E_Client *ec;

   Eina_List *event_handlers;

   struct
   {
      E_Client_Video_Info_Get_Cb info_get;
      E_Client_Video_Commit_Data_Release_Cb commit_data_release;
      E_Client_Video_Tbm_Surface_Get_Cb tbm_surface_get;
   } cb;

   Eina_Bool redirect;
};

static void
_e_client_video_comp_iface_deinit(E_Client_Video *ecv)
{
   ecv->iface->destroy(ecv->iface);
   ecv->iface = NULL;
}

static Eina_Bool
_e_client_video_comp_iface_init(E_Client_Video *ecv, E_Client *ec)
{
   E_Video_Comp_Iface *iface = NULL;
   E_Hwc_Policy hwc_pol;

   if ((e_config->eom_enable == EINA_TRUE) && (e_eom_is_ec_external(ec)))
     {
        INF("Try to intialize external interface");
        iface = e_video_external_iface_create(ecv);
        goto end;
     }

   if (e_video_debug_display_primary_plane_value_get())
     {
        INF("Select SW Compositing mode according to configuration");
        goto end;
     }

   hwc_pol = e_zone_video_hwc_policy_get(ec->zone);
   if (hwc_pol != E_HWC_POLICY_NONE)
     {
        INF("Initialize the interface of the client_video for HWC mode");
        iface = e_video_hwc_iface_create(ecv);
     }

end:
   if (!iface)
     {
        iface = e_video_fallback_iface_create(ecv);
        if (!iface)
          {
             ERR("Failed to create 'E_Video_Comp_Iface'");
             return EINA_FALSE;
          }
     }

   ecv->iface = iface;

   return EINA_TRUE;
}

static void
_e_client_video_deinit(E_Client_Video *ecv)
{
   _e_client_video_comp_iface_deinit(ecv);

   E_FREE_LIST(ecv->event_handlers, ecore_event_handler_del);
}

static void
_e_client_video_del(E_Client_Video *ecv)
{
   _e_client_video_deinit(ecv);

   evas_object_data_del(ecv->ec->frame, EO_DATA_KEY);
   e_object_unref(E_OBJECT(ecv->ec));

   free(ecv);
}

static Eina_Bool
_e_client_video_cb_ec_zone_set(void *data, int type EINA_UNUSED, void *event)
{
   E_Client_Video *ecv;
   E_Event_Client_Zone_Set *ev;

   ecv = data;
   ev = event;

   if (ecv->ec != ev->ec)
     goto end;

   /* TODO
    * if compositing policy of given E_Zone is changed, de-initialize previous
    * compositing interface and initialize new compositing interface. */

end:
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_client_video_cb_ec_remove(void *data, int type EINA_UNUSED, void *event)
{
   E_Client_Video *ecv;
   E_Event_Client *ev;

   ev = event;
   ecv = data;
   if (ev->ec != ecv->ec)
     goto end;

   _e_client_video_del(ecv);

end:
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_client_video_init(E_Client_Video *ecv, E_Client *ec)
{
   Eina_Bool res;

   ecv->ec = ec;

   res = _e_client_video_comp_iface_init(ecv, ec);
   if (!res)
     {
        ERR("Failed to initialize the composition interface for video");
        return EINA_FALSE;
     }

   E_LIST_HANDLER_APPEND(ecv->event_handlers, E_EVENT_CLIENT_ZONE_SET,
                         _e_client_video_cb_ec_zone_set, ecv);
   E_LIST_HANDLER_APPEND(ecv->event_handlers, E_EVENT_CLIENT_REMOVE,
                         _e_client_video_cb_ec_remove, ecv);

   return EINA_TRUE;
}

E_API Eina_Bool
e_client_video_set(E_Client *ec)
{
   E_Client_Video *ecv;
   Eina_Bool res;

   VIN("Set video client", ec);

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec->frame, EINA_FALSE);

   if (e_object_is_del(E_OBJECT(ec)))
     {
        ERR("Can't handle deleted client (%p)", ec);
        return EINA_FALSE;
     }

   ecv = evas_object_data_get(ec->frame, EO_DATA_KEY);
   if (ecv)
     {
        ERR("Given client (%p) was already set as Video client", ec);
        return EINA_FALSE;
     }

   ecv = E_NEW(E_Client_Video, 1);
   if (!ecv)
     {
        ERR("Failed to allocate memory");
        return EINA_FALSE;
     }

   res = _e_client_video_init(ecv, ec);
   if (!res)
     {
        ERR("Failed to initialize video setting");
        free(ecv);
        return EINA_FALSE;
     }

   evas_object_data_set(ec->frame, EO_DATA_KEY, ecv);
   e_object_ref(E_OBJECT(ec));

   return EINA_TRUE;
}

E_API void
e_client_video_unset(E_Client *ec)
{
   INTERNAL_DATA_GET;

   if (!ecv)
     {
        VWR("It's not video client or already deleted(%d)",
            ec, e_object_is_del(E_OBJECT(ec)));
        return;
     }

   VIN("Unset video client", ec);

   _e_client_video_del(ecv);
}

E_API Eina_Bool
e_client_video_topmost_visibility_follow(E_Client *ec)
{
   IFACE_CHECK_RET(follow_topmost_visibility, EINA_FALSE);

   return ecv->iface->follow_topmost_visibility(ecv->iface);
}

E_API Eina_Bool
e_client_video_topmost_visibility_unfollow(E_Client *ec)
{
   IFACE_CHECK_RET(unfollow_topmost_visibility, EINA_FALSE);

   return ecv->iface->unfollow_topmost_visibility(ecv->iface);
}

EINTERN Eina_Bool
e_client_video_property_allow(E_Client *ec)
{
   IFACE_CHECK_RET(allowed_property, EINA_FALSE);

   return ecv->iface->allowed_property(ecv->iface);
}

EINTERN Eina_Bool
e_client_video_property_disallow(E_Client *ec)
{
   IFACE_CHECK_RET(disallowed_property, EINA_FALSE);

   return ecv->iface->disallowed_property(ecv->iface);
}

E_API Eina_Bool
e_client_video_property_get(E_Client *ec, unsigned int id, tdm_value *value)
{
   IFACE_CHECK_RET(property_get, EINA_FALSE);

   return ecv->iface->property_get(ecv->iface, id, value);
}

E_API Eina_Bool
e_client_video_property_set(E_Client *ec, unsigned int id, tdm_value value)
{
   IFACE_CHECK_RET(property_set, EINA_FALSE);

   return ecv->iface->property_set(ecv->iface, id, value);
}

EINTERN Eina_Bool
e_client_video_property_delay_set(E_Client *ec, unsigned int id, tdm_value value)
{
   IFACE_CHECK_RET(property_delay_set, EINA_FALSE);

   return ecv->iface->property_delay_set(ecv->iface, id, value);
}

E_API Eina_Bool
e_client_video_available_properties_get(E_Client *ec, const tdm_prop **props, int *count)
{
   IFACE_CHECK_RET(available_properties_get, EINA_FALSE);

   return ecv->iface->available_properties_get(ecv->iface, props, count);
}

EINTERN Eina_Bool
e_client_video_info_get(E_Client *ec, E_Client_Video_Info *info)
{
   INTERNAL_DATA_GET;

   if (!ecv)
     return EINA_FALSE;

   if (ecv->cb.info_get)
     return ecv->cb.info_get(ec, info);
   else if (ecv->iface->info_get)
     return ecv->iface->info_get(ecv->iface, info);

   return EINA_FALSE;
}

EINTERN Eina_Bool
e_client_video_commit_data_release(E_Client *ec, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec)
{
   INTERNAL_DATA_GET;

   if (!ecv)
     return EINA_FALSE;

   if (ecv->cb.commit_data_release)
     return ecv->cb.commit_data_release(ec, sequence, tv_sec, tv_usec);
   else if (ecv->iface->commit_data_release)
     return ecv->iface->commit_data_release(ecv->iface, sequence, tv_sec, tv_usec);

   return EINA_FALSE;
}

EINTERN tbm_surface_h
e_client_video_tbm_surface_get(E_Client *ec)
{
   INTERNAL_DATA_GET;

   if (!ecv)
     return NULL;

   if (ecv->cb.tbm_surface_get)
     return ecv->cb.tbm_surface_get(ec);
   else if (ecv->iface->tbm_surface_get)
     return ecv->iface->tbm_surface_get(ecv->iface);

   return NULL;
}

EINTERN void
e_client_video_info_get_func_set(E_Client *ec, E_Client_Video_Info_Get_Cb func)
{
   API_ENTRY;

   ecv->cb.info_get = func;
}

EINTERN void
e_client_video_commit_data_release_func_set(E_Client *ec, E_Client_Video_Commit_Data_Release_Cb func)
{
   API_ENTRY;

   ecv->cb.commit_data_release = func;
}

EINTERN void
e_client_video_tbm_surface_get_func_set(E_Client *ec, E_Client_Video_Tbm_Surface_Get_Cb func)
{
   API_ENTRY;

   ecv->cb.tbm_surface_get = func;
}

EINTERN Eina_Bool
e_client_video_comp_redirect_get(E_Client *ec)
{
   INTERNAL_DATA_GET;

   if (!ecv)
     return EINA_FALSE;

   return ecv->redirect;
}

/* Video Internal Functions */
EINTERN E_Client *
e_client_video_ec_get(E_Client_Video *ecv)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(ecv, NULL);
   return ecv->ec;
}

EINTERN void
e_client_video_comp_redirect_set(E_Client_Video *ecv)
{
   EINA_SAFETY_ON_NULL_RETURN(ecv);
   ecv->redirect = EINA_TRUE;
}

EINTERN void
e_client_video_comp_redirect_unset(E_Client_Video *ecv)
{
   EINA_SAFETY_ON_NULL_RETURN(ecv);
   ecv->redirect = EINA_FALSE;
}
