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
 */

/** \file
 * \ingroup bmesh
 *
 * Cut meshes along intersections and boolean operations on the intersections.
 *
 * Supported:
 * - Concave faces.
 * - Non-planar faces.
 * - Coplanar intersections
 * - Custom-data (UV's etc).
 *
 */

#include "MEM_guardedalloc.h"

#include "BLI_alloca.h"
#include "BLI_bitmap.h"
#include "BLI_delaunay_2d.h"
#include "BLI_linklist.h"
#include "BLI_math.h"
#include "BLI_memarena.h"
#include "BLI_utildefines.h"

#include "bmesh.h"
#include "intern/bmesh_private.h"

#include "bmesh_boolean.h" /* own include */

#include "BLI_strict_flags.h"

#define BOOLDEBUG

/* NOTE: Work in progress. Initial implementation using slow data structures and algorithms
 * just to get the correct calculations down. After that, will replace data structures with
 * faster-lookup versions (hash tables, kd-trees, bvh structures) and will parallelize.
 */

/* A set of integers. TODO: faster structure. */
typedef struct IntSet {
  LinkNode *list;
} IntSet;

typedef struct IntSetIterator {
  LinkNode *cur;
} IntSetIterator;

/* A set of integers, where each member gets an index
 * that can be used to access the member.
 * TODO: faster structure for lookup.
 */
typedef struct IndexedIntSet {
  LinkNodePair listhead; /* links are ints */
  int size;
} IndexedIntSet;

/* A map from int -> int.
 * TODO: faster structure for lookup.
 */
typedef struct IntIntMap {
  LinkNodePair listhead; /* Links are pointers to IntPair, allocated from arena. */
} IntIntMap;

typedef struct IntPair {
  int first;
  int second;
} IntPair;

typedef struct IntIntMapIterator {
  const IntIntMap *map;
  LinkNode *curlink;
  IntPair *keyvalue;
} IntIntMapIterator;

/* A Mesh Interface.
 * This would be an abstract interface in C++,
 * but similate that effect in C.
 * Idea is to write the rest of the code so that
 * it will work with either Mesh or BMesh as the
 * concrete representation.
 * Thus, editmesh and modifier can use the same
 * code but without need to convert to BMesh (or Mesh).
 *
 * Exactly one of bm and me should be non-null.
 */
typedef struct IMesh {
  BMesh *bm;
  struct Mesh *me;
} IMesh;

/* MeshAdd holds an incremental addition to an IMesh.
 * New verts, edges, and faces are given indices starting
 * beyond those of the underlying IMesh, and that new
 * geometry is stored here. For edges and faces, the
 * indices used can be either from the IMesh or from the
 * new geometry stored here.
 * Sometimes the new geometric elements are based on
 * an example element in the underlying IMesh (the example
 * will be used to copy attributes).
 * So we store examples here too.
 */
typedef struct MeshAdd {
  LinkNodePair verts; /* Links are NewVerts. */
  LinkNodePair edges; /* Links are NewEdges. */
  LinkNodePair faces; /* Links are NewFaces. */
  int vindex_start;   /* Add this to position in verts to get index of new vert. */
  int eindex_start;   /* Add this to position in edges to get index of new edge. */
  int findex_start;   /* Add this to position in faces to get index of new face. */
  IMesh *im;          /* Underlying IMesh. */
} MeshAdd;

typedef struct NewVert {
  float co[3];
  int example; /* If not -1, example vert in IMesh. */
} NewVert;

typedef struct NewEdge {
  int v1;
  int v2;
  int example; /* If not -1, example edge in IMesh. */
} NewEdge;

typedef struct NewFace {
  IntPair *vert_edge_pairs; /* Array of len (vert, edge) pairs. */
  int len;
  int example; /* If not -1, example face in IMesh. */
  IntSet *other_examples; /* rest of faces in IMesh that are originals for this face */
} NewFace;

/* MeshDelete holds an incremental deletion to an IMesh.
 * It records the indices of the edges and faces that need
 * to be deleted. (As of now, we don't have to delete verts
 * except those that recorded separately as merges.)
 */
typedef struct MeshDelete {
  BLI_bitmap *vert_bmap;
  BLI_bitmap *edge_bmap;
  BLI_bitmap *face_bmap;
  int totvert;
  int totedge;
  int totface;
} MeshDelete;

/* MeshChange holds all of the information needed to transform
 * an IMesh into the desired result: vertex merges, adds, deletes,
 * and which edges are to be tagged to mark intersection edges.
 */
typedef struct MeshChange {
  MeshAdd add;
  MeshDelete delete;
  IntIntMap vert_merge_map;
  IntSet intersection_edges;
  IntSet face_flip;
  bool use_face_kill_loose;
} MeshChange;

/* A MeshPart is a subset of the geometry of an IndexMesh,
 * with some possible additional geometry.
 * The indices refer to vertex, edges, and faces in the IndexMesh
 * that this part is based on,
 * or, if the indices are larger than the total in the IndexMesh,
 * then it is in extra geometry incrementally added.
 * Unlike for IndexMesh, the edges implied by faces need not be explicitly
 * represented here.
 * Commonly a MeshPart will contain geometry that shares a plane,
 * and when that is so, the plane member says which plane,
 * TODO: faster structure for looking up verts, edges, faces.
 */
typedef struct MeshPart {
  double plane[4]; /* First 3 are normal, 4th is signed distance to plane. */
  double bbmin[3]; /* Bounding box min, with eps padding. */
  double bbmax[3]; /* Bounding box max, with eps padding. */
  LinkNode *verts; /* Links are ints (vert indices). */
  LinkNode *edges; /* Links are ints (edge indices). */
  LinkNode *faces; /* Links are ints (face indices). */
} MeshPart;

/* A MeshPartSet set is a set of MeshParts.
 * For any two distinct elements of the set, either they are not
 * coplanar or if they are, they are known not to intersect.
 * TODO: faster structure for looking up by plane.
 */
typedef struct MeshPartSet {
  double bbmin[3];
  double bbmax[3];
  LinkNode *meshparts; /* links are MeshParts* */
  int tot_part;
  const char *label; /* for debugging */
} MeshPartSet;

/* An IMeshPlus is an IMesh plus a MeshAdd.
 * If the element indices are in range for the IMesh, then functions
 * access those, else they access the MeshAdd.
 */
typedef struct IMeshPlus {
  IMesh *im;
  MeshAdd *meshadd;
} IMeshPlus;

/* Result of intersecting two MeshParts.
 * This only need identify the thngs that probably intersect,
 * as the actual intersections will be done later, when
 * parts are self-intersected. Dedup will handle any problems.
 * It is not necessary to include verts that are part of included
 * edges, nor edges that are part of included faces.
 */
typedef struct PartPartIntersect {
  LinkNodePair verts; /* Links are vert indices. */
  LinkNodePair edges; /* Links are edge indices. */
  LinkNodePair faces; /* Links are face indices. */
  int a_index;
  int b_index;
} PartPartIntersect;

typedef struct BoolState {
  MemArena *mem_arena;
  IMesh im;
  int boolean_mode;
  double eps;
  int (*test_fn)(void *elem, void *user_data);
  void *test_fn_user_data;
} BoolState;

/* test_fn results used to distinguish parts of mesh. */
enum { TEST_NONE = -1, TEST_B = 0, TEST_A = 1, TEST_ALL = 2 };

/* Decoration to shut up gnu 'unused function' warning. */
#ifdef __GNUC__
#  define ATTU __attribute__((unused))
#else
#  define ATTU
#endif

#ifdef BOOLDEBUG
/* For Debugging. */
#  define CO3(v) (v)->co[0], (v)->co[1], (v)->co[2]
#  define F2(v) (v)[0], (v)[1]
#  define F3(v) (v)[0], (v)[1], (v)[2]
#  define F4(v) (v)[0], (v)[1], (v)[2], (v)[3]
#  define BMI(e) BM_elem_index_get(e)

ATTU static void dump_part(const MeshPart *part, const char *label);
ATTU static void dump_partset(const MeshPartSet *pset);
ATTU static void dump_partpartintersect(const PartPartIntersect *ppi, const char *label);
ATTU static void dump_meshadd(const MeshAdd *ma, const char *label);
ATTU static void dump_meshdelete(const MeshDelete *meshdelete, const char *label);
ATTU static void dump_intintmap(const IntIntMap *map, const char *label, const char *prefix);
ATTU static void dump_intset(const IntSet *set, const char *label, const char *prefix);
ATTU static void dump_meshchange(const MeshChange *change, const char *label);
ATTU static void dump_cdt_input(const CDT_input *cdt, const char *label);
ATTU static void dump_cdt_result(const CDT_result *cdt, const char *label, const char *prefix);
ATTU static void dump_bm(struct BMesh *bm, const char *msg);
#endif

/* Forward declarations of some static functions. */
static int min_int_in_array(int *array, int len);
static LinkNode *linklist_shallow_copy_arena(LinkNode *list, struct MemArena *arena);
static void calc_part_bb_eps(BoolState *bs, MeshPart *part, double eps);
static bool find_in_intintmap(const IntIntMap *map, int key, int *r_val);
static int meshadd_totvert(const MeshAdd *meshadd);
static int meshadd_totedge(const MeshAdd *meshadd);
static int meshadd_totface(const MeshAdd *meshadd);
static NewVert *meshadd_get_newvert(const MeshAdd *meshadd, int v);
static NewEdge *meshadd_get_newedge(const MeshAdd *meshadd, int e);
static NewFace *meshadd_get_newface(const MeshAdd *meshadd, int f);
static int meshadd_facelen(const MeshAdd *meshadd, int f);
static void meshadd_get_face_no(const MeshAdd *meshadd, int f, double *r_no);
static int meshadd_face_vert(const MeshAdd *meshadd, int f, int index);
static void meshadd_get_vert_co(const MeshAdd *meshadd, int v, float *r_coords);
static void meshadd_get_edge_verts(const MeshAdd *meshadd, int e, int *r_v1, int *r_v2);
static bool meshdelete_find_vert(MeshDelete *meshdelete, int v);
static bool meshdelete_find_edge(MeshDelete *meshdelete, int e);
static bool meshdelete_find_face(MeshDelete *meshdelete, int f);
static int find_edge_by_verts_in_meshadd(const MeshAdd *meshadd, int v1, int v2);

/** Intset functions */

static void init_intset(IntSet *inset)
{
  inset->list = NULL;
}

static bool find_in_intset(const IntSet *set, int value)
{
  LinkNode *ln;

  for (ln = set->list; ln; ln = ln->next) {
    if (POINTER_AS_INT(ln->link) == value) {
      return true;
    }
  }
  return false;
}

static void add_to_intset(BoolState *bs, IntSet *set, int value)
{
  if (!find_in_intset(set, value)) {
    BLI_linklist_prepend_arena(&set->list, POINTER_FROM_INT(value), bs->mem_arena);
  }
}

/** IntSetIterator functions. */

static void intset_iter_init(IntSetIterator *iter, const IntSet *set)
{
  iter->cur = set->list;
}

static bool intset_iter_done(IntSetIterator *iter)
{
  return iter->cur == NULL;
}

static void intset_iter_step(IntSetIterator *iter)
{
  iter->cur = iter->cur->next;
}

static int intset_iter_value(IntSetIterator *iter)
{
  return POINTER_AS_INT(iter->cur->link);
}

/** IndexedIntSet functions. */

static void init_indexedintset(IndexedIntSet *intset)
{
  intset->listhead.list = NULL;
  intset->listhead.last_node = NULL;
  intset->size = 0;
}

static int add_int_to_indexedintset(BoolState *bs, IndexedIntSet *intset, int value)
{
  int index;

  index = BLI_linklist_index(intset->listhead.list, POINTER_FROM_INT(value));
  if (index == -1) {
    BLI_linklist_append_arena(&intset->listhead, POINTER_FROM_INT(value), bs->mem_arena);
    index = intset->size;
    intset->size++;
  }
  return index;
}

static bool find_in_indexedintset(const IndexedIntSet *intset, int value)
{
  LinkNode *ln;

  for (ln = intset->listhead.list; ln; ln = ln->next) {
    if (POINTER_AS_INT(ln->link) == value) {
      return true;
    }
  }
  return false;
}

static int indexedintset_get_value_by_index(const IndexedIntSet *intset, int index)
{
  LinkNode *ln;
  int i;

  if (index < 0 || index >= intset->size) {
    return -1;
  }
  ln = intset->listhead.list;
  for (i = 0; i < index; i++) {
    BLI_assert(ln != NULL);
    ln = ln->next;
  }
  BLI_assert(ln != NULL);
  return POINTER_AS_INT(ln->link);
}

static int indexedintset_get_index_for_value(const IndexedIntSet *intset, int value)
{
  return BLI_linklist_index(intset->listhead.list, POINTER_FROM_INT(value));
}

/** IntIntMap functions. */

static void init_intintmap(IntIntMap *intintmap)
{
  intintmap->listhead.list = NULL;
  intintmap->listhead.last_node = NULL;
}

ATTU static int intintmap_size(const IntIntMap *intintmap)
{
  return BLI_linklist_count(intintmap->listhead.list);
}

static void add_to_intintmap(BoolState *bs, IntIntMap *map, int key, int val)
{
  IntPair *keyvalpair;

  keyvalpair = BLI_memarena_alloc(bs->mem_arena, sizeof(*keyvalpair));
  keyvalpair->first = key;
  keyvalpair->second = val;
  BLI_linklist_append_arena(&map->listhead, keyvalpair, bs->mem_arena);
}

static bool find_in_intintmap(const IntIntMap *map, int key, int *r_val)
{
  LinkNode *ln;

  for (ln = map->listhead.list; ln; ln = ln->next) {
    IntPair *pair = (IntPair *)ln->link;
    if (pair->first == key) {
      *r_val = pair->second;
      return true;
    }
  }
  return false;
}

/* Note: this is a shallow copy: afterwards, dst and src will
 * share underlying list
 */
ATTU static void copy_intintmap_intintmap(IntIntMap *dst, IntIntMap *src)
{
  dst->listhead.list = src->listhead.list;
  dst->listhead.last_node = src->listhead.last_node;
}

ATTU static void set_intintmap_entry(BoolState *bs, IntIntMap *map, int key, int value)
{
  LinkNode *ln;

  for (ln = map->listhead.list; ln; ln = ln->next) {
    IntPair *pair = (IntPair *)ln->link;
    if (pair->first == key) {
      pair->second = value;
      return;
    }
  }
  add_to_intintmap(bs, map, key, value);
}

/* Abstract the IntIntMap iteration to allow for changes to
 * implementation later.
 * We allow the value of a map to be changed during
 * iteration, but not the key.
 */

ATTU static void intintmap_iter_init(IntIntMapIterator *iter, const IntIntMap *map)
{
  iter->map = map;
  iter->curlink = map->listhead.list;
  if (iter->curlink) {
    iter->keyvalue = (IntPair *)iter->curlink->link;
  }
  else {
    iter->keyvalue = NULL;
  }
}

static inline bool intintmap_iter_done(IntIntMapIterator *iter)
{
  return iter->curlink == NULL;
}

ATTU static void intintmap_iter_step(IntIntMapIterator *iter)
{
  iter->curlink = iter->curlink->next;
  if (iter->curlink) {
    iter->keyvalue = (IntPair *)iter->curlink->link;
  }
  else {
    iter->keyvalue = NULL;
  }
}

static inline int intintmap_iter_key(IntIntMapIterator *iter)
{
  return iter->keyvalue->first;
}

ATTU static inline int *intintmap_iter_valuep(IntIntMapIterator *iter)
{
  return &iter->keyvalue->second;
}

static inline int intintmap_iter_value(IntIntMapIterator *iter)
{
  return iter->keyvalue->second;
}

/** Miscellaneous utility functions. */
#pragma mark Miscellaneous utility functions

static int min_int_in_array(int *array, int len)
{
  int min = INT_MAX;
  int i;

  for (i = 0; i < len; i++) {
    min = min_ii(min, array[i]);
  }
  return min;
}

/* Shallow copy. Result will share link pointers with original. */
static LinkNode *linklist_shallow_copy_arena(LinkNode *list, struct MemArena *arena)
{
  LinkNodePair listhead = {NULL, NULL};
  LinkNode *ln;

  for (ln = list; ln; ln = ln->next) {
    BLI_linklist_append_arena(&listhead, ln->link, arena);
  }

  return listhead.list;
}

/* Get to last node of a liskned list. Also return list size in r_count. */
ATTU static LinkNode *linklist_last(LinkNode *ln, int *r_count)
{
  int i;

  if (ln) {
    i = 1;
    for (; ln->next; ln = ln->next) {
      i++;
    }
    *r_count = i;
    return ln;
  }
  *r_count = 0;
  return NULL;
}

/** Functions to move to Blenlib's math_geom when stable. */

#if 0
/* This general tri-tri intersect routine may be useful as an optimization later, but is unused for now. */

/*
 * Return -1, 0, or 1 as (px,py) is right of, collinear (within epsilon) with, or left of line
 * (l1,l2)
 */
