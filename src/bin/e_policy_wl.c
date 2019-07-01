#include "e_policy_wl.h"
#include "e.h"
#include "services/e_service_quickpanel.h"
#include "services/e_service_volume.h"
#include "services/e_service_lockscreen.h"
#include "services/e_service_indicator.h"
#include "services/e_service_cbhm.h"
#include "services/e_service_scrsaver.h"
#include "services/e_service_softkey.h"
#include "services/e_service_launcher.h"
#include "e_policy_wl_display.h"
#include "e_policy_conformant_internal.h"
#include "e_policy_visibility.h"
#include "e_policy_appinfo.h"

#include <device/display.h>
#include <wayland-server.h>
#include <tizen-extension-server-protocol.h>
#include <tizen-launch-server-protocol.h>
#include <tzsh_server.h>

#define APP_DEFINE_GROUP_NAME "effect"

typedef enum _Tzsh_Srv_Role
{
   TZSH_SRV_ROLE_UNKNOWN = -1,
   TZSH_SRV_ROLE_CALL,
   TZSH_SRV_ROLE_VOLUME,
   TZSH_SRV_ROLE_QUICKPANEL_SYSTEM_DEFAULT,
   TZSH_SRV_ROLE_QUICKPANEL_CONTEXT_MENU,
   TZSH_SRV_ROLE_LOCKSCREEN,
   TZSH_SRV_ROLE_INDICATOR,
   TZSH_SRV_ROLE_TVSERVICE,
   TZSH_SRV_ROLE_SCREENSAVER_MNG,
   TZSH_SRV_ROLE_SCREENSAVER,
   TZSH_SRV_ROLE_CBHM,
   TZSH_SRV_ROLE_SOFTKEY,
   TZSH_SRV_ROLE_MAGNIFIER,
   TZSH_SRV_ROLE_LAUNCHER,
   TZSH_SRV_ROLE_MAX
} Tzsh_Srv_Role;

typedef enum _Tzsh_Type
{
   TZSH_TYPE_UNKNOWN = 0,
   TZSH_TYPE_SRV,
   TZSH_TYPE_CLIENT
} Tzsh_Type;

typedef enum _Tzlaunch_Effect_Type
{
   TZLAUNCH_EFFECT_TYPE_LAUNCH = 0,
   TZLAUNCH_EFFECT_TYPE_DEPTH_IN
} Tzlaunch_Effect_Type;

typedef struct _E_Policy_Wl_Tzpol
{
   struct wl_resource *res_tzpol; /* tizen_policy_interface */
   Eina_List          *psurfs;    /* list of E_Policy_Wl_Surface */
   Eina_List          *pending_bg;
} E_Policy_Wl_Tzpol;

typedef struct _E_Policy_Wl_Tz_Dpy_Pol
{
   struct wl_resource *res_tz_dpy_pol;
   Eina_List          *dpy_surfs;  // list of E_Policy_Wl_Dpy_Surface
} E_Policy_Wl_Tz_Dpy_Pol;

typedef struct _E_Policy_Wl_Tzsh
{
   struct wl_resource *res_tzsh; /* tizen_ws_shell_interface */
   Tzsh_Type           type;
   E_Pixmap           *cp;
   E_Client           *ec;
} E_Policy_Wl_Tzsh;

typedef struct _E_Policy_Wl_Tzsh_Srv
{
   E_Policy_Wl_Tzsh        *tzsh;
   struct wl_resource *res_tzsh_srv;
   Tzsh_Srv_Role       role;
   const char         *name;
} E_Policy_Wl_Tzsh_Srv;

typedef struct _E_Policy_Wl_Tzsh_Client
{
   E_Policy_Wl_Tzsh        *tzsh;
   struct wl_resource *res_tzsh_client;
   Eina_Bool           qp_client;
   E_Quickpanel_Type   qp_type;
} E_Policy_Wl_Tzsh_Client;

typedef struct _E_Policy_Wl_Tzsh_Region
{
   E_Policy_Wl_Tzsh        *tzsh;
   struct wl_resource *res_tzsh_reg;
   Eina_Tiler         *tiler;
   struct wl_listener  destroy_listener;
} E_Policy_Wl_Tzsh_Region;

typedef struct _E_Policy_Wl_Tzsh_Extension
{
   char                    *name;
   E_Policy_Wl_Tzsh_Ext_Hook_Cb cb;
} E_Policy_Wl_Tzsh_Extension;

typedef struct _E_Policy_Wl_Surface
{
   struct wl_resource *surf;
   E_Policy_Wl_Tzpol       *tzpol;
   E_Pixmap           *cp;
   E_Client           *ec;
   pid_t               pid;
   Eina_Bool           pending_notilv;
   int32_t             notilv;
   Eina_List          *vislist; /* list of tizen_visibility_interface resources */
   Eina_List          *poslist; /* list of tizen_position_inteface resources */
   Eina_Bool           is_background;
} E_Policy_Wl_Surface;

typedef struct _E_Policy_Wl_Dpy_Surface
{
   E_Policy_Wl_Tz_Dpy_Pol  *tz_dpy_pol;
   struct wl_resource *surf;
   E_Client           *ec;
   Eina_Bool           set;
   int32_t             brightness;
} E_Policy_Wl_Dpy_Surface;

typedef struct _E_Policy_Wl_Tzlaunch_Effect
{
   struct wl_resource *res_tzlaunch_effect;  /* tizen_launch_effect */
   Eina_List          *splash_list;            /* list of E_Policy_Wl_Tzlaunch_Splash */
} E_Policy_Wl_Tzlaunch_Effect;

typedef struct _E_Policy_Wl_Tzlaunch_Splash
{
   struct wl_resource        *res_tzlaunch_splash; /* tizen_launch_image */
   E_Policy_Wl_Tzlaunch_Effect *tzlaunch_effect;         /* launcher */

   const char                *path;             /* image resource path */
   uint32_t                   type;             /* 0: image, 1: edc */
   uint32_t                   indicator;        /* 0: off, 1: on */
   uint32_t                   angle;            /* 0, 90, 180, 270 : rotation angle */
   uint32_t                   pid;

   Evas_Object               *obj;              /* launch screen image */
   E_Pixmap                  *ep;               /* pixmap for launch screen client */
   E_Client                  *ec;               /* client for launch screen image */
   Ecore_Timer               *timeout;          /* launch screen image hide timer */
   Evas_Object               *indicator_obj;    /* plug object of indicator */

   Eina_Bool                  valid;            /* validation check */
   Eina_Bool                  replaced;
   E_Comp_Object_Content_Type content_type;     /* type of content */
} E_Policy_Wl_Tzlaunch_Splash;

typedef struct _E_Policy_Wl_Tzlaunch_Effect_Info
{
   uint32_t                   pid;              /* pid */
   int                        effect_type;       /* effect_type */
} E_Policy_Wl_Tzlaunch_Effect_Info;

typedef struct _E_Policy_Wl_Tzlaunch_Appinfo
{
   struct wl_resource        *res_tzlaunch_appinfo; /* tizen_launch_appinfo */
} E_Policy_Wl_Tzlaunch_Appinfo;

typedef enum _Launch_Img_File_type
{
   LAUNCH_IMG_FILE_TYPE_ERROR = -1,
   LAUNCH_IMG_FILE_TYPE_IMAGE = 0,
   LAUNCH_IMG_FILE_TYPE_EDJ
} Launch_Img_File_type;

typedef struct _E_Policy_Wl_Tz_Indicator
{
   struct wl_resource *res_tz_indicator;
   Eina_List          *ec_list;
} E_Policy_Wl_Tz_Indicator;

typedef struct _E_Policy_Wl_Tz_Clipboard
{
   struct wl_resource *res_tz_clipboard;
   Eina_List *ec_list;
} E_Policy_Wl_Tz_Clipboard;

typedef struct _E_Policy_Wl
{
   Eina_List       *globals;                 /* list of wl_global */
   Eina_Hash       *tzpols;                  /* list of E_Policy_Wl_Tzpol */

   Eina_List       *tz_dpy_pols;             /* list of E_Policy_Wl_Tz_Dpy_Pol */
   Eina_List       *pending_vis;             /* list of clients that have pending visibility change*/

   /* tizen_ws_shell_interface */
   Eina_List       *tzshs;                   /* list of E_Policy_Wl_Tzsh */
   Eina_List       *tzsh_srvs;               /* list of E_Policy_Wl_Tzsh_Srv */
   Eina_List       *tzsh_clients;            /* list of E_Policy_Wl_Tzsh_Client */
   E_Policy_Wl_Tzsh_Srv *srvs[TZSH_SRV_ROLE_MAX]; /* list of registered E_Policy_Wl_Tzsh_Srv */
   Eina_List       *tvsrv_bind_list;         /* list of activated E_Policy_Wl_Tzsh_Client */
   Eina_List       *tz_indicators;
   Eina_List       *tz_clipboards;           /* list of E_Policy_Wl_Tz_Clipboard */

   /* tizen_launch_effect_interface */
   Eina_List       *tzlaunch_effect;        /* list of E_Policy_Wl_Tzlaunch_Effect */
   Eina_List       *tzlaunch_effect_info;  /* list of E_Policy_Wl_Tzlaunch_Effect_Info */
   /* tizen_launch_appinfo_interface */
   Eina_List       *tzlaunch_appinfo;       /* list of E_Policy_Wl_Tzlaunch_Appinfo */
   /* tizen_ws_shell_interface ver_2 */
   Eina_List       *tzsh_extensions;           /* list of E_Policy_Wl_Tzsh_Extension */
} E_Policy_Wl;

typedef struct _E_Tzsh_QP_Event
{
   int type;
   int val;
} E_Tzsh_QP_Event;

static E_Policy_Wl *polwl = NULL;

static Eina_List *handlers = NULL;
static Eina_List *hooks_cw = NULL;
static Eina_List *hooks_co = NULL;
static struct wl_resource *_scrsaver_mng_res = NULL; // TODO
static struct wl_resource *_indicator_srv_res = NULL;

static int _e_policy_wl_hooks_delete = 0;
static int _e_policy_wl_hooks_walking = 0;

static Eina_Inlist *_e_policy_wl_hooks[] =
{
   [E_POLICY_WL_HOOK_BASE_OUTPUT_RESOLUTION_GET] = NULL,
};

E_API int E_EVENT_POLICY_INDICATOR_STATE_CHANGE = -1;
E_API int E_EVENT_POLICY_INDICATOR_OPACITY_MODE_CHANGE = -1;
E_API int E_EVENT_POLICY_INDICATOR_VISIBLE_STATE_CHANGE = -1;

enum _E_Policy_Hint_Type
{
   E_POLICY_HINT_USER_GEOMETRY = 0,
   E_POLICY_HINT_FIXED_RESIZE = 1,
   E_POLICY_HINT_DEICONIFY_UPDATE = 2,
   E_POLICY_HINT_ICONIFY = 3,
   E_POLICY_HINT_ABOVE_LOCKSCREEN = 4,
   E_POLICY_HINT_GESTURE_DISABLE = 5,
   E_POLICY_HINT_EFFECT_DISABLE = 6,
   E_POLICY_HINT_MSG_USE = 7,
   E_COMP_HINT_ALWAYS_SELECTIVE = 8,
   E_POLICY_HINT_DEPENDENT_ROTATION = 9,
   E_POLICY_HINT_ROT_RENDER_NOPENDING = 10,
   E_POLICY_HINT_ICONIFY_BUFFER_FLUSH = 11,
};

static const char *hint_names[] =
{
   "wm.policy.win.user.geometry",
   "wm.policy.win.fixed.resize",
   "wm.policy.win.deiconify.update",
   "wm.policy.win.iconify",
   "wm.policy.win.above.lock",
   "wm.policy.win.gesture.disable",
   "wm.policy.win.effect.disable",
   "wm.policy.win.msg.use",
   "wm.comp.win.always.selective.mode",
   "wm.policy.win.rot.dependent",
   "wm.policy.win.rot.render.nopending",
   "wm.policy.win.iconify.buffer.flush",
};

static void                _e_policy_wl_surf_del(E_Policy_Wl_Surface *psurf);
static void                _e_policy_wl_tzsh_srv_register_handle(E_Policy_Wl_Tzsh_Srv *tzsh_srv);
static void                _e_policy_wl_tzsh_srv_unregister_handle(E_Policy_Wl_Tzsh_Srv *tzsh_srv);
static void                _e_policy_wl_tzsh_srv_state_broadcast(E_Policy_Wl_Tzsh_Srv *tzsh_srv, Eina_Bool reg);
static void                _e_policy_wl_tzsh_srv_tvsrv_bind_update(void);
static Eina_Bool           _e_policy_wl_e_client_is_valid(E_Client *ec);
static E_Policy_Wl_Tzsh_Srv    *_e_policy_wl_tzsh_srv_add(E_Policy_Wl_Tzsh *tzsh, Tzsh_Srv_Role role, struct wl_resource *res_tzsh_srv, const char *name);
static void                _e_policy_wl_tzsh_srv_del(E_Policy_Wl_Tzsh_Srv *tzsh_srv);
static E_Policy_Wl_Tzsh_Client *_e_policy_wl_tzsh_client_add(E_Policy_Wl_Tzsh *tzsh, struct wl_resource *res_tzsh_client);
static void                _e_policy_wl_tzsh_client_del(E_Policy_Wl_Tzsh_Client *tzsh_client);
static void                _e_policy_wl_background_state_set(E_Policy_Wl_Surface *psurf, Eina_Bool state);

static void                _e_policy_wl_tzlaunch_effect_type_sync(E_Client *ec);
static int                 _e_policy_wl_tzlaunch_effect_type_get(const char* effect_type);
static void                _e_policy_wl_tzlaunch_effect_type_unset(uint32_t pid);

static void                _launch_effect_hide(uint32_t pid);
static void                _launch_effect_client_del(E_Client *ec);
static void                _launch_splash_off(E_Policy_Wl_Tzlaunch_Splash *tzlaunch_splash);

// --------------------------------------------------------
// E_Policy_Wl_Hook
// --------------------------------------------------------

static void
_e_policy_wl_hooks_clean()
{
   E_Policy_Wl_Hook *epwh = NULL;
   Eina_Inlist *l = NULL;
   unsigned int x;

   for (x = 0; x < E_POLICY_WL_HOOK_LAST; x++)
     {
        EINA_INLIST_FOREACH_SAFE(_e_policy_wl_hooks[x], l, epwh)
          {
             if (!epwh->delete_me) continue;
             _e_policy_wl_hooks[x] = eina_inlist_remove(_e_policy_wl_hooks[x], EINA_INLIST_GET(epwh));
             free(epwh);
          }
     }
}

static void
_e_policy_wl_hook_call(E_Policy_Wl_Hook_Point hookpoint, pid_t pid)
{
   E_Policy_Wl_Hook *epwh = NULL;

   _e_policy_wl_hooks_walking++;
   EINA_INLIST_FOREACH(_e_policy_wl_hooks[hookpoint], epwh)
     {
        if (epwh->delete_me) continue;
        epwh->func(epwh->data, pid);
     }
   _e_policy_wl_hooks_walking--;

   if ((_e_policy_wl_hooks_walking == 0) && (_e_policy_wl_hooks_delete > 0))
     _e_policy_wl_hooks_clean();
}

E_API E_Policy_Wl_Hook *
e_policy_wl_hook_add(E_Policy_Wl_Hook_Point hookpoint, E_Policy_Wl_Hook_Cb func, const void *data)
{
   E_Policy_Wl_Hook *epwh = NULL;

   EINA_SAFETY_ON_TRUE_RETURN_VAL(hookpoint < 0, NULL);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(hookpoint >= E_POLICY_WL_HOOK_LAST, NULL);

   epwh = E_NEW(E_Policy_Wl_Hook, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(epwh, NULL);

   epwh->hookpoint = hookpoint;
   epwh->func = func;
   epwh->data = (void *)data;
   _e_policy_wl_hooks[hookpoint] = eina_inlist_append(_e_policy_wl_hooks[hookpoint], EINA_INLIST_GET(epwh));

   return epwh;
}

E_API void
e_policy_wl_hook_del(E_Policy_Wl_Hook *epwh)
{
   epwh->delete_me = 1;
   if (_e_policy_wl_hooks_walking == 0)
     {
        _e_policy_wl_hooks[epwh->hookpoint] = eina_inlist_remove(_e_policy_wl_hooks[epwh->hookpoint], EINA_INLIST_GET(epwh));
        free(epwh);
     }
   else
     _e_policy_wl_hooks_delete++;
}

// --------------------------------------------------------
// E_Policy_Wl_Tzpol
// --------------------------------------------------------
static E_Policy_Wl_Tzpol *
_e_policy_wl_tzpol_add(struct wl_resource *res_tzpol)
{
   E_Policy_Wl_Tzpol *tzpol;

   tzpol = E_NEW(E_Policy_Wl_Tzpol, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tzpol, NULL);

   eina_hash_add(polwl->tzpols, &res_tzpol, tzpol);

   tzpol->res_tzpol = res_tzpol;

   return tzpol;
}

static void
_e_policy_wl_tzpol_del(void *data)
{
   E_Policy_Wl_Tzpol *tzpol;
   E_Policy_Wl_Surface *psurf;

   tzpol = (E_Policy_Wl_Tzpol *)data;

   EINA_LIST_FREE(tzpol->psurfs, psurf)
     {
        _e_policy_wl_surf_del(psurf);
     }

   tzpol->pending_bg = eina_list_free(tzpol->pending_bg);

   memset(tzpol, 0x0, sizeof(E_Policy_Wl_Tzpol));
   E_FREE(tzpol);
}

static E_Policy_Wl_Tzpol *
_e_policy_wl_tzpol_get(struct wl_resource *res_tzpol)
{
   return (E_Policy_Wl_Tzpol *)eina_hash_find(polwl->tzpols, &res_tzpol);
}

static E_Policy_Wl_Surface *
_e_policy_wl_tzpol_surf_find(E_Policy_Wl_Tzpol *tzpol, E_Client *ec)
{
   Eina_List *l;
   E_Policy_Wl_Surface *psurf;

   EINA_LIST_FOREACH(tzpol->psurfs, l, psurf)
     {
        if (psurf->ec == ec)
          return psurf;
     }

   return NULL;
}

static Eina_List *
_e_policy_wl_tzpol_surf_find_by_pid(E_Policy_Wl_Tzpol *tzpol, pid_t pid)
{
   Eina_List *surfs = NULL, *l;
   E_Policy_Wl_Surface *psurf;

   EINA_LIST_FOREACH(tzpol->psurfs, l, psurf)
     {
        if (psurf->pid == pid)
          {
             surfs = eina_list_append(surfs, psurf);
          }
     }

   return surfs;
}

static Eina_Bool
_e_policy_wl_surf_is_valid(E_Policy_Wl_Surface *psurf)
{
   E_Policy_Wl_Tzpol *tzpol;
   E_Policy_Wl_Surface *psurf2;
   Eina_Iterator *it;
   Eina_List *l;

   it = eina_hash_iterator_data_new(polwl->tzpols);
   EINA_ITERATOR_FOREACH(it, tzpol)
     EINA_LIST_FOREACH(tzpol->psurfs, l, psurf2)
       {
          if (psurf2 == psurf)
            {
               eina_iterator_free(it);
               return EINA_TRUE;
            }
       }
   eina_iterator_free(it);

   return EINA_FALSE;
}

// --------------------------------------------------------
// E_Policy_Wl_Tzsh
// --------------------------------------------------------
static E_Policy_Wl_Tzsh *
_e_policy_wl_tzsh_add(struct wl_resource *res_tzsh)
{
   E_Policy_Wl_Tzsh *tzsh;

   tzsh = E_NEW(E_Policy_Wl_Tzsh, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tzsh, NULL);

   tzsh->res_tzsh = res_tzsh;
   tzsh->type = TZSH_TYPE_UNKNOWN;

   polwl->tzshs = eina_list_append(polwl->tzshs, tzsh);

   return tzsh;
}

static void
_e_policy_wl_tzsh_del(E_Policy_Wl_Tzsh *tzsh)
{
   E_Policy_Wl_Tzsh_Srv *tzsh_srv;
   E_Policy_Wl_Tzsh_Client *tzsh_client;
   Eina_List *l, *ll;

   polwl->tzshs = eina_list_remove(polwl->tzshs, tzsh);

   if (tzsh->type == TZSH_TYPE_SRV)
     {
        EINA_LIST_FOREACH_SAFE(polwl->tzsh_srvs, l, ll, tzsh_srv)
          {
             if (tzsh_srv->tzsh != tzsh) continue;
             _e_policy_wl_tzsh_srv_del(tzsh_srv);
             break;
          }
     }
   else
     {
        EINA_LIST_FOREACH_SAFE(polwl->tzsh_clients, l, ll, tzsh_client)
          {
             if (tzsh_client->tzsh != tzsh) continue;
             _e_policy_wl_tzsh_client_del(tzsh_client);
             break;
          }
     }

   memset(tzsh, 0x0, sizeof(E_Policy_Wl_Tzsh));
   E_FREE(tzsh);
}

static void
_e_policy_wl_tzsh_data_set(E_Policy_Wl_Tzsh *tzsh, Tzsh_Type type, E_Pixmap *cp, E_Client *ec)
{
   tzsh->type = type;
   tzsh->cp = cp;
   tzsh->ec = ec;
}

/* notify current registered services to the client */
static void
_e_policy_wl_tzsh_registered_srv_send(E_Policy_Wl_Tzsh *tzsh)
{
   int i;

   for (i = 0; i < TZSH_SRV_ROLE_MAX; i++)
     {
        if (!polwl->srvs[i]) continue;

        tizen_ws_shell_send_service_register
          (tzsh->res_tzsh, polwl->srvs[i]->name);
     }
}

static E_Policy_Wl_Tzsh *
_e_policy_wl_tzsh_get_from_client(E_Client *ec)
{
   E_Policy_Wl_Tzsh *tzsh = NULL;
   Eina_List *l;

   EINA_LIST_FOREACH(polwl->tzshs, l, tzsh)
     {
        if (tzsh->cp == ec->pixmap)
          {
             if ((tzsh->ec) &&
                 (tzsh->ec != ec))
               {
                  ELOGF("TZSH",
                        "CRI ERR!!|tzsh_cp:%8p|tzsh_ec:%8p|tzsh:%8p",
                        ec,
                        tzsh->cp,
                        tzsh->ec,
                        tzsh);
               }

             return tzsh;
          }
     }

   return NULL;
}

static E_Policy_Wl_Tzsh_Client *
_e_policy_wl_tzsh_client_get_from_tzsh(E_Policy_Wl_Tzsh *tzsh)
{
   E_Policy_Wl_Tzsh_Client *tzsh_client;
   Eina_List *l;

   EINA_LIST_FOREACH(polwl->tvsrv_bind_list, l, tzsh_client)
     {
        if (tzsh_client->tzsh == tzsh)
          return tzsh_client;
     }

   return NULL;
}

static void
_e_policy_wl_tzsh_client_set(E_Client *ec)
{
   E_Policy_Wl_Tzsh *tzsh, *tzsh2;
   E_Policy_Wl_Tzsh_Srv *tzsh_srv;

   tzsh = _e_policy_wl_tzsh_get_from_client(ec);
   if (!tzsh) return;

   tzsh->ec = ec;

   if (tzsh->type == TZSH_TYPE_SRV)
     {
        tzsh_srv = polwl->srvs[TZSH_SRV_ROLE_TVSERVICE];
        if (tzsh_srv)
          {
             tzsh2 = tzsh_srv->tzsh;
             if (tzsh2 == tzsh)
               _e_policy_wl_tzsh_srv_register_handle(tzsh_srv);
          }
     }
   else
     {
        if (_e_policy_wl_tzsh_client_get_from_tzsh(tzsh))
          _e_policy_wl_tzsh_srv_tvsrv_bind_update();
     }
}

static void
_e_policy_wl_tzsh_client_unset(E_Client *ec)
{
   E_Policy_Wl_Tzsh *tzsh, *tzsh2;
   E_Policy_Wl_Tzsh_Srv *tzsh_srv;

   tzsh = _e_policy_wl_tzsh_get_from_client(ec);
   if (!tzsh) return;

   if (tzsh->type == TZSH_TYPE_SRV)
     {
        tzsh_srv = polwl->srvs[TZSH_SRV_ROLE_TVSERVICE];
        if (tzsh_srv)
          {
             tzsh2 = tzsh_srv->tzsh;
             if (tzsh2 == tzsh)
               _e_policy_wl_tzsh_srv_unregister_handle(tzsh_srv);
          }

        tzsh_srv = polwl->srvs[TZSH_SRV_ROLE_SOFTKEY];
        if (tzsh_srv)
          {
             if (tzsh_srv->tzsh == tzsh)
               {
                  e_service_softkey_client_unset(ec);
                  tzsh->ec = NULL;
               }
          }
     }
   else
     {
        if (_e_policy_wl_tzsh_client_get_from_tzsh(tzsh))
          _e_policy_wl_tzsh_srv_tvsrv_bind_update();
     }
}

// --------------------------------------------------------
// E_Policy_Wl_Tzsh_Srv
// --------------------------------------------------------
static E_Policy_Wl_Tzsh_Srv *
_e_policy_wl_tzsh_srv_add(E_Policy_Wl_Tzsh *tzsh, Tzsh_Srv_Role role, struct wl_resource *res_tzsh_srv, const char *name)
{
   E_Policy_Wl_Tzsh_Srv *tzsh_srv;

   tzsh_srv = E_NEW(E_Policy_Wl_Tzsh_Srv, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tzsh_srv, NULL);

   tzsh_srv->tzsh = tzsh;
   tzsh_srv->res_tzsh_srv = res_tzsh_srv;
   tzsh_srv->role = role;
   tzsh_srv->name = eina_stringshare_add(name);

   polwl->srvs[role] = tzsh_srv;
   polwl->tzsh_srvs = eina_list_append(polwl->tzsh_srvs, tzsh_srv);

   _e_policy_wl_tzsh_srv_register_handle(tzsh_srv);
   _e_policy_wl_tzsh_srv_state_broadcast(tzsh_srv, EINA_TRUE);

   return tzsh_srv;
}

static void
_e_policy_wl_tzsh_srv_del(E_Policy_Wl_Tzsh_Srv *tzsh_srv)
{
   polwl->tzsh_srvs = eina_list_remove(polwl->tzsh_srvs, tzsh_srv);

   if (polwl->srvs[tzsh_srv->role] == tzsh_srv)
     polwl->srvs[tzsh_srv->role] = NULL;

   _e_policy_wl_tzsh_srv_state_broadcast(tzsh_srv, EINA_TRUE);
   _e_policy_wl_tzsh_srv_unregister_handle(tzsh_srv);

   if (tzsh_srv->name)
     eina_stringshare_del(tzsh_srv->name);

   if (tzsh_srv->role == TZSH_SRV_ROLE_INDICATOR)
     {
        E_Client *ec;
        ec = tzsh_srv->tzsh->ec;

        if (ec && ec->internal)
          {
             e_pixmap_win_id_del(tzsh_srv->tzsh->cp);
             e_object_del(E_OBJECT(ec));
          }

        _indicator_srv_res = NULL;
     }
   else if (tzsh_srv->role == TZSH_SRV_ROLE_SOFTKEY)
     {
        E_Client *softkey_ec = NULL;

        softkey_ec = tzsh_srv->tzsh->ec;
        if (softkey_ec)
          {
             e_service_softkey_client_unset(softkey_ec);
          }
     }
   else if (tzsh_srv->role == TZSH_SRV_ROLE_MAGNIFIER)
     {
        E_Client *magnifier_ec = NULL;

        magnifier_ec = tzsh_srv->tzsh->ec;
        if (magnifier_ec)
          {
             e_magnifier_owner_unset(magnifier_ec);
             e_magnifier_del();
          }
     }
   else if (tzsh_srv->role == TZSH_SRV_ROLE_LAUNCHER)
     {
        E_Client *launcher_ec = NULL;

        launcher_ec = tzsh_srv->tzsh->ec;
        if (launcher_ec)
          {
             e_service_launcher_client_unset(launcher_ec);
          }
     }

   memset(tzsh_srv, 0x0, sizeof(E_Policy_Wl_Tzsh_Srv));
   E_FREE(tzsh_srv);
}

static int
_e_policy_wl_tzsh_srv_role_get(const char *name)
{
   Tzsh_Srv_Role role = TZSH_SRV_ROLE_UNKNOWN;

   if      (!e_util_strcmp(name, "call"                     )) role = TZSH_SRV_ROLE_CALL;
   else if (!e_util_strcmp(name, "volume"                   )) role = TZSH_SRV_ROLE_VOLUME;
   else if (!e_util_strcmp(name, "quickpanel_system_default")) role = TZSH_SRV_ROLE_QUICKPANEL_SYSTEM_DEFAULT;
   else if (!e_util_strcmp(name, "quickpanel_context_menu"  )) role = TZSH_SRV_ROLE_QUICKPANEL_CONTEXT_MENU;
   else if (!e_util_strcmp(name, "lockscreen"               )) role = TZSH_SRV_ROLE_LOCKSCREEN;
   else if (!e_util_strcmp(name, "indicator"                )) role = TZSH_SRV_ROLE_INDICATOR;
   else if (!e_util_strcmp(name, "tvsrv"                    )) role = TZSH_SRV_ROLE_TVSERVICE;
   else if (!e_util_strcmp(name, "screensaver_manager"      )) role = TZSH_SRV_ROLE_SCREENSAVER_MNG;
   else if (!e_util_strcmp(name, "screensaver"              )) role = TZSH_SRV_ROLE_SCREENSAVER;
   else if (!e_util_strcmp(name, "cbhm"                     )) role = TZSH_SRV_ROLE_CBHM;
   else if (!e_util_strcmp(name, "softkey"                  )) role = TZSH_SRV_ROLE_SOFTKEY;
   else if (!e_util_strcmp(name, "magnifier"                )) role = TZSH_SRV_ROLE_MAGNIFIER;
   else if (!e_util_strcmp(name, "launcher"                 )) role = TZSH_SRV_ROLE_LAUNCHER;

   return role;
}

static E_Client *
_e_policy_wl_tzsh_srv_parent_client_pick(void)
{
   E_Policy_Wl_Tzsh *tzsh = NULL;
   E_Policy_Wl_Tzsh_Client *tzsh_client;
   E_Client *ec = NULL, *ec2;
   Eina_List *l;

   EINA_LIST_REVERSE_FOREACH(polwl->tvsrv_bind_list, l, tzsh_client)
     {
        tzsh = tzsh_client->tzsh;
        if (!tzsh) continue;

        ec2 = tzsh->ec;
        if (!ec2) continue;
        if (!_e_policy_wl_e_client_is_valid(ec2)) continue;

        ec = ec2;
        break;
     }

   return ec;
}

static void
_e_policy_wl_tzsh_srv_tvsrv_bind_update(void)
{
   E_Policy_Wl_Tzsh_Srv *tzsh_srv;
   E_Client *tzsh_client_ec = NULL;
   E_Client *tzsh_srv_ec = NULL;

   tzsh_srv = polwl->srvs[TZSH_SRV_ROLE_TVSERVICE];
   if ((tzsh_srv) && (tzsh_srv->tzsh))
     tzsh_srv_ec = tzsh_srv->tzsh->ec;

   tzsh_client_ec = _e_policy_wl_tzsh_srv_parent_client_pick();

   if ((tzsh_srv_ec) &&
       (tzsh_srv_ec->parent == tzsh_client_ec))
     return;

   if ((tzsh_client_ec) && (tzsh_srv_ec))
     {
        ELOGF("TZSH",
              "TR_SET   |parent_ec:0x%08zx|child_ec:0x%08zx",
              NULL,
              e_client_util_win_get(tzsh_client_ec),
              e_client_util_win_get(tzsh_srv_ec));

        e_policy_stack_transient_for_set(tzsh_srv_ec, tzsh_client_ec);
        evas_object_stack_below(tzsh_srv_ec->frame, tzsh_client_ec->frame);
     }
   else
     {
        if (tzsh_srv_ec)
          {
             ELOGF("TZSH",
                   "TR_UNSET |                    |child_ec:0x%08zx",
                   NULL,
                   e_client_util_win_get(tzsh_srv_ec));

             e_policy_stack_transient_for_set(tzsh_srv_ec, NULL);
          }
     }
}

static void
_e_policy_wl_tzsh_srv_register_handle(E_Policy_Wl_Tzsh_Srv *tzsh_srv)
{
   E_Policy_Wl_Tzsh *tzsh;

   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv);

   tzsh = tzsh_srv->tzsh;
   EINA_SAFETY_ON_NULL_RETURN(tzsh);

   switch (tzsh_srv->role)
     {
      case TZSH_SRV_ROLE_TVSERVICE:
         if (tzsh->ec) tzsh->ec->transient_policy = E_TRANSIENT_BELOW;
         _e_policy_wl_tzsh_srv_tvsrv_bind_update();
         break;

      default:
         break;
     }
}

