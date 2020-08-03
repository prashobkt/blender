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

#ifndef __ED_LRT_H__
#define __ED_LRT_H__

#ifndef WITH_LINEART
#  error Lineart code included in non-Lineart-enabled build
#endif

#include "BLI_linklist.h"
#include "BLI_listbase.h"
#include "BLI_math.h"
#include "BLI_threads.h"

#include "DNA_lineart_types.h"
#include "DNA_windowmanager_types.h"

#include <math.h>
#include <string.h>

typedef struct LineartStaticMemPoolNode {
  Link item;
  size_t size;
  size_t used_byte;
  /* User memory starts here */
} LineartStaticMemPoolNode;

typedef struct LineartStaticMemPool {
  ListBase pools;
  SpinLock lock_mem;
} LineartStaticMemPool;

typedef struct LineartRenderTriangle {
  struct LineartRenderTriangle *next, *prev;
  struct LineartRenderVert *v[3];
  struct LineartRenderLine *rl[3];
  double gn[3];
  /*  struct BMFace *F; */
  short material_id;
  ListBase intersecting_verts;
  char cull_status;
} LineartRenderTriangle;

typedef struct LineartRenderTriangleThread {
  struct LineartRenderTriangle base;
  /** This variable is used to store per-thread triangle-line testing pair,
   *  also re-used to store triangle-triangle pair for intersection testing stage.
   *  Do not directly use LineartRenderTriangleThread, but use it as a pointer,
   * the size of LineartRenderTriangle is dynamically allocated to contain set thread number of
   * "testing" field, at least one thread is present, thus we always have at least testing[0].*/
  struct LineartRenderLine *testing[127];
} LineartRenderTriangleThread;

typedef struct LineartRenderElementLinkNode {
  struct LineartRenderElementLinkNode *next, *prev;
  void *pointer;
  int element_count;
  void *object_ref;
  char additional;
} LineartRenderElementLinkNode;

typedef struct LineartRenderLineSegment {
  struct LineartRenderLineSegment *next, *prev;
  /** at==0: left  at==1: right  (this is in 2D projected space) */
  double at;
  /** Occlusion level after "at" point */
  unsigned char occlusion;

  /** For determining lines beind a glass window material. (TODO: implement this)
   *  the size of this variable should also be dynamically decided, 1 byte to 8 byte,
   *  allows 8 to 64 materials for "transparent mask". 1 byte (8 materials) should be
   *  enought for most cases.
   */
  /* short material_mask_mark; */
} LineartRenderLineSegment;

typedef struct LineartRenderVert {
  struct LineartRenderVert *next, *prev;
  double gloc[3];
  double fbcoord[4];
  /**  Used as "r" when intersecting */
  struct BMVert *v;
  struct LineartRenderLine *intersecting_line;
  struct LineartRenderLine *intersecting_line2;
  struct LineartRenderTriangle *intersecting_with;

  /** This will used in future acceleration for intersection processing. */
  char edge_used;
} LineartRenderVert;

typedef struct LineartRenderLine {
  struct LineartRenderLine *next, *prev;
  struct LineartRenderVert *l, *r;
  struct LineartRenderTriangle *tl, *tr;
  ListBase segments;
  char min_occ;

  /**  Also for line type determination on chainning */
  char flags;

  /**  Still need this entry because culled lines will not add to object reln node */
  struct Object *object_ref;
} LineartRenderLine;

typedef struct LineartRenderLineChain {
  struct LineartRenderLineChain *next, *prev;
  ListBase chain;

  /**  Calculated before draw cmd. */
  float length;

  /**  Used when re-connecting and gp stroke generation */
  char picked;
  char level;

  /** Chain now only contains one type of segments */
  int type;
  struct Object *object_ref;
} LineartRenderLineChain;

typedef struct LineartRenderLineChainItem {
  struct LineartRenderLineChainItem *next, *prev;
  /** Need z value for fading */
  float pos[3];
  /** For restoring position to 3d space */
  float gpos[3];
  float normal[3];
  char line_type;
  char occlusion;
} LineartRenderLineChainItem;

typedef struct LineartChainRegisterEntry {
  struct LineartChainRegisterEntry *next, *prev;
  LineartRenderLineChain *rlc;
  LineartRenderLineChainItem *rlci;
  char picked;

  /** left/right mark.
   * Because we revert list in chaining so we need the flag. */
  char is_left;
} LineartChainRegisterEntry;

