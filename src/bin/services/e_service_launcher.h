#ifndef E_SERVICE_LAUNCHER_H
#define E_SERVICE_LAUNCHER_H

#include "e_policy_private_data.h"
#include <tzsh_server.h>

EINTERN void              e_service_launcher_resource_set(E_Client *ec, struct wl_resource *res_tws_lc);
EINTERN void              e_service_launcher_client_set(E_Client *ec);
EINTERN void              e_service_launcher_client_unset(E_Client *ec);

#endif
