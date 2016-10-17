#ifdef E_TYPEDEFS


#else

#ifndef E_SLOT_H_
#define E_SLOT_H_


EINTERN void                  e_slot_init(void);
EINTERN int                   e_slot_shutdown(void);

E_API Evas_Object            *e_slot_new(Evas_Object *parent);
E_API void                    e_slot_del(Evas_Object *obj);
E_API void                    e_slot_move(Evas_Object *obj, int x, int y);
E_API void                    e_slot_resize(Evas_Object *obj, int w, int h);

E_API Evas_Object            *e_slot_find_by_id(int slot_id);
E_API int                     e_slot_find_id(Evas_Object *obj);
E_API void                    e_slot_update(Evas_Object *obj);

E_API Eina_Bool               e_slot_client_add(Evas_Object *obj, E_Client *ec, Eina_Bool resizable);
E_API Eina_Bool               e_slot_client_remove(Evas_Object *obj, E_Client *ec);
E_API Eina_List              *e_slot_client_list_get(Evas_Object *obj);
E_API int                     e_slot_client_count_get(Evas_Object *obj);
E_API int                     e_slot_client_slot_id_get(E_Client* ec);
E_API int                     e_slot_client_type_get(E_Client* ec);
E_API void                    e_slot_client_update(E_Client *ec);
E_API E_Client               *e_slot_client_top_find(Evas_Object *obj);


E_API void                    e_slot_raise(Evas_Object *obj);
E_API void                    e_slot_lower(Evas_Object *obj);
E_API void                    e_slot_focus_set(Evas_Object *obj);


E_API Eina_List              *e_slot_list_get(void); // list of e_slot smart obj

#endif
#endif
