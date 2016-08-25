#ifndef E_SERVICE_CBHM_H
#define E_SERVICE_CBHM_H

#include "e_policy_private_data.h"

EINTERN void          e_service_cbhm_client_set(E_Client *ec);
EINTERN E_Client     *e_service_cbhm_client_get(void);
EINTERN void          e_service_cbhm_show(void);
EINTERN void          e_service_cbhm_hide(void);

EINTERN void         e_cbhm_client_add(E_Client *ec);
EINTERN void         e_cbhm_client_del(E_Client *ec);
EINTERN void         e_cbhm_client_transient_for_set(E_Client *ec, Eina_Bool set);
EINTERN void         e_cbhm_client_show(E_Client *ec);
EINTERN void         e_cbhm_client_hide(E_Client *ec);

#endif
