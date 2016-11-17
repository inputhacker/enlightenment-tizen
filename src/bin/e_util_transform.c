#include "e.h"
#define E_UTIL_TRANSFORM_IS_ZERO(p) ((p) > -1e-21 && (p) < 1e-21)

static void _e_util_transform_value_merge(E_Util_Transform_Value *inout, E_Util_Transform_Value *input);
static void _e_util_transform_value_convert_to_matrix(E_Util_Transform_Matrix *out,
                                                      E_Util_Transform_Value *value,
                                                      E_Util_Transform_Rect *source_rect);

E_API E_Util_Transform*
e_util_transform_new(void)
{
   E_Util_Transform *transform = (E_Util_Transform*)malloc(sizeof(E_Util_Transform));
   if (transform)
     {
        transform->ref_count = 0;
        e_util_transform_init(transform);
        e_util_transform_ref(transform);
     }
   return transform;
}

E_API void
e_util_transform_del(E_Util_Transform *transform)
{
   if (!transform) return;
   e_util_transform_unref(transform);
}

E_API void
e_util_transform_copy(E_Util_Transform *dest, E_Util_Transform *source)
{
   if ((!dest) || (!source)) return;

   int back_ref_count = dest->ref_count;
   memcpy(dest, source, sizeof(E_Util_Transform));
   dest->ref_count = back_ref_count;
   dest->changed = EINA_TRUE;
}

E_API void
e_util_transform_init(E_Util_Transform *transform)
{
   int back_ref_count = 0;
   if (!transform) return;

   back_ref_count = transform->ref_count;
   memset(transform, 0, sizeof(E_Util_Transform));
   transform->transform.scale[0] = 1.0;
   transform->transform.scale[1] = 1.0;
   transform->transform.scale[2] = 1.0;
   transform->bg_transform.scale[0] = 1.0;
   transform->bg_transform.scale[1] = 1.0;
   transform->bg_transform.scale[2] = 1.0;
   transform->changed = EINA_TRUE;
   transform->ref_count = back_ref_count;
}

E_API void
e_util_transform_ref(E_Util_Transform *transform)
{
   if (!transform) return;
   transform->ref_count += 1;
}

E_API void
e_util_transform_unref(E_Util_Transform *transform)
{
   if (!transform) return;
   transform->ref_count -= 1;
   if (transform->ref_count <= 0)
     free(transform);
}

E_API int
e_util_transform_ref_count_get(E_Util_Transform *transform)
{
   if (!transform) return 0;
   return transform->ref_count;
}

E_API void
e_util_transform_move(E_Util_Transform *transform, double x, double y, double z)
{
   if (!transform) return;

   transform->transform.move[0] = x;
   transform->transform.move[1] = y;
   transform->transform.move[2] = z;
   transform->changed = EINA_TRUE;
}

E_API void
e_util_transform_scale(E_Util_Transform *transform, double sx, double sy, double sz)
{
   if (!transform) return;

   transform->transform.scale[0] = sx;
   transform->transform.scale[1] = sy;
   transform->transform.scale[2] = sz;
   transform->changed = EINA_TRUE;
}

E_API void
e_util_transform_rotation(E_Util_Transform *transform, double rx, double ry, double rz)
{
   if (!transform) return;

   transform->transform.rotation[0] = rx;
   transform->transform.rotation[1] = ry;
   transform->transform.rotation[2] = rz;
   transform->changed = EINA_TRUE;
}

E_API void
e_util_transform_bg_move(E_Util_Transform *transform, double x, double y, double z)
{
   if (!transform) return;

   transform->bg_transform.move[0] = x;
   transform->bg_transform.move[1] = y;
   transform->bg_transform.move[2] = z;
   transform->use_bg_transform = EINA_TRUE;
   transform->changed = EINA_TRUE;
}

E_API void
e_util_transform_bg_scale(E_Util_Transform *transform, double sx, double sy, double sz)
{
   if (!transform) return;

   transform->bg_transform.scale[0] = sx;
   transform->bg_transform.scale[1] = sy;
   transform->bg_transform.scale[2] = sz;
   transform->use_bg_transform = EINA_TRUE;
   transform->changed = EINA_TRUE;
}

