#ifdef E_TYPEDEFS
typedef enum _E_Client_Screen_Limit
{
    E_CLIENT_OFFSCREEN_LIMIT_ALLOW_PARTIAL = 0,
    E_CLIENT_OFFSCREEN_LIMIT_ALLOW_FULL = 1,
    E_CLIENT_OFFSCREEN_LIMIT_ALLOW_NONE = 2
} E_Client_Screen_Limit;

typedef enum _E_Icon_Preference
{
   E_ICON_PREF_E_DEFAULT,
   E_ICON_PREF_NETWM,
   E_ICON_PREF_USER
} E_Icon_Preference;

typedef enum _E_Direction
{
   E_DIRECTION_UP,
   E_DIRECTION_DOWN,
   E_DIRECTION_LEFT,
   E_DIRECTION_RIGHT
} E_Direction;

typedef enum _E_Transition
{
   E_TRANSITION_LINEAR = 0,
   E_TRANSITION_SINUSOIDAL = 1,
   E_TRANSITION_ACCELERATE = 2,
   E_TRANSITION_DECELERATE = 3,
   E_TRANSITION_ACCELERATE_LOTS = 4,
   E_TRANSITION_DECELERATE_LOTS = 5,
   E_TRANSITION_SINUSOIDAL_LOTS = 6,
   E_TRANSITION_BOUNCE = 7,
   E_TRANSITION_BOUNCE_LOTS = 8
} E_Transition;

typedef enum _E_Stacking
{
   E_STACKING_NONE,
   E_STACKING_ABOVE,
   E_STACKING_BELOW
} E_Stacking;

typedef enum _E_Focus_Policy
{
   E_FOCUS_CLICK,
   E_FOCUS_MOUSE,
   E_FOCUS_SLOPPY,
   E_FOCUS_LAST,
} E_Focus_Policy;

typedef enum _E_Focus_Policy_Ext
{
   E_FOCUS_EXT_NONE,
   E_FOCUS_EXT_TOP_STACK,
} E_Focus_Policy_Ext;

typedef enum
{
   /* same as ecore-x types */
   E_WINDOW_TYPE_UNKNOWN = 0,
   E_WINDOW_TYPE_DESKTOP,
   E_WINDOW_TYPE_DOCK,
   E_WINDOW_TYPE_TOOLBAR,
   E_WINDOW_TYPE_MENU,
   E_WINDOW_TYPE_UTILITY,
   E_WINDOW_TYPE_SPLASH,
   E_WINDOW_TYPE_DIALOG,
   E_WINDOW_TYPE_NORMAL,
   E_WINDOW_TYPE_DROPDOWN_MENU,
   E_WINDOW_TYPE_POPUP_MENU,
   E_WINDOW_TYPE_TOOLTIP,
   E_WINDOW_TYPE_NOTIFICATION,
   E_WINDOW_TYPE_COMBO,
   E_WINDOW_TYPE_DND,
   /* since UNKNOWN was used improperly in comp matching,
    * this value is a placeholder to indicate that we want
    * only type UNKNOWN windows
    */
   E_WINDOW_TYPE_REAL_UNKNOWN = 999
} E_Window_Type;

typedef enum _E_Focus_Setting
{
   E_FOCUS_NONE,
   E_FOCUS_NEW_WINDOW,
   E_FOCUS_NEW_DIALOG,
   E_FOCUS_NEW_DIALOG_IF_OWNER_FOCUSED
} E_Focus_Setting;

typedef enum _E_Maximize
{
   E_MAXIMIZE_NONE = 0x00000000,
   E_MAXIMIZE_FULLSCREEN = 0x00000001,
   E_MAXIMIZE_SMART = 0x00000002,
   E_MAXIMIZE_EXPAND = 0x00000003,
   E_MAXIMIZE_FILL = 0x00000004,
   E_MAXIMIZE_TYPE = 0x0000000f,
   E_MAXIMIZE_VERTICAL = 0x00000010,
   E_MAXIMIZE_HORIZONTAL = 0x00000020,
   E_MAXIMIZE_BOTH = 0x00000030,
   E_MAXIMIZE_LEFT = 0x00000070,
   E_MAXIMIZE_RIGHT = 0x000000b0,
   E_MAXIMIZE_DIRECTION = 0x000000f0
} E_Maximize;

// TODO: should be removed - yigl
typedef enum _E_Fullscreen
{
   /* Resize window */
   E_FULLSCREEN_RESIZE,
   /* Change screen resolution and resize window */
   E_FULLSCREEN_ZOOM
} E_Fullscreen;

typedef enum _E_Transient
{
   E_TRANSIENT_ABOVE,
   E_TRANSIENT_BELOW,
} E_Transient;

typedef enum _E_Window_Placement
{
   E_WINDOW_PLACEMENT_SMART,
   E_WINDOW_PLACEMENT_ANTIGADGET,
   E_WINDOW_PLACEMENT_CURSOR,
   E_WINDOW_PLACEMENT_MANUAL
} E_Window_Placement;

typedef enum E_Client_Property
{
   E_CLIENT_PROPERTY_NONE = 0,
   E_CLIENT_PROPERTY_SIZE = (1 << 0),
   E_CLIENT_PROPERTY_POS = (1 << 1),
   E_CLIENT_PROPERTY_TITLE = (1 << 2),
   E_CLIENT_PROPERTY_ICON = (1 << 3),
   E_CLIENT_PROPERTY_URGENCY = (1 << 4),
   E_CLIENT_PROPERTY_GRAVITY = (1 << 5),
   E_CLIENT_PROPERTY_NETWM_STATE = (1 << 6),
   E_CLIENT_PROPERTY_STICKY = (1 << 7),
   E_CLIENT_PROPERTY_CLIENT_TYPE = (1 << 8),
} E_Client_Property;

#ifdef _F_E_VIRTUAL_KEYBOARD_TYPE_
typedef enum _E_Virtual_Keyboard_Window_Type
{
   E_VIRTUAL_KEYBOARD_WINDOW_TYPE_NONE = 0,
   E_VIRTUAL_KEYBOARD_WINDOW_TYPE_KEYPAD = 1,
   E_VIRTUAL_KEYBOARD_WINDOW_TYPE_PREDICTION = 2,
   E_VIRTUAL_KEYBOARD_WINDOW_TYPE_MAGNIFIER = 3,
   E_VIRTUAL_KEYBOARD_WINDOW_TYPE_POPUP = 4,
} E_Virtual_Keyboard_Window_Type;
#endif