typedef struct LineartRenderBuffer {
  struct LineartRenderBuffer *prev, *next;

  /** For render. */
  int is_copied;

  int w, h;
  int tile_size_w, tile_size_h;
  int tile_count_x, tile_count_y;
  double width_per_tile, height_per_tile;
  double view_projection[4][4];

  int output_mode;
  int output_aa_level;

  struct LineartBoundingArea *initial_bounding_areas;
  unsigned int bounding_area_count;

  ListBase vertex_buffer_pointers;
  ListBase line_buffer_pointers;
  ListBase triangle_buffer_pointers;
  ListBase all_render_lines;

  ListBase intersecting_vertex_buffer;
  /** Use the one comes with Line Art. */
  LineartStaticMemPool render_data_pool;

  struct Material *material_pointers[2048];

  /*  Render status */
  double view_vector[3];

  int triangle_size;

  unsigned int contour_count;
  unsigned int contour_processed;
  LinkData *contour_managed;
  ListBase contours;

  unsigned int intersection_count;
  unsigned int intersection_processed;
  LinkData *intersection_managed;
  ListBase intersection_lines;

  unsigned int crease_count;
  unsigned int crease_processed;
  LinkData *crease_managed;
  ListBase crease_lines;

  unsigned int material_line_count;
  unsigned int material_processed;
  LinkData *material_managed;
  ListBase material_lines;

  unsigned int edge_mark_count;
  unsigned int edge_mark_processed;
  LinkData *edge_mark_managed;
  ListBase edge_marks;

  ListBase chains;

  /** For managing calculation tasks for multiple threads. */
  SpinLock lock_task;

  /*  settings */

  int max_occlusion_level;
  double crease_angle;
  double crease_cos;
  int thread_count;

  int draw_material_preview;
  double material_transparency;

  char use_contour;
  char use_crease;
  char use_material;
  char use_edge_marks;
  char use_intersections;
  char fuzzy_intersections;
  char fuzzy_everything;

  /** Keep an copy of these data so the scene can be freed when lineart is runnning. */
  char cam_is_persp;
  float cam_obmat[4][4];
  double camera_pos[3];
  double near_clip, far_clip;
  float shift_x, shift_y;
  float chaining_image_threshold;
  float chaining_geometry_threshold;
  float angle_splitting_threshold;
} LineartRenderBuffer;

typedef enum eLineartRenderStatus {
  LRT_RENDER_IDLE = 0,
  LRT_RENDER_RUNNING = 1,
  LRT_RENDER_INCOMPELTE = 2,
  LRT_RENDER_FINISHED = 3,
} eLineartRenderStatus;

typedef enum eLineartInitStatus {
  LRT_INIT_ENGINE = (1 << 0),
  LRT_INIT_LOCKS = (1 << 1),
} eLineartInitStatus;

typedef enum eLineartModifierSyncStatus {
  LRT_SYNC_IDLE = 0,
  LRT_SYNC_WAITING = 1,
  LRT_SYNC_FRESH = 2,
  LRT_SYNC_IGNORE = 3,
} eLineartModifierSyncStatus;

typedef struct LineartSharedResource {

  /* We only allocate once for all */
  LineartRenderBuffer *render_buffer_shared;

  /* cache */
  struct BLI_mempool *mp_sample;
  struct BLI_mempool *mp_line_strip;
  struct BLI_mempool *mp_line_strip_point;
  struct BLI_mempool *mp_batch_list;

  struct TaskPool *background_render_task;

  eLineartInitStatus init_complete;

  /** To bypass or cancel rendering.
   * This status flag should be kept in lineart_share not render_buffer,
   * because render_buffer will get re-initialized every frame.
   */
  SpinLock lock_render_status;
  eLineartRenderStatus flag_render_status;
  eLineartModifierSyncStatus flag_sync_staus;

  /** Geometry loading is done in the worker thread,
   * Lock the render thread until loading is done, so that
   * we can avoid depsgrapgh deleting the scene before
   * LRT finishes loading. Also keep this in lineart_share.
   */
  SpinLock lock_loader;

  /** When drawing in the viewport, use the following values. */
  /** Set to override to -1 before creating lineart render buffer to use scene camera. */
  int viewport_camera_override;
  char camera_is_persp;
  float camera_pos[3];
  float near_clip, far_clip;
  float viewinv[4][4];
  float persp[4][4];
  float viewquat[4];

  /* Use these to set cursor and progress. */
  wmWindowManager *wm;
  wmWindow *main_window;
} LineartSharedResource;

