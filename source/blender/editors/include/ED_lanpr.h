/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2008 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup editors
 */

#ifndef __ED_LANPR_H__
#define __ED_LANPR_H__

#include <string.h>
#include "BLI_listbase.h"
#include "BLI_linklist.h"
#include "BLI_threads.h"

#include <math.h>
#include "BLI_math.h"

#include "DNA_lanpr_types.h"

#ifndef BYTE
#  define BYTE unsigned char
#endif

typedef double real;
typedef unsigned long long u64bit;
typedef unsigned int u32bit;
typedef unsigned short u16bit;
typedef unsigned short ushort;
typedef unsigned char u8bit;
typedef char nShortBuf[16];

typedef float tnsMatrix44f[16];

typedef real tnsVector2d[2];
typedef real tnsVector3d[3];
typedef real tnsVector4d[4];
typedef float tnsVector3f[3];
typedef float tnsVector4f[4];
typedef int tnsVector2i[2];

typedef struct LANPR_StaticMemPoolNode {
  Link item;
  int used_byte;
  /* User memory starts here */
} LANPR_StaticMemPoolNode;

typedef struct LANPR_StaticMemPool {
  int each_size;
  ListBase pools;
  SpinLock cs_mem;
} LANPR_StaticMemPool;

typedef struct LANPR_TextureSample {
  struct LANPR_TextureSample *next, *prev;
  int X, Y;
  /** For future usage */
  float Z;
} LANPR_TextureSample;

typedef struct LANPR_LineStripPoint {
  struct LANPR_LineStripPoint *next, *prev;
  float P[3];
} LANPR_LineStripPoint;

typedef struct LANPR_LineStrip {
  struct LANPR_LineStrip *next, *prev;
  ListBase points;
  int point_count;
  float total_length;
} LANPR_LineStrip;

typedef struct LANPR_RenderTriangle {
  struct LANPR_RenderTriangle *next, *prev;
  struct LANPR_RenderVert *v[3];
  struct LANPR_RenderLine *rl[3];
  real gn[3];
  real gc[3];
  /*  struct BMFace *F; */
  short material_id;
  ListBase intersecting_verts;
  char cull_status;
  /**  Should be testing** , Use testing[NumOfThreads] to access. */
  struct LANPR_RenderTriangle *testing;
} LANPR_RenderTriangle;

typedef struct LANPR_RenderTriangleThread {
  struct LANPR_RenderTriangle base;
  struct LANPR_RenderLine *testing[127];
} LANPR_RenderTriangleThread;

typedef struct LANPR_RenderElementLinkNode {
  struct LANPR_RenderElementLinkNode *next, *prev;
  void *pointer;
  int element_count;
  void *object_ref;
  char additional;
} LANPR_RenderElementLinkNode;

typedef struct LANPR_RenderLineSegment {
  struct LANPR_RenderLineSegment *next, *prev;
  /** at==0: left  at==1: right  (this is in 2D projected space) */
  real at;
  /** This is used to reconstruct 3d stroke  (TODO: implement global space?) */
  real at_global;
  /** Occlusion level after "at" point */
  u8bit occlusion;
  /** For determining lines beind a glass window material. (TODO: implement this) */
  short material_mask_mark;
} LANPR_RenderLineSegment;

typedef struct LANPR_RenderVert {
  struct LANPR_RenderVert *next, *prev;
  real gloc[4];
  real fbcoord[4];
  int fbcoordi[2];
  /**  Used as "r" when intersecting */
  struct BMVert *v;
  struct LANPR_RenderLine *intersecting_line;
  struct LANPR_RenderLine *intersecting_line2;
  struct LANPR_RenderTriangle *intersecting_with;

  /** positive 1     Negative 0
   *      <|              |>
   * l---->|----->r	l---->|----->r
   *      <|		          |>
   * this means dot(r-l,face_normal)<0 then 1 otherwise 0
   */
  char positive;
  char edge_used;
} LANPR_RenderVert;