E_API void
e_util_transform_bg_rotation(E_Util_Transform *transform, double rx, double ry, double rz)
{
   if (!transform) return;

   transform->bg_transform.rotation[0] = rx;
   transform->bg_transform.rotation[1] = ry;
   transform->bg_transform.rotation[2] = rz;
   transform->use_bg_transform = EINA_TRUE;
   transform->changed = EINA_TRUE;
}

E_API void
e_util_transform_texcoord_set(E_Util_Transform *transform, int index, double tu, double tv)
{
   if (!transform) return;
   if (index < 0 || index > 3) return;

   transform->texcoord.value[index][0] = tu;
   transform->texcoord.value[index][1] = tv;
   transform->use_texcoord = EINA_TRUE;
   transform->changed = EINA_TRUE;
}

E_API void
e_util_transform_viewport_set(E_Util_Transform *transform, int x, int y, int w, int h)
{
   if (!transform) return;

   transform->viewport.x = x;
   transform->viewport.y = y;
   transform->viewport.w = w;
   transform->viewport.h = h;
   transform->use_viewport = EINA_TRUE;
   transform->changed = EINA_TRUE;
}

E_API void
e_util_transform_merge(E_Util_Transform *in_out, E_Util_Transform *input)
{
   int i;

   if (!in_out) return;
   if (!input) return;

   if ((in_out->use_bg_transform) || (input->use_bg_transform))
     {
        if (!in_out->use_bg_transform)
          memcpy(&in_out->bg_transform, &in_out->transform, sizeof(E_Util_Transform_Value));

        if (input->use_bg_transform)
          _e_util_transform_value_merge(&in_out->bg_transform, &input->bg_transform);
        else
          _e_util_transform_value_merge(&in_out->bg_transform, &input->transform);
     }

   _e_util_transform_value_merge(&in_out->transform, &input->transform);

   // texcoord and viewport just one setting.
   if (input->use_texcoord)
     memcpy(&in_out->texcoord, &input->texcoord, sizeof(input->texcoord));
   if (input->use_viewport)
     memcpy(&in_out->viewport, &input->viewport, sizeof(input->viewport));

   in_out->use_texcoord |= input->use_texcoord;
   in_out->use_viewport |= input->use_viewport;
   in_out->use_bg_transform |= input->use_bg_transform;

   in_out->changed = EINA_TRUE;
}

E_API E_Util_Transform_Matrix
e_util_transform_convert_to_matrix(E_Util_Transform *transform, E_Util_Transform_Rect *source_rect)
{
   E_Util_Transform_Matrix result;

   if (!transform) return result;
   if (!source_rect) return result;

   _e_util_transform_value_convert_to_matrix(&result, &transform->transform, source_rect);

   return result;
}

E_API E_Util_Transform_Matrix
e_util_transform_bg_convert_to_matrix(E_Util_Transform *transform, E_Util_Transform_Rect *source_rect)
{
   E_Util_Transform_Matrix result;

   if (!transform) return result;
   if (!source_rect) return result;

   _e_util_transform_value_convert_to_matrix(&result, &transform->bg_transform, source_rect);

   return result;
}

E_API Eina_Bool
e_util_transform_change_get(E_Util_Transform *transform)
{
   if (!transform) return EINA_FALSE;
   return transform->changed;
}

E_API void
e_util_transform_change_unset(E_Util_Transform *transform)
{
   if (!transform) return;
   transform->changed = EINA_FALSE;
}

E_API void
e_util_transform_move_get(E_Util_Transform *transform, double *x, double *y, double *z)
{
   if (!transform) return;
   if (x) *x = transform->transform.move[0];
   if (y) *y = transform->transform.move[1];
   if (z) *z = transform->transform.move[2];
}