#ifdef _F_ZONE_WINDOW_ROTATION_
typedef enum _E_Client_Rotation_Type
{
   E_CLIENT_ROTATION_TYPE_NORMAL = 0,
   E_CLIENT_ROTATION_TYPE_DEPENDENT = 1
} E_Client_Rotation_Type;
#endif

typedef enum _E_Visibility
{
   E_VISIBILITY_UNKNOWN = -1,
   E_VISIBILITY_UNOBSCURED = 0,
   E_VISIBILITY_PARTIALLY_OBSCURED = 1,
   E_VISIBILITY_FULLY_OBSCURED = 2,
   E_VISIBILITY_PRE_UNOBSCURED = 3,
} E_Visibility;

typedef enum _E_Changable_Layer_Type
{
   E_CHANGABLE_LAYER_TYPE_TRANSIENT = 0,
   E_CHANGABLE_LAYER_TYPE_ABOVE_NOTIFICATION = 1,
   E_CHANGABLE_LAYER_TYPE_MAX
} E_Changable_Layer_Type;

typedef enum _E_Indicator_Visible_Type
{
   E_INDICATOR_VISIBLE_TYPE_HIDDEN = 0,
   E_INDICATOR_VISIBLE_TYPE_SHOWN,
} E_Indicator_Visible_Type;

typedef enum _E_Indicator_State
{
   E_INDICATOR_STATE_UNKNOWN = 0,
   E_INDICATOR_STATE_OFF,
   E_INDICATOR_STATE_ON
} E_Indicator_State;

typedef enum _E_Indicator_Opacity_Mode
{
   E_INDICATOR_OPACITY_MODE_UNKNOWN = 0,
   E_INDICATOR_OPACITY_MODE_OPAQUE,
   E_INDICATOR_OPACITY_MODE_TRANSLUCENT,
   E_INDICATOR_OPACITY_MODE_TRANSPARENT,
   E_INDICATOR_OPACITY_MODE_BG_TRANSPARENT,
} E_Indicator_Opacity_Mode;

typedef struct E_Client E_Client;

typedef struct E_Event_Client E_Event_Client;
typedef struct _E_Event_Client_Property E_Event_Client_Property;
typedef struct _E_Event_Client_Focus_Skip E_Event_Client_Focus_Skip_Set;
typedef struct _E_Event_Client_Focus_Skip E_Event_Client_Focus_Skip_Unset;
typedef struct _E_Client_Pending_Resize E_Client_Pending_Resize;
typedef struct _E_Client_Pending_Geometry E_Client_Pending_Geometry;
typedef struct E_Event_Client_Zone_Set E_Event_Client_Zone_Set;
typedef struct E_Event_Client_Desk_Set E_Event_Client_Desk_Set;
typedef struct _E_Client_Hook E_Client_Hook;
typedef struct _E_Client_Intercept_Hook E_Client_Intercept_Hook;

#ifdef _F_ZONE_WINDOW_ROTATION_
typedef struct E_Event_Client E_Event_Client_Rotation_Change_Begin;
typedef struct E_Event_Client E_Event_Client_Rotation_Change_Cancel;
typedef struct E_Event_Client E_Event_Client_Rotation_Change_End;
#endif

typedef enum _E_Client_Hook_Point
{
   E_CLIENT_HOOK_EVAL_PRE_FETCH,
   E_CLIENT_HOOK_EVAL_FETCH,
   E_CLIENT_HOOK_EVAL_PRE_POST_FETCH,
   E_CLIENT_HOOK_EVAL_POST_FETCH,
   E_CLIENT_HOOK_EVAL_PRE_FRAME_ASSIGN,
   E_CLIENT_HOOK_EVAL_POST_FRAME_ASSIGN,
   E_CLIENT_HOOK_EVAL_PRE_NEW_CLIENT,
   E_CLIENT_HOOK_EVAL_POST_NEW_CLIENT,
   E_CLIENT_HOOK_EVAL_END,
   E_CLIENT_HOOK_FOCUS_SET,
   E_CLIENT_HOOK_FOCUS_UNSET,
   E_CLIENT_HOOK_NEW_CLIENT,
   E_CLIENT_HOOK_DESK_SET,
   E_CLIENT_HOOK_MOVE_BEGIN,
   E_CLIENT_HOOK_MOVE_UPDATE,
   E_CLIENT_HOOK_MOVE_END,
   E_CLIENT_HOOK_RESIZE_BEGIN,
   E_CLIENT_HOOK_RESIZE_UPDATE,
   E_CLIENT_HOOK_RESIZE_END,
   E_CLIENT_HOOK_FULLSCREEN_PRE,
   E_CLIENT_HOOK_DEL,
   E_CLIENT_HOOK_UNREDIRECT,
   E_CLIENT_HOOK_REDIRECT,
#ifdef _F_E_CLIENT_NEW_CLIENT_POST_HOOK_
   E_CLIENT_HOOK_NEW_CLIENT_POST,
#endif
   E_CLIENT_HOOK_EVAL_VISIBILITY,
   E_CLIENT_HOOK_ICONIFY,
   E_CLIENT_HOOK_UNICONIFY,
   E_CLIENT_HOOK_AUX_HINT_CHANGE,
   E_CLIENT_HOOK_WINDOW_ROLE_CHANGE,
   E_CLIENT_HOOK_CAL_VISIBILITY_DISPLAY_OFF,
   E_CLIENT_HOOK_TRANSFORM_CHANGE,
   E_CLIENT_HOOK_ACTIVATE_DONE,
   E_CLIENT_HOOK_LAST,
} E_Client_Hook_Point;

typedef enum _E_Client_Intercept_Hook_Point
{
   E_CLIENT_INTERCEPT_HOOK_FOCUS_REVERT,
   E_CLIENT_INTERCEPT_HOOK_LAST,
} E_Client_Intercept_Hook_Point;

typedef void (*E_Client_Move_Intercept_Cb)(E_Client *, int x, int y);
typedef void (*E_Client_Hook_Cb)(void *data, E_Client *ec);
typedef Eina_Bool (*E_Client_Intercept_Hook_Cb)(void *data, E_Client *ec);
typedef void (*E_Client_Layout_Cb)(void);
#else

#define E_CLIENT_TYPE (int)0xE0b01002

struct E_Event_Client
{
   E_Client *ec;
};

struct E_Event_Client_Desk_Set
{
   E_Client *ec;
   E_Desk *desk;
};

struct E_Event_Client_Zone_Set
{
   E_Client *ec;
   E_Zone *zone;
};

struct _E_Event_Client_Property
{
   E_Client *ec;
   unsigned int property;
};

struct _E_Event_Client_Focus_Skip
{
   E_Client  *ec;
   Eina_Bool  by_client;
};