static void
_e_policy_wl_tzsh_srv_unregister_handle(E_Policy_Wl_Tzsh_Srv *tzsh_srv)
{
   E_Policy_Wl_Tzsh *tzsh;

   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv);

   tzsh = tzsh_srv->tzsh;
   EINA_SAFETY_ON_NULL_RETURN(tzsh);

   switch (tzsh_srv->role)
     {
      case TZSH_SRV_ROLE_TVSERVICE:
         _e_policy_wl_tzsh_srv_tvsrv_bind_update();
         break;

      default:
         break;
     }
}

/* broadcast state of registered service to all subscribers */
static void
_e_policy_wl_tzsh_srv_state_broadcast(E_Policy_Wl_Tzsh_Srv *tzsh_srv, Eina_Bool reg)
{
   E_Policy_Wl_Tzsh *tzsh;
   Eina_List *l;

   EINA_LIST_FOREACH(polwl->tzshs, l, tzsh)
     {
        if (tzsh->type == TZSH_TYPE_SRV) continue;

        if (reg)
          tizen_ws_shell_send_service_register
            (tzsh->res_tzsh, tzsh_srv->name);
        else
          tizen_ws_shell_send_service_unregister
            (tzsh->res_tzsh, tzsh_srv->name);
     }
}

// --------------------------------------------------------
// E_Policy_Wl_Tzsh_Client
// --------------------------------------------------------
static E_Policy_Wl_Tzsh_Client *
_e_policy_wl_tzsh_client_add(E_Policy_Wl_Tzsh *tzsh, struct wl_resource *res_tzsh_client)
{
   E_Policy_Wl_Tzsh_Client *tzsh_client;

   tzsh_client = E_NEW(E_Policy_Wl_Tzsh_Client, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tzsh_client, NULL);

   tzsh_client->tzsh = tzsh;
   tzsh_client->res_tzsh_client = res_tzsh_client;

   /* TODO: add tzsh_client to list or hash */

   polwl->tzsh_clients = eina_list_append(polwl->tzsh_clients, tzsh_client);

   return tzsh_client;
}

static void
_e_policy_wl_tzsh_client_del(E_Policy_Wl_Tzsh_Client *tzsh_client)
{
   if (!tzsh_client) return;

   if (!eina_list_data_find(polwl->tzsh_clients, tzsh_client))
     return;

   polwl->tzsh_clients = eina_list_remove(polwl->tzsh_clients, tzsh_client);
   polwl->tvsrv_bind_list = eina_list_remove(polwl->tvsrv_bind_list, tzsh_client);

   if ((tzsh_client->tzsh) &&
       (tzsh_client->tzsh->ec))
     {
        if (tzsh_client->qp_client)
          e_qp_client_del(tzsh_client->tzsh->ec,
                          tzsh_client->qp_type);
     }

   memset(tzsh_client, 0x0, sizeof(E_Policy_Wl_Tzsh_Client));
   E_FREE(tzsh_client);
}

static E_Policy_Wl_Tzsh_Extension*
_e_policy_wl_tzsh_extension_get(const char *name)
{
   E_Policy_Wl_Tzsh_Extension *tzsh_ext;
   Eina_List *l;

   EINA_LIST_FOREACH(polwl->tzsh_extensions, l, tzsh_ext)
     {
        if (strcmp(tzsh_ext->name, name)) continue;

        return tzsh_ext;
     }

   return NULL;
}


// --------------------------------------------------------
// E_Policy_Wl_Surface
// --------------------------------------------------------
static E_Policy_Wl_Surface *
_e_policy_wl_surf_add(E_Client *ec, struct wl_resource *res_tzpol)
{
   E_Policy_Wl_Surface *psurf = NULL;

   E_Policy_Wl_Tzpol *tzpol;

   tzpol = _e_policy_wl_tzpol_get(res_tzpol);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tzpol, NULL);

   psurf = _e_policy_wl_tzpol_surf_find(tzpol, ec);
   if (psurf) return psurf;

   psurf = E_NEW(E_Policy_Wl_Surface, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(psurf, NULL);

   psurf->tzpol = tzpol;
   psurf->cp = ec->pixmap;
   psurf->ec = ec;
   psurf->pid = ec->netwm.pid;

   if (wl_resource_get_client(ec->comp_data->surface) == wl_resource_get_client(res_tzpol))
       psurf->surf = ec->comp_data->surface;

   tzpol->psurfs = eina_list_append(tzpol->psurfs, psurf);

   return psurf;
}

static void
_e_policy_wl_surf_del(E_Policy_Wl_Surface *psurf)
{
   eina_list_free(psurf->vislist);
   eina_list_free(psurf->poslist);

   memset(psurf, 0x0, sizeof(E_Policy_Wl_Surface));
   E_FREE(psurf);
}

static void
_e_policy_wl_surf_client_set(E_Client *ec)
{
   E_Policy_Wl_Tzpol *tzpol;
   E_Policy_Wl_Surface *psurf;
   Eina_Iterator *it;

   it = eina_hash_iterator_data_new(polwl->tzpols);
   EINA_ITERATOR_FOREACH(it, tzpol)
     {
        psurf = _e_policy_wl_tzpol_surf_find(tzpol, ec);
        if (psurf)
          {
             if ((psurf->ec) && (psurf->ec != ec))
               {
                  ELOGF("POLSURF",
                        "CRI ERR!!|s:%8p|tzpol:%8p|ps:%8p|new_ec:%8p|new_cp:%8p",
                        psurf->ec,
                        psurf->surf,
                        psurf->tzpol,
                        psurf,
                        ec,
                        ec->pixmap);
               }

             psurf->ec = ec;
          }
     }
   eina_iterator_free(it);

   return;
}

static void
_e_policy_wl_pending_bg_client_set(E_Client *ec)
{
   E_Policy_Wl_Tzpol *tzpol;
   E_Policy_Wl_Surface *psurf;
   Eina_Iterator *it;

   if (ec->netwm.pid == 0) return;

   it = eina_hash_iterator_data_new(polwl->tzpols);
   EINA_ITERATOR_FOREACH(it, tzpol)
     {
        Eina_List *psurfs;

        if (!tzpol->pending_bg) continue;

        if ((psurfs = _e_policy_wl_tzpol_surf_find_by_pid(tzpol, ec->netwm.pid)))
          {
             EINA_LIST_FREE(psurfs, psurf)
               {
                  psurf->ec = ec;

                  if (eina_list_data_find(tzpol->pending_bg, psurf))
                    {
                       _e_policy_wl_background_state_set(psurf, EINA_TRUE);
                       tzpol->pending_bg = eina_list_remove(tzpol->pending_bg, psurf);
                    }
               }
          }
     }
   eina_iterator_free(it);
}

static E_Pixmap *
_e_policy_wl_e_pixmap_get_from_id(struct wl_client *client, uint32_t id)
{
   E_Pixmap *cp;
   E_Client *ec;
   struct wl_resource *res_surf;

   res_surf = wl_client_get_object(client, id);
   if (!res_surf)
     {
        ERR("Could not get surface resource");
        return NULL;
     }

   ec = wl_resource_get_user_data(res_surf);
   if (!ec)
     {
        ERR("Could not get surface's user data");
        return NULL;
     }

   /* check E_Pixmap */
   cp = e_pixmap_find(E_PIXMAP_TYPE_WL, (uintptr_t)res_surf);
   if (cp != ec->pixmap)
     {
        ELOGF("POLWL",
              "CRI ERR!!|cp2:%8p|ec2:%8p|res_surf:%8p",
              ec,
              cp,
              e_pixmap_client_get(cp),
              res_surf);
        return NULL;
     }

   return cp;
}

static Eina_Bool
_e_policy_wl_e_client_is_valid(E_Client *ec)
{
   E_Client *ec2;
   Eina_List *l;
   Eina_Bool del = EINA_FALSE;
   Eina_Bool found = EINA_FALSE;

   EINA_LIST_FOREACH(e_comp->clients, l, ec2)
     {
        if (ec2 == ec)
          {
             if (e_object_is_del(E_OBJECT(ec2)))
               del = EINA_TRUE;
             found = EINA_TRUE;
             break;
          }
     }

   return ((!del) && (found));
}

static Eina_List *
_e_policy_wl_e_clients_find_by_pid(pid_t pid)
{
   E_Client *ec;
   Eina_List *clients = NULL, *l;

   EINA_LIST_FOREACH(e_comp->clients, l, ec)
     {
        if (e_object_is_del(E_OBJECT(ec))) continue;
        if (ec->netwm.pid != pid) continue;
        clients = eina_list_append(clients, ec);
     }

   return clients;
}

// --------------------------------------------------------
// visibility
// --------------------------------------------------------
static void
_tzvis_iface_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzvis)
{
   wl_resource_destroy(res_tzvis);
}

static const struct tizen_visibility_interface _tzvis_iface =
{
   _tzvis_iface_cb_destroy
};

static void
_tzvis_iface_cb_vis_destroy(struct wl_resource *res_tzvis)
{
   E_Policy_Wl_Surface *psurf;
   Eina_Bool r;

   psurf = wl_resource_get_user_data(res_tzvis);
   EINA_SAFETY_ON_NULL_RETURN(psurf);

   r = _e_policy_wl_surf_is_valid(psurf);
   if (!r) return;

   psurf->vislist = eina_list_remove(psurf->vislist, res_tzvis);
}

static void
_tzpol_iface_cb_vis_get(struct wl_client *client, struct wl_resource *res_tzpol, uint32_t id, struct wl_resource *surf)
{
   E_Client *ec;
   E_Policy_Wl_Surface *psurf;
   struct wl_resource *res_tzvis;

   ec = wl_resource_get_user_data(surf);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   psurf = _e_policy_wl_surf_add(ec, res_tzpol);
   EINA_SAFETY_ON_NULL_RETURN(psurf);

   res_tzvis = wl_resource_create(client,
                                  &tizen_visibility_interface,
                                  wl_resource_get_version(res_tzpol),
                                  id);
   if (!res_tzvis)
     {
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(res_tzvis,
                                  &_tzvis_iface,
                                  psurf,
                                  _tzvis_iface_cb_vis_destroy);

   psurf->vislist = eina_list_append(psurf->vislist, res_tzvis);

   if (eina_list_data_find(polwl->pending_vis, ec))
     {
        e_policy_wl_visibility_send(ec, ec->visibility.obscured);
     }
}

void
e_policy_wl_visibility_send(E_Client *ec, int vis)
{
   E_Policy_Wl_Tzpol *tzpol;
   E_Policy_Wl_Surface *psurf;
   struct wl_resource *res_tzvis;
   Eina_List *l, *ll;
   Eina_Iterator *it;
   E_Client *ec2;
   Ecore_Window win;
   Eina_Bool sent = EINA_FALSE;
   int ver = 1;
   int sent_vis = E_VISIBILITY_UNKNOWN;

   EINA_SAFETY_ON_TRUE_RETURN(vis == E_VISIBILITY_UNKNOWN);
   if (ec && (ec->visibility.last_sent_type == vis))
     return;

   win = e_client_util_win_get(ec);

   it = eina_hash_iterator_data_new(polwl->tzpols);
   EINA_ITERATOR_FOREACH(it, tzpol)
     EINA_LIST_FOREACH(tzpol->psurfs, l, psurf)
       {
          ec2 = e_pixmap_client_get(psurf->cp);
          if (ec2 != ec) continue;

          EINA_LIST_FOREACH(psurf->vislist, ll, res_tzvis)
            {
               // below code is workaround for checking visibility by display off or not
               if (ec->zone)
                 {
                    if (ec->zone->display_state == E_ZONE_DISPLAY_STATE_ON)
                      e_policy_aux_message_send(ec, "dpms_wm", "on", NULL);
                    else
                      e_policy_aux_message_send(ec, "dpms_wm", "off", NULL);
                 }

               ver = wl_resource_get_version(res_tzvis);
               sent_vis = vis;

               if (vis == E_VISIBILITY_PRE_UNOBSCURED)
                 {
                    if (ver >= 5)
                      {
                         ec->visibility.last_sent_type = vis;
                         tizen_visibility_send_changed(res_tzvis, vis, 0);
                      }
                    else
                      sent_vis = -2;
                 }
               else
                 {
                    if ((vis >= E_VISIBILITY_UNOBSCURED) && (vis <= E_VISIBILITY_FULLY_OBSCURED))
                      {
                         ec->visibility.last_sent_type = vis;
                         tizen_visibility_send_notify(res_tzvis, vis);
                      }
                    else
                      sent_vis = -3;
                 }

               ELOGF("POL_VIS",
                     "SEND     |win:0x%08zx|res_tzvis:%8p|ver:%d|sent_vis:%d|pid:%d|title:%s, name:%s",
                     ec,
                     win,
                     res_tzvis,
                     ver,
                     sent_vis,
                     ec->netwm.pid, ec->icccm.title, ec->netwm.name);
               sent = EINA_TRUE;
               if (ec->comp_data->mapped)
                 {
                    _launch_effect_hide(ec->netwm.pid);
                 }
            }
       }
   eina_iterator_free(it);

   polwl->pending_vis = eina_list_remove(polwl->pending_vis, ec);
   if (!sent)
     polwl->pending_vis = eina_list_append(polwl->pending_vis, ec);
}

Eina_Bool
e_policy_wl_iconify_state_supported_get(E_Client *ec)
{
   E_Policy_Wl_Tzpol *tzpol;
   E_Policy_Wl_Surface *psurf;
   E_Client *ec2;
   Eina_List *l;
   Eina_Iterator *it;
   Eina_Bool found = EINA_FALSE;

   it = eina_hash_iterator_data_new(polwl->tzpols);
   EINA_ITERATOR_FOREACH(it, tzpol)
      EINA_LIST_FOREACH(tzpol->psurfs, l, psurf)
        {
           ec2 = e_pixmap_client_get(psurf->cp);
           if (ec2 == ec)
             {
                found = EINA_TRUE;
                break;
             }
        }
   eina_iterator_free(it);

   return found;
}

void
e_policy_wl_iconify_state_change_send(E_Client *ec, int iconic)
{
   E_Policy_Wl_Tzpol *tzpol;
   E_Policy_Wl_Surface *psurf;
   E_Client *ec2;
   Eina_List *l;
   Eina_Iterator *it;
   Ecore_Window win;

   if (ec->exp_iconify.skip_iconify) return;
   if (ec->exp_iconify.skip_by_remote) return;

   if (e_config->transient.iconify)
     {
        E_Client *child;
        Eina_List *list = eina_list_clone(ec->transients);

        EINA_LIST_FREE(list, child)
          {
             if ((child->iconic == ec->iconic) &&
                 (child->exp_iconify.by_client == ec->exp_iconify.by_client))
               e_policy_wl_iconify_state_change_send(child, iconic);

          }
     }

   win = e_client_util_win_get(ec);

   it = eina_hash_iterator_data_new(polwl->tzpols);
   EINA_ITERATOR_FOREACH(it, tzpol)
     EINA_LIST_FOREACH(tzpol->psurfs, l, psurf)
       {
          ec2 = e_pixmap_client_get(psurf->cp);
          if (ec2 != ec) continue;
          if (!psurf->surf) continue;

          tizen_policy_send_iconify_state_changed(tzpol->res_tzpol, psurf->surf, iconic, 1);
          ELOGF("ICONIFY",
                "SEND     |win:0x%08zx|iconic:%d |sur:%p",
                ec,
                win,
                iconic, psurf->surf);
          break;
       }
   eina_iterator_free(it);
}

// --------------------------------------------------------
// position
// --------------------------------------------------------
static void
_tzpos_iface_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzpos)
{
   wl_resource_destroy(res_tzpos);
}

static void
_tzpos_iface_cb_set(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzpos, int32_t x, int32_t y)
{
   E_Client *ec;
   E_Policy_Wl_Surface *psurf;

   psurf = wl_resource_get_user_data(res_tzpos);
   EINA_SAFETY_ON_NULL_RETURN(psurf);

   ec = e_pixmap_client_get(psurf->cp);
   EINA_SAFETY_ON_NULL_RETURN(ec);
   EINA_SAFETY_ON_NULL_RETURN(ec->frame);

   if (!ec->lock_client_location)
     {
        if (e_client_pending_geometry_has(ec))
          {
             // if there is geometry pending list, add move job at the end of the list.
             // so client to be applied new position at the same time with the pending requests
             // pending geometries are flushed when 'wl surface commit' and matched serial are delivered.
             e_comp_wl_commit_sync_client_geometry_add(ec, E_GEOMETRY_POS, ec->surface_sync.serial, x, y, 0, 0);
          }
        else
          {
             ec->client.x = ec->desk->geom.x + x;
             ec->client.y = ec->desk->geom.y + y;
             e_client_pos_set(ec, ec->client.x, ec->client.y);
             ec->placed = 1;
             ec->changes.pos = 1;
          }
        ec->changes.tz_position = 1;
        EC_CHANGED(ec);
     }

   e_policy_hook_call(E_POLICY_HOOK_CLIENT_POSITION_SET, ec);
}

static const struct tizen_position_interface _tzpos_iface =
{
   _tzpos_iface_cb_destroy,
   _tzpos_iface_cb_set,
};

static void
_tzpol_iface_cb_pos_destroy(struct wl_resource *res_tzpos)
{
   E_Policy_Wl_Surface *psurf;
   Eina_Bool r;

   psurf = wl_resource_get_user_data(res_tzpos);
   EINA_SAFETY_ON_NULL_RETURN(psurf);

   r = _e_policy_wl_surf_is_valid(psurf);
   if (!r) return;

   psurf->poslist = eina_list_remove(psurf->poslist, res_tzpos);
}

