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

#ifndef __BLI_DELAUNAY_2D_H__
#define __BLI_DELAUNAY_2D_H__

/** \file
 * \ingroup bli
 *
 *  This header file contains both a C interface and a C++ interface
 *  to the 2D Constrained Delaunay Triangulation library routine.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Interface for Constrained Delaunay Triangulation (CDT) in 2D.
 *
 * The input is a set of vertices, edges between those vertices,
 * and faces using those vertices.
 * Those inputs are called "constraints". The output must contain
 * those constraints, or at least edges, points, and vertices that
 * may be pieced together to form the constraints. Part of the
 * work of doing the CDT is to detect intersections and mergers
 * among the input elements, so these routines are also useful
 * for doing 2D intersection.
 *
 * The output is a triangulation of the plane that includes the
 * constraints in the above sense, and also satisfies the
 * "Delaunay condition" as modified to take into account that
 * the constraints must be there: for every non-constrained edge
 * in the output, there is a circle through the endpoints that
 * does not contain any of the vertices directly connected to
 * those endpoints. What this means in practice is that as
 * much as possible the triangles look "nice" -- not too long
 * and skinny.
 *
 * Optionally, the output can be a subset of the triangulation
 * (but still containing all of the constraints), to get the
 * effect of 2D intersection.
 *
 * The underlying method is incremental, but we need to know
 * beforehand a bounding box for all of the constraints.
 * This code can be extended in the future to allow for
 * deletion of constraints, if there is a use in Blender
 * for dynamically maintaining a triangulation.
 */

/**
 * Input to Constrained Delaunay Triangulation.
 * There are verts_len vertices, whose coordinates
 * are given by vert_coords. For the rest of the input,
 * vertices are referred to by indices into that array.
 * Edges and Faces are optional. If provided, they will
 * appear in the output triangulation ("constraints").
 * One can provide faces and not edges -- the edges
 * implied by the faces will be inferred.
 *
 * The edges are given by pairs of vertex indices.
 * The faces are given in a triple `(faces, faces_start_table, faces_len_table)`
 * to represent a list-of-lists as follows:
 * the vertex indices for a counterclockwise traversal of
 * face number `i` starts at `faces_start_table[i]` and has `faces_len_table[i]`
 * elements.
 *
 * The edges implied by the faces are automatically added
 * and need not be put in the edges array, which is intended
 * as a way to specify edges that are not part of any face.
 *
 * Some notes about some special cases and how they are handled:
 * - Input faces can have any number of vertices greater than 2. Depending
 *   on the output option, ngons may be triangulated or they may remain
 *   as ngons.
 * - Input faces may have repeated vertices. Output faces will not,
 *   except when the CDT_CONSTRAINTS output option is used.
 * - Input faces may have edges that self-intersect, but currently the labeling
 *   of which output faces have which input faces may not be done correctly,
 *   since the labeling relies on the inside being on the left of edges
 *   as one traverses the face. Output faces will not self-intersect.
 * - Input edges, including those implied by the input faces, may have
 *   zero-length or near-zero-length edges (nearness as determined by epsilon),
 *   but those edges will not be in the output.
 * - Input edges (including face edges) can overlap or nearly overlap each other.
 *   The output edges will not overlap, but instead be divided into as many
 *   edges as necessary to represent each overlap regime.
 * - Input vertices may be coincide with, or nearly coincide with (as determined
 *   by epsilon) other input vertices. Only one representative will survive
 *   in the output. If an input vertex is within epsilon of an edge (including
 *   an added triangulation edge), it will be snapped to that edge, so the
 *   output coordinates may not exactly match the input coordinates in all cases.
 * - Wire edges (those not part of faces) and isolated vertices are allowed in
 *   the input. If they are inside faces, they will be incorporated into the
 *   triangulation of those faces.
 *
 * Epsilon is used for "is it near enough" distance calculations.
 * If zero is supplied for epsilon, an internal value of 1e-8 used
 * instead, since this code will not work correctly if it is not allowed
 * to merge "too near" vertices.
 */