E_API void
e_util_transform_scale_get(E_Util_Transform *transform, double *x, double *y, double *z)
{
   if (!transform) return;
   if (x) *x = transform->transform.scale[0];
   if (y) *y = transform->transform.scale[1];
   if (z) *z = transform->transform.scale[2];
}

E_API void
e_util_transform_rotation_get(E_Util_Transform *transform, double *x, double *y, double *z)
{
   if (!transform) return;
   if (x) *x = transform->transform.rotation[0];
   if (y) *y = transform->transform.rotation[1];
   if (z) *z = transform->transform.rotation[2];
}

E_API void
e_util_transform_bg_move_get(E_Util_Transform *transform, double *x, double *y, double *z)
{
   if (!transform) return;
   if (x) *x = transform->bg_transform.move[0];
   if (y) *y = transform->bg_transform.move[1];
   if (z) *z = transform->bg_transform.move[2];
}

E_API void
e_util_transform_bg_scale_get(E_Util_Transform *transform, double *x, double *y, double *z)
{
   if (!transform) return;
   if (x) *x = transform->bg_transform.scale[0];
   if (y) *y = transform->bg_transform.scale[1];
   if (z) *z = transform->bg_transform.scale[2];
}

E_API void
e_util_transform_bg_rotation_get(E_Util_Transform *transform, double *x, double *y, double *z)
{
   if (!transform) return;
   if (x) *x = transform->bg_transform.rotation[0];
   if (y) *y = transform->bg_transform.rotation[1];
   if (z) *z = transform->bg_transform.rotation[2];
}

E_API void
e_util_transform_texcoord_get(E_Util_Transform *transform,int index, double *tu, double *tv)
{
   if (!transform) return;
   if (index < 0 || index > 3) return;

   if (tu) *tu = transform->texcoord.value[index][0];
   if (tv) *tv = transform->texcoord.value[index][1];
}

E_API void
e_util_transform_viewport_get(E_Util_Transform *transform, int *x, int *y, int *w, int *h)
{
   if (!transform) return;

   if (x) *x = transform->viewport.x;
   if (y) *y = transform->viewport.y;
   if (w) *w = transform->viewport.w;
   if (h) *h = transform->viewport.h;
}

E_API Eina_Bool
e_util_transform_texcoord_flag_get(E_Util_Transform *transform)
{
   if (!transform) return EINA_FALSE;
   return transform->use_texcoord;
}

E_API Eina_Bool
e_util_transform_viewport_flag_get(E_Util_Transform *transform)
{
   if (!transform) return EINA_FALSE;
   return transform->use_viewport;
}

E_API Eina_Bool
e_util_transform_bg_transform_flag_get(E_Util_Transform *transform)
{
   if (!transform) return EINA_FALSE;
   return transform->use_bg_transform;
}

E_API void
e_util_transform_rect_init(E_Util_Transform_Rect *rect, int x, int y, int w, int h)
{
   if (!rect) return;

   rect->x = x;
   rect->y = y;
   rect->w = w;
   rect->h = h;
}

E_API void
e_util_transform_rect_client_rect_get(E_Util_Transform_Rect *rect, E_Client *ec)
{
   if (!rect || !ec) return;
   e_util_transform_rect_init(rect, ec->x, ec->y, ec->w, ec->h);
}

E_API E_Util_Transform_Rect_Vertex
e_util_transform_rect_to_vertices(E_Util_Transform_Rect *rect)
{
   E_Util_Transform_Rect_Vertex result;
   int i;

   e_util_transform_vertices_init(&result);

   if (!rect) return result;

   // LT, RT, RB, LB
   result.vertices[0].vertex[0] = rect->x;
   result.vertices[0].vertex[1] = rect->y;

   result.vertices[1].vertex[0] = rect->x + rect->w;
   result.vertices[1].vertex[1] = rect->y;

   result.vertices[2].vertex[0] = rect->x + rect->w;
   result.vertices[2].vertex[1] = rect->y + rect->h;

   result.vertices[3].vertex[0] = rect->x;
   result.vertices[3].vertex[1] = rect->y + rect->h;

   for (i = 0 ; i < 4 ; ++i)
     {
        result.vertices[i].vertex[2] = 1.0;
        result.vertices[i].vertex[3] = 1.0;
     }

   return result;
}