static void
_tzpol_iface_cb_pos_get(struct wl_client *client, struct wl_resource *res_tzpol, uint32_t id, struct wl_resource *surf)
{
   E_Client *ec;
   E_Policy_Wl_Surface *psurf;
   struct wl_resource *res_tzpos;

   ec = wl_resource_get_user_data(surf);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   psurf = _e_policy_wl_surf_add(ec, res_tzpol);
   EINA_SAFETY_ON_NULL_RETURN(psurf);

   res_tzpos = wl_resource_create(client,
                                  &tizen_position_interface,
                                  wl_resource_get_version(res_tzpol),
                                  id);
   if (!res_tzpos)
     {
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(res_tzpos,
                                  &_tzpos_iface,
                                  psurf,
                                  _tzpol_iface_cb_pos_destroy);

   psurf->poslist = eina_list_append(psurf->poslist, res_tzpos);
}

void
e_policy_wl_position_send(E_Client *ec)
{
   E_Policy_Wl_Tzpol *tzpol;
   E_Policy_Wl_Surface *psurf;
   struct wl_resource *res_tzpos;
   Eina_List *l, *ll;
   Eina_Iterator *it;
   Ecore_Window win;

   EINA_SAFETY_ON_NULL_RETURN(ec);

   win = e_client_util_win_get(ec);

   it = eina_hash_iterator_data_new(polwl->tzpols);
   EINA_ITERATOR_FOREACH(it, tzpol)
     EINA_LIST_FOREACH(tzpol->psurfs, l, psurf)
       {
          if (e_pixmap_client_get(psurf->cp) != ec) continue;

          EINA_LIST_FOREACH(psurf->poslist, ll, res_tzpos)
            {
               tizen_position_send_changed(res_tzpos, ec->client.x, ec->client.y);
               ELOGF("TZPOS",
                     "SEND     |win:0x%08zx|res_tzpos:%8p|ec->x:%d, ec->y:%d, ec->client.x:%d, ec->client.y:%d",
                     ec,
                     win,
                     res_tzpos,
                     ec->x, ec->y,
                     ec->client.x, ec->client.y);
            }
       }
   eina_iterator_free(it);
}

// --------------------------------------------------------
// stack: activate, raise, lower
// --------------------------------------------------------

E_API void
e_policy_wl_activate(E_Client *ec)
{
   EINA_SAFETY_ON_NULL_RETURN(ec);
   EINA_SAFETY_ON_NULL_RETURN(ec->frame);

   ELOGF("TZPOL", "REAL ACTIVATE", ec);

   if ((!starting) && (!ec->focused) && (!ec->visibility.force_obscured))
     {
        if (!e_policy_visibility_client_activate(ec))
          {
             if ((ec->iconic) && (!ec->exp_iconify.by_client))
               e_policy_wl_iconify_state_change_send(ec, 0);
             e_client_activate(ec, EINA_TRUE);
          }
     }
   else
     evas_object_raise(ec->frame);

   if (e_policy_client_is_lockscreen(ec))
     {
        int ex, ey, ew, eh;
        e_client_geometry_get(ec, &ex, &ey, &ew, &eh);

        if (E_CONTAINS(ex, ey, ew, eh, ec->zone->x, ec->zone->y, ec->zone->w, ec->zone->h))
          e_policy_stack_clients_restack_above_lockscreen(ec, EINA_TRUE);
     }
   else
     e_policy_stack_check_above_lockscreen(ec, ec->layer, NULL, EINA_TRUE);
}

static void
_tzpol_iface_cb_activate(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzpol EINA_UNUSED, struct wl_resource *surf)
{
   E_Client *ec;

   ec = wl_resource_get_user_data(surf);
   EINA_SAFETY_ON_NULL_RETURN(ec);
   EINA_SAFETY_ON_NULL_RETURN(ec->frame);

   ELOGF("TZPOL", "ACTIVATE", ec);

   e_policy_hook_call(E_POLICY_HOOK_CLIENT_ACTIVE_REQ, ec);

   ec->post_lower = EINA_FALSE;
   if (ec->comp_data && !ec->comp_data->mapped)
     ec->post_raise = EINA_TRUE;
   e_policy_wl_activate(ec);
}

E_API void
e_policy_wl_stack_changed_send(E_Client *ec)
{
   E_Client *above = NULL;
   E_Client *below = NULL;
   int above_pid = -1;
   int below_pid = -1;

   above = e_client_above_get(ec);
   while (above)
     {
        if ((!e_object_is_del(E_OBJECT(above))) &&
            (!e_client_util_ignored_get(above)) &&
            (above->visible) &&
            (above->frame))
          break;

        above = e_client_above_get(above);
     }

   below = e_client_below_get(ec);
   while (below)
     {
        if ((!e_object_is_del(E_OBJECT(below))) &&
            (!e_client_util_ignored_get(below)) &&
            (below->visible) &&
            (below->frame))
          break;

        below = e_client_below_get(below);
     }

   if (above) above_pid = above->netwm.pid;
   if (below) below_pid = below->netwm.pid;

   ELOGF("TZPOL", "Send stack_changed by activate_below. above(win:%zx, pid:%d), below(win:%zx, pid:%d)",
         ec, e_client_util_win_get(above), above_pid, e_client_util_win_get(below), below_pid);


   e_policy_aux_message_send_from_int(ec, "stack_changed", "activate_below", 2, above_pid, below_pid);

}

static void
_tzpol_iface_cb_activate_below_by_res_id(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzpol,  uint32_t res_id, uint32_t below_res_id)
{
   E_Client *ec = NULL;
   E_Client *below_ec = NULL;
   E_Client *parent_ec = NULL;
   E_Client *focus_ec = NULL;
   Eina_Bool check_ancestor = EINA_FALSE;
   Eina_Bool intercepted = EINA_FALSE;

   ec = e_pixmap_find_client_by_res_id(res_id);
   EINA_SAFETY_ON_NULL_RETURN(ec);
   EINA_SAFETY_ON_NULL_RETURN(ec->frame);

   below_ec = e_pixmap_find_client_by_res_id(below_res_id);
   EINA_SAFETY_ON_NULL_RETURN(below_ec);
   EINA_SAFETY_ON_NULL_RETURN(below_ec->frame);

   ELOGF("TZPOL",
         "ACTIVATE_BELOW|win:0x%08zx(res_id:%d)|below_win:0x%08zx(res_id:%d)",
         NULL, e_client_util_win_get(ec), res_id, e_client_util_win_get(below_ec), below_res_id);

   intercepted = e_policy_interceptor_call(E_POLICY_INTERCEPT_ACTIVATE_BELOW,
                                           ec, below_ec);
   if (intercepted)
     {
        ELOGF("TZPOL", "ACTIVATE_BELOW|Handled by Intercept function", ec);
        return;
     }

   if (ec->layer > below_ec->layer) return;

   parent_ec = ec->parent;
   while (parent_ec)
     {
        if (parent_ec == below_ec)
          {
             check_ancestor = EINA_TRUE;
             break;
          }
        parent_ec = parent_ec->parent;
     }
   if (check_ancestor) return;

   if ((!starting) && (!ec->focused))
     {
        if ((ec->iconic) && (!ec->exp_iconify.by_client))
          e_policy_wl_iconify_state_change_send(ec, 0);

        e_client_uniconify(ec);
     }

   e_policy_stack_below(ec, below_ec);

   if ((ec->comp_data) && (!ec->comp_data->mapped))
     {
        ELOGF("TZPOL", "POST_RAISE_LOWER SET... raise:%d, lower:%d", ec, EINA_FALSE, EINA_FALSE);
        e_client_post_raise_lower_set(ec, EINA_FALSE, EINA_FALSE);
     }

   e_policy_wl_stack_changed_send(ec);

   // check focus
   focus_ec = e_client_focused_get();
   if (focus_ec == below_ec)
     e_client_focus_latest_set(ec);
}

static void
_tzpol_iface_cb_activate_above_by_res_id(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzpol,  uint32_t res_id, uint32_t above_res_id)
{
   E_Client *ec = NULL;
   E_Client *above_ec = NULL;
   E_Client *parent_ec = NULL;
   Eina_Bool check_ancestor = EINA_FALSE;
   Eina_Bool intercepted = EINA_FALSE;

   ec = e_pixmap_find_client_by_res_id(res_id);
   EINA_SAFETY_ON_NULL_RETURN(ec);
   EINA_SAFETY_ON_NULL_RETURN(ec->frame);

   above_ec = e_pixmap_find_client_by_res_id(above_res_id);
   EINA_SAFETY_ON_NULL_RETURN(above_ec);
   EINA_SAFETY_ON_NULL_RETURN(above_ec->frame);

   ELOGF("TZPOL",
         "ACTIVATE_ABOVE|win:0x%08zx(res_id:%d)|above_win:0x%08zx(res_id:%d)",
         NULL, e_client_util_win_get(ec), res_id, e_client_util_win_get(above_ec), above_res_id);

   intercepted = e_policy_interceptor_call(E_POLICY_INTERCEPT_ACTIVATE_ABOVE,
                                           ec, above_ec);
   if (intercepted)
     {
        ELOGF("TZPOL", "ACTIVATE_ABOVE|Handled by Intercept function", ec);
        return;
     }

   if (ec->layer < above_ec->layer) return;

   /* check child */
   parent_ec = above_ec->parent;
   while (parent_ec)
     {
        if (parent_ec == ec)
          {
             check_ancestor = EINA_TRUE;
             break;
          }
        parent_ec = parent_ec->parent;
     }
   if (check_ancestor) return;

   if (!starting)
     {
        if ((ec->iconic) && (!ec->exp_iconify.by_client))
          e_policy_wl_iconify_state_change_send(ec, 0);

        e_client_uniconify(ec);
     }

   e_policy_stack_above(ec, above_ec);

   if ((ec->comp_data) && (!ec->comp_data->mapped))
     {
        ELOGF("TZPOL", "POST_RAISE_LOWER SET... raise:%d, lower:%d", ec, EINA_FALSE, EINA_FALSE);
        e_client_post_raise_lower_set(ec, EINA_FALSE, EINA_FALSE);
     }
}

static void
_tzpol_iface_cb_raise(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzpol EINA_UNUSED, struct wl_resource *surf)
{
   E_Client *ec;

   ec = wl_resource_get_user_data(surf);
   EINA_SAFETY_ON_NULL_RETURN(ec);
   EINA_SAFETY_ON_NULL_RETURN(ec->frame);

   ELOGF("TZPOL", "RAISE", ec);

   e_policy_hook_call(E_POLICY_HOOK_CLIENT_RAISE_REQ, ec);

   evas_object_raise(ec->frame);

   if ((ec->comp_data) && (!ec->comp_data->mapped))
     {
        ELOGF("TZPOL", "POST_RAISE_LOWER SET... raise:%d, lower:%d", ec, EINA_TRUE, EINA_FALSE);
        e_client_post_raise_lower_set(ec, EINA_TRUE, EINA_FALSE);
     }
}

static void
_tzpol_iface_cb_lower(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzpol EINA_UNUSED, struct wl_resource *surf)
{
   E_Client *ec = NULL;

   ec = wl_resource_get_user_data(surf);
   EINA_SAFETY_ON_NULL_RETURN(ec);
   EINA_SAFETY_ON_NULL_RETURN(ec->frame);

   ELOGF("TZPOL", "LOWER", ec);

   e_policy_hook_call(E_POLICY_HOOK_CLIENT_LOWER_REQ, ec);

   if (e_policy_visibility_client_lower(ec))
     return;

   evas_object_lower(ec->frame);

   if ((ec->comp_data) && (!ec->comp_data->mapped))
     {
        ELOGF("TZPOL", "POST_RAISE_LOWER SET... raise:%d, lower:%d", ec, EINA_FALSE, EINA_TRUE);
        e_client_post_raise_lower_set(ec, EINA_FALSE, EINA_TRUE);
     }

   if (ec->focused)
     e_client_revert_focus(ec);
}

static void
_tzpol_iface_cb_lower_by_res_id(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzpol,  uint32_t res_id)
{
   E_Client *ec = NULL;

   ec = e_pixmap_find_client_by_res_id(res_id);
   EINA_SAFETY_ON_NULL_RETURN(ec);
   EINA_SAFETY_ON_NULL_RETURN(ec->frame);

   ELOGF("TZPOL", "LOWER by res id:%d", ec, res_id);
   evas_object_lower(ec->frame);

   if ((ec->comp_data) && (!ec->comp_data->mapped))
     {
        ELOGF("TZPOL", "POST_RAISE_LOWER SET... raise:%d, lower:%d", ec, EINA_FALSE, EINA_TRUE);
        e_client_post_raise_lower_set(ec, EINA_FALSE, EINA_TRUE);
     }
}

// --------------------------------------------------------
// focus
// --------------------------------------------------------
static void
_tzpol_iface_cb_focus_skip_set(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzpol EINA_UNUSED, struct wl_resource *surf)
{
   E_Client *ec;

   ec = wl_resource_get_user_data(surf);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   e_client_focus_skip_set(ec, EINA_TRUE, EINA_TRUE);
}

static void
_tzpol_iface_cb_focus_skip_unset(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzpol EINA_UNUSED, struct wl_resource *surf)
{
   E_Client *ec;

   ec = wl_resource_get_user_data(surf);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   e_client_focus_skip_set(ec, EINA_FALSE, EINA_TRUE);
}

// --------------------------------------------------------
// role
// --------------------------------------------------------
static void
_tzpol_iface_cb_role_set(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzpol EINA_UNUSED, struct wl_resource *surf, const char *role)
{
   E_Client *ec;

   EINA_SAFETY_ON_NULL_RETURN(role);

   ec = wl_resource_get_user_data(surf);
   EINA_SAFETY_ON_NULL_RETURN(ec);
   EINA_SAFETY_ON_NULL_RETURN(ec->frame);

   ELOGF("TZPOL", "ROLE SET (role:%s)", ec, role);
   e_client_window_role_set(ec, role);

   /* TODO: support multiple roles */
   if (!e_util_strcmp("tv-volume-popup", role))
     {
        evas_object_layer_set(ec->frame, E_LAYER_CLIENT_NOTIFICATION_LOW);
        ec->lock_client_location = 1;
     }
   else if (!e_util_strcmp("e_demo", role))
     {
        evas_object_layer_set(ec->frame, E_LAYER_CLIENT_NOTIFICATION_HIGH);
        ec->lock_client_location = 1;
     }
   else if (!e_util_strcmp("cbhm", role))
     {
        if (!ec->comp_data) return;
        e_comp_wl->selection.cbhm = ec->comp_data->surface;
     }
   else if (!e_util_strcmp("wl_pointer-cursor", role))
     {
        ELOGF("TZPOL", "Set CURSOR role", ec);
        evas_object_layer_set(ec->frame, E_LAYER_CLIENT_CURSOR);
        ec->layer = E_LAYER_CLIENT_CURSOR;
        ec->is_cursor = EINA_TRUE;
     }
}

static void
_tzpol_iface_cb_type_set(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzpol, struct wl_resource *surf, uint32_t type)
{
   E_Client *ec;
   E_Window_Type win_type;

   ec = wl_resource_get_user_data(surf);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   switch (type)
     {
      /* TODO: support other types */
      case TIZEN_POLICY_WIN_TYPE_TOPLEVEL:
         win_type = E_WINDOW_TYPE_NORMAL;
         if (ec->layer != E_LAYER_CLIENT_NORMAL)
           {
              ec->layer = E_LAYER_CLIENT_NORMAL;
              if (ec->frame)
                {
                   if (ec->layer != evas_object_layer_get(ec->frame))
                     evas_object_layer_set(ec->frame, ec->layer);
                }
           }
         break;

      case TIZEN_POLICY_WIN_TYPE_NOTIFICATION:
         win_type = E_WINDOW_TYPE_NOTIFICATION;
         break;

      case TIZEN_POLICY_WIN_TYPE_UTILITY:
         win_type = E_WINDOW_TYPE_UTILITY;
         break;

      case TIZEN_POLICY_WIN_TYPE_DIALOG:
         win_type = E_WINDOW_TYPE_DIALOG;
         break;

      default: return;
     }

   ELOGF("TZPOL",
         "TYPE_SET |win:0x%08zx|s:%8p|res_tzpol:%8p|tizen_win_type:%d, e_win_type:%d",
         ec,
         e_client_util_win_get(ec),
         surf,
         res_tzpol,
         type, win_type);

   ec->netwm.type = win_type;

   EC_CHANGED(ec);
}
// --------------------------------------------------------
// conformant
// --------------------------------------------------------
static void
_tzpol_iface_cb_conformant_set(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzpol, struct wl_resource *surf)
{
   E_Client *ec;

   ec = wl_resource_get_user_data(surf);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   e_policy_conformant_client_add(ec, res_tzpol);
}

static void
_tzpol_iface_cb_conformant_unset(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzpol EINA_UNUSED, struct wl_resource *surf)
{
   E_Client *ec;

   ec = wl_resource_get_user_data(surf);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   e_policy_conformant_client_del(ec);
}

static void
_tzpol_iface_cb_conformant_get(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzpol, struct wl_resource *surf)
{
   E_Client *ec;

   ec = wl_resource_get_user_data(surf);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   tizen_policy_send_conformant(res_tzpol, surf, e_policy_conformant_client_check(ec));
}

// --------------------------------------------------------
// notification level
// --------------------------------------------------------
static void
_tzpol_notilv_set(E_Client *ec, int lv)
{
   short ly;

   switch (lv)
     {
      case  0: ly = E_LAYER_CLIENT_NOTIFICATION_LOW;    break;
      case  1: ly = E_LAYER_CLIENT_NOTIFICATION_NORMAL; break;
      case  2: ly = E_LAYER_CLIENT_NOTIFICATION_TOP;    break;
      case -1: ly = E_LAYER_CLIENT_NORMAL;              break;
      case 10: ly = E_LAYER_CLIENT_NOTIFICATION_LOW;    break;
      case 20: ly = E_LAYER_CLIENT_NOTIFICATION_NORMAL; break;
      case 30: ly = E_LAYER_CLIENT_NOTIFICATION_HIGH;   break;
      case 40: ly = E_LAYER_CLIENT_NOTIFICATION_TOP;    break;
      default: ly = E_LAYER_CLIENT_NOTIFICATION_LOW;    break;
     }

   if (ly != evas_object_layer_get(ec->frame))
     {
        if (ly == E_LAYER_CLIENT_NORMAL)
          e_policy_animatable_lock(ec, E_POLICY_ANIMATABLE_LAYER, 0);
        else
          e_policy_animatable_lock(ec, E_POLICY_ANIMATABLE_LAYER, 1);

        evas_object_layer_set(ec->frame, ly);
     }

   e_policy_hook_call(E_POLICY_HOOK_CLIENT_NOTILAYER_SET, ec);

   ec->layer = ly;
}

static void
_tzpol_iface_cb_notilv_set(struct wl_client *client, struct wl_resource *res_tzpol, struct wl_resource *surf, int32_t lv)
{
   E_Client *ec;
   E_Policy_Wl_Surface *psurf;
   pid_t pid = 0;
   uid_t uid = 0;
   Eina_Bool res;

   ec = wl_resource_get_user_data(surf);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   psurf = _e_policy_wl_surf_add(ec, res_tzpol);
   EINA_SAFETY_ON_NULL_RETURN(psurf);

   wl_client_get_credentials(client, &pid, &uid, NULL);
   res = e_security_privilege_check(pid, uid,
                                    E_PRIVILEGE_NOTIFICATION_LEVEL_SET);
   if (!res)
     {
        ELOGF("TZPOL",
              "Privilege Check Failed! DENY set_notification_level",
              ec);

        tizen_policy_send_notification_done
           (res_tzpol,
            surf,
            -1,
            TIZEN_POLICY_ERROR_STATE_PERMISSION_DENIED);
        return;
     }

   ELOGF("TZPOL", "NOTI_LEVEL|level:%d", ec, lv);
   _tzpol_notilv_set(ec, lv);

   psurf->notilv = lv;

   tizen_policy_send_notification_done
     (res_tzpol, surf, lv, TIZEN_POLICY_ERROR_STATE_NONE);

   if (e_policy_client_is_lockscreen(ec))
     {
        int ex, ey, ew, eh;
        e_client_geometry_get(ec, &ex, &ey, &ew, &eh);

        if (E_CONTAINS(ex, ey, ew, eh, ec->zone->x, ec->zone->y, ec->zone->w, ec->zone->h))
          e_policy_stack_clients_restack_above_lockscreen(ec, EINA_TRUE);
     }
   else
     e_policy_stack_check_above_lockscreen(ec, ec->layer, NULL, EINA_TRUE);
}

void
e_policy_wl_notification_level_fetch(E_Client *ec)
{
   E_Pixmap *cp;
   E_Policy_Wl_Surface *psurf;
   E_Policy_Wl_Tzpol *tzpol;
   Eina_Iterator *it;
   Eina_List *l;
   Eina_Bool changed_stack = EINA_FALSE;

   EINA_SAFETY_ON_NULL_RETURN(ec);

   cp = ec->pixmap;
   EINA_SAFETY_ON_NULL_RETURN(cp);

   // TODO: use pending_notilv_list instead of loop
   it = eina_hash_iterator_data_new(polwl->tzpols);
   EINA_ITERATOR_FOREACH(it, tzpol)
     EINA_LIST_FOREACH(tzpol->psurfs, l, psurf)
       {
          if (psurf->cp != cp) continue;
          if (!psurf->pending_notilv) continue;

          psurf->pending_notilv = EINA_FALSE;
          _tzpol_notilv_set(ec, psurf->notilv);
          changed_stack = EINA_TRUE;
       }
   eina_iterator_free(it);

   if (changed_stack &&
       e_policy_client_is_lockscreen(ec))
     {
        int ex, ey, ew, eh;
        e_client_geometry_get(ec, &ex, &ey, &ew, &eh);

        if (E_CONTAINS(ex, ey, ew, eh, ec->zone->x, ec->zone->y, ec->zone->w, ec->zone->h))
          e_policy_stack_clients_restack_above_lockscreen(ec, EINA_TRUE);
     }
}

// --------------------------------------------------------
// transient for
// --------------------------------------------------------
static void
_e_policy_wl_parent_surf_set(E_Client *ec, struct wl_resource *parent_surf)
{
   E_Client *pc = NULL;

   if (parent_surf)
     {
        if (!(pc = wl_resource_get_user_data(parent_surf)))
          {
             ERR("Could not get parent res e_client");
             return;
          }
     }

   e_policy_stack_transient_for_set(ec, pc);
}

static void
_tzpol_iface_cb_transient_for_set(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzpol, uint32_t child_id, uint32_t parent_id)
{
   E_Client *ec, *pc;
   struct wl_resource *parent_surf;

   ELOGF("TZPOL",
         "TF_SET   |res_tzpol:%8p|parent:%d|child:%d",
         NULL, res_tzpol, parent_id, child_id);

   ec = e_pixmap_find_client_by_res_id(child_id);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   pc = e_pixmap_find_client_by_res_id(parent_id);
   EINA_SAFETY_ON_NULL_RETURN(pc);
   EINA_SAFETY_ON_NULL_RETURN(pc->comp_data);

   parent_surf = pc->comp_data->surface;

   _e_policy_wl_parent_surf_set(ec, parent_surf);

   ELOGF("TZPOL",
         "         |win:0x%08zx|parent|s:%8p",
         pc,
         e_client_util_win_get(pc),
         parent_surf);

   ELOGF("TZPOL",
         "         |win:0x%08zx|child |s:%8p",
         ec,
         e_client_util_win_get(ec),
         (ec->comp_data ? ec->comp_data->surface : NULL));

   tizen_policy_send_transient_for_done(res_tzpol, child_id);

   EC_CHANGED(ec);
}

static void
_tzpol_iface_cb_transient_for_unset(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzpol, uint32_t child_id)
{
   E_Client *ec;

   ELOGF("TZPOL",
         "TF_UNSET |res_tzpol:%8p|child:%d",
         NULL, res_tzpol, child_id);

   ec = e_pixmap_find_client_by_res_id(child_id);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   _e_policy_wl_parent_surf_set(ec, NULL);

   tizen_policy_send_transient_for_done(res_tzpol, child_id);

   EC_CHANGED(ec);
}

// --------------------------------------------------------
// window screen mode
// --------------------------------------------------------
static void
_tzpol_iface_cb_win_scrmode_set(struct wl_client *client, struct wl_resource *res_tzpol, struct wl_resource *surf, uint32_t mode)
{
   E_Client *ec;
   pid_t pid = 0;
   uid_t uid = 0;
   Eina_Bool res;

   ec = wl_resource_get_user_data(surf);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   wl_client_get_credentials(client, &pid, &uid, NULL);
   res = e_security_privilege_check(pid, uid,
                                    E_PRIVILEGE_SCREEN_MODE_SET);
   if (!res)
     {
        ELOGF("TZPOL",
              "Privilege Check Failed! DENY set_screen_mode",
              ec);

        tizen_policy_send_window_screen_mode_done
           (res_tzpol,
            surf,
            -1,
            TIZEN_POLICY_ERROR_STATE_PERMISSION_DENIED);
        return;
     }

   ELOGF("TZPOL", "SCR_MODE |mode:%d", ec, mode);

   e_policy_display_screen_mode_set(ec, mode);
   e_policy_wl_win_scrmode_apply();

   tizen_policy_send_window_screen_mode_done
     (res_tzpol, surf, mode, TIZEN_POLICY_ERROR_STATE_NONE);
}

void
e_policy_wl_win_scrmode_apply(void)
{
   e_policy_display_screen_mode_apply();
}

// --------------------------------------------------------
// subsurface
// --------------------------------------------------------
static void
_tzpol_iface_cb_subsurf_place_below_parent(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzpol EINA_UNUSED, struct wl_resource *subsurf)
{
   E_Client *ec;
   E_Client *epc;
   E_Comp_Wl_Subsurf_Data *sdata;

   ec = wl_resource_get_user_data(subsurf);
   EINA_SAFETY_ON_NULL_RETURN(ec);
   EINA_SAFETY_ON_NULL_RETURN(ec->comp_data);

   sdata = ec->comp_data->sub.data;
   EINA_SAFETY_ON_NULL_RETURN(sdata);

   epc = sdata->parent;
   EINA_SAFETY_ON_NULL_RETURN(epc);

   /* check if a subsurface has already placed below a parent */
   if (eina_list_data_find(epc->comp_data->sub.below_list, ec)) return;

   ELOGF("TZPOL", "SUBSURF|BELOW_PARENT", ec);
   epc->comp_data->sub.list = eina_list_remove(epc->comp_data->sub.list, ec);
   epc->comp_data->sub.list_pending = eina_list_remove(epc->comp_data->sub.list_pending, ec);
   epc->comp_data->sub.below_list = eina_list_append(epc->comp_data->sub.below_list, ec);
   epc->comp_data->sub.list_changed = EINA_TRUE;
}

static void
_tzpol_iface_cb_subsurf_stand_alone_set(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzpol EINA_UNUSED, struct wl_resource *subsurf)
{
   E_Client *ec;
   E_Comp_Wl_Subsurf_Data *sdata;

   ec = wl_resource_get_user_data(subsurf);
   EINA_SAFETY_ON_NULL_RETURN(ec);
   EINA_SAFETY_ON_NULL_RETURN(ec->comp_data);

   sdata = ec->comp_data->sub.data;
   EINA_SAFETY_ON_NULL_RETURN(sdata);

   ELOGF("TZPOL", "SUBSURF|STAND_ALONE", ec);
   sdata->stand_alone = EINA_TRUE;
}

static void
_tzpol_iface_cb_subsurface_get(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *surface, uint32_t parent_id)
{
   E_Client *ec, *epc;

   ELOGF("TZPOL",
         "SUBSURF   |wl_surface@%d|parent_id:%d",
         NULL, wl_resource_get_id(surface), parent_id);

   ec = wl_resource_get_user_data(surface);
   if (!ec)
     {
        wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "tizen_policy failed: wrong wl_surface@%d resource",
                               wl_resource_get_id(surface));
        return;
     }

   if (e_object_is_del(E_OBJECT(ec))) return;

   /* check if this surface is already a sub-surface */
   if ((ec->comp_data) && (ec->comp_data->sub.data))
     {
        wl_resource_post_error(resource,
                               WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
                               "wl_surface@%d is already a sub-surface",
                               wl_resource_get_id(surface));
        return;
     }

   epc = e_pixmap_find_client_by_res_id(parent_id);

   /* try to create a new subsurface */
   if (!e_comp_wl_subsurface_create(ec, epc, id, surface))
     {
        ERR("Failed to create subsurface for surface@%d", wl_resource_get_id(surface));
        return;
     }

   /* ec's parent comes from another process */
   if (ec->comp_data)
     ec->comp_data->has_extern_parent = EINA_TRUE;
}

static void
_tzpol_iface_cb_opaque_state_set(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface, int32_t state)
{
   E_Client *ec;

   ec = wl_resource_get_user_data(surface);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   ELOGF("TZPOL", "OPAQUE   |opaque_state:%d", ec, state);
   if(ec->visibility.opaque == state)
     return;
   ec->visibility.opaque = state;

   EC_CHANGED(ec);
}

// --------------------------------------------------------
// iconify
// --------------------------------------------------------

E_API void
e_policy_wl_iconify(E_Client *ec)
{
   EINA_SAFETY_ON_NULL_RETURN(ec);
   EINA_SAFETY_ON_NULL_RETURN(ec->frame);

   ELOG("Set ICONIFY BY CLIENT", ec);

   if (e_policy_visibility_client_iconify(ec))
     {
        ec->exp_iconify.by_client = 1;
        return;
     }
   ec->exp_iconify.by_client = 1;

   e_client_iconify(ec);

   EC_CHANGED(ec);
}

E_API void
e_policy_wl_uniconify(E_Client *ec)
{
   EINA_SAFETY_ON_NULL_RETURN(ec);
   EINA_SAFETY_ON_NULL_RETURN(ec->frame);

   if (ec->visibility.force_obscured)
     {
        ec->exp_iconify.by_client = 0;
        return;
     }

   if (e_policy_visibility_client_uniconify(ec, 1))
     return;

   if ((ec->iconic) && (!ec->exp_iconify.by_client))
     e_policy_wl_iconify_state_change_send(ec, 0);

   e_client_uniconify(ec);
   ELOG("Un-Set ICONIFY BY CLIENT", ec);
   ec->exp_iconify.by_client = 0;

   EC_CHANGED(ec);
}

static void
_tzpol_iface_cb_iconify(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzpol EINA_UNUSED, struct wl_resource *surf)
{
   E_Client *ec;

   ec = wl_resource_get_user_data(surf);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   ELOGF("TZPOL", "ICONIFY", ec);

   e_policy_hook_call(E_POLICY_HOOK_CLIENT_ICONIFY_REQ, ec);
   e_policy_wl_iconify(ec);
}

static void
_tzpol_iface_cb_uniconify(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzpol EINA_UNUSED, struct wl_resource *surf)
{
   E_Client *ec;

   ec = wl_resource_get_user_data(surf);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   ELOGF("TZPOL", "UNICONIFY", ec);

   e_policy_hook_call(E_POLICY_HOOK_CLIENT_UNICONIFY_REQ, ec);
   e_policy_wl_uniconify(ec);
}

static void
_e_policy_wl_allowed_aux_hint_send(struct wl_resource *res_tzpol, struct wl_resource *surf, int32_t id)
{
   E_Client *ec;

   ec = wl_resource_get_user_data(surf);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   ELOGF("TZPOL", "SEND     |res_tzpol:%8p|id:%d, hint allowed ", ec, res_tzpol, id);
   tizen_policy_send_allowed_aux_hint(res_tzpol, surf, id);
}

static void
_e_policy_wl_aux_hint_apply(E_Client *ec)
{
   E_Comp_Wl_Aux_Hint *hint;
   Eina_List *l;

   if (!ec->comp_data) return;
   if (!ec->comp_data->aux_hint.changed) return;

   EINA_LIST_FOREACH(ec->comp_data->aux_hint.hints, l, hint)
     {
        if (!hint->changed) continue;
        EC_CHANGED(ec);

        if (!strcmp(hint->hint, hint_names[E_POLICY_HINT_USER_GEOMETRY]))
          {
             if (hint->deleted)
               {
                  e_policy_allow_user_geometry_set(ec, EINA_FALSE);
                  continue;
               }

             if (!strcmp(hint->val, "1"))
               {
                  e_policy_allow_user_geometry_set(ec, EINA_TRUE);
               }
             else if (strcmp(hint->val, "1"))
               {
                  e_policy_allow_user_geometry_set(ec, EINA_FALSE);
               }
          }
        else if (!strcmp(hint->hint, hint_names[E_POLICY_HINT_FIXED_RESIZE]))
          {
             /* TODO: support other aux_hints */
          }
        else if (!strcmp(hint->hint, hint_names[E_POLICY_HINT_DEICONIFY_UPDATE]))
          {
             if (!strcmp(hint->val, "1"))
               ec->exp_iconify.deiconify_update = EINA_TRUE;
             else
               ec->exp_iconify.deiconify_update = EINA_FALSE;

          }
        else if (!strcmp(hint->hint, hint_names[E_POLICY_HINT_GESTURE_DISABLE]))
          {
             if (hint->deleted)
               {
                  ec->gesture_disable = EINA_FALSE;
                  continue;
               }

             if (atoi(hint->val) == 1)
               {
                  ec->gesture_disable = EINA_TRUE;
               }
             else
               {
                  ec->gesture_disable = EINA_FALSE;
               }
          }
        else if (!strcmp(hint->hint, hint_names[E_POLICY_HINT_ICONIFY]))
          {
             if (hint->deleted)
               {
                  ec->exp_iconify.skip_iconify = 0;
                  EC_CHANGED(ec);
                  continue;
               }

             if (!strcmp(hint->val, "disable"))
               {
                  ec->exp_iconify.skip_iconify = 1;
                  EC_CHANGED(ec);
               }
             else if (!strcmp(hint->val, "enable"))
               {
                  ec->exp_iconify.skip_iconify = 0;
                  EC_CHANGED(ec);
               }
          }
        else if (!strcmp(hint->hint, hint_names[E_POLICY_HINT_ABOVE_LOCKSCREEN]))
          {
             if ((hint->deleted) ||
                 (!strcmp(hint->val, "0")))
               {
                  E_Layer original_layer = ec->changable_layer[E_CHANGABLE_LAYER_TYPE_ABOVE_NOTIFICATION].saved_layer;
                  if (ec->changable_layer[E_CHANGABLE_LAYER_TYPE_ABOVE_NOTIFICATION].set &&
                      ec->changable_layer[E_CHANGABLE_LAYER_TYPE_ABOVE_NOTIFICATION].saved)
                    {
                       // restore original layer
                       if (original_layer != evas_object_layer_get(ec->frame))
                         {
                            Eina_Bool pend = EINA_FALSE;
                            pend = e_policy_visibility_client_layer_lower(ec, original_layer);
                            if (!pend)
                              {
                                 evas_object_layer_set(ec->frame, original_layer);
                                 ec->layer = original_layer;
                              }
                         }
                    }
                  ec->changable_layer[E_CHANGABLE_LAYER_TYPE_ABOVE_NOTIFICATION].set = 0;
                  ec->changable_layer[E_CHANGABLE_LAYER_TYPE_ABOVE_NOTIFICATION].saved = 0;
                  ec->changable_layer[E_CHANGABLE_LAYER_TYPE_ABOVE_NOTIFICATION].saved_layer = 0;
                  EC_CHANGED(ec);
               }
             else if (!strcmp(hint->val, "1"))
               {
                  if (!ec->changable_layer[E_CHANGABLE_LAYER_TYPE_ABOVE_NOTIFICATION].saved)
                    {
                       ec->changable_layer[E_CHANGABLE_LAYER_TYPE_ABOVE_NOTIFICATION].set = 1;
                       ec->changable_layer[E_CHANGABLE_LAYER_TYPE_ABOVE_NOTIFICATION].saved = 0;
                       ec->changable_layer[E_CHANGABLE_LAYER_TYPE_ABOVE_NOTIFICATION].saved_layer = ec->layer;
                       EC_CHANGED(ec);
                    }
               }
          }
        else if (!strcmp(hint->hint, hint_names[E_POLICY_HINT_EFFECT_DISABLE]))
          {
             if ((hint->deleted) ||
                 (!strcmp(hint->val, "0")))
               {
                  e_policy_animatable_lock(ec, E_POLICY_ANIMATABLE_HINT, 0);
               }
             else if (!strcmp(hint->val, "1"))
               {
                  e_policy_animatable_lock(ec, E_POLICY_ANIMATABLE_HINT, 1);
               }
          }
        else if (!strcmp(hint->hint, hint_names[E_POLICY_HINT_MSG_USE]))
          {
             if ((hint->deleted) || (!strcmp(hint->val, "0")))
               ec->comp_data->aux_hint.use_msg = EINA_FALSE;
             else if (!strcmp(hint->val, "1"))
               ec->comp_data->aux_hint.use_msg = EINA_TRUE;
          }
        else if (!strcmp(hint->hint, hint_names[E_COMP_HINT_ALWAYS_SELECTIVE]))
          {
             if ((hint->deleted) || (!strcmp(hint->val, "0")))
               ec->comp_data->never_hwc = EINA_FALSE;
             else if (!strcmp(hint->val, "1"))
               ec->comp_data->never_hwc = EINA_TRUE;

             e_comp_render_queue();
          }
        else if (!strcmp(hint->hint, hint_names[E_POLICY_HINT_DEPENDENT_ROTATION]))
          {
             if ((hint->deleted) || (!strcmp(hint->val, "0")))
               ec->e.state.rot.type = E_CLIENT_ROTATION_TYPE_NORMAL;
             else if (!strcmp(hint->val, "1"))
               ec->e.state.rot.type = E_CLIENT_ROTATION_TYPE_DEPENDENT;
          }
        else if (!strcmp(hint->hint, hint_names[E_POLICY_HINT_ROT_RENDER_NOPENDING]))
          {
             if ((hint->deleted) || (!strcmp(hint->val, "0")))
               {
                  ELOGF("ROTATION", "nopending render:0", ec);
                  ec->e.state.rot.nopending_render = 0;
               }
             else if (!strcmp(hint->val, "1"))
               {
                  ELOGF("ROTATION", "nopending render:1", ec);
                  ec->e.state.rot.nopending_render = 1;
               }
          }
        else if (!strncmp(hint->hint, hint_names[E_POLICY_HINT_ICONIFY_BUFFER_FLUSH], strlen(hint->hint)))
          {
             if (!strncmp(hint->val, "1", 1))
               ec->exp_iconify.buffer_flush = EINA_TRUE;
             else
               ec->exp_iconify.buffer_flush = EINA_FALSE;
          }
     }
}

static void
_tzpol_iface_cb_aux_hint_add(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzpol, struct wl_resource *surf, int32_t id, const char *name, const char *value)
{
   E_Client *ec;
   Eina_Bool res = EINA_FALSE;

   ec = wl_resource_get_user_data(surf);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   res = e_hints_aux_hint_add(ec, id, name, value);

   ELOGF("TZPOL", "HINT_ADD |res_tzpol:%8p|id:%d, name:%s, val:%s, res:%d", ec, res_tzpol, id, name, value, res);

   if (res)
     {
        _e_policy_wl_aux_hint_apply(ec);
        _e_policy_wl_allowed_aux_hint_send(res_tzpol, surf, id);
     }
}

static void
_tzpol_iface_cb_aux_hint_change(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzpol, struct wl_resource *surf, int32_t id, const char *value)
{
   E_Client *ec;
   Eina_Bool res = EINA_FALSE;

   ec = wl_resource_get_user_data(surf);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   res = e_hints_aux_hint_change(ec, id, value);

   ELOGF("TZPOL", "HINT_CHD |res_tzpol:%8p|id:%d, val:%s, result:%d", ec, res_tzpol, id, value, res);

   if (res)
     {
        _e_policy_wl_aux_hint_apply(ec);
        _e_policy_wl_allowed_aux_hint_send(res_tzpol, surf, id);
     }
}

static void
_tzpol_iface_cb_aux_hint_del(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzpol, struct wl_resource *surf, int32_t id)
{
   E_Client *ec;
   unsigned int res = -1;

   ec = wl_resource_get_user_data(surf);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   res = e_hints_aux_hint_del(ec, id);
   ELOGF("TZPOL", "HINT_DEL |res_tzpol:%8p|id:%d, result:%d", ec, res_tzpol, id, res);

   if (res)
     {
        _e_policy_wl_aux_hint_apply(ec);
        _e_policy_wl_allowed_aux_hint_send(res_tzpol, surf, id);
     }
}

static void
_tzpol_iface_cb_supported_aux_hints_get(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzpol, struct wl_resource *surf)
{
   E_Client *ec;
   const Eina_List *hints_list;
   const Eina_List *l;
   struct wl_array hints;
   const char *hint_name;
   int len;
   char *p;

   ec = wl_resource_get_user_data(surf);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   hints_list = e_hints_aux_hint_supported_get();

   wl_array_init(&hints);
   EINA_LIST_FOREACH(hints_list, l, hint_name)
     {
        len = strlen(hint_name) + 1;
        p = wl_array_add(&hints, len);

        if (p == NULL)
          break;
        strncpy(p, hint_name, len);
     }

   tizen_policy_send_supported_aux_hints(res_tzpol, surf, &hints, eina_list_count(hints_list));
   ELOGF("TZPOL",
         "SEND     |res_tzpol:%8p|supported_hints size:%d",
         ec,
         res_tzpol,
         eina_list_count(hints_list));
   wl_array_release(&hints);
}

static void
e_client_background_state_set(E_Client *ec, Eina_Bool state)
{
   if (!ec) return;

   ELOGF("TZPOL",
         "BACKGROUND STATE %s for PID(%u)",
         ec,
         state?"SET":"UNSET", ec->netwm.pid);

   if (state)
     {
        ec->bg_state = EINA_TRUE;
        evas_object_hide(ec->frame);
        e_pixmap_image_clear(ec->pixmap, 1);
        EC_CHANGED(ec);
     }
   else
     {
        ec->bg_state = EINA_FALSE;
        if (ec->iconic)
          e_policy_wl_uniconify(ec);
        else
          {
             evas_object_show(ec->frame);
             e_comp_object_damage(ec->frame, 0, 0, ec->w, ec->h);
          }
     }
}

static void
_e_policy_wl_background_state_set(E_Policy_Wl_Surface *psurf, Eina_Bool state)
{
   if (state)
     {
        if (psurf->ec)
          e_client_background_state_set(psurf->ec, EINA_TRUE);
        else
          {
             ELOGF("TZPOL",
                   "PENDING BACKGROUND STATE SET for PID(%u) psurf:%p tzpol:%p",
                   NULL, psurf->pid, psurf, psurf->tzpol);

             if (!eina_list_data_find(psurf->tzpol->pending_bg, psurf))
               psurf->tzpol->pending_bg =
                  eina_list_append(psurf->tzpol->pending_bg, psurf);
          }
     }
   else
     {
        if (psurf->ec)
          e_client_background_state_set(psurf->ec, EINA_FALSE);
        else
          {
             ELOGF("TZPOL",
                   "UNSET PENDING BACKGROUND STATE for PID(%u) psurf:%p tzpol:%p",
                   NULL, psurf->pid, psurf, psurf->tzpol);

             if (eina_list_data_find(psurf->tzpol->pending_bg, psurf))
               psurf->tzpol->pending_bg =
                  eina_list_remove(psurf->tzpol->pending_bg, psurf);
          }
     }
}

static void
_e_policy_wl_tzlaunch_effect_type_sync(E_Client *ec)
{
   Eina_List *l;
   E_Policy_Wl_Tzlaunch_Effect_Info *effect_info;

   EINA_SAFETY_ON_NULL_RETURN(ec);

   EINA_LIST_FOREACH(polwl->tzlaunch_effect_info, l, effect_info)
     {
        if (effect_info->pid == ec->netwm.pid)
          {
             ELOGF("TZPOL",
                   "Launchscreen effect type sync | pid (%d) effect_type (%d)",
                   ec, ec->netwm.pid, effect_info->effect_type);
             ec->effect_type = effect_info->effect_type;
             _e_policy_wl_tzlaunch_effect_type_unset(ec->netwm.pid);
             break;
          }
     }
}

static int
_e_policy_wl_tzlaunch_effect_type_get(const char * effect_type)
{
   Tzlaunch_Effect_Type type = TZLAUNCH_EFFECT_TYPE_LAUNCH;

   if      (!e_util_strcmp(effect_type, "launch"    )) type = TZLAUNCH_EFFECT_TYPE_LAUNCH;
   else if (!e_util_strcmp(effect_type, "depth-in" )) type = TZLAUNCH_EFFECT_TYPE_DEPTH_IN;

   return type;
}

static void
_e_policy_wl_tzlaunch_effect_type_unset(uint32_t pid)
{
   Eina_List *l;
   E_Policy_Wl_Tzlaunch_Effect_Info *effect_info;

   EINA_LIST_FOREACH(polwl->tzlaunch_effect_info, l, effect_info)
     {
        if (effect_info->pid == pid)
          {
             ELOGF("TZPOL",
                   "Launchscreen effect type unset | pid (%d)",
                   NULL, pid);
             polwl->tzlaunch_effect_info = eina_list_remove(polwl->tzlaunch_effect_info, effect_info);
             memset(effect_info, 0x0, sizeof(E_Policy_Wl_Tzlaunch_Effect_Info));
             E_FREE(effect_info);
             break;
          }
     }
}

