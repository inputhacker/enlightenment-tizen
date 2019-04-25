typedef struct _E_Policy_Appinfo E_Policy_Appinfo;

EINTERN E_Policy_Appinfo *e_policy_appinfo_new(void);
EINTERN void              e_policy_appinfo_del(E_Policy_Appinfo *epai);
EINTERN Eina_Bool         e_policy_appinfo_pid_set(E_Policy_Appinfo *epai, pid_t pid);
EINTERN Eina_Bool         e_policy_appinfo_appid_set(E_Policy_Appinfo *epai, const char *appid);
EINTERN Eina_Bool         e_policy_appinfo_base_output_resolution_get(E_Policy_Appinfo *epai, int *width, int *height);

E_API E_Policy_Appinfo *e_policy_appinfo_find_with_pid(pid_t pid);
E_API Eina_Bool         e_policy_appinfo_base_output_resolution_set(E_Policy_Appinfo *epai, int width, int height);