static int line_point_side_v2_array_db(
    double l1x, double l1y, double l2x, double l2y, double px, double py, double epsilon)
{
  double det, dx, dy, lnorm;

  dx = l2x - l1x;
  dy = l2y - l1y;
  det = dx * (py - l1y) - dy * (px - l1x);
  lnorm = sqrt(dx * dx + dy * dy);
  if (fabs(det) < epsilon * lnorm)
    return 0;
  else if (det < 0)
    return -1;
  else
    return 1;
}

/**
 * Intersect two triangles, allowing for coplanar intersections too.
 *
 * The result can be any of: a single point, a segment, or an n-gon (n <= 6),
 * so can indicate all of these with a return array of coords max size 6 and a returned
 * length: if length is 1, then intersection is a point; if 2, then a segment;
 * if 3 or more, a (closed) ngon.
 * Use double arithmetic, and use consistent ordering of vertices and
 * segments in tests to make some precision problems less likely.
 * Algorithm: Tomas Möller's "A Fast Triangle-Triangle Intersection Test"
 *
 * \param r_pts, r_npts: Optional arguments to retrieve the intersection points between the 2
 * triangles. \return true when the triangles intersect.
 *
 */
static bool isect_tri_tri_epsilon_v3_db_ex(const double t_a0[3],
                                           const double t_a1[3],
                                           const double t_a2[3],
                                           const double t_b0[3],
                                           const double t_b1[3],
                                           const double t_b2[3],
                                           double r_pts[6][3],
                                           int *r_npts,
                                           const double epsilon)
{
  const double *tri_pair[2][3] = {{t_a0, t_a1, t_a2}, {t_b0, t_b1, t_b2}};
  double co[2][3][3];
  double plane_a[4], plane_b[4];
  double plane_co[3], plane_no[3];
  int verti;

  BLI_assert((r_pts != NULL) == (r_npts != NULL));

  /* This is remnant from when input args were floats. TODO: remove this copying. */
  for (verti = 0; verti < 3; verti++) {
    copy_v3_v3_db(co[0][verti], tri_pair[0][verti]);
    copy_v3_v3_db(co[1][verti], tri_pair[1][verti]);
  }
  /* normalizing is needed for small triangles T46007 */
  normal_tri_v3_db(plane_a, UNPACK3(co[0]));
  normal_tri_v3_db(plane_b, UNPACK3(co[1]));

  plane_a[3] = -dot_v3v3_db(plane_a, co[0][0]);
  plane_b[3] = -dot_v3v3_db(plane_b, co[1][0]);

  if (isect_plane_plane_v3_db(plane_a, plane_b, plane_co, plane_no) &&
      (normalize_v3_d(plane_no) > epsilon)) {
    struct {
      double min, max;
    } range[2] = {{DBL_MAX, -DBL_MAX}, {DBL_MAX, -DBL_MAX}};
    int t;
    double co_proj[3];

    closest_to_plane3_normalized_v3_db(co_proj, plane_no, plane_co);

    /* For both triangles, find the overlap with the line defined by the ray [co_proj, plane_no].
     * When the ranges overlap we know the triangles do too. */
    for (t = 0; t < 2; t++) {
      int j, j_prev;
      double tri_proj[3][3];

      closest_to_plane3_normalized_v3_db(tri_proj[0], plane_no, co[t][0]);
      closest_to_plane3_normalized_v3_db(tri_proj[1], plane_no, co[t][1]);
      closest_to_plane3_normalized_v3_db(tri_proj[2], plane_no, co[t][2]);

      for (j = 0, j_prev = 2; j < 3; j_prev = j++) {
        /* note that its important to have a very small nonzero epsilon here
         * otherwise this fails for very small faces.
         * However if its too small, large adjacent faces will count as intersecting */
        const double edge_fac = line_point_factor_v3_ex_db(
            co_proj, tri_proj[j_prev], tri_proj[j], 1e-10, -1.0);
        if (UNLIKELY(edge_fac == -1.0)) {
          /* pass */
        }
        else if (edge_fac > -epsilon && edge_fac < 1.0 + epsilon) {
          double ix_tri[3];
          double span_fac;

          interp_v3_v3v3_db(ix_tri, co[t][j_prev], co[t][j], edge_fac);
          /* the actual distance, since 'plane_no' is normalized */
          span_fac = dot_v3v3_db(plane_no, ix_tri);

          range[t].min = min_dd(range[t].min, span_fac);
          range[t].max = max_dd(range[t].max, span_fac);
        }
      }

      if (range[t].min == DBL_MAX) {
        return false;
      }
    }

    if (((range[0].min > range[1].max + epsilon) || (range[0].max < range[1].min - epsilon)) ==
        0) {
      if (r_pts && r_npts) {
        double pt1[3], pt2[3];
        project_plane_normalized_v3_v3v3_db(plane_co, plane_co, plane_no);
        madd_v3_v3v3db_db(pt1, plane_co, plane_no, max_dd(range[0].min, range[1].min));
        madd_v3_v3v3db_db(pt2, plane_co, plane_no, min_dd(range[0].max, range[1].max));
        copy_v3_v3_db(r_pts[0], pt1);
        copy_v3_v3_db(r_pts[1], pt2);
        if (len_v3v3_db(pt1, pt2) <= epsilon)
          *r_npts = 1;
        else
          *r_npts = 2;
      }

      return true;
    }
  }
  else if (fabs(plane_a[3] - plane_b[3]) <= epsilon) {
    double pts[9][3];
    int ia, ip, ia_n, ia_nn, ip_prev, npts, same_side[6], ss, ss_prev, j;

    for (ip = 0; ip < 3; ip++)
      copy_v3_v3_db(pts[ip], co[1][ip]);
    npts = 3;

    /* a convex polygon vs convex polygon clipping algorithm */
    for (ia = 0; ia < 3; ia++) {
      ia_n = (ia + 1) % 3;
      ia_nn = (ia_n + 1) % 3;

      /* set same_side[i] = 0 if A[ia], A[ia_n], pts[ip] are collinear.
       * else same_side[i] =1 if A[ia_nn] and pts[ip] are on same side of A[ia], A[ia_n].
       * else same_side[i] = -1
       */
      for (ip = 0; ip < npts; ip++) {
        double t;
        const double *l1 = co[0][ia];
        const double *l2 = co[0][ia_n];
        const double *p1 = co[0][ia_nn];
        const double *p2 = pts[ip];

        /* rather than projecting onto plane, do same-side tests projecting onto 3 ortho planes */
        t = line_point_side_v2_array_db(l1[0], l1[1], l2[0], l2[1], p1[0], p1[1], epsilon);
        if (t != 0.0) {
          same_side[ip] = t * line_point_side_v2_array_db(
                                  l1[0], l1[1], l2[0], l2[1], p2[0], p2[1], epsilon);
        }
        else {
          t = line_point_side_v2_array_db(l1[1], l1[2], l2[1], l2[2], p1[1], p1[2], epsilon);
          if (t != 0.0) {
            same_side[ip] = t * line_point_side_v2_array_db(
                                    l1[1], l1[2], l2[1], l2[2], p2[1], p2[2], epsilon);
          }
          else {
            t = line_point_side_v2_array_db(l1[0], l1[2], l2[0], l2[2], p1[0], p1[2], epsilon);
            same_side[ip] = t * line_point_side_v2_array_db(
                                    l1[0], l1[2], l2[0], l2[2], p2[0], p2[2], epsilon);
          }
        }
      }
      ip_prev = npts - 1;
      for (ip = 0; ip < (npts > 2 ? npts : npts - 1); ip++) {
        ss = same_side[ip];
        ss_prev = same_side[ip_prev];
        if ((ss_prev == 1 && ss == -1) || (ss_prev == -1 && ss == 1)) {
          /* do coplanar line-line intersect, specialized verison of isect_line_line_epsilon_v3_db
           */
          double *v1 = co[0][ia], *v2 = co[0][ia_n], *v3 = pts[ip_prev], *v4 = pts[ip];
          double a[3], b[3], c[3], ab[3], cb[3], isect[3], div;

          sub_v3_v3v3_db(c, v3, v1);
          sub_v3_v3v3_db(a, v2, v1);
          sub_v3_v3v3_db(b, v4, v3);

          cross_v3_v3v3_db(ab, a, b);
          div = dot_v3v3_db(ab, ab);
          if (div == 0.0) {
            /* shouldn't happen! */
            printf("div == 0, shouldn't happen\n");
            continue;
          }
          cross_v3_v3v3_db(cb, c, b);
          mul_v3db_db(a, dot_v3v3_db(cb, ab) / div);
          add_v3_v3v3_db(isect, v1, a);

          /* insert isect at current location */
          BLI_assert(npts < 9);
          for (j = npts; j > ip; j--) {
            copy_v3_v3_db(pts[j], pts[j - 1]);
            same_side[j] = same_side[j - 1];
          }
          copy_v3_v3_db(pts[ip], isect);
          same_side[ip] = 0;
          npts++;
        }
        ip_prev = ip;
      }
      /* cut out some pts that are no longer in intersection set */
      for (ip = 0; ip < npts;) {
        if (same_side[ip] == -1) {
          /* remove point at ip */
          for (j = ip; j < npts - 1; j++) {
            copy_v3_v3_db(pts[j], pts[j + 1]);
            same_side[j] = same_side[j + 1];
          }
          npts--;
        }
        else {
          ip++;
        }
      }
    }

    *r_npts = npts;
    /* This copy is remant of old signature that returned floats. TODO: remove this copy. */
    for (ip = 0; ip < npts; ip++) {
      copy_v3_v3_db(r_pts[ip], pts[ip]);
    }
    return npts > 0;
  }
  if (r_npts)
    *r_npts = 0;
  return false;
}
#endif

/* Does the line intersect the segment (within epsilon)?
 * If line and seg are parallel and coincident, return 2.
 * If line intersects the segment within epsilon, return 1.
 * Otherwise return 0.
 * If the return value is 1, put the intersection point in isect,
 * and the fraction of the way from line_v1 to line_v2 to isect in *r_lambda
 * (may be outside of the range [0,1]) and the fraction of the way from seg_v1
 * to seg_v2 in *r_mu.
 */
static int isect_line_seg_epsilon_v3_db(const double line_v1[3],
                                        const double line_v2[3],
                                        const double seg_v1[3],
                                        const double seg_v2[3],
                                        double epsilon,
                                        double isect[3],
                                        double *r_lambda,
                                        double *r_mu)
{
  double fac_a, fac_b;
  double a_dir[3], b_dir[3], a0b0[3], crs_ab[3];
  double epsilon_squared = epsilon * epsilon;
  sub_v3_v3v3_db(a_dir, line_v2, line_v1);
  sub_v3_v3v3_db(b_dir, seg_v2, seg_v1);
  cross_v3_v3v3_db(crs_ab, b_dir, a_dir);
  const double nlen = len_squared_v3_db(crs_ab);

  if (nlen < epsilon_squared) {
    /* Parallel Lines or near parallel. Are the coincident? */
    double d1, d2;
    d1 = dist_squared_to_line_v3_db(seg_v1, line_v1, line_v2);
    d2 = dist_squared_to_line_v3_db(seg_v2, line_v1, line_v2);
    if (d1 <= epsilon_squared || d2 <= epsilon_squared) {
      return 2;
    }
    else {
      return 0;
    }
  }
  else {
    double c[3], cray[3];
    double blen = len_v3_db(b_dir);
    sub_v3_v3v3_db(a0b0, seg_v1, line_v1);
    sub_v3_v3v3_db(c, crs_ab, a0b0);

    cross_v3_v3v3_db(cray, c, b_dir);
    fac_a = dot_v3v3_db(cray, crs_ab) / nlen;

    cross_v3_v3v3_db(cray, c, a_dir);
    fac_b = dot_v3v3_db(cray, crs_ab) / nlen;

    if (fac_b * blen < -epsilon || fac_b * blen - blen > epsilon) {
      return 0;
    }
    else {
      madd_v3_v3v3db_db(isect, seg_v1, b_dir, fac_b);
      *r_lambda = fac_a;
      *r_mu = fac_b;
      return 1;
    }
  }
}

/** IMesh functions. */
#pragma mark IMesh functions

static void init_imesh_from_bmesh(IMesh *im, BMesh *bm)
{
  im->bm = bm;
  im->me = NULL;
  BM_mesh_elem_table_ensure(bm, BM_VERT | BM_EDGE | BM_FACE);
}

static int imesh_totvert(const IMesh *im)
{
  if (im->bm) {
    return im->bm->totvert;
  }
  else {
    return 0; /* TODO */
  }
}

static int imesh_totedge(const IMesh *im)
{
  if (im->bm) {
    return im->bm->totedge;
  }
  else {
    return 0; /* TODO */
  }
}

static int imesh_totface(const IMesh *im)
{
  if (im->bm) {
    return im->bm->totface;
  }
  else {
    return 0; /* TODO */
  }
}

static int imesh_facelen(const IMesh *im, int f)
{
  int ans = 0;

  if (im->bm) {
    BMFace *bmf = BM_face_at_index(im->bm, f);
    if (bmf) {
      ans = bmf->len;
    }
  }
  else {
    ; /* TODO */
  }
  return ans;
}

static void imesh_get_face_no(const IMesh *im, int f, double *r_no)
{
  if (im->bm) {
    BMFace *bmf = BM_face_at_index(im->bm, f);
    copy_v3db_v3fl(r_no, bmf->no);
  }
  else {
    ; /* TODO */
  }
}

static int imesh_face_vert(const IMesh *im, int f, int index)
{
  int i;
  int ans = -1;

  if (im->bm) {
    BMFace *bmf = BM_face_at_index(im->bm, f);
    if (bmf) {
      BMLoop *l = bmf->l_first;
      for (i = 0; i < index; i++) {
        l = l->next;
      }
      BMVert *bmv = l->v;
      ans = BM_elem_index_get(bmv);
    }
  }
  else {
    ; /* TODO */
  }
  return ans;
}

static void imesh_get_vert_co(const IMesh *im, int v, float *r_coords)
{
  if (im->bm) {
    BMVert *bmv = BM_vert_at_index(im->bm, v);
    if (bmv) {
      copy_v3_v3(r_coords, bmv->co);
      return;
    }
    else {
      zero_v3(r_coords);
    }
  }
  else {
    ; /* TODO */
  }
}

static void imesh_get_vert_co_db(const IMesh *im, int v, double *r_coords)
{
  if (im->bm) {
    BMVert *bmv = BM_vert_at_index(im->bm, v);
    if (bmv) {
      copy_v3db_v3fl(r_coords, bmv->co);
      return;
    }
    else {
      zero_v3_db(r_coords);
    }
  }
  else {
    ; /* TODO */
  }
}

/* Find a vertex in im eps-close to co, if it exists.
 * Else return -1.
 * TODO: speed this up.
 */
static int imesh_find_co_db(const IMesh *im, const double co[3], double eps)
{
  int v;
  BMesh *bm = im->bm;
  float feps = (float)eps;
  float fco[3];

  copy_v3fl_v3db(fco, co);
  if (bm) {
    for (v = 0; v < bm->totvert; v++) {
      BMVert *bmv = BM_vert_at_index(bm, v);
      if (compare_v3v3(fco, bmv->co, feps)) {
        return v;
      }
    }
    return -1;
  }
  else {
    return -1; /* TODO */
  }
}

/* Find an edge in im between given two verts (either order ok), if it exists.
 * Else return -1.
 * TODO: speed this up.
 */
static int imesh_find_edge(const IMesh *im, int v1, int v2)
{
  BMesh *bm = im->bm;
  int e;

  if (bm) {
    if (v1 >= bm->totvert || v2 >= bm->totvert) {
      return -1;
    }
    for (e = 0; e < bm->totedge; e++) {
      BMEdge *bme = BM_edge_at_index(bm, e);
      int ev1 = BM_elem_index_get(bme->v1);
      int ev2 = BM_elem_index_get(bme->v2);
      if ((ev1 == v1 && ev2 == v2) || (ev1 == v2 && ev2 == v1)) {
        return e;
      }
    }
    return -1;
  }
  else {
    return -1; /* TODO */
  }
}

static void imesh_get_edge_cos_db(const IMesh *im, int e, double *r_coords1, double *r_coords2)
{
  if (im->bm) {
    BMEdge *bme = BM_edge_at_index(im->bm, e);
    if (bme) {
      copy_v3db_v3fl(r_coords1, bme->v1->co);
      copy_v3db_v3fl(r_coords2, bme->v2->co);
    }
    else {
      zero_v3_db(r_coords1);
      zero_v3_db(r_coords2);
    }
  }
  else {
    ; /* TODO */
  }
}

static void imesh_get_edge_verts(const IMesh *im, int e, int *r_v1, int *r_v2)
{
  if (im->bm) {
    BMEdge *bme = BM_edge_at_index(im->bm, e);
    if (bme) {
      *r_v1 = BM_elem_index_get(bme->v1);
      *r_v2 = BM_elem_index_get(bme->v2);
    }
    else {
      *r_v1 = -1;
      *r_v2 = -1;
    }
  }
  else {
    ; /* TODO */
  }
}