typedef struct CDT_input {
  int verts_len;
  int edges_len;
  int faces_len;
  float (*vert_coords)[2];
  int (*edges)[2];
  int *faces;
  int *faces_start_table;
  int *faces_len_table;
  float epsilon;
} CDT_input;

/**
 * A representation of the triangulation for output.
 * See #CDT_input for the representation of the output
 * vertices, edges, and faces, all represented in
 * a similar way to the input.
 *
 * The output may have merged some input vertices together,
 * if they were closer than some epsilon distance.
 * The output edges may be overlapping sub-segments of some
 * input edges; or they may be new edges for the triangulation.
 * The output faces may be pieces of some input faces, or they
 * may be new.
 *
 * In the same way that faces lists-of-lists were represented by
 * a run-together array and a "start" and "len" extra array,
 * similar triples are used to represent the output to input
 * mapping of vertices, edges, and faces.
 *
 * Those triples are:
 * - verts_orig, verts_orig_start_table, verts_orig_len_table
 * - edges_orig, edges_orig_start_table, edges_orig_len_table
 * - faces_orig, faces_orig_start_table, faces_orig_len_table
 *
 * For edges, the edges_orig triple can also say which original face
 * edge is part of a given output edge. If an index in edges_orig
 * is greater than the input's edges_len, then subtract input's edges_len
 * from it to some number i: then the face edge that starts from the
 * input vertex at input's faces[i] is the corresponding face edge.
 * for convenience, face_edge_offset in the result will be the input's
 * edges_len, so that this conversion can be easily done by the caller.
 */
typedef struct CDT_result {
  int verts_len;
  int edges_len;
  int faces_len;
  int face_edge_offset;
  float (*vert_coords)[2];
  int (*edges)[2];
  int *faces;
  int *faces_start_table;
  int *faces_len_table;
  int *verts_orig;
  int *verts_orig_start_table;
  int *verts_orig_len_table;
  int *edges_orig;
  int *edges_orig_start_table;
  int *edges_orig_len_table;
  int *faces_orig;
  int *faces_orig_start_table;
  int *faces_orig_len_table;
} CDT_result;

/** What triangles and edges of CDT are desired when getting output? */
typedef enum CDT_output_type {
  /** All triangles, outer boundary is convex hull. */
  CDT_FULL,
  /** All triangles fully enclosed by constraint edges or faces. */
  CDT_INSIDE,
  /**  Only point, edge, and face constraints, and their intersections. */
  CDT_CONSTRAINTS,
  /**
   * Like CDT_CONSTRAINTS, but keep enough
   * edges so that any output faces that came from input faces can be made as valid
   * #BMesh faces in Blender: that is,
   * no vertex appears more than once and no isolated holes in faces.
   */
  CDT_CONSTRAINTS_VALID_BMESH
} CDT_output_type;

/**
 * API interface to CDT.
 * This returns a pointer to an allocated CDT_result.
 * When the caller is finished with it, the caller
 * should use #BLI_delaunay_2d_cdt_free() to free it.
 */
CDT_result *BLI_delaunay_2d_cdt_calc(const CDT_input *input, const CDT_output_type output_type);

void BLI_delaunay_2d_cdt_free(CDT_result *result);

#ifdef __cplusplus
}

/* C++ Interface. */

#  include "gmpxx.h"

#  include "BLI_array.hh"
#  include "BLI_double2.hh"
#  include "BLI_linklist.h"
#  include "BLI_mempool.h"
#  include "BLI_mpq2.hh"
#  include "BLI_vector.hh"