#define DBL_TRIANGLE_LIM 1e-8
#define DBL_EDGE_LIM 1e-9

#define LRT_MEMORY_POOL_1MB 1048576
#define LRT_MEMORY_POOL_64MB 67108864

typedef enum eLineartCullState {
  LRT_CULL_DONT_CARE = 0,
  LRT_CULL_USED = 1,
  LRT_CULL_DISCARD = 2,
  LRT_CULL_GENERATED = 3,
} eLineartCullState;

/** Controls how many lines a worker thread is processing at one request.
 * There's no significant performance impact on choosing different values.
 * Don't make it too small so that the worker thread won't request too many times. */
#define LRT_THREAD_LINE_COUNT 1000

typedef struct LineartRenderTaskInfo {
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

} LineartRenderTaskInfo;

/** Bounding area diagram:
 *
 * +----+ <----U (Upper edge Y value)
 * |    |
 * +----+ <----B (Bottom edge Y value)
 * ^    ^
 * L    R (Left/Right edge X value)
 *
 * Example structure when subdividing 1 bounding areas:
 * 1 area can be divided into 4 smaller children to
 * accomodate image areas with denser triangle distribution.
 * +--+--+-----+
 * +--+--+     |
 * +--+--+-----+
 * |     |     |
 * +-----+-----+
 * lp/rp/up/bp is the list for
 * storing pointers to adjacent bounding areas.
 */
typedef struct LineartBoundingArea {
  double l, r, u, b;
  double cx, cy;

  /** 1,2,3,4 quadrant */
  struct LineartBoundingArea *child;

  ListBase lp;
  ListBase rp;
  ListBase up;
  ListBase bp;

  int triangle_count;
  ListBase linked_triangles;
  ListBase linked_lines;

  /** Reserved for image space reduction && multithread chainning */
  ListBase linked_chains;
} LineartBoundingArea;

#define LRT_TILE(tile, r, c, CCount) tile[r * CCount + c]

#define LRT_CLAMP(a, Min, Max) a = a < Min ? Min : (a > Max ? Max : a)

#define LRT_MAX3_INDEX(a, b, c) (a > b ? (a > c ? 0 : (b > c ? 1 : 2)) : (b > c ? 1 : 2))

#define LRT_MIN3_INDEX(a, b, c) (a < b ? (a < c ? 0 : (b < c ? 1 : 2)) : (b < c ? 1 : 2))

#define LRT_MAX3_INDEX_ABC(x, y, z) (x > y ? (x > z ? a : (y > z ? b : c)) : (y > z ? b : c))

#define LRT_MIN3_INDEX_ABC(x, y, z) (x < y ? (x < z ? a : (y < z ? b : c)) : (y < z ? b : c))

#define LRT_ABC(index) (index == 0 ? a : (index == 1 ? b : c))

#define LRT_DOUBLE_CLOSE_ENOUGH(a, b) (((a) + DBL_EDGE_LIM) >= (b) && ((a)-DBL_EDGE_LIM) <= (b))

BLI_INLINE double lineart_get_linear_ratio(double l, double r, double from_l);
BLI_INLINE int lineart_LineIntersectTest2d(
    const double *a1, const double *a2, const double *b1, const double *b2, double *aRatio)
{
  double k1, k2;
  double x;
  double y;
  double ratio;
  double x_diff = (a2[0] - a1[0]);
  double x_diff2 = (b2[0] - b1[0]);

  if (x_diff == 0) {
    if (x_diff2 == 0) {
      *aRatio = 0;
      return 0;
    }
    double r2 = lineart_get_linear_ratio(b1[0], b2[0], a1[0]);
    x = interpd(b2[0], b1[0], r2);
    y = interpd(b2[1], b1[1], r2);
    *aRatio = ratio = lineart_get_linear_ratio(a1[1], a2[1], y);
  }
  else {
    if (x_diff2 == 0) {
      ratio = lineart_get_linear_ratio(a1[0], a2[0], b1[0]);
      x = interpd(a2[0], a1[0], ratio);
      *aRatio = ratio;
    }
    else {
      k1 = (a2[1] - a1[1]) / x_diff;
      k2 = (b2[1] - b1[1]) / x_diff2;

      if ((k1 == k2))
        return 0;

      x = (a1[1] - b1[1] - k1 * a1[0] + k2 * b1[0]) / (k2 - k1);

      ratio = (x - a1[0]) / x_diff;

      *aRatio = ratio;
    }
  }

  if (b1[0] == b2[0]) {
    y = interpd(a2[1], a1[1], ratio);
    if (y > MAX2(b1[1], b2[1]) || y < MIN2(b1[1], b2[1]))
      return 0;
  }
  else if (ratio <= 0 || ratio > 1 || (b1[0] > b2[0] && x > b1[0]) ||
           (b1[0] < b2[0] && x < b1[0]) || (b2[0] > b1[0] && x > b2[0]) ||
           (b2[0] < b1[0] && x < b2[0]))
    return 0;

  return 1;
}

