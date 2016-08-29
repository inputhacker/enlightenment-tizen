#ifdef E_TYPEDEFS


#else
#ifndef E_SPLITLAYOUT_H
#define E_SPLITLAYOUT_H


E_API Evas_Object                *e_splitlayout_add               (E_Desk *desk);
E_API int                         e_splitlayout_freeze            (Evas_Object *obj);
E_API int                         e_splitlayout_thaw              (Evas_Object *obj);
E_API void                        e_splitlayout_unpack            (Evas_Object *obj);
E_API void                        e_splitlayout_pack              (Evas_Object *obj, Evas_Object *child);
E_API void                        e_splitlayout_child_move        (Evas_Object *obj, Evas_Coord x, Evas_Coord y);
E_API void                        e_splitlayout_child_resize      (Evas_Object *obj, Evas_Coord w, Evas_Coord h);
E_API void                        e_splitlayout_child_raise       (Evas_Object *obj);
E_API void                        e_splitlayout_child_lower       (Evas_Object *obj);
E_API void                        e_splitlayout_child_raise_above (Evas_Object *obj, Evas_Object *above);
E_API void                        e_splitlayout_child_lower_below (Evas_Object *obj, Evas_Object *below);
E_API void                        e_splitlayout_child_geometry_get(Evas_Object *obj, Evas_Coord *x, Evas_Coord *y, Evas_Coord *w, Evas_Coord *h);
E_API Eina_List                  *e_splitlayout_children_get      (Evas_Object *obj);

#endif
#endif