namespace blender {
namespace meshintersect {

/* vec2<Arith_t> is a 2d vector with Arith_t as the type for coordinates. */
template<typename Arith_t> struct vec2_impl;
template<> struct vec2_impl<double> {
  typedef double2 type;
};
template<> struct vec2_impl<mpq_class> {
  typedef mpq2 type;
};
template<typename Arith_t> using vec2 = typename vec2_impl<Arith_t>::type;

/* Define a templated 2D arrangment of vertices, edges, and faces.
 * The SymEdge data structure is the basis for a structure that allows
 * easy traversal to neighboring (by toplogy) geometric elements.
 * Each of CDTVert, CDTEdge, and CDTFace have an input_id linked list,
 * whose nodes contain integers that keep track of which input verts, edges,
 * and faces, respectively, that the element was derived from.
 *
 * While this could be cleaned up some, it is usable by other routines in Blender
 * that need to keep track of a 2D arrangement, with topology.
 */
template<typename Arith_t> class CDTVert;
template<typename Arith_t> class CDTEdge;
template<typename Arith_t> class CDTFace;

template<typename Arith_t> class SymEdge {
 public:
  SymEdge<Arith_t> *next{nullptr}; /* In face, doing CCW traversal of face. */
  SymEdge<Arith_t> *rot{nullptr};  /* CCW around vert. */
  CDTVert<Arith_t> *vert{nullptr}; /* Vert at origin. */
  CDTEdge<Arith_t> *edge{nullptr}; /* Undirected edge this is for. */
  CDTFace<Arith_t> *face{nullptr}; /* Face on left side. */

  SymEdge() = default;
};

/* Return other SymEdge for same CDTEdge as se. */
template<typename T> inline SymEdge<T> *sym(const SymEdge<T> *se)
{
  return se->next->rot;
}

/* Return SymEdge whose next is se. */
template<typename T> inline SymEdge<T> *prev(const SymEdge<T> *se)
{
  return se->rot->next->rot;
}

template<typename Arith_t> class CDTVert {
 public:
  vec2<Arith_t> co;                   /* Coordinate. */
  SymEdge<Arith_t> *symedge{nullptr}; /* Some edge attached to it. */
  LinkNode *input_ids{nullptr};       /* List of corresponding vertex input ids. */
  int index{-1};                      /* Index into array that CDTArrangement keeps. */
  int merge_to_index{-1}; /* Index of a CDTVert that this has merged to. -1 if no merge. */
  int visit_index{0};     /* Used by algorithms operating on CDT structures. */

  CDTVert() = default;
  explicit CDTVert(const vec2<Arith_t> &pt);
};

template<typename Arith_t> class CDTEdge {
 public:
  LinkNode *input_ids{nullptr}; /* List of input edge ids that this is part of. */
  SymEdge<Arith_t> symedges[2]{SymEdge<Arith_t>(),
                               SymEdge<Arith_t>()}; /* The directed edges for this edge. */

  CDTEdge() = default;
};

template<typename Arith_t> class CDTFace {
 public:
  SymEdge<Arith_t> *symedge{
      nullptr}; /* A symedge in face; only used during output, so only valid then. */
  LinkNode *input_ids{nullptr}; /* List of input face ids that this is part of. */
  int visit_index{0};           /* Used by algorithms operating on CDT structures. */
  bool deleted{false};          /* Marks this face no longer used. */

  CDTFace() = default;
};

template<typename Arith_t> class CDTArrangement {
 public:
  /* The arrangement owns the memory pointed to by the pointers in these vectors.
   * They are pointers instead of actual structures because these vectors may be resized and
   * other elements refer to the elements by pointer.
   */
  Vector<CDTVert<Arith_t> *>
      verts; /* The verts. Some may be merged to others (see their merge_to_index). */
  Vector<CDTEdge<Arith_t> *>
      edges; /* The edges. Some may be deleted (SymEdge next and rot pointers are null). */
  Vector<CDTFace<Arith_t> *> faces; /* The faces. Some may be deleted (see their delete member). */
  CDTFace<Arith_t> *outer_face{nullptr}; /* Which CDTFace is the outer face. */

  CDTArrangement() = default;
  ~CDTArrangement();

  /* Hint to how much space to reserve in the Vectors of the arrangement, based on these counts of
   * input elements. */
  void reserve(int num_verts, int num_edges, int num_faces);

  /* Add a new vertex to the arrangement, with the given 2D coordinate. It will not be connected to
   * anything yet. */
  CDTVert<Arith_t> *add_vert(const vec2<Arith_t> &pt);

  /* Add an edge from v1 to v2. The edge will have a left face and a right face, specified by fleft
   * and fright. The edge will not be connected to anything yet. If the vertices do not yet have a
   * symedge pointer, their pointer is set to the symedge in this new edge.
   */
  CDTEdge<Arith_t> *add_edge(CDTVert<Arith_t> *v1,
                             CDTVert<Arith_t> *v2,
                             CDTFace<Arith_t> *fleft,
                             CDTFace<Arith_t> *fright);

  /* Add a new face. It is disconnected until an add_edge makes it the left or right face of an
   * edge. */
  CDTFace<Arith_t> *add_face();

  /* Make a new edge from v to se->vert, splicing it in. */
  CDTEdge<Arith_t> *add_vert_to_symedge_edge(CDTVert<Arith_t> *v, SymEdge<Arith_t> *se);

  /* Assuming s1 and s2 are both SymEdges in a face with > 3 sides and one is not the next of the
   * other, Add an edge from s1->v to s2->v, splitting the face in two. The original face will be
   * the one that s1 has as left face, and a new face will be added and made s2 and its
   * next-cycle's left face.
   */
  CDTEdge<Arith_t> *add_diagonal(SymEdge<Arith_t> *s1, SymEdge<Arith_t> *s2);

  /* Connect the verts of se1 and se2, assuming that currently those two SymEdges are on teh outer
   * boundary (have face == outer_face) of two components that are isolated from each other.
   */
  CDTEdge<Arith_t> *connect_separate_parts(SymEdge<Arith_t> *se1, SymEdge<Arith_t> *se2);

  /* Split se at fraction lambda, and return the new CDTEdge that is the new second half.
   * Copy the edge input_ids into the new one.
   */
  CDTEdge<Arith_t> *split_edge(SymEdge<Arith_t> *se, Arith_t lambda);

  /* Delete an edge. The new combined face on either side of the deleted edge will be the one that
   * was e's face. There will now be an unused face, which will be marked deleted, and an unused
   * CDTEdge, marked by setting the next and rot pointers of its SymEdges to nullptr.
   */
  void delete_edge(SymEdge<Arith_t> *se);

  /* If the vertex with index i in the vert array has not been merge, return it.
   * Else return the one that it has merged to. */
  CDTVert<Arith_t> *get_vert_resolve_merge(int i)
  {
    CDTVert<Arith_t> *v = this->verts[i];
    if (v->merge_to_index != -1) {
      v = this->verts[v->merge_to_index];
    }
    return v;
  }
};

template<typename Arith_t> class CDT_input {
 public:
  Array<vec2<Arith_t>> vert;
  Array<std::pair<int, int>> edge;
  Array<Vector<int>> face;
  Arith_t epsilon{0};
};

template<typename Arith_t> class CDT_result {
 public:
  Array<vec2<Arith_t>> vert;
  Array<std::pair<int, int>> edge;
  Array<Vector<int>> face;
  Array<Vector<int>> vert_orig;
  Array<Vector<int>> edge_orig;
  Array<Vector<int>> face_orig;
  int face_edge_offset;
};

CDT_result<double> delaunay_2d_calc(const CDT_input<double> &input, CDT_output_type output_type);

CDT_result<mpq_class> delaunay_2d_calc(const CDT_input<mpq_class> &input,
                                       CDT_output_type output_type);

} /* namespace meshintersect */
} /* namespace blender */

#endif /* __cplusplus */

#endif /* __BLI_DELAUNAY_2D_H__ */
