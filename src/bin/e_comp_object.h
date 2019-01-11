#ifdef E_TYPEDEFS
typedef struct E_Comp_Object_Frame E_Comp_Object_Frame;
typedef struct E_Event_Comp_Object E_Event_Comp_Object;
typedef void (*E_Comp_Object_Autoclose_Cb)(void *, Evas_Object *);
typedef Eina_Bool (*E_Comp_Object_Key_Cb)(void *, Ecore_Event_Key *);
typedef Eina_Bool (*E_Comp_Object_Mover_Cb) (void *data, Evas_Object *comp_object, const char *signal);

typedef struct E_Comp_Object_Mover E_Comp_Object_Mover;

typedef enum
{
   E_COMP_OBJECT_TYPE_NONE,
   E_COMP_OBJECT_TYPE_MENU,
   E_COMP_OBJECT_TYPE_POPUP,
   E_COMP_OBJECT_TYPE_LAST,
} E_Comp_Object_Type;

typedef enum
{
   E_COMP_OBJECT_CONTENT_TYPE_NONE,
   E_COMP_OBJECT_CONTENT_TYPE_INT_IMAGE,
   E_COMP_OBJECT_CONTENT_TYPE_EXT_IMAGE,
   E_COMP_OBJECT_CONTENT_TYPE_EXT_EDJE,
   E_COMP_OBJECT_CONTENT_TYPE_LAST,
} E_Comp_Object_Content_Type;

#else
#ifndef E_COMP_OBJECT_H
#define E_COMP_OBJECT_H

#define E_COMP_OBJECT_FRAME_RESHADOW "COMP_RESHADOW"

struct E_Event_Comp_Object
{
   Evas_Object *comp_object;
};

struct E_Comp_Object_Frame
{
   int l, r, t, b;
   Eina_Bool calc : 1; // inset has been calculated
};

typedef struct _E_Comp_Object_Hook E_Comp_Object_Hook;

typedef enum _E_Comp_Object_Hook_Point
{
   E_COMP_OBJECT_HOOK_EFFECT_START,
   E_COMP_OBJECT_HOOK_EFFECT_END,
   E_COMP_OBJECT_HOOK_RESTACK,
   E_COMP_OBJECT_HOOK_LAST,
} E_Comp_Object_Hook_Point;

typedef Eina_Bool (*E_Comp_Object_Hook_Cb)(void *data, E_Client *ec);

struct _E_Comp_Object_Hook
{
   EINA_INLIST;
   E_Comp_Object_Hook_Point hookpoint;
   E_Comp_Object_Hook_Cb func;
   void *data;
   unsigned char delete_me : 1;
};

#ifdef _F_E_COMP_OBJECT_INTERCEPT_HOOK_
typedef struct _E_Comp_Object_Intercept_Hook E_Comp_Object_Intercept_Hook;

typedef enum _E_Comp_Object_Intercept_Hook_Point
{
   E_COMP_OBJECT_INTERCEPT_HOOK_SHOW_HELPER,
   E_COMP_OBJECT_INTERCEPT_HOOK_HIDE,
   E_COMP_OBJECT_INTERCEPT_HOOK_LAST,
} E_Comp_Object_Intercept_Hook_Point;

typedef Eina_Bool (*E_Comp_Object_Intercept_Hook_Cb)(void *data, E_Client *ec);

struct _E_Comp_Object_Intercept_Hook
{
   EINA_INLIST;
   E_Comp_Object_Intercept_Hook_Point hookpoint;
   E_Comp_Object_Intercept_Hook_Cb func;
   void               *data;
   unsigned char       delete_me : 1;
};
#endif

extern E_API int E_EVENT_COMP_OBJECT_ADD;

