#ifdef E_TYPEDEFS

typedef struct _E_Util_Transform_Value              E_Util_Transform_Value;
typedef struct _E_Util_Transform_Texcoord           E_Util_Transform_Texcoord;
typedef struct _E_Util_Transform                    E_Util_Transform;
typedef struct _E_Util_Transform_Rect               E_Util_Transform_Rect;
typedef struct _E_Util_Transform_Axis               E_Util_Transform_Axis;
typedef struct _E_Util_Transform_Vertex             E_Util_Transform_Vertex;
typedef struct _E_Util_Transform_Rect_Vertex        E_Util_Transform_Rect_Vertex;
typedef struct _E_Util_Transform_Matrix             E_Util_Transform_Matrix;


#else
#ifndef E_UTIL_TRANSFORM_H_
#define E_UTIL_TRANSFORM_H_

struct _E_Util_Transform_Texcoord
{
   double value[4][2];
};

struct _E_Util_Transform_Rect
{
   int x;
   int y;
   int w;
   int h;
};

struct _E_Util_Transform_Value
{
   double move[3];
   double scale[3];
   double rotation[3];
};

struct _E_Util_Transform_Axis
{
   double axis[3];
};

struct _E_Util_Transform
{
   E_Util_Transform_Value    transform;
   E_Util_Transform_Value    bg_transform;
   E_Util_Transform_Texcoord texcoord;
   E_Util_Transform_Rect     viewport;
   E_Util_Transform_Axis     rotation_axis;
   int                       ref_count;
   Eina_Bool                 changed;
   Eina_Bool                 use_texcoord;
   Eina_Bool                 use_viewport;
   Eina_Bool                 use_bg_transform;
   Eina_Bool                 use_axis;
};

struct _E_Util_Transform_Vertex
{
   double vertex[4];
};

struct _E_Util_Transform_Rect_Vertex
{
   E_Util_Transform_Vertex vertices[4];
};

struct _E_Util_Transform_Matrix
{
   double mat[4][4];
};

E_API E_Util_Transform            *e_util_transform_new(void);
E_API void                         e_util_transform_del(E_Util_Transform *transform);
E_API void                         e_util_transform_copy(E_Util_Transform *dest, E_Util_Transform *source);
E_API void                         e_util_transform_ref(E_Util_Transform *transform);
E_API void                         e_util_transform_unref(E_Util_Transform *transform);
E_API int                          e_util_transform_ref_count_get(E_Util_Transform *transform);
E_API void                         e_util_transform_init(E_Util_Transform *transform);
E_API void                         e_util_transform_move(E_Util_Transform *transform, double x, double y, double z);
E_API void                         e_util_transform_scale(E_Util_Transform *transform, double sx, double sy, double sz);
E_API void                         e_util_transform_rotation(E_Util_Transform *transform, double rx, double ry, double rz);
E_API void                         e_util_transform_bg_move(E_Util_Transform *transform, double x, double y, double z);
E_API void                         e_util_transform_bg_scale(E_Util_Transform *transform, double sx, double sy, double sz);
E_API void                         e_util_transform_bg_rotation(E_Util_Transform *transform, double rx, double ry, double rz);
E_API void                         e_util_transform_texcoord_set(E_Util_Transform *transform, int index, double tu, double tv);
E_API void                         e_util_transform_viewport_set(E_Util_Transform *transform, int x, int y, int w, int h);
E_API void                         e_util_transform_rotation_axis_set(E_Util_Transform *transform, double ax, double ay, double az);

E_API void                         e_util_transform_merge(E_Util_Transform *in_out, E_Util_Transform *input);
E_API E_Util_Transform_Matrix      e_util_transform_convert_to_matrix(E_Util_Transform *transform, E_Util_Transform_Rect *source_rect);
E_API E_Util_Transform_Matrix      e_util_transform_bg_convert_to_matrix(E_Util_Transform *transform, E_Util_Transform_Rect *source_rect);
E_API Eina_Bool                    e_util_transform_change_get(E_Util_Transform *transform);
E_API void                         e_util_transform_change_unset(E_Util_Transform *transform);

E_API void                         e_util_transform_move_get(E_Util_Transform *transform, double *x, double *y, double *z);
E_API void                         e_util_transform_move_round_get(E_Util_Transform *transform, int *x, int *y, int *z);
E_API void                         e_util_transform_scale_get(E_Util_Transform *transform, double *x, double *y, double *z);
E_API void                         e_util_transform_scale_round_get(E_Util_Transform *transform, int *x, int *y, int *z);
E_API void                         e_util_transform_rotation_get(E_Util_Transform *transform, double *x, double *y, double *z);
E_API void                         e_util_transform_rotation_round_get(E_Util_Transform *transform, int *x, int *y, int *z);
E_API void                         e_util_transform_bg_move_get(E_Util_Transform *transform, double *x, double *y, double *z);
E_API void                         e_util_transform_bg_move_round_get(E_Util_Transform *transform, int *x, int *y, int *z);
E_API void                         e_util_transform_bg_scale_get(E_Util_Transform *transform, double *x, double *y, double *z);
E_API void                         e_util_transform_bg_scale_round_get(E_Util_Transform *transform, int *x, int *y, int *z);
E_API void                         e_util_transform_bg_rotation_get(E_Util_Transform *transform, double *x, double *y, double *z);
E_API void                         e_util_transform_bg_rotation_round_get(E_Util_Transform *transform, int *x, int *y, int *z);