struct _E_Client_Hook
{
   EINA_INLIST;
   E_Client_Hook_Point hookpoint;
   E_Client_Hook_Cb func;
   void               *data;
   unsigned char       delete_me : 1;
};

struct _E_Client_Intercept_Hook
{
   EINA_INLIST;
   E_Client_Intercept_Hook_Point hookpoint;
   E_Client_Intercept_Hook_Cb func;
   void               *data;
   unsigned char       delete_me : 1;
};

struct _E_Client_Pending_Resize
{
   int           w, h;
   unsigned int  serial;
};

typedef enum
{
   E_GEOMETRY_NONE = 0,
   E_GEOMETRY_POS  = (1 << 0),
   E_GEOMETRY_SIZE = (1 << 1)
} E_Client_Demand_Geometry;

struct _E_Client_Pending_Geometry
{
   E_Client_Demand_Geometry      mode;
   int                           x, y, w, h;
   unsigned int                  serial;
};

struct E_Client
{
   E_Object e_obj_inherit;
   EINA_INLIST;

   E_Pixmap *pixmap;
   int depth;
   int x, y, w, h; //frame+client geom
   struct {
      int x, y, w, h; //frame+client geom before move or resize callback
   } pre_cb;
   Eina_Rectangle client; //client geom
   Evas_Object *frame; //comp object
   E_Zone *zone;
   E_Desk *desk;

   Ecore_Poller              *ping_poller;
   Ecore_Timer               *kill_timer;

   E_Client                  *modal;

   E_Client                  *leader;
   Eina_List                 *group;

   E_Client                  *parent;
   Eina_List                 *transients;

   E_Layer                    layer;

   Eina_Rectangle           *shape_rects;
   unsigned int              shape_rects_num;

   Eina_Rectangle           *shape_input_rects;
   unsigned int              shape_input_rects_num;

   Eina_Stringshare         *internal_icon;
   Eina_Stringshare         *internal_icon_key;

   E_Direction               shade_dir;

   E_Comp_Client_Data       *comp_data;

   E_Action                  *cur_mouse_action;

   int               border_size; //size of client's border

   struct
   {
      struct
      {
         int x, y, w, h;
         int mx, my;
      } current, last_down[3], last_up[3];
      Eina_Bool in : 1;
   } mouse;

   struct
   {
      struct
      {
         int x, y, w, h;
         int mx, my;
         int button;
      } down;
   } moveinfo;

   unsigned char      ignore_first_unmap;
   E_Pointer_Mode     resize_mode;

   struct
   {
      Eina_Bool mapping_change : 1;
      Eina_Bool iconic_shading : 1;
   } hacks;

   struct
   {
      unsigned char changed : 1;
      unsigned char user_selected : 1;
      Eina_Stringshare *name;
   } border;

   struct
   {
      int          x, y, w, h;
      E_Layer      layer;
      int          zone;
      E_Maximize   maximized;
   } saved;

   struct
   {
      unsigned char valid : 1;
      int           x, y, w, h;
      struct
      {
         int x, y, w, h;
      } saved;
   } pre_res_change;

   unsigned char  shaped : 1;
   unsigned char  argb : 1;

   /* ICCCM */
   struct
   {
      Eina_Stringshare        *title;
      Eina_Stringshare        *name;
      Eina_Stringshare        *class;
      Eina_Stringshare        *icon_name;
      Eina_Stringshare        *machine;
      int                       min_w, min_h;
      int                       max_w, max_h;
      int                       base_w, base_h;
      int                       step_w, step_h;
      int                       start_x, start_y;
      double                    min_aspect, max_aspect;
      Ecore_Window            icon_window;
      Ecore_Window            window_group;
      Ecore_Window            transient_for;
      Ecore_Window            client_leader;
      Eina_Stringshare         *window_role;
      unsigned char             take_focus : 1;
      unsigned char             accepts_focus : 1;
      unsigned char             urgent : 1;
      unsigned char             delete_request : 1;
      unsigned char             request_pos : 1;
      struct
      {
         int    argc;
         char **argv;
      } command;
      struct
      {
         unsigned char title : 1;
         unsigned char name_class : 1;
         unsigned char icon_name : 1;
         unsigned char machine : 1;
         unsigned char hints : 1;
         unsigned char size_pos_hints : 1;
         unsigned char protocol : 1;
         unsigned char transient_for : 1;
         unsigned char client_leader : 1;
         unsigned char window_role : 1;
         unsigned char state : 1;
         unsigned char command : 1;
      } fetch;
   } icccm;

   /* MWM */
   struct
   {
      unsigned char          exists : 1;
      unsigned char          borderless : 1;
      struct
      {
         unsigned char hints : 1;
      } fetch;
   } mwm;

   /* NetWM */
   struct
   {
      pid_t         pid;
      unsigned int  desktop;
      Eina_Stringshare *name;
      Eina_Stringshare *icon_name;

      Ecore_X_Icon *icons;

      int           num_icons;
      unsigned int  user_time;
      unsigned char opacity;
      Eina_Bool     opacity_changed : 1; // prevent fetching opacity next prop change
      struct
      {
         int left;
         int right;
         int top;
         int bottom;
         int left_start_y;
         int left_end_y;
         int right_start_y;
         int right_end_y;
         int top_start_x;
         int top_end_x;
         int bottom_start_x;
         int bottom_end_x;
      } strut;
      unsigned char ping : 1;
      struct
      {
         unsigned char        request : 1;
         unsigned char        alarm : 1;
         unsigned int         wait;
         unsigned int         serial;
         double               send_time;
      } sync;

      /* NetWM Window state */
      struct
      {
         unsigned char modal : 1;
         unsigned char sticky : 1;
         unsigned char maximized_v : 1;
         unsigned char maximized_h : 1;
         unsigned char shaded : 1;
         unsigned char skip_taskbar : 1;
         unsigned char skip_pager : 1;
         unsigned char hidden : 1;
         unsigned char fullscreen : 1;
         E_Stacking    stacking;
      } state;

      /* NetWM Window allowed actions */
      struct
      {
         unsigned char move : 1;
         unsigned char resize : 1;
         unsigned char minimize : 1;
         unsigned char shade : 1;
         unsigned char stick : 1;
         unsigned char maximized_h : 1;
         unsigned char maximized_v : 1;
         unsigned char fullscreen : 1;
         unsigned char change_desktop : 1;
         unsigned char close : 1;
      } action;
      E_Window_Type  type;
      E_Window_Type *extra_types;
      int                  extra_types_num;
      int                  startup_id;

