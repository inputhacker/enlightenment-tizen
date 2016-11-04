#ifdef E_TYPEDEFS
#else
# ifndef E_COMP_WL_INPUT_H
#  define E_COMP_WL_INPUT_H

E_API extern int E_EVENT_TEXT_INPUT_PANEL_VISIBILITY_CHANGE;
E_API extern int E_EVENT_SEAT_BOUND;

typedef struct _E_Event_Text_Input_Panel_Visibility_Change E_Event_Text_Input_Panel_Visibility_Change;
typedef struct _E_Event_Seat_Bound E_Event_Seat_Bound;

struct _E_Event_Text_Input_Panel_Visibility_Change
{
   Eina_Bool visible;
};

struct _E_Event_Seat_Bound
{
   struct wl_client *client;
   E_Comp_Wl_Seat *seat;
};

EINTERN Eina_Bool e_comp_wl_input_init(void);
EINTERN void e_comp_wl_input_shutdown(void);
EINTERN Eina_Bool e_comp_wl_input_add(const char *seatname);
EINTERN Eina_Bool e_comp_wl_input_del(const char *seatname);
EINTERN Eina_Bool e_comp_wl_input_pointer_check(struct wl_resource *res);
EINTERN Eina_Bool e_comp_wl_input_keyboard_check(struct wl_resource *res);
EINTERN Eina_Bool e_comp_wl_input_touch_check(struct wl_resource *res);

EINTERN Eina_Bool e_comp_wl_input_keyboard_modifiers_serialize(E_Comp_Wl_Seat *seat);
EINTERN void e_comp_wl_input_keyboard_modifiers_update(E_Comp_Wl_Seat *seat);
EINTERN void e_comp_wl_input_keyboard_state_update(E_Comp_Wl_Seat *seat, uint32_t keycode, Eina_Bool pressed);
EINTERN void e_comp_wl_input_keyboard_enter_send(E_Client *client);
E_API E_Comp_Wl_Seat *e_comp_wl_input_seat_get(const char *seatname);

E_API void e_comp_wl_input_pointer_enabled_set(E_Comp_Wl_Seat *seat, Eina_Bool enabled);
E_API void e_comp_wl_input_keyboard_enabled_set(E_Comp_Wl_Seat *seat, Eina_Bool enabled);
E_API void e_comp_wl_input_touch_enabled_set(E_Comp_Wl_Seat *seat, Eina_Bool enabled);

E_API Eina_Bool e_comp_wl_input_keymap_cache_file_use_get(void);
E_API Eina_Stringshare *e_comp_wl_input_keymap_path_get(struct xkb_rule_names names);
E_API struct xkb_keymap *e_comp_wl_input_keymap_compile(struct xkb_context *ctx, struct xkb_rule_names names, char **keymap_path);
E_API void e_comp_wl_input_keymap_set(const char *rules, const char *model, const char *layout, const char *variant, const char *options, struct xkb_context *dflt_ctx, struct xkb_keymap *dflt_map);

E_API const char *e_comp_wl_input_keymap_default_rules_get(void);
E_API const char *e_comp_wl_input_keymap_default_model_get(void);
E_API const char *e_comp_wl_input_keymap_default_layout_get(void);
E_API const char *e_comp_wl_input_keymap_default_variant_get(void);
E_API const char *e_comp_wl_input_keymap_default_options_get(void);

# endif
#endif
