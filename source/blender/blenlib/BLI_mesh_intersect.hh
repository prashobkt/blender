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

#pragma once

/** \file
 * \ingroup bli
 *
 *  Mesh intersection library functions.
 *  Uses exact arithmetic, so need GMP.
 */

#ifdef WITH_GMP

#  include <iostream>

#  include "BLI_array.hh"
#  include "BLI_double3.hh"
#  include "BLI_index_range.hh"
#  include "BLI_map.hh"
#  include "BLI_math_mpq.hh"
#  include "BLI_mpq3.hh"
#  include "BLI_span.hh"
#  include "BLI_vector.hh"

namespace blender::meshintersect {

constexpr int NO_INDEX = -1;

/* Vertex coordinates are stored both as double3 and mpq3, which should agree.
 * Most calculations are done in exact arithmetic, using the mpq3 version,
 * but some predicates can be sped up by operating on doubles and using error analysis
 * to find the cases where that is good enough.
 * Vertices also carry along an id, created on allocation. The id
 * is useful for making algorithms that don't depend on pointers.
 * Also, they are easier to read while debugging.
 * They also carry an orig index, which can be used to tie them back to
 * vertices that tha caller may have in a different way (e.g., BMVerts).
 * An orig index can be NO_INDEX, indicating the Vert was created by
 * the algorithm and doesn't match an original Vert.
 * Vertices can be reliably compared for equality,
 * and hashed (on their co_exact field).
 */
struct Vert {
  mpq3 co_exact;
  double3 co;
  int id = NO_INDEX;
  int orig = NO_INDEX;

  Vert() = default;
  Vert(const mpq3 &mco, const double3 &dco, int id, int orig);
  Vert(const Vert &other);
  Vert(Vert &&other) noexcept;

  ~Vert() = default;

  Vert &operator=(const Vert &other);
  Vert &operator=(Vert &&other) noexcept;

  /* Test equality on the co_exact field. */
  bool operator==(const Vert &other) const;

  /* Hash on the co_exact field. */
  uint64_t hash() const;
};

/* Use Vertp for Verts everywhere: can modify the pointer
 * but not the underlying Vert, which should stay constant
 * after creation.
 */
using Vertp = const Vert *;

std::ostream &operator<<(std::ostream &os, Vertp v);

/* A Plane whose equation is dot(nprm, p) + d = 0. */
struct Plane {
  mpq3 norm_exact;
  mpq_class d_exact;
  double3 norm;
  double d;

  Plane() = default;
  Plane(const mpq3 &norm_exact, const mpq_class &d_exact);

  /* Test equality on the exact fields. */
  bool operator==(const Plane &other) const;

  /* Hash onthe exact fields. */
  uint64_t hash() const;

  void make_canonical();
};

std::ostream &operator<<(std::ostream &os, const Plane &plane);

/* A Face has a sequence of Verts that for a CCW ordering around them.
 * Faces carry an index, created at allocation time, useful for making
 * pointer-indenpendent algorithms, and for debugging.
 * They also carry an original index, meaningful to the caller.
 * And they carry original edge indices too: each is a number meaningful
 * to the caller for the edge starting from the corresponding face position.
 * A "face position" is the index of a vertex around a face.
 * Faces don't own the memory pointed at by the vert array.
 * Also indexed by face position, the is_intersect array says
 * for each edge whether or not it is the result of intersecting
 * with another face in the intersect algorithm.
 * Since the intersect algorithm needs the plane for each face,
 * a Face also stores the Plane of the face.
 */
struct Face {
  Array<Vertp> vert;
  Array<int> edge_orig;
  Array<bool> is_intersect;
  Plane plane;
  int id = NO_INDEX;
  int orig = NO_INDEX;

  using FacePos = int;

  Face() = default;
  Face(Span<Vertp> verts, int id, int orig, Span<int> edge_origs, Span<bool> is_intersect);
  Face(Span<Vertp> verts, int id, int orig);
  Face(const Face &other);
  Face(Face &&other) noexcept;

  ~Face() = default;

  Face &operator=(const Face &other);
  Face &operator=(Face &&other) noexcept;

  bool is_tri() const
  {
    return vert.size() == 3;
  }

  /* Test equality of verts, in same positions. */
  bool operator==(const Face &other) const;

  /* Test equaliy faces allowing cyclic shifts. */
  bool cyclic_equal(const Face &other) const;

  FacePos next_pos(FacePos p) const
  {
    return (p + 1) % vert.size();
  }

  FacePos prev_pos(FacePos p) const
  {
    return (p + vert.size() - 1) % vert.size();
  }

  const Vertp &operator[](int index) const
  {
    return vert[index];
  }

  int size() const
  {
    return vert.size();
  }

  const Vertp *begin() const
  {
    return vert.begin();
  }

  const Vertp *end() const
  {
    return vert.end();
  }