typedef enum LANPR_EdgeFlag {
  LANPR_EDGE_FLAG_EDGE_MARK = (1 << 0),
  LANPR_EDGE_FLAG_CONTOUR = (1 << 1),
  LANPR_EDGE_FLAG_CREASE = (1 << 2),
  LANPR_EDGE_FLAG_MATERIAL = (1 << 3),
  LANPR_EDGE_FLAG_INTERSECTION = (1 << 4),
  /**  floating edge, unimplemented yet */
  LANPR_EDGE_FLAG_FLOATING = (1 << 5),
  LANPR_EDGE_FLAG_CHAIN_PICKED = (1 << 6),
} LANPR_EdgeFlag;

#define LANPR_EDGE_FLAG_ALL_TYPE 0x3f

typedef struct LANPR_RenderLine {
  struct LANPR_RenderLine *next, *prev;
  struct LANPR_RenderVert *l, *r;
  struct LANPR_RenderTriangle *tl, *tr;
  ListBase segments;
  char min_occ;

  /**  Also for line type determination on chainning */
  char flags;

  /**  Still need this entry because culled lines will not add to object reln node */
  struct Object *object_ref;

  /**  For gpencil stroke modifier */
  int edge_idx;
} LANPR_RenderLine;

typedef struct LANPR_RenderLineChain {
  struct LANPR_RenderLineChain *next, *prev;
  ListBase chain;

  /**  Calculated before draw cmd. */
  float length;

  /**  Used when re-connecting and gp stroke generation */
  char picked;
  char level;

  /** Chain now only contains one type of segments */
  int type;
  struct Object *object_ref;
} LANPR_RenderLineChain;

typedef struct LANPR_RenderLineChainItem {
  struct LANPR_RenderLineChainItem *next, *prev;
  /** Need z value for fading */
  float pos[3];
  /** For restoring position to 3d space */
  float gpos[3];
  float normal[3];
  char line_type;
  char occlusion;
} LANPR_RenderLineChainItem;

typedef struct LANPR_ChainRegisterEntry {
  struct LANPR_ChainRegisterEntry *next, *prev;
  LANPR_RenderLineChain *rlc;
  LANPR_RenderLineChainItem *rlci;
  char picked;

  /** left/right mark.
   * Because we revert list in chaining so we need the flag. */
  char is_left;
} LANPR_ChainRegisterEntry;

typedef struct LANPR_RenderBuffer {
  struct LANPR_RenderBuffer *prev, *next;

  /** For render. */
  int is_copied;

  int w, h;
  int tile_size_w, tile_size_h;
  int tile_count_x, tile_count_y;
  real width_per_tile, height_per_tile;
  double view_projection[4][4];

  int output_mode;
  int output_aa_level;

  struct LANPR_BoundingArea *initial_bounding_areas;
  u32bit bounding_area_count;

  ListBase vertex_buffer_pointers;
  ListBase line_buffer_pointers;
  ListBase triangle_buffer_pointers;
  ListBase all_render_lines;

  ListBase intersecting_vertex_buffer;

  struct GPUBatch *DPIXIntersectionTransformBatch;
  struct GPUBatch *DPIXIntersectionBatch;

  /** Use the one comes with LANPR. */
  LANPR_StaticMemPool render_data_pool;

  struct Material *material_pointers[2048];

  /*  Render status */

  int cached_for_frame;

  real view_vector[3];

  int triangle_size;

  u32bit contour_count;
  u32bit contour_processed;
  LinkData *contour_managed;
  ListBase contours;

  u32bit intersection_count;
  u32bit intersection_processed;
  LinkData *intersection_managed;
  ListBase intersection_lines;

  u32bit crease_count;
  u32bit crease_processed;
  LinkData *crease_managed;
  ListBase crease_lines;

  u32bit material_line_count;
  u32bit material_processed;
  LinkData *material_managed;
  ListBase material_lines;

  u32bit edge_mark_count;
  u32bit edge_mark_processed;
  LinkData *edge_mark_managed;
  ListBase edge_marks;

  ListBase chains;
  struct GPUBatch *chain_draw_batch;

  struct DRWShadingGroup *ChainShgrp;

  SpinLock cs_info;
  SpinLock cs_data;
  SpinLock cs_management;

  /*  settings */

  int max_occlusion_level;
  real crease_angle;
  real crease_cos;
  int thread_count;

  /** Deprecated, need another report mechanism */
  real overall_progress;
  int calculation_status;

  int draw_material_preview;
  real material_transparency;

  int show_line;
  int show_fast;
  int show_material;
  int override_display;

  struct Scene *scene;
  struct Object *camera;

  int enable_intersections;
  int _pad;

} LANPR_RenderBuffer;