E_API void
e_util_transform_vertex_init(E_Util_Transform_Vertex *vertex, double x, double y, double z, double w)
{
   if (!vertex) return;

   vertex->vertex[0] = x;
   vertex->vertex[1] = y;
   vertex->vertex[2] = z;
   vertex->vertex[3] = w;
}

E_API void
e_util_transform_vertex_pos_get(E_Util_Transform_Vertex *vertex, double *x, double *y, double *z, double *w)
{
   if (!vertex) return;

   if (x) *x = vertex->vertex[0];
   if (y) *y = vertex->vertex[1];
   if (z) *z = vertex->vertex[2];
   if (w) *w = vertex->vertex[3];
}

E_API void
e_util_transform_vertices_init(E_Util_Transform_Rect_Vertex *vertices)
{
   int i;
   if (!vertices) return;

   for (i = 0 ; i < 4 ; ++i)
     e_util_transform_vertex_init(&vertices->vertices[i], 0.0, 0.0, 0.0, 1.0);
}

E_API E_Util_Transform_Rect
e_util_transform_vertices_to_rect(E_Util_Transform_Rect_Vertex *vertices)
{
   E_Util_Transform_Rect result;
   e_util_transform_rect_init(&result, 0, 0, 0, 0);

   if (vertices)
     {
        result.x = (int)(vertices->vertices[0].vertex[0] + 0.5);
        result.y = (int)(vertices->vertices[0].vertex[1] + 0.5);
        result.w = (int)(vertices->vertices[2].vertex[0] - vertices->vertices[0].vertex[0] + 0.5);
        result.h = (int)(vertices->vertices[2].vertex[1] - vertices->vertices[0].vertex[1] + 0.5);
     }

   return result;
}

E_API void
e_util_transform_vertices_pos_get(E_Util_Transform_Rect_Vertex *vertices, int index,
                                  double *x, double *y, double *z, double *w)
{
   if (!vertices) return;
   if (index < 0 || index >= 4) return;

   e_util_transform_vertex_pos_get(&vertices->vertices[index], x, y, z, w);
}

E_API void
e_util_transform_matrix_load_identity(E_Util_Transform_Matrix *matrix)
{
   if (!matrix) return;

   matrix->mat[0][0] = 1; matrix->mat[0][1] = 0; matrix->mat[0][2] = 0; matrix->mat[0][3] = 0;
   matrix->mat[1][0] = 0; matrix->mat[1][1] = 1; matrix->mat[1][2] = 0; matrix->mat[1][3] = 0;
   matrix->mat[2][0] = 0; matrix->mat[2][1] = 0; matrix->mat[2][2] = 1; matrix->mat[2][3] = 0;
   matrix->mat[3][0] = 0; matrix->mat[3][1] = 0; matrix->mat[3][2] = 0; matrix->mat[3][3] = 1;
}

E_API void
e_util_transform_matrix_translate(E_Util_Transform_Matrix *matrix, double x, double y, double z)
{
   E_Util_Transform_Matrix source;

   if (!matrix) return;

   source = *matrix;

   //   | 1 0 0 dx|     |m00 m01 m02 m03|
   //   | 0 1 0 dy|     |m10 m11 m12 m13|
   //   | 0 0 1 dz|  *  |m20 m21 m22 m23|
   //   | 0 0 0 1 |     |m30 m31 m32 m33|

   matrix->mat[0][0] = source.mat[0][0] + x * source.mat[3][0];
   matrix->mat[0][1] = source.mat[0][1] + x * source.mat[3][1];
   matrix->mat[0][2] = source.mat[0][2] + x * source.mat[3][2];
   matrix->mat[0][3] = source.mat[0][3] + x * source.mat[3][3];

   matrix->mat[1][0] = source.mat[1][0] + y * source.mat[3][0];
   matrix->mat[1][1] = source.mat[1][1] + y * source.mat[3][1];
   matrix->mat[1][2] = source.mat[1][2] + y * source.mat[3][2];
   matrix->mat[1][3] = source.mat[1][3] + y * source.mat[3][3];

   matrix->mat[2][0] = source.mat[2][0] + z * source.mat[3][0];
   matrix->mat[2][1] = source.mat[2][1] + z * source.mat[3][1];
   matrix->mat[2][2] = source.mat[2][2] + z * source.mat[3][2];
   matrix->mat[2][3] = source.mat[2][3] + z * source.mat[3][3];
}