BLI_INLINE double lineart_get_linear_ratio(double l, double r, double from_l)
{
  double ra = (from_l - l) / (r - l);
  return ra;
}

int ED_lineart_point_inside_triangled(double v[2], double v0[2], double v1[2], double v2[2]);

struct Depsgraph;
struct SceneLineart;
struct Scene;
struct LineartRenderBuffer;

void ED_lineart_init_locks(void);
struct LineartRenderBuffer *ED_lineart_create_render_buffer(struct Scene *s);
void ED_lineart_destroy_render_data(void);
void ED_lineart_destroy_render_data_external(void);

int ED_lineart_object_collection_usage_check(struct Collection *c, struct Object *o);

void ED_lineart_chain_feature_lines(LineartRenderBuffer *rb);
void ED_lineart_chain_split_for_fixed_occlusion(LineartRenderBuffer *rb);
void ED_lineart_chain_connect(LineartRenderBuffer *rb, const int do_geometry_space);
void ED_lineart_chain_discard_short(LineartRenderBuffer *rb, const float threshold);
void ED_lineart_chain_split_angle(LineartRenderBuffer *rb, float angle_threshold_rad);

int ED_lineart_chain_count(const LineartRenderLineChain *rlc);
void ED_lineart_chain_clear_picked_flag(struct LineartRenderBuffer *rb);

void ED_lineart_calculation_flag_set(eLineartRenderStatus flag);
bool ED_lineart_calculation_flag_check(eLineartRenderStatus flag);

void ED_lineart_modifier_sync_flag_set(eLineartModifierSyncStatus flag, bool is_from_modifier);
bool ED_lineart_modifier_sync_flag_check(eLineartModifierSyncStatus flag);

int ED_lineart_compute_feature_lines_internal(struct Depsgraph *depsgraph,
                                              const int show_frame_progress);

void ED_lineart_compute_feature_lines_background(struct Depsgraph *dg,
                                                 const int show_frame_progress);

struct Scene;

LineartBoundingArea *ED_lineart_get_point_bounding_area(LineartRenderBuffer *rb,
                                                        double x,
                                                        double y);
LineartBoundingArea *ED_lineart_get_point_bounding_area_deep(LineartRenderBuffer *rb,
                                                             double x,
                                                             double y);

struct bGPDlayer;
struct bGPDframe;
struct GpencilModifierData;

void ED_lineart_gpencil_generate_from_chain(struct Depsgraph *depsgraph,
                                            struct Object *ob,
                                            struct bGPDlayer *UNUSED(gpl),
                                            struct bGPDframe *gpf,
                                            int level_start,
                                            int level_end,
                                            int material_nr,
                                            struct Collection *col,
                                            int types,
                                            short thickness,
                                            float opacity,
                                            float pre_sample_length);
void ED_lineart_gpencil_generate_strokes_direct(struct Depsgraph *depsgraph,
                                                struct Object *ob,
                                                struct bGPDlayer *gpl,
                                                struct bGPDframe *gpf,
                                                char source_type,
                                                void *source_reference,
                                                int level_start,
                                                int level_end,
                                                int mat_nr,
                                                short line_types,
                                                short thickness,
                                                float opacity,
                                                float pre_sample_length);

struct bContext;

void ED_lineart_post_frame_update_external(struct bContext *C,
                                           struct Scene *s,
                                           struct Depsgraph *dg);

struct SceneLineart;

void ED_lineart_update_render_progress(int nr, const char *info);

float ED_lineart_chain_compute_length(LineartRenderLineChain *rlc);

struct wmOperatorType;

/* Operator types */
void SCENE_OT_lineart_update_strokes(struct wmOperatorType *ot);
void SCENE_OT_lineart_bake_strokes(struct wmOperatorType *ot);

void ED_operatortypes_lineart(void);

#endif /* __ED_LRT_H__ */