E_API void                         e_util_transform_texcoord_get(E_Util_Transform *transform, int index, double *tu, double *tv);
E_API void                         e_util_transform_viewport_get(E_Util_Transform *transform, int *x, int *y, int *w, int *h);
E_API void                         e_util_transform_rotation_axis_get(E_Util_Transform *transform, double *x, double *y, double *z);
E_API void                         e_util_transform_rotation_axis_round_get(E_Util_Transform *transform, int *x, int *y, int *z);
E_API Eina_Bool                    e_util_transform_texcoord_flag_get(E_Util_Transform *transform);
E_API Eina_Bool                    e_util_transform_viewport_flag_get(E_Util_Transform *transform);
E_API Eina_Bool                    e_util_transform_bg_transform_flag_get(E_Util_Transform *transform);
E_API Eina_Bool                    e_util_transform_rotation_axis_flag_get(E_Util_Transform *transform);

E_API void                         e_util_transform_rect_init(E_Util_Transform_Rect *rect, int x, int y, int w, int h);
E_API void                         e_util_transform_rect_client_rect_get(E_Util_Transform_Rect *rect, E_Client *ec);
E_API E_Util_Transform_Rect_Vertex e_util_transform_rect_to_vertices(E_Util_Transform_Rect *rect);

E_API void                         e_util_transform_vertex_init(E_Util_Transform_Vertex *vertex, double x, double y, double z, double w);
E_API void                         e_util_transform_vertex_pos_get(E_Util_Transform_Vertex *vertex, double *x, double *y, double *z, double *w);
E_API void                         e_util_transform_vertex_pos_round_get(E_Util_Transform_Vertex *vertex, int *x, int *y, int *z, int *w);

E_API void                         e_util_transform_vertices_init(E_Util_Transform_Rect_Vertex *vertices);
E_API E_Util_Transform_Rect        e_util_transform_vertices_to_rect(E_Util_Transform_Rect_Vertex *vertex);
E_API void                         e_util_transform_vertices_pos_get(E_Util_Transform_Rect_Vertex *vertices, int index,
                                                                     double *x, double *y, double *z, double *w);
E_API void                         e_util_transform_vertices_pos_round_get(E_Util_Transform_Rect_Vertex *vertices, int index,
		                                                                   int *x, int *y, int *z, int *w);

E_API void                         e_util_transform_matrix_load_identity(E_Util_Transform_Matrix *matrix);
E_API void                         e_util_transform_matrix_translate(E_Util_Transform_Matrix *matrix, double x, double y, double z);
E_API void                         e_util_transform_matrix_rotation_x(E_Util_Transform_Matrix *matrix, double degree);
E_API void                         e_util_transform_matrix_rotation_y(E_Util_Transform_Matrix *matrix, double degree);
E_API void                         e_util_transform_matrix_rotation_z(E_Util_Transform_Matrix *matrix, double degree);
E_API void                         e_util_transform_matrix_flip_x(E_Util_Transform_Matrix *matrix);
E_API void                         e_util_transform_matrix_flip_y(E_Util_Transform_Matrix *matrix);
E_API void                         e_util_transform_matrix_scale(E_Util_Transform_Matrix *matrix, double sx, double sy, double sz);
E_API E_Util_Transform_Matrix      e_util_transform_matrix_multiply(E_Util_Transform_Matrix *matrix1,
                                                                    E_Util_Transform_Matrix *matrix2);
E_API E_Util_Transform_Vertex      e_util_transform_matrix_multiply_vertex(E_Util_Transform_Matrix *matrix,
                                                                           E_Util_Transform_Vertex *vertex);
E_API E_Util_Transform_Rect_Vertex e_util_transform_matrix_multiply_rect_vertex(E_Util_Transform_Matrix *matrix,
                                                                                E_Util_Transform_Rect_Vertex *vertices);
E_API Eina_Bool                    e_util_transform_matrix_equal_check(E_Util_Transform_Matrix *matrix,
                                                                       E_Util_Transform_Matrix *matrix2);
E_API E_Util_Transform_Matrix      e_util_transform_matrix_inverse_get(E_Util_Transform_Matrix *matrix);

// will delete function
E_API void                         e_util_transform_source_to_target(E_Util_Transform *transform,
                                                                     E_Util_Transform_Rect *dest,
                                                                     E_Util_Transform_Rect *source);
E_API void                         e_util_transform_keep_ratio_set(E_Util_Transform *transform, Eina_Bool enable);
E_API Eina_Bool                    e_util_transform_keep_ratio_get(E_Util_Transform *transform);
E_API E_Util_Transform             e_util_transform_keep_ratio_apply(E_Util_Transform *transform, int origin_w, int origin_h);
E_API void                         e_util_transform_log(E_Util_Transform *transform, const char *str);

E_API void e_util_transform_matrix_inv_rect_coords_get(E_Util_Transform *transform, E_Util_Transform_Rect_Vertex *vetices, int w, int h, int x, int y, int *out_x, int *out_y);

#endif
#endif