      struct
      {
         unsigned char name : 1;
         unsigned char icon_name : 1;
         unsigned char icon : 1;
         unsigned char user_time : 1;
         unsigned char strut : 1;
         unsigned char type : 1;
         unsigned char state : 1;
         unsigned char opacity : 1;
         /* No, fetch on new_client, shouldn't be changed after map.
            unsigned char pid : 1;
          */
         /* No, ignore this
            unsigned char desktop : 1;
          */
      } fetch;

      struct
      {
         unsigned char state : 1;
      } update;
   } netwm;

   /* Extra e stuff */
   struct
   {
      struct
      {
         struct
         {
            int           x, y;

            unsigned char updated : 1;
         } video_position;
         Ecore_Window video_parent;
         E_Client      *video_parent_client;
         Eina_List     *video_child;
         struct
         {
            Eina_Stringshare *name;
            Eina_Stringshare **available_list;
            Eina_Stringshare *set;
            Eina_Stringshare *wait;
            E_Desk           *wait_desk;
            E_Object_Delfn   *wait_desk_delfn;
            int               num;
            unsigned char     wait_for_done : 1;
            unsigned char     use : 1;
         } profile;
         unsigned char  centered : 1;
         unsigned char  video : 1;
#ifdef _F_ZONE_WINDOW_ROTATION_
         struct
         {
            E_Client_Rotation_Type type;
            struct
              {
                 int prev, curr, next, reserve;
              } ang;
            struct
              {
                 int x, y, w, h;
              } geom[4];
            unsigned char support : 1;
            unsigned char geom_hint: 1;
            unsigned char pending_change_request : 1;
            unsigned char pending_show : 1;  // newly created window that has to be rotated will be show after rotating done.
                                             // so, it will be used pending e_border_show called at main eval time.
            unsigned char wait_for_done: 1;
            unsigned char app_set : 1;    // app wants to communicate with the window manager
            int           rot;            // decided rotation by the window manager
            int           preferred_rot;  // app specified rotation
            int          *available_rots; // app specified available rotations
            unsigned int  count;          // number of elements of available rotations
            unsigned int  nopending_render : 1; // app wants to show rendering jobs before sending rotation_done
                                                // it is set by 'wm.policy.win.rot.render.nopending' aux hint
         } rot;
#endif
      } state;

      struct
      {
#ifdef _F_ZONE_WINDOW_ROTATION_
         struct
         {
            unsigned char support : 1;
            unsigned char geom_hint : 1;
            unsigned char app_set : 1;        // app wants to communicate with the window manager
            unsigned char preferred_rot : 1;  // app specified rotation
            unsigned char available_rots : 1; // app specified available rotations
            unsigned char need_rotation : 1;
         } rot;
#endif
         unsigned char state : 1;
         unsigned char opaque : 1;
         unsigned char video_parent : 1;
         unsigned char video_position : 1;
         unsigned char profile : 1;
      } fetch;
   } e;

   struct
   {
      struct
      {
         unsigned char state : 1;
         unsigned char vkbd : 1;
      } fetch;
      unsigned char                  have_property : 1;
      unsigned char                  vkbd : 1;
#ifdef _F_E_VIRTUAL_KEYBOARD_TYPE_
      E_Virtual_Keyboard_Window_Type win_type;
#endif
      unsigned char                  floating : 1;
   } vkbd;

   struct
   {
      unsigned char visible : 1;
      unsigned char pos : 1;
      unsigned char size : 1;
      unsigned char stack : 1;
      unsigned char prop : 1;
      unsigned char border : 1;
      unsigned char reset_gravity : 1;
      unsigned char shading : 1;
      unsigned char shaded : 1;
      unsigned char shape : 1;
      unsigned char shape_input : 1;
      unsigned char icon : 1;
      Eina_Bool internal_state : 1;
      Eina_Bool need_maximize : 1;
      Eina_Bool need_unmaximize : 1;
#ifdef _F_ZONE_WINDOW_ROTATION_
      Eina_Bool rotation : 1;
#endif
      Eina_Bool accepts_focus : 1;
      Eina_Bool tz_position : 1; /* true when new position is set using tizen_position interface */
      unsigned char title : 1;
   } changes;

   struct
   {
      int opaque;
      int obscured;
      unsigned char changed : 1;
      Eina_Bool skip : 1;
      Eina_Bool force_obscured : 1;
      int last_sent_type;
   } visibility;

   struct
   {
      double zoom;
      double angle;

      Evas_Point center;
      Evas_Point adjusted;

      struct
      {
         double x, y, z;
      } saved[4];
   } transform;

   unsigned int       visible : 1; // client is set to be visible by display server (never use this)
   unsigned int       hidden : 1; // set when window has been hidden by api and should not be shown
   unsigned int       moving : 1;
   unsigned int       focused : 1;
   unsigned int       new_client : 1;
   unsigned int       re_manage : 1; // client is persisting from before E restart
   unsigned int       placed : 1;
   unsigned int       shading : 1;
   unsigned int       shaded : 1;
   unsigned int       iconic : 1;
   unsigned int       deskshow : 1;
   unsigned int       sticky : 1;
   unsigned int       urgent : 1;
   unsigned int       shaped_input : 1;
   unsigned int       need_shape_merge : 1;
   unsigned int       need_shape_export : 1;
   unsigned int       fullscreen : 1;
   unsigned int       need_fullscreen : 1;
   unsigned int       skip_fullscreen : 1;
   unsigned int       already_unparented : 1;
   unsigned int       need_reparent : 1;
   unsigned int       button_grabbed : 1;
   unsigned int       delete_requested : 1;
   unsigned int       ping_ok : 1;
   unsigned int       hung : 1;
   unsigned int       take_focus : 1;
   unsigned int       want_focus : 1;
   unsigned int       user_skip_winlist : 1;
   E_Maximize         maximized;
   E_Fullscreen       fullscreen_policy; // TODO: should be removed - yigl
   E_Transient        transient_policy;
   unsigned int       borderless : 1;
   unsigned char      offer_resistance : 1;
   Eina_Stringshare  *bordername;

