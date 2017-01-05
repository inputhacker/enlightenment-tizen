#ifndef E_POLICY_KEYBOARD_H
#define E_POLICY_KEYBOARD_H
#include <e.h>

E_API   Eina_Bool e_policy_client_is_keyboard(E_Client *ec);
E_API   Eina_Bool e_policy_client_is_keyboard_sub(E_Client *ec);
EINTERN void      e_policy_keyboard_layout_apply(E_Client *ec);

#endif