typedef struct LANPR_SharedResource {

  /* We only allocate once for all */
  LANPR_RenderBuffer *render_buffer_shared;

  /* cache */
  struct BLI_mempool *mp_sample;
  struct BLI_mempool *mp_line_strip;
  struct BLI_mempool *mp_line_strip_point;
  struct BLI_mempool *mp_batch_list;

  /* Image filtering */
  struct GPUShader *multichannel_shader;
  struct GPUShader *edge_detect_shader;
  struct GPUShader *edge_thinning_shader;
  struct GPUShader *snake_connection_shader;

  /* GPU */
  struct GPUShader *dpix_transform_shader;
  struct GPUShader *dpix_preview_shader;
  int dpix_shader_error;
  int texture_size;
  ListBase dpix_batch_list;
  int dpix_reloaded;
  int dpix_reloaded_deg;

  /* CPU */
  struct GPUShader *software_shader;
  struct GPUShader *software_chaining_shader;

  void *ved_viewport;
  void *ved_render;

  int init_complete;

  SpinLock render_flag_lock;

  /** Set before rendering and cleared upon finish! */
  struct RenderEngine *re_render;
} LANPR_SharedResource;

#define DBL_TRIANGLE_LIM 1e-8
#define DBL_EDGE_LIM 1e-9

#define LANPR_MEMORY_POOL_1MB 1048576
#define LANPR_MEMORY_POOL_128MB 134217728
#define LANPR_MEMORY_POOL_256MB 268435456
#define LANPR_MEMORY_POOL_512MB 536870912

typedef enum LANPR_CullState {
  LANPR_CULL_DONT_CARE = 0,
  LANPR_CULL_USED = 1,
  LANPR_CULL_DISCARD = 2,
} LANPR_CullState;

#define TNS_THREAD_LINE_COUNT 10000

typedef struct LANPR_RenderTaskInfo {
  int thread_id;

  LinkData *contour;
  ListBase contour_pointers;

  LinkData *intersection;
  ListBase intersection_pointers;

  LinkData *crease;
  ListBase crease_pointers;

  LinkData *material;
  ListBase material_pointers;

  LinkData *edge_mark;
  ListBase edge_mark_pointers;

} LANPR_RenderTaskInfo;

typedef struct LANPR_BoundingArea {
  real l, r, u, b;
  real cx, cy;

  /** 1,2,3,4 quadrant */
  struct LANPR_BoundingArea *child;

  ListBase lp;
  ListBase rp;
  ListBase up;
  ListBase bp;

  int triangle_count;
  ListBase linked_triangles;
  ListBase linked_lines;

  /** Reserved for image space reduction && multithread chainning */
  ListBase linked_chains;
} LANPR_BoundingArea;

#define TNS_COMMAND_LINE 0
#define TNS_COMMAND_MATERIAL 1
#define TNS_COMMAND_EDGE 2

#define TNS_TRANSPARENCY_DRAW_SIMPLE 0
#define TNS_TRANSPARENCY_DRAW_LAYERED 1

#define TNS_OVERRIDE_ONLY 0
#define TNS_OVERRIDE_EXCLUDE 1
/* #define TNS_OVERRIDE_ALL_OTHERS_OUTSIDE_GROUP 2 */
/* #define TNS_OVERRIDE_ALL_OTHERS_IN_GROUP      3 */
/* #define TNS_OVERRIDE_ALL_OTHERS               4 */