E_API void e_comp_object_zoomap_set(Evas_Object *obj, Eina_Bool enabled);
E_API Eina_Bool e_comp_object_mirror_visibility_check(Evas_Object *obj);
E_API Evas_Object *e_comp_object_client_add(E_Client *ec);
E_API Evas_Object *e_comp_object_util_mirror_add(Evas_Object *obj);
E_API Evas_Object *e_comp_object_util_add(Evas_Object *obj, E_Comp_Object_Type type);
E_API void e_comp_object_frame_xy_adjust(Evas_Object *obj, int x, int y, int *ax, int *ay);
E_API void e_comp_object_frame_xy_unadjust(Evas_Object *obj, int x, int y, int *ax, int *ay);
E_API void e_comp_object_frame_wh_adjust(Evas_Object *obj, int w, int h, int *aw, int *ah);
E_API void e_comp_object_frame_wh_unadjust(Evas_Object *obj, int w, int h, int *aw, int *ah);
E_API void e_comp_object_frame_extends_get(Evas_Object *obj, int *x, int *y, int *w, int *h);
E_API E_Client *e_comp_object_client_get(Evas_Object *obj);
E_API E_Zone *e_comp_object_util_zone_get(Evas_Object *obj);
E_API void e_comp_object_util_del_list_append(Evas_Object *obj, Evas_Object *to_del);
E_API void e_comp_object_util_del_list_remove(Evas_Object *obj, Evas_Object *to_del);
E_API void e_comp_object_util_autoclose(Evas_Object *obj, E_Comp_Object_Autoclose_Cb del_cb, E_Comp_Object_Key_Cb cb, const void *data);
E_API void e_comp_object_util_center(Evas_Object *obj);
E_API void e_comp_object_util_center_on(Evas_Object *obj, Evas_Object *on);
E_API void e_comp_object_util_center_pos_get(Evas_Object *obj, int *x, int *y);
E_API void e_comp_object_util_fullscreen(Evas_Object *obj);
E_API Eina_Bool e_comp_object_frame_allowed(Evas_Object *obj);
E_API void e_comp_object_frame_geometry_get(Evas_Object *obj, int *l, int *r, int *t, int *b);
E_API void e_comp_object_frame_geometry_set(Evas_Object *obj, int l, int r, int t, int b);
E_API Eina_Bool e_comp_object_frame_title_set(Evas_Object *obj, const char *name);
E_API Eina_Bool e_comp_object_frame_exists(Evas_Object *obj);
E_API Eina_Bool e_comp_object_frame_theme_set(Evas_Object *obj, const char *name);
E_API void e_comp_object_signal_emit(Evas_Object *obj, const char *sig, const char *src);
E_API void e_comp_object_signal_callback_add(Evas_Object *obj, const char *sig, const char *src, Edje_Signal_Cb cb, const void *data);
E_API void e_comp_object_signal_callback_del(Evas_Object *obj, const char *sig, const char *src, Edje_Signal_Cb cb);
E_API void e_comp_object_signal_callback_del_full(Evas_Object *obj, const char *sig, const char *src, Edje_Signal_Cb cb, const void *data);
E_API void e_comp_object_input_objs_del(Evas_Object *obj);
E_API void e_comp_object_input_area_set(Evas_Object *obj, int x, int y, int w, int h);
EINTERN void e_comp_object_input_rect_get(Evas_Object *obj, Eina_List **list);
E_API void e_comp_object_damage(Evas_Object *obj, int x, int y, int w, int h);
E_API Eina_Bool e_comp_object_damage_exists(Evas_Object *obj);
E_API void e_comp_object_render_update_add(Evas_Object *obj);
E_API void e_comp_object_render_update_del(Evas_Object *obj);
E_API void e_comp_object_shape_apply(Evas_Object *obj);
E_API void e_comp_object_redirected_set(Evas_Object *obj, Eina_Bool set);
E_API void e_comp_object_native_surface_set(Evas_Object *obj, Eina_Bool set);
E_API void e_comp_object_native_surface_override(Evas_Object *obj, Evas_Native_Surface *ns);
E_API void e_comp_object_blank(Evas_Object *obj, Eina_Bool set);
E_API void e_comp_object_dirty(Evas_Object *obj);
E_API Eina_Bool e_comp_object_render(Evas_Object *obj);
E_API Eina_Bool e_comp_object_effect_allowed_get(Evas_Object *obj);
E_API Eina_Bool e_comp_object_effect_set(Evas_Object *obj, const char *effect);
E_API void e_comp_object_effect_params_set(Evas_Object *obj, int id, int *params, unsigned int count);
E_API void e_comp_object_effect_clip(Evas_Object *obj);
E_API void e_comp_object_effect_unclip(Evas_Object *obj);
E_API Eina_Bool e_comp_object_effect_start(Evas_Object *obj, Edje_Signal_Cb end_cb, const void *end_data);
E_API Eina_Bool e_comp_object_effect_stop(Evas_Object *obj, Edje_Signal_Cb end_cb);
E_API E_Comp_Object_Mover *e_comp_object_effect_mover_add(int pri, const char *sig, E_Comp_Object_Mover_Cb provider, const void *data);
E_API void e_comp_object_effect_mover_del(E_Comp_Object_Mover *prov);
E_API Evas_Object *e_comp_object_effect_object_get(Evas_Object *obj);
E_API E_Comp_Object_Hook *e_comp_object_hook_add(E_Comp_Object_Hook_Point hookpoint, E_Comp_Object_Hook_Cb func, const void *data);
E_API void e_comp_object_hook_del(E_Comp_Object_Hook *ch);

