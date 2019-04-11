#ifdef E_TYPEDEFS
typedef enum _E_Capture_Save_State
{
   E_CAPTURE_SAVE_STATE_START = 0,
   E_CAPTURE_SAVE_STATE_DONE,
   E_CAPTURE_SAVE_STATE_CANCEL,
   E_CAPTURE_SAVE_STATE_INVALID,
   E_CAPTURE_SAVE_STATE_BUSY,
} E_Capture_Save_State;

typedef void (*E_Capture_Client_Save_End_Cb)(void *data, E_Client *ec, const Eina_Stringshare *dest, E_Capture_Save_State state);

#else
# ifndef E_COMP_WL_CAPTURE_H
# define E_COMP_WL_CAPTURE_H
EINTERN void                 e_comp_wl_capture_init(void);
EINTERN void                 e_comp_wl_capture_shutdown(void);

E_API   E_Capture_Save_State e_comp_wl_capture_client_image_save(E_Client *ec, const char* path, const char* name, E_Capture_Client_Save_End_Cb func_end, void *data, Eina_Bool skip_child);
E_API   void                 e_comp_wl_capture_client_image_save_cancel(E_Client *ec);
# endif
#endif