   unsigned int       lock_user_location : 1; /*DONE*/
   unsigned int       lock_client_location : 1; /*DONE*/
   unsigned int       lock_user_size : 1; /*DONE*/
   unsigned int       lock_client_size : 1; /*DONE*/
   unsigned int       lock_user_stacking : 1; /*DONE*/
   unsigned int       lock_client_stacking : 1; /*DONE*/
   unsigned int       lock_user_iconify : 1; /*DONE*/
   unsigned int       lock_client_iconify : 1; /*DONE*/
   unsigned int       lock_user_desk : 1;
   unsigned int       lock_client_desk : 1;
   unsigned int       lock_user_sticky : 1; /*DONE*/
   unsigned int       lock_client_sticky : 1; /*DONE*/
   unsigned int       lock_user_shade : 1; /*DONE*/
   unsigned int       lock_client_shade : 1; /*DONE*/
   unsigned int       lock_user_maximize : 1; /*DONE*/
   unsigned int       lock_client_maximize : 1; /*DONE*/
   unsigned int       lock_user_fullscreen : 1; /*DONE*/
   unsigned int       lock_client_fullscreen : 1; /*DONE*/
   unsigned int       lock_border : 1; /*DONE*/
   unsigned int       lock_close : 1; /*DONE*/
   unsigned int       lock_focus_in : 1; /*DONE*/
   unsigned int       lock_focus_out : 1; /*DONE*/
   unsigned int       lock_life : 1; /*DONE*/

   unsigned int       stolen : 1;

   unsigned int       internal : 1;
   unsigned int       internal_no_remember : 1; /* TODO: should be removed */
   unsigned int       internal_no_reopen : 1;
   Eina_Bool          dead : 1;

   Evas_Object       *internal_elm_win;

   double             ping;

   unsigned char      changed : 1;

   unsigned char      icon_preference;

   struct
   {
      int x, y;
      int modified;
   } shelf_fix;

   Eina_List       *stick_desks;
   Evas_Object     *color_editor;
   E_Dialog        *border_prop_dialog;
   Eina_List       *pending_resize;

   struct
   {
      unsigned char start : 1;
      int           x, y;
   } drag;

   E_Client_Move_Intercept_Cb move_intercept_cb;
   void                      *remember; /* TODO: should be removed */

   unsigned char              comp_hidden   : 1;

   unsigned char              post_move   : 1;
   unsigned char              post_resize : 1;
   unsigned char              post_show : 1;
   unsigned char              during_lost : 1;

   Ecore_Idle_Enterer        *post_job;

   E_Focus_Policy             focus_policy_override;

   Eina_Bool override : 1;
   Eina_Bool input_only : 1;
   Eina_Bool dialog : 1;
   Eina_Bool tooltip : 1;
   Eina_Bool redirected : 1;
   Eina_Bool unredirected_single : 1; //window has been selectively unredirected
   Eina_Bool shape_changed : 1;
   Eina_Bool layer_block : 1; // client is doing crazy stuff and should not be relayered in protocol
   Eina_Bool layer_pending : 1; // change inlist stack but postpone evas stack
   Eina_Bool ignored : 1; // client is comp-ignored
   Eina_Bool no_shape_cut : 1; // client shape should not be cut
   Eina_Bool maximize_override : 1; // client is doing crazy stuff and should "just do it" when moving/resizing
   Eina_Bool transformed : 1;
   Eina_Bool keyboard_resizing : 1;
   Eina_Bool gesture_disable : 1;
   Eina_Bool floating : 1; //client is set as floating mode
   Eina_Bool first_mapped : 1;
   Eina_Bool post_raise : 1;
   Eina_Bool post_lower : 1;
   Eina_Bool animatable : 1; //client is animatable (can apply animation)

   Eina_Bool on_post_updates : 1; // client is on the post update list
   uuid_t uuid;

   int client_type; //e_client_type

   struct
   {
      unsigned char by_client : 1;
      unsigned char not_raise : 1;
      unsigned char skip_iconify : 1;
      unsigned char skip_by_remote : 1;    // skip iconify by remote surface client
      unsigned char deiconify_update : 1;  // wait client render if deiconify_update is 1
      unsigned char buffer_flush : 1;      // 0: no flush, 1: flush buffer when iconify
   } exp_iconify;

   struct
   {
      Eina_Bool set;
      Eina_Bool saved;
      E_Layer   saved_layer; // original layer
   } changable_layer[E_CHANGABLE_LAYER_TYPE_MAX];

   struct
   {
      Eina_List *transform_list;
      Eina_Bool  background;
      Eina_Bool  changed;

      struct
      {
         int client_x, client_y, client_w, client_h;
         int frame_w, frame_h;
      } backup;

      struct
      {
         Eina_Bool                    enable;
         E_Util_Transform_Matrix      matrix;
         E_Util_Transform_Matrix      inv_matrix;
         E_Util_Transform_Rect_Vertex vertices;
         E_Util_Transform             transform;

         struct
         {
            E_Util_Transform_Rect_Vertex vertices;
            int x, y, w, h;
         } boundary;

      } result;

      struct
      {
         Eina_Bool               enable;
         E_Util_Transform_Matrix matrix;
      } parent;
   } transform_core;

   struct
   {
      unsigned char            resizable : 1;  // or scalable
      unsigned char            splited : 1;
      int                      s_id;           // split(slot) id
   } layout;

   Ecore_Timer *map_timer;
   Eina_Bool pointer_enter_sent;

   Eina_Bool launching : 1;
   Eina_Bool extra_animating : 1;
   Eina_Bool bg_state : 1;

   struct
   {
      E_Indicator_State        state;
      E_Indicator_Opacity_Mode opacity_mode;
      E_Indicator_Visible_Type visible_type;
   } indicator;

   E_Plane_Renderer_Client *renderer_client;

   struct
   {
      Eina_Bool provider : 1;
      Eina_Bool consumer : 1;
      int       bind_ref;
   } remote_surface;

   Eina_Bool is_cursor : 1; // The client is a cursor client

   int effect_type;
   Eina_Bool use_splash : 1;
   Eina_Bool saved_img : 1; // indicates that window has been saved as the image file even once
   Eina_Bool skip_save_img: 1; // indicates that window doesn't want to save its image file

   E_Hwc_Window *hwc_window; // hwc window for the tdm_hwc.
   int comp_override;

   struct
   {
      Eina_Bool    wait_commit : 1;
      unsigned int serial;
      Eina_List   *pending_geometry; // E_Client_Pending_Geometry
   } surface_sync;

   Eina_Bool on_render_list : 1; // client is on the render list

   // flag to check event pairing
   struct
   {
      Eina_Bool add  : 1; // ADD / REMOVE
      Eina_Bool show : 1; // SHOW / HIDE
   } reg_ev;

   Evas_Object *magnifier_proxy;   // The proxy object used by magnifier
   Eina_Bool    is_magnifier : 1;  // The client is a magnifier client

   struct
   {
      Eina_Bool    use : 1; // use the base output resolution
      int          w;
      int          h;
      E_Util_Transform *transform;
      E_Comp_Wl_Hook *hook_subsurf_create;
   } base_output_resolution;
};

