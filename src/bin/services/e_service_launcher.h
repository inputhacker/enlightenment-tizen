#ifndef E_SERVICE_LAUNCHER_H
#define E_SERVICE_LAUNCHER_H

#include "e_policy_private_data.h"
#include <tzsh_server.h>

EINTERN void              e_service_launcher_resource_set(E_Client *ec, struct wl_resource *res_tws_lc);
EINTERN void              e_service_launcher_client_set(E_Client *ec);
EINTERN void              e_service_launcher_client_unset(E_Client *ec);
EINTERN void              e_service_launcher_prepare_send_with_shared_widget_info(E_Client *target_ec, const char *shared_widget_info, uint32_t state);

#endif
