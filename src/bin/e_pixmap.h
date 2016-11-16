#ifdef E_TYPEDEFS

typedef struct _E_Pixmap E_Pixmap;

typedef enum
{
   E_PIXMAP_TYPE_X,
   E_PIXMAP_TYPE_WL,
   E_PIXMAP_TYPE_EXT_OBJECT,
   E_PIXMAP_TYPE_NONE,
   E_PIXMAP_TYPE_MAX
} E_Pixmap_Type;

#else
# ifndef E_PIXMAP_H
# define E_PIXMAP_H

typedef struct _E_Pixmap_Hook E_Pixmap_Hook;

typedef enum _E_Pixmap_Hook_Point
{
   E_PIXMAP_HOOK_NEW,
   E_PIXMAP_HOOK_DEL,
   E_PIXMAP_HOOK_USABLE,
   E_PIXMAP_HOOK_UNUSABLE,
   E_PIXMAP_HOOK_LAST
} E_Pixmap_Hook_Point;

typedef void (*E_Pixmap_Hook_Cb)(void *data, E_Pixmap *cp);

struct _E_Pixmap_Hook
{
   EINA_INLIST;
   E_Pixmap_Hook_Point hookpoint;
   E_Pixmap_Hook_Cb    func;
   void               *data;
   unsigned char       delete_me : 1;
};

E_API int e_pixmap_free(E_Pixmap *cp);
E_API void e_pixmap_win_id_del(E_Pixmap *cp);
E_API Eina_Bool e_pixmap_is_del(E_Pixmap *cp);
E_API E_Pixmap *e_pixmap_ref(E_Pixmap *cp);
E_API E_Pixmap *e_pixmap_new(E_Pixmap_Type type, ...);
E_API E_Pixmap_Type e_pixmap_type_get(const E_Pixmap *cp);
E_API void *e_pixmap_resource_get(E_Pixmap *cp);
E_API E_Comp_Client_Data *e_pixmap_cdata_get(E_Pixmap *cp);
E_API void e_pixmap_cdata_set(E_Pixmap *cp, E_Comp_Client_Data *cdata);
E_API void e_pixmap_resource_set(E_Pixmap *cp, void *resource);
E_API void e_pixmap_parent_window_set(E_Pixmap *cp, Ecore_Window win);
E_API unsigned int e_pixmap_failures_get(const E_Pixmap *cp);
E_API Eina_Bool e_pixmap_dirty_get(E_Pixmap *cp);
E_API void e_pixmap_clear(E_Pixmap *cp);
E_API void e_pixmap_usable_set(E_Pixmap *cp, Eina_Bool set);
E_API Eina_Bool e_pixmap_usable_get(const E_Pixmap *cp);
E_API void e_pixmap_dirty(E_Pixmap *cp);
E_API Eina_Bool e_pixmap_refresh(E_Pixmap *cp);
E_API Eina_Bool e_pixmap_size_changed(E_Pixmap *cp, int w, int h);
E_API Eina_Bool e_pixmap_size_get(E_Pixmap *cp, int *w, int *h);
E_API void e_pixmap_client_set(E_Pixmap *cp, E_Client *ec);
E_API E_Client *e_pixmap_client_get(E_Pixmap *cp);
E_API E_Pixmap *e_pixmap_find(E_Pixmap_Type type, ...);
E_API E_Client *e_pixmap_find_client(E_Pixmap_Type type, ...);
E_API E_Client *e_pixmap_find_client_by_res_id(uint32_t res_id);
E_API uint32_t e_pixmap_res_id_get(E_Pixmap *cp);
E_API uint64_t e_pixmap_window_get(E_Pixmap *cp);
E_API Ecore_Window e_pixmap_parent_window_get(E_Pixmap *cp);
E_API Eina_Bool e_pixmap_native_surface_init(E_Pixmap *cp, Evas_Native_Surface *ns);
E_API void e_pixmap_image_clear(E_Pixmap *cp, Eina_Bool cache);
E_API Eina_Bool e_pixmap_image_refresh(E_Pixmap *cp);
E_API Eina_Bool e_pixmap_image_exists(const E_Pixmap *cp);
E_API Eina_Bool e_pixmap_image_is_argb(const E_Pixmap *cp);
E_API void *e_pixmap_image_data_get(E_Pixmap *cp);
E_API void e_pixmap_image_data_ref(E_Pixmap *cp);
E_API Eina_Bool e_pixmap_image_data_argb_convert(E_Pixmap *cp, void *pix, void *ipix, Eina_Rectangle *r, int stride);

E_API void e_pixmap_image_opaque_set(E_Pixmap *cp, int x, int y, int w, int h);
E_API void e_pixmap_image_opaque_get(E_Pixmap *cp, int *x, int *y, int *w, int *h);

E_API E_Pixmap_Hook *e_pixmap_hook_add(E_Pixmap_Hook_Point hookpoint, E_Pixmap_Hook_Cb func, const void *data);
E_API void e_pixmap_hook_del(E_Pixmap_Hook *ph);

E_API Eina_Bool e_pixmap_init(void);
E_API void e_pixmap_shutdown(void);
E_API void e_pixmap_buffer_clear(E_Pixmap *cp);

static inline Eina_Bool
e_pixmap_is_x(const E_Pixmap *cp)
{
   return cp && e_pixmap_type_get(cp) == E_PIXMAP_TYPE_X;
}

# endif

#endif
