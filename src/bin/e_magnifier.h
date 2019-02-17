# ifdef E_TYPEDEFS
typedef enum _E_Magnifier_Zoom_Ratio
{
   E_MAGNIFIER_ZOOM_RATIO_100 = 100,
   E_MAGNIFIER_ZOOM_RATIO_110 = 110,
   E_MAGNIFIER_ZOOM_RATIO_120 = 120,
   E_MAGNIFIER_ZOOM_RATIO_130 = 130,
   E_MAGNIFIER_ZOOM_RATIO_140 = 140,
   E_MAGNIFIER_ZOOM_RATIO_150 = 150,
   E_MAGNIFIER_ZOOM_RATIO_160 = 160,
   E_MAGNIFIER_ZOOM_RATIO_170 = 170,
   E_MAGNIFIER_ZOOM_RATIO_180 = 180,
   E_MAGNIFIER_ZOOM_RATIO_190 = 190,
   E_MAGNIFIER_ZOOM_RATIO_200 = 200,
} E_Magnifier_Zoom_Ratio;
# else

# ifndef E_MAGNIFIER_H
# define E_MAGNIFIER_H

#define E_MAGNIFIER_SMART_OBJ_TYPE "E_Magnifier_Smart_Object"

EINTERN int       e_magnifier_init(void);
EINTERN int       e_magnifier_shutdown(void);

E_API Eina_Bool   e_magnifier_new(void);
E_API void        e_magnifier_del(void);

E_API void        e_magnifier_show(void);
E_API void        e_magnifier_hide(void);

E_API Eina_Bool   e_magnifier_zoom_obj_ratio_set(E_Client *ec, E_Magnifier_Zoom_Ratio ratio);
E_API Eina_Bool   e_magnifier_zoom_obj_geometry_set(E_Client *ec, int angle, int x, int y, int w, int h);

EINTERN Eina_Bool e_magnifier_smart_member_add(E_Desk *desk, Evas_Object *obj);
EINTERN Eina_Bool e_magnifier_smart_member_del(Evas_Object *obj);

E_API Eina_Bool   e_magnifier_owner_set(E_Client *ec);

#endif
#endif