static void imesh_get_face_plane(const IMesh *im, int f, double r_plane[4])
{
  double plane_co[3];

  zero_v4_db(r_plane);
  if (im->bm) {
    BMFace *bmf = BM_face_at_index(im->bm, f);
    if (bmf) {
      /* plane_from_point_normal_v3 with mixed arithmetic */
      copy_v3db_v3fl(r_plane, bmf->no);
      copy_v3db_v3fl(plane_co, bmf->l_first->v->co);
      r_plane[3] = -dot_v3v3_db(r_plane, plane_co);
    }
  }
}

static void imesh_calc_point_in_face(IMesh *im, int f, double co[3])
{
  if (im->bm) {
    float fco[3];
    BMFace *bmf = BM_face_at_index(im->bm, f);
    BM_face_calc_point_in_face(bmf, fco);
    copy_v3db_v3fl(co, fco);
  }
  else {
    ; /* TODO */
  }
}

/* Return a tesselation of f into triangles.
 * There will always be flen - 2 triangles where f is f's face length.
 * Caller must supply array of size (flen - 2) * 3 ints.
 * Return will be triples of indices of the vertices around f.
 */
static void imesh_face_calc_tesselation(IMesh *im, int f, int (*r_index)[3])
{
  if (im->bm) {
    BMFace *bmf = BM_face_at_index(im->bm, f);
    BMLoop **loops = BLI_array_alloca(loops, (size_t)bmf->len);
    /* OK to use argument use_fixed_quad == true: don't need convex quads. */
    BM_face_calc_tessellation(bmf, true, loops, (uint(*)[3])r_index);
    /* Need orientation of triangles to match that of face. Because of using
     * use_fix_quads == true, we know that we only might have a problem here
     * for polygons with more than 4 sides. */
    if (bmf->len > 4) {
      float tri0_no[3];
      BMVert *v0, *v1, *v2;
      v0 = loops[r_index[0][0]]->v;
      v1 = loops[r_index[0][1]]->v;
      v2 = loops[r_index[0][2]]->v;
      normal_tri_v3(tri0_no, v0->co, v1->co, v2->co);
      if (dot_v3v3(tri0_no, bmf->no) < 0.0f) {
        /* Need to reverse winding order for all triangles in tesselation. */
        int i, tmp;
        for (i = 0; i < bmf->len - 2; i++) {
          tmp = r_index[i][1];
          r_index[i][1] = r_index[i][2];
          r_index[i][2] = tmp;
        }
      }
    }
  }
  else {
    ; /* TODO */
  }
}

static int imesh_test_face(const IMesh *im, int (*test_fn)(void *, void *), void *user_data, int f)
{
  if (im->bm) {
    BMFace *bmf = BM_face_at_index(im->bm, f);
    if (bmf) {
      return test_fn(bmf, user_data);
    }
    return 0;
  }
  else {
    /* TODO */
    return 0;
  }
}

static int resolve_merge(int v, const IntIntMap *vert_merge_map)
{
  int vmapped = v;
  int target;

  while (find_in_intintmap(vert_merge_map, vmapped, &target)) {
    vmapped = target;
  }
  return vmapped;
}

static void apply_meshchange_to_bmesh(BMesh *bm, MeshChange *change)
{
  int bm_tot_v, bm_tot_e, bm_tot_f, tot_new_v, tot_new_e, tot_new_f;
  int i, v, e, f, v1, v2;
  int facelen, max_facelen;
  NewVert *newvert;
  NewEdge *newedge;
  NewFace *newface;
  BMVert **new_bmvs, **face_bmvs;
  BMVert *bmv, *bmv1, *bmv2;
  BMEdge **new_bmes, **face_bmes;
  BMEdge *bme, *bme_eg;
  BMFace *bmf, *bmf_eg;
  MeshAdd *meshadd = &change->add;
  MeshDelete *meshdelete = &change->delete;
  IntIntMap *vert_merge_map = &change->vert_merge_map;
  IntSet *intersection_edges = &change->intersection_edges;
  IntSetIterator is_iter;
#ifdef BOOLDEBUG
  int dbg_level = 0;
#endif

#ifdef BOOLDEBUG
  if (dbg_level > 0) {
    printf("\n\nAPPLY_MESHADD_TO_BMESH\n\n");
  }
#endif

  /* Create new BMVerts. */
  bm_tot_v = bm->totvert;
  tot_new_v = meshadd_totvert(meshadd);
  if (tot_new_v > 0) {
    new_bmvs = BLI_array_alloca(new_bmvs, (size_t)tot_new_v);
    for (v = meshadd->vindex_start; v < meshadd->vindex_start + tot_new_v; v++) {
      newvert = meshadd_get_newvert(meshadd, v);
      BLI_assert(newvert != NULL);
      bmv = BM_vert_create(bm, newvert->co, NULL, 0);
#ifdef BOOLDEBUG
      if (dbg_level > 0) {
        printf("created new BMVert for new vert %d at (%f,%f,%f)\n", v, F3(newvert->co));
        printf("  -> bmv=%p\n", bmv);
        /* BM_mesh_validate(bm); */
      }
#endif
      new_bmvs[v - meshadd->vindex_start] = bmv;
    }
  }

  /* Adding verts has made the vertex table dirty.
   * It is probably still ok, but just in case...
   * TODO: find a way to avoid regenerating this table, maybe.
   */
  BM_mesh_elem_table_ensure(bm, BM_VERT);

  /* Now the edges. */
  bm_tot_e = bm->totedge;
  tot_new_e = meshadd_totedge(meshadd);
  if (tot_new_e > 0) {
    new_bmes = BLI_array_alloca(new_bmes, (size_t)tot_new_e);
    for (e = meshadd->eindex_start; e < meshadd->eindex_start + tot_new_e; e++) {
      newedge = meshadd_get_newedge(meshadd, e);
      BLI_assert(newedge != NULL);
      if (newedge->example != -1) {
        /* Not really supposed to access bm->etable directly but even
         * though it may be technically dirty, all of the example indices
         * will still be OK. */
        bme_eg = bm->etable[newedge->example];
      }
      else {
        bme_eg = NULL;
      }
      v1 = newedge->v1;
      v2 = newedge->v2;
      if (v1 < bm_tot_v) {
        v1 = resolve_merge(v1, vert_merge_map);
        bmv1 = BM_vert_at_index(bm, v1);
      }
      else {
        bmv1 = new_bmvs[v1 - meshadd->vindex_start];
      }
      BLI_assert(bmv1 != NULL);
      if (v2 < bm_tot_v) {
        v2 = resolve_merge(v2, vert_merge_map);
        bmv2 = BM_vert_at_index(bm, v2);
      }
      else {
        bmv2 = new_bmvs[v2 - meshadd->vindex_start];
      }
      BLI_assert(bmv2 != NULL);
      bme = BM_edge_create(bm, bmv1, bmv2, bme_eg, BM_CREATE_NO_DOUBLE);
      if (bme_eg) {
        BM_elem_select_copy(bm, bme, bme_eg);
      }
#ifdef BOOLDEBUG
      if (dbg_level > 0) {
        printf("created BMEdge for new edge %d, v1=%d, v2=%d, bmv1=%p, bmv2=%p\n",
               e,
               v1,
               v2,
               bmv1,
               bmv2);
        printf("  -> bme=%p\n", bme);
        /* BM_mesh_validate(bm); */
      }
#endif
      new_bmes[e - meshadd->eindex_start] = bme;
    }
  }

  /* Now the faces. */
  bm_tot_f = bm->totface;
  tot_new_f = meshadd_totface(meshadd);
  if (tot_new_f > 0) {
    /* Find max face length so can allocate buffers just once. */
    max_facelen = 0;
    for (f = meshadd->findex_start; f < meshadd->findex_start + tot_new_f; f++) {
      newface = meshadd_get_newface(meshadd, f);
      BLI_assert(newface != NULL);
      max_facelen = max_ii(max_facelen, newface->len);
    }
    face_bmvs = BLI_array_alloca(face_bmvs, (size_t)max_facelen);
    face_bmes = BLI_array_alloca(face_bmes, (size_t)max_facelen);
    for (f = meshadd->findex_start; f < meshadd->findex_start + tot_new_f; f++) {
      newface = meshadd_get_newface(meshadd, f);
      BLI_assert(newface != NULL);
      if (newface->example != -1) {
        bmf_eg = bm->ftable[newface->example];
      }
      else {
        bmf_eg = NULL;
      }
      facelen = newface->len;
      for (i = 0; i < facelen; i++) {
        v = newface->vert_edge_pairs[i].first;
        if (v < bm_tot_v) {
          v = resolve_merge(v, vert_merge_map);
          bmv = BM_vert_at_index(bm, v);
        }
        else {
          bmv = new_bmvs[v - meshadd->vindex_start];
        }
        BLI_assert(bmv != NULL);
        face_bmvs[i] = bmv;
        e = newface->vert_edge_pairs[i].second;
        if (e < bm_tot_e) {
          bme = bm->etable[e];
        }
        else {
          bme = new_bmes[e - meshadd->eindex_start];
        }
        BLI_assert(bme != NULL);
        face_bmes[i] = bme;
      }
      bmf = BM_face_create(bm, face_bmvs, face_bmes, facelen, bmf_eg, 0);
      if (bmf_eg) {
        BM_elem_select_copy(bm, bmf, bmf_eg);
      }
      if (find_in_intset(&change->face_flip, f)) {
        BM_face_normal_flip(bm, bmf);
      }
#ifdef BOOLDEBUG
      if (dbg_level > 0) {
        printf("created BMFace for new face %d\n", f);
        printf("  -> bmf = %p\n", bmf);
        /* BM_mesh_validate(bm); */
      }
#endif
    }
  }

  /* Some original faces need their normals flipped. */
  intset_iter_init(&is_iter, &change->face_flip);
  for (; !intset_iter_done(&is_iter); intset_iter_step(&is_iter)) {
    f = intset_iter_value(&is_iter);
    if (f < bm_tot_f) {
      bmf = bm->ftable[f];
      BM_face_normal_flip(bm, bmf);
    }
  }

  /* Need to tag the intersection edges. */
  intset_iter_init(&is_iter, intersection_edges);
  for (; !intset_iter_done(&is_iter); intset_iter_step(&is_iter)) {
    e = intset_iter_value(&is_iter);
    if (e < bm_tot_e) {
      bme = bm->etable[e];
    }
    else {
      bme = new_bmes[e - meshadd->eindex_start];
    }
    BM_elem_flag_enable(bme, BM_ELEM_TAG);
  }

  /* Delete the geometry we are supposed to delete now. */
  for (f = 0; f < bm_tot_f; f++) {
    if (meshdelete_find_face(meshdelete, f)) {
      bmf = bm->ftable[f];
      if (change->use_face_kill_loose) {
        BM_face_kill_loose(bm, bmf);
      }
      else {
        BM_face_kill(bm, bmf);
      }
#ifdef BOOLDEBUG
      if (dbg_level > 0) {
        printf("killed bmf=%p for ftable[%d]\n", bmf, f);
        /* BM_mesh_validate(bm); */
      }
#endif
    }
  }
  for (e = 0; e < bm_tot_e; e++) {
    if (meshdelete_find_edge(meshdelete, e)) {
      bme = bm->etable[e];
      BM_edge_kill(bm, bme);
#ifdef BOOLDEBUG
      if (dbg_level > 0) {
        printf("killed bme=%p for etable[%d]\n", bme, e);
        /* BM_mesh_validate(bm); */
      }
#endif
    }
  }
  for (v = 0; v < bm_tot_v; v++) {
    if (meshdelete_find_vert(meshdelete, v)) {
      bmv = bm->vtable[v];
      BM_vert_kill(bm, bmv);
#ifdef BOOLDEBUG
      if (dbg_level > 0) {
        printf("killed bmv=%p for vtable[%d]\n", bmv, v);
        /* BM_mesh_validate(bm); */
      }
#endif
    }
  }
  BM_mesh_elem_index_ensure(bm, BM_VERT | BM_EDGE | BM_FACE);
  BM_mesh_elem_table_ensure(bm, BM_VERT | BM_EDGE | BM_FACE);
}

static void apply_meshchange_to_imesh(IMesh *im, MeshChange *change)
{
  if (im->bm) {
    apply_meshchange_to_bmesh(im->bm, change);
  }
  else {
    /* TODO */
  }
}

static void bb_update(double bbmin[3], double bbmax[3], int v, const IMesh *im)
{
  int i;
  float vco[3];
  double vcod[3];

  imesh_get_vert_co(im, v, vco);
  copy_v3db_v3fl(vcod, vco);
  for (i = 0; i < 3; i++) {
    bbmin[i] = min_dd(vcod[i], bbmin[i]);
    bbmax[i] = max_dd(vcod[i], bbmax[i]);
  }
}

/* Used as user_data in call to BM_mesh_calc_face_groups. */
struct BoolFilterWrap {
  int (*test_fn)(BMFace *f, void *user_data);
  void *user_data;
};

/* Function used for imesh_calc_face_groups to return true
 * when we should cross this loop l to new faces to accumulate
 * faces in the same group.
 * This allows such traversal if the edge isn't tagged (it
 * should be tagged if it was an intersection edge) and if
 * there is no other loop in the loop radial that has a face
 * on the opposite 'side' of the boolean operation.
 * (This assumes that test_fn returns 0 for one side, 1 for the
 * other side, and -1 for things not part of either side.)
 */
static bool boolfilterfn(const BMLoop *l, void *user_data)
{
  if (BM_elem_flag_test(l->e, BM_ELEM_TAG)) {
    return false;
  }

  if (l->radial_next != l) {
    struct BoolFilterWrap *data = user_data;
    BMLoop *l_iter = l->radial_next;
    const int face_side = data->test_fn(l->f, data->user_data);
    do {
      const int face_side_other = data->test_fn(l_iter->f, data->user_data);
      if (UNLIKELY(face_side_other == -1)) {
        /* pass */
      }
      else if (face_side_other != face_side) {
        return false;
      }
    } while ((l_iter = l_iter->radial_next) != l);
    return true;
  }
  return false;
}

/* Calculate groups of faces.
 * In this context, a 'group' is a set of maximal set of faces in the same
 * boolean 'side', where side is determined by bs->test_fn.
 * Maximal in the sense that the faces are connected across edges that
 * are only attached to faces in the same side.
 *
 * The r_groups_array ahould be an array of length = # of faces in the IMesh.
 * It will be filled with face indices, partitioned into groups.
 * The r_group_index pointer will be set to point at an array of pairs of ints
 * (which must be MEM_freeN'd when done), of length # of groups = returned value.
 * Each pair has a start index in r_groups_array and a length, specifying
 * the part of r_groups_array that has the face indices for the group.
 */
static int imesh_calc_face_groups(BoolState *bs, int *r_groups_array, int (**r_group_index)[2])
{
  int ngroups;
  IMesh *im = &bs->im;

  if (im->bm) {
    struct BoolFilterWrap user_data_wrap = {
        .test_fn = (int (*)(BMFace *, void *))bs->test_fn,
        .user_data = bs->test_fn_user_data,
    };

    BM_mesh_elem_table_ensure(im->bm, BM_FACE);
    ngroups = BM_mesh_calc_face_groups(
        im->bm, r_groups_array, r_group_index, boolfilterfn, &user_data_wrap, 0, BM_EDGE);
  }
  else {
    /* TODO */
    ngroups = 0;
  }
  return ngroups;
}

/** MeshAdd functions. */
#pragma mark MeshAdd functions

static void init_meshadd(BoolState *bs, MeshAdd *meshadd)
{
  IMesh *im = &bs->im;

  memset(meshadd, 0, sizeof(MeshAdd));
  meshadd->im = im;
  meshadd->vindex_start = imesh_totvert(im);
  meshadd->eindex_start = imesh_totedge(im);
  meshadd->findex_start = imesh_totface(im);
}

static int meshadd_totvert(const MeshAdd *meshadd)
{
  return BLI_linklist_count(meshadd->verts.list);
}

static int meshadd_totedge(const MeshAdd *meshadd)
{
  return BLI_linklist_count(meshadd->edges.list);
}

static int meshadd_totface(const MeshAdd *meshadd)
{
  return BLI_linklist_count(meshadd->faces.list);
}

static int meshadd_add_vert(
    BoolState *bs, MeshAdd *meshadd, const float co[3], int example, bool checkdup)
{
  NewVert *newv;
  MemArena *arena = bs->mem_arena;
  LinkNode *ln;
  int i;

  if (checkdup) {
    for (ln = meshadd->verts.list, i = meshadd->vindex_start; ln; ln = ln->next, i++) {
      newv = (NewVert *)ln->link;
      if (compare_v3v3(newv->co, co, (float)bs->eps)) {
        return i;
      }
    }
  }
  newv = BLI_memarena_alloc(arena, sizeof(*newv));
  copy_v3_v3(newv->co, co);
  newv->example = example;
  BLI_linklist_append_arena(&meshadd->verts, newv, arena);
  return meshadd->vindex_start + BLI_linklist_count(meshadd->verts.list) - 1;
}

static int meshadd_add_vert_db(
    BoolState *bs, MeshAdd *meshadd, const double co[3], int example, bool checkdup)
{
  float fco[3];
  copy_v3fl_v3db(fco, co);
  return meshadd_add_vert(bs, meshadd, fco, example, checkdup);
}

