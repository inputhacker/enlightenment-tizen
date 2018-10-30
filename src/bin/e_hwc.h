#ifdef E_TYPEDEFS

typedef struct _E_Hwc     E_Hwc;

typedef enum _E_Hwc_Mode
{
   E_HWC_MODE_NONE = 0,
   E_HWC_MODE_HYBRID,
   E_HWC_MODE_FULL
} E_Hwc_Mode;

typedef enum _E_Hwc_Policy
{
   E_HWC_POLICY_NONE = 0,
   E_HWC_POLICY_PLANES,   // hwc_planes policy that controls the hwc policy at e20 with e_planes
   E_HWC_POLICY_WINDOWS,  // hwc_windows policy that controls the hwc policy at tdm-backend with e_hwc_windows
} E_Hwc_Policy;

typedef enum _E_Hwc_Intercept_Hook_Point
{
   E_HWC_INTERCEPT_HOOK_PREPARE_PLANE,
   E_HWC_INTERCEPT_HOOK_END_ALL_PLANE,
   E_HWC_INTERCEPT_HOOK_LAST,
} E_Hwc_Intercept_Hook_Point;

typedef Eina_Bool (*E_Hwc_Intercept_Hook_Cb)(void *data, E_Hwc *hwc);
typedef struct _E_Hwc_Intercept_Hook E_Hwc_Intercept_Hook;

#else
#ifndef E_HWC_H
#define E_HWC_H

struct _E_Hwc_Intercept_Hook
{
   EINA_INLIST;
   E_Hwc_Intercept_Hook_Point  hookpoint;
   E_Hwc_Intercept_Hook_Cb     func;
   void                       *data;
   unsigned char               delete_me : 1;
};

struct _E_Hwc
{
   E_Output            *output;

   E_Hwc_Policy         hwc_policy;
   E_Hwc_Mode           hwc_mode;
   Eina_Bool            hwc_deactive : 1; // deactive hwc policy

   Ecore_Evas          *ee;

   /* variables for hwc_planes polic  */
   Eina_Bool            hwc_use_multi_plane;

   /* variables for hwc_windows policy  */
   tdm_hwc             *thwc;
   Eina_Bool            hwc_wins;
   Eina_List           *hwc_windows;
   E_Hwc_Window_Target *target_hwc_window;
   tbm_surface_queue_h  target_buffer_queue;
   Eina_Bool            wait_commit;
   Eina_List           *visible_windows;
   int                  num_visible_windows;

   Eina_Bool            intercept_pol;

   /* variables for pp at hwc_windows policy */
   tdm_pp               *tpp;
   Eina_List            *pp_hwc_window_list;
   Eina_List            *pending_pp_hwc_window_list;
   Eina_List            *pending_pp_commit_data_list;
   tbm_surface_queue_h   pp_tqueue;
   tbm_surface_h         pp_tsurface;
   Eina_Bool             pp_set_info;
   Eina_Bool             pp_set;
   Eina_Bool             pp_commit;
   Eina_Bool             pp_output_commit;
   E_Hwc_Window_Commit_Data  *pp_output_commit_data;
   Eina_Rectangle        pp_rect;
};

EINTERN E_Hwc        *e_hwc_new(E_Output *output);
EINTERN void          e_hwc_del(E_Hwc *hwc);
EINTERN E_Hwc_Policy  e_hwc_policy_get(E_Hwc *hwc);
EINTERN E_Hwc_Mode    e_hwc_mode_get(E_Hwc *hwc);
EINTERN void          e_hwc_deactive_set(E_Hwc *hwc, Eina_Bool set);
EINTERN Eina_Bool     e_hwc_deactive_get(E_Hwc *hwc);
EINTERN Eina_Bool     e_hwc_client_is_above_hwc(E_Client *ec, E_Client *hwc_ec);

EINTERN Eina_Bool     e_hwc_intercept_hook_call(E_Hwc_Intercept_Hook_Point hookpoint, E_Hwc *hwc);

E_API E_Hwc_Intercept_Hook *e_hwc_intercept_hook_add(E_Hwc_Intercept_Hook_Point hookpoint, E_Hwc_Intercept_Hook_Cb func, const void *data);
E_API void e_hwc_intercept_hook_del(E_Hwc_Intercept_Hook *ch);

#endif
#endif