E_API void
e_util_transform_matrix_rotation_x(E_Util_Transform_Matrix *matrix, double degree)
{
   E_Util_Transform_Matrix source;
   double radian = 0.0;
   double s, c;

   if (!matrix) return;

   source = *matrix;
   radian = degree * M_PI / 180.0;
   s = sin(radian);
   c = cos(radian);

   //   | 1  0  0  0 |     |m00 m01 m02 m03|
   //   | 0  c -s  0 |     |m10 m11 m12 m13|
   //   | 0  s  c  0 |  *  |m20 m21 m22 m23|
   //   | 0  0  0  1 |     |m30 m31 m32 m33|

   matrix->mat[1][0] = c * source.mat[1][0] + (-s) * source.mat[2][0];
   matrix->mat[1][1] = c * source.mat[1][1] + (-s) * source.mat[2][1];
   matrix->mat[1][2] = c * source.mat[1][2] + (-s) * source.mat[2][2];
   matrix->mat[1][3] = c * source.mat[1][3] + (-s) * source.mat[2][3];

   matrix->mat[2][0] = s * source.mat[1][0] + c * source.mat[2][0];
   matrix->mat[2][1] = s * source.mat[1][1] + c * source.mat[2][1];
   matrix->mat[2][2] = s * source.mat[1][2] + c * source.mat[2][2];
   matrix->mat[2][3] = s * source.mat[1][3] + c * source.mat[2][3];
}

E_API void
e_util_transform_matrix_rotation_y(E_Util_Transform_Matrix *matrix, double degree)
{
   E_Util_Transform_Matrix source;
   double radian = 0.0;
   double s, c;

   if (!matrix) return;

   source = *matrix;
   radian = degree * M_PI / 180.0;
   s = sin(radian);
   c = cos(radian);

   //   | c  0  s  0 |     |m00 m01 m02 m03|
   //   | 0  1  0  0 |     |m10 m11 m12 m13|
   //   |-s  0  c  0 |  *  |m20 m21 m22 m23|
   //   | 0  0  0  1 |     |m30 m31 m32 m33|

   matrix->mat[0][0] = c * source.mat[0][0] + s * source.mat[2][0];
   matrix->mat[0][1] = c * source.mat[0][1] + s * source.mat[2][1];
   matrix->mat[0][2] = c * source.mat[0][2] + s * source.mat[2][2];
   matrix->mat[0][3] = c * source.mat[0][3] + s * source.mat[2][3];

   matrix->mat[2][0] = (-s) * source.mat[0][0] + c * source.mat[2][0];
   matrix->mat[2][0] = (-s) * source.mat[0][1] + c * source.mat[2][1];
   matrix->mat[2][0] = (-s) * source.mat[0][2] + c * source.mat[2][2];
   matrix->mat[2][0] = (-s) * source.mat[0][3] + c * source.mat[2][3];
}