static int meshadd_add_edge(
    BoolState *bs, MeshAdd *meshadd, int v1, int v2, int example, bool checkdup)
{
  NewEdge *newe;
  MemArena *arena = bs->mem_arena;
  LinkNode *ln;
  int i;

  if (checkdup) {
    for (ln = meshadd->edges.list, i = meshadd->eindex_start; ln; ln = ln->next, i++) {
      newe = (NewEdge *)ln->link;
      if ((newe->v1 == v1 && newe->v2 == v2) || (newe->v1 == v2 && newe->v2 == v1)) {
        return i;
      }
    }
  }
  newe = BLI_memarena_alloc(arena, sizeof(*newe));
  newe->v1 = v1;
  newe->v2 = v2;
  newe->example = example;
  BLI_linklist_append_arena(&meshadd->edges, newe, arena);
  return meshadd->eindex_start + BLI_linklist_count(meshadd->edges.list) - 1;
}

/* This assumes that vert_edge is an arena-allocated array that will persist. */
static int meshadd_add_face(
    BoolState *bs, MeshAdd *meshadd, IntPair *vert_edge, int len, int example, IntSet *other_examples)
{
  NewFace *newf;
  MemArena *arena = bs->mem_arena;

  newf = BLI_memarena_alloc(arena, sizeof(*newf));
  newf->vert_edge_pairs = vert_edge;
  newf->len = len;
  newf->example = example;
  newf->other_examples = other_examples;
  BLI_linklist_append_arena(&meshadd->faces, newf, arena);
  return meshadd->findex_start + BLI_linklist_count(meshadd->faces.list) - 1;
}

static int meshadd_facelen(const MeshAdd *meshadd, int f)
{
  LinkNode *ln;
  NewFace *nf;

  ln = BLI_linklist_find(meshadd->faces.list, f - meshadd->findex_start);
  if (ln) {
    nf = (NewFace *)ln->link;
    return nf->len;
  }
  return 0;
}

static void meshadd_get_face_no(const MeshAdd *meshadd, int f, double *r_no)
{
  LinkNode *ln;
  NewFace *nf;

  ln = BLI_linklist_find(meshadd->faces.list, f - meshadd->findex_start);
  if (ln) {
    nf = (NewFace *)ln->link;
    if (nf->example) {
      imesh_get_face_no(meshadd->im, nf->example, r_no);
    }
    else {
      printf("unexpected meshadd_get_face_no on face without example\n");
      BLI_assert(false);
    }
  }
}

static int meshadd_face_vert(const MeshAdd *meshadd, int f, int index)
{
  LinkNode *ln;
  NewFace *nf;

  ln = BLI_linklist_find(meshadd->faces.list, f - meshadd->findex_start);
  if (ln) {
    nf = (NewFace *)ln->link;
    if (index >= 0 && index < nf->len) {
      return nf->vert_edge_pairs[index].first;
    }
  }
  return -1;
}

static NewVert *meshadd_get_newvert(const MeshAdd *meshadd, int v)
{
  LinkNode *ln;

  ln = BLI_linklist_find(meshadd->verts.list, v - meshadd->vindex_start);
  if (ln) {
    return (NewVert *)ln->link;
  }
  else {
    return NULL;
  }
}

static NewEdge *meshadd_get_newedge(const MeshAdd *meshadd, int e)
{
  LinkNode *ln;

  ln = BLI_linklist_find(meshadd->edges.list, e - meshadd->eindex_start);
  if (ln) {
    return (NewEdge *)ln->link;
  }
  else {
    return NULL;
  }
}

static NewFace *meshadd_get_newface(const MeshAdd *meshadd, int f)
{
  LinkNode *ln;

  ln = BLI_linklist_find(meshadd->faces.list, f - meshadd->findex_start);
  if (ln) {
    return (NewFace *)ln->link;
  }
  else {
    return NULL;
  }
}

static void meshadd_get_vert_co(const MeshAdd *meshadd, int v, float *r_coords)
{
  NewVert *nv;

  nv = meshadd_get_newvert(meshadd, v);
  if (nv) {
    copy_v3_v3(r_coords, nv->co);
  }
  else {
    zero_v3(r_coords);
  }
}

static void meshadd_get_vert_co_db(const MeshAdd *meshadd, int v, double *r_coords)
{
  float fco[3];

  meshadd_get_vert_co(meshadd, v, fco);
  copy_v3db_v3fl(r_coords, fco);
}

static void meshadd_get_edge_verts(const MeshAdd *meshadd, int e, int *r_v1, int *r_v2)
{
  NewEdge *ne;

  ne = meshadd_get_newedge(meshadd, e);
  if (ne) {
    *r_v1 = ne->v1;
    *r_v2 = ne->v2;
  }
  else {
    *r_v1 = -1;
    *r_v2 = -1;
  }
}

static int find_edge_by_verts_in_meshadd(const MeshAdd *meshadd, int v1, int v2)
{
  LinkNode *ln;
  NewEdge *ne;
  int i;

  for (ln = meshadd->edges.list, i = 0; ln; ln = ln->next, i++) {
    ne = (NewEdge *)ln->link;
    if ((ne->v1 == v1 && ne->v2 == v2) || (ne->v1 == v2 && ne->v2 == v1)) {
      return meshadd->eindex_start + i;
    }
  }
  return -1;
}

/** MeshDelete functions. */
#pragma mark MeshDelete functions

static void init_meshdelete(BoolState *bs, MeshDelete *meshdelete)
{
  IMesh *im = &bs->im;
  MemArena *arena = bs->mem_arena;

  meshdelete->totvert = imesh_totvert(im);
  meshdelete->totedge = imesh_totedge(im);
  meshdelete->totface = imesh_totface(im);
  /* The BLI_BITMAP_NEW... functions clear the bitmaps too. */
  meshdelete->vert_bmap = BLI_BITMAP_NEW_MEMARENA(arena, meshdelete->totvert);
  meshdelete->edge_bmap = BLI_BITMAP_NEW_MEMARENA(arena, meshdelete->totedge);
  meshdelete->face_bmap = BLI_BITMAP_NEW_MEMARENA(arena, meshdelete->totface);
}

ATTU static void meshdelete_add_vert(MeshDelete *meshdelete, int v)
{
  BLI_assert(0 <= v && v < meshdelete->totvert);
  BLI_BITMAP_ENABLE(meshdelete->vert_bmap, v);
}

static void meshdelete_add_edge(MeshDelete *meshdelete, int e)
{
  BLI_assert(0 <= e && e < meshdelete->totedge);
  BLI_BITMAP_ENABLE(meshdelete->edge_bmap, e);
}

static void meshdelete_add_face(MeshDelete *meshdelete, int f)
{
  BLI_assert(0 <= f && f < meshdelete->totface);
  BLI_BITMAP_ENABLE(meshdelete->face_bmap, f);
}

ATTU static void meshdelete_remove_vert(MeshDelete *meshdelete, int v)
{
  BLI_assert(0 <= v && v < meshdelete->totvert);
  BLI_BITMAP_DISABLE(meshdelete->vert_bmap, v);
}

static void meshdelete_remove_edge(MeshDelete *meshdelete, int e)
{
  BLI_assert(0 <= e && e < meshdelete->totedge);
  BLI_BITMAP_DISABLE(meshdelete->edge_bmap, e);
}

ATTU static void meshdelete_remove_face(MeshDelete *meshdelete, int f)
{
  BLI_assert(0 <= f && f < meshdelete->totface);
  BLI_BITMAP_DISABLE(meshdelete->face_bmap, f);
}

ATTU static bool meshdelete_find_vert(MeshDelete *meshdelete, int v)
{
  BLI_assert(0 <= v && v < meshdelete->totvert);
  return BLI_BITMAP_TEST_BOOL(meshdelete->vert_bmap, v);
}

static bool meshdelete_find_edge(MeshDelete *meshdelete, int e)
{
  BLI_assert(0 <= e && e < meshdelete->totedge);
  return BLI_BITMAP_TEST_BOOL(meshdelete->edge_bmap, e);
}

static bool meshdelete_find_face(MeshDelete *meshdelete, int f)
{
  BLI_assert(0 <= f && f < meshdelete->totface);
  return BLI_BITMAP_TEST_BOOL(meshdelete->face_bmap, f);
}

/** MeshChange functions. */
#pragma mark MeshChange functions

static void init_meshchange(BoolState *bs, MeshChange *meshchange)
{
  init_intintmap(&meshchange->vert_merge_map);
  init_meshadd(bs, &meshchange->add);
  init_meshdelete(bs, &meshchange->delete);
  init_intset(&meshchange->intersection_edges);
  init_intset(&meshchange->face_flip);
  meshchange->use_face_kill_loose = false;
}

/** MeshPartSet functions. */
#pragma mark MeshPartSet functions

static void init_meshpartset(MeshPartSet *partset, const char *label)
{
  partset->meshparts = NULL;
  partset->tot_part = 0;
  zero_v3_db(partset->bbmin);
  zero_v3_db(partset->bbmax);
  partset->label = label;
}

static void add_part_to_partset(BoolState *bs, MeshPartSet *partset, MeshPart *part)
{
  BLI_linklist_prepend_arena(&partset->meshparts, part, bs->mem_arena);
  partset->tot_part++;
}

static MeshPart *partset_part(const MeshPartSet *partset, int index)
{
  LinkNode *ln = BLI_linklist_find(partset->meshparts, index);
  if (ln) {
    return (MeshPart *)ln->link;
  }
  return NULL;
}

/* Fill in partset->bbmin and partset->bbmax with axis aligned bounding box
 * for the partset.
 * Also calculates bbmin and bbmax for each part.
 * Add epsilon buffer on all sides.
 */
static void calc_partset_bb_eps(BoolState *bs, MeshPartSet *partset, double eps)
{
  LinkNode *ln;
  MeshPart *part;
  int i;

  if (partset->meshparts == NULL) {
    zero_v3_db(partset->bbmin);
    zero_v3_db(partset->bbmax);
    return;
  }
  copy_v3_db(partset->bbmin, DBL_MAX);
  copy_v3_db(partset->bbmax, -DBL_MAX);
  for (ln = partset->meshparts; ln; ln = ln->next) {
    part = (MeshPart *)ln->link;
    calc_part_bb_eps(bs, part, eps);
    for (i = 0; i < 3; i++) {
      partset->bbmin[i] = min_dd(partset->bbmin[i], part->bbmin[i]);
      partset->bbmax[i] = max_dd(partset->bbmax[i], part->bbmax[i]);
    }
  }
  /* eps padding was already added in calc_part_bb_eps. */
}

/** MeshPart functions. */
#pragma mark MeshPart functions

static void init_meshpart(BoolState *UNUSED(bs), MeshPart *part)
{
  memset(part, 0, sizeof(*part));
}

ATTU static MeshPart *copy_part(BoolState *bs, const MeshPart *part)
{
  MeshPart *copy;
  MemArena *arena = bs->mem_arena;

  copy = BLI_memarena_alloc(bs->mem_arena, sizeof(*copy));
  copy_v4_v4_db(copy->plane, part->plane);
  copy_v3_v3_db(copy->bbmin, part->bbmin);
  copy_v3_v3_db(copy->bbmax, part->bbmax);

  /* All links in lists are ints, so can use shallow copy. */
  copy->verts = linklist_shallow_copy_arena(part->verts, arena);
  copy->edges = linklist_shallow_copy_arena(part->edges, arena);
  copy->faces = linklist_shallow_copy_arena(part->faces, arena);
  return copy;
}

static int part_totvert(const MeshPart *part)
{
  return BLI_linklist_count(part->verts);
}

static int part_totedge(const MeshPart *part)
{
  return BLI_linklist_count(part->edges);
}

static int part_totface(const MeshPart *part)
{
  return BLI_linklist_count(part->faces);
}

/*
 * Return the index in MeshPart space of the index'th face in part.
 * "MeshPart space" means that if the f returned is in the range of
 * face indices in the underlying IMesh, then it represents the face
 * in the IMesh. If f is greater than or equal to that, then it represents
 * the face that is (f - im's totf)th in the new_faces list.
 */
static int part_face(const MeshPart *part, int index)
{
  LinkNode *ln;

  ln = BLI_linklist_find(part->faces, index);
  if (ln) {
    return POINTER_AS_INT(ln->link);
  }
  return -1;
}

/* Like part_face, but for vertices. */
static int part_vert(const MeshPart *part, int index)
{
  LinkNode *ln;

  ln = BLI_linklist_find(part->verts, index);
  if (ln) {
    return POINTER_AS_INT(ln->link);
  }
  return -1;
}

/* Like part_face, but for edges. */
static int part_edge(const MeshPart *part, int index)
{
  LinkNode *ln;

  ln = BLI_linklist_find(part->edges, index);
  if (ln) {
    return POINTER_AS_INT(ln->link);
  }
  return -1;
}

/* Fill part->bbmin and part->bbmax with the axis-aligned bounding box
 * for the part.
 * Add an epsilon buffer on all sides.
 */
static void calc_part_bb_eps(BoolState *bs, MeshPart *part, double eps)
{
  IMesh *im = &bs->im;
  LinkNode *ln;
  int v, e, f, i, flen, j;

  copy_v3_db(part->bbmin, DBL_MAX);
  copy_v3_db(part->bbmax, -DBL_MAX);
  for (ln = part->verts; ln; ln = ln->next) {
    v = POINTER_AS_INT(ln->link);
    bb_update(part->bbmin, part->bbmax, v, im);
  }
  for (ln = part->edges; ln; ln = ln->next) {
    e = POINTER_AS_INT(ln->link);
    /* TODO: handle edge verts */
    printf("calc_part_bb_eps please implement edge (%d)\n", e);
  }
  for (ln = part->faces; ln; ln = ln->next) {
    f = POINTER_AS_INT(ln->link);
    flen = imesh_facelen(im, f);
    for (j = 0; j < flen; j++) {
      v = imesh_face_vert(im, f, j);
      bb_update(part->bbmin, part->bbmax, v, im);
    }
  }
  if (part->bbmin[0] == DBL_MAX) {
    zero_v3_db(part->bbmin);
    zero_v3_db(part->bbmax);
    return;
  }
  for (i = 0; i < 3; i++) {
    part->bbmin[i] -= eps;
    part->bbmax[i] += eps;
  }
}

static bool parts_may_intersect(const MeshPart *part1, const MeshPart *part2)
{
  return isect_aabb_aabb_v3_db(part1->bbmin, part1->bbmax, part2->bbmin, part2->bbmax);
}

static bool part_may_intersect_partset(const MeshPart *part, const MeshPartSet *partset)
{
  return isect_aabb_aabb_v3_db(part->bbmin, part->bbmax, partset->bbmin, partset->bbmax);
}

/* Return true if a_plane and b_plane are the same plane, to within eps.
 * Assume normal part of plane is normalized. */
static bool planes_are_coplanar(const double a_plane[4], const double b_plane[4], double eps)
{
  double norms_dot;

  /* They are the same plane even if they have opposite-facing normals,
   * in which case the 4th constants will also be opposite. */
  norms_dot = dot_v3v3_db(a_plane, b_plane);
  if (norms_dot > 0.0) {
    return fabs(norms_dot - 1.0) <= eps && fabs(a_plane[3] - b_plane[3]) <= eps;
  }
  else {
    return fabs(norms_dot + 1.0) <= eps && fabs(a_plane[3] + b_plane[3]) <= eps;
  }
}

/* Return the MeshPart in partset for plane.
 * If none exists, make a new one for the plane and add
 * it to partset.
 */
static MeshPart *find_part_for_plane(BoolState *bs, MeshPartSet *partset, const double plane[4])
{
  LinkNode *ln;
  MeshPart *new_part;

  for (ln = partset->meshparts; ln; ln = ln->next) {
    MeshPart *p = (MeshPart *)ln->link;
    if (planes_are_coplanar(plane, p->plane, bs->eps)) {
      return p;
    }
  }
  new_part = BLI_memarena_alloc(bs->mem_arena, sizeof(MeshPart));
  init_meshpart(bs, new_part);
  copy_v4_v4_db(new_part->plane, plane);
  add_part_to_partset(bs, partset, new_part);
  return new_part;
}

ATTU static void add_vert_to_part(BoolState *bs, MeshPart *part, int v)
{
  BLI_linklist_prepend_arena(&part->verts, POINTER_FROM_INT(v), bs->mem_arena);
}

ATTU static void add_edge_to_part(BoolState *bs, MeshPart *part, int e)
{
  BLI_linklist_prepend_arena(&part->verts, POINTER_FROM_INT(e), bs->mem_arena);
}

static void add_face_to_part(BoolState *bs, MeshPart *part, int f)
{
  BLI_linklist_prepend_arena(&part->faces, POINTER_FROM_INT(f), bs->mem_arena);
}

/* If part consists of only one face from IMesh, return the number of vertices
 * in the face. Else return 0.
 */
ATTU static int part_is_one_im_face(BoolState *bs, const MeshPart *part)
{
  int f;

  if (part->verts == NULL && part->edges == NULL && part->faces != NULL &&
      BLI_linklist_count(part->faces) == 1) {
    f = POINTER_AS_INT(part->faces->link);
    return imesh_facelen(&bs->im, f);
  }
  return 0;
}