  IndexRange index_range() const
  {
    return IndexRange(vert.size());
  }
};

using Facep = const Face *;

std::ostream &operator<<(std::ostream &os, Facep f);

/* MArena is the owner of the Vert and Face resources used
 * during a run of one of the meshintersect main functions.
 * It also keeps has a hash table of all Verts created so that it can
 * ensure that only one instance of a Vert with a given co_exact will
 * exist. I.e., it dedups the vertices.
 */
class MArena {
  class MArenaImpl;
  std::unique_ptr<MArenaImpl> pimpl_;

 public:
  MArena();
  MArena(const MArena &) = delete;
  MArena(MArena &&) = delete;

  ~MArena();

  MArena &operator=(const MArena &) = delete;
  MArena &operator=(MArena &&) = delete;

  /* Provide hints to number of expected Verts and Faces expected
   * to be allocated.
   */
  void reserve(int vert_num_hint, int face_num_hint);

  int tot_allocated_verts() const;
  int tot_allocated_faces() const;

  /* These add routines find and return an existing Vert with the same
   * co_exact, if it exists (the orig argument is ignored in this case),
   * or else allocates and returns a new one. The index field of a
   * newly allocated Vert will be the index in creation order.
   */
  Vertp add_or_find_vert(const mpq3 &co, int orig);
  Vertp add_or_find_vert(const double3 &co, int orig);

  Facep add_face(Span<Vertp> verts, int orig, Span<int> edge_origs, Span<bool> is_intersect);
  Facep add_face(Span<Vertp> verts, int orig, Span<int> edge_origs);
  Facep add_face(Span<Vertp> verts, int orig);

  /* The following return nullptr if not found. */
  Vertp find_vert(const mpq3 &co) const;
  Facep find_face(Span<Vertp> verts) const;
};

/* A blender::meshintersect::Mesh is a self-contained mesh structure
 * that can be used in Blenlib without depending on the rest of Blender.
 * The Vert and Face resources used in the Mesh should be owned by
 * some MArena.
 * The Verts used by a Mesh can be recovered from the Faces, so
 * are usually not stored, but on request, the Mesh can populate
 * internal structures for indexing exactly the set of needed Verts,
 * and also going from a Vert pointer to the index in that system.
 */

class Mesh {
  Array<Facep> face_;
  Array<Vertp> vert_;             /* Only valid if vert_populated_. */
  Map<Vertp, int> vert_to_index_; /* Only valid if vert_populated_. */
  bool vert_populated_ = false;

 public:
  Mesh() = default;
  Mesh(Span<Facep> faces) : face_(faces)
  {
  }

  void set_faces(Span<Facep> faces);
  Facep face(int index) const
  {
    return face_[index];
  }

  int face_size() const
  {
    return face_.size();
  }

  int vert_size() const
  {
    return vert_.size();
  }

  bool has_verts() const
  {
    return vert_populated_;
  }

  void set_dirty_verts()
  {
    vert_populated_ = false;
    vert_to_index_.clear();
    vert_ = Array<Vertp>();
  }

  /* Use the second of these if there is a good bound
   * estimate on the maximum number of verts.
   */
  void populate_vert();
  void populate_vert(int max_verts);

  Vertp vert(int index) const
  {
    BLI_assert(vert_populated_);
    return vert_[index];
  }

  /* Returns index in vert_ where v is, or NO_INDEX. */
  int lookup_vert(Vertp v) const;

  IndexRange vert_index_range() const
  {
    BLI_assert(vert_populated_);
    return IndexRange(vert_.size());
  }

  IndexRange face_index_range() const
  {
    return IndexRange(face_.size());
  }

  Span<Vertp> vertices() const
  {
    BLI_assert(vert_populated_);
    return Span<Vertp>(vert_);
  }

  Span<Facep> faces() const
  {
    return Span<Facep>(face_);
  }

  /* Replace face at given index with one that elides the
   * vertices at the positions in face_pos_erase that are true.
   * Use arena to allocate the new face in.
   */
  void erase_face_positions(int f_index, Span<bool> face_pos_erase, MArena *arena);
};

std::ostream &operator<<(std::ostream &os, const Mesh &mesh);

/* The output will have dup vertices merged and degenerate triangles ignored.
 * If the input has overlapping coplanar triangles, then there will be
 * as many duplicates as there are overlaps in each overlapping triangular region.
 * The orig field of each IndexedTriangle will give the orig index in the input TriMesh
 * that the output triangle was a part of (input can have -1 for that field and then
 * the index in tri[] will be used as the original index).
 * The orig structure of the output TriMesh gives the originals for vertices and edges.
 * Note: if the input tm_in has a non-empty orig structure, then it is ignored.
 */
Mesh trimesh_self_intersect(const Mesh &tm_in, MArena *arena);

Mesh trimesh_nary_intersect(const Mesh &tm_in,
                            int nshapes,
                            std::function<int(int)> shape_fn,
                            bool use_self,
                            MArena *arena);

/* This has the side effect of populating verts in the Mesh. */
void write_obj_mesh(Mesh &m, const std::string &objname);

} /* namespace blender::meshintersect */

#endif /* WITH_GMP */
