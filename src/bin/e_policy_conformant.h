#ifndef E_POLICY_CONFORMANT_H
#define E_POLICY_CONFORMANT_H

EINTERN Eina_Bool           e_policy_conformant_init(void);
EINTERN void                e_policy_conformant_shutdown(void);

E_API Eina_Bool             e_policy_conformant_part_add(E_Client *ec);
E_API Eina_Bool             e_policy_conformant_part_del(E_Client *ec);

#endif