static void
_tzpol_iface_cb_background_state_set(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzpol, uint32_t pid)
{
   E_Policy_Wl_Tzpol *tzpol;
   E_Policy_Wl_Surface *psurf;
   Eina_List *psurfs = NULL, *clients = NULL;
   E_Client *ec;

   tzpol = _e_policy_wl_tzpol_get(res_tzpol);
   EINA_SAFETY_ON_NULL_RETURN(tzpol);

   if ((psurfs = _e_policy_wl_tzpol_surf_find_by_pid(tzpol, pid)))
     {
        EINA_LIST_FREE(psurfs, psurf)
          {
             if (psurf->is_background) continue;

             psurf->is_background = EINA_TRUE;
             _e_policy_wl_background_state_set(psurf, EINA_TRUE);
          }

        return;
     }

   clients = _e_policy_wl_e_clients_find_by_pid(pid);

   if (clients)
     {
        EINA_LIST_FREE(clients, ec)
          {
             psurf = _e_policy_wl_surf_add(ec, res_tzpol);

             ELOGF("TZPOL",
                   "Register PID(%u) for BACKGROUND STATE psurf:%p tzpol:%p",
                   ec, pid, psurf, psurf ? psurf->tzpol : NULL);

             psurf->is_background = EINA_TRUE;
             _e_policy_wl_background_state_set(psurf, EINA_TRUE);
          }

        return;
     }
   else
     {
        psurf = E_NEW(E_Policy_Wl_Surface, 1);
        EINA_SAFETY_ON_NULL_RETURN(psurf);

        psurf->tzpol = tzpol;
        psurf->pid = pid;
        psurf->ec = NULL;

        tzpol->psurfs = eina_list_append(tzpol->psurfs, psurf);

        ELOGF("TZPOL",
              "Register PID(%u) for BACKGROUND STATE psurf:%p tzpol:%p",
              NULL, pid, psurf, psurf->tzpol);
     }
   if (psurf)
     {
        psurf->is_background = EINA_TRUE;
        _e_policy_wl_background_state_set(psurf, EINA_TRUE);
     }
}

static void
_tzpol_iface_cb_background_state_unset(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzpol, uint32_t pid)
{
   E_Policy_Wl_Surface *psurf = NULL;
   E_Policy_Wl_Tzpol *tzpol;
   Eina_List *psurfs = NULL;

   tzpol = _e_policy_wl_tzpol_get(res_tzpol);
   EINA_SAFETY_ON_NULL_RETURN(tzpol);

   if ((psurfs = _e_policy_wl_tzpol_surf_find_by_pid(tzpol, pid)))
     {
        EINA_LIST_FREE(psurfs, psurf)
          {
             if (!psurf->is_background) continue;
             psurf->is_background = EINA_FALSE;
             _e_policy_wl_background_state_set(psurf, EINA_FALSE);
          }
        return;
     }
}

static void
_e_policy_wl_floating_mode_apply(E_Client *ec, Eina_Bool floating)
{
   if (ec->floating == floating) return;

   ec->floating = floating;
   ec->lock_client_location = EINA_FALSE;

   if (ec->frame)
     {
        if (floating)
          evas_object_layer_set(ec->frame, E_LAYER_CLIENT_ABOVE);
        else
          evas_object_layer_set(ec->frame, E_LAYER_CLIENT_NORMAL);
     }

   EC_CHANGED(ec);
}

static void
_tzpol_iface_cb_floating_mode_set(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzpol, struct wl_resource *surf)
{
   E_Client *ec;

   ec = wl_resource_get_user_data(surf);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   ELOGF("TZPOL", "FLOATING Set", ec);

   _e_policy_wl_floating_mode_apply(ec, EINA_TRUE);
}

static void
_tzpol_iface_cb_floating_mode_unset(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzpol, struct wl_resource *surf)
{
   E_Client *ec;

   ec = wl_resource_get_user_data(surf);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   ELOGF("TZPOL", "FLOATING Unset", ec);

   e_client_pending_geometry_flush(ec);

   _e_policy_wl_floating_mode_apply(ec, EINA_FALSE);
}

static void
_tzpol_iface_cb_stack_mode_set(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzpol, struct wl_resource *surf, uint32_t mode)
{
   E_Client *ec;

   ec = wl_resource_get_user_data(surf);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   ELOGF("TZPOL", "STACK Mode Set. mode:%d", ec, mode);

   if (ec->frame)
     {
        if (mode == TIZEN_POLICY_STACK_MODE_ABOVE)
          {
             evas_object_layer_set(ec->frame, E_LAYER_CLIENT_ABOVE);
          }
        else if (mode == TIZEN_POLICY_STACK_MODE_BELOW)
          {
             evas_object_layer_set(ec->frame, E_LAYER_CLIENT_BELOW);
          }
        else
          {
             evas_object_layer_set(ec->frame, E_LAYER_CLIENT_NORMAL);
          }
        EC_CHANGED(ec);
     }
}

// --------------------------------------------------------
// E_Policy_Wl_Tz_Dpy_Pol
// --------------------------------------------------------
static E_Policy_Wl_Tz_Dpy_Pol *
_e_policy_wl_tz_dpy_pol_add(struct wl_resource *res_tz_dpy_pol)
{
   E_Policy_Wl_Tz_Dpy_Pol *tz_dpy_pol;

   tz_dpy_pol = E_NEW(E_Policy_Wl_Tz_Dpy_Pol, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tz_dpy_pol, NULL);

   tz_dpy_pol->res_tz_dpy_pol = res_tz_dpy_pol;

   polwl->tz_dpy_pols = eina_list_append(polwl->tz_dpy_pols, tz_dpy_pol);

   return tz_dpy_pol;
}

static void
_e_policy_wl_tz_dpy_pol_del(E_Policy_Wl_Tz_Dpy_Pol *tz_dpy_pol)
{
   E_Policy_Wl_Dpy_Surface *dpy_surf;

   EINA_SAFETY_ON_NULL_RETURN(tz_dpy_pol);

   polwl->tz_dpy_pols = eina_list_remove(polwl->tz_dpy_pols, tz_dpy_pol);

   EINA_LIST_FREE(tz_dpy_pol->dpy_surfs, dpy_surf)
     {
        E_FREE(dpy_surf);
     }

   E_FREE(tz_dpy_pol);
}

static E_Policy_Wl_Tz_Dpy_Pol *
_e_policy_wl_tz_dpy_pol_get(struct wl_resource *res_tz_dpy_pol)
{
   Eina_List *l;
   E_Policy_Wl_Tz_Dpy_Pol *tz_dpy_pol;

   EINA_LIST_FOREACH(polwl->tz_dpy_pols, l, tz_dpy_pol)
     {
        if (tz_dpy_pol->res_tz_dpy_pol == res_tz_dpy_pol)
          return tz_dpy_pol;
     }

   return NULL;
}

// --------------------------------------------------------
// E_Policy_Wl_Dpy_Surface
// --------------------------------------------------------
static E_Policy_Wl_Dpy_Surface *
_e_policy_wl_dpy_surf_find(E_Policy_Wl_Tz_Dpy_Pol *tz_dpy_pol, E_Client *ec)
{
   Eina_List *l;
   E_Policy_Wl_Dpy_Surface *dpy_surf;

   EINA_LIST_FOREACH(tz_dpy_pol->dpy_surfs, l, dpy_surf)
     {
        if (dpy_surf->ec == ec)
          return dpy_surf;
     }

   return NULL;
}

static E_Policy_Wl_Dpy_Surface *
_e_policy_wl_dpy_surf_add(E_Client *ec, struct wl_resource *res_tz_dpy_pol)
{
   E_Policy_Wl_Tz_Dpy_Pol  *tz_dpy_pol = NULL;
   E_Policy_Wl_Dpy_Surface *dpy_surf   = NULL;

   tz_dpy_pol = _e_policy_wl_tz_dpy_pol_get(res_tz_dpy_pol);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tz_dpy_pol, NULL);

   dpy_surf = _e_policy_wl_dpy_surf_find(tz_dpy_pol, ec);
   if (dpy_surf)
     return dpy_surf;

   dpy_surf = E_NEW(E_Policy_Wl_Dpy_Surface, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(dpy_surf, NULL);

   dpy_surf->surf = ec->comp_data->surface;
   dpy_surf->tz_dpy_pol = tz_dpy_pol;
   dpy_surf->ec = ec;
   dpy_surf->brightness = -1;

   tz_dpy_pol->dpy_surfs = eina_list_append(tz_dpy_pol->dpy_surfs, dpy_surf);
   return dpy_surf;
}

static void
_e_policy_wl_dpy_surf_del(E_Client *ec)
{
   Eina_List *l;
   E_Policy_Wl_Tz_Dpy_Pol *tz_dpy_pol;
   E_Policy_Wl_Dpy_Surface *dpy_surf;

   EINA_SAFETY_ON_NULL_RETURN(ec);

   EINA_LIST_FOREACH(polwl->tz_dpy_pols, l, tz_dpy_pol)
     {
        dpy_surf = _e_policy_wl_dpy_surf_find(tz_dpy_pol, ec);
        if (dpy_surf)
          {
             tz_dpy_pol->dpy_surfs = eina_list_remove(tz_dpy_pol->dpy_surfs, dpy_surf);
             E_FREE(dpy_surf);
          }
     }
}

// --------------------------------------------------------
// brightness
// --------------------------------------------------------
static Eina_Bool
_e_policy_system_brightness_get(int *brightness)
{
   int error;
   int sys_brightness = -1;

   if (!brightness) return EINA_FALSE;

   error = device_display_get_brightness(0, &sys_brightness);
   if (error != DEVICE_ERROR_NONE)
     {
        // error
        return EINA_FALSE;
     }

   *brightness = sys_brightness;

   return EINA_TRUE;
}

static Eina_Bool
_e_policy_system_brightness_set(int brightness)
{
   Eina_Bool ret;
   int error;
   int num_of_dpy;
   int id;

   ret = EINA_TRUE;

   error = device_display_get_numbers(&num_of_dpy);
   if (error != DEVICE_ERROR_NONE)
     {
        // error
        return EINA_FALSE;
     }

   for (id = 0; id < num_of_dpy; id++)
     {
        error = device_display_set_brightness(id, brightness);
        if (error != DEVICE_ERROR_NONE)
          {
             // error
             ret = EINA_FALSE;
             break;
          }
     }

   return ret;
}

static Eina_Bool
_e_policy_change_system_brightness(int new_brightness)
{
   Eina_Bool ret;
   int sys_brightness;

   if (!e_policy_system_info.brightness.use_client)
     {
        // save system brightness
        ret = _e_policy_system_brightness_get(&sys_brightness);
        if (!ret)
          {
             return EINA_FALSE;
          }
        e_policy_system_info.brightness.system = sys_brightness;
     }

   ret = _e_policy_system_brightness_set(new_brightness);
   if (!ret)
     {
        return EINA_FALSE;
     }
   e_policy_system_info.brightness.client = new_brightness;
   e_policy_system_info.brightness.use_client = EINA_TRUE;

   return EINA_TRUE;
}

static Eina_Bool
_e_policy_restore_system_brightness(void)
{
   Eina_Bool ret;

   if (!e_policy_system_info.brightness.use_client) return EINA_TRUE;

   // restore system brightness
   ret = _e_policy_system_brightness_set(e_policy_system_info.brightness.system);
   if (!ret)
     {
        return EINA_FALSE;
     }
   e_policy_system_info.brightness.use_client = EINA_FALSE;

   // Todo:
   // if there are another window which set brighteness, then we change brighteness of it
   // if no, then we rollback system brightness

   return EINA_TRUE;
}

Eina_Bool
e_policy_wl_win_brightness_apply(E_Client *ec)
{
   Eina_Bool ret;
   Eina_List *l;
   E_Policy_Wl_Tz_Dpy_Pol *tz_dpy_pol;
   E_Policy_Wl_Dpy_Surface *dpy_surf = NULL;
   int ec_visibility;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);
   if (e_object_is_del(E_OBJECT(ec)))
     ec_visibility = E_VISIBILITY_FULLY_OBSCURED;
   else
     ec_visibility = ec->visibility.obscured;

   EINA_LIST_FOREACH(polwl->tz_dpy_pols, l, tz_dpy_pol)
     {
        dpy_surf = _e_policy_wl_dpy_surf_find(tz_dpy_pol, ec);
        if (dpy_surf)
          break;
     }

   if (!dpy_surf) return EINA_FALSE;
   if (!dpy_surf->set) return EINA_FALSE;

   // use system brightness
   if (dpy_surf->brightness < 0)
     {
        ELOGF("TZ_DPY_POL", "Restore system brightness. Win(0x%08zx)'s brightness:%d", ec, e_client_util_win_get(ec), dpy_surf->brightness);
        ret = _e_policy_restore_system_brightness();
        return ret;
     }

   if (ec_visibility == E_VISIBILITY_UNOBSCURED)
     {
        ELOGF("TZ_DPY_POL", "Change system brightness(%d). Win(0x%08zx) is un-obscured", ec, dpy_surf->brightness, e_client_util_win_get(ec));
        ret = _e_policy_change_system_brightness(dpy_surf->brightness);
        if (!ret) return EINA_FALSE;
     }
   else
     {
        ELOGF("TZ_DPY_POL", "Restore system brightness. Win(0x%08zx) is obscured", ec, e_client_util_win_get(ec));
        ret = _e_policy_restore_system_brightness();
        if (!ret) return EINA_FALSE;
     }

   return EINA_TRUE;
}

static void
_tz_dpy_pol_iface_cb_brightness_set(struct wl_client *client, struct wl_resource *res_tz_dpy_pol, struct wl_resource *surf, int32_t brightness)
{
   E_Client *ec;
   E_Policy_Wl_Dpy_Surface *dpy_surf;
   pid_t pid = 0;
   uid_t uid = 0;
   Eina_Bool res;

   ec = wl_resource_get_user_data(surf);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   dpy_surf = _e_policy_wl_dpy_surf_add(ec, res_tz_dpy_pol);
   EINA_SAFETY_ON_NULL_RETURN(dpy_surf);

   wl_client_get_credentials(client, &pid, &uid, NULL);
   res = e_security_privilege_check(pid, uid,
                                    E_PRIVILEGE_BRIGHTNESS_SET);
   if (!res)
     {
        ELOGF("TZ_DPY_POL",
              "Privilege Check Failed! DENY set_brightness",
              ec);

        tizen_display_policy_send_window_brightness_done
           (res_tz_dpy_pol,
            surf,
            -1,
            TIZEN_DISPLAY_POLICY_ERROR_STATE_PERMISSION_DENIED);
        return;
     }
   ELOGF("TZ_DPY_POL", "Set Win(0x%08zx)'s brightness:%d", ec, e_client_util_win_get(ec), brightness);
   dpy_surf->set = EINA_TRUE;
   dpy_surf->brightness = brightness;

   e_policy_wl_win_brightness_apply(ec);

   tizen_display_policy_send_window_brightness_done
      (res_tz_dpy_pol, surf, brightness, TIZEN_DISPLAY_POLICY_ERROR_STATE_NONE);
}

static void
_tz_dpy_pol_iface_cb_destroy(struct wl_client *client, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void
_tzpol_iface_cb_subsurf_watcher_destroy(struct wl_resource *resource)
{
   E_Client *ec;

   if (!(ec = wl_resource_get_user_data(resource))) return;
   if (e_object_is_del(E_OBJECT(ec)) || !ec->comp_data) return;

   ec->comp_data->sub.watcher = NULL;
}

static void
_tzpol_subsurf_watcher_iface_cb_destroy(struct wl_client *client, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static const struct tizen_subsurface_watcher_interface _tzpol_subsurf_watcher_iface =
{
   _tzpol_subsurf_watcher_iface_cb_destroy,
};

static void
_tzpol_iface_cb_subsurf_watcher_get(struct wl_client *client, struct wl_resource *res_tzpol, uint32_t id, struct wl_resource *surface)
{
   E_Client *ec;
   struct wl_resource *res;

   if (!(ec = wl_resource_get_user_data(surface))) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   if (!(res = wl_resource_create(client, &tizen_subsurface_watcher_interface, 1, id)))
     {
        wl_resource_post_no_memory(res_tzpol);
        return;
     }

   ec->comp_data->sub.watcher = res;

   wl_resource_set_implementation(res,
                                  &_tzpol_subsurf_watcher_iface,
                                  ec,
                                  _tzpol_iface_cb_subsurf_watcher_destroy);
}

static void
_tzpol_iface_cb_parent_set(struct wl_client *client, struct wl_resource *res_tzpol, struct wl_resource *child, struct wl_resource *parent)
{
   E_Client *ec, *pc;
   struct wl_resource *parent_surf;

   ELOGF("TZPOL",
         "PARENT_SET   |res_tzpol:%8p|parent:%8p|child:%8p",
         NULL, res_tzpol, parent, child);

   ec = wl_resource_get_user_data(child);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   pc = wl_resource_get_user_data(parent);
   if (!pc)
     {
        _e_policy_wl_parent_surf_set(ec, NULL);
     }
   else
     {
        EINA_SAFETY_ON_NULL_RETURN(pc->comp_data);

        parent_surf = pc->comp_data->surface;
        _e_policy_wl_parent_surf_set(ec, parent_surf);

        ELOGF("TZPOL",
              "         |win:0x%08zx|parent|s:%8p",
              pc,
              e_client_util_win_get(pc),
              parent_surf);

        ELOGF("TZPOL",
              "         |win:0x%08zx|child |s:%8p",
              ec,
              e_client_util_win_get(ec),
              (ec->comp_data ? ec->comp_data->surface : NULL));
     }

   EC_CHANGED(ec);
}

static void
_tzpol_iface_cb_ack_conformant_region(struct wl_client *client, struct wl_resource *res_tzpol, struct wl_resource *surface, uint32_t serial)
{
   E_Client *ec;

   if (!(ec = wl_resource_get_user_data(surface))) return;

   e_policy_conformant_client_ack(ec, res_tzpol, serial);
}

static void
_tzpol_iface_cb_destroy(struct wl_client *client, struct wl_resource *res_tzpol)
{
   wl_resource_destroy(res_tzpol);
}

static void
_tzpol_iface_cb_has_video(struct wl_client *client, struct wl_resource *res_tzpol, struct wl_resource *surface, uint32_t has)
{
   E_Client *ec;

   if (!(ec = wl_resource_get_user_data(surface))) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (ec->comp_data->has_video_client == has) return;

   ELOGF("TZPOL", "video client has(%d)", ec, has);

   ec->comp_data->has_video_client = has;
}

// --------------------------------------------------------
// tizen_policy_interface
// --------------------------------------------------------
static const struct tizen_policy_interface _tzpol_iface =
{
   _tzpol_iface_cb_vis_get,
   _tzpol_iface_cb_pos_get,
   _tzpol_iface_cb_activate,
   _tzpol_iface_cb_activate_below_by_res_id,
   _tzpol_iface_cb_raise,
   _tzpol_iface_cb_lower,
   _tzpol_iface_cb_lower_by_res_id,
   _tzpol_iface_cb_focus_skip_set,
   _tzpol_iface_cb_focus_skip_unset,
   _tzpol_iface_cb_role_set,
   _tzpol_iface_cb_type_set,
   _tzpol_iface_cb_conformant_set,
   _tzpol_iface_cb_conformant_unset,
   _tzpol_iface_cb_conformant_get,
   _tzpol_iface_cb_notilv_set,
   _tzpol_iface_cb_transient_for_set,
   _tzpol_iface_cb_transient_for_unset,
   _tzpol_iface_cb_win_scrmode_set,
   _tzpol_iface_cb_subsurf_place_below_parent,
   _tzpol_iface_cb_subsurf_stand_alone_set,
   _tzpol_iface_cb_subsurface_get,
   _tzpol_iface_cb_opaque_state_set,
   _tzpol_iface_cb_iconify,
   _tzpol_iface_cb_uniconify,
   _tzpol_iface_cb_aux_hint_add,
   _tzpol_iface_cb_aux_hint_change,
   _tzpol_iface_cb_aux_hint_del,
   _tzpol_iface_cb_supported_aux_hints_get,
   _tzpol_iface_cb_background_state_set,
   _tzpol_iface_cb_background_state_unset,
   _tzpol_iface_cb_floating_mode_set,
   _tzpol_iface_cb_floating_mode_unset,
   _tzpol_iface_cb_stack_mode_set,
   _tzpol_iface_cb_activate_above_by_res_id,
   _tzpol_iface_cb_subsurf_watcher_get,
   _tzpol_iface_cb_parent_set,
   _tzpol_iface_cb_ack_conformant_region,
   _tzpol_iface_cb_destroy,
   _tzpol_iface_cb_has_video,
};

static void
_tzpol_cb_unbind(struct wl_resource *res_tzpol)
{
   E_Policy_Wl_Tzpol *tzpol;

   tzpol = _e_policy_wl_tzpol_get(res_tzpol);
   EINA_SAFETY_ON_NULL_RETURN(tzpol);

   eina_hash_del_by_key(polwl->tzpols, &res_tzpol);
}

static void
_tzpol_cb_bind(struct wl_client *client, void *data EINA_UNUSED, uint32_t ver, uint32_t id)
{
   E_Policy_Wl_Tzpol *tzpol;
   struct wl_resource *res_tzpol;

   EINA_SAFETY_ON_NULL_GOTO(polwl, err);

   res_tzpol = wl_resource_create(client,
                                  &tizen_policy_interface,
                                  ver,
                                  id);
   EINA_SAFETY_ON_NULL_GOTO(res_tzpol, err);

   tzpol = _e_policy_wl_tzpol_add(res_tzpol);
   EINA_SAFETY_ON_NULL_GOTO(tzpol, err);

   wl_resource_set_implementation(res_tzpol,
                                  &_tzpol_iface,
                                  NULL,
                                  _tzpol_cb_unbind);
   return;

err:
   ERR("Could not create tizen_policy_interface res: %m");
   wl_client_post_no_memory(client);
}

// --------------------------------------------------------
// tizen_display_policy_interface
// --------------------------------------------------------
static const struct tizen_display_policy_interface _tz_dpy_pol_iface =
{
   _tz_dpy_pol_iface_cb_brightness_set,
   _tz_dpy_pol_iface_cb_destroy,
};

static void
_tz_dpy_pol_cb_unbind(struct wl_resource *res_tz_dpy_pol)
{
   E_Policy_Wl_Tz_Dpy_Pol *tz_dpy_pol;

   tz_dpy_pol = _e_policy_wl_tz_dpy_pol_get(res_tz_dpy_pol);
   EINA_SAFETY_ON_NULL_RETURN(tz_dpy_pol);

   _e_policy_wl_tz_dpy_pol_del(tz_dpy_pol);
}

static void
_tz_dpy_pol_cb_bind(struct wl_client *client, void *data EINA_UNUSED, uint32_t ver, uint32_t id)
{
   E_Policy_Wl_Tz_Dpy_Pol *tz_dpy_pol;
   struct wl_resource *res_tz_dpy_pol;

   EINA_SAFETY_ON_NULL_GOTO(polwl, err);

   res_tz_dpy_pol = wl_resource_create(client,
                                       &tizen_display_policy_interface,
                                       ver,
                                       id);
   EINA_SAFETY_ON_NULL_GOTO(res_tz_dpy_pol, err);

   tz_dpy_pol = _e_policy_wl_tz_dpy_pol_add(res_tz_dpy_pol);
   EINA_SAFETY_ON_NULL_GOTO(tz_dpy_pol, err);

   wl_resource_set_implementation(res_tz_dpy_pol,
                                  &_tz_dpy_pol_iface,
                                  NULL,
                                  _tz_dpy_pol_cb_unbind);
   return;

err:
   ERR("Could not create tizen_display_policy_interface res: %m");
   wl_client_post_no_memory(client);
}

// --------------------------------------------------------
// tizen_ws_shell_interface::service
// --------------------------------------------------------
static void
_tzsh_srv_iface_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzsh_srv)
{
   wl_resource_destroy(res_tzsh_srv);
}

static void
_tzsh_srv_iface_cb_region_set(struct wl_client *client, struct wl_resource *res_tzsh_srv, int32_t type, int32_t angle, struct wl_resource *res_reg)
{
   E_Policy_Wl_Tzsh_Srv *tzsh_srv;
   E_Policy_Wl_Tzsh_Region *tzsh_reg;

   tzsh_srv = wl_resource_get_user_data(res_tzsh_srv);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv);

   if (!eina_list_data_find(polwl->tzsh_srvs, tzsh_srv))
     return;

   tzsh_reg = wl_resource_get_user_data(res_reg);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_reg);

   if ((tzsh_srv->role == TZSH_SRV_ROLE_QUICKPANEL_SYSTEM_DEFAULT) ||
       (tzsh_srv->role == TZSH_SRV_ROLE_QUICKPANEL_CONTEXT_MENU))
     {
        EINA_SAFETY_ON_NULL_RETURN(tzsh_srv->tzsh);
        EINA_SAFETY_ON_NULL_RETURN(tzsh_srv->tzsh->ec);

        e_service_quickpanel_region_set(tzsh_srv->tzsh->ec,
                                        type,
                                        angle,
                                        tzsh_reg->tiler);
     }
   else if (tzsh_srv->role == TZSH_SRV_ROLE_VOLUME)
     e_service_volume_region_set(type, angle, tzsh_reg->tiler);
}

static void
_tzsh_srv_indicator_cb_resource_destroy(struct wl_resource *resource)
{
   if (_indicator_srv_res == resource)
     _indicator_srv_res = NULL;
}

static void
_tzsh_srv_indicator_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   _indicator_srv_res = NULL;
   wl_resource_destroy(resource);
}

static const struct tws_service_indicator_interface _tzsh_srv_indicator_iface =
{
   _tzsh_srv_indicator_cb_destroy,
};

static void
_tzsh_srv_iface_cb_indicator_get(struct wl_client *client, struct wl_resource *res_tzsh_srv, uint32_t id)
{
   E_Policy_Wl_Tzsh_Srv *tzsh_srv;
   struct wl_resource *res;

   tzsh_srv = wl_resource_get_user_data(res_tzsh_srv);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv);

   if (!eina_list_data_find(polwl->tzsh_srvs, tzsh_srv))
     return;

   res = wl_resource_create(client, &tws_service_indicator_interface, 1, id);
   if (!res)
     {
        wl_client_post_no_memory(client);
        return;
     }
   _indicator_srv_res = res;

   wl_resource_set_implementation(res, &_tzsh_srv_indicator_iface, tzsh_srv,
                                  _tzsh_srv_indicator_cb_resource_destroy);
}

static void
_tzsh_srv_qp_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void
_tzsh_srv_qp_cb_msg(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, uint32_t msg)
{
   E_Policy_Wl_Tzsh_Srv *tzsh_srv;

   tzsh_srv = wl_resource_get_user_data(resource);

   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv->tzsh);

#define EC  tzsh_srv->tzsh->ec
   EINA_SAFETY_ON_NULL_RETURN(EC);

   switch (msg)
     {
      case TWS_SERVICE_QUICKPANEL_MSG_SHOW:
         e_service_quickpanel_show(EC);
         break;
      case TWS_SERVICE_QUICKPANEL_MSG_HIDE:
         e_service_quickpanel_hide(EC);
         break;
      default:
         ERR("Unknown message!! msg %d", msg);
         break;
     }
#undef EC
}

static void
_tzsh_srv_qp_cb_effect_type_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, uint32_t type)
{
   E_Policy_Wl_Tzsh_Srv *tzsh_srv;

   tzsh_srv = wl_resource_get_user_data(resource);

   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv->tzsh);

#define EC  tzsh_srv->tzsh->ec
   EINA_SAFETY_ON_NULL_RETURN(EC);
   e_service_quickpanel_effect_type_set(EC, type);
#undef EC
}

static void
_tzsh_srv_qp_cb_scroll_lock_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, uint32_t lock)
{
   E_Policy_Wl_Tzsh_Srv *tzsh_srv;

   tzsh_srv = wl_resource_get_user_data(resource);

   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv->tzsh);

#define EC  tzsh_srv->tzsh->ec
   EINA_SAFETY_ON_NULL_RETURN(EC);
   e_service_quickpanel_scroll_lock_set(EC, lock);
#undef EC
}

static const struct tws_service_quickpanel_interface _tzsh_srv_qp_iface =
{
   _tzsh_srv_qp_cb_destroy,
   _tzsh_srv_qp_cb_msg,
   _tzsh_srv_qp_cb_effect_type_set,
   _tzsh_srv_qp_cb_scroll_lock_set,
};

static void
_tzsh_srv_iface_cb_quickpanel_get(struct wl_client *client, struct wl_resource *res_tzsh_srv, uint32_t id)
{
   E_Policy_Wl_Tzsh_Srv *tzsh_srv;
   struct wl_resource *res;

   tzsh_srv = wl_resource_get_user_data(res_tzsh_srv);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv);

   if (!eina_list_data_find(polwl->tzsh_srvs, tzsh_srv))
     return;

   res = wl_resource_create(client,
                            &tws_service_quickpanel_interface,
                            wl_resource_get_version(res_tzsh_srv),
                            id);
   if (!res)
     {
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(res, &_tzsh_srv_qp_iface, tzsh_srv, NULL);
}



static void
_tzsh_srv_softkey_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void
_tzsh_srv_softkey_cb_msg_send(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, uint32_t msg)
{
   E_Policy_Wl_Tzsh_Srv *tzsh_srv;
   E_Service_Softkey *softkey;
   E_Client *softkey_ec;

   tzsh_srv = wl_resource_get_user_data(resource);

   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv->tzsh);

   softkey_ec = tzsh_srv->tzsh->ec;
   EINA_SAFETY_ON_NULL_RETURN(softkey_ec);

   softkey = e_service_softkey_get(softkey_ec->zone);
   EINA_SAFETY_ON_NULL_RETURN(softkey);

   switch (msg)
     {
      case TWS_SERVICE_SOFTKEY_MSG_SHOW:
         e_service_softkey_show(softkey);
         break;
      case TWS_SERVICE_SOFTKEY_MSG_HIDE:
         e_service_softkey_hide(softkey);
         break;
      default:
         ERR("Unknown message!! msg %d", msg);
         break;
     }
}

static const struct tws_service_softkey_interface _tzsh_srv_softkey_iface =
{
   _tzsh_srv_softkey_cb_destroy,
   _tzsh_srv_softkey_cb_msg_send,
};

static void
_tzsh_srv_iface_cb_softkey_get(struct wl_client *client, struct wl_resource *res_tzsh_srv, uint32_t id)
{
   E_Policy_Wl_Tzsh_Srv *tzsh_srv;
   E_Service_Softkey *softkey = NULL;
   struct wl_resource *res;

   tzsh_srv = wl_resource_get_user_data(res_tzsh_srv);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv);

   if (!eina_list_data_find(polwl->tzsh_srvs, tzsh_srv))
     return;

   res = wl_resource_create(client, &tws_service_softkey_interface, 1, id);
   if (!res)
     {
        wl_client_post_no_memory(client);
        return;
     }

   ELOGF("TZSH", "[SOFTKEY SERVICE] resource created. res:%p, res_tzsh_srv:%p, id:%d", NULL, res, res_tzsh_srv, id);

   if (tzsh_srv->tzsh && tzsh_srv->tzsh->ec)
     {
        E_Client *softkey_ec = tzsh_srv->tzsh->ec;
        softkey = e_service_softkey_get(softkey_ec->zone);
        ELOGF("TZSH", "[SOFTKEY SERVICE] resource set. res:%p, softkey:%p, softkey_ec:%p", NULL, res, softkey, softkey_ec);
        if (softkey)
          e_service_softkey_wl_resource_set(softkey, res);
     }

   wl_resource_set_implementation(res, &_tzsh_srv_softkey_iface, tzsh_srv, NULL);
}

static void
_tzsh_srv_magnifier_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void
_tzsh_srv_magnifier_cb_zoom_geometry_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, uint32_t angle, int32_t x, int32_t y, uint32_t w, uint32_t h)
{
   E_Policy_Wl_Tzsh_Srv *tzsh_srv;
   E_Client *ec;

   tzsh_srv = wl_resource_get_user_data(resource);

   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv->tzsh);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv->tzsh->ec);

   ELOGF("TZSH", "[MAGNIFIER] Set Geometry. angle:%d, geo:%d,%d,%dx%d", tzsh_srv->tzsh->ec, angle, x, y, w, h);

   ec = tzsh_srv->tzsh->ec;
   // angle: 0, 90, 180, 270
   e_magnifier_zoom_obj_geometry_set(ec, angle, x, y, w, h);
}

static void
_tzsh_srv_magnifier_cb_ratio_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, int32_t ratio)
{
   E_Policy_Wl_Tzsh_Srv *tzsh_srv;
   E_Client *ec;

   tzsh_srv = wl_resource_get_user_data(resource);

   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv->tzsh);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv->tzsh->ec);

   ELOGF("TZSH", "[MAGNIFIER] Set Ratio. ratio:%d", tzsh_srv->tzsh->ec, ratio);

   ec = tzsh_srv->tzsh->ec;
   // ratio : 100 ~ 200 (each 10)
   e_magnifier_zoom_obj_ratio_set(ec, ratio);
}