/** IMeshPlus functions. */
#pragma mark IMeshPlus functions

static void init_imeshplus(IMeshPlus *imp, IMesh *im, MeshAdd *meshadd)
{
  imp->im = im;
  imp->meshadd = meshadd;
}

static int imeshplus_facelen(const IMeshPlus *imp, int f)
{
  IMesh *im = imp->im;
  return (f < imesh_totface(im)) ? imesh_facelen(im, f) : meshadd_facelen(imp->meshadd, f);
}

static void imeshplus_get_face_no(const IMeshPlus *imp, int f, double *r_no)
{
  IMesh *im = imp->im;
  if (f < imesh_totface(im)) {
    imesh_get_face_no(im, f, r_no);
  }
  else {
    meshadd_get_face_no(imp->meshadd, f, r_no);
  }
}

static int imeshplus_face_vert(const IMeshPlus *imp, int f, int index)
{
  IMesh *im = imp->im;
  return (f < imesh_totface(im)) ? imesh_face_vert(im, f, index) :
                                   meshadd_face_vert(imp->meshadd, f, index);
}

ATTU static void imeshplus_get_vert_co(const IMeshPlus *imp, int v, float *r_coords)
{
  IMesh *im = imp->im;
  if (v < imesh_totvert(im)) {
    imesh_get_vert_co(im, v, r_coords);
  }
  else {
    meshadd_get_vert_co(imp->meshadd, v, r_coords);
  }
}

static void imeshplus_get_vert_co_db(const IMeshPlus *imp, int v, double *r_coords)
{
  IMesh *im = imp->im;
  if (v < imesh_totvert(im)) {
    imesh_get_vert_co_db(im, v, r_coords);
  }
  else {
    meshadd_get_vert_co_db(imp->meshadd, v, r_coords);
  }
}

static void imeshplus_get_edge_verts(const IMeshPlus *imp, int e, int *r_v1, int *r_v2)
{
  IMesh *im = imp->im;
  if (e < imesh_totedge(im)) {
    imesh_get_edge_verts(im, e, r_v1, r_v2);
  }
  else {
    meshadd_get_edge_verts(imp->meshadd, e, r_v1, r_v2);
  }
}

/** PartPartIntersect functions. */
#pragma mark PartPartIntersect functions

static void init_partpartintersect(PartPartIntersect *ppi)
{
  memset(ppi, 0, sizeof(*ppi));
}

static void add_vert_to_partpartintersect(BoolState *bs, PartPartIntersect *ppi, int v)
{
  BLI_linklist_append_arena(&ppi->verts, POINTER_FROM_INT(v), bs->mem_arena);
}

static void add_edge_to_partpartintersect(BoolState *bs, PartPartIntersect *ppi, int e)
{
  BLI_linklist_append_arena(&ppi->edges, POINTER_FROM_INT(e), bs->mem_arena);
}

static void add_face_to_partpartintersect(BoolState *bs, PartPartIntersect *ppi, int f)
{
  BLI_linklist_append_arena(&ppi->faces, POINTER_FROM_INT(f), bs->mem_arena);
}

/** Intersection Algorithm functions. */
#pragma mark Intersection Algorithm functions

/* Fill partset with parts for each plane for which there is a face
 * in bs->im.
 * Use bs->test_fn to check elements against test_val, to see whether or not it should be in the
 * result.
 */
static void find_coplanar_parts(BoolState *bs,
                                MeshPartSet *partset,
                                int test_val,
                                const char *label)
{
  IMesh *im = &bs->im;
  MeshPart *part;
  int im_nf, f, test;
  double plane[4];

  init_meshpartset(partset, label);
  im_nf = imesh_totface(im);
  for (f = 0; f < im_nf; f++) {
    if (test_val != TEST_ALL) {
      test = imesh_test_face(im, bs->test_fn, bs->test_fn_user_data, f);
      if (test != test_val) {
        continue;
      }
    }
    imesh_get_face_plane(im, f, plane);
    part = find_part_for_plane(bs, partset, plane);
    add_face_to_part(bs, part, f);
  }
  /* TODO: look for loose verts and wire edges to add to each partset */
  calc_partset_bb_eps(bs, partset, bs->eps);
}

/*
 * Intersect all the geometry in part, assumed to be in one plane, together with other
 * geometry as given in the ppis list (each link of which is a PartPartIntersect *).
 * Return a PartPartIntersect that gives the new geometry that should
 * replace the geometry in part. May also add new elements in meshadd,
 * and may also add vert merges in vert_merge_map.
 * If no output is needed, return NULL.
 */
static PartPartIntersect *self_intersect_part_and_ppis(BoolState *bs,
                                                       MeshPart *part,
                                                       LinkNodePair *ppis,
                                                       MeshChange *change)
{
  CDT_input in;
  CDT_result *out;
  LinkNode *ln, *lnv, *lne, *lnf;
  PartPartIntersect *ppi, *ppi_out;
  MemArena *arena = bs->mem_arena;
  IMeshPlus imp;
  int i, j, part_nf, part_ne, part_nv, tot_ne, face_len, v, e, f, v1, v2;
  int nfaceverts, v_index, e_index, f_index, faces_index;
  int in_v, out_v, out_v2, start, in_e, out_e, in_f, out_f, e_eg, f_eg, f_eg_o, eg_len;
  int *imp_v, *imp_e;
  IntPair *new_face_data;
  IMesh *im = &bs->im;
  IndexedIntSet verts_needed;
  IndexedIntSet edges_needed;
  IndexedIntSet faces_needed;
  IntIntMap in_to_vmap;
  IntIntMap in_to_emap;
  IntIntMap in_to_fmap;
  IntSet *f_other_egs;
  double mat_2d[3][3];
  double mat_2d_inv[3][3];
  double xyz[3], save_z, p[3], q[3], fno[3];
  bool ok, reverse_face;
  MeshAdd *meshadd = &change->add;
  MeshDelete *meshdelete = &change->delete;
  IntIntMap *vert_merge_map = &change->vert_merge_map;
#ifdef BOOLDEBUG
  int dbg_level = 1;
#endif

#ifdef BOOLDEBUG
  if (dbg_level > 0) {
    printf("\nSELF_INTERSECT_PART_AND_PPIS\n\n");
    if (dbg_level > 1) {
      dump_part(part, "self_intersect_part");
      printf("ppis\n");
      for (ln = ppis->list; ln; ln = ln->next) {
        ppi = (PartPartIntersect *)ln->link;
        dump_partpartintersect(ppi, "");
      }
    }
  }
#endif
  /* Find which vertices are needed for CDT input */
  part_nf = part_totface(part);
  part_ne = part_totedge(part);
  part_nv = part_totvert(part);
  if (part_nf <= 1 && part_ne == 0 && part_nv == 0 && ppis->list == NULL) {
#ifdef BOOLDEBUG
    if (dbg_level > 0) {
      printf("trivial 1 face case\n");
    }
#endif
    return NULL;
  }
  init_indexedintset(&verts_needed);
  init_indexedintset(&edges_needed);
  init_indexedintset(&faces_needed);
  init_intintmap(&in_to_vmap);
  init_intintmap(&in_to_emap);
  init_intintmap(&in_to_fmap);
  init_imeshplus(&imp, &bs->im, meshadd);

  /* nfaceverts will accumulate the total lengths of all faces added. */
#ifdef BOOLDEBUG
  if (dbg_level > 0) {
    printf("\nself_intersect_part_and_ppis: gathering needed edges and verts\n\n");
  }
#endif
  nfaceverts = 0;
  for (i = 0; i < part_nf; i++) {
    f = part_face(part, i);
    BLI_assert(f != -1);
    face_len = imesh_facelen(im, f);
    nfaceverts += face_len;
    for (j = 0; j < face_len; j++) {
      v = imesh_face_vert(im, f, j);
      BLI_assert(v != -1);
      v_index = add_int_to_indexedintset(bs, &verts_needed, v);
      add_to_intintmap(bs, &in_to_vmap, v_index, v);
    }
    f_index = add_int_to_indexedintset(bs, &faces_needed, f);
    add_to_intintmap(bs, &in_to_fmap, f_index, f);
  }
  for (i = 0; i < part_ne; i++) {
    e = part_edge(part, i);
    BLI_assert(e != -1);
    imeshplus_get_edge_verts(&imp, e, &v1, &v2);
    BLI_assert(v1 != -1 && v2 != -1);
    v_index = add_int_to_indexedintset(bs, &verts_needed, v1);
    add_to_intintmap(bs, &in_to_vmap, v_index, v1);
    v_index = add_int_to_indexedintset(bs, &verts_needed, v2);
    add_to_intintmap(bs, &in_to_vmap, v_index, v2);
    e_index = add_int_to_indexedintset(bs, &edges_needed, e);
    add_to_intintmap(bs, &in_to_emap, e_index, e);
  }
  for (i = 0; i < part_nv; i++) {
    v = part_vert(part, i);
    BLI_assert(v != -1);
    v_index = add_int_to_indexedintset(bs, &verts_needed, v);
    add_to_intintmap(bs, &in_to_vmap, v_index, v);
  }
  for (ln = ppis->list; ln; ln = ln->next) {
    ppi = (PartPartIntersect *)ln->link;
    for (lnv = ppi->verts.list; lnv; lnv = lnv->next) {
      v = POINTER_AS_INT(lnv->link);
      if (!find_in_indexedintset(&verts_needed, v)) {
        v_index = add_int_to_indexedintset(bs, &verts_needed, v);
        add_to_intintmap(bs, &in_to_vmap, v_index, v);
      }
    }
    for (lne = ppi->edges.list; lne; lne = lne->next) {
      e = POINTER_AS_INT(lne->link);
      if (!find_in_indexedintset(&edges_needed, e)) {
        imeshplus_get_edge_verts(&imp, e, &v1, &v2);
        BLI_assert(v1 != -1 && v2 != -1);
        v_index = add_int_to_indexedintset(bs, &verts_needed, v1);
        add_to_intintmap(bs, &in_to_vmap, v_index, v1);
        v_index = add_int_to_indexedintset(bs, &verts_needed, v2);
        add_to_intintmap(bs, &in_to_vmap, v_index, v2);
        e_index = add_int_to_indexedintset(bs, &edges_needed, e);
        add_to_intintmap(bs, &in_to_emap, e_index, e);
      }
    }
    for (lnf = ppi->faces.list; lnf; lnf = lnf->next) {
      f = POINTER_AS_INT(lnf->link);
      if (!find_in_indexedintset(&faces_needed, f)) {
        face_len = imeshplus_facelen(&imp, f);
        nfaceverts += face_len;
        for (j = 0; j < face_len; j++) {
          v = imesh_face_vert(im, f, j);
          BLI_assert(v != -1);
          if (!find_in_indexedintset(&verts_needed, v)) {
            v_index = add_int_to_indexedintset(bs, &verts_needed, v);
            add_to_intintmap(bs, &in_to_vmap, v_index, v);
          }
        }
        f_index = add_int_to_indexedintset(bs, &faces_needed, f);
        add_to_intintmap(bs, &in_to_fmap, f_index, f);
      }
    }
  }
  /* Edges implicit in faces will come back as orig edges, so handle those. */
  tot_ne = edges_needed.size;
  for (i = 0; i < faces_needed.size; i++) {
    f = indexedintset_get_value_by_index(&faces_needed, i);
    face_len = imesh_facelen(im, f);
    for (j = 0; j < face_len; j++) {
      v1 = imesh_face_vert(im, f, j);
      v2 = imesh_face_vert(im, f, (j + 1) % face_len);
      e = imesh_find_edge(im, v1, v2);
      BLI_assert(e != -1);
      add_to_intintmap(bs, &in_to_emap, j + tot_ne, e);
    }
    tot_ne += face_len;
  }

#ifdef BOOLDEBUG
  if (dbg_level > 0) {
    printf("self_intersect_part_and_ppis: cdt input maps\n\n");
    dump_intintmap(&in_to_vmap, "cdt v -> mesh v", "  ");
    printf("\n");
    dump_intintmap(&in_to_emap, "cdt e -> mesh e", "  ");
    printf("\n");
    dump_intintmap(&in_to_fmap, "cdt f -> mesh f", " ");
    printf("\n");
  }
#endif

  in.verts_len = verts_needed.size;
  in.edges_len = edges_needed.size;
  in.faces_len = faces_needed.size;
  in.vert_coords = BLI_array_alloca(in.vert_coords, (size_t)in.verts_len);
  if (in.edges_len != 0) {
    in.edges = BLI_array_alloca(in.edges, (size_t)in.edges_len);
  }
  else {
    in.edges = NULL;
  }
  in.faces = BLI_array_alloca(in.faces, (size_t)nfaceverts);
  in.faces_start_table = BLI_array_alloca(in.faces_start_table, (size_t)in.faces_len);
  in.faces_len_table = BLI_array_alloca(in.faces_len_table, (size_t)in.faces_len);
  in.epsilon = (float)bs->eps;

  /* Fill in the vert_coords of CDT input */

  /* Find mat_2d: matrix to rotate so that plane normal moves to z axis */
  axis_dominant_v3_to_m3_db(mat_2d, part->plane);
  ok = invert_m3_m3_db(mat_2d_inv, mat_2d);
  BLI_assert(ok);

  for (i = 0; i < in.verts_len; i++) {
    v = indexedintset_get_value_by_index(&verts_needed, i);
    BLI_assert(v != -1);
    imeshplus_get_vert_co_db(&imp, v, p);
    mul_v3_m3v3_db(xyz, mat_2d, p);
    copy_v2fl_v2db(in.vert_coords[i], xyz);
#ifdef BOOLDEBUG
    if (dbg_level > 0) {
      printf("in vert %d (needed vert %d) was (%g,%g,%g), rotated (%g,%g,%g)\n",
             i,
             v,
             F3(p),
             F3(xyz));
    }
#endif
    if (i == 0) {
      /* If part is truly coplanar, all z components of rotated v should be the same.
       * Save it so that can rotate back to correct place when done.
       */
      save_z = xyz[2];
    }
  }

  /* Fill in the face data of CDT input. */
  /* faces_index is next place in flattened faces table to put a vert index. */
  faces_index = 0;
  for (i = 0; i < in.faces_len; i++) {
    f = indexedintset_get_value_by_index(&faces_needed, i);
    face_len = imeshplus_facelen(&imp, f);
    in.faces_start_table[i] = faces_index;
    imeshplus_get_face_no(&imp, f, fno);
    reverse_face = (dot_v3v3_db(part->plane, fno) < 0.0);
    for (j = 0; j < face_len; j++) {
      v = imeshplus_face_vert(&imp, f, reverse_face ? face_len - j - 1 : j);
      BLI_assert(v != -1);
      v_index = indexedintset_get_index_for_value(&verts_needed, v);
      in.faces[faces_index++] = v_index;
    }
    in.faces_len_table[i] = faces_index - in.faces_start_table[i];
  }

  /* Fill in edge data of CDT input. */
  for (i = 0; i < in.edges_len; i++) {
    e = indexedintset_get_value_by_index(&edges_needed, i);
    imeshplus_get_edge_verts(&imp, e, &v1, &v2);
    BLI_assert(v1 != -1 && v2 != -1);
    in.edges[i][0] = indexedintset_get_index_for_value(&verts_needed, v1);
    in.edges[i][1] = indexedintset_get_index_for_value(&verts_needed, v2);
  }

  /* TODO: fill in loose vert data of CDT input. */

#ifdef BOOLDEBUG
  if (dbg_level > 0) {
    printf("\n");
    dump_cdt_input(&in, "");
    printf("\n");
  }
#endif

  out = BLI_delaunay_2d_cdt_calc(&in, CDT_CONSTRAINTS_VALID_BMESH);

#ifdef BOOLDEBUG
  if (dbg_level > 0) {
    printf("\n");
    dump_cdt_result(out, "", "");
    printf("\n");
    printf("self_intersect_part_and_ppis: make ppi result\n");
  }
#endif

  /* Make the PartPartIntersect that represents the output of the CDT. */
  ppi_out = BLI_memarena_alloc(bs->mem_arena, sizeof(*ppi_out));
  init_partpartintersect(ppi_out);

  /* imp_v will map an output vert index to an IMesh + MeshAdd space vertex. */
  imp_v = BLI_array_alloca(imp_v, (size_t)out->verts_len);
  for (out_v = 0; out_v < out->verts_len; out_v++) {
    if (out->verts_orig_len_table[out_v] > 0) {
      int try_v;
      /* out_v maps to a vertex we fed in from verts_needed. */
      start = out->verts_orig_start_table[out_v];
      /* Choose orig that maps to lowest imesh vert, to make for a stable algorithm. */
      in_v = -1;
      v = INT_MAX;
      for (i = 0; i < out->verts_orig_len_table[out_v]; i++) {
        int try_in_v = out->verts_orig[start + i];
        if (!find_in_intintmap(&in_to_vmap, try_in_v, &try_v)) {
          printf("shouldn't happen, %d not in in_to_vmap\n", try_in_v);
          BLI_assert(false);
        }
        if (try_v < v) {
          v = try_v;
          in_v = try_in_v;
        }
      }
      BLI_assert(v != INT_MAX);
      /* If v is in IMesh then any other orig's that are in IMesh need to
       * go into the vert_merge_map. */
      if (v < imesh_totvert(im) && out->verts_orig_len_table[out_v] > 1) {
        for (i = 0; i < out->verts_orig_len_table[out_v]; i++) {
          j = out->verts_orig[start + i];
          if (j != in_v) {
            if (!find_in_intintmap(&in_to_vmap, j, &v1)) {
              printf("shouldn't happen, %d not in in_to_vmap\n", j);
              BLI_assert(false);
            }
            if (v1 < imesh_totvert(im)) {
              add_to_intintmap(bs, vert_merge_map, v1, v);
              meshdelete_add_vert(meshdelete, v1);
            }
          }
        }
      }
    }
    else {
      /* Need a new imp vertex for out_v. */
      copy_v2db_v2fl(q, out->vert_coords[out_v]);
      q[2] = save_z;
      mul_v3_m3v3_db(p, mat_2d_inv, q);
      /* p should not already be in the IMesh because such verts should have been added to the
       * input. However, it is possible that the vert might already be in meshadd.  */
      v = meshadd_add_vert_db(bs, meshadd, p, -1, true);
    }
    imp_v[out_v] = v;
    add_vert_to_partpartintersect(bs, ppi_out, v);
  }

#ifdef BOOLDEBUG
  if (dbg_level > 0) {
    printf("\nimp_v, the map from output vert to imesh/meshadd vert\n");
    for (out_v = 0; out_v < out->verts_len; out_v++) {
      printf("  outv %d => imeshv %d\n", out_v, imp_v[out_v]);
    }
    printf("\n");
  }
#endif

  /* Similar to above code, but for edges. */
  imp_e = BLI_array_alloca(imp_e, (size_t)out->edges_len);
  for (out_e = 0; out_e < out->edges_len; out_e++) {
    e_eg = -1;
    if (out->edges_orig_len_table[out_e] > 0) {
      start = out->edges_orig_start_table[out_e];
      in_e = min_int_in_array(&out->edges_orig[start], out->edges_orig_len_table[out_e]);
      if (!find_in_intintmap(&in_to_emap, in_e, &e_eg)) {
        printf("shouldn't happen, %d not in in_to_emap\n", in_e);
        BLI_assert(false);
      }
      /* If e_eg is in IMesh then need to record e_eg and any other edges
       * in the orig for out_e as deleted unless the output edge is
       * the same as the input one. We'll discover the "same as"
       * condition below, so delete here and add back there if so.
       */
      if (e_eg < imesh_totedge(im)) {
        for (i = 0; i < out->edges_orig_len_table[out_e]; i++) {
          j = out->edges_orig[start + i];
          if (!find_in_intintmap(&in_to_emap, j, &e)) {
            printf("shouldn't happen, %d not in in_to_emap\n", j);
            BLI_assert(false);
          }
          if (j < imesh_totedge(im)) {
            meshdelete_add_edge(meshdelete, e);
          }
        }
      }
    }
    /* If e_eg != -1 now, out_e may be only a part of e_eg; if so, make a new e but use e_eg as
     * example.
     */
    v1 = imp_v[out->edges[out_e][0]];
    v2 = imp_v[out->edges[out_e][1]];
    v1 = resolve_merge(v1, vert_merge_map);
    v2 = resolve_merge(v2, vert_merge_map);
    if (e_eg != -1) {
      int ev1, ev2;
      imeshplus_get_edge_verts(&imp, e_eg, &ev1, &ev2);
      if (!((v1 == ev1 && v2 == ev2) || (v1 == ev2 && v2 == ev1))) {
        e = meshadd_add_edge(bs, meshadd, v1, v2, e_eg, true);
      }
      else {
        e = e_eg;
        if (e < imesh_totedge(im)) {
          /* Don't want to delete e after all. */
          meshdelete_remove_edge(meshdelete, e);
        }
      }
    }
    else {
      e = meshadd_add_edge(bs, meshadd, v1, v2, e_eg, true);
    }
    imp_e[out_e] = e;
    add_edge_to_partpartintersect(bs, ppi_out, e);
  }

#ifdef BOOLDEBUG
  if (dbg_level > 0) {
    printf("\nimp_e, the map from output edge to imesh/meshadd edge\n");
    for (out_e = 0; out_e < out->edges_len; out_e++) {
      printf("  oute %d => imeshe %d\n", out_e, imp_e[out_e]);
    }
    printf("\n");
  }
#endif

  /* Now for the faces. */
  for (out_f = 0; out_f < out->faces_len; out_f++) {
    in_f = -1;
    f_eg = -1;
    f_other_egs = NULL;
    if (out->faces_orig_len_table[out_f] > 0) {
      start = out->faces_orig_start_table[out_f];
      eg_len = out->faces_orig_len_table[out_f];
      in_f = min_int_in_array(&out->faces_orig[start], eg_len);
      if (!find_in_intintmap(&in_to_fmap, in_f, &f_eg)) {
        printf("shouldn't happen, %d not in in_to_fmap\n", in_f);
        BLI_assert(false);
      }
      if (eg_len > 1) {
        /* Record the other examples too. They may be needed for boolean operations. */
        f_other_egs = BLI_memarena_alloc(arena, sizeof(*f_other_egs));
        for (i = start; i < start + eg_len; i++) {
          if (!find_in_intintmap(&in_to_fmap, out->faces_orig[i], &f_eg_o)) {
            printf("shouldn't happen, %d not in in_to_fmap\n", out->faces_orig[i]);
          }
          if (f_eg_o != f_eg) {
            add_to_intset(bs, f_other_egs, f_eg_o);
          }
        }
      }
      /* If f_eg is in IMesh then need to record f_eg and any other faces
       * in the orig for out_f as deleted. */
      if (f_eg < imesh_totface(im)) {
        for (i = 0; i < out->faces_orig_len_table[out_f]; i++) {
          j = out->faces_orig[start + i];
          if (!find_in_intintmap(&in_to_fmap, j, &f)) {
            printf("shouldn't happen, %d not in in_to_fmap\n", j);
            BLI_assert(false);
          }
          if (j < imesh_totface(im)) {
            meshdelete_add_face(meshdelete, f);
          }
        }
      }
    }
    /* Even if f is same as an existing face, we make a new one, to simplify "what to delete"
     * bookkeeping later. */
    face_len = out->faces_len_table[out_f];
    start = out->faces_start_table[out_f];
    new_face_data = (IntPair *)BLI_memarena_alloc(arena,
                                                  (size_t)face_len * sizeof(new_face_data[0]));
    for (i = 0; i < face_len; i++) {
      out_v = out->faces[start + i];
      v = imp_v[out_v];
      new_face_data[i].first = v;
      out_v2 = out->faces[start + ((i + 1) % face_len)];
      v2 = imp_v[out_v2];
      /* Edge (v, v2) should be an edge already added to ppi_out. Also e is either in im or
       * meshadd. */
      e = find_edge_by_verts_in_meshadd(meshadd, v, v2);
      if (e == -1) {
        e = imesh_find_edge(&bs->im, v, v2);
      }
      if (e == -1) {
        printf("shouldn't happen: couldn't find e=(%d,%d)\n", v, v2);
        BLI_assert(false);
      }
      new_face_data[i].second = e;
    }
    f = meshadd_add_face(bs, meshadd, new_face_data, face_len, f_eg, f_other_egs);
    add_face_to_partpartintersect(bs, ppi_out, f);
  }

  BLI_delaunay_2d_cdt_free(out);
  return ppi_out;
}

