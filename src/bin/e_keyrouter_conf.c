#include "e_keyrouter_private.h"

void
e_keyrouter_conf_init(E_Keyrouter_Config_Data *kconfig)
{
   kconfig->conf_hwkeys_edd= E_CONFIG_DD_NEW("E_Keyrouter_Config_Key",
                                               E_Keyrouter_Tizen_HWKey);
#undef T
#undef D
#define T E_Keyrouter_Tizen_HWKey
#define D kconfig->conf_hwkeys_edd
   E_CONFIG_VAL(D, T, name, STR);
   E_CONFIG_VAL(D, T, keycode, INT);
   E_CONFIG_VAL(D, T, no_privcheck, INT);
   E_CONFIG_VAL(D, T, repeat, INT);

   kconfig->conf_edd = E_CONFIG_DD_NEW("Keyrouter_Config", E_Keyrouter_Conf_Edd);
#undef T
#undef D
#define T E_Keyrouter_Conf_Edd
#define D kconfig->conf_edd
   E_CONFIG_VAL(D, T, num_keycode, INT);
   E_CONFIG_VAL(D, T, max_keycode, INT);
   E_CONFIG_VAL(D, T, pictureoff_disabled, INT);
   E_CONFIG_LIST(D, T, KeyList, kconfig->conf_hwkeys_edd);

#undef T
#undef D
   kconfig->conf = e_config_domain_load("module.keyrouter", kconfig->conf_edd);

   if (!kconfig->conf)
     {
        KLDBG("Failed to find module.keyrouter config file.");
     }
}

void
e_keyrouter_conf_deinit(E_Keyrouter_Config_Data *kconfig)
{
   E_Keyrouter_Tizen_HWKey *kdata;

   if (kconfig->conf)
     {
        EINA_LIST_FREE(kconfig->conf->KeyList, kdata)
          {
             eina_stringshare_del(kdata->name);
             free(kdata);
          }
        free(kconfig->conf);
     }

   E_CONFIG_DD_FREE(kconfig->conf_hwkeys_edd);
   E_CONFIG_DD_FREE(kconfig->conf_edd);
}

