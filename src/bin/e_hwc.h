#ifdef E_TYPEDEFS

typedef struct _E_Hwc     E_Hwc;

#define HWC_NAME_LEN 64

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

/*The hwc value type enumeration */
typedef enum {
	HWC_VALUE_TYPE_UNKNOWN,
	HWC_VALUE_TYPE_PTR,
	HWC_VALUE_TYPE_INT32,
	HWC_VALUE_TYPE_UINT32,
	HWC_VALUE_TYPE_INT64,
	HWC_VALUE_TYPE_UINT64,
} hwc_value_type;

/*brief The hwc value union */
typedef union {
	void	 *ptr;
	int32_t  s32;
	uint32_t u32;
	int64_t  s64;
	uint64_t u64;
} hwc_value;

/* The property of the hwc client */
typedef struct _hwc_prop {
	unsigned int id;
	char name[HWC_NAME_LEN];
	hwc_value_type type;
} hwc_prop;

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
   Evas                *evas;

   Eina_Bool            primary_output;

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
   Eina_Bool            device_state_available;
   Eina_Bool            transition;

   /* capabilities */
   Eina_Bool     tdm_hwc_video_stream;
   Eina_Bool     tdm_hwc_video_scale;
   Eina_Bool     tdm_hwc_video_transform;
   Eina_Bool     tdm_hwc_video_scanout;

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
   Eina_Bool             pp_unset;
   Eina_Bool             pp_commit;
   Eina_Bool             pp_output_commit;
   E_Hwc_Window_Commit_Data  *pp_output_commit_data;
   Eina_Rectangle        pp_rect;
   E_Hwc_Window         *pp_hwc_window;

   /* external output */
   Eina_Rectangle       mirror_rect;
   E_Hwc               *mirror_src_hwc;
   tbm_surface_h        mirror_src_tsurface;
   Eina_List           *mirror_dst_hwc;
   E_Hwc_Window        *presentation_hwc_window;

   int                  norender;

   /* for fps */
   double               fps;
   double               old_fps;
   double               frametimes[122];
   double               time;
   double               lapse;
   int                  cframes;
   int                  flapse;
};

EINTERN E_Hwc                *e_hwc_new(E_Output *output, Eina_Bool primary_output);
EINTERN void                  e_hwc_del(E_Hwc *hwc);
EINTERN E_Hwc_Mode            e_hwc_mode_get(E_Hwc *hwc);
EINTERN void                  e_hwc_deactive_set(E_Hwc *hwc, Eina_Bool set);
EINTERN Eina_Bool             e_hwc_deactive_get(E_Hwc *hwc);
EINTERN Eina_Bool             e_hwc_client_is_above_hwc(E_Client *ec, E_Client *hwc_ec);
EINTERN char                 *e_hwc_output_id_get(E_Hwc *hwc);
EINTERN tbm_surface_queue_h   e_hwc_tbm_surface_queue_get(E_Hwc *hwc);

EINTERN void                  e_hwc_norender_push(E_Hwc *hwc);
EINTERN void                  e_hwc_norender_pop(E_Hwc *hwc);
EINTERN int                   e_hwc_norender_get(E_Hwc *hwc);

EINTERN Eina_Bool             e_hwc_intercept_hook_call(E_Hwc_Intercept_Hook_Point hookpoint, E_Hwc *hwc);

E_API E_Hwc_Intercept_Hook   *e_hwc_intercept_hook_add(E_Hwc_Intercept_Hook_Point hookpoint, E_Hwc_Intercept_Hook_Cb func, const void *data);
E_API void                    e_hwc_intercept_hook_del(E_Hwc_Intercept_Hook *ch);

E_API E_Hwc_Policy            e_hwc_policy_get(E_Hwc *hwc);

E_API Eina_Bool               e_hwc_available_properties_get(E_Hwc *hwc, const hwc_prop **props, int *count);
E_API Eina_Bool               e_hwc_property_get(E_Hwc *hwc, unsigned int id, hwc_value *value);
E_API Eina_Bool               e_hwc_property_set(E_Hwc *hwc, unsigned int id, hwc_value value);

E_API Eina_Bool               e_client_hwc_available_properties_get(E_Client *ec, const hwc_prop **props, int *count);
E_API Eina_Bool               e_client_hwc_property_get(E_Client *ec, unsigned int id, hwc_value *value);
E_API Eina_Bool               e_client_hwc_property_set(E_Client *ec, unsigned int id, hwc_value value);

#endif
#endif