/* Find geometry that in the coplanar parts which may intersect.
 * For now, just assume all can intersect.
 */
static PartPartIntersect *coplanar_part_part_intersect(
    BoolState *bs, MeshPart *part_a, int a_index, MeshPart *part_b, int b_index)
{
  MemArena *arena = bs->mem_arena;
  PartPartIntersect *ppi;
  int totv_a, tote_a, totf_a, totv_b, tote_b, totf_b;
  int i, v, e, f;

  ppi = BLI_memarena_alloc(arena, sizeof(*ppi));
  ppi->a_index = a_index;
  ppi->b_index = b_index;
  init_partpartintersect(ppi);
  totv_a = part_totvert(part_a);
  tote_a = part_totedge(part_a);
  totf_a = part_totface(part_a);
  totv_b = part_totvert(part_b);
  tote_b = part_totedge(part_b);
  totf_b = part_totface(part_b);

  for (i = 0; i < totv_a; i++) {
    v = part_vert(part_a, i);
    add_vert_to_partpartintersect(bs, ppi, v);
  }
  for (i = 0; i < totv_b; i++) {
    v = part_vert(part_b, i);
    add_vert_to_partpartintersect(bs, ppi, v);
  }
  for (i = 0; i < tote_a; i++) {
    e = part_edge(part_a, i);
    add_edge_to_partpartintersect(bs, ppi, e);
  }
  for (i = 0; i < tote_b; i++) {
    e = part_edge(part_b, i);
    add_edge_to_partpartintersect(bs, ppi, e);
  }
  for (i = 0; i < totf_a; i++) {
    f = part_face(part_a, i);
    add_face_to_partpartintersect(bs, ppi, f);
  }
  for (i = 0; i < totf_b; i++) {
    f = part_face(part_b, i);
    add_face_to_partpartintersect(bs, ppi, f);
  }
  return ppi;
}

typedef struct FaceEdgeInfo {
  double co[3];    /* coord of this face vertex */
  double isect[3]; /* intersection, if any, of this edge segment (starts at v) with line */
  double factor;   /* co = line_co1 + factor * line_dir */
  int v;           /* vertex index of this face coord */
  bool v_on;       /* is co on the line (within epsilon)? */
  bool isect_ok;   /* does this edge segment (excluding end vertex) intersect line? */
} FaceEdgeInfo;

/* Find intersection of a face with a line and return the intervals on line.
 * It is known that the line is in the same plane as the face.
 * If there is any intersection, it should be a series of points
 * and line segments on the line.
 * A point on the line can be represented as fractions of the distance from
 * line_co1 to line_co2. (The fractions can be negative or greater than 1.)
 * The intervals will be returned as a linked list of (start, end) fractions,
 * where single points will be represented by a pair (start, start).
 * The returned list will be in increasing order of the start fraction, and
 * the intervals will not overlap, though they may touch.
 * [TODO: handle case where face has several
 * edges on the line, or where faces fold back on themselves.]
 */
static void find_face_line_intersects(BoolState *bs,
                                      LinkNodePair *intervals,
                                      int f,
                                      const double line_co1[3],
                                      const double line_co2[3])
{
  IMesh *im = &bs->im;
  int flen, i, is;
  double eps = bs->eps;
  FaceEdgeInfo *finfo, *fi, *finext;
  double co_close[3], line_co1_to_co[3], line_dir[3];
  double *interval;
  double l_no[3], mu;
  double line_dir_len;
#ifdef BOOLDEBUG
  int dbg_level = 0;
#endif

#ifdef BOOLDEBUG
  if (dbg_level > 0) {
    printf("\nFIND_FACE_LINE_INTERSECTS, face %d\n", f);
    printf("along line (%f,%f,%f)(%f,%f,%f)\n", F3(line_co1), F3(line_co2));
  }
#endif
  intervals[0].list = NULL;
  intervals[0].last_node = NULL;
  flen = imesh_facelen(im, f);
  finfo = BLI_array_alloca(finfo, (size_t)flen);
  sub_v3_v3v3_db(line_dir, line_co2, line_co1);
  line_dir_len = len_v3_db(line_dir);
  for (i = 0; i < flen; i++) {
    fi = &finfo[i];
    fi->v = imesh_face_vert(im, f, i);
    imesh_get_vert_co_db(im, fi->v, fi->co);
    closest_to_line_v3_db(co_close, fi->co, line_co1, line_co2);
    fi->v_on = compare_v3v3_db(fi->co, co_close, eps);
    fi->isect_ok = fi->v_on;
    if (fi->v_on) {
      copy_v3_v3_db(fi->isect, co_close);
      sub_v3_v3v3_db(line_co1_to_co, co_close, line_co1);
      fi->factor = len_v3_db(line_co1_to_co) / line_dir_len;
      if (dot_v3v3_db(line_co1_to_co, line_dir) < 0.0) {
        fi->factor = -fi->factor;
      }
    }
    else {
      zero_v3_db(fi->isect);
      fi->factor = 0.0f;
    }
  }
  sub_v3_v3v3_db(l_no, line_co2, line_co1);
  normalize_v3_d(l_no);
  for (i = 0; i < flen; i++) {
    fi = &finfo[i];
    if (fi->isect_ok) {
      continue;
    }
    finext = &finfo[(i + 1) % flen];
    is = isect_line_seg_epsilon_v3_db(
        line_co1, line_co2, fi->co, finext->co, eps, fi->isect, &fi->factor, &mu);
    if (is > 0) {
      fi->isect_ok = true;
      if (finext->v_on && is != 2) {
        /* Don't count intersections of only the end of the line segment. */
        fi->isect_ok = false;
      }
    }
  }
#ifdef BOOLDEBUG
  if (dbg_level > 0) {
    for (i = 0; i < flen; i++) {
      fi = &finfo[i];
      printf("finfo[%d]: v=%d v_on=%d isect_ok=%d factor=%g isect=(%f,%f,%f)\n",
             i,
             fi->v,
             fi->v_on,
             fi->isect_ok,
             fi->factor,
             F3(fi->isect));
    }
  }
#endif
  /* For now just handle case of convex faces, which should be one of the following
   * cases: (1) no intersects; (2) 1 intersect (a vertex); (2) 2 intersects on two
   * edges; (3) line coincindes with one edge.
   * TODO: handle general case. Needs ray shooting to test inside/outside
   * or division into convex pieces or something.
   */
  if (true) { /* TODO: replace this with "is face convex?" test */
    int startpos = -1;
    int endpos = -1;
    for (i = 0; i < flen; i++) {
      if (finfo[i].isect_ok) {
        startpos = i;
        break;
      }
    }
    if (startpos == -1) {
#ifdef BOOLDEBUG
      if (dbg_level > 0) {
        printf("no intersections\n");
      }
#endif
      return;
    }
    endpos = startpos;
    for (i = (startpos + 1) % flen; i != startpos; i = (i + 1) % flen) {
      if (finfo[i].isect_ok) {
        endpos = i;
      }
    }
#ifdef BOOLDEBUG
    if (dbg_level > 0) {
      printf("startpos=%d, endpos=%d\n", startpos, endpos);
    }
#endif
    interval = BLI_memarena_alloc(bs->mem_arena, 2 * sizeof(double));
    if (finfo[startpos].factor <= finfo[endpos].factor) {
      interval[0] = finfo[startpos].factor;
      interval[1] = finfo[endpos].factor;
    }
    else {
      interval[0] = finfo[endpos].factor;
      interval[1] = finfo[startpos].factor;
    }
    if (interval[1] - interval[0] <= eps) {
      interval[1] = interval[0];
    }
  }
#ifdef BOOLDEBUG
  if (dbg_level > 0) {
    double co1[3], co2[3];
    madd_v3_v3v3db_db(co1, line_co1, line_dir, interval[0]);
    madd_v3_v3v3db_db(co2, line_co2, line_dir, interval[1]);
    printf("interval (dists) = (%f,%f) -> cooords (%.3f,%.3f,%.3f)(%.3f,%.3f,%.3f)\n",
           interval[0],
           interval[1],
           F3(co1),
           F3(co2));
  }
#endif
  BLI_linklist_append_arena(intervals, interval, bs->mem_arena);
}

/* Find geometry that in the non-coplanar parts which may intersect.
 * Needs to be the part of the geometry that is on the common line
 * of intersection, so that it is in the plane of both parts.
 */