#define TNS_TILE(tile, r, c, CCount) tile[r * CCount + c]

#define TNS_CLAMP(a, Min, Max) a = a < Min ? Min : (a > Max ? Max : a)

#define TNS_MAX3_INDEX(a, b, c) (a > b ? (a > c ? 0 : (b > c ? 1 : 2)) : (b > c ? 1 : 2))

#define TNS_MIN3_INDEX(a, b, c) (a < b ? (a < c ? 0 : (b < c ? 1 : 2)) : (b < c ? 1 : 2))

#define TNS_MAX3_INDEX_ABC(x, y, z) (x > y ? (x > z ? a : (y > z ? b : c)) : (y > z ? b : c))

#define TNS_MIN3_INDEX_ABC(x, y, z) (x < y ? (x < z ? a : (y < z ? b : c)) : (y < z ? b : c))

#define TNS_ABC(index) (index == 0 ? a : (index == 1 ? b : c))

#define TNS_DOUBLE_CLOSE_ENOUGH(a, b) (((a) + DBL_EDGE_LIM) >= (b) && ((a)-DBL_EDGE_LIM) <= (b))

/* #define TNS_DOUBLE_CLOSE_ENOUGH(a,b)\ */
/*  //(((a)+0.00000000001)>=(b) && ((a)-0.0000000001)<=(b)) */

#define TNS_FLOAT_CLOSE_ENOUGH_WIDER(a, b) (((a) + 0.0000001) >= (b) && ((a)-0.0000001) <= (b))

#define TNS_IN_TILE_X(RenderTile, Fx) (RenderTile->FX <= Fx && RenderTile->FXLim >= Fx)

#define TNS_IN_TILE_Y(RenderTile, Fy) (RenderTile->FY <= Fy && RenderTile->FYLim >= Fy)

#define TNS_IN_TILE(RenderTile, Fx, Fy) \
  (TNS_IN_TILE_X(RenderTile, Fx) && TNS_IN_TILE_Y(RenderTile, Fy))

BLI_INLINE int lanpr_TrangleLineBoundBoxTest(LANPR_RenderTriangle *rt, LANPR_RenderLine *rl)
{
  if (MAX3(rt->v[0]->fbcoord[2], rt->v[1]->fbcoord[2], rt->v[2]->fbcoord[2]) >
      MIN2(rl->l->fbcoord[2], rl->r->fbcoord[2]))
    return 0;
  if (MAX3(rt->v[0]->fbcoord[0], rt->v[1]->fbcoord[0], rt->v[2]->fbcoord[0]) <
      MIN2(rl->l->fbcoord[0], rl->r->fbcoord[0]))
    return 0;
  if (MIN3(rt->v[0]->fbcoord[0], rt->v[1]->fbcoord[0], rt->v[2]->fbcoord[0]) >
      MAX2(rl->l->fbcoord[0], rl->r->fbcoord[0]))
    return 0;
  if (MAX3(rt->v[0]->fbcoord[1], rt->v[1]->fbcoord[1], rt->v[2]->fbcoord[1]) <
      MIN2(rl->l->fbcoord[1], rl->r->fbcoord[1]))
    return 0;
  if (MIN3(rt->v[0]->fbcoord[1], rt->v[1]->fbcoord[1], rt->v[2]->fbcoord[1]) >
      MAX2(rl->l->fbcoord[1], rl->r->fbcoord[1]))
    return 0;
  return 1;
}

