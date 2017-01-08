#ifdef E_TYPEDEFS

typedef enum _E_Zone_Edge
{
   E_ZONE_EDGE_NONE,
   E_ZONE_EDGE_LEFT,
   E_ZONE_EDGE_RIGHT,
   E_ZONE_EDGE_TOP,
   E_ZONE_EDGE_BOTTOM,
   E_ZONE_EDGE_TOP_LEFT,
   E_ZONE_EDGE_TOP_RIGHT,
   E_ZONE_EDGE_BOTTOM_RIGHT,
   E_ZONE_EDGE_BOTTOM_LEFT
} E_Zone_Edge;

typedef enum _E_Zone_Display_State
{
   E_ZONE_DISPLAY_STATE_OFF,
   E_ZONE_DISPLAY_STATE_ON,
} E_Zone_Display_State;

typedef struct _E_Zone                      E_Zone;

typedef struct _E_Event_Zone_Generic        E_Event_Zone_Desk_Count_Set;
typedef struct _E_Event_Zone_Generic        E_Event_Zone_Move_Resize;
typedef struct _E_Event_Zone_Generic        E_Event_Zone_Add;
typedef struct _E_Event_Zone_Generic        E_Event_Zone_Del;
/* TODO: Move this to a general place? */
typedef struct _E_Event_Pointer_Warp        E_Event_Pointer_Warp;
typedef struct _E_Event_Zone_Edge           E_Event_Zone_Edge;
typedef struct _E_Event_Zone_Generic        E_Event_Zone_Stow;
typedef struct _E_Event_Zone_Generic        E_Event_Zone_Unstow;
#ifdef _F_ZONE_WINDOW_ROTATION_
typedef struct _E_Event_Zone_Rotation_Change_Begin  E_Event_Zone_Rotation_Change_Begin;
typedef struct _E_Event_Zone_Rotation_Change_Cancel E_Event_Zone_Rotation_Change_Cancel;
typedef struct _E_Event_Zone_Rotation_Change_End    E_Event_Zone_Rotation_Change_End;
typedef struct _E_Event_Zone_Generic                E_Event_Zone_Rotation_Effect_Ready;
typedef struct _E_Event_Zone_Generic                E_Event_Zone_Rotation_Effect_Cancel;
typedef struct _E_Event_Zone_Generic                E_Event_Zone_Rotation_Effect_Done;
#endif
typedef struct _E_Event_Zone_Display_State_Change   E_Event_Zone_Display_State_Change;

#else
#ifndef E_ZONE_H
#define E_ZONE_H

typedef Eina_Bool (*E_Zone_Cb_Orientation_Block_Set)(E_Zone *zone, const char* name_hint, Eina_Bool set);
typedef void      (*E_Zone_Cb_Orientation_Force_Update_Add)(E_Zone *zone, E_Client *client);
typedef void      (*E_Zone_Cb_Orientation_Force_Update_Del)(E_Zone *zone, E_Client *client);

#define E_ZONE_TYPE (int)0xE0b0100d

struct _E_Zone
{
   E_Object     e_obj_inherit;

   int          x, y, w, h;
   const char  *name;
   /* num matches the id of the xinerama screen
    * this zone belongs to. */
   unsigned int num;
   int          fullscreen;

   Evas_Object *bg_object;
   Evas_Object *bg_event_object;
   Evas_Object *bg_clip_object;
   Evas_Object *prev_bg_object;
   Evas_Object *transition_object;

   int          desk_x_count, desk_y_count;
   int          desk_x_current, desk_y_current;
   int          desk_x_prev, desk_y_prev;
   E_Desk     **desks;

   Eina_List   *handlers;

   /* formerly E_Comp_Zone */
   Evas_Object *base;
   Evas_Object *over;
   //double       bl;    // backlight level
   //Eina_Bool    bloff; // backlight is off

   struct
   {
      E_Zone_Edge        switching;
      //E_Shelf           *es;
      E_Event_Zone_Edge *ev;
      E_Binding_Edge    *bind;
   } flip;

   struct
   {
      Evas_Object *top, *right, *bottom, *left;
   } edge;
   struct
   {
      Evas_Object *left_top, *top_left, *top_right, *right_top,
                  *right_bottom, *bottom_right, *bottom_left, *left_bottom;
   } corner;

   E_Action      *cur_mouse_action;

   int            id;

   Eina_Bool      stowed : 1;
#ifdef _F_ZONE_WINDOW_ROTATION_
   struct
   {
      int       prev, curr, next, sub;
      int       block_count; /* deprecated. use rot.block.mod_count instead */
      struct
      {
         Eina_Bool app_hint; /* true: rotation is NOT blocked for the specific app which set special hint */
      } unblock;
      struct
      {
         Eina_Bool sys_auto_rot; /* true: system auto rotation is disabled */
         int       mod_count;    /* 1 or higher: temporary block count for the E sub-modules */
      } block;
      Eina_Bool wait_for_done : 1;
      Eina_Bool pending : 1;
      Eina_Bool unknown_state : 1;
   } rot;
#endif

   struct
   {
      E_Zone_Cb_Orientation_Block_Set block_set;
      E_Zone_Cb_Orientation_Force_Update_Add force_update_add;
      E_Zone_Cb_Orientation_Force_Update_Del force_update_del;
   } orientation;

   E_Zone_Display_State display_state;
   char                 *output_id; // same id we get from e_comp_screen so look it up there
};