static PartPartIntersect *non_coplanar_part_part_intersect(BoolState *bs,
                                                           MeshPart *part_a,
                                                           int a_index,
                                                           MeshPart *part_b,
                                                           int b_index,
                                                           MeshChange *change)
{
  MemArena *arena = bs->mem_arena;
  IMesh *im = &bs->im;
  PartPartIntersect *ppi;
  MeshPart *part;
  int totv[2], tote[2], totf[2];
  int i, v, e, f, pi, v1, v2;
  int index_a, index_b;
  LinkNodePair *intervals_a;
  LinkNodePair *intervals_b;
  LinkNodePair *intervals;
  LinkNode *ln, *lna, *lnb;
  int is;
  double co[3], co1[3], co2[3], co_close[3], line_co1[3], line_co2[3], line_dir[3];
  double co_close1[3], co_close2[3];
  double elen_squared;
  double faca1, faca2, facb1, facb2, facstart, facend, *inta, *intb;
  double eps = bs->eps;
  double eps_squared = eps * eps;
  bool on1, on2;
  MeshAdd *meshadd = &change->add;
  IntSet *intersection_edges = &change->intersection_edges;
#ifdef BOOLDEBUG
  int dbg_level = 1;
#endif

#ifdef BOOLDEBUG
  if (dbg_level > 0) {
    printf("\nNON_COPLANAR_PART_PART_INTERSECT a%d b%d\n\n", a_index, b_index);
  }
#endif
  if (!isect_plane_plane_v3_db(part_a->plane, part_b->plane, line_co1, line_dir)) {
    /* Presumably the planes are parallel if they are not coplanar and don't intersect. */
#ifdef BOOLDEBUG
    if (dbg_level > 0) {
      printf("planes don't intersect\n");
    }
#endif
    return NULL;
  }
  add_v3_v3v3_db(line_co2, line_co1, line_dir);

  ppi = BLI_memarena_alloc(arena, sizeof(*ppi));
  init_partpartintersect(ppi);
  ppi->a_index = a_index;
  ppi->b_index = b_index;

  for (pi = 0; pi < 2; pi++) {
    part = pi == 0 ? part_a : part_b;
    totv[pi] = part_totvert(part);
    for (i = 0; i < totv[pi]; i++) {
      v = part_vert(part, i);
      imesh_get_vert_co_db(im, v, co);
      closest_to_line_v3_db(co_close, co, line_co1, line_co2);
      if (compare_v3v3_db(co, co_close, eps)) {
        add_vert_to_partpartintersect(bs, ppi, v);
      }
    }
  }
  for (pi = 0; pi < 2; pi++) {
    part = pi == 0 ? part_a : part_b;
    tote[pi] = part_totedge(part);
    for (i = 0; i < tote[pi]; i++) {
      e = part_edge(part, i);
      imesh_get_edge_cos_db(im, e, co1, co2);
      /* First check if co1 and/or co2 are on line, within eps. */
      closest_to_line_v3_db(co_close1, co1, line_co1, line_co2);
      closest_to_line_v3_db(co_close2, co2, line_co1, line_co2);
      on1 = compare_v3v3_db(co1, co_close1, eps);
      on2 = compare_v3v3_db(co2, co_close2, eps);
      if (on1 || on2) {
        if (on1 && on2) {
          add_edge_to_partpartintersect(bs, ppi, e);
        }
        else {
          imesh_get_edge_verts(im, e, &v1, &v2);
          add_vert_to_partpartintersect(bs, ppi, on1 ? v1 : v2);
        }
      }
      else {
        is = isect_line_line_epsilon_v3_db(
            line_co1, line_co2, co1, co2, co_close1, co_close2, eps);
        if (is > 0) {
          /* co_close1 is closest on line to segment (co1,co2). */
          if (is == 1 || compare_v3v3_db(co_close1, co_close2, eps)) {
            /* Intersection is on line or within eps. Is it on e's segment? */
            elen_squared = len_squared_v3v3_db(co1, co2) + eps_squared;
            if (len_squared_v3v3_db(co_close1, co1) <= elen_squared &&
                len_squared_v3v3_db(co_close1, co2) <= elen_squared) {
              /* Maybe intersection point is some other point in mesh. */
              v = imesh_find_co_db(im, co_close1, eps);
              if (v == -1) {
                /* A new point. Need to add to meshadd. */
                v = meshadd_add_vert_db(bs, meshadd, co, -1, true);
              }
              add_vert_to_partpartintersect(bs, ppi, v);
            }
          }
        }
      }
    }
  }
  totf[0] = part_totface(part_a);
  totf[1] = part_totface(part_b);
  intervals_a = BLI_array_alloca(intervals_a, (size_t)totf[0]);
  intervals_b = BLI_array_alloca(intervals_b, (size_t)totf[1]);
  for (pi = 0; pi < 2; pi++) {
#ifdef BOOLDEBUG
    if (dbg_level > 0) {
      printf("non_coplanar_part_part_intersect: doing faces from part %s\n", pi == 0 ? "a" : "b");
    }
#endif
    part = pi == 0 ? part_a : part_b;
    totf[pi] = part_totface(part);
    for (i = 0; i < totf[pi]; i++) {
#ifdef BOOLDEBUG
      if (dbg_level > 0) {
        printf("doing %dth face of part\n", i);
      }
#endif
      f = part_face(part, i);
      intervals = pi == 0 ? intervals_a : intervals_b;
      find_face_line_intersects(bs, &intervals[i], f, line_co1, line_co2);
      if (intervals[i].list == NULL) {
#ifdef BOOLDEBUG
        if (dbg_level > 0) {
          printf("no intersections\n");
        }
#endif
      }
      else {
        for (ln = intervals[i].list; ln; ln = ln->next) {
          inta = (double *)ln->link;
          faca1 = inta[0];
          faca2 = inta[1];
          madd_v3_v3v3db_db(co, line_co1, line_dir, faca1);
          madd_v3_v3v3db_db(co2, line_co1, line_dir, faca2);
#ifdef BOOLDEBUG
          if (dbg_level > 0) {
            printf(
                "  (%f,%f) = (%.3f,%.3f,%.3f)(%.3f,%.3f,%.3f)\n", faca1, faca2, F3(co), F3(co2));
          }
#endif
        }
      }
    }
  }

#ifdef BOOLDEBUG
  if (dbg_level > 0) {
    printf("non_coplanar_part_part_intersect: intersecting face pair intervals\n");
  }
#endif
  /* Need to intersect the intervals of each face pair's intervals. */
  for (index_a = 0; index_a < totf[0]; index_a++) {
    lna = intervals_a[index_a].list;
    if (lna == NULL) {
      continue;
    }
    for (index_b = 0; index_b < totf[1]; index_b++) {
      lnb = intervals_b[index_b].list;
      if (lnb == NULL) {
        continue;
      }
#ifdef BOOLDEBUG
      if (dbg_level > 0) {
        printf("intersect intervals for faces %d and %d\n",
               part_face(part_a, index_a),
               part_face(part_b, index_b));
      }
#endif
      if (BLI_linklist_count(lna) == 1 && BLI_linklist_count(lnb) == 1) {
        /* Common special case of two single intervals to intersect. */
        inta = (double *)lna->link;
        intb = (double *)lnb->link;
        faca1 = inta[0];
        faca2 = inta[1];
        facb1 = intb[0];
        facb2 = intb[1];
        facstart = max_dd(faca1, facb1);
        facend = min_dd(faca2, facb2);
        if (facend < facstart - eps) {
#ifdef BOOLDEBUG
          if (dbg_level > 0) {
            printf("  no intersection\n");
          }
#endif
        }
        else {
          madd_v3_v3v3db_db(co, line_co1, line_dir, facstart);
          madd_v3_v3v3db_db(co2, line_co1, line_dir, facend);
#ifdef BOOLDEBUG
          if (dbg_level > 0) {
            printf(
                "  interval result: factors (%f,%f) = coords (%.5f,%.5f,%.5f)(%.5f,%.5f,%.5f)\n",
                facstart,
                facend,
                F3(co),
                F3(co2));
          }
#endif
          if (compare_v3v3_db(co, co2, eps)) {
            /* Add a single vertex. */
            v = imesh_find_co_db(im, co, eps);
            if (v == -1) {
              /* A new point. Need to add to meshadd. */
              v = meshadd_add_vert_db(bs, meshadd, co, -1, true);
            }
            add_vert_to_partpartintersect(bs, ppi, v);
          }
          else {
            /* Add an edge. */
            v1 = imesh_find_co_db(im, co, eps);
            if (v1 == -1) {
              /* A new point. Need to add to meshadd. */
              v1 = meshadd_add_vert_db(bs, meshadd, co, -1, true);
            }
            v2 = imesh_find_co_db(im, co2, eps);
            if (v2 == -1) {
              v2 = meshadd_add_vert_db(bs, meshadd, co2, -1, true);
            }
            e = imesh_find_edge(im, v1, v2);
            if (e == -1) {
              /* TODO: if overlaps an existing edge, use as example. */
              e = meshadd_add_edge(bs, meshadd, v1, v2, -1, true);
            }
            add_edge_to_partpartintersect(bs, ppi, e);
            add_to_intset(bs, intersection_edges, e);
          }
        }
      }
      else {
        printf("implement the multi-interval intersect case\n");
      }
    }
  }
  return ppi;
}

static PartPartIntersect *part_part_intersect(BoolState *bs,
                                              MeshPart *part_a,
                                              int a_index,
                                              MeshPart *part_b,
                                              int b_index,
                                              MeshChange *change)
{
  PartPartIntersect *ans;
  if (!parts_may_intersect(part_a, part_b)) {
    ans = NULL;
  }
  if (planes_are_coplanar(part_a->plane, part_b->plane, bs->eps)) {
    ans = coplanar_part_part_intersect(bs, part_a, a_index, part_b, b_index);
  }
  else {
    ans = non_coplanar_part_part_intersect(bs, part_a, a_index, part_b, b_index, change);
  }
  return ans;
}

/* Intersect all parts of a_partset with all parts of b_partset.
 */
static void intersect_partset_pair(BoolState *bs,
                                   MeshPartSet *a_partset,
                                   MeshPartSet *b_partset,
                                   MeshChange *meshchange)
{
  int a_index, b_index, tot_part_a, tot_part_b, bstart;
  MeshPart *part_a, *part_b;
  MemArena *arena = bs->mem_arena;
  bool same_partsets = (a_partset == b_partset);
  LinkNodePair *a_isects; /* Array of List of PairPartIntersect. */
  LinkNodePair *b_isects; /* Array of List of PairPartIntersect. */
  PartPartIntersect *isect;
  BLI_bitmap *bpart_coplanar_with_apart;
#ifdef BOOLDEBUG
  int dbg_level = 1;
#endif

#ifdef BOOLDEBUG
  if (dbg_level > 0) {
    printf("\nINTERSECT_PARTSET_PAIR\n\n");
    if (dbg_level > 0) {
      dump_partset(a_partset);
      dump_partset(b_partset);
    }
  }
#endif
  tot_part_a = a_partset->tot_part;
  tot_part_b = b_partset->tot_part;
  a_isects = BLI_memarena_calloc(arena, (size_t)tot_part_a * sizeof(a_isects[0]));
  b_isects = BLI_memarena_calloc(arena, (size_t)tot_part_b * sizeof(b_isects[0]));
  bpart_coplanar_with_apart = BLI_BITMAP_NEW_MEMARENA(arena, tot_part_b);

#ifdef BOOLDEBUG
  if (dbg_level > 0) {
    printf("\nIntersect_partset_pair: do all part - part preliminary intersections\n\n");
  }
#endif
  for (a_index = 0; a_index < tot_part_a; a_index++) {
    part_a = partset_part(a_partset, a_index);
    if (!part_may_intersect_partset(part_a, b_partset)) {
      continue;
    }
    bstart = same_partsets ? a_index + 1 : 0;
    for (b_index = bstart; b_index < tot_part_b; b_index++) {
      part_b = partset_part(b_partset, b_index);
      if (!same_partsets) {
        if (planes_are_coplanar(part_a->plane, part_b->plane, bs->eps)) {
          BLI_BITMAP_ENABLE(bpart_coplanar_with_apart, b_index);
        }
      }
      isect = part_part_intersect(bs, part_a, a_index, part_b, b_index, meshchange);
      if (isect != NULL) {
#ifdef BOOLDEBUG
        if (dbg_level > 0) {
          printf("Part a%d intersects part b%d\n", a_index, b_index);
          dump_partpartintersect(isect, "");
          printf("\n");
          dump_meshadd(&meshchange->add, "incremental");
        }
#endif
        BLI_linklist_append_arena(&a_isects[a_index], isect, arena);
        BLI_linklist_append_arena(&b_isects[b_index], isect, arena);
        if (same_partsets) {
          BLI_linklist_append_arena(&a_isects[b_index], isect, arena);
          BLI_linklist_append_arena(&b_isects[a_index], isect, arena);
        }
      }
    }
  }

#ifdef BOOLDEBUG
  if (dbg_level > 0) {
    printf("\nintersect_partset_pair: do self intersections\n\n");
  }
#endif
  /* Now self-intersect the parts with their lists of isects. */
  for (a_index = 0; a_index < tot_part_a; a_index++) {
    part_a = partset_part(a_partset, a_index);
#ifdef BOOLDEBUG
    if (dbg_level > 0) {
      printf("\nSELF INTERSECT part a%d with its ppis\n", a_index);
    }
#endif
    isect = self_intersect_part_and_ppis(bs, part_a, &a_isects[a_index], meshchange);
#ifdef BOOLDEBUG
    if (isect && dbg_level > 0) {
      dump_partpartintersect(isect, "after self intersect");
      dump_meshchange(meshchange, "after self intersect");
    }
#endif
  }
  if (!same_partsets) {
    for (b_index = 0; b_index < tot_part_b; b_index++) {
      part_b = partset_part(b_partset, b_index);
#ifdef BOOLDEBUG
      if (dbg_level > 0) {
        printf("\nSELF INTERSECT part b%d with its ppis\n", b_index);
      }
#endif
      if (BLI_BITMAP_TEST_BOOL(bpart_coplanar_with_apart, b_index)) {
#ifdef BOOLDEBUG
        if (dbg_level > 0) {
          printf("skipping self_intersect because coplanar with some a part\n");
        }
#endif
        continue;
      }
      isect = self_intersect_part_and_ppis(bs, part_b, &b_isects[b_index], meshchange);
#ifdef BOOLDEBUG
      if (isect && dbg_level > 0) {
        dump_partpartintersect(isect, "after self intersect b");
        dump_meshchange(meshchange, "after self intersect b");
      }
#endif
    }
  }
}

static void do_boolean_op(BoolState *bm, const int boolean_mode);

/**
 * Intersect faces
 * leaving the resulting edges tagged.
 *
 * \param test_fn: Return value: -1: skip, 0: tree_a, 1: tree_b (use_self == false)
 * \param boolean_mode: -1: no-boolean, 0: intersection... see #BMESH_ISECT_BOOLEAN_ISECT.
 * \return true if the mesh is changed (intersections cut or faces removed from boolean).
 */
bool BM_mesh_boolean(BMesh *bm,
                     int (*test_fn)(BMFace *f, void *user_data),
                     void *user_data,
                     const bool use_self,
                     const bool UNUSED(use_separate),
                     const int boolean_mode,
                     const float eps)
{
  BoolState bs = {NULL};
  MeshPartSet all_parts, a_parts, b_parts;
  MeshChange meshchange;
#ifdef BOOLDEBUG
  int dbg_level = 2;
#endif

  init_imesh_from_bmesh(&bs.im, bm);
  bs.boolean_mode = boolean_mode;
  bs.eps = eps;
  bs.test_fn = (int (*)(void *, void *))test_fn;
  bs.test_fn_user_data = user_data;
  bs.mem_arena = BLI_memarena_new(MEM_SIZE_OPTIMAL(1 << 16), __func__);

#ifdef BOOLDEBUG
  if (dbg_level > 0) {
    printf("\n\nBOOLEAN, use_self=%d, boolean_mode=%d, eps=%g\n", use_self, boolean_mode, eps);
  }
#endif

  init_meshchange(&bs, &meshchange);

  if (use_self) {
    find_coplanar_parts(&bs, &all_parts, TEST_ALL, "all");
    intersect_partset_pair(&bs, &all_parts, &all_parts, &meshchange);
  }
  else {
    find_coplanar_parts(&bs, &a_parts, TEST_A, "A");
    find_coplanar_parts(&bs, &b_parts, TEST_B, "B");
    intersect_partset_pair(&bs, &a_parts, &b_parts, &meshchange);
  }

  if (dbg_level > 1) {
    dump_meshchange(&meshchange, "change for intersection");
  }

  apply_meshchange_to_imesh(&bs.im, &meshchange);

  if (boolean_mode != -1) {
    do_boolean_op(&bs, boolean_mode);
  }

  BLI_memarena_free(bs.mem_arena);
  return true;
}

#ifdef BOOLDEBUG
#  pragma mark Debug functions

ATTU static void dump_part(const MeshPart *part, const char *label)
{
  LinkNode *ln;
  int i;
  struct namelist {
    const char *name;
    LinkNode *list;
  } nl[3] = {{"verts", part->verts}, {"edges", part->edges}, {"faces", part->faces}};

  printf("part %s\n", label);
  for (i = 0; i < 3; i++) {
    if (nl[i].list) {
      printf("  %s:{", nl[i].name);
      for (ln = nl[i].list; ln; ln = ln->next) {
        printf("%d", POINTER_AS_INT(ln->link));
        if (ln->next) {
          printf(", ");
        }
      }
      printf("}\n");
    }
  }
  printf("  plane=(%.3f,%.3f,%.3f),%.3f:\n", F4(part->plane));
  printf("  bb=(%.3f,%.3f,%.3f)(%.3f,%.3f,%.3f)\n", F3(part->bbmin), F3(part->bbmax));
}

/** Boolean funcgions. */
#  pragma mark Boolean functions

/* Return the Generalized Winding Number of point co wih respect to the
 * volume implied by the faces for which bs->test_fn returns the value side.
 * See "Robust Inside-Outside Segmentation using Generalized Winding Numbers"
 * by Jacobson, Kavan, and Sorkine-Hornung.
 * This is like a winding number in that if it is positive, the point
 * is inside the volume. But it is tolerant of not-completely-watertight
 * volumes, still doing a passable job of classifying inside/outside
 * as we intuitively understand that to mean.
 * TOOD: speed up this calculation using the heirarchical algorithm in
 * that paper.
 */