static void
_tzsh_srv_magnifier_cb_enable_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, int32_t enable)
{
   E_Policy_Wl_Tzsh_Srv *tzsh_srv;
   E_Client *ec;

   tzsh_srv = wl_resource_get_user_data(resource);

   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv->tzsh);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv->tzsh->ec);

   ELOGF("TZSH", "[MAGNIFIER] Set Enable. enable:%d", tzsh_srv->tzsh->ec, enable);

   ec = tzsh_srv->tzsh->ec;

   if (enable)
     e_magnifier_show(ec);
   else
     e_magnifier_hide(ec);
}

static const struct tws_service_magnifier_interface _tzsh_srv_magnifier_iface =
{
   _tzsh_srv_magnifier_cb_destroy,
   _tzsh_srv_magnifier_cb_zoom_geometry_set,
   _tzsh_srv_magnifier_cb_ratio_set,
   _tzsh_srv_magnifier_cb_enable_set,
};

static void
_tzsh_srv_iface_cb_magnifier_get(struct wl_client *client, struct wl_resource *res_tzsh_srv, uint32_t id)
{
   E_Policy_Wl_Tzsh_Srv *tzsh_srv;
   struct wl_resource *res;

   tzsh_srv = wl_resource_get_user_data(res_tzsh_srv);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv);

   if (!eina_list_data_find(polwl->tzsh_srvs, tzsh_srv))
     return;

   res = wl_resource_create(client, &tws_service_magnifier_interface, 1, id);
   if (!res)
     {
        wl_client_post_no_memory(client);
        return;
     }

   ELOGF("TZSH", "[MAGNIFIER] resource created. res:%p, res_tzsh_srv:%p, id:%d", NULL, res, res_tzsh_srv, id);
   wl_resource_set_implementation(res, &_tzsh_srv_magnifier_iface, tzsh_srv, NULL);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////
static void
_tzsh_srv_scrsaver_cb_release(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static const struct tws_service_screensaver_interface _tzsh_srv_scrsaver_iface =
{
   _tzsh_srv_scrsaver_cb_release
};

static void
_tzsh_srv_iface_cb_scrsaver_get(struct wl_client *client, struct wl_resource *res_tzsh_srv, uint32_t id)
{
   E_Policy_Wl_Tzsh_Srv *tzsh_srv;
   struct wl_resource *res;

   tzsh_srv = wl_resource_get_user_data(res_tzsh_srv);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv);

   if (!eina_list_data_find(polwl->tzsh_srvs, tzsh_srv))
     return;

   res = wl_resource_create(client, &tws_service_screensaver_interface, 1, id);
   if (!res)
     {
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(res, &_tzsh_srv_scrsaver_iface, tzsh_srv, NULL);
}

static void
_tzsh_srv_scrsaver_mng_cb_resource_destroy(struct wl_resource *resource)
{
   if (_scrsaver_mng_res == resource)
     {
        _scrsaver_mng_res = NULL;
        e_screensaver_disable();
     }
}

static void
_tzsh_srv_scrsaver_mng_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   _scrsaver_mng_res = NULL;
   wl_resource_destroy(resource);
   e_screensaver_disable();
}

static void
_tzsh_srv_scrsaver_mng_cb_enable(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   E_Policy_Wl_Tzsh_Srv *tzsh_srv;

   tzsh_srv = wl_resource_get_user_data(resource);

   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv->tzsh);

   e_screensaver_enable();
}

static void
_tzsh_srv_scrsaver_mng_cb_disable(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   E_Policy_Wl_Tzsh_Srv *tzsh_srv;

   tzsh_srv = wl_resource_get_user_data(resource);

   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv->tzsh);

   e_screensaver_disable();
}

static void
_tzsh_srv_scrsaver_mng_cb_idle_time_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, uint32_t time)
{
   E_Policy_Wl_Tzsh_Srv *tzsh_srv;
   double timeout;

   tzsh_srv = wl_resource_get_user_data(resource);

   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv->tzsh);

   /* convert time to seconds (double) from milliseconds (unsigned int) */
   timeout = (double)time * 0.001f;

   e_screensaver_timeout_set(timeout);
}

static void
_tzsh_srv_scrsaver_mng_cb_state_get(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, uint32_t type)
{
   E_Policy_Wl_Tzsh_Srv *tzsh_srv;
   uint32_t val = 0;
   double timeout;

   tzsh_srv = wl_resource_get_user_data(resource);

   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv->tzsh);

   switch (type)
     {
      case TWS_SERVICE_SCREENSAVER_MANAGER_STATE_TYPE_IDLE_TIMEOUT:
        /* convert time to milliseconds (unsigned int) from seconds (double) */
        timeout = e_screensaver_timeout_get();
        val = (uint32_t)(timeout * 1000);
        break;
      default:
        break;
     }

   tws_service_screensaver_manager_send_state_get_done(resource, type, val, 0);
}

static const struct tws_service_screensaver_manager_interface _tzsh_srv_scrsaver_mng_iface =
{
   _tzsh_srv_scrsaver_mng_cb_destroy,
   _tzsh_srv_scrsaver_mng_cb_enable,
   _tzsh_srv_scrsaver_mng_cb_disable,
   _tzsh_srv_scrsaver_mng_cb_idle_time_set,
   _tzsh_srv_scrsaver_mng_cb_state_get
};

static void
_tzsh_srv_iface_cb_scrsaver_mng_get(struct wl_client *client, struct wl_resource *res_tzsh_srv, uint32_t id)
{
   E_Policy_Wl_Tzsh_Srv *tzsh_srv;
   struct wl_resource *res;

   tzsh_srv = wl_resource_get_user_data(res_tzsh_srv);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv);

   if (!eina_list_data_find(polwl->tzsh_srvs, tzsh_srv))
     return;

   res = wl_resource_create(client, &tws_service_screensaver_manager_interface, 1, id);
   if (!res)
     {
        wl_client_post_no_memory(client);
        return;
     }

   _scrsaver_mng_res = res;

   wl_resource_set_implementation(res, &_tzsh_srv_scrsaver_mng_iface, tzsh_srv,
                                  _tzsh_srv_scrsaver_mng_cb_resource_destroy);
}

static void
_tzsh_srv_cbhm_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void
_tzsh_srv_cbhm_cb_msg(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, uint32_t msg)
{
   E_Policy_Wl_Tzsh_Srv *tzsh_srv;

   tzsh_srv = wl_resource_get_user_data(resource);

   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv->tzsh);

#define EC  tzsh_srv->tzsh->ec
   EINA_SAFETY_ON_NULL_RETURN(EC);

   switch (msg)
     {
      case TWS_SERVICE_CBHM_MSG_SHOW:
         e_service_cbhm_show();
         break;
      case TWS_SERVICE_CBHM_MSG_HIDE:
         e_service_cbhm_hide();
         break;
      case TWS_SERVICE_CBHM_MSG_DATA_SELECTED:
         e_service_cbhm_data_selected();
         break;
      default:
         ERR("Unknown message!! msg %d", msg);
         break;
     }
#undef EC
}

static const struct tws_service_cbhm_interface _tzsh_srv_cbhm_iface =
{
   _tzsh_srv_cbhm_cb_destroy,
   _tzsh_srv_cbhm_cb_msg
};

static void
_tzsh_srv_iface_cb_cbhm_get(struct wl_client *client, struct wl_resource *res_tzsh_srv, uint32_t id)
{
   E_Policy_Wl_Tzsh_Srv *tzsh_srv;
   struct wl_resource *res;

   tzsh_srv = wl_resource_get_user_data(res_tzsh_srv);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv);

   if (!eina_list_data_find(polwl->tzsh_srvs, tzsh_srv))
     return;

   res = wl_resource_create(client, &tws_service_cbhm_interface, 1, id);
   if (!res)
     {
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(res, &_tzsh_srv_cbhm_iface, tzsh_srv, NULL);
}


static void
_tzsh_srv_iface_cb_launcher_get(struct wl_client *client, struct wl_resource *res_tzsh_srv, uint32_t id)
{
   E_Policy_Wl_Tzsh_Srv *tzsh_srv;
   struct wl_resource *res;

   tzsh_srv = wl_resource_get_user_data(res_tzsh_srv);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv->tzsh);

   if (!eina_list_data_find(polwl->tzsh_srvs, tzsh_srv))
     return;

   res = wl_resource_create(client, &tws_service_launcher_interface, 1, id);
   if (!res)
     {
        wl_client_post_no_memory(client);
        return;
     }

   e_service_launcher_resource_set(tzsh_srv->tzsh->ec, res);
}

static const struct tws_service_interface _tzsh_srv_iface =
{
   _tzsh_srv_iface_cb_destroy,
   _tzsh_srv_iface_cb_region_set,
   _tzsh_srv_iface_cb_indicator_get,
   _tzsh_srv_iface_cb_quickpanel_get,
   _tzsh_srv_iface_cb_scrsaver_mng_get,
   _tzsh_srv_iface_cb_scrsaver_get,
   _tzsh_srv_iface_cb_cbhm_get,
   _tzsh_srv_iface_cb_softkey_get,
   _tzsh_srv_iface_cb_magnifier_get,
   _tzsh_srv_iface_cb_launcher_get,
};

static void
_tzsh_cb_srv_destroy(struct wl_resource *res_tzsh_srv)
{
   E_Policy_Wl_Tzsh_Srv *tzsh_srv;

   tzsh_srv = wl_resource_get_user_data(res_tzsh_srv);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_srv);

   if (!eina_list_data_find(polwl->tzsh_srvs, tzsh_srv))
     return;

   _e_policy_wl_tzsh_srv_del(tzsh_srv);
}

static void
_tzsh_iface_cb_srv_create(struct wl_client *client, struct wl_resource *res_tzsh, uint32_t id, uint32_t surf_id, const char *name)
{
   E_Policy_Wl_Tzsh *tzsh;
   E_Policy_Wl_Tzsh_Srv *tzsh_srv;
   struct wl_resource *res_tzsh_srv;
   E_Client *ec;
   E_Pixmap *cp;
   int role;
   pid_t pid;
   uid_t uid;
   Eina_Bool res;

   role = _e_policy_wl_tzsh_srv_role_get(name);
   if (role == TZSH_SRV_ROLE_UNKNOWN)
     {
        wl_resource_post_error
          (res_tzsh,
           WL_DISPLAY_ERROR_INVALID_OBJECT,
           "Invalid res_tzsh");
        return;
     }

   /* check whether client has a privilege */
   if (role == TZSH_SRV_ROLE_MAGNIFIER)
     {
        wl_client_get_credentials(client, &pid, &uid, NULL);
        res = e_security_privilege_check(pid,
                                         uid,
                                         E_PRIVILEGE_MAGNIFIER_SERVICE);
        if (!res)
          {
             ERR("Could not get privilege of resource: %m");
             tizen_ws_shell_send_error(res_tzsh,
                                       TIZEN_WS_SHELL_ERROR_PERMISSION_DENIED);
             return;
          }
     }
   else if (role == TZSH_SRV_ROLE_LAUNCHER)
     {
        wl_client_get_credentials(client, &pid, &uid, NULL);
        res = e_security_privilege_check(pid, uid,
                                         E_PRIVILEGE_LAUNCHER_SERVICE);
        if (!res)
          {
             ERR("Could not get privilege of resource: %m");
             tizen_ws_shell_send_error(res_tzsh,
                                       TIZEN_WS_SHELL_ERROR_PERMISSION_DENIED);
             return;
          }
     }

   /* to avoid sending a wayland error after tzsh ERROR_NONE for every cases
    * such as invalid object or no memory error, tzsh ERROR_NONE should be sent
    * first to clients without privilege problem.
    */
   tizen_ws_shell_send_error(res_tzsh, TIZEN_WS_SHELL_ERROR_NONE);

   tzsh = wl_resource_get_user_data(res_tzsh);
   if (!tzsh)
     {
        wl_resource_post_error
          (res_tzsh,
           WL_DISPLAY_ERROR_INVALID_OBJECT,
           "Invalid res_tzsh's user data");
        return;
     }

   cp = _e_policy_wl_e_pixmap_get_from_id(client, surf_id);
   if (!cp)
     {
        if (role == TZSH_SRV_ROLE_INDICATOR)
          cp = e_pixmap_new(E_PIXMAP_TYPE_NONE, 0);

        if (!cp)
          {
             wl_resource_post_error
               (res_tzsh,
                WL_DISPLAY_ERROR_INVALID_OBJECT,
                "Invalid surface id");
             return;
          }
     }

   ec = e_pixmap_client_get(cp);
   if (!ec)
     {
        if (role == TZSH_SRV_ROLE_INDICATOR)
          {
             ec = e_client_new(cp, 0, 1);
             if (ec) ec->ignored = 1;
          }
     }

   if (ec)
     {
        if (!_e_policy_wl_e_client_is_valid(ec))
          {
             wl_resource_post_error
               (res_tzsh,
                WL_DISPLAY_ERROR_INVALID_OBJECT,
                "Invalid surface id");
             return;
          }
     }

   res_tzsh_srv = wl_resource_create(client,
                                     &tws_service_interface,
                                     wl_resource_get_version(res_tzsh),
                                     id);
   if (!res_tzsh_srv)
     {
        ERR("Could not create tws_service resource: %m");
        wl_client_post_no_memory(client);
        return;
     }

   _e_policy_wl_tzsh_data_set(tzsh, TZSH_TYPE_SRV, cp, ec);

   tzsh_srv = _e_policy_wl_tzsh_srv_add(tzsh,
                                        role,
                                        res_tzsh_srv,
                                        name);
   if (!tzsh_srv)
     {
        ERR("Could not create WS_Shell_Service");
        wl_client_post_no_memory(client);
        wl_resource_destroy(res_tzsh_srv);
        return;
     }

   wl_resource_set_implementation(res_tzsh_srv,
                                  &_tzsh_srv_iface,
                                  tzsh_srv,
                                  _tzsh_cb_srv_destroy);

   if (role == TZSH_SRV_ROLE_QUICKPANEL_SYSTEM_DEFAULT)
     e_service_quickpanel_client_add(tzsh->ec, E_SERVICE_QUICKPANEL_TYPE_SYSTEM_DEFAULT);
   else if (role == TZSH_SRV_ROLE_QUICKPANEL_CONTEXT_MENU)
     e_service_quickpanel_client_add(tzsh->ec, E_SERVICE_QUICKPANEL_TYPE_CONTEXT_MENU);
   else if (role == TZSH_SRV_ROLE_VOLUME)
     e_service_volume_client_set(tzsh->ec);
   else if (role == TZSH_SRV_ROLE_LOCKSCREEN)
     e_service_lockscreen_client_set(tzsh->ec);
   else if (role == TZSH_SRV_ROLE_SCREENSAVER_MNG)
     e_service_scrsaver_client_set(tzsh->ec);
   else if (role == TZSH_SRV_ROLE_SCREENSAVER)
     e_service_scrsaver_client_set(tzsh->ec);
   else if (role == TZSH_SRV_ROLE_INDICATOR)
     e_mod_indicator_client_set(tzsh->ec);
   else if (role == TZSH_SRV_ROLE_CBHM)
     e_service_cbhm_client_set(tzsh->ec);
   else if (role == TZSH_SRV_ROLE_SOFTKEY)
     e_service_softkey_client_set(tzsh->ec);
   else if (role == TZSH_SRV_ROLE_MAGNIFIER)
     {
        e_magnifier_new();
        e_magnifier_owner_set(tzsh->ec);
     }
   else if (role == TZSH_SRV_ROLE_LAUNCHER)
     e_service_launcher_client_set(tzsh->ec);
}

// --------------------------------------------------------
// tizen_ws_shell common
// --------------------------------------------------------
E_API Eina_Bool
e_tzsh_extension_add(const char *name, E_Policy_Wl_Tzsh_Ext_Hook_Cb cb)
{
   E_Policy_Wl_Tzsh_Extension *tzsh_ext;

   if (_e_policy_wl_tzsh_extension_get(name))
     {
        ERR("Already exists the %s extension\n", name);
        return EINA_FALSE;
     }

   tzsh_ext = E_NEW(E_Policy_Wl_Tzsh_Extension, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tzsh_ext, EINA_FALSE);

   tzsh_ext->name = strndup(name, 512);
   tzsh_ext->cb = cb;

   polwl->tzsh_extensions = eina_list_append(polwl->tzsh_extensions, tzsh_ext);
   ELOGF("TZSH",
         "EXTENSION_ADD | name:%s | cb:%p",
         NULL,
         name, cb);

   return EINA_TRUE;
}

E_API void
e_tzsh_extension_del(const char *name)
{
   E_Policy_Wl_Tzsh_Extension *tzsh_ext;

   tzsh_ext = _e_policy_wl_tzsh_extension_get(name);
   if (!tzsh_ext)
     {
        ERR("Cannot find the %s extension\n", name);
        return;
     }

   polwl->tzsh_extensions = eina_list_remove(polwl->tzsh_extensions, tzsh_ext);
   memset(tzsh_ext, 0x0, sizeof(E_Policy_Wl_Tzsh_Extension));
   E_FREE(tzsh_ext);

   ELOGF("TZSH",
         "EXTENSION_DEL | name:%s",
         NULL,
         name);
}

// --------------------------------------------------------
// tizen_ws_shell_interface::region
// --------------------------------------------------------
static void
_tzsh_reg_cb_shell_destroy(struct wl_listener *listener, void *data)
{
   E_Policy_Wl_Tzsh_Region *tzsh_reg;

   tzsh_reg = container_of(listener, E_Policy_Wl_Tzsh_Region, destroy_listener);
   if (tzsh_reg->destroy_listener.notify)
     {
        wl_list_remove(&tzsh_reg->destroy_listener.link);
        tzsh_reg->destroy_listener.notify = NULL;
     }

   if (tzsh_reg->res_tzsh_reg)
     {
        wl_resource_destroy(tzsh_reg->res_tzsh_reg);
        tzsh_reg->res_tzsh_reg = NULL;
     }
}

static void
_tzsh_reg_iface_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzsh_reg)
{
   wl_resource_destroy(res_tzsh_reg);
}

static void
_tzsh_reg_iface_cb_add(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzsh_reg, int32_t x, int32_t y, int32_t w, int32_t h)
{
   E_Policy_Wl_Tzsh_Region *tzsh_reg;
   Eina_Tiler *src;
   int area_w = 0, area_h = 0;

   tzsh_reg = wl_resource_get_user_data(res_tzsh_reg);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_reg);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_reg->tiler);

   eina_tiler_area_size_get(tzsh_reg->tiler, &area_w, &area_h);
   src = eina_tiler_new(area_w, area_h);
   eina_tiler_tile_size_set(src, 1, 1);
   eina_tiler_rect_add(src, &(Eina_Rectangle){x, y, w, h});
   eina_tiler_union(tzsh_reg->tiler, src);
   eina_tiler_free(src);
}

static void
_tzsh_reg_iface_cb_subtract(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzsh_reg, int32_t x, int32_t y, int32_t w, int32_t h)
{
   E_Policy_Wl_Tzsh_Region *tzsh_reg;
   Eina_Tiler *src;
   int area_w = 0, area_h = 0;

   tzsh_reg = wl_resource_get_user_data(res_tzsh_reg);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_reg);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_reg->tiler);

   eina_tiler_area_size_get(tzsh_reg->tiler, &area_w, &area_h);
   src = eina_tiler_new(area_w, area_h);
   eina_tiler_tile_size_set(src, 1, 1);
   eina_tiler_rect_add(src, &(Eina_Rectangle){x, y, w, h});
   eina_tiler_subtract(tzsh_reg->tiler, src);
   eina_tiler_free(src);
}

static const struct tws_region_interface _tzsh_reg_iface =
{
   _tzsh_reg_iface_cb_destroy,
   _tzsh_reg_iface_cb_add,
   _tzsh_reg_iface_cb_subtract
};

static void
_tzsh_reg_cb_destroy(struct wl_resource *res_tzsh_reg)
{
   E_Policy_Wl_Tzsh_Region *tzsh_reg;

   tzsh_reg = wl_resource_get_user_data(res_tzsh_reg);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_reg);

   wl_list_remove(&tzsh_reg->destroy_listener.link);
   eina_tiler_free(tzsh_reg->tiler);

   E_FREE(tzsh_reg);
}

static void
_tzsh_iface_cb_reg_create(struct wl_client *client, struct wl_resource *res_tzsh, uint32_t id)
{
   E_Policy_Wl_Tzsh *tzsh;
   E_Policy_Wl_Tzsh_Region *tzsh_reg = NULL;
   Eina_Tiler *tz = NULL;
   struct wl_resource *res_tzsh_reg;
   int zw = 0, zh = 0;

   tzsh = wl_resource_get_user_data(res_tzsh);
   if (!tzsh)
     {
        wl_resource_post_error
          (res_tzsh,
           WL_DISPLAY_ERROR_INVALID_OBJECT,
           "Invalid res_tzsh's user data");
        return;
     }

   tzsh_reg = E_NEW(E_Policy_Wl_Tzsh_Region, 1);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_reg);

   e_zone_useful_geometry_get(e_zone_current_get(),
                              NULL, NULL, &zw, &zh);

   tz = eina_tiler_new(zw, zh);
   EINA_SAFETY_ON_NULL_GOTO(tz, err);
   tzsh_reg->tiler = tz;

   eina_tiler_tile_size_set(tzsh_reg->tiler, 1, 1);

   if (!(res_tzsh_reg = wl_resource_create(client,
                                           &tws_region_interface,
                                           wl_resource_get_version(res_tzsh),
                                           id)))
     {
        ERR("Could not create tws_service resource: %m");
        wl_client_post_no_memory(client);
        goto err;
     }

   wl_resource_set_implementation(res_tzsh_reg,
                                  &_tzsh_reg_iface,
                                  tzsh_reg,
                                  _tzsh_reg_cb_destroy);

   tzsh_reg->tzsh = tzsh;
   tzsh_reg->res_tzsh_reg = res_tzsh_reg;
   tzsh_reg->destroy_listener.notify = _tzsh_reg_cb_shell_destroy;

   wl_resource_add_destroy_listener(res_tzsh,
                                    &tzsh_reg->destroy_listener);
   return;

err:
   if (tzsh_reg->tiler) eina_tiler_free(tzsh_reg->tiler);
   E_FREE(tzsh_reg);
}

// --------------------------------------------------------
// tizen_ws_shell_interface::indicator
// --------------------------------------------------------
static E_Client *
_e_tzsh_indicator_find_topvisible_client(E_Zone *zone)
{
   E_Client *ec;
   Evas_Object *o;
   E_Comp_Wl_Client_Data *cdata;
   int ex, ey, ew, eh;

   o = evas_object_top_get(e_comp->evas);
   for (; o; o = evas_object_below_get(o))
     {
        ec = evas_object_data_get(o, "E_Client");

        /* check e_client and skip e_clients not intersects with zone */
        if (!ec) continue;
        if (e_object_is_del(E_OBJECT(ec))) continue;
        if (e_client_util_ignored_get(ec)) continue;
        if (ec->zone != zone) continue;
        if (!ec->frame) continue;

        if (!ec->visible) continue;
        if (ec->visibility.skip) continue;
        if ((ec->visibility.obscured != E_VISIBILITY_UNOBSCURED) &&
            (ec->visibility.obscured != E_VISIBILITY_PARTIALLY_OBSCURED) &&
            (!eina_list_data_find(e_comp->launchscrns, ec)))
          continue;

        /* if ec is subsurface, skip this */
        cdata = (E_Comp_Wl_Client_Data *)ec->comp_data;
        if (cdata && cdata->sub.data) continue;

        e_client_geometry_get(ec, &ex, &ey, &ew, &eh);
        if (!E_CONTAINS(ex, ey, ew, eh, zone->x, zone->y, zone->w, zone->h))
          continue;

        return ec;

     }

   return NULL;
}

EINTERN void
e_tzsh_indicator_srv_property_change_send(E_Client *ec, int angle)
{
   int opacity;

   if (!ec) return;
   if (!_indicator_srv_res)
     {
        ELOGF("TZ_IND", "NO indicator service", NULL);
        return;
     }

   opacity = ec->indicator.opacity_mode;

   ELOGF("TZ_IND", "SEND indicator info. angle:%d, opacity:%d", ec, angle, opacity);
   tws_service_indicator_send_property_change(_indicator_srv_res, angle, opacity);
}

EINTERN void
e_tzsh_indicator_srv_property_update(E_Client *ec)
{
   E_Client *ec_ind_owner;
   if (!_indicator_srv_res) return;

   ec_ind_owner = e_mod_indicator_owner_get();
   if (ec != ec_ind_owner) return;

   if (ec->e.state.rot.ang.next != -1)
     e_tzsh_indicator_srv_property_change_send(ec, ec->e.state.rot.ang.next);
   else
     e_tzsh_indicator_srv_property_change_send(ec, ec->e.state.rot.ang.curr);
}

EINTERN void
e_tzsh_indicator_srv_ower_win_update(E_Zone *zone)
{
   E_Client *ec = NULL;
   E_Client *ec_cur_owner = NULL;

   if (!zone) return;
   if (!_indicator_srv_res) return;

   ec_cur_owner = e_mod_indicator_owner_get();
   ec = _e_tzsh_indicator_find_topvisible_client(zone);

   if (ec != ec_cur_owner)
     {
        ELOGF("TZ_IND", "Changed OWNER. win:%zx, state:%d, opacity:%d, vtype:%d", NULL, e_client_util_win_get(ec),
              ec ? ec->indicator.state:-1, ec ? ec->indicator.opacity_mode:-1, ec ? ec->indicator.visible_type:-1);
        e_mod_indicator_owner_set(ec);

        if (ec && !ec->e.state.rot.pending_show)
          {
             ELOGF("TZ_IND", "Property Update. name:%s curr:%d, next:%d", NULL, ec->icccm.name?:"NULL",
                   ec->e.state.rot.ang.curr, ec->e.state.rot.ang.next);
             e_tzsh_indicator_srv_property_update(ec);
          }
     }
}

// --------------------------------------------------------
// tizen_ws_shell_interface::quickpanel
// --------------------------------------------------------
static void
_e_tzsh_qp_state_change_send(struct wl_resource *res_tzsh_client, int type, int value)
{
   struct wl_array states;
   E_Tzsh_QP_Event *ev;

   if (!res_tzsh_client) return;

   wl_array_init(&states);
   ev = wl_array_add(&states, sizeof(E_Tzsh_QP_Event));
   if (!ev) return;

   ev->type = type;
   ev->val = value;

   tws_quickpanel_send_state_changed(res_tzsh_client, &states);

   wl_array_release(&states);
}

E_API void
e_tzsh_qp_state_visible_update(E_Client *ec, Eina_Bool vis, E_Quickpanel_Type type)
{
   E_Policy_Wl_Tzsh_Client *tzsh_client;
   Eina_List *l;
   int val;

   if (!ec) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   EINA_LIST_FOREACH(polwl->tzsh_clients, l, tzsh_client)
     {
        /* check for type of qp */
        if (tzsh_client->qp_type != type) continue;
        if (!tzsh_client->tzsh) continue;
        if (!tzsh_client->tzsh->ec) continue;

        if (tzsh_client->tzsh->cp == ec->pixmap)
          {
             if (tzsh_client->tzsh->ec != ec)
               {
                  ELOGF("TZSH",
                        "CRI ERR!!|tzsh_cp:%8p|tzsh_ec:%8p|tzsh:%8p",
                        ec,
                        tzsh_client->tzsh->cp,
                        tzsh_client->tzsh->ec,
                        tzsh_client->tzsh);
                  continue;
               }

             val = vis ? TWS_QUICKPANEL_STATE_VALUE_VISIBLE_SHOW : TWS_QUICKPANEL_STATE_VALUE_VISIBLE_HIDE;
             _e_tzsh_qp_state_change_send(tzsh_client->res_tzsh_client,
                                          TWS_QUICKPANEL_STATE_TYPE_VISIBILITY,
                                          val);
          }
     }
}

E_API void
e_tzsh_qp_state_scrollable_update(E_Client *ec, Eina_Bool scrollable, E_Quickpanel_Type type)
{
   E_Policy_Wl_Tzsh_Client *tzsh_client;
   Eina_List *l;
   int val;

   if (!ec) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   EINA_LIST_FOREACH(polwl->tzsh_clients, l, tzsh_client)
     {
        /* check for type of qp */
        if (tzsh_client->qp_type != type) continue;
        if (!tzsh_client->tzsh) continue;
        if (!tzsh_client->tzsh->ec) continue;

        if (tzsh_client->tzsh->cp == ec->pixmap)
          {
             if (tzsh_client->tzsh->ec != ec)
               {
                  ELOGF("TZSH",
                        "CRI ERR!!|tzsh_cp:%8p|tzsh_ec:%8p|tzsh:%8p",
                        ec,
                        tzsh_client->tzsh->cp,
                        tzsh_client->tzsh->ec,
                        tzsh_client->tzsh);
                  continue;
               }

             val = scrollable ? TWS_QUICKPANEL_STATE_VALUE_SCROLLABLE_SET : TWS_QUICKPANEL_STATE_VALUE_SCROLLABLE_UNSET;
             _e_tzsh_qp_state_change_send(tzsh_client->res_tzsh_client,
                                          TWS_QUICKPANEL_STATE_TYPE_SCROLLABLE,
                                          val);
          }
     }
}

E_API void
e_tzsh_qp_state_orientation_update(E_Client *ec, int ridx, E_Quickpanel_Type type)
{
   E_Policy_Wl_Tzsh_Client *tzsh_client;
   Eina_List *l;
   int val;

   if (!ec) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   EINA_LIST_FOREACH(polwl->tzsh_clients, l, tzsh_client)
     {
        /* check for type of qp */
        if (tzsh_client->qp_type != type) continue;
        if (!tzsh_client->tzsh) continue;
        if (!tzsh_client->tzsh->ec) continue;

        if (tzsh_client->tzsh->cp == ec->pixmap)
          {
             if (tzsh_client->tzsh->ec != ec)
               {
                  ELOGF("TZSH",
                        "CRI ERR!!|tzsh_cp:%8p|tzsh_ec:%8p|tzsh:%8p",
                        ec,
                        tzsh_client->tzsh->cp,
                        tzsh_client->tzsh->ec,
                        tzsh_client->tzsh);
                  continue;
               }

             val = TWS_QUICKPANEL_STATE_VALUE_ORIENTATION_0 + ridx;
             _e_tzsh_qp_state_change_send(tzsh_client->res_tzsh_client,
                                          TWS_QUICKPANEL_STATE_TYPE_ORIENTATION,
                                          val);
          }
     }
}

static void
_tzsh_qp_iface_cb_release(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzsh_qp)
{
   wl_resource_destroy(res_tzsh_qp);
}

static void
_tzsh_qp_iface_cb_show(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzsh_qp)
{
   E_Policy_Wl_Tzsh_Client *tzsh_client;
   E_Client *ec;

   tzsh_client = wl_resource_get_user_data(res_tzsh_qp);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client->tzsh);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client->tzsh->ec);

   /* unexpected case: this client doesn't have specific quickpanel type */
   EINA_SAFETY_ON_TRUE_RETURN(tzsh_client->qp_type == E_QUICKPANEL_TYPE_UNKNOWN);

   ec = tzsh_client->tzsh->ec;

   if (!eina_list_data_find(polwl->tzsh_clients, tzsh_client))
     return;

   e_qp_client_show(ec, tzsh_client->qp_type);
}

static void
_tzsh_qp_iface_cb_hide(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzsh_qp)
{
   E_Policy_Wl_Tzsh_Client *tzsh_client;
   E_Client *ec;

   tzsh_client = wl_resource_get_user_data(res_tzsh_qp);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client->tzsh);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client->tzsh->ec);

   /* unexpected case: this client doesn't have specific quickpanel type */
   EINA_SAFETY_ON_TRUE_RETURN(tzsh_client->qp_type == E_QUICKPANEL_TYPE_UNKNOWN);

   ec = tzsh_client->tzsh->ec;

   if (!eina_list_data_find(polwl->tzsh_clients, tzsh_client))
     return;

   e_qp_client_hide(ec, tzsh_client->qp_type);
}

