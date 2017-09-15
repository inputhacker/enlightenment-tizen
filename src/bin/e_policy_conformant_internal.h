#ifndef E_POLICY_CONFORMANT_INTERNAL_H
#define E_POLICY_CONFORMANT_INTERNAL_H

EINTERN void       e_policy_conformant_client_add(E_Client *ec, struct wl_resource *res);
EINTERN void       e_policy_conformant_client_del(E_Client *ec);
EINTERN Eina_Bool  e_policy_conformant_client_check(E_Client *ec);
EINTERN void       e_policy_conformant_client_ack(E_Client *ec, struct wl_resource *res, uint32_t serial);

#endif