E_API void
e_util_transform_matrix_rotation_z(E_Util_Transform_Matrix *matrix, double degree)
{
   E_Util_Transform_Matrix source;
   double radian = 0.0;
   double s, c;

   if (!matrix) return;

   source = *matrix;
   radian = degree * M_PI / 180.0;
   s = sin(radian);
   c = cos(radian);

   //   | c -s  0  0 |     |m00 m01 m02 m03|
   //   | s  c  0  0 |     |m10 m11 m12 m13|
   //   | 0  0  1  0 |  *  |m20 m21 m22 m23|
   //   | 0  0  0  1 |     |m30 m31 m32 m33|

   matrix->mat[0][0] = c * source.mat[0][0] + (-s) * source.mat[1][0];
   matrix->mat[0][1] = c * source.mat[0][1] + (-s) * source.mat[1][1];
   matrix->mat[0][2] = c * source.mat[0][2] + (-s) * source.mat[1][2];
   matrix->mat[0][3] = c * source.mat[0][3] + (-s) * source.mat[1][3];

   matrix->mat[1][0] = s * source.mat[0][0] + c * source.mat[1][0];
   matrix->mat[1][1] = s * source.mat[0][1] + c * source.mat[1][1];
   matrix->mat[1][2] = s * source.mat[0][2] + c * source.mat[1][2];
   matrix->mat[1][3] = s * source.mat[0][3] + c * source.mat[1][3];
}

E_API void
e_util_transform_matrix_scale(E_Util_Transform_Matrix *matrix, double sx, double sy, double sz)
{
   E_Util_Transform_Matrix source;

   if (!matrix) return;

   source = *matrix;

   //   | sx 0 0 0|     |m00 m01 m02 m03|
   //   | 0 sy 0 0|     |m10 m11 m12 m13|
   //   | 0 0 sz 0|  *  |m20 m21 m22 m23|
   //   | 0 0  0 1|     |m30 m31 m32 m33|

   matrix->mat[0][0] = sx * source.mat[0][0];
   matrix->mat[0][1] = sx * source.mat[0][1];
   matrix->mat[0][2] = sx * source.mat[0][2];
   matrix->mat[0][3] = sx * source.mat[0][3];

   matrix->mat[1][0] = sy * source.mat[1][0];
   matrix->mat[1][1] = sy * source.mat[1][1];
   matrix->mat[1][2] = sy * source.mat[1][2];
   matrix->mat[1][3] = sy * source.mat[1][3];

   matrix->mat[2][0] = sz * source.mat[2][0];
   matrix->mat[2][1] = sz * source.mat[2][1];
   matrix->mat[2][2] = sz * source.mat[2][2];
   matrix->mat[2][3] = sz * source.mat[2][3];
}

E_API E_Util_Transform_Matrix
e_util_transform_matrix_multiply(E_Util_Transform_Matrix *matrix1,
                                 E_Util_Transform_Matrix *matrix2)
{
   E_Util_Transform_Matrix result;
   int row, col, i;
   e_util_transform_matrix_load_identity(&result);

   if (!matrix1) return result;
   if (!matrix2) return result;
   //   |m00 m01 m02 m03|     |m00 m01 m02 m03|
   //   |m10 m11 m12 m13|     |m10 m11 m12 m13|
   //   |m20 m21 m22 m23|  *  |m20 m21 m22 m23|
   //   |m30 m31 m32 m33|     |m30 m31 m32 m33|

   for (row = 0 ; row < 4 ; ++row)
     {
        for (col = 0 ; col < 4; ++col)
          {
             double sum = 0.0;

             for (i = 0 ; i < 4 ; ++i)
               {
                  sum += matrix1->mat[row][i] * matrix2->mat[i][col];
               }

             result.mat[row][col] = sum;
          }
     }

   return result;
}

E_API E_Util_Transform_Vertex
e_util_transform_matrix_multiply_vertex(E_Util_Transform_Matrix *matrix,
                                        E_Util_Transform_Vertex *vertex)
{
   E_Util_Transform_Vertex result;
   int row, col;

   e_util_transform_vertex_init(&result, 0.0, 0.0, 0.0, 1.0);
   if (!vertex) return result;
   if (!matrix) return result;

   //   |m00 m01 m02 m03|     |x|
   //   |m10 m11 m12 m13|     |y|
   //   |m20 m21 m22 m23|  *  |z|
   //   |m30 m31 m32 m33|     |w|

   for (row = 0 ; row < 4 ; ++row)
     {
        double sum = 0.0;

        for (col = 0 ; col < 4; ++col)
          {
             sum += matrix->mat[row][col] * vertex->vertex[col];
          }

        result.vertex[row] = sum;
     }

   return result;
}