#define e_client_focus_policy_click(ec) \
  ((ec->focus_policy_override == E_FOCUS_CLICK) || (e_config->focus_policy == E_FOCUS_CLICK))

/* macro for finding misuse of changed flag */
#if 0
# define EC_CHANGED(EC) \
  do { \
     if (e_object_is_del(E_OBJECT(EC))) \
       EINA_LOG_CRIT("CHANGED SET ON DELETED CLIENT!"); \
     EC->changed = 1; \
     INF("%s:%d - EC CHANGED: %p", __FILE__, __LINE__, EC); \
  } while (0)
#else
# define EC_CHANGED(EC) EC->changed = 1
#endif

#define E_CLIENT_FOREACH(EC) \
  for (EC = e_client_bottom_get(); EC; EC = e_client_above_get(EC))

#define E_CLIENT_REVERSE_FOREACH(EC) \
  for (EC = e_client_top_get(); EC; EC = e_client_below_get(EC))


E_API extern int E_EVENT_CLIENT_ADD;
E_API extern int E_EVENT_CLIENT_REMOVE;
E_API extern int E_EVENT_CLIENT_ZONE_SET;
E_API extern int E_EVENT_CLIENT_DESK_SET;
E_API extern int E_EVENT_CLIENT_RESIZE;
E_API extern int E_EVENT_CLIENT_MOVE;
E_API extern int E_EVENT_CLIENT_SHOW;
E_API extern int E_EVENT_CLIENT_HIDE;
E_API extern int E_EVENT_CLIENT_ICONIFY;
E_API extern int E_EVENT_CLIENT_UNICONIFY;
E_API extern int E_EVENT_CLIENT_STACK;
E_API extern int E_EVENT_CLIENT_FOCUS_IN;
E_API extern int E_EVENT_CLIENT_FOCUS_OUT;
E_API extern int E_EVENT_CLIENT_PROPERTY;
E_API extern int E_EVENT_CLIENT_FULLSCREEN;
E_API extern int E_EVENT_CLIENT_UNFULLSCREEN;
#ifdef _F_ZONE_WINDOW_ROTATION_
E_API extern int E_EVENT_CLIENT_ROTATION_CHANGE_BEGIN;
E_API extern int E_EVENT_CLIENT_ROTATION_CHANGE_CANCEL;
E_API extern int E_EVENT_CLIENT_ROTATION_CHANGE_END;
#endif
E_API extern int E_EVENT_CLIENT_VISIBILITY_CHANGE;
E_API extern int E_EVENT_CLIENT_BUFFER_CHANGE;
E_API extern int E_EVENT_CLIENT_FOCUS_SKIP_SET;
E_API extern int E_EVENT_CLIENT_FOCUS_SKIP_UNSET;

EINTERN void e_client_idler_before(void);
EINTERN Eina_Bool e_client_init(void);
EINTERN void e_client_shutdown(void);
E_API E_Client *e_client_new(E_Pixmap *cp, int first_map, int internal);
E_API void e_client_unignore(E_Client *ec);
E_API void e_client_desk_set(E_Client *ec, E_Desk *desk);
E_API Eina_Bool e_client_comp_grabbed_get(void);
E_API E_Client *e_client_action_get(void);
E_API E_Client *e_client_warping_get(void);
E_API Eina_List *e_clients_immortal_list(void);
E_API void e_client_mouse_in(E_Client *ec, int x, int y);
E_API void e_client_mouse_out(E_Client *ec, int x, int y);
E_API void e_client_mouse_wheel(E_Client *ec, Evas_Point *output, E_Binding_Event_Wheel *ev);
E_API void e_client_mouse_down(E_Client *ec, int button, Evas_Point *output, E_Binding_Event_Mouse_Button *ev);
E_API void e_client_mouse_up(E_Client *ec, int button, Evas_Point *output, E_Binding_Event_Mouse_Button* ev);
E_API void e_client_mouse_move(E_Client *ec, Evas_Point *output);
E_API void e_client_res_change_geometry_save(E_Client *bd);
E_API void e_client_res_change_geometry_restore(E_Client *ec);
E_API void e_client_zone_set(E_Client *ec, E_Zone *zone);
E_API void e_client_pos_set(E_Client *ec, int x, int y);
E_API void e_client_pos_get(E_Client *ec, int *x, int *y);
E_API void e_client_size_set(E_Client *ec, int w, int h);
E_API void e_client_size_get(E_Client *ec, int *w, int *h);
E_API void e_client_geometry_get(E_Client *ec, int *x, int *y, int *w, int *h);
E_API E_Client *e_client_above_get(const E_Client *ec);
E_API E_Client *e_client_below_get(const E_Client *ec);
E_API E_Client *e_client_bottom_get(void);
E_API E_Client *e_client_top_get(void);
E_API unsigned int e_clients_count(void);
E_API void e_client_move_intercept_cb_set(E_Client *ec, E_Client_Move_Intercept_Cb cb);
E_API E_Client_Hook *e_client_hook_add(E_Client_Hook_Point hookpoint, E_Client_Hook_Cb func, const void *data);
E_API void e_client_hook_del(E_Client_Hook *ch);
E_API E_Client_Intercept_Hook *e_client_intercept_hook_add(E_Client_Intercept_Hook_Point hookpoint, E_Client_Intercept_Hook_Cb func, const void *data);
E_API void e_client_intercept_hook_del(E_Client_Intercept_Hook *ch);
EINTERN void e_client_focus_stack_lower(E_Client *ec);
E_API void e_client_focus_latest_set(E_Client *ec);
EINTERN void e_client_focus_stack_append_current_focused(E_Client *ec);
E_API void e_client_focus_defer_set(E_Client *ec);
E_API void e_client_focus_defer_unset(E_Client *ec);
E_API Eina_Bool e_client_focus_track_enabled(void);
E_API void e_client_focus_track_freeze(void);
E_API void e_client_focus_track_thaw(void);
E_API void e_client_refocus(void);
E_API void e_client_focus_set_with_pointer(E_Client *ec);
E_API void e_client_activate(E_Client *ec, Eina_Bool just_do_it);
E_API E_Client *e_client_focused_get(void);
E_API Eina_List *e_client_focus_stack_get(void);
E_API Eina_List *e_client_lost_windows_get(E_Zone *zone);
E_API void e_client_shade(E_Client *ec, E_Direction dir);
E_API void e_client_unshade(E_Client *ec, E_Direction dir);
E_API void e_client_maximize(E_Client *ec, E_Maximize max);
E_API void e_client_unmaximize(E_Client *ec, E_Maximize max);
E_API void e_client_fullscreen(E_Client *ec, E_Fullscreen policy);
E_API void e_client_unfullscreen(E_Client *ec);
E_API void e_client_iconify(E_Client *ec);
E_API void e_client_uniconify(E_Client *ec);
E_API void e_client_urgent_set(E_Client *ec, Eina_Bool urgent);
E_API void e_client_stick(E_Client *ec);
E_API void e_client_unstick(E_Client *ec);
E_API void e_client_pinned_set(E_Client *ec, Eina_Bool set);
E_API void e_client_comp_hidden_set(E_Client *ec, Eina_Bool hidden);
E_API Eina_Bool e_client_border_set(E_Client *ec, const char *name);
E_API void e_client_act_move_keyboard(E_Client *ec);
E_API void e_client_act_resize_keyboard(E_Client *ec);
E_API void e_client_act_move_begin(E_Client *ec, E_Binding_Event_Mouse_Button *ev);
E_API void e_client_act_move_end(E_Client *ec, E_Binding_Event_Mouse_Button *ev EINA_UNUSED);
E_API void e_client_act_resize_begin(E_Client *ec, E_Binding_Event_Mouse_Button *ev, E_Pointer_Mode resize_mode);
E_API void e_client_act_resize_end(E_Client *ec, E_Binding_Event_Mouse_Button *ev EINA_UNUSED);
E_API void e_client_act_menu_begin(E_Client *ec, E_Binding_Event_Mouse_Button *ev, int key);
E_API void e_client_act_close_begin(E_Client *ec);
E_API void e_client_act_kill_begin(E_Client *ec);
E_API Evas_Object *e_client_icon_add(E_Client *ec, Evas *evas);
E_API void e_client_ping(E_Client *cw);
E_API void e_client_move_cancel(void);
E_API void e_client_resize_cancel(void);
E_API Eina_Bool e_client_resize_begin(E_Client *ec);
E_API void e_client_frame_recalc(E_Client *ec);
E_API void e_client_signal_move_begin(E_Client *ec, const char *sig, const char *src EINA_UNUSED);
E_API void e_client_signal_move_end(E_Client *ec, const char *sig EINA_UNUSED, const char *src EINA_UNUSED);
E_API void e_client_signal_resize_begin(E_Client *ec, const char *dir, const char *sig, const char *src EINA_UNUSED);
E_API void e_client_signal_resize_end(E_Client *ec, const char *dir EINA_UNUSED, const char *sig EINA_UNUSED, const char *src EINA_UNUSED);
E_API void e_client_resize_limit(E_Client *ec, int *w, int *h);
E_API E_Client *e_client_under_pointer_get(E_Desk *desk, E_Client *exclude);
E_API E_Client *e_client_under_position_get(E_Desk *desk, int x, int y, E_Client *exclude);
E_API int e_client_pointer_warp_to_center_now(E_Client *ec);
E_API int e_client_pointer_warp_to_center(E_Client *ec);
E_API void e_client_redirected_set(E_Client *ec, Eina_Bool set);
E_API Eina_Bool e_client_is_stacking(const E_Client *ec);
E_API Eina_Bool e_client_has_xwindow(const E_Client *ec);
E_API Eina_Bool e_client_desk_window_profile_available_check(E_Client *ec, const char *profile);
E_API void      e_client_desk_window_profile_wait_desk_set(E_Client *ec, E_Desk *desk);
E_API void      e_client_layout_cb_set(E_Client_Layout_Cb cb);
E_API E_Client *e_client_transient_child_top_get(E_Client *ec, Eina_Bool consider_focus);
E_API void      e_client_visibility_calculate(void);
E_API void      e_client_visibility_skip_set(E_Client *ec, Eina_Bool skip);
E_API void      e_client_post_raise_lower_set(E_Client *ec, Eina_Bool raise_set, Eina_Bool lower_set);
E_API Eina_Bool e_client_first_mapped_get(E_Client *ec);