BLI_INLINE double tMatGetLinearRatio(real l, real r, real FromL);
BLI_INLINE int lanpr_LineIntersectTest2d(
    const double *a1, const double *a2, const double *b1, const double *b2, double *aRatio)
{
  double k1, k2;
  double x;
  double y;
  double Ratio;
  double xDiff = (a2[0] - a1[0]); /*  +DBL_EPSILON; */
  double xDiff2 = (b2[0] - b1[0]);

  if (xDiff == 0) {
    if (xDiff2 == 0) {
      *aRatio = 0;
      return 0;
    }
    double r2 = tMatGetLinearRatio(b1[0], b2[0], a1[0]);
    x = interpd(b2[0], b1[0], r2);
    y = interpd(b2[1], b1[1], r2);
    *aRatio = Ratio = tMatGetLinearRatio(a1[1], a2[1], y);
  }
  else {
    if (xDiff2 == 0) {
      Ratio = tMatGetLinearRatio(a1[0], a2[0], b1[0]);
      x = interpd(a2[0], a1[0], Ratio);
      *aRatio = Ratio;
    }
    else {
      k1 = (a2[1] - a1[1]) / xDiff;
      k2 = (b2[1] - b1[1]) / xDiff2;

      if ((k1 == k2))
        return 0;

      x = (a1[1] - b1[1] - k1 * a1[0] + k2 * b1[0]) / (k2 - k1);

      Ratio = (x - a1[0]) / xDiff;

      *aRatio = Ratio;
    }
  }

  if (b1[0] == b2[0]) {
    y = interpd(a2[1], a1[1], Ratio);
    if (y > MAX2(b1[1], b2[1]) || y < MIN2(b1[1], b2[1]))
      return 0;
  }
  else if (Ratio <= 0 || Ratio > 1 || (b1[0] > b2[0] && x > b1[0]) ||
           (b1[0] < b2[0] && x < b1[0]) || (b2[0] > b1[0] && x > b2[0]) ||
           (b2[0] < b1[0] && x < b2[0]))
    return 0;

  return 1;
}
BLI_INLINE double lanpr_GetLineZ(tnsVector3d l, tnsVector3d r, real Ratio)
{
  double z = interpd(r[2], l[2], Ratio);
  return z;
}
BLI_INLINE double lanpr_GetLineZPoint(tnsVector3d l, tnsVector3d r, tnsVector3d FromL)
{
  double ra = (FromL[0] - l[0]) / (r[0] - l[0]);
  return interpd(r[2], l[2], ra);
}
BLI_INLINE double lanpr_GetLinearRatio(tnsVector3d l, tnsVector3d r, tnsVector3d FromL)
{
  double ra = (FromL[0] - l[0]) / (r[0] - l[0]);
  return ra;
}

BLI_INLINE double tMatGetLinearRatio(real l, real r, real FromL)
{
  double ra = (FromL - l) / (r - l);
  return ra;
}

int ED_lanpr_point_inside_triangled(tnsVector2d v, tnsVector2d v0, tnsVector2d v1, tnsVector2d v2);

void *list_append_pointer_static(ListBase *h, LANPR_StaticMemPool *smp, void *p);
void *list_append_pointer_static_sized(ListBase *h, LANPR_StaticMemPool *smp, void *p, int size);
void *list_push_pointer_static(ListBase *h, LANPR_StaticMemPool *smp, void *p);
void *list_push_pointer_static_sized(ListBase *h, LANPR_StaticMemPool *smp, void *p, int size);

void *list_append_pointer_static_pool(LANPR_StaticMemPool *mph, ListBase *h, void *p);
void *list_pop_pointer_no_free(ListBase *h);
void list_remove_pointer_item_no_free(ListBase *h, LinkData *lip);

LANPR_StaticMemPoolNode *mem_new_static_pool(LANPR_StaticMemPool *smp);
void *mem_static_aquire(LANPR_StaticMemPool *smp, int size);
void *mem_static_aquire_thread(LANPR_StaticMemPool *smp, int size);
void *mem_static_destroy(LANPR_StaticMemPool *smp);

void tmat_make_ortho_matrix_44d(
    double (*mProjection)[4], real xMin, real xMax, real yMin, real yMax, real zMin, real zMax);
void tmat_make_perspective_matrix_44d(
    double (*mProjection)[4], real fFov_rad, real fAspect, real zMin, real zMax);