static void
_tzsh_qp_iface_cb_enable(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzsh_qp)
{
   E_Policy_Wl_Tzsh_Client *tzsh_client;
   E_Client *ec;

   tzsh_client = wl_resource_get_user_data(res_tzsh_qp);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client->tzsh);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client->tzsh->ec);

   /* unexpected case: this client doesn't have specific quickpanel type */
   EINA_SAFETY_ON_TRUE_RETURN(tzsh_client->qp_type == E_QUICKPANEL_TYPE_UNKNOWN);

   ec = tzsh_client->tzsh->ec;

   if (!eina_list_data_find(polwl->tzsh_clients, tzsh_client))
     return;

   e_qp_client_scrollable_set(ec, tzsh_client->qp_type, EINA_TRUE);
}

static void
_tzsh_qp_iface_cb_disable(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzsh_qp)
{
   E_Policy_Wl_Tzsh_Client *tzsh_client;
   E_Client *ec;

   tzsh_client = wl_resource_get_user_data(res_tzsh_qp);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client->tzsh);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client->tzsh->ec);

   /* unexpected case: this client doesn't have specific quickpanel type */
   EINA_SAFETY_ON_TRUE_RETURN(tzsh_client->qp_type == E_QUICKPANEL_TYPE_UNKNOWN);

   ec = tzsh_client->tzsh->ec;

   if (!eina_list_data_find(polwl->tzsh_clients, tzsh_client))
     return;

   e_qp_client_scrollable_set(ec, tzsh_client->qp_type, EINA_FALSE);
}

static void
_tzsh_qp_iface_cb_state_get(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzsh_qp, int32_t type)
{
   E_Policy_Wl_Tzsh_Client *tzsh_client;
   E_Client *ec;
   Eina_Bool vis, scrollable;
   int ridx;
   int val = TWS_QUICKPANEL_STATE_VALUE_UNKNOWN;

   tzsh_client = wl_resource_get_user_data(res_tzsh_qp);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client->tzsh);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client->tzsh->ec);

   /* unexpected case: this client doesn't have specific quickpanel type */
   EINA_SAFETY_ON_TRUE_RETURN(tzsh_client->qp_type == E_QUICKPANEL_TYPE_UNKNOWN);

   ec = tzsh_client->tzsh->ec;

   if (!eina_list_data_find(polwl->tzsh_clients, tzsh_client))
     return;

   switch (type)
     {
      case TWS_QUICKPANEL_STATE_TYPE_VISIBILITY:
        val = TWS_QUICKPANEL_STATE_VALUE_VISIBLE_HIDE;
        vis = e_qp_visible_get(ec, tzsh_client->qp_type);
        if (vis) val = TWS_QUICKPANEL_STATE_VALUE_VISIBLE_SHOW;
        break;
      case TWS_QUICKPANEL_STATE_TYPE_SCROLLABLE:
        val = TWS_QUICKPANEL_STATE_VALUE_SCROLLABLE_UNSET;
        scrollable = e_qp_client_scrollable_get(ec, tzsh_client->qp_type);
        if (scrollable) val = TWS_QUICKPANEL_STATE_VALUE_SCROLLABLE_SET;
        break;
      case TWS_QUICKPANEL_STATE_TYPE_ORIENTATION:
        ridx = e_qp_orientation_get(ec, tzsh_client->qp_type);
        val = TWS_QUICKPANEL_STATE_VALUE_ORIENTATION_0 + ridx;
        break;
      default:
        break;
     }

   tws_quickpanel_send_state_get_done(res_tzsh_qp, type, val, 0);
}

static void
_tzsh_qp_iface_cb_type_set(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzsh_qp, uint32_t type)
{
   E_Policy_Wl_Tzsh_Client *tzsh_client;
   E_Client *ec;

   tzsh_client = wl_resource_get_user_data(res_tzsh_qp);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client->tzsh);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client->tzsh->ec);

   /* unexpected case: this client has already specific quickpanel type */
   EINA_SAFETY_ON_FALSE_RETURN(tzsh_client->qp_type == E_QUICKPANEL_TYPE_UNKNOWN);

   tzsh_client->qp_type = (E_Quickpanel_Type)type;
   ec = tzsh_client->tzsh->ec;

   if (!eina_list_data_find(polwl->tzsh_clients, tzsh_client))
     return;

   /* Since various types of qp are supported, one ec can be used for handler for
    * two or more qp types. So e_qp_client_add function is called at the callback
    * of qp_type_set, because it is easy to add the E_QP_Client instance after setting
    * of qp type is completed.
    */
   e_qp_client_add(ec, type);
}

static const struct tws_quickpanel_interface _tzsh_qp_iface =
{
   _tzsh_qp_iface_cb_release,
   _tzsh_qp_iface_cb_show,
   _tzsh_qp_iface_cb_hide,
   _tzsh_qp_iface_cb_enable,
   _tzsh_qp_iface_cb_disable,
   _tzsh_qp_iface_cb_state_get,
   _tzsh_qp_iface_cb_type_set
};

static void
_tzsh_cb_qp_destroy(struct wl_resource *res_tzsh_qp)
{
   E_Policy_Wl_Tzsh_Client *tzsh_client;

   tzsh_client = wl_resource_get_user_data(res_tzsh_qp);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client);

   _e_policy_wl_tzsh_client_del(tzsh_client);
}

static void
_tzsh_iface_cb_qp_get(struct wl_client *client, struct wl_resource *res_tzsh, uint32_t id, uint32_t surf_id)
{
   E_Policy_Wl_Tzsh *tzsh;
   E_Policy_Wl_Tzsh_Client *tzsh_client;
   struct wl_resource *res_tzsh_qp;
   E_Client *ec;
   E_Pixmap *cp;

   tzsh = wl_resource_get_user_data(res_tzsh);
   if (!tzsh)
     {
        wl_resource_post_error
          (res_tzsh,
           WL_DISPLAY_ERROR_INVALID_OBJECT,
           "Invalid res_tzsh's user data");
        return;
     }

   cp = _e_policy_wl_e_pixmap_get_from_id(client, surf_id);
   if (!cp)
     {
        wl_resource_post_error
          (res_tzsh,
           WL_DISPLAY_ERROR_INVALID_OBJECT,
           "Invalid surface id");
        return;
     }

   ec = e_pixmap_client_get(cp);
   if (ec)
     {
        if (!_e_policy_wl_e_client_is_valid(ec))
          {
             wl_resource_post_error
               (res_tzsh,
                WL_DISPLAY_ERROR_INVALID_OBJECT,
                "Invalid surface id");
             return;
          }
     }

   res_tzsh_qp = wl_resource_create(client,
                                    &tws_quickpanel_interface,
                                    wl_resource_get_version(res_tzsh),
                                    id);
   if (!res_tzsh_qp)
     {
        ERR("Could not create tws_quickpanel resource: %m");
        wl_client_post_no_memory(client);
        return;
     }

   _e_policy_wl_tzsh_data_set(tzsh, TZSH_TYPE_CLIENT, cp, ec);

   tzsh_client = _e_policy_wl_tzsh_client_add(tzsh, res_tzsh_qp);
   if (!tzsh_client)
     {
        ERR("Could not create tzsh_client");
        wl_client_post_no_memory(client);
        return;
     }

   tzsh_client->qp_client = EINA_TRUE;
   tzsh_client->qp_type = E_QUICKPANEL_TYPE_UNKNOWN;

   /* Since various types of qp are supported, one ec can be used for handler for
    * two or more qp types. So e_qp_client_add function is called at the callback
    * of qp_type_set, because it is easy to add the E_QP_Client instance after setting
    * of qp type is completed.
    */
   //e_qp_client_add(tzsh->ec);

   wl_resource_set_implementation(res_tzsh_qp,
                                  &_tzsh_qp_iface,
                                  tzsh_client,
                                  _tzsh_cb_qp_destroy);
}

// --------------------------------------------------------
// tizen_ws_shell_interface::tvservice
// --------------------------------------------------------
static void
_tzsh_tvsrv_iface_cb_release(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzsh_tvsrv)
{
   wl_resource_destroy(res_tzsh_tvsrv);
}

static void
_tzsh_tvsrv_iface_cb_bind(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzsh_tvsrv)
{
   E_Policy_Wl_Tzsh_Client *tzsh_client;

   tzsh_client = wl_resource_get_user_data(res_tzsh_tvsrv);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client);

   if (!eina_list_data_find(polwl->tzsh_clients, tzsh_client))
     return;

   polwl->tvsrv_bind_list = eina_list_append(polwl->tvsrv_bind_list, tzsh_client);

   _e_policy_wl_tzsh_srv_tvsrv_bind_update();
}

static void
_tzsh_tvsrv_iface_cb_unbind(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzsh_tvsrv)
{
   E_Policy_Wl_Tzsh_Client *tzsh_client;

   tzsh_client = wl_resource_get_user_data(res_tzsh_tvsrv);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client);

   if (!eina_list_data_find(polwl->tzsh_clients, tzsh_client))
     return;

   polwl->tvsrv_bind_list = eina_list_remove(polwl->tvsrv_bind_list, tzsh_client);

   _e_policy_wl_tzsh_srv_tvsrv_bind_update();
}

static const struct tws_tvsrv_interface _tzsh_tvsrv_iface =
{
   _tzsh_tvsrv_iface_cb_release,
   _tzsh_tvsrv_iface_cb_bind,
   _tzsh_tvsrv_iface_cb_unbind
};

static void
_tzsh_cb_tvsrv_destroy(struct wl_resource *res_tzsh_tvsrv)
{
   E_Policy_Wl_Tzsh_Client *tzsh_client;

   tzsh_client = wl_resource_get_user_data(res_tzsh_tvsrv);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client);

   if (!eina_list_data_find(polwl->tzsh_clients, tzsh_client))
     return;

   polwl->tvsrv_bind_list = eina_list_remove(polwl->tvsrv_bind_list, tzsh_client);

   _e_policy_wl_tzsh_srv_tvsrv_bind_update();
   _e_policy_wl_tzsh_client_del(tzsh_client);
}

static void
_tzsh_iface_cb_tvsrv_get(struct wl_client *client, struct wl_resource *res_tzsh, uint32_t id, uint32_t surf_id)
{
   E_Policy_Wl_Tzsh *tzsh;
   E_Policy_Wl_Tzsh_Client *tzsh_client;
   struct wl_resource *res_tzsh_tvsrv;
   E_Pixmap *cp;
   E_Client *ec;

   tzsh = wl_resource_get_user_data(res_tzsh);
   if (!tzsh)
     {
        wl_resource_post_error
          (res_tzsh,
           WL_DISPLAY_ERROR_INVALID_OBJECT,
           "Invalid res_tzsh's user data");
        return;
     }

   cp = _e_policy_wl_e_pixmap_get_from_id(client, surf_id);
   if (!cp)
     {
        wl_resource_post_error
          (res_tzsh,
           WL_DISPLAY_ERROR_INVALID_OBJECT,
           "Invalid surface id");
        return;
     }

   ec = e_pixmap_client_get(cp);
   if (ec)
     {
        if (!_e_policy_wl_e_client_is_valid(ec))
          {
             wl_resource_post_error
               (res_tzsh,
                WL_DISPLAY_ERROR_INVALID_OBJECT,
                "Invalid surface id");
             return;
          }
     }

   res_tzsh_tvsrv = wl_resource_create(client,
                                       &tws_tvsrv_interface,
                                       wl_resource_get_version(res_tzsh),
                                       id);
   if (!res_tzsh_tvsrv)
     {
        ERR("Could not create tws_tvsrv resource: %m");
        wl_client_post_no_memory(client);
        return;
     }

   _e_policy_wl_tzsh_data_set(tzsh, TZSH_TYPE_CLIENT, cp, ec);

   tzsh_client = _e_policy_wl_tzsh_client_add(tzsh, res_tzsh_tvsrv);
   if (!tzsh_client)
     {
        ERR("Could not create tzsh_client");
        wl_client_post_no_memory(client);
        wl_resource_destroy(res_tzsh_tvsrv);
        return;
     }

   wl_resource_set_implementation(res_tzsh_tvsrv,
                                  &_tzsh_tvsrv_iface,
                                  tzsh_client,
                                  _tzsh_cb_tvsrv_destroy);
}

static void _tzsh_iface_cb_extension_get(struct wl_client *client, struct wl_resource *res_tzsh, uint32_t id, const char *name)
{
   E_Policy_Wl_Tzsh *tzsh;
   E_Policy_Wl_Tzsh_Extension *tzsh_ext;
   struct wl_resource *res_ext;

   tzsh = wl_resource_get_user_data(res_tzsh);
   if (!tzsh)
     {
        wl_resource_post_error
          (res_tzsh,
           WL_DISPLAY_ERROR_INVALID_OBJECT,
           "Invalid res_tzsh's user data");
        return;
     }

   tzsh_ext = _e_policy_wl_tzsh_extension_get(name);
   if (!tzsh_ext)
      {
         ERR("Could not find tzsh_extension(%s)", name);
         wl_resource_post_error
           (res_tzsh,
            WL_DISPLAY_ERROR_INVALID_OBJECT,
            "unregistered ext:%s", name);
      }
   else
      {
         res_ext = tzsh_ext->cb(client, res_tzsh, id);
         if (!res_ext)
            {
               ERR("Could not create extension(%s) resource", name);
               wl_resource_post_error
                 (res_tzsh,
                  WL_DISPLAY_ERROR_INVALID_OBJECT,
                  "Unknown error:%s", name);
            }
      }
}

// --------------------------------------------------------
// tizen_ws_shell_interface::softkey
// --------------------------------------------------------
static void
_tzsh_softkey_iface_cb_release(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzsh_softkey)
{
   wl_resource_destroy(res_tzsh_softkey);
}

static void
_tzsh_softkey_iface_cb_support_check(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzsh_softkey)
{
   E_Policy_Wl_Tzsh_Client *tzsh_client;
   int support;

   ELOGF("TZ_SOFTKEY", "Request to Check supporting softkey", NULL);

   tzsh_client = wl_resource_get_user_data(res_tzsh_softkey);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client->tzsh);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client->tzsh->ec);

   if (!eina_list_data_find(polwl->tzsh_clients, tzsh_client))
     return;

   if (e_config->use_softkey || e_config->use_softkey_service)
     support = 1;
   else
     support = 0;

   ELOGF("TZ_SOFTKEY", "Send SUPPORT_CHECK_DONE. support:%d", NULL, support);
   tws_softkey_send_support_check_done(res_tzsh_softkey, support);
}

static void
_tzsh_softkey_iface_cb_show(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzsh_softkey)
{
   E_Policy_Wl_Tzsh_Client *tzsh_client;

   ELOGF("TZ_SOFTKEY", "Request to Show softkey", NULL);

   if (!e_config->use_softkey && !e_config->use_softkey_service)
     return;

   tzsh_client = wl_resource_get_user_data(res_tzsh_softkey);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client->tzsh);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client->tzsh->ec);

   if (!eina_list_data_find(polwl->tzsh_clients, tzsh_client))
     return;

   if (e_config->use_softkey)
     {
        E_Policy_Softkey *softkey;
        softkey = e_policy_softkey_get(tzsh_client->tzsh->ec->zone);
        if (softkey)
          {
             ELOGF("TZ_SOFTKEY", "SHOW softkey", NULL);
             e_policy_softkey_show(softkey);
          }
     }

   if (e_config->use_softkey_service)
     {
        E_Service_Softkey *softkey;
        softkey = e_service_softkey_get(tzsh_client->tzsh->ec->zone);
        if (softkey)
          {
             ELOGF("TZ_SOFTKEY", "Request to SHOW softkey. (service:%p)", NULL, softkey);
             e_service_softkey_visible_set(softkey, 1);
          }
     }
}

static void
_tzsh_softkey_iface_cb_hide(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzsh_softkey)
{
   E_Policy_Wl_Tzsh_Client *tzsh_client;

   ELOGF("TZ_SOFTKEY", "Request to Hide softkey", NULL);

   if (!e_config->use_softkey && !e_config->use_softkey_service)
     return;

   tzsh_client = wl_resource_get_user_data(res_tzsh_softkey);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client->tzsh);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client->tzsh->ec);

   if (!eina_list_data_find(polwl->tzsh_clients, tzsh_client))
     return;

   if (e_config->use_softkey)
     {
        E_Policy_Softkey *softkey;
        softkey = e_policy_softkey_get(tzsh_client->tzsh->ec->zone);
        if (softkey)
          {
             ELOGF("TZ_SOFTKEY", "HIDE softkey", NULL);
             e_policy_softkey_hide(softkey);
          }
     }

   if (e_config->use_softkey_service)
     {
        E_Service_Softkey *softkey;
        softkey = e_service_softkey_get(tzsh_client->tzsh->ec->zone);
        if (softkey)
          {
             ELOGF("TZ_SOFTKEY", "Request to HIDE softkey. (service:%p)", NULL, softkey);
             e_service_softkey_visible_set(softkey, 0);
          }
     }
}

static void
_tzsh_softkey_iface_cb_state_set(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzsh_softkey, int32_t type, int32_t val)
{
   E_Policy_Wl_Tzsh_Client *tzsh_client;
   E_Policy_Softkey_Expand expand;
   E_Policy_Softkey_Opacity opacity;

   ELOGF("TZ_SOFTKEY", "Request to Set state (tz_type:%d, tz_val:%d)", NULL, type, val);

   if (!e_config->use_softkey && !e_config->use_softkey_service)
     return;

   tzsh_client = wl_resource_get_user_data(res_tzsh_softkey);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client->tzsh);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client->tzsh->ec);

   if (!eina_list_data_find(polwl->tzsh_clients, tzsh_client))
     return;

   if (e_config->use_softkey)
     {
        E_Policy_Softkey *softkey;
        softkey = e_policy_softkey_get(tzsh_client->tzsh->ec->zone);
        if (!softkey) return;

        switch (type)
          {
           case TWS_SOFTKEY_STATE_EXPAND:
              if (val == TWS_SOFTKEY_STATE_EXPAND_ON)
                expand = E_POLICY_SOFTKEY_EXPAND_ON;
              else
                expand = E_POLICY_SOFTKEY_EXPAND_OFF;

              ELOGF("TZ_SOFTKEY", "Set EXPAND state to %d", NULL, expand);
              e_policy_softkey_expand_set(softkey, expand);
              break;

           case TWS_SOFTKEY_STATE_OPACITY:
              if (val == TWS_SOFTKEY_STATE_OPACITY_TRANSPARENT)
                opacity = E_POLICY_SOFTKEY_OPACITY_TRANSPARENT;
              else
                opacity = E_POLICY_SOFTKEY_OPACITY_OPAQUE;

              ELOGF("TZ_SOFTKEY", "Set OPACITY state to %d", NULL, opacity);
              e_policy_softkey_opacity_set(softkey, opacity);
              break;

           default:
              break;
          }
     }

   if (e_config->use_softkey_service)
     {
        E_Service_Softkey *softkey;

        softkey = e_service_softkey_get(tzsh_client->tzsh->ec->zone);
        if (!softkey) return;

        switch (type)
          {
           case TWS_SOFTKEY_STATE_EXPAND:
              if (val == TWS_SOFTKEY_STATE_EXPAND_ON)
                expand = E_POLICY_SOFTKEY_EXPAND_ON;
              else
                expand = E_POLICY_SOFTKEY_EXPAND_OFF;

              ELOGF("TZ_SOFTKEY", "Request to Change EXPAND state to %d. (service:%p)", NULL, expand, softkey);
              e_service_softkey_expand_set(softkey, expand);
              break;

           case TWS_SOFTKEY_STATE_OPACITY:
              if (val == TWS_SOFTKEY_STATE_OPACITY_TRANSPARENT)
                opacity = E_POLICY_SOFTKEY_OPACITY_TRANSPARENT;
              else
                opacity = E_POLICY_SOFTKEY_OPACITY_OPAQUE;

              ELOGF("TZ_SOFTKEY", "Request to Change OPACITY state to %d. (service:%p)", NULL, opacity, softkey);
              e_service_softkey_opacity_set(softkey, opacity);
              break;

           default:
              break;
          }
     }
}

static void
_tzsh_softkey_iface_cb_state_get(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzsh_softkey, int32_t type)
{
   E_Policy_Wl_Tzsh_Client *tzsh_client;
   E_Policy_Softkey_Expand expand;
   E_Policy_Softkey_Opacity opacity;
   int visible;
   int val;

   ELOGF("TZ_SOFTKEY", "Request to Get state (tz_type:%d)", NULL, type);

   if (!e_config->use_softkey && !e_config->use_softkey_service)
     return;

   tzsh_client = wl_resource_get_user_data(res_tzsh_softkey);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client->tzsh);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client->tzsh->ec);

   if (!eina_list_data_find(polwl->tzsh_clients, tzsh_client))
     return;

   if (e_config->use_softkey)
     {
        E_Policy_Softkey *softkey;
        softkey = e_policy_softkey_get(tzsh_client->tzsh->ec->zone);
        if (!softkey) return;

        switch (type)
          {
           case TWS_SOFTKEY_STATE_VISIBLE:
              visible = e_policy_softkey_visible_get(softkey);
              if (visible)
                val = TWS_SOFTKEY_STATE_VISIBLE_SHOW;
              else
                val = TWS_SOFTKEY_STATE_VISIBLE_HIDE;

              ELOGF("TZ_SOFTKEY", "Send current VISIBLE state: %d (tz_val:%d)", NULL, visible, val);
              tws_softkey_send_state_get_done(res_tzsh_softkey, type, val, 0);
              break;

           case TWS_SOFTKEY_STATE_EXPAND:
              e_policy_softkey_expand_get(softkey, &expand);
              if (expand == E_POLICY_SOFTKEY_EXPAND_ON)
                val = TWS_SOFTKEY_STATE_EXPAND_ON;
              else
                val = TWS_SOFTKEY_STATE_EXPAND_OFF;

              ELOGF("TZ_SOFTKEY", "Send current EXPAND state: %d (tz_val:%d)", NULL, expand, val);
              tws_softkey_send_state_get_done(res_tzsh_softkey, type, val, 0);
              break;

           case TWS_SOFTKEY_STATE_OPACITY:
              e_policy_softkey_opacity_get(softkey, &opacity);
              if (opacity == E_POLICY_SOFTKEY_OPACITY_TRANSPARENT)
                val = TWS_SOFTKEY_STATE_OPACITY_TRANSPARENT;
              else
                val = TWS_SOFTKEY_STATE_OPACITY_OPAQUE;

              ELOGF("TZ_SOFTKEY", "Send current OPACITY state: %d (tz_val:%d)", NULL, opacity, val);
              tws_softkey_send_state_get_done(res_tzsh_softkey, type, val, 0);
              break;

           default:
              break;
          }
     }

   if (e_config->use_softkey_service)
     {
        E_Service_Softkey *softkey;
        softkey = e_service_softkey_get(tzsh_client->tzsh->ec->zone);
        if (!softkey) return;

        switch (type)
          {
           case TWS_SOFTKEY_STATE_VISIBLE:
              visible = e_service_softkey_visible_get(softkey);
              if (visible)
                val = TWS_SOFTKEY_STATE_VISIBLE_SHOW;
              else
                val = TWS_SOFTKEY_STATE_VISIBLE_HIDE;

              ELOGF("TZ_SOFTKEY", "Send service's current VISIBLE state: %d (tz_val:%d)", NULL, visible, val);
              tws_softkey_send_state_get_done(res_tzsh_softkey, type, val, 0);
              break;

           case TWS_SOFTKEY_STATE_EXPAND:
              e_service_softkey_expand_get(softkey, &expand);
              if (expand == E_POLICY_SOFTKEY_EXPAND_ON)
                val = TWS_SOFTKEY_STATE_EXPAND_ON;
              else
                val = TWS_SOFTKEY_STATE_EXPAND_OFF;

              ELOGF("TZ_SOFTKEY", "Send service's current EXPAND state: %d (tz_val:%d)", NULL, expand, val);
              tws_softkey_send_state_get_done(res_tzsh_softkey, type, val, 0);
              break;

           case TWS_SOFTKEY_STATE_OPACITY:
              e_service_softkey_opacity_get(softkey, &opacity);
              if (opacity == E_POLICY_SOFTKEY_OPACITY_TRANSPARENT)
                val = TWS_SOFTKEY_STATE_OPACITY_TRANSPARENT;
              else
                val = TWS_SOFTKEY_STATE_OPACITY_OPAQUE;

              ELOGF("TZ_SOFTKEY", "Send service's current OPACITY state: %d (tz_val:%d)", NULL, opacity, val);
              tws_softkey_send_state_get_done(res_tzsh_softkey, type, val, 0);
              break;

           default:
              break;
          }
     }
}

static const struct tws_softkey_interface _tzsh_softkey_iface =
{
   _tzsh_softkey_iface_cb_release,
   _tzsh_softkey_iface_cb_support_check,
   _tzsh_softkey_iface_cb_show,
   _tzsh_softkey_iface_cb_hide,
   _tzsh_softkey_iface_cb_state_set,
   _tzsh_softkey_iface_cb_state_get
};

static void
_tzsh_cb_softkey_destroy(struct wl_resource *res_tzsh_softkey)
{
   E_Policy_Wl_Tzsh_Client *tzsh_client;

   tzsh_client = wl_resource_get_user_data(res_tzsh_softkey);
   EINA_SAFETY_ON_NULL_RETURN(tzsh_client);

   _e_policy_wl_tzsh_client_del(tzsh_client);
}


static void
_tzsh_iface_cb_softkey_get(struct wl_client *client, struct wl_resource *res_tzsh, uint32_t id, uint32_t surf_id)
{
   E_Policy_Wl_Tzsh *tzsh;
   E_Policy_Wl_Tzsh_Client *tzsh_client;
   struct wl_resource *res_tzsh_softkey;
   E_Client *ec;
   E_Pixmap *cp;
   pid_t pid;
   uid_t uid;

   tzsh = wl_resource_get_user_data(res_tzsh);
   if (!tzsh)
     {
        wl_resource_post_error
           (res_tzsh,
            WL_DISPLAY_ERROR_INVALID_OBJECT,
            "Invalid res_tzsh's user data");
        return;
     }

   wl_client_get_credentials(client, &pid, &uid, NULL);
   if (!e_security_privilege_check(pid, uid, E_PRIVILEGE_SOFTKEY))
     {
        ERR("Could not get privilege of resource: %m");
        tizen_ws_shell_send_error(tzsh->res_tzsh, TIZEN_WS_SHELL_ERROR_PERMISSION_DENIED);
        return;
     }
   else
     tizen_ws_shell_send_error(tzsh->res_tzsh, TIZEN_WS_SHELL_ERROR_NONE);

   cp = _e_policy_wl_e_pixmap_get_from_id(client, surf_id);
   if (!cp)
     {
        wl_resource_post_error
           (res_tzsh,
            WL_DISPLAY_ERROR_INVALID_OBJECT,
            "Invalid surface id");
        return;
     }

   ec = e_pixmap_client_get(cp);
   if (ec)
     {
        if (!_e_policy_wl_e_client_is_valid(ec))
          {
             wl_resource_post_error
                (res_tzsh,
                 WL_DISPLAY_ERROR_INVALID_OBJECT,
                 "Invalid surface id");
             return;
          }
     }

   res_tzsh_softkey = wl_resource_create(client,
                                         &tws_softkey_interface,
                                         wl_resource_get_version(res_tzsh),
                                         id);
   if (!res_tzsh_softkey)
     {
        ERR("Could not create tws_softkey resource: %m");
        wl_client_post_no_memory(client);
        return;
     }

   _e_policy_wl_tzsh_data_set(tzsh, TZSH_TYPE_CLIENT, cp, ec);

   tzsh_client = _e_policy_wl_tzsh_client_add(tzsh, res_tzsh_softkey);
   if (!tzsh_client)
     {
        ERR("Could not create tzsh_client");
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(res_tzsh_softkey,
                                  &_tzsh_softkey_iface,
                                  tzsh_client,
                                  _tzsh_cb_softkey_destroy);
}


// --------------------------------------------------------
// tizen_ws_shell_interface
// --------------------------------------------------------
static void
_tzsh_iface_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzsh)
{
   wl_resource_destroy(res_tzsh);
}

static const struct tizen_ws_shell_interface _tzsh_iface =
{
   _tzsh_iface_cb_destroy,
   _tzsh_iface_cb_srv_create,
   _tzsh_iface_cb_reg_create,
   _tzsh_iface_cb_qp_get,
   _tzsh_iface_cb_tvsrv_get,
   _tzsh_iface_cb_extension_get,
   _tzsh_iface_cb_softkey_get,
};

static void
_tzsh_cb_unbind(struct wl_resource *res_tzsh)
{
   E_Policy_Wl_Tzsh *tzsh;

   tzsh = wl_resource_get_user_data(res_tzsh);
   EINA_SAFETY_ON_NULL_RETURN(tzsh);

   _e_policy_wl_tzsh_del(tzsh);
}

static void
_tzsh_cb_bind(struct wl_client *client, void *data EINA_UNUSED, uint32_t ver, uint32_t id)
{
   E_Policy_Wl_Tzsh *tzsh;
   struct wl_resource *res_tzsh;

   EINA_SAFETY_ON_NULL_GOTO(polwl, err);

   res_tzsh = wl_resource_create(client,
                                 &tizen_ws_shell_interface,
                                 ver,
                                 id);
   EINA_SAFETY_ON_NULL_GOTO(res_tzsh, err);

   tzsh = _e_policy_wl_tzsh_add(res_tzsh);
   EINA_SAFETY_ON_NULL_GOTO(tzsh, err);

   wl_resource_set_implementation(res_tzsh,
                                  &_tzsh_iface,
                                  tzsh,
                                  _tzsh_cb_unbind);

   _e_policy_wl_tzsh_registered_srv_send(tzsh);
   return;

err:
   ERR("Could not create tizen_ws_shell_interface res: %m");
   wl_client_post_no_memory(client);
}

// --------------------------------------------------------
// tizen_launch_effect_interface
// --------------------------------------------------------
static void
_launch_effect_hide(uint32_t pid)
{
   Eina_List *l, *ll;
   E_Policy_Wl_Tzlaunch_Effect *tzlaunch_effect;
   E_Policy_Wl_Tzlaunch_Splash *tzlaunch_splash;

   if(pid <= 0) return;

   EINA_LIST_FOREACH(polwl->tzlaunch_effect, l, tzlaunch_effect)
     {
        EINA_LIST_FOREACH(tzlaunch_effect->splash_list, ll, tzlaunch_splash)
           if (tzlaunch_splash->pid == pid)
             {
                _launch_splash_off(tzlaunch_splash);
             }
     }
}

static void
_launch_effect_client_del(E_Client *ec)
{
   Eina_List *l, *ll;
   E_Policy_Wl_Tzlaunch_Effect *tzlaunch_effect;
   E_Policy_Wl_Tzlaunch_Splash *tzlaunch_splash;

   EINA_LIST_FOREACH(polwl->tzlaunch_effect, l, tzlaunch_effect)
     {
        EINA_LIST_FOREACH(tzlaunch_effect->splash_list, ll, tzlaunch_splash)
           if (tzlaunch_splash->ec == ec)
             {
                _launch_splash_off(tzlaunch_splash);
             }
     }
}

static void
_launchscreen_splash_cb_indicator_resized(Ecore_Evas *ee)
{
   Evas_Coord_Size size = {0, 0};
   Evas_Object *indicator_obj;
   E_Policy_Wl_Tzlaunch_Splash *tzlaunch_splash;

   tzlaunch_splash = ecore_evas_data_get(ee, "tzlaunch_splash");
   if (!tzlaunch_splash) return;

   indicator_obj = tzlaunch_splash->indicator_obj;

   ecore_evas_geometry_get(ee, NULL, NULL, &(size.w), &(size.h));
   ELOGF("TZPOL", "Launchscreen indicator_obj resized(%d x %d)",
         NULL,
         size.w, size.h);
   evas_object_size_hint_min_set(indicator_obj, size.w, size.h);
   evas_object_size_hint_max_set(indicator_obj, size.w, size.h);
   e_comp_object_indicator_size_set(tzlaunch_splash->ec->frame, size.w, size.h);
}

static void
_launchscreen_splash_cb_del(void *data, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   E_Policy_Wl_Tzlaunch_Splash *tzlaunch_splash = data;

   if ((tzlaunch_splash) && (tzlaunch_splash->obj == obj))
     tzlaunch_splash->obj = NULL;
}

static void
_launchscreen_splash_cb_hide(void *data, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   E_Policy_Wl_Tzlaunch_Splash *tzlaunch_splash = data;

   if ((tzlaunch_splash) && (tzlaunch_splash->obj == obj))
     _launch_splash_off(tzlaunch_splash);
}