struct _E_Event_Zone_Generic
{
   E_Zone *zone;
};

struct _E_Event_Pointer_Warp
{
   struct
   {
      int x, y;
   } prev;
   struct
   {
      int x, y;
   } curr;
};

struct _E_Event_Zone_Edge
{
   E_Zone     *zone;
   E_Zone_Edge edge;
   int         x, y;
   int         modifiers;
   int         button;
   Eina_Bool  drag : 1;
};

#ifdef _F_ZONE_WINDOW_ROTATION_
struct _E_Event_Zone_Rotation_Change_Begin
{
   E_Zone     *zone;
};

struct _E_Event_Zone_Rotation_Change_Cancel
{
   E_Zone     *zone;
};

struct _E_Event_Zone_Rotation_Change_End
{
   E_Zone     *zone;
};
#endif

struct _E_Event_Zone_Display_State_Change
{
   E_Zone *zone;
};

EINTERN int    e_zone_init(void);
EINTERN int    e_zone_shutdown(void);
E_API E_Zone   *e_zone_new(int num, int id, int x, int y, int w, int h);
E_API void      e_zone_name_set(E_Zone *zone, const char *name);
E_API void      e_zone_move(E_Zone *zone, int x, int y);
E_API void      e_zone_resize(E_Zone *zone, int w, int h);
E_API Eina_Bool  e_zone_move_resize(E_Zone *zone, int x, int y, int w, int h);
E_API E_Zone   *e_zone_current_get(void);
E_API void      e_zone_bg_reconfigure(E_Zone *zone);
E_API void      e_zone_flip_coords_handle(E_Zone *zone, int x, int y);
E_API void      e_zone_desk_count_set(E_Zone *zone, int x_count, int y_count);
E_API void      e_zone_desk_count_get(E_Zone *zone, int *x_count, int *y_count);
E_API void      e_zone_desk_flip_by(E_Zone *zone, int dx, int dy);
E_API void      e_zone_desk_flip_to(E_Zone *zone, int x, int y);
E_API void      e_zone_desk_linear_flip_by(E_Zone *zone, int dx);
E_API void      e_zone_desk_linear_flip_to(E_Zone *zone, int x);
E_API void      e_zone_edge_flip_eval(E_Zone *zone);
E_API void      e_zone_edge_new(E_Zone_Edge edge);
E_API void      e_zone_edge_free(E_Zone_Edge edge);
E_API void      e_zone_edge_enable(void);
E_API void      e_zone_edge_disable(void);
E_API void      e_zone_edges_desk_flip_capable(E_Zone *zone, Eina_Bool l, Eina_Bool r, Eina_Bool t, Eina_Bool b);
E_API Eina_Bool e_zone_exists_direction(E_Zone *zone, E_Zone_Edge edge);
E_API void      e_zone_edge_win_layer_set(E_Zone *zone, E_Layer layer);

E_API void      e_zone_useful_geometry_dirty(E_Zone *zone);
E_API void      e_zone_useful_geometry_get(E_Zone *zone, int *x, int *y, int *w, int *h);
E_API void      e_zone_stow(E_Zone *zone);
E_API void      e_zone_unstow(E_Zone *zone);

E_API void      e_zone_fade_handle(E_Zone *zone, int out, double tim);

E_API void                 e_zone_display_state_set(E_Zone *zone, E_Zone_Display_State state);
E_API E_Zone_Display_State e_zone_display_state_get(E_Zone *zone);

E_API void      e_zone_orientation_callback_set(E_Zone *zone, E_Zone_Cb_Orientation_Block_Set block_set, E_Zone_Cb_Orientation_Force_Update_Add force_update_add, E_Zone_Cb_Orientation_Force_Update_Del force_update_del);
E_API Eina_Bool e_zone_orientation_block_set(E_Zone *zone, const char *name_hint, Eina_Bool set);
E_API void      e_zone_orientation_force_update_add(E_Zone *zone, E_Client *client);
E_API void      e_zone_orientation_force_update_del(E_Zone *zone, E_Client *client);

extern E_API int E_EVENT_ZONE_DESK_COUNT_SET;
extern E_API int E_EVENT_ZONE_MOVE_RESIZE;
extern E_API int E_EVENT_ZONE_ADD;
extern E_API int E_EVENT_ZONE_DEL;
extern E_API int E_EVENT_POINTER_WARP;
extern E_API int E_EVENT_ZONE_EDGE_IN;
extern E_API int E_EVENT_ZONE_EDGE_OUT;
extern E_API int E_EVENT_ZONE_EDGE_MOVE;
extern E_API int E_EVENT_ZONE_STOW;
extern E_API int E_EVENT_ZONE_UNSTOW;

#ifdef _F_ZONE_WINDOW_ROTATION_
extern E_API int E_EVENT_ZONE_ROTATION_CHANGE_BEGIN;
extern E_API int E_EVENT_ZONE_ROTATION_CHANGE_CANCEL;
extern E_API int E_EVENT_ZONE_ROTATION_CHANGE_END;
extern E_API int E_EVENT_ZONE_ROTATION_EFFECT_READY;
extern E_API int E_EVENT_ZONE_ROTATION_EFFECT_CANCEL;
extern E_API int E_EVENT_ZONE_ROTATION_EFFECT_DONE;
#endif

extern E_API int E_EVENT_ZONE_DISPLAY_STATE_CHANGE;

#endif
#endif