int lanpr_count_this_line(LANPR_RenderLine *rl, LANPR_LineLayer *ll);
long lanpr_count_leveled_edge_segment_count(ListBase *LineList, LANPR_LineLayer *ll);
long lanpr_count_intersection_segment_count(LANPR_RenderBuffer *rb);
void *lanpr_make_leveled_edge_vertex_array(LANPR_RenderBuffer *rb,
                                           ListBase *LineList,
                                           float *vertexArray,
                                           float *NormalArray,
                                           float **NextNormal,
                                           LANPR_LineLayer *ll,
                                           float componet_id);

struct Depsgraph;
struct SceneLANPR;

int ED_lanpr_object_collection_usage_check(struct Collection *c, struct Object *o);

void ED_lanpr_NO_THREAD_chain_feature_lines(LANPR_RenderBuffer *rb);
void ED_lanpr_split_chains_for_fixed_occlusion(LANPR_RenderBuffer *rb);
void ED_lanpr_connect_chains(LANPR_RenderBuffer *rb, int do_geometry_space);
void ED_lanpr_discard_short_chains(LANPR_RenderBuffer *rb, float threshold);
int ED_lanpr_count_chain(LANPR_RenderLineChain *rlc);
void ED_lanpr_chain_clear_picked_flag(struct LANPR_RenderBuffer *rb);

int ED_lanpr_compute_feature_lines_internal(struct Depsgraph *depsgraph, int instersections_only);

void ED_lanpr_destroy_render_data(struct LANPR_RenderBuffer *rb);

LANPR_RenderBuffer *ED_lanpr_create_render_buffer(void);

bool ED_lanpr_dpix_shader_error(void);
bool ED_lanpr_disable_edge_splits(struct Scene *s);

int ED_lanpr_max_occlusion_in_line_layers(struct SceneLANPR *lanpr);
LANPR_LineLayer *ED_lanpr_new_line_layer(struct SceneLANPR *lanpr);
LANPR_LineLayerComponent *ED_lanpr_new_line_component(struct SceneLANPR *lanpr);

LANPR_BoundingArea *ED_lanpr_get_point_bounding_area(LANPR_RenderBuffer *rb, real x, real y);
LANPR_BoundingArea *ED_lanpr_get_point_bounding_area_deep(LANPR_RenderBuffer *rb, real x, real y);

void ED_lanpr_post_frame_update_external(struct Scene *s, struct Depsgraph *dg);

struct SceneLANPR;

void ED_lanpr_rebuild_all_command(struct SceneLANPR *lanpr);

void ED_lanpr_update_render_progress(const char *text);

void ED_lanpr_calculate_normal_object_vector(LANPR_LineLayer *ll, float *normal_object_direction);

float ED_lanpr_compute_chain_length(LANPR_RenderLineChain *rlc);

struct wmOperatorType;

/* Operator types */
void SCENE_OT_lanpr_calculate_feature_lines(struct wmOperatorType *ot);
void SCENE_OT_lanpr_add_line_layer(struct wmOperatorType *ot);
void SCENE_OT_lanpr_delete_line_layer(struct wmOperatorType *ot);
void SCENE_OT_lanpr_rebuild_all_commands(struct wmOperatorType *ot);
void SCENE_OT_lanpr_auto_create_line_layer(struct wmOperatorType *ot);
void SCENE_OT_lanpr_move_line_layer(struct wmOperatorType *ot);
void SCENE_OT_lanpr_add_line_component(struct wmOperatorType *ot);
void SCENE_OT_lanpr_delete_line_component(struct wmOperatorType *ot);
void SCENE_OT_lanpr_enable_all_line_types(struct wmOperatorType *ot);
void SCENE_OT_lanpr_update_gp_strokes(struct wmOperatorType *ot);
void SCENE_OT_lanpr_bake_gp_strokes(struct wmOperatorType *ot);

void OBJECT_OT_lanpr_update_gp_target(struct wmOperatorType *ot);
void OBJECT_OT_lanpr_update_gp_source(struct wmOperatorType *ot);

void ED_operatortypes_lanpr(void);

#endif /* __ED_LANPR_H__ */