static void
_launch_splash_off(E_Policy_Wl_Tzlaunch_Splash *tzlaunch_splash)
{
   E_Client *ec = NULL;
   Evas_Object *obj = NULL;

   if (!tzlaunch_splash->valid) return;
   if (!tzlaunch_splash->ec) return;

   ec = tzlaunch_splash->ec;
   obj = tzlaunch_splash->obj;

   tzlaunch_splash->obj = NULL;
   tzlaunch_splash->ec = NULL;
   tzlaunch_splash->valid = EINA_FALSE;
   if (tzlaunch_splash->timeout) ecore_timer_del(tzlaunch_splash->timeout);
   tzlaunch_splash->timeout = NULL;

   ELOGF("TZPOL",
         "Launchscreen hide | pid %d, replaced:%d, tzlaunch_pixmap:%p, ec_pixmap:%p",
         ec, tzlaunch_splash->pid, tzlaunch_splash->replaced, tzlaunch_splash->ep, ec->pixmap);

   if (tzlaunch_splash->indicator_obj)
     {
        e_comp_object_indicator_unswallow(ec->frame, tzlaunch_splash->indicator_obj);
        evas_object_del(tzlaunch_splash->indicator_obj);
        evas_object_unref(tzlaunch_splash->indicator_obj);
        tzlaunch_splash->indicator_obj = NULL;
     }

   if ((ec->pixmap) &&
       (ec->pixmap == tzlaunch_splash->ep))
     {
        /* case 1: Surface for this pid is not created until timeout or
         * launchscreen resource is destroied.
         */
        if (ec->visible)
          {
             ec->visible = EINA_FALSE;
             evas_object_hide(ec->frame);
             ec->ignored = EINA_TRUE;
          }

        e_comp->launchscrns = eina_list_remove(e_comp->launchscrns, ec);

        e_pixmap_win_id_del(tzlaunch_splash->ep);
        e_object_del(E_OBJECT(ec));
        ec = NULL;
     }

   if (ec)
     {
        if (!e_util_strcmp("wl_pointer-cursor", ec->icccm.window_role))
          {
             // if Launchscreen is replaced to cursor, than hide
             e_comp_object_content_unset(ec->frame);
             ec->visible = EINA_FALSE;
             evas_object_hide(ec->frame);
             ec->ignored = EINA_TRUE;
          }
        else if (!tzlaunch_splash->replaced)
          {
             if (ec->focused)
               e_comp_wl_feed_focus_in(ec);

             /* to send launch,done event to launchscreen client */
             if (!e_object_is_del(E_OBJECT(ec)))
               e_comp_object_signal_emit(ec->frame, "e,action,launch,done", "e");
          }

        e_comp->launchscrns = eina_list_remove(e_comp->launchscrns, ec);
     }

   if (obj)
     evas_object_unref(obj);

   tzlaunch_splash->ep = NULL;
   tzlaunch_splash->replaced = EINA_FALSE;
}

static Eina_Bool
_launchscreen_splash_timeout(void *data)
{
   E_Policy_Wl_Tzlaunch_Splash *tzlaunch_splash;
   tzlaunch_splash = (E_Policy_Wl_Tzlaunch_Splash *)data;

   EINA_SAFETY_ON_NULL_RETURN_VAL(tzlaunch_splash, 0);

   _launch_splash_off(tzlaunch_splash);

   return ECORE_CALLBACK_CANCEL;
}

static void
_tzlaunch_splash_iface_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzlaunch_splash)
{
   wl_resource_destroy(res_tzlaunch_splash);
}

static void
_tzlaunch_splash_iface_cb_launch(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzlaunch_splash,
                             const char *pfname, uint32_t ftype,
                             uint32_t depth, uint32_t angle,
                             uint32_t indicator, const char *effect_type,
                             const char *theme_type, struct wl_array *options)
{
   E_Policy_Wl_Tzlaunch_Splash *tzlaunch_splash;
   Evas_Load_Error err;
   E_Client *ec = NULL;
   E_Comp_Object_Content_Type content_type = 0;
   Eina_Bool intercepted = EINA_FALSE;
   int tzlaunch_effect_type;

   tzlaunch_splash = wl_resource_get_user_data(res_tzlaunch_splash);
   EINA_SAFETY_ON_NULL_RETURN(tzlaunch_splash);
   EINA_SAFETY_ON_NULL_RETURN(tzlaunch_splash->ec);
   EINA_SAFETY_ON_NULL_RETURN(tzlaunch_splash->ec->frame);

   tzlaunch_effect_type = _e_policy_wl_tzlaunch_effect_type_get(effect_type);

   ec = tzlaunch_splash->ec;
   ec->effect_type = tzlaunch_effect_type;

   // TO DO
   // invaid parameter handle
   ELOGF("TZPOL",
         "Launchscreen | path %s(%d), indicator(%d), angle(%d), effect_type(%s), theme_type(%s)",
         ec, pfname, ftype, indicator, angle, effect_type, theme_type);

   tzlaunch_splash->path = pfname;
   tzlaunch_splash->type = ftype;
   tzlaunch_splash->indicator = indicator;
   tzlaunch_splash->angle = angle;

   if (indicator)
     {
        /* To configure indicator options */
        ec->indicator.state = TIZEN_INDICATOR_STATE_ON;
        ec->indicator.visible_type = TIZEN_INDICATOR_VISIBLE_TYPE_SHOWN;
        ec->indicator.opacity_mode = TIZEN_INDICATOR_OPACITY_MODE_BG_TRANSPARENT;
     }

   intercepted = e_policy_interceptor_call(E_POLICY_INTERCEPT_LAUNCHSCREEN_OBJECT_SETUP,
                                           ec,
                                           pfname, ftype, depth,
                                           angle, indicator, options);
   if (intercepted)
     {
        tzlaunch_splash->obj = e_comp_object_content_get(ec->frame);

        ELOGF("TZPOL",
              "Launchscreen object setup was successfully intercepted content(%p)",
              ec, tzlaunch_splash->obj);
     }
   else
     {
        if (tzlaunch_splash->type == LAUNCH_IMG_FILE_TYPE_IMAGE)
          {
             content_type = E_COMP_OBJECT_CONTENT_TYPE_EXT_IMAGE;
             tzlaunch_splash->obj = evas_object_image_add(e_comp->evas);
             EINA_SAFETY_ON_NULL_GOTO(tzlaunch_splash->obj, error);
             evas_object_image_file_set(tzlaunch_splash->obj, tzlaunch_splash->path, NULL);

             err = evas_object_image_load_error_get(tzlaunch_splash->obj);
             EINA_SAFETY_ON_FALSE_GOTO(err == EVAS_LOAD_ERROR_NONE, error);

             evas_object_image_fill_set(tzlaunch_splash->obj, 0, 0,  e_comp->w, e_comp->h);
             evas_object_image_filled_set(tzlaunch_splash->obj, EINA_TRUE);
          }
        else
          {
             content_type = E_COMP_OBJECT_CONTENT_TYPE_EXT_EDJE;
             tzlaunch_splash->obj = edje_object_add(e_comp->evas);
             EINA_SAFETY_ON_NULL_GOTO(tzlaunch_splash->obj, error);
             edje_object_file_set (tzlaunch_splash->obj, tzlaunch_splash->path, APP_DEFINE_GROUP_NAME);

             evas_object_move(tzlaunch_splash->obj, 0, 0);
             evas_object_resize(tzlaunch_splash->obj, e_comp->w, e_comp->h);
          }

        if (depth == 32) ec->argb = EINA_TRUE;
        else ec->argb = EINA_FALSE;

        if (!e_comp_object_content_set(ec->frame, tzlaunch_splash->obj, content_type))
          {
             ERR("Setting comp object content for %p failed!", ec);
             goto error;
          }

     }

   if (indicator)
     {
        Evas_Object *indicator_obj = NULL;
        Eina_Bool ret = EINA_FALSE;

        e_mod_indicator_owner_set(ec);
        e_tzsh_indicator_srv_property_update(ec);

        indicator_obj = ecore_evas_extn_plug_new(e_comp->ee);
        if (!indicator_obj)
          {
             ELOGF("TZPOL",
                   "Launchscreen launch | Faild to create ecore_evas_plug for indicator",
                   ec);
          }
        else
          {
             if (e_config->indicator_plug_name)
               {
                  ret = ecore_evas_extn_plug_connect(indicator_obj, e_config->indicator_plug_name, 0, EINA_FALSE);
                  if (ret)
                    {
                       Ecore_Evas *ee;

                       ee = ecore_evas_object_ecore_evas_get(indicator_obj);
                       ecore_evas_data_set(ee, "tzlaunch_splash", tzlaunch_splash);
                       ecore_evas_callback_resize_set(ee,
                                                      _launchscreen_splash_cb_indicator_resized);
                       e_comp_object_indicator_swallow(ec->frame, indicator_obj);
                       evas_object_ref(indicator_obj);
                       ELOGF("TZPOL",
                             "Launchscreen launch | Succeeded to add indicator object plug_name(%s) indicator_obj(%p)",
                             ec, e_config->indicator_plug_name, indicator_obj);
                    }
                  else
                    {
                       evas_object_del(indicator_obj);
                       indicator_obj = NULL;
                    }
               }

             if (!indicator_obj)
               {
                  ELOGF("TZPOL",
                        "Launchscreen launch | Failed to add indicator object plug_name(%s)",
                        ec, e_config->indicator_plug_name?:"NO PLUG NAME");
               }
          }

        tzlaunch_splash->indicator_obj = indicator_obj;
     }

   if (tzlaunch_splash->obj)
     {
        evas_object_ref(tzlaunch_splash->obj);

        evas_object_event_callback_add(tzlaunch_splash->obj,
                                       EVAS_CALLBACK_DEL,
                                       _launchscreen_splash_cb_del, tzlaunch_splash);
        evas_object_event_callback_add(tzlaunch_splash->obj,
                                       EVAS_CALLBACK_HIDE,
                                       _launchscreen_splash_cb_hide, tzlaunch_splash);
     }

   tzlaunch_splash->valid = EINA_TRUE;
   tzlaunch_splash->content_type = e_comp_object_content_type_get(ec->frame);

   return;
error:
   ERR("Could not complete %s", __FUNCTION__);
   if (tzlaunch_splash->obj)
     evas_object_del(tzlaunch_splash->obj);
}

static void
_tzlaunch_splash_iface_cb_owner(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tzlaunch_splash, uint32_t pid)
{
   E_Policy_Wl_Tzlaunch_Splash *tzlaunch_splash;
   E_Client *pre_ec = NULL, *new_ec = NULL, *old_ec;
   Eina_List *clients, *l;

   tzlaunch_splash = wl_resource_get_user_data(res_tzlaunch_splash);
   EINA_SAFETY_ON_NULL_RETURN(tzlaunch_splash);
   EINA_SAFETY_ON_FALSE_RETURN(tzlaunch_splash->valid);

   /* use ec was already created */
   clients = _e_policy_wl_e_clients_find_by_pid(pid);
   EINA_LIST_FOREACH(clients, l, pre_ec)
     {
        if (pre_ec == tzlaunch_splash->ec) continue;
        if (!pre_ec->ignored) continue;
        if (pre_ec->is_cursor) continue;
        new_ec = pre_ec;
        break;
     }
   eina_list_free(clients);

   old_ec = tzlaunch_splash->ec;
   if (new_ec)
     {
        if (e_comp_object_content_set(new_ec->frame,
                                      tzlaunch_splash->obj,
                                      tzlaunch_splash->content_type))
          {
             e_client_unignore(new_ec);
             new_ec->visible = EINA_TRUE;
             if (new_ec->new_client)
               e_comp->new_clients--;
             new_ec->new_client = EINA_FALSE;
             new_ec->argb = old_ec->argb;
             new_ec->effect_type = old_ec->effect_type;
             new_ec->use_splash = EINA_TRUE;
             new_ec->icccm.title = eina_stringshare_add("launchscreen");

             e_comp->launchscrns = eina_list_append(e_comp->launchscrns, new_ec);

             evas_object_show(new_ec->frame);
             evas_object_raise(new_ec->frame);

             tzlaunch_splash->ec = new_ec;
             tzlaunch_splash->replaced = EINA_TRUE;

             ELOGF("TZPOL",
                   "Launchscreen client changed | old(%p) new(%p) using obj(%p)",
                   new_ec,
                   old_ec, new_ec, tzlaunch_splash->obj);

             if (tzlaunch_splash->indicator_obj)
               {
                  e_mod_indicator_owner_set(new_ec);
                  e_tzsh_indicator_srv_property_update(new_ec);
                  e_comp_object_indicator_unswallow(old_ec->frame, tzlaunch_splash->indicator_obj);
                  e_comp_object_indicator_swallow(new_ec->frame, tzlaunch_splash->indicator_obj);
               }

             /* delete ec was created for launchscreen */
             e_comp->launchscrns = eina_list_remove(e_comp->launchscrns, old_ec);

             e_pixmap_win_id_del(tzlaunch_splash->ep);
             e_object_del(E_OBJECT(old_ec));
             tzlaunch_splash->ep = NULL;
          }
        else
          ERR("Can't set external content for new_ec(%p)", new_ec);
     }
   else
     {
       old_ec->ignored = EINA_FALSE;
       old_ec->visible = EINA_TRUE;
       if (old_ec->new_client)
         e_comp->new_clients--;
       old_ec->new_client = EINA_FALSE;
       old_ec->icccm.accepts_focus = EINA_TRUE;

        evas_object_show(old_ec->frame);
        evas_object_raise(old_ec->frame);
     }

   EC_CHANGED(tzlaunch_splash->ec);
   e_client_visibility_calculate();

   if (tzlaunch_splash->timeout)
     {
        ecore_timer_del(tzlaunch_splash->timeout);
        tzlaunch_splash->timeout = NULL;
     }
   if (!e_config->launchscreen_without_timer)
     tzlaunch_splash->timeout = ecore_timer_add(e_config->launchscreen_timeout, _launchscreen_splash_timeout, tzlaunch_splash);

   ELOGF("TZPOL", "Launchscreen img(%d) set owner pid: %d",
         tzlaunch_splash->ec,
         wl_resource_get_id(res_tzlaunch_splash), pid);

   tzlaunch_splash->pid = pid;
   tzlaunch_splash->ec->netwm.pid = pid;
   tzlaunch_splash->ec->use_splash = EINA_TRUE;
}

static const struct tizen_launch_splash_interface _tzlaunch_splash_iface =
{
   _tzlaunch_splash_iface_cb_destroy,
   _tzlaunch_splash_iface_cb_launch,
   _tzlaunch_splash_iface_cb_owner
};

static E_Policy_Wl_Tzlaunch_Splash *
_tzlaunch_splash_add(struct wl_resource *res_tzlaunch_effect, struct wl_resource *res_tzlaunch_splash)
{
   E_Policy_Wl_Tzlaunch_Effect *tzlaunch_effect;
   E_Policy_Wl_Tzlaunch_Splash *tzlaunch_splash;

   tzlaunch_splash = E_NEW(E_Policy_Wl_Tzlaunch_Splash, 1);
   EINA_SAFETY_ON_NULL_GOTO(tzlaunch_splash, error);

   tzlaunch_effect = wl_resource_get_user_data(res_tzlaunch_effect);
   EINA_SAFETY_ON_NULL_GOTO(tzlaunch_effect, error);

   tzlaunch_effect->splash_list = eina_list_append(tzlaunch_effect->splash_list, tzlaunch_splash);

   tzlaunch_splash->tzlaunch_effect  = tzlaunch_effect;
   tzlaunch_splash->res_tzlaunch_splash = res_tzlaunch_splash;

   tzlaunch_splash->replaced = EINA_FALSE;
   tzlaunch_splash->ep = e_pixmap_new(E_PIXMAP_TYPE_EXT_OBJECT, 0);
   EINA_SAFETY_ON_NULL_GOTO(tzlaunch_splash->ep, error);
   tzlaunch_splash->ec = e_client_new(tzlaunch_splash->ep, 0, 1);
   EINA_SAFETY_ON_NULL_GOTO(tzlaunch_splash->ec, error);

   tzlaunch_splash->ec->icccm.title = eina_stringshare_add("Launchscreen");
   tzlaunch_splash->ec->icccm.name = eina_stringshare_add("Launchscreen");
   tzlaunch_splash->ec->ignored = EINA_TRUE;

   e_comp->launchscrns = eina_list_append(e_comp->launchscrns, tzlaunch_splash->ec);

   return tzlaunch_splash;
error:
   if (tzlaunch_splash)
     {
        ERR("Could not initialize launchscreen client");
        if (tzlaunch_splash->ep)
          e_pixmap_win_id_del(tzlaunch_splash->ep);
        if (tzlaunch_splash->ec)
          e_object_del(E_OBJECT(tzlaunch_splash->ec));
        E_FREE(tzlaunch_splash);
     }
   return NULL;
}


static void
_tzlaunch_splash_destroy(struct wl_resource *res_tzlaunch_splash)
{
   E_Policy_Wl_Tzlaunch_Splash *tzlaunch_splash;
   E_Policy_Wl_Tzlaunch_Effect *tzlaunch_effect;

   EINA_SAFETY_ON_NULL_RETURN(res_tzlaunch_splash);

   tzlaunch_splash = wl_resource_get_user_data(res_tzlaunch_splash);
   EINA_SAFETY_ON_NULL_RETURN(tzlaunch_splash);

   if (tzlaunch_splash->obj)
     {
        evas_object_event_callback_del_full(tzlaunch_splash->obj, EVAS_CALLBACK_DEL, _launchscreen_splash_cb_del, tzlaunch_splash);
        evas_object_event_callback_del_full(tzlaunch_splash->obj, EVAS_CALLBACK_HIDE, _launchscreen_splash_cb_hide, tzlaunch_splash);
     }

   _launch_splash_off(tzlaunch_splash);

   tzlaunch_effect = tzlaunch_splash->tzlaunch_effect;
   tzlaunch_effect->splash_list = eina_list_remove(tzlaunch_effect->splash_list, tzlaunch_splash);

   memset(tzlaunch_splash, 0x0, sizeof(E_Policy_Wl_Tzlaunch_Splash));
   E_FREE(tzlaunch_splash);
}

static void
_tzlaunch_effect_iface_cb_create_splash_img(struct wl_client *client, struct wl_resource *res_tzlaunch_effect, uint32_t id)
{

   E_Policy_Wl_Tzlaunch_Splash *plaunch_splash;
   struct wl_resource *res_tzlaunch_splash;

   res_tzlaunch_splash = wl_resource_create(client,
                                         &tizen_launch_splash_interface,
                                         wl_resource_get_version(res_tzlaunch_effect),
                                         id);
   if (!res_tzlaunch_splash)
     {
        wl_resource_post_error
           (res_tzlaunch_effect,
            WL_DISPLAY_ERROR_INVALID_OBJECT,
            "Invalid res_tzlaunch effect's user data");
        return;
     }

   plaunch_splash = _tzlaunch_splash_add(res_tzlaunch_effect, res_tzlaunch_splash);
   EINA_SAFETY_ON_NULL_GOTO(plaunch_splash, err);

   wl_resource_set_implementation(res_tzlaunch_splash,
                                  &_tzlaunch_splash_iface,
                                  plaunch_splash,
                                  _tzlaunch_splash_destroy);

   return;

err:
   ERR("Could not create tizen_launch_splash_interface res: %m");
   wl_client_post_no_memory(client);
}

static void
_tzlaunch_effect_iface_cb_type_set(struct wl_client *client, struct wl_resource *res_tzlaunch_effect,
                                               const char *effect_type, uint32_t pid, struct wl_array *options)
{
   Eina_List *clients, *l;
   E_Client *_ec = NULL;
   int effect_set = 0;
   int tzlaunch_effect_type = _e_policy_wl_tzlaunch_effect_type_get(effect_type);

   clients = _e_policy_wl_e_clients_find_by_pid(pid);
   EINA_LIST_FOREACH(clients, l, _ec)
     {
        if (_ec)
          {
             _ec->effect_type = tzlaunch_effect_type;
             effect_set = 1;
             ELOGF("TZPOL",
                    "Launchscreen effect type set | exist ec | effect (%d) pid (%d)",
                    _ec, tzlaunch_effect_type, pid);
          }
     }
   eina_list_free(clients);

   if (effect_set)
     _e_policy_wl_tzlaunch_effect_type_unset(pid);
   else
     {
        E_Policy_Wl_Tzlaunch_Effect_Info *tzlaunch_effect_info;

        tzlaunch_effect_info = E_NEW(E_Policy_Wl_Tzlaunch_Effect_Info, 1);
        EINA_SAFETY_ON_NULL_RETURN(tzlaunch_effect_info);
        tzlaunch_effect_info->pid = pid;
        tzlaunch_effect_info->effect_type = tzlaunch_effect_type;
        polwl->tzlaunch_effect_info = eina_list_append(polwl->tzlaunch_effect_info, tzlaunch_effect_info);

        ELOGF("TZPOL",
              "Launchscreen effect type set | no match ec | effect (%d) pid (%d)",
              NULL, tzlaunch_effect_type, pid);
     }
}

static void
_tzlaunch_effect_iface_cb_type_unset(struct wl_client *client, struct wl_resource *res_tzlaunch_effect,
                                                 uint32_t pid)
{
   _e_policy_wl_tzlaunch_effect_type_unset(pid);
}

static void
_tzlaunch_effect_iface_cb_destroy(struct wl_client *client, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static const struct tizen_launch_effect_interface _tzlaunch_effect_iface =
{
   _tzlaunch_effect_iface_cb_create_splash_img,
   _tzlaunch_effect_iface_cb_type_set,
   _tzlaunch_effect_iface_cb_type_unset,
   _tzlaunch_effect_iface_cb_destroy,
};

static void
_tzlaunch_effect_del(E_Policy_Wl_Tzlaunch_Effect *tzlaunch_effect)
{
   E_Policy_Wl_Tzlaunch_Splash *plaunch_splash;
   Eina_List *l, *ll;

   EINA_SAFETY_ON_NULL_RETURN(tzlaunch_effect);

   // remove tzlaunch created splash list
   EINA_LIST_FOREACH_SAFE(tzlaunch_effect->splash_list, l, ll, plaunch_splash)
     {
        if (plaunch_splash->tzlaunch_effect != tzlaunch_effect) continue;
        wl_resource_destroy(plaunch_splash->res_tzlaunch_splash);
        break;
     }

   polwl->tzlaunch_effect = eina_list_remove(polwl->tzlaunch_effect, tzlaunch_effect);

   memset(tzlaunch_effect, 0x0, sizeof(E_Policy_Wl_Tzlaunch_Effect));
   E_FREE(tzlaunch_effect);
}

static E_Policy_Wl_Tzlaunch_Effect *
_tzlaunch_effect_add(struct wl_resource *res_tzlaunch_effect)
{
   E_Policy_Wl_Tzlaunch_Effect *tzlaunch_effect;

   tzlaunch_effect = E_NEW(E_Policy_Wl_Tzlaunch_Effect, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tzlaunch_effect, NULL);

   tzlaunch_effect->res_tzlaunch_effect = res_tzlaunch_effect;

   polwl->tzlaunch_effect = eina_list_append(polwl->tzlaunch_effect, tzlaunch_effect);

   return tzlaunch_effect;
}

static void
_tzlaunch_effect_cb_unbind(struct wl_resource *res_tzlaunch_effect)
{
   E_Policy_Wl_Tzlaunch_Effect *tzlaunch_effect = NULL;
   Eina_List *l, *ll;

   EINA_LIST_FOREACH_SAFE(polwl->tzlaunch_effect, l, ll, tzlaunch_effect)
     {
        if (tzlaunch_effect->res_tzlaunch_effect != res_tzlaunch_effect) continue;
        _tzlaunch_effect_del(tzlaunch_effect);
        break;
     }
}

static void
_tzlaunch_effect_cb_bind(struct wl_client *client, void *data EINA_UNUSED, uint32_t ver, uint32_t id)
{
   E_Policy_Wl_Tzlaunch_Effect *tzlaunch_effect = NULL;
   struct wl_resource *res_tzlaunch_effect;

   EINA_SAFETY_ON_NULL_GOTO(polwl, err);

   res_tzlaunch_effect = wl_resource_create(client,
                                     &tizen_launch_effect_interface,
                                     ver,
                                     id);
   EINA_SAFETY_ON_NULL_GOTO(res_tzlaunch_effect, err);

   tzlaunch_effect = _tzlaunch_effect_add(res_tzlaunch_effect);
   EINA_SAFETY_ON_NULL_GOTO(tzlaunch_effect, err);

   wl_resource_set_implementation(res_tzlaunch_effect,
                                  &_tzlaunch_effect_iface,
                                  tzlaunch_effect,
                                  _tzlaunch_effect_cb_unbind);

   return;

err:
   ERR("Could not create tizen_launch_effect_interface res: %m");
   wl_client_post_no_memory(client);
}

// --------------------------------------------------------
// tizen_launch_appinfo_interface
// --------------------------------------------------------
static void
_tzlaunch_appinfo_iface_cb_register_pid(struct wl_client *client, struct wl_resource *res_tzlaunch_appinfo,
                                            uint32_t pid)
{
   E_Policy_Wl_Tzlaunch_Appinfo *appinfo = NULL;
   E_Policy_Appinfo *epai = NULL;

   appinfo = wl_resource_get_user_data(res_tzlaunch_appinfo);
   if (!appinfo)
     {
        wl_resource_post_error(res_tzlaunch_appinfo,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "Invalid tzlaunch_appinfo's user data");
        return;
     }

   if (pid <= 0)
     {
        ELOGF("TZ_APPINFO", "registered pid is invalid. pid:%u", NULL, pid);
        return;
     }

   epai = e_policy_appinfo_new();
   EINA_SAFETY_ON_NULL_RETURN(epai);

   if (!e_policy_appinfo_pid_set(epai, pid))
     {
        ELOGF("TZ_APPINFO", "failed to set pid is invalid. pid:%u", NULL, pid);
        e_policy_appinfo_del(epai);
        return;
     }
}

static void
_tzlaunch_appinfo_iface_cb_deregister_pid(struct wl_client *client, struct wl_resource *res_tzlaunch_appinfo,
                                            uint32_t pid)
{
   E_Policy_Wl_Tzlaunch_Appinfo *appinfo = NULL;
   E_Policy_Appinfo *epai = NULL;

   appinfo = wl_resource_get_user_data(res_tzlaunch_appinfo);
   if (!appinfo)
     {
        wl_resource_post_error(res_tzlaunch_appinfo,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "Invalid tzlaunch_appinfo's user data");
        return;
     }

   epai = e_policy_appinfo_find_with_pid(pid);
   EINA_SAFETY_ON_NULL_RETURN(epai);

   e_policy_appinfo_del(epai);
}

static void
_tzlaunch_appinfo_iface_cb_set_appid(struct wl_client *client, struct wl_resource *res_tzlaunch_appinfo,
                                      uint32_t pid, const char *appid)
{
   E_Policy_Wl_Tzlaunch_Appinfo *tzlaunch_appinfo = NULL;
   E_Policy_Appinfo *epai = NULL;
   int width = 0;
   int height = 0;

   tzlaunch_appinfo = wl_resource_get_user_data(res_tzlaunch_appinfo);
   if (!tzlaunch_appinfo)
     {
        wl_resource_post_error(res_tzlaunch_appinfo,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "Invalid tzlaunch_appinfo's user data");
        return;
     }

   if (pid <= 0)
     {
        ELOGF("TZ_APPINFO", "set pid is invalid. pid:%u", NULL, pid);
        return;
     }

   epai = e_policy_appinfo_find_with_pid(pid);
   EINA_SAFETY_ON_NULL_RETURN(epai);

   if (!e_policy_appinfo_appid_set(epai, appid))
     {
        ELOGF("TZ_APPINFO", "failed to set appid, appid:%s", NULL, appid);
        return;
     }

   // 1. send HOOK with pid
   _e_policy_wl_hook_call(E_POLICY_WL_HOOK_BASE_OUTPUT_RESOLUTION_GET, pid);
   // 2. module must set the base_output_resolution.
   if (!e_policy_appinfo_base_output_resolution_get(epai, &width, &height))
     {
        ELOGF("TZ_APPINFO", "failed to set base_output_resolution in module, pid:%u, appid:%s", NULL, pid, appid);
        return;
     }
   // 3. server has to get the base_screern_resolution via e_policy_appinfo_base_output_resolution_get.
   //    3-1. if success, use the base_rescreen_resolution
   //    3-2. if fail, get the base_output_resolution from the E_Comp_Wl_Output.

   // 4. send output.
   if (!e_comp_wl_pid_output_configured_resolution_send(pid, width, height))
     {
        ELOGF("TZ_APPINFO", "failed to send output_configured_resolution, pid:%u, appid:%s", NULL, pid, appid);
        return;
     }

   return;
}

static void
_tzlaunch_appinfo_iface_cb_destroy(struct wl_client *client, struct wl_resource *res_tzlaunch_appinfo)
{
   wl_resource_destroy(res_tzlaunch_appinfo);
}

static void
_tzlaunch_appinfo_iface_cb_get_base_output_resolution(struct wl_client *client, struct wl_resource *res_tzlaunch_appinfo,
                                    uint32_t pid)
{
   E_Policy_Appinfo *epai = NULL;
   int width = 0, height = 0;

   if (pid <= 0)
     {
        ELOGF("TZ_APPINFO", "requested pid is invalid. pid:%u", NULL, pid);
        goto err;
     }

   epai = e_policy_appinfo_find_with_pid(pid);
   if (!epai)
     {
        ELOGF("TZ_APPINFO", "cannot find pid. pid:%u", NULL, pid);
        goto err;
     }

   if (!e_policy_appinfo_base_output_resolution_get(epai, &width, &height))
     {
        ELOGF("TZ_APPINFO", "cannot read size. pid:%u", NULL, pid);
        goto err;
     }

   tizen_launch_appinfo_send_base_output_resolution_done(res_tzlaunch_appinfo, pid, width, height);
   ELOGF("TZ_APPINFO", "send Output base_output_resolution size(%d, %d) : pid(%u)", NULL, width, height, pid);

   return;

err:
   if (!e_comp_wl_pid_output_configured_resolution_get(pid, &width, &height))
     ELOGF("TZ_APPINFO", "ERROR failed to get Output base_output_resolution send size(%d, %d) : pid(%u)", NULL, width, height, pid);
   else
     ELOGF("TZ_APPINFO", "send Output base_output_resolution size(%d, %d) : pid(%u)", NULL, width, height, pid);
   tizen_launch_appinfo_send_base_output_resolution_done(res_tzlaunch_appinfo, pid, width, height);

   return;
}

static const struct tizen_launch_appinfo_interface _tzlaunch_appinfo_iface =
{
   _tzlaunch_appinfo_iface_cb_destroy,
   _tzlaunch_appinfo_iface_cb_register_pid,
   _tzlaunch_appinfo_iface_cb_deregister_pid,
   _tzlaunch_appinfo_iface_cb_set_appid,
   _tzlaunch_appinfo_iface_cb_get_base_output_resolution,
};

static void
_tzlaunch_appinfo_del(E_Policy_Wl_Tzlaunch_Appinfo *tzlaunch_appinfo)
{
   EINA_SAFETY_ON_NULL_RETURN(tzlaunch_appinfo);

   polwl->tzlaunch_appinfo = eina_list_remove(polwl->tzlaunch_appinfo, tzlaunch_appinfo);

   memset(tzlaunch_appinfo, 0x0, sizeof(E_Policy_Wl_Tzlaunch_Appinfo));
   E_FREE(tzlaunch_appinfo);
}

static E_Policy_Wl_Tzlaunch_Appinfo *
_tzlaunch_appinfo_add(struct wl_resource *res_tzlaunch_appinfo)
{
   E_Policy_Wl_Tzlaunch_Appinfo *tzlaunch_appinfo;

   tzlaunch_appinfo = E_NEW(E_Policy_Wl_Tzlaunch_Appinfo, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tzlaunch_appinfo, NULL);

   tzlaunch_appinfo->res_tzlaunch_appinfo = res_tzlaunch_appinfo;

   polwl->tzlaunch_appinfo = eina_list_append(polwl->tzlaunch_appinfo, tzlaunch_appinfo);

   return tzlaunch_appinfo;
}

static void
_tzlaunch_appinfo_cb_unbind(struct wl_resource *res_tzlaunch_appinfo)
{
   E_Policy_Wl_Tzlaunch_Appinfo *tzlaunch_appinfo= NULL;
   Eina_List *l, *ll;

   EINA_LIST_FOREACH_SAFE(polwl->tzlaunch_appinfo, l, ll, tzlaunch_appinfo)
     {
        if (tzlaunch_appinfo->res_tzlaunch_appinfo != res_tzlaunch_appinfo) continue;
        _tzlaunch_appinfo_del(tzlaunch_appinfo);
        break;
     }
}

static void
_tzlaunch_appinfo_cb_bind(struct wl_client *client, void *data EINA_UNUSED, uint32_t ver, uint32_t id)
{
   E_Policy_Wl_Tzlaunch_Appinfo *tzlaunch_appinfo = NULL;
   struct wl_resource *res_tzlaunch_appinfo;

   EINA_SAFETY_ON_NULL_GOTO(polwl, err);

   res_tzlaunch_appinfo = wl_resource_create(client,
                                     &tizen_launch_appinfo_interface,
                                     ver,
                                     id);
   EINA_SAFETY_ON_NULL_GOTO(res_tzlaunch_appinfo, err);

   tzlaunch_appinfo = _tzlaunch_appinfo_add(res_tzlaunch_appinfo);
   EINA_SAFETY_ON_NULL_GOTO(tzlaunch_appinfo, err);

   wl_resource_set_implementation(res_tzlaunch_appinfo,
                                  &_tzlaunch_appinfo_iface,
                                  tzlaunch_appinfo,
                                  _tzlaunch_appinfo_cb_unbind);

   return;

err:
   ERR("Could not create tizen_launch_appinfo_interface res: %m");
   wl_client_post_no_memory(client);
}

static Eina_Bool
_e_policy_wl_cb_hook_intercept_show_helper(void *data, E_Client *ec)
{
   E_Policy_Wl_Tzpol *tzpol;
   E_Policy_Wl_Surface *psurf;
   Eina_Iterator *it;

   it = eina_hash_iterator_data_new(polwl->tzpols);
   EINA_ITERATOR_FOREACH(it, tzpol)
     {
        psurf = _e_policy_wl_tzpol_surf_find(tzpol, ec);
        if (psurf)
          {
             if (psurf->is_background)
               {
                  ELOGF("TZPOL",
                        "BACKGROUND State is On, Deny Show",
                        ec);
                  return EINA_FALSE;
               }
          }
     }
   eina_iterator_free(it);

   return EINA_TRUE;
}

static Eina_Bool
_e_policy_wl_cb_scrsaver_on(void *data EINA_UNUSED, int type EINA_UNUSED, void *event EINA_UNUSED)
{
   if (_scrsaver_mng_res)
     tws_service_screensaver_manager_send_idle(_scrsaver_mng_res);
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_policy_wl_cb_scrsaver_off(void *data EINA_UNUSED, int type EINA_UNUSED, void *event EINA_UNUSED)
{
   if (_scrsaver_mng_res)
     tws_service_screensaver_manager_send_active(_scrsaver_mng_res);
   return ECORE_CALLBACK_PASS_ON;
}

// --------------------------------------------------------
// E_Policy_Wl_Tz_Indicator
// --------------------------------------------------------
static E_Policy_Wl_Tz_Indicator *
_e_policy_wl_tz_indicator_add(struct wl_resource *res_tz_indicator)
{
   E_Policy_Wl_Tz_Indicator *tz_indicator;

   tz_indicator = E_NEW(E_Policy_Wl_Tz_Indicator, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tz_indicator, NULL);

   tz_indicator->res_tz_indicator = res_tz_indicator;

   polwl->tz_indicators = eina_list_append(polwl->tz_indicators, tz_indicator);

   return tz_indicator;
}

static void
_e_policy_wl_tz_indicator_del(E_Policy_Wl_Tz_Indicator *tz_indicator)
{
   EINA_SAFETY_ON_NULL_RETURN(tz_indicator);

   polwl->tz_indicators = eina_list_remove(polwl->tz_indicators, tz_indicator);
   E_FREE(tz_indicator);
}

static E_Policy_Wl_Tz_Indicator *
_e_policy_wl_tz_indicator_get(struct wl_resource *res_tz_indicator)
{
   Eina_List *l;
   E_Policy_Wl_Tz_Indicator *tz_indicator;

   EINA_LIST_FOREACH(polwl->tz_indicators, l, tz_indicator)
     {
        if (tz_indicator->res_tz_indicator == res_tz_indicator)
          return tz_indicator;
     }
   return NULL;
}

static E_Policy_Wl_Tz_Indicator *
_e_policy_wl_tz_indicator_get_from_client(E_Client *ec)
{
   Eina_List *l;
   E_Policy_Wl_Tz_Indicator *tz_indicator;

   EINA_LIST_FOREACH(polwl->tz_indicators, l, tz_indicator)
     {
        if (eina_list_data_find(tz_indicator->ec_list, ec))
          return tz_indicator;
     }

   return NULL;
}

static Eina_Bool
_e_policy_wl_tz_indicator_set_client(struct wl_resource *res_tz_indicator, E_Client *ec)
{
   E_Policy_Wl_Tz_Indicator *tz_indicator = NULL;

   tz_indicator = _e_policy_wl_tz_indicator_get(res_tz_indicator);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tz_indicator, EINA_FALSE);

   if (!eina_list_data_find(tz_indicator->ec_list, ec))
     tz_indicator->ec_list = eina_list_append(tz_indicator->ec_list, ec);

   return EINA_TRUE;
}

static void
_e_policy_wl_tz_indicator_unset_client(E_Client *ec)
{
   Eina_List *l;
   E_Policy_Wl_Tz_Indicator *tz_indicator;

   EINA_SAFETY_ON_NULL_RETURN(ec);

   EINA_LIST_FOREACH(polwl->tz_indicators, l, tz_indicator)
     {
        if (eina_list_data_find(tz_indicator->ec_list, ec))
          tz_indicator->ec_list = eina_list_remove(tz_indicator->ec_list, ec);
     }
}

static void
_tz_indicator_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tz_indicator)
{
   wl_resource_destroy(res_tz_indicator);
}