E_API void e_client_transform_update(E_Client *ec);
E_API void e_client_transform_apply(E_Client *ec, double degree, double zoom, int cx, int cy);
E_API void e_client_transform_clear(E_Client *ec);
E_API void e_client_cursor_map_apply(E_Client *ec, int rotation, int x, int y);

YOLO E_API void e_client_focus_stack_set(Eina_List *l);

E_API Eina_Bool         e_client_transform_core_enable_get(E_Client *ec);
E_API void              e_client_transform_core_add(E_Client *ec, E_Util_Transform *transform);
E_API void              e_client_transform_core_remove(E_Client *ec, E_Util_Transform *transform);
E_API void              e_client_transform_core_update(E_Client *ec);
E_API int               e_client_transform_core_transform_count_get(E_Client *ec);
E_API E_Util_Transform *e_client_transform_core_transform_get(E_Client *ec, int index);
E_API void              e_client_transform_core_input_transform(E_Client *ec, int x, int y, int *out_x, int *out_y);
E_API void              e_client_transform_core_input_inv_transform(E_Client *ec, int x, int y, int *out_x, int *out_y);
E_API void              e_client_transform_core_input_inv_rect_transform(E_Client *ec, int x, int y, int *out_x, int *out_y);
EINTERN void            e_client_transform_core_update_with_desk_zoom(E_Client *ec);

E_API E_Pixmap *e_client_pixmap_change(E_Client *ec, E_Pixmap *newcp);
E_API void e_client_window_role_set(E_Client *ec, const char *role);

E_API Eina_Bool e_client_key_send(E_Client *ec, int keycode, Eina_Bool pressed, Evas_Device *dev, unsigned int time);
E_API Eina_Bool e_client_touch_send(E_Client *ec, int idx, int x, int y, Eina_Bool pressed, Evas_Device *dev, double radius_x, double radius_y, double pressure, double angle, unsigned int time);
E_API Eina_Bool e_client_touch_update_send(E_Client *ec, int idx, int x, int y, Evas_Device *dev, double radius_x, double radius_y, double pressure, double angle, unsigned int time);
E_API Eina_Bool e_client_touch_cancel_send(E_Client *ec);
E_API Eina_Bool e_client_mouse_button_send(E_Client *ec, int buttons, Eina_Bool pressed, Evas_Device *dev, unsigned int time);
E_API Eina_Bool e_client_mouse_move_send(E_Client *ec, int x, int y, Evas_Device *dev, unsigned int time);
E_API Eina_Bool e_client_mouse_wheel_send(E_Client *ec, int direction, int z, Evas_Device *dev, unsigned int time);
E_API Eina_Bool e_client_mouse_in_send(E_Client *ec, int x, int y, Evas_Device *dev, unsigned int time);
E_API Eina_Bool e_client_mouse_out_send(E_Client *ec, Evas_Device *dev, unsigned int time);

