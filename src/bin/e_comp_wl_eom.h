#ifdef E_TYPEDEFS

#else
#ifndef E_COMP_WL_EOM_H
#define E_COMP_WL_EOM_H

int e_comp_wl_eom_init(void);
void e_comp_wl_eom_fini(void);
Eina_Bool e_comp_wl_eom_is_ec_external(E_Client *ec);
tdm_output* e_comp_wl_eom_tdm_output_by_ec_get(E_Client *ec);

#endif
#endif