static double generalized_winding_number(BoolState *bs, int side, const double co[3])
{
  double gwn, fgwn;
  int i, v1, v2, v3, f, totf, flen, tottri;
  IMesh *im = &bs->im;
  int(*index)[3];
  int index_buffer_len;
  double p1[3], p2[3], p3[3], a[3], b[3], c[3], bxc[3];
  double alen, blen, clen, num, denom, t, x;
#  ifdef BOOLDEBUG
  int dbg_level = 0;

  if (dbg_level > 0) {
    printf("generalized_winding_number, side=%d, co=(%f,%f,%f)\n", side, F3(co));
  }
#  endif

  /* Use the same buffer for all tesselations. Will increase size if necessary. */
#  ifdef BOOLDEBUG
  index_buffer_len = 3;
#  else
  index_buffer_len = 64;
#  endif
  index = MEM_mallocN((size_t)index_buffer_len * sizeof(index[0]), __func__);
  totf = imesh_totface(im);
  gwn = 0.0;
  for (f = 0; f < totf; f++) {
    if (imesh_test_face(im, bs->test_fn, bs->test_fn_user_data, f) != side) {
      continue;
    }
    flen = imesh_facelen(im, f);
    tottri = flen - 2;
    if (tottri > index_buffer_len) {
      index_buffer_len = tottri * 2;
      MEM_freeN(index);
      index = MEM_mallocN((size_t)index_buffer_len * sizeof(index[0]), __func__);
    }
    imesh_face_calc_tesselation(im, f, index);
    for (i = 0; i < tottri; i++) {
      v1 = imesh_face_vert(im, f, index[i][0]);
      imesh_get_vert_co_db(im, v1, p1);
      v2 = imesh_face_vert(im, f, index[i][1]);
      imesh_get_vert_co_db(im, v2, p2);
      v3 = imesh_face_vert(im, f, index[i][2]);
      imesh_get_vert_co_db(im, v3, p3);
#  ifdef BOOLDEBUG
      if (dbg_level > 1) {
        printf("face f%d tess tri %d is V=(%d,%d,%d)\n", f, i, v1, v2, v3);
      }
#  endif
      sub_v3_v3v3_db(a, p1, co);
      sub_v3_v3v3_db(b, p2, co);
      sub_v3_v3v3_db(c, p3, co);

      /* Calculate the solid angle of abc relative to origin.
       * Using Oosterom and Strackee formula. */
      alen = len_v3_db(a);
      blen = len_v3_db(b);
      clen = len_v3_db(c);
      cross_v3_v3v3_db(bxc, b, c);
      num = dot_v3v3_db(a, bxc);
      denom = alen * blen * clen + dot_v3v3_db(a, b) * clen + dot_v3v3_db(a, c) * blen +
              dot_v3v3_db(b, c) * alen;
      if (denom == 0.0) {
        denom = 10e-10;
      }
      t = num / denom;
      x = atan(t);
      if (false && x < 0.0) {
        x += M_PI;
      }
      fgwn = 2 * x;
#  ifdef BOOLDEBUG
      if (dbg_level > 1) {
        printf("face f%d tess tri %d contributes %f\n", f, i, fgwn);
      }
#  endif
      gwn += fgwn;
    }
  }
  gwn = gwn / (M_PI * 4.0);
#  ifdef BOOLDEBUG
  if (dbg_level > 0) {
    printf("gwn=%f\n\n", gwn);
  }
#  endif

  MEM_freeN(index);
  return gwn;
}

/* Return true if point co is inside the volume implied by the
 * faces for which bs->test_fn returns the value side.
 */
static bool point_is_inside_side(BoolState *bs, int side, const double co[3])
{
  double gwn;

  gwn = generalized_winding_number(bs, side, co);
  return (fabs(gwn) >= 0.5);
}

static void do_boolean_op(BoolState *bs, const int boolean_mode)
{
  int *groups_array;
  int(*group_index)[2];
  int group_tot, totface;
  double co[3];
  int i, f, fg, fg_end, side, otherside;
  bool do_remove, do_flip, inside;
  MeshChange meshchange;
#  ifdef BOOLDEBUG
  bool dbg_level = 0;

  if (dbg_level > 0) {
    printf("\nDO_BOOLEAN_OP, boolean_mode=%d\n\n", boolean_mode);
  }
#  endif

  init_meshchange(bs, &meshchange);
  meshchange.use_face_kill_loose = true;

  /* Partition faces into groups, where a group is a maximal set
   * of edge-connected faces on the same side (A vs B) of the boolean operand.
   */
  totface = imesh_totface(&bs->im);
  groups_array = BLI_memarena_alloc(bs->mem_arena, sizeof(*groups_array) * (size_t)totface);
  group_tot = imesh_calc_face_groups(bs, groups_array, &group_index);
#  ifdef BOOLDEBUG
  if (dbg_level > 0) {
    printf("Groups\n");
    for (i = 0; i < group_tot; i++) {
      printf("group %d:\n  ", i);
      fg = group_index[i][0];
      fg_end = fg + group_index[i][1];
      for (; fg != fg_end; fg++) {
        printf("%d ", groups_array[fg]);
      }
      printf("\n");
    }
  }
#  endif

  /* For each group, determine if it is inside or outside the part
   * on the other side, and remove and/or flip the normals of the
   * faces in the group according to the result and the boolean operation. */
  for (i = 0; i < group_tot; i++) {
    fg = group_index[i][0];
    fg_end = fg + group_index[i][1];

    /* Test if first face of group is inside. */
    f = groups_array[fg];
    side = imesh_test_face(&bs->im, bs->test_fn, bs->test_fn_user_data, f);
#  ifdef BOOLDEBUG
    if (dbg_level > 0) {
      printf("group %d side = %d\n", i, side);
    }
#  endif

    if (side == -1) {
      continue;
    }
    BLI_assert(ELEM(side, 0, 1));
    otherside = !side;

    imesh_calc_point_in_face(&bs->im, f, co);
#  ifdef BOOLDEBUG
    if (dbg_level > 0) {
      printf("face %d test co=(%f,%f,%f)\n", f, F3(co));
    }
#  endif

    inside = point_is_inside_side(bs, otherside, co);

    switch (boolean_mode) {
      case BMESH_BOOLEAN_ISECT:
        do_remove = !inside;
        do_flip = false;
        break;
      case BMESH_BOOLEAN_UNION:
        do_remove = inside;
        do_flip = false;
        break;
      case BMESH_BOOLEAN_DIFFERENCE:
        do_remove = (side == 0) ? inside : !inside;
        do_flip = (side == 1);
        break;
    }

#  ifdef BOOLDEBUG
    if (dbg_level > 0) {
      printf(
          "result for group %d: inside=%d, remove=%d, flip=%d\n\n", i, inside, do_remove, do_flip);
    }
#  endif

    if (do_remove || do_flip) {
      for (; fg != fg_end; fg++) {
        f = groups_array[fg];
        if (do_remove) {
          meshdelete_add_face(&meshchange.delete, f);
        }
        else if (do_flip) {
          add_to_intset(bs, &meshchange.face_flip, f);
        }
      }
    }
  }

#  ifdef BOOLDEBUG
  if (dbg_level > 0) {
    dump_meshchange(&meshchange, "after boolean op");
  }
#  endif

  apply_meshchange_to_imesh(&bs->im, &meshchange);
  MEM_freeN(group_index);
}

ATTU static void dump_partset(const MeshPartSet *partset)
{
  int i;
  MeshPart *part;
  char partlab[20];

  printf("partset %s\n", partset->label);
  for (i = 0; i < partset->tot_part; i++) {
    part = partset_part(partset, i);
    sprintf(partlab, "%d", i);
    if (!part) {
      printf("<NULL PART>\n");
    }
    else {
      dump_part(part, partlab);
    }
  }
  printf(
      "partset bb=(%.3f,%.3f,%.3f)(%.3f,%.3f,%.3f)\n\n", F3(partset->bbmin), F3(partset->bbmax));
}

ATTU static void dump_partpartintersect(const PartPartIntersect *ppi, const char *label)
{
  struct namelist {
    const char *name;
    LinkNode *list;
  } nl[3] = {{"verts", ppi->verts.list}, {"edges", ppi->edges.list}, {"faces", ppi->faces.list}};
  LinkNode *ln;
  int i;

  printf("partpartintersect %s parts a[%d] and b[%d]\n", label, ppi->a_index, ppi->b_index);

  for (i = 0; i < 3; i++) {
    if (nl[i].list) {
      printf("  %s:{", nl[i].name);
      for (ln = nl[i].list; ln; ln = ln->next) {
        printf("%d", POINTER_AS_INT(ln->link));
        if (ln->next) {
          printf(", ");
        }
      }
      printf("}\n");
    }
  }
}

ATTU static void dump_meshadd(const MeshAdd *ma, const char *label)
{
  LinkNode *ln;
  NewVert *nv;
  NewEdge *ne;
  NewFace *nf;
  int i, j;

  printf("meshadd %s\n", label);
  if (ma->verts.list) {
    printf("verts:\n");
    i = ma->vindex_start;
    for (ln = ma->verts.list; ln; ln = ln->next) {
      nv = (NewVert *)ln->link;
      printf("  %d: (%f,%f,%f) %d\n", i, F3(nv->co), nv->example);
      i++;
    }
  }
  if (ma->edges.list) {
    printf("edges:\n");
    i = ma->eindex_start;
    for (ln = ma->edges.list; ln; ln = ln->next) {
      ne = (NewEdge *)ln->link;
      printf("  %d: (%d,%d) %d\n", i, ne->v1, ne->v2, ne->example);
      i++;
    }
  }
  if (ma->faces.list) {
    printf("faces:\n");
    i = ma->findex_start;
    for (ln = ma->faces.list; ln; ln = ln->next) {
      nf = (NewFace *)ln->link;
      printf("  %d: face of length %d, example %d\n", i, nf->len, nf->example);
      if (nf->other_examples) {
        dump_intset(nf->other_examples, "other examples", "    ");
      }
      for (j = 0; j < nf->len; j++) {
        printf("(v=%d,e=%d)", nf->vert_edge_pairs[j].first, nf->vert_edge_pairs[j].second);
      }
      printf("\n");
      i++;
    }
  }
}

ATTU static void dump_bitmap(const BLI_bitmap *bmap, int tot)
{
  int i;

  for (i = 0; i < tot; i++) {
    if (BLI_BITMAP_TEST_BOOL(bmap, i)) {
      printf("%d ", i);
    }
  }
}

ATTU static void dump_meshdelete(const MeshDelete *meshdelete, const char *label)
{
  printf("MeshDelete %s\n", label);
  printf("verts: ");
  dump_bitmap(meshdelete->vert_bmap, meshdelete->totvert);
  printf("\nedges: ");
  dump_bitmap(meshdelete->edge_bmap, meshdelete->totedge);
  printf("\nfaces: ");
  dump_bitmap(meshdelete->face_bmap, meshdelete->totface);
  printf("\n");
}

ATTU static void dump_intintmap(const IntIntMap *map, const char *label, const char *prefix)
{
  IntIntMapIterator iter;

  printf("%sintintmap %s\n", prefix, label);
  for (intintmap_iter_init(&iter, map); !intintmap_iter_done(&iter); intintmap_iter_step(&iter)) {
    printf("%s  %d -> %d\n", prefix, intintmap_iter_key(&iter), intintmap_iter_value(&iter));
  }
}

ATTU static void dump_intset(const IntSet *set, const char *label, const char *prefix)
{
  LinkNode *ln;
  int v;

  printf("%sintset %s\n%s", prefix, label, prefix);
  for (ln = set->list; ln; ln = ln->next) {
    v = POINTER_AS_INT(ln->link);
    printf("%d ", v);
  }
  printf("\n");
}

ATTU static void dump_meshchange(const MeshChange *change, const char *label)
{
  printf("meshchange %s\n\n", label);
  dump_meshadd(&change->add, "add");
  printf("\n");
  dump_meshdelete(&change->delete, "delete");
  printf("\n");
  dump_intintmap(&change->vert_merge_map, "vert_merge_map", "");
  printf("\n");
  dump_intset(&change->intersection_edges, "intersection_edges", "");
  printf("\n");
  dump_intset(&change->face_flip, "face_flip", "");
  printf("\n");
}

ATTU static void dump_intlist_from_tables(const int *table,
                                          const int *start_table,
                                          const int *len_table,
                                          int index)
{
  int start, len, i;

  start = start_table[index];
  len = len_table[index];
  for (i = 0; i < len; i++) {
    printf("%d", table[start + i]);
    if (i < len - 1) {
      printf(" ");
    }
  }
}

ATTU static void dump_cdt_input(const CDT_input *cdt, const char *label)
{
  int i;

  printf("cdt input %s\n", label);
  printf("  verts\n");
  for (i = 0; i < cdt->verts_len; i++) {
    printf("  %d: (%.3f,%.3f)\n", i, F2(cdt->vert_coords[i]));
  }
  printf("  edges\n");
  for (i = 0; i < cdt->edges_len; i++) {
    printf("  %d: (%d,%d)\n", i, cdt->edges[i][0], cdt->edges[i][1]);
  }
  printf("  faces\n");
  for (i = 0; i < cdt->faces_len; i++) {
    printf("  %d: ", i);
    dump_intlist_from_tables(cdt->faces, cdt->faces_start_table, cdt->faces_len_table, i);
    printf("\n");
  }
}

ATTU static void dump_cdt_result(const CDT_result *cdt, const char *label, const char *prefix)
{
  int i;

  printf("%scdt result %s\n", prefix, label);
  printf("%s  verts\n", prefix);
  for (i = 0; i < cdt->verts_len; i++) {
    printf("%s  %d: (%.3f,%.3f) orig=[", prefix, i, F2(cdt->vert_coords[i]));
    dump_intlist_from_tables(
        cdt->verts_orig, cdt->verts_orig_start_table, cdt->verts_orig_len_table, i);
    printf("]\n");
  }
  printf("%s  edges\n", prefix);
  for (i = 0; i < cdt->edges_len; i++) {
    printf("%s  %d: (%d,%d) orig=[", prefix, i, cdt->edges[i][0], cdt->edges[i][1]);
    dump_intlist_from_tables(
        cdt->edges_orig, cdt->edges_orig_start_table, cdt->edges_orig_len_table, i);
    printf("]\n");
  }
  printf("%s  faces\n", prefix);
  for (i = 0; i < cdt->faces_len; i++) {
    printf("%s  %d: ", prefix, i);
    dump_intlist_from_tables(cdt->faces, cdt->faces_start_table, cdt->faces_len_table, i);
    printf(" orig=[");
    dump_intlist_from_tables(
        cdt->faces_orig, cdt->faces_orig_start_table, cdt->faces_orig_len_table, i);
    printf("]\n");
  }
}
#  define BMI(e) BM_elem_index_get(e)
#  define CO3(v) (v)->co[0], (v)->co[1], (v)->co[2]
static void dump_v(BMVert *v)
{
  printf("v%d[(%.3f,%.3f,%.3f)]@%p", BMI(v), CO3(v), v);
}

static void dump_e(BMEdge *e)
{
  printf("e%d[", BMI(e));
  dump_v(e->v1);
  printf(", ");
  dump_v(e->v2);
  printf("]@%p", e);
}

static void dump_f(BMFace *f)
{
  printf("f%d@%p", BMI(f), f);
}

static void dump_l(BMLoop *l)
{
  printf("l%d[", BMI(l));
  dump_v(l->v);
  printf(" ");
  dump_e(l->e);
  printf(" ");
  dump_f(l->f);
  printf("]@%p", l);
}

ATTU static void dump_bm(struct BMesh *bm, const char *msg)
{
  BMIter iter, iter2;
  BMVert *v;
  BMEdge *e;
  BMFace *f;
  BMLoop *l;

  printf("BMesh %s: %d verts, %d edges, %d loops, %d faces\n",
         msg,
         bm->totvert,
         bm->totedge,
         bm->totloop,
         bm->totface);

  printf("verts:\n");
  BM_ITER_MESH (v, &iter, bm, BM_VERTS_OF_MESH) {
    dump_v(v);
    printf(" %c", BM_elem_flag_test(v, BM_ELEM_SELECT) ? 's' : ' ');
    printf(" %c\n", BM_elem_flag_test(v, BM_ELEM_TAG) ? 't' : ' ');
  }
  printf("edges:\n");
  BM_ITER_MESH (e, &iter, bm, BM_EDGES_OF_MESH) {
    dump_e(e);
    printf(" %c", BM_elem_flag_test(e, BM_ELEM_SELECT) ? 's' : ' ');
    printf(" %c\n", BM_elem_flag_test(e, BM_ELEM_TAG) ? 't' : ' ');
  }
  printf("faces:\n");
  BM_ITER_MESH (f, &iter, bm, BM_FACES_OF_MESH) {
    dump_f(f);
    printf(" %c", BM_elem_flag_test(f, BM_ELEM_SELECT) ? 's' : ' ');
    printf(" %c\n", BM_elem_flag_test(f, BM_ELEM_TAG) ? 't' : ' ');
    printf(" 	loops:\n");
    BM_ITER_ELEM (l, &iter2, f, BM_LOOPS_OF_FACE) {
      printf(" 			");
      dump_l(l);
      printf(" ");
      printf(" %s\n", BM_elem_flag_test(l, (1 << 6)) ? "long" : "");
    }
  }
}
#endif