#ifdef _F_E_COMP_OBJECT_INTERCEPT_HOOK_
E_API E_Comp_Object_Intercept_Hook *e_comp_object_intercept_hook_add(E_Comp_Object_Intercept_Hook_Point hookpoint, E_Comp_Object_Intercept_Hook_Cb func, const void *data);
E_API void e_comp_object_intercept_hook_del(E_Comp_Object_Intercept_Hook *ch);
#endif
E_API unsigned int e_comp_object_is_animating(Evas_Object *obj);
E_API void e_comp_object_alpha_set(Evas_Object *obj, Eina_Bool alpha);
E_API Eina_Bool e_comp_object_alpha_get(Evas_Object *obj);
E_API void e_comp_object_mask_set(Evas_Object *obj, Eina_Bool set);
E_API Eina_Bool e_comp_object_mask_has(Evas_Object *obj);
E_API void e_comp_object_size_update(Evas_Object *obj, int w, int h);
E_API void e_comp_object_transform_bg_set(Evas_Object *obj, Eina_Bool set);
E_API void e_comp_object_transform_bg_vertices_set(Evas_Object *obj, E_Util_Transform_Rect_Vertex *vertices);
E_API void e_comp_object_transform_transp_set(Evas_Object *obj, Eina_Bool set);
E_API void e_comp_object_transform_transp_vertices_set(Evas_Object *obj, E_Util_Transform_Rect_Vertex *vertices);

E_API void e_comp_object_layer_update(Evas_Object *obj, Evas_Object *above, Evas_Object *below);

E_API Eina_Bool                  e_comp_object_content_set(Evas_Object* obj, Evas_Object *content, E_Comp_Object_Content_Type type);
E_API Eina_Bool                  e_comp_object_content_unset(Evas_Object* obj);
E_API Evas_Object               *e_comp_object_content_get(Evas_Object* obj);
E_API E_Comp_Object_Content_Type e_comp_object_content_type_get(Evas_Object* obj);

E_API void e_comp_object_dim_client_set(E_Client *ec);
E_API E_Client *e_comp_object_dim_client_get(void);
E_API void e_comp_object_dim_mask_update(Evas_Object *obj, Eina_Bool mask_set, int x, int y, int w, int h);
E_API void e_comp_object_dim_mask_set(Evas_Object *obj, Eina_Bool set);
E_API void      e_comp_object_clear(Evas_Object *obj);

E_API Eina_Bool e_comp_object_hwc_update_exists(Evas_Object *obj);
E_API void e_comp_object_hwc_update_set(Evas_Object *obj, Eina_Bool set);

E_API Eina_Bool e_comp_object_effect_object_part_swallow(Evas_Object *obj, const char *part_name, Evas_Object *swallow_obj);

E_API void e_comp_object_indicator_swallow(Evas_Object *obj, Evas_Object *indicator);
E_API void e_comp_object_indicator_unswallow(Evas_Object *obj, Evas_Object *indicator);
E_API void e_comp_object_indicator_size_set(Evas_Object *obj, int w, int h);

E_API void e_comp_object_map_update(Evas_Object *obj);

E_API Eina_Bool e_comp_object_native_usable_get(Evas_Object *obj);
#endif
#endif