E_API E_Util_Transform_Rect_Vertex
e_util_transform_matrix_multiply_rect_vertex(E_Util_Transform_Matrix *matrix,
                                             E_Util_Transform_Rect_Vertex *vertices)
{
   E_Util_Transform_Rect_Vertex result;
   int i;
   e_util_transform_vertices_init(&result);

   if (!matrix) return result;
   if (!vertices) return result;

   for (i = 0 ; i < 4 ; ++i)
     result.vertices[i] = e_util_transform_matrix_multiply_vertex(matrix, &vertices->vertices[i]);

   return result;
}

E_API Eina_Bool
e_util_transform_matrix_equal_check(E_Util_Transform_Matrix *matrix,
                                    E_Util_Transform_Matrix *matrix2)
{
   int row, col;
   Eina_Bool result = EINA_TRUE;
   if (!matrix || !matrix2) return EINA_FALSE;

   for (row = 0 ; row < 4 && result ; ++row)
     {
        for (col = 0 ; col < 4 ; ++col)
          {
             if (matrix->mat[row][col] != matrix2->mat[row][col])
               {
                  result = EINA_FALSE;
                  break;
               }
          }
     }

   return result;
}

static void
_e_util_transform_value_merge(E_Util_Transform_Value *inout, E_Util_Transform_Value *input)
{
   int i;

   if ((!inout) || (!input)) return;

   for (i = 0 ; i < 3 ; ++i)
     {
        inout->move[i] *= input->scale[i];
        inout->move[i] += input->move[i];
        inout->scale[i] *= input->scale[i];
        inout->rotation[i] += input->rotation[i];
     }
}

static void
_e_util_transform_value_convert_to_matrix(E_Util_Transform_Matrix *out, E_Util_Transform_Value *value, E_Util_Transform_Rect *source_rect)
{
   if (!out) return;
   if (!value) return;
   if (!source_rect) return;

   e_util_transform_matrix_load_identity(out);

   double dest_w = source_rect->w * value->scale[0];
   double dest_h = source_rect->h * value->scale[1];

   e_util_transform_matrix_translate(out, -source_rect->x - source_rect->w / 2.0, -source_rect->y - source_rect->h / 2.0, 0.0);
   e_util_transform_matrix_scale(out, value->scale[0], value->scale[1], value->scale[2]);

   if (!E_UTIL_TRANSFORM_IS_ZERO(value->rotation[0]))
     e_util_transform_matrix_rotation_x(out, value->rotation[0]);
   if (!E_UTIL_TRANSFORM_IS_ZERO(value->rotation[1]))
     e_util_transform_matrix_rotation_y(out, value->rotation[1]);
   if (!E_UTIL_TRANSFORM_IS_ZERO(value->rotation[2]))
     e_util_transform_matrix_rotation_z(out, value->rotation[2]);

   e_util_transform_matrix_translate(out, source_rect->x + value->move[0] + (dest_w / 2.0),
                                     source_rect->y + value->move[1] + (dest_h / 2.0), 0.0);
}


// will delete function
E_API void
e_util_transform_source_to_target(E_Util_Transform *transform,
                                  E_Util_Transform_Rect *dest,
                                  E_Util_Transform_Rect *source)
{
}

E_API void
e_util_transform_log(E_Util_Transform *transform, const char *str)
{
}

E_API void
e_util_transform_keep_ratio_set(E_Util_Transform *transform, Eina_Bool enable)
{
}

E_API Eina_Bool
e_util_transform_keep_ratio_get(E_Util_Transform *transform)
{
   return EINA_FALSE;
}

E_API E_Util_Transform
e_util_transform_keep_ratio_apply(E_Util_Transform *transform, int origin_w, int origin_h)
{
   E_Util_Transform result;
   return result;
}