E_API Eina_Bool e_client_video_client_has(E_Client *ec);
E_API Eina_Bool e_client_normal_client_has(E_Client *ec);

E_API Eina_Bool e_client_cursor_hide(E_Client *ec);

E_API void e_client_visibility_force_obscured_set(E_Client *ec, Eina_Bool set);

E_API void e_client_stay_within_canvas_margin(E_Client *ec);

E_API E_Capture_Save_State e_client_image_save(E_Client *ec, const char *dir, const char *name, E_Capture_Client_Save_End_Cb func_end, void *data, Eina_Bool skip_child);

E_API void      e_client_base_output_resolution_transform_adjust(E_Client *ec);
E_API Eina_Bool e_client_base_output_resolution_update(E_Client *ec);
E_API void      e_client_base_output_resolution_useful_geometry_get(E_Client *ec, int *x, int *y, int *w, int *h);

E_API void e_client_focus_skip_set(E_Client *ec, Eina_Bool skip, Eina_Bool by_client);
EINTERN void e_client_revert_focus(E_Client *ec);
EINTERN Eina_Bool e_client_check_above_focused(E_Client *ec);

EINTERN void      e_client_pending_geometry_flush(E_Client *ec);
EINTERN Eina_Bool e_client_pending_geometry_has(E_Client *ec);

E_API void e_client_frame_focus_set(E_Client *ec, Eina_Bool focus);

E_API void e_client_layer_set(E_Client *ec, E_Layer layer);

/**
 * Move window to coordinates that do not account client decorations yet.
 *
 * This call will consider given position does not account client
 * decoration, so these values (e_comp_object_frame) will be
 * accounted automatically. This is specially useful when it is a new
 * client and has not be evaluated yet, in this case
 * the frame will be zeroed and no information is known. It
 * will mark pending requests so client will be accounted on
 * evalutation phase.
 *
 * @parm x horizontal position to place window.
 * @parm y vertical position to place window.
 *
 * @see e_client_move()
 */
static inline void
e_client_util_move_without_frame(E_Client *ec, int x, int y)
{
   if (!ec) return;
   e_comp_object_frame_xy_adjust(ec->frame, x, y, &x, &y);
   evas_object_move(ec->frame, x, y);
}

/**
 * Resize window to values that do not account client decorations yet.
 *
 * This call will consider given size and does not for account client
 * decoration, so these values (e_comp_object_frame) will be
 * accounted for automatically. This is specially useful when it is a new
 * client and has not been evaluated yet, in this case
 * e_comp_object_frame will be zeroed and no information is known. It
 * will mark pending requests so the client will be accounted for on
 * evalutation phase.
 *
 * @parm w horizontal window size.
 * @parm h vertical window size.
 *
 * @see e_client_resize()
 */
static inline void
e_client_util_resize_without_frame(E_Client *ec, int w, int h)
{
   if (!ec) return;
   e_comp_object_frame_wh_adjust(ec->frame, w, h, &w, &h);
   evas_object_resize(ec->frame, w, h);
   e_client_stay_within_canvas_margin(ec);
}

/**
 * Move and resize window to values that do not account for client decorations yet.
 *
 * This call will consider given values already accounts client
 * decorations, so it will not be considered later. This will just
 * work properly with clients that have being evaluated and client
 * decorations are known (e_comp_object_frame).
 *
 * @parm x horizontal position to place window.
 * @parm y vertical position to place window.
 * @parm w horizontal window size.
 * @parm h vertical window size.
 *
 * @see e_client_move_resize()
 */
static inline void
e_client_util_move_resize_without_frame(E_Client *ec, int x, int y, int w, int h)
{
   e_client_util_move_without_frame(ec, x, y);
   e_client_util_resize_without_frame(ec, w, h);
}

static inline Eina_Bool
e_client_util_ignored_get(const E_Client *ec)
{
   if (!ec) return EINA_TRUE;
   return ec->override || ec->input_only || ec->ignored;
}

static inline Eina_Bool
e_client_util_desk_visible(const E_Client *ec, const E_Desk *desk)
{
   if (!ec) return EINA_FALSE;
   return !ec->desk || ec->sticky || (ec->desk == desk);
}

static inline Ecore_Window
e_client_util_pwin_get(const E_Client *ec)
{
   if (!ec) return 0;
   if (!ec->pixmap) return 0;
   return e_pixmap_parent_window_get(ec->pixmap);
}

static inline Ecore_Window
e_client_util_win_get(const E_Client *ec)
{
   if (!ec) return 0;
   if (!ec->pixmap) return 0;
   return e_pixmap_window_get(ec->pixmap);
}

static inline Eina_Bool
e_client_util_resizing_get(const E_Client *ec)
{
   if (!ec) return EINA_FALSE;
   return (ec->resize_mode != E_POINTER_RESIZE_NONE);
}

static inline Eina_Bool
e_client_util_borderless(const E_Client *ec)
{
   if (!ec) return EINA_FALSE;
   return (ec->borderless || ec->mwm.borderless || (!ec->border.name) || (!strcmp(ec->border.name, "borderless")));
}

static inline Eina_Bool
e_client_util_shadow_state_get(const E_Client *ec)
{
   Eina_Bool on;
   if (!ec) return EINA_FALSE;
   if (ec->shaped) return EINA_FALSE;
   if (ec->argb)
     {
        return (!ec->borderless) && (ec->bordername || (ec->border.name && strcmp(ec->border.name, "borderless")));
     }
   on = !ec->e.state.video;
   if (on)
     on = !ec->fullscreen;
   return on;
}

static inline Eina_Stringshare *
e_client_util_name_get(const E_Client *ec)
{
   if (!ec) return NULL;
   if (ec->netwm.name)
     return ec->netwm.name;
   else if (ec->icccm.title)
     return ec->icccm.title;
   return NULL;
}

static inline Eina_Bool
e_client_util_is_popup(const E_Client *ec)
{
   if (!ec) return EINA_FALSE;
   switch (ec->netwm.type)
     {
        case E_WINDOW_TYPE_MENU:
        case E_WINDOW_TYPE_SPLASH:
        case E_WINDOW_TYPE_DROPDOWN_MENU:
        case E_WINDOW_TYPE_POPUP_MENU:
        case E_WINDOW_TYPE_TOOLTIP:
        case E_WINDOW_TYPE_NOTIFICATION:
        case E_WINDOW_TYPE_COMBO:
        case E_WINDOW_TYPE_DND:
          return EINA_TRUE;
        default: break;
     }
  return EINA_FALSE;
}

#endif
