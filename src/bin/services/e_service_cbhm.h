#ifndef E_SERVICE_CBHM_H
#define E_SERVICE_CBHM_H

#include "e_policy_private_data.h"

EINTERN void          e_service_cbhm_client_set(E_Client *ec);
EINTERN E_Client     *e_service_cbhm_client_get(void);
EINTERN void          e_service_cbhm_show(void);
EINTERN void          e_service_cbhm_hide(void);
EINTERN void          e_service_cbhm_data_selected(void);
EINTERN void          e_service_cbhm_parent_set(E_Client *parent, Eina_Bool set);
#endif