static void
_tz_indicator_cb_state_set(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tz_indicator, struct wl_resource *surf, int32_t state)
{
   E_Client *ec;
   E_Indicator_State ind_state;

   ec = wl_resource_get_user_data(surf);
   EINA_SAFETY_ON_NULL_RETURN(ec);
   EINA_SAFETY_ON_NULL_RETURN(ec->frame);

   if (state == TIZEN_INDICATOR_STATE_ON)
     ind_state = E_INDICATOR_STATE_ON;
   else if (state == TIZEN_INDICATOR_STATE_OFF)
     ind_state = E_INDICATOR_STATE_OFF;
   else
     ind_state = E_INDICATOR_STATE_UNKNOWN;

   ELOGF("TZ_IND", "TZ_STATE:%d, E_STATE:%d", ec, state, ind_state);
   _e_policy_wl_tz_indicator_set_client(res_tz_indicator, ec);
   ec->indicator.state = ind_state;

   e_policy_event_simple(ec, E_EVENT_POLICY_INDICATOR_STATE_CHANGE);
}

static void
_tz_indicator_cb_opacity_mode_set(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tz_indicator, struct wl_resource *surf, int32_t mode)
{
   E_Client *ec;
   E_Indicator_Opacity_Mode op_mode;

   ec = wl_resource_get_user_data(surf);
   EINA_SAFETY_ON_NULL_RETURN(ec);
   EINA_SAFETY_ON_NULL_RETURN(ec->frame);

   switch (mode)
     {
      case TIZEN_INDICATOR_OPACITY_MODE_OPAQUE:
        op_mode = E_INDICATOR_OPACITY_MODE_OPAQUE;
        break;

      case TIZEN_INDICATOR_OPACITY_MODE_TRANSLUCENT:
        op_mode = E_INDICATOR_OPACITY_MODE_TRANSLUCENT;
        break;

      case TIZEN_INDICATOR_OPACITY_MODE_TRANSPARENT:
        op_mode = E_INDICATOR_OPACITY_MODE_TRANSPARENT;
        break;

      case TIZEN_INDICATOR_OPACITY_MODE_BG_TRANSPARENT:
        op_mode = E_INDICATOR_OPACITY_MODE_BG_TRANSPARENT;
        break;

      default:
        op_mode = E_INDICATOR_OPACITY_MODE_OPAQUE;
        break;
     }

   ELOGF("TZ_IND", "TZ_OP_MODE:%d, E_OP_MODE:%d", ec, mode, op_mode);
   _e_policy_wl_tz_indicator_set_client(res_tz_indicator, ec);

   if (ec->indicator.opacity_mode == op_mode) return;

   ec->indicator.opacity_mode = op_mode;
   e_tzsh_indicator_srv_property_change_send(ec, ec->e.state.rot.ang.curr);

   e_policy_event_simple(ec, E_EVENT_POLICY_INDICATOR_OPACITY_MODE_CHANGE);
}

static void
_tz_indicator_cb_visible_type_set(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tz_indicator, struct wl_resource *surf, int32_t vtype)
{
   E_Client *ec;
   E_Indicator_Visible_Type vis_type;

   ec = wl_resource_get_user_data(surf);
   EINA_SAFETY_ON_NULL_RETURN(ec);
   EINA_SAFETY_ON_NULL_RETURN(ec->frame);

   if (vtype == TIZEN_INDICATOR_VISIBLE_TYPE_SHOWN)
     vis_type = E_INDICATOR_VISIBLE_TYPE_SHOWN;
   else
     vis_type = E_INDICATOR_VISIBLE_TYPE_HIDDEN;

   ELOGF("TZ_IND", "TZ_VIS_TYPE:%d, E_VIS_TYPE:%d", ec, vtype, vis_type);
   _e_policy_wl_tz_indicator_set_client(res_tz_indicator, ec);
   ec->indicator.visible_type = vis_type;

   e_policy_event_simple(ec, E_EVENT_POLICY_INDICATOR_VISIBLE_STATE_CHANGE);
}

// --------------------------------------------------------
// tizen_indicator_interface
// --------------------------------------------------------
static const struct tizen_indicator_interface _tz_indicator_iface =
{
   _tz_indicator_cb_destroy,
   _tz_indicator_cb_state_set,
   _tz_indicator_cb_opacity_mode_set,
   _tz_indicator_cb_visible_type_set,
};

static void
_tz_indicator_cb_unbind(struct wl_resource *res_tz_indicator)
{
   E_Policy_Wl_Tz_Indicator *tz_indicator;

   tz_indicator = _e_policy_wl_tz_indicator_get(res_tz_indicator);
   EINA_SAFETY_ON_NULL_RETURN(tz_indicator);

   _e_policy_wl_tz_indicator_del(tz_indicator);
}

static void
_tz_indicator_cb_bind(struct wl_client *client, void *data EINA_UNUSED, uint32_t ver, uint32_t id)
{
   E_Policy_Wl_Tz_Indicator *tz_indicator_pol;
   struct wl_resource *res_tz_indicator;

   EINA_SAFETY_ON_NULL_GOTO(polwl, err);

   res_tz_indicator = wl_resource_create(client,
                                         &tizen_indicator_interface,
                                         ver,
                                         id);
   EINA_SAFETY_ON_NULL_GOTO(res_tz_indicator, err);

   tz_indicator_pol = _e_policy_wl_tz_indicator_add(res_tz_indicator);
   EINA_SAFETY_ON_NULL_GOTO(tz_indicator_pol, err);

   wl_resource_set_implementation(res_tz_indicator,
                                  &_tz_indicator_iface,
                                  NULL,
                                  _tz_indicator_cb_unbind);
   return;

err:
   ERR("Could not create tizen_indicator_interface res: %m");
   wl_client_post_no_memory(client);
}

void
e_policy_wl_indicator_flick_send(E_Client *ec)
{
   E_Policy_Wl_Tz_Indicator *tz_indicator;
   struct wl_resource *surf;

   tz_indicator = _e_policy_wl_tz_indicator_get_from_client(ec);
   EINA_SAFETY_ON_NULL_RETURN(tz_indicator);

   if (ec->comp_data)
     surf = ec->comp_data->surface;
   else
     surf = NULL;

   ELOGF("TZ_IND", "SEND FLICK EVENT", ec);
   tizen_indicator_send_flick(tz_indicator->res_tz_indicator, surf, 0);
}


// --------------------------------------------------------
// E_Policy_Wl_Tz_Clipboard
// --------------------------------------------------------
static E_Policy_Wl_Tz_Clipboard *
_e_policy_wl_tz_clipboard_add(struct wl_resource *res_tz_clipboard)
{
   E_Policy_Wl_Tz_Clipboard *tz_clipboard;

   tz_clipboard = E_NEW(E_Policy_Wl_Tz_Clipboard, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tz_clipboard, NULL);

   tz_clipboard->res_tz_clipboard = res_tz_clipboard;
   polwl->tz_clipboards = eina_list_append(polwl->tz_clipboards, tz_clipboard);

   return tz_clipboard;
}

static void
_e_policy_wl_tz_clipboard_del(E_Policy_Wl_Tz_Clipboard *tz_clipboard)
{
   EINA_SAFETY_ON_NULL_RETURN(tz_clipboard);

   polwl->tz_clipboards = eina_list_remove(polwl->tz_clipboards, tz_clipboard);
   E_FREE(tz_clipboard);
}

static E_Policy_Wl_Tz_Clipboard *
_e_policy_wl_tz_clipboard_get_from_client(E_Client *ec)
{
   Eina_List *l;
   E_Policy_Wl_Tz_Clipboard *tz_clipboard;

   EINA_LIST_FOREACH(polwl->tz_clipboards, l, tz_clipboard)
     {
        if (eina_list_data_find(tz_clipboard->ec_list, ec))
          return tz_clipboard;
     }

   return NULL;
}

static Eina_Bool
_e_policy_wl_tz_clipboard_set_client(struct wl_resource *res_tz_clipboard, E_Client *ec)
{
   E_Policy_Wl_Tz_Clipboard *tz_clipboard = NULL;

   tz_clipboard = wl_resource_get_user_data(res_tz_clipboard);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tz_clipboard, EINA_FALSE);

   if (!eina_list_data_find(tz_clipboard->ec_list, ec))
     {
        tz_clipboard->ec_list = eina_list_append(tz_clipboard->ec_list, ec);
     }
   return EINA_TRUE;
}

static void
_e_policy_wl_tz_clipboard_unset_client(E_Client *ec)
{
   Eina_List *l;
   E_Policy_Wl_Tz_Clipboard *tz_clipboard = NULL;

   EINA_SAFETY_ON_NULL_RETURN(ec);

   EINA_LIST_FOREACH(polwl->tz_clipboards, l, tz_clipboard)
     {
        if (eina_list_data_find(tz_clipboard->ec_list, ec))
          {
             tz_clipboard->ec_list = eina_list_remove(tz_clipboard->ec_list, ec);
          }
     }
}

// --------------------------------------------------------
// tizen_clipboard_interface
// --------------------------------------------------------
static void
_tz_clipboard_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tz_clipboard)
{
   wl_resource_destroy(res_tz_clipboard);
}

static void
_tz_clipboard_cb_show(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tz_clipboard, struct wl_resource *surf)
{
   E_Client *ec;

   ec = wl_resource_get_user_data(surf);
   EINA_SAFETY_ON_NULL_RETURN(ec);
   EINA_SAFETY_ON_NULL_RETURN(ec->frame);

   _e_policy_wl_tz_clipboard_set_client(res_tz_clipboard, ec);
   e_service_cbhm_parent_set(ec, EINA_TRUE);
   e_service_cbhm_show();
}

static void
_tz_clipboard_cb_hide(struct wl_client *client EINA_UNUSED, struct wl_resource *res_tz_clipboard, struct wl_resource *surf)
{
   E_Client *ec;

   ec = wl_resource_get_user_data(surf);
   EINA_SAFETY_ON_NULL_RETURN(ec);
   EINA_SAFETY_ON_NULL_RETURN(ec->frame);

   e_service_cbhm_parent_set(ec, EINA_FALSE);
   e_service_cbhm_hide();
}

static void
_tz_clipboard_cb_data_only_set(struct wl_client *client, struct wl_resource *res_tz_clipboard, uint32_t set)
{
   E_Policy_Wl_Tz_Clipboard *tz_clipboard = NULL;
   struct wl_client *_wc;
   struct wl_resource *data_res;
   pid_t pid = 0;
   uid_t uid = 0;
   Eina_Bool res;
   Eina_List *clients;
   E_Client *ec, *found = NULL;

   tz_clipboard = wl_resource_get_user_data(res_tz_clipboard);
   EINA_SAFETY_ON_NULL_RETURN(tz_clipboard);

   if (tz_clipboard->ec_list)
     {
        ELOGF("TZPOL",
              "Unable to set data only mode for wl_client(%p) : "
              "ec_list exists",
              NULL, client);
        goto send_deny;
     }

   if (!(data_res = e_comp_wl_data_find_for_client(client)))
     {
        ELOGF("TZPOL",
              "Unable to set data only mode for wl_client(%p) : "
              "no wl_data_device resource",
              NULL, client);
        goto send_deny;
     }

   clients = _e_policy_wl_e_clients_find_by_pid(pid);
   if (clients)
     {
        EINA_LIST_FREE(clients, ec)
          {
             if (found) continue;
             if (ec->comp_data && ec->comp_data->surface)
               {
                  _wc = wl_resource_get_client(ec->comp_data->surface);
                  if (_wc == client)
                    found = ec;
               }
          }
     }

   if (found)
     {
        ELOGF("TZPOL",
              "Unable to set data only mode for wl_client(%p) : "
              "have ec(%p)",
              NULL, client, ec);
        goto send_deny;
     }

   /* Privilege Check */
   wl_client_get_credentials(client, &pid, &uid, NULL);
   res = e_security_privilege_check(pid, uid,
                                    E_PRIVILEGE_DATA_ONLY_SET);
   if (!res)
     {
        ELOGF("TZPOL",
              "Privilege Check Failed! DENY data_only_set",
              NULL);
        goto send_deny;
     }

   ELOGF("TZPOL",
         "Set data only mode :%d for wl_client(%p)",
         NULL, set, client);
   e_comp_wl_data_device_only_set(data_res, !(set == 0));
   tizen_clipboard_send_allowed_data_only(res_tz_clipboard, (uint32_t)1);
   return;

send_deny:
   tizen_clipboard_send_allowed_data_only(res_tz_clipboard, (uint32_t)0);
}

static const struct tizen_clipboard_interface _tz_clipboard_iface =
{
   _tz_clipboard_cb_destroy,
   _tz_clipboard_cb_show,
   _tz_clipboard_cb_hide,
   _tz_clipboard_cb_data_only_set,
};

static void
_tz_clipboard_cb_unbind(struct wl_resource *res_tz_clipboard)
{
   E_Policy_Wl_Tz_Clipboard *tz_clipboard;

   tz_clipboard = wl_resource_get_user_data(res_tz_clipboard);
   EINA_SAFETY_ON_NULL_RETURN(tz_clipboard);

   _e_policy_wl_tz_clipboard_del(tz_clipboard);
}

static void
_tz_clipboard_cb_bind(struct wl_client *client, void *data EINA_UNUSED, uint32_t ver, uint32_t id)
{
   E_Policy_Wl_Tz_Clipboard *tz_clipboard_pol;
   struct wl_resource *res_tz_clipboard;

   EINA_SAFETY_ON_NULL_GOTO(polwl, err);

   res_tz_clipboard = wl_resource_create(client,
                                         &tizen_clipboard_interface,
                                         ver,
                                         id);
   EINA_SAFETY_ON_NULL_GOTO(res_tz_clipboard, err);

   tz_clipboard_pol = _e_policy_wl_tz_clipboard_add(res_tz_clipboard);
   EINA_SAFETY_ON_NULL_GOTO(tz_clipboard_pol, err);

   wl_resource_set_implementation(res_tz_clipboard,
                                  &_tz_clipboard_iface,
                                  tz_clipboard_pol,
                                  _tz_clipboard_cb_unbind);
   return;

err:
   ERR("Could not create tizen_clipboard_interface res: %m");
   wl_client_post_no_memory(client);
}

EINTERN void
e_policy_wl_clipboard_data_selected_send(E_Client *ec)
{
   E_Policy_Wl_Tz_Clipboard *tz_clipboard;

   EINA_SAFETY_ON_NULL_RETURN(ec);

   tz_clipboard = _e_policy_wl_tz_clipboard_get_from_client(ec);
   EINA_SAFETY_ON_NULL_RETURN(tz_clipboard);

   tizen_clipboard_send_data_selected(tz_clipboard->res_tz_clipboard,
                                      ec->comp_data? ec->comp_data->surface : NULL);
}

static void
_e_policy_wl_cb_hook_shell_surface_ready(void *d, E_Client *ec)
{
   if (EINA_UNLIKELY(!ec))
     return;

   e_policy_client_maximize(ec);

   e_client_base_output_resolution_transform_adjust(ec);

   if ((ec->comp_data->shell.configure_send) &&
       (ec->comp_data->shell.surface))
     {
        int w = 0, h = 0;
        if (ec->lock_client_size)
          {
             w = ec->w;
             h = ec->h;
          }
        ec->comp_data->shell.configure_send(ec->comp_data->shell.surface,
                                            0, w, h);
     }
}

// --------------------------------------------------------
// public functions
// --------------------------------------------------------
void
e_policy_wl_client_add(E_Client *ec)
{
   EINA_SAFETY_ON_NULL_RETURN(ec);
   if (!ec->pixmap) return;

   _e_policy_wl_surf_client_set(ec);
   _e_policy_wl_tzsh_client_set(ec);
   _e_policy_wl_pending_bg_client_set(ec);
   _e_policy_wl_tzlaunch_effect_type_sync(ec);
}

void
e_policy_wl_client_del(E_Client *ec)
{
   EINA_SAFETY_ON_NULL_RETURN(ec);
   if (!ec->pixmap) return;

   e_policy_wl_pixmap_del(ec->pixmap);
   _e_policy_wl_tzsh_client_unset(ec);
   _e_policy_wl_dpy_surf_del(ec);
   _e_policy_wl_tz_indicator_unset_client(ec);
   _e_policy_wl_tz_clipboard_unset_client(ec);
   _launch_effect_client_del(ec);

   polwl->pending_vis = eina_list_remove(polwl->pending_vis, ec);
}

void
e_policy_wl_pixmap_del(E_Pixmap *cp)
{
   E_Policy_Wl_Tzpol *tzpol;
   E_Policy_Wl_Surface *psurf;
   Eina_List *l, *ll;
   Eina_Iterator *it;

   it = eina_hash_iterator_data_new(polwl->tzpols);
   EINA_ITERATOR_FOREACH(it, tzpol)
     EINA_LIST_FOREACH_SAFE(tzpol->psurfs, l, ll, psurf)
       {
          if (psurf->cp != cp) continue;
          tzpol->psurfs = eina_list_remove_list(tzpol->psurfs, l);
          _e_policy_wl_surf_del(psurf);
       }
   eina_iterator_free(it);
}

void
e_policy_wl_aux_message_send(E_Client *ec,
                             const char *key,
                             const char *val,
                             Eina_List *options)
{
   E_Policy_Wl_Tzpol *tzpol;
   E_Policy_Wl_Surface *psurf;
   Eina_List *l;
   Eina_Iterator *it;
   struct wl_array opt_array;
   const char *option;
   int len;
   char *p;

   if (!ec->comp_data) return;
   if (!ec->comp_data->aux_hint.use_msg) return;

   wl_array_init(&opt_array);
   EINA_LIST_FOREACH(options, l, option)
     {
        len = strlen(option) + 1;
        p = wl_array_add(&opt_array, len);

        if (p == NULL)
          break;
        strncpy(p, option, len);
     }

   it = eina_hash_iterator_data_new(polwl->tzpols);
   EINA_ITERATOR_FOREACH(it, tzpol)
      EINA_LIST_FOREACH(tzpol->psurfs, l, psurf)
        {
           if (e_pixmap_client_get(psurf->cp) != ec) continue;
           if (!psurf->surf) continue;

           tizen_policy_send_aux_message(tzpol->res_tzpol,
                                         psurf->surf,
                                         key, val, &opt_array);
          ELOGF("TZPOL",
                "SEND     |res_tzpol:%8p|aux message key:%s val:%s opt_count:%d",
                ec,
                tzpol->res_tzpol,
                key, val, eina_list_count(options));
        }
   eina_iterator_free(it);
   wl_array_release(&opt_array);
}

void
e_policy_wl_aux_hint_init(void)
{
   int i, n;
   E_Config_Aux_Hint_Supported *auxhint;
   Eina_List *l;

   n = (sizeof(hint_names) / sizeof(char *));

   for (i = 0; i < n; i++)
     {
        e_hints_aux_hint_supported_add(hint_names[i]);
     }

   EINA_LIST_FOREACH(e_config->aux_hint_supported, l, auxhint)
     {
        if (!auxhint->name) continue;
        e_hints_aux_hint_supported_add(auxhint->name);
     }

   return;
}

Eina_Bool
e_policy_wl_defer_job(void)
{
   struct wl_global *global = NULL;
   EINA_SAFETY_ON_NULL_GOTO(polwl, err);

   global = wl_global_create(e_comp_wl->wl.disp,
                             &tizen_launch_effect_interface,
                             1,
                             NULL,
                             _tzlaunch_effect_cb_bind);
   EINA_SAFETY_ON_NULL_GOTO(global, err);

   polwl->globals = eina_list_append(polwl->globals, global);

   if (e_config->configured_output_resolution.use)
     {
        global = wl_global_create(e_comp_wl->wl.disp,
                                  &tizen_launch_appinfo_interface,
                                  1,
                                  NULL,
                                  _tzlaunch_appinfo_cb_bind);
        EINA_SAFETY_ON_NULL_GOTO(global, err);

        polwl->globals = eina_list_append(polwl->globals, global);
     }

   return EINA_TRUE;

err:
   return EINA_FALSE;
}

#undef E_COMP_WL_HOOK_APPEND
#define E_COMP_WL_HOOK_APPEND(l, t, cb, d) \
  do                                      \
    {                                     \
       E_Comp_Wl_Hook *_h;                 \
       _h = e_comp_wl_hook_add(t, cb, d);  \
       assert(_h);                        \
       l = eina_list_append(l, _h);       \
    }                                     \
  while (0)

#undef E_COMP_OBJECT_INTERCEPT_HOOK_APPEND
#define E_COMP_OBJECT_INTERCEPT_HOOK_APPEND(l, t, cb, d) \
  do                                                     \
    {                                                    \
       E_Comp_Object_Intercept_Hook *_h;                 \
       _h = e_comp_object_intercept_hook_add(t, cb, d);  \
       assert(_h);                                       \
       l = eina_list_append(l, _h);                      \
    }                                                    \
  while (0)

Eina_Bool
e_policy_wl_init(void)
{
   struct wl_global *global;

   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp_wl, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp_wl->wl.disp, EINA_FALSE);

   polwl = E_NEW(E_Policy_Wl, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(polwl, EINA_FALSE);

   /* create globals */
   global = wl_global_create(e_comp_wl->wl.disp,
                             &tizen_policy_interface,
                             7,
                             NULL,
                             _tzpol_cb_bind);
   EINA_SAFETY_ON_NULL_GOTO(global, err);
   polwl->globals = eina_list_append(polwl->globals, global);

   global = wl_global_create(e_comp_wl->wl.disp,
                             &tizen_display_policy_interface,
                             1,
                             NULL,
                             _tz_dpy_pol_cb_bind);
   EINA_SAFETY_ON_NULL_GOTO(global, err);
   polwl->globals = eina_list_append(polwl->globals, global);

   global = wl_global_create(e_comp_wl->wl.disp,
                             &tizen_ws_shell_interface,
                             3,
                             NULL,
                             _tzsh_cb_bind);

   EINA_SAFETY_ON_NULL_GOTO(global, err);
   polwl->globals = eina_list_append(polwl->globals, global);

   global = wl_global_create(e_comp_wl->wl.disp,
                             &tizen_indicator_interface,
                             1,
                             NULL,
                             _tz_indicator_cb_bind);
   EINA_SAFETY_ON_NULL_GOTO(global, err);
   polwl->globals = eina_list_append(polwl->globals, global);

   global = wl_global_create(e_comp_wl->wl.disp,
                             &tizen_clipboard_interface,
                             2,
                             NULL,
                             _tz_clipboard_cb_bind);
   EINA_SAFETY_ON_NULL_GOTO(global, err);
   polwl->globals = eina_list_append(polwl->globals, global);

   polwl->tzpols = eina_hash_pointer_new(_e_policy_wl_tzpol_del);

   E_COMP_OBJECT_INTERCEPT_HOOK_APPEND(hooks_co, E_COMP_OBJECT_INTERCEPT_HOOK_SHOW_HELPER, _e_policy_wl_cb_hook_intercept_show_helper, NULL);

   E_LIST_HANDLER_APPEND(handlers, E_EVENT_SCREENSAVER_ON,  _e_policy_wl_cb_scrsaver_on,  NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_SCREENSAVER_OFF, _e_policy_wl_cb_scrsaver_off, NULL);

   E_COMP_WL_HOOK_APPEND(hooks_cw, E_COMP_WL_HOOK_SHELL_SURFACE_READY, _e_policy_wl_cb_hook_shell_surface_ready, NULL);

   E_EVENT_POLICY_INDICATOR_STATE_CHANGE = ecore_event_type_new();
   E_EVENT_POLICY_INDICATOR_OPACITY_MODE_CHANGE = ecore_event_type_new();
   E_EVENT_POLICY_INDICATOR_VISIBLE_STATE_CHANGE = ecore_event_type_new();

   e_policy_display_init();

   return EINA_TRUE;

err:
   if (polwl)
     {
        EINA_LIST_FREE(polwl->globals, global)
          wl_global_destroy(global);

        E_FREE(polwl);
     }
   return EINA_FALSE;
}

void
e_policy_wl_shutdown(void)
{
   E_Policy_Wl_Tzsh *tzsh;
   E_Policy_Wl_Tzsh_Srv *tzsh_srv;
   E_Policy_Wl_Tzsh_Extension *tzsh_extension;
   E_Policy_Wl_Tzlaunch_Effect_Info *effect_info;
   E_Policy_Wl_Tz_Dpy_Pol *tz_dpy_pol;
   E_Policy_Wl_Tz_Indicator *tz_indicator;
   struct wl_global *global;
   int i;

   e_policy_display_shutdown();

   EINA_SAFETY_ON_NULL_RETURN(polwl);

   E_FREE_LIST(hooks_cw, e_comp_wl_hook_del);
   E_FREE_LIST(hooks_co, e_comp_object_intercept_hook_del);
   E_FREE_LIST(handlers, ecore_event_handler_del);

   polwl->pending_vis = eina_list_free(polwl->pending_vis);

   for (i = 0; i < TZSH_SRV_ROLE_MAX; i++)
     {
        tzsh_srv = polwl->srvs[i];
        if (!tzsh_srv) continue;

        wl_resource_destroy(tzsh_srv->res_tzsh_srv);
     }

   EINA_LIST_FREE(polwl->tzshs, tzsh)
     wl_resource_destroy(tzsh->res_tzsh);

   EINA_LIST_FREE(polwl->tz_dpy_pols, tz_dpy_pol)
     {
        E_Policy_Wl_Dpy_Surface *dpy_surf;
        EINA_LIST_FREE(tz_dpy_pol->dpy_surfs, dpy_surf)
          {
             E_FREE(dpy_surf);
          }
        wl_resource_destroy(tz_dpy_pol->res_tz_dpy_pol);
     }

   EINA_LIST_FREE(polwl->tzlaunch_effect_info, effect_info)
     {
        E_FREE(effect_info);
     }

   EINA_LIST_FREE(polwl->tz_indicators, tz_indicator)
     {
        eina_list_free(tz_indicator->ec_list);
        wl_resource_destroy(tz_indicator->res_tz_indicator);
     }

   EINA_LIST_FREE(polwl->tzsh_extensions, tzsh_extension)
     {
        free(tzsh_extension->name);
     }

   EINA_LIST_FREE(polwl->globals, global)
     wl_global_destroy(global);

   E_FREE_FUNC(polwl->tzpols, eina_hash_free);

   E_FREE(polwl);
}
