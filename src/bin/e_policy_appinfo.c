#include "e.h"
#include "e_policy_appinfo.h"

Eina_List *appinfo_list;

struct _E_Policy_Appinfo
{
   pid_t               pid;
   Eina_Stringshare   *appid;
   Eina_Bool base_output_available;
   int base_output_width;
   int base_output_height;
};

EINTERN E_Policy_Appinfo *
e_policy_appinfo_new(void)
{
   E_Policy_Appinfo *epai = NULL;

   epai = E_NEW(E_Policy_Appinfo, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(epai, NULL);

   appinfo_list = eina_list_append(appinfo_list, epai);

   ELOGF("POL_APPINFO", "appinfo(%p) create", NULL, epai);

   return epai;
}

EINTERN void
e_policy_appinfo_del(E_Policy_Appinfo *epai)
{
   EINA_SAFETY_ON_NULL_RETURN(epai);

   ELOGF("POL_APPINFO", "appinfo(%p) delete", NULL, epai);

   if (epai->appid)
     eina_stringshare_del(epai->appid);

   appinfo_list = eina_list_remove(appinfo_list, epai);

   E_FREE(epai);
}

EINTERN Eina_Bool
e_policy_appinfo_pid_set(E_Policy_Appinfo *epai, pid_t pid)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(epai, EINA_FALSE);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(pid <= 0, EINA_FALSE);

   epai->pid = pid;

   ELOGF("POL_APPINFO", "appinfo(%p) set pid(%u)", NULL, epai, pid);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_policy_appinfo_appid_set(E_Policy_Appinfo *epai, const char *appid)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(epai, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(appid, EINA_FALSE);

   if (epai->appid)
     eina_stringshare_del(epai->appid);

   epai->appid = eina_stringshare_add(appid);

   ELOGF("POL_APPINFO", "appinfo(%p) set appid(%s)", NULL, epai, appid);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_policy_appinfo_base_output_resolution_get(E_Policy_Appinfo *epai, int *width, int *height)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(epai, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(width, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(height, EINA_FALSE);

   if (!epai->base_output_available)
     {
        *width = 0;
        *height = 0;
        return EINA_FALSE;
     }

   *width = epai->base_output_width;
   *height = epai->base_output_height;

   return EINA_TRUE;
}

E_API E_Policy_Appinfo *
e_policy_appinfo_find_with_pid(pid_t pid)
{
   E_Policy_Appinfo *epai = NULL;
   Eina_List *l = NULL;

   EINA_SAFETY_ON_TRUE_RETURN_VAL(pid <= 0, EINA_FALSE);

   EINA_LIST_FOREACH(appinfo_list, l, epai)
     {
        if (epai->pid == pid)
          return epai;
     }

   return NULL;
}

E_API Eina_Bool
e_policy_appinfo_base_output_resolution_set(E_Policy_Appinfo *epai, int width, int height)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(epai, EINA_FALSE);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(width < 0, EINA_FALSE);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(height < 0, EINA_FALSE);

   epai->base_output_width = width;
   epai->base_output_height = height;
   epai->base_output_available = EINA_TRUE;

   ELOGF("POL_APPINFO", "appinfo(%p) set base_output_resolution(%d, %d):(pid,%u)", NULL, epai, width, height, epai->pid);

   return EINA_TRUE;
}
