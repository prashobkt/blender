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

#include <fstream>
#include <iostream>

#include "BLI_allocator.hh"
#include "BLI_array.hh"
#include "BLI_array_ref.hh"
#include "BLI_assert.h"
#include "BLI_delaunay_2d.h"
#include "BLI_double3.hh"
#include "BLI_hash.hh"
#include "BLI_map.hh"
#include "BLI_mpq2.hh"
#include "BLI_mpq3.hh"
#include "BLI_vector.hh"
#include "BLI_vector_set.hh"

#include "BLI_mesh_intersect.hh"

namespace BLI {
namespace MeshIntersect {

/* A plane whose equation is dot(n, p) + d = 0. */
struct planeq {
  mpq3 n;
  mpq_class d;

  planeq() = default;

  planeq(mpq3 n, mpq_class d) : n(n), d(d)
  {
  }

  operator const mpq_class *() const
  {
    return &n.x;
  }

  operator mpq_class *()
  {
    return &n.x;
  }
};

/* A Triangle mesh but with a different structure to hold vertices, so that
 * we can dedup them efficiently.
 * Also optionally keeps the planeqs of the triangles, as those are needed sometimes.
 */
class TMesh {
 public:
  TMesh() : m_has_planes(false)
  {
  }

  /* Copies verts and triangles from tm_in, but dedups the vertices
   * and ignores degenerate and invalid triangles.
   * If want_planes is true, calculate and store the planes for each triangle.
   */
  TMesh(const TriMesh &tm_in, bool want_planes)
  {
    int nvert = static_cast<int>(tm_in.vert.size());
    this->m_verts.reserve(nvert);
    Array<int> input_v_to_tm_v = Array<int>(nvert); /* Input vert index -> our vert index. */
    for (int v = 0; v < nvert; ++v) {
      const mpq3 &co = tm_in.vert[v];
      int tmv = this->add_vert(co);
      input_v_to_tm_v[v] = tmv;
    }
    int ntri = static_cast<int>(tm_in.tri.size());
    this->m_tris.reserve(ntri);
    for (int t = 0; t < ntri; ++t) {
      const IndexedTriangle &tri = tm_in.tri[t];
      int v0 = tri.v0();
      int v1 = tri.v1();
      int v2 = tri.v2();
      int orig = tri.orig();
      if (orig == -1) {
        orig = t;
      }
      if (v0 == v1 || v0 == v2 || v1 == v2 || v0 < 0 || v0 >= nvert || v1 < 0 || v1 >= nvert ||
          v2 < 0 || v2 >= nvert) {
        /* Skip degenerate triangle and ones with invalid indices. */
        /* TODO: test for collinear v0, v1, v2 and skip if so, too. */
        continue;
      }
      int tmv0 = input_v_to_tm_v[v0];
      int tmv1 = input_v_to_tm_v[v1];
      int tmv2 = input_v_to_tm_v[v2];
      this->m_tris.append(IndexedTriangle(tmv0, tmv1, tmv2, orig));
    }
    if (want_planes) {
      this->init_planes();
    }
    else {
      m_has_planes = false;
    }
  }

  /* Copy a single triangle from the source TMesh. */
  TMesh(const TMesh &source_tm, int t)
  {
    BLI_assert(t >= 0 && t < source_tm.tot_tri());
    this->m_verts.reserve(3);
    const IndexedTriangle &source_it = source_tm.tri(t);
    int tmv0 = this->add_vert(source_tm.vert(source_it.v0()));
    int tmv1 = this->add_vert(source_tm.vert(source_it.v1()));
    int tmv2 = this->add_vert(source_tm.vert(source_it.v2()));
    this->m_tris.append(IndexedTriangle(tmv0, tmv1, tmv2, source_it.orig()));
    if (source_tm.m_has_planes) {
      this->m_planes.append(source_tm.m_planes[t]);
      this->m_has_planes = true;
    }
    else {
      this->m_has_planes = false;
    }
  }

  /* TODO: make sure assignment, copy, init, move init, and move assignment all work as expected.
   */

  void init_planes()
  {
    int ntri = this->tot_tri();
    m_planes.reserve(ntri);
    for (int t = 0; t < ntri; ++t) {
      IndexedTriangle &tri = m_tris[t];
      mpq3 tr02 = m_verts[tri[0]] - m_verts[tri[2]];
      mpq3 tr12 = m_verts[tri[1]] - m_verts[tri[2]];
      mpq3 n = mpq3::cross(tr02, tr12);
      mpq_class d = -mpq3::dot(n, m_verts[tri[0]]);
      this->m_planes.append(planeq(n, d));
    }
    m_has_planes = true;
  }

  int tot_vert() const
  {
    return static_cast<int>(m_verts.size());
  }

  int tot_tri() const
  {
    return static_cast<int>(m_tris.size());
  }

  bool has_planes() const
  {
    return m_has_planes;
  }

  const IndexedTriangle &tri(int index) const
  {
    return m_tris[index];
  }

  const mpq3 &vert(int index) const
  {
    return m_verts[index];
  }

  const planeq &tri_plane(int index) const
  {
    BLI_assert(m_has_planes);
    return m_planes[index];
  }

  int add_tri(int v0, int v1, int v2, int tri_orig)
  {
    int t = this->tot_vert();
    this->m_tris.append(IndexedTriangle(v0, v1, v2, tri_orig));
    return t;
  }

  int add_tri(const IndexedTriangle &itri)
  {
    int t = this->tot_vert();
    this->m_tris.append(itri);
    return t;
  }

  int add_vert(const mpq3 &co)
  {
    int co_index = this->m_verts.index_try(co);
    if (co_index == -1) {
      co_index = static_cast<int>(this->m_verts.size());
      this->m_verts.add_new(co);
    }
    return co_index;
  }

 private:
  /* Invariants:
   * m_verts contains no duplicates.
   * Every index t in an m_tris element satisfies
   *   0 <= t < m_verts.size()
   * No degenerate triangles.
   * If m_has_planes is true then m_planes parallels
   * the m_tris vector, and has the corresponding planes.
   * (The init_planes() function will set that up.)
   */
  bool m_has_planes;
  VectorSet<mpq3> m_verts;
  Vector<IndexedTriangle> m_tris;
  Vector<planeq> m_planes;
};

static std::ostream &operator<<(std::ostream &os, const TMesh &tm);

/* A cluster of coplanar triangles, by index.
 * A pair of triangles T0 and T1 is said to "nontrivially coplanar-intersect"
 * if they are coplanar, intersect, and their intersection is not just existing
 * elements (verts, edges) of both triangles.
 * A coplanar cluster is said to be "nontrivial" if it has more than one triangle
 * and every triangle in it nontrivially coplanar-intersects with at least one other
 * triangle in the cluster.
 */
class CoplanarCluster {
 public:
  CoplanarCluster() = default;
  explicit CoplanarCluster(int t)
  {
    this->add_tri(t);
  }
  ~CoplanarCluster() = default;
  /* Assume that caller knows this will not be a duplicate. */
  void add_tri(int t)
  {
    m_tris.append(t);
  }
  int tot_tri() const
  {
    return static_cast<int>(m_tris.size());
  }
  int tri(int index) const
  {
    return m_tris[index];
  }
  const int *begin() const
  {
    return m_tris.begin();
  }
  const int *end() const
  {
    return m_tris.end();
  }

 private:
  Vector<int> m_tris;
};

/* Maintains indexed set of CoplanarCluster, with the added ability
 * to efficiently find the cluster index of any given triangle
 * (the max triangle index needs to be given in the initializer).
 * The tri_cluster(t) function returns -1 if t is not part of any cluster.
 */
class CoplanarClusterInfo {
 public:
  CoplanarClusterInfo() = default;
  explicit CoplanarClusterInfo(int numtri) : m_tri_cluster(Array<int>(numtri))
  {
    m_tri_cluster.fill(-1);
  }
  const int tri_cluster(int t) const
  {
    BLI_assert(0 <= t && t < static_cast<int>(m_tri_cluster.size()));
    return m_tri_cluster[t];
  }
  int add_cluster(const CoplanarCluster cl)
  {
    int c_index = static_cast<int>(m_clusters.append_and_get_index(cl));
    for (const int t : cl) {
      BLI_assert(0 <= t && t < static_cast<int>(m_tri_cluster.size()));
      m_tri_cluster[t] = c_index;
    }
    return c_index;
  }
  int tot_cluster() const
  {
    return static_cast<int>(m_clusters.size());
  }
  const CoplanarCluster &cluster(int index) const
  {
    BLI_assert(0 <= index && index < static_cast<int>(m_clusters.size()));
    return m_clusters[index];
  }

 private:
  Vector<CoplanarCluster> m_clusters;
  Array<int> m_tri_cluster;
};

static std::ostream &operator<<(std::ostream &os, const CoplanarCluster &cl);

static std::ostream &operator<<(std::ostream &os, const CoplanarClusterInfo &clinfo);

enum ITT_value_kind { INONE, IPOINT, ISEGMENT, ICOPLANAR };

class ITT_value {
 public:
  enum ITT_value_kind kind;
  mpq3 p1;      /* Only relevant for IPOINT and ISEGMENT kind. */
  mpq3 p2;      /* Only relevant for ISEGMENT kind. */
  int t_source; /* Index of the source triangle that intersected the target one. */

  ITT_value() : kind(INONE), t_source(-1)
  {
  }
  ITT_value(ITT_value_kind k) : kind(k), t_source(-1)
  {
  }
  ITT_value(ITT_value_kind k, int tsrc) : kind(k), t_source(tsrc)
  {
  }
  ITT_value(ITT_value_kind k, const mpq3 &p1) : kind(k), p1(p1), t_source(-1)
  {
  }
  ITT_value(ITT_value_kind k, const mpq3 &p1, const mpq3 &p2)
      : kind(k), p1(p1), p2(p2), t_source(-1)
  {
  }
  ~ITT_value()
  {
  }
};

static std::ostream &operator<<(std::ostream &os, const ITT_value &itt);

/*
 * interesect_tri_tri and helper functions.
 * This code uses the algorithm of Guigue and Devillers, as described
 * in "Faster Triangle-Triangle Intersection Tests".
 * Adapted from github code by Eric Haines:
 * github.com/erich666/jgt-code/tree/master/Volume_08/Number_1/Guigue2003
 */

/* Helper function for intersect_tri_tri. Args have been fully canonicalized
 * and we can construct the segment of intersection (triangles not coplanar).
 */

static ITT_value itt_canon2(const mpq3 &p1,
                            const mpq3 &q1,
                            const mpq3 &r1,
                            const mpq3 &p2,
                            const mpq3 &q2,
                            const mpq3 &r2,
                            const mpq3 &n1,
                            const mpq3 &n2)
{
  constexpr int dbg_level = 0;
  mpq3 source;
  mpq3 target;
  mpq_class alpha;
  bool ans_ok = false;

  mpq3 v1 = q1 - p1;
  mpq3 v2 = r2 - p1;
  mpq3 n = mpq3::cross(v1, v2);
  mpq3 v = p2 - p1;
  if (dbg_level > 1) {
    std::cout << "itt_canon2:\n";
    std::cout << "p1=" << p1 << " q1=" << q1 << " r1=" << r1 << "\n";
    std::cout << "p2=" << p2 << " q2=" << q2 << " r2=" << r2 << "\n";
    std::cout << "v=" << v << " n=" << n << "\n";
  }
  if (mpq3::dot(v, n) > 0) {
    v1 = r1 - p1;
    n = mpq3::cross(v1, v2);
    if (dbg_level > 1) {
      std::cout << "case 1: v1=" << v1 << " v2=" << v2 << " n=" << n << "\n";
    }
    if (mpq3::dot(v, n) <= 0) {
      v2 = q2 - p1;
      n = mpq3::cross(v1, v2);
      if (dbg_level > 1) {
        std::cout << "case 1a: v2=" << v2 << " n=" << n << "\n";
      }
      if (mpq3::dot(v, n) > 0) {
        v1 = p1 - p2;
        v2 = p1 - r1;
        alpha = mpq3::dot(v1, n2) / mpq3::dot(v2, n2);
        v1 = v2 * alpha;
        source = p1 - v1;
        v1 = p2 - p1;
        v2 = p2 - r2;
        alpha = mpq3::dot(v1, n1) / mpq3::dot(v2, n1);
        v1 = v2 * alpha;
        target = p2 - v1;
        ans_ok = true;
      }
      else {
        v1 = p2 - p1;
        v2 = p2 - q2;
        alpha = mpq3::dot(v1, n1) / mpq3::dot(v2, n1);
        v1 = v2 * alpha;
        source = p2 - v1;
        v1 = p2 - p1;
        v2 = p2 - r2;
        alpha = mpq3::dot(v1, n1) / mpq3::dot(v2, n1);
        v1 = v2 * alpha;
        target = p2 - v1;
        ans_ok = true;
      }
    }
    else {
      if (dbg_level > 1) {
        std::cout << "case 1b: ans=false\n";
      }
      ans_ok = false;
    }
  }
  else {
    v2 = q2 - p1;
    n = mpq3::cross(v1, v2);
    if (dbg_level > 1) {
      std::cout << "case 2: v1=" << v1 << " v2=" << v2 << " n=" << n << "\n";
    }
    if (mpq3::dot(v, n) < 0) {
      if (dbg_level > 1) {
        std::cout << "case 2a: ans=false\n";
      }
      ans_ok = false;
    }
    else {
      v1 = r1 - p1;
      n = mpq3::cross(v1, v2);
      if (dbg_level > 1) {
        std::cout << "case 2b: v1=" << v1 << " v2=" << v2 << " n=" << n << "\n";
      }
      if (mpq3::dot(v, n) > 0) {
        v1 = p1 - p2;
        v2 = p1 - r1;
        alpha = mpq3::dot(v1, n2) / mpq3::dot(v2, n2);
        v1 = v2 * alpha;
        source = p1 - v1;
        v1 = p1 - p2;
        v2 = p1 - q1;
        alpha = mpq3::dot(v1, n2) / mpq3::dot(v2, n2);
        v1 = v2 * alpha;
        target = p1 - v1;
        ans_ok = true;
      }
      else {
        v1 = p2 - p1;
        v2 = p2 - q2;
        alpha = mpq3::dot(v1, n1) / mpq3::dot(v2, n1);
        v1 = v2 * alpha;
        source = p2 - v1;
        v1 = p1 - p2;
        v2 = p1 - q1;
        alpha = mpq3::dot(v1, n2) / mpq3::dot(v2, n2);
        v1 = v2 * alpha;
        target = p1 - v1;
        ans_ok = true;
      }
    }
  }

  if (dbg_level > 0) {
    if (ans_ok) {
      std::cout << "intersect: " << source << ", " << target << "\n";
    }
    else {
      std::cout << "no intersect\n";
    }
  }
  if (ans_ok) {
    if (source == target) {
      return ITT_value(IPOINT, source);
    }
    else {
      return ITT_value(ISEGMENT, source, target);
    }
  }
  else {
    return ITT_value(INONE);
  }
}

/* Helper function for intersect_tri_tri. Args have been canonicalized for triangle 1. */

static ITT_value itt_canon1(const mpq3 &p1,
                            const mpq3 &q1,
                            const mpq3 &r1,
                            const mpq3 &p2,
                            const mpq3 &q2,
                            const mpq3 &r2,
                            const mpq3 &n1,
                            const mpq3 &n2,
                            int sp2,
                            int sq2,
                            int sr2)
{
  constexpr int dbg_level = 0;
  if (sp2 > 0) {
    if (sq2 > 0) {
      return itt_canon2(p1, r1, q1, r2, p2, q2, n1, n2);
    }
    else if (sr2 > 0) {
      return itt_canon2(p1, r1, q1, q2, r2, p2, n1, n2);
    }
    else {
      return itt_canon2(p1, q1, r1, p2, q2, r2, n1, n2);
    }
  }
  else if (sp2 < 0) {
    if (sq2 < 0) {
      return itt_canon2(p1, q1, r1, r2, p2, q2, n1, n2);
    }
    else if (sr2 < 0) {
      return itt_canon2(p1, q1, r1, q2, r2, p2, n1, n2);
    }
    else {
      return itt_canon2(p1, r1, q1, p2, q2, r2, n1, n2);
    }
  }
  else {
    if (sq2 < 0) {
      if (sr2 >= 0) {
        return itt_canon2(p1, r1, q1, q2, r2, p2, n1, n2);
      }
      else {
        return itt_canon2(p1, q1, r1, p2, q2, r2, n1, n2);
      }
    }
    else if (sq2 > 0) {
      if (sr2 > 0) {
        return itt_canon2(p1, r1, q1, p2, q2, r2, n1, n2);
      }
      else {
        return itt_canon2(p1, q1, r1, q2, r2, p2, n1, n2);
      }
    }
    else {
      if (sr2 > 0) {
        return itt_canon2(p1, q1, r1, r2, p2, q2, n1, n2);
      }
      else if (sr2 < 0) {
        return itt_canon2(p1, r1, q1, r2, p2, q2, n1, n2);
      }
      else {
        if (dbg_level > 0) {
          std::cout << "triangles are coplanar\n";
        }
        return ITT_value(ICOPLANAR);
      }
    }
  }
  return ITT_value(INONE);
}

static ITT_value intersect_tri_tri(const TMesh &tm, int t1, int t2)
{
  constexpr int dbg_level = 0;
  const IndexedTriangle &tri1 = tm.tri(t1);
  const IndexedTriangle &tri2 = tm.tri(t2);
  const mpq3 &p1 = tm.vert(tri1.v0());
  const mpq3 &q1 = tm.vert(tri1.v1());
  const mpq3 &r1 = tm.vert(tri1.v2());
  const mpq3 &p2 = tm.vert(tri2.v0());
  const mpq3 &q2 = tm.vert(tri2.v1());
  const mpq3 &r2 = tm.vert(tri2.v2());

  if (dbg_level > 0) {
    std::cout << "\nINTERSECT_TRI_TRI t1=" << t1 << ", t2=" << t2 << "\n";
    std::cout << "  p1 = " << p1 << "\n";
    std::cout << "  q1 = " << q1 << "\n";
    std::cout << "  r1 = " << r1 << "\n";
    std::cout << "  p2 = " << p2 << "\n";
    std::cout << "  q2 = " << q2 << "\n";
    std::cout << "  r2 = " << r2 << "\n";
  }

  /* Get signs of t1's vertices' distances to plane of t2. */
  /* If don't have normal, use mpq3 n2 = cross_v3v3(sub_v3v3(p2, r2), sub_v3v3(q2, r2)); */
  const mpq3 &n2 = tm.tri_plane(t2).n;
  int sp1 = sgn(mpq3::dot(p1 - r2, n2));
  int sq1 = sgn(mpq3::dot(q1 - r2, n2));
  int sr1 = sgn(mpq3::dot(r1 - r2, n2));

  if (dbg_level > 1) {
    std::cout << "  sp1=" << sp1 << " sq1=" << sq1 << " sr1=" << sr1 << "\n";
  }

  if ((sp1 * sq1 > 0) && (sp1 * sr1 > 0)) {
    if (dbg_level > 0) {
      std::cout << "no intersection, all t1's verts above or below t2\n";
    }
    return ITT_value(INONE);
  }

  /* Repeat for signs of t2's vertices with respect to plane of t1. */
  /* If don't have normal, use mpq3 n1 = cross_v3v3(sub_v3v3(q1, p1), sub_v3v3(r1, p1)); */
  const mpq3 &n1 = tm.tri_plane(t1).n;
  int sp2 = sgn(mpq3::dot(p2 - r1, n1));
  int sq2 = sgn(mpq3::dot(q2 - r1, n1));
  int sr2 = sgn(mpq3::dot(r2 - r1, n1));

  if (dbg_level > 1) {
    std::cout << "  sp2=" << sp2 << " sq2=" << sq2 << " sr2=" << sr2 << "\n";
  }

  if ((sp2 * sq2 > 0) && (sp2 * sr2 > 0)) {
    if (dbg_level > 0) {
      std::cout << "no intersection, all t2's verts above or below t1\n";
    }
    return ITT_value(INONE);
  }

  /* Do rest of the work with vertices in a canonical order, where p1 is on
   * postive side of plane and q1, r1 are not; similarly for p2.
   */
  ITT_value ans;
  if (sp1 > 0) {
    if (sq1 > 0) {
      ans = itt_canon1(r1, p1, q1, p2, r2, q2, n1, n2, sp2, sr2, sq2);
    }
    else if (sr1 > 0) {
      ans = itt_canon1(q1, r1, p1, p2, r2, q2, n1, n2, sp2, sr2, sq2);
    }
    else {
      ans = itt_canon1(p1, q1, r1, p2, q2, r2, n1, n2, sp2, sq2, sr2);
    }
  }
  else if (sp1 < 0) {
    if (sq1 < 0) {
      ans = itt_canon1(r1, p1, q1, p2, q2, r2, n1, n2, sp2, sq2, sr2);
    }
    else if (sr1 < 0) {
      ans = itt_canon1(q1, r1, p1, p2, q2, r2, n1, n2, sp2, sq2, sr2);
    }
    else {
      ans = itt_canon1(p1, q1, r1, p2, r2, q2, n1, n2, sp2, sr2, sq2);
    }
  }
  else {
    if (sq1 < 0) {
      if (sr1 >= 0) {
        ans = itt_canon1(q1, r1, p1, p2, r2, q2, n1, n2, sp2, sr2, sq2);
      }
      else {
        ans = itt_canon1(p1, q1, r1, p2, q2, r2, n1, n2, sp2, sq2, sr2);
      }
    }
    else if (sq1 > 0) {
      if (sr1 > 0) {
        ans = itt_canon1(p1, q1, r1, p2, r2, q2, n1, n2, sp2, sr2, sq2);
      }
      else {
        ans = itt_canon1(q1, r1, p1, p2, q2, r2, n1, n2, sp2, sq2, sr2);
      }
    }
    else {
      if (sr1 > 0) {
        ans = itt_canon1(r1, p1, q1, p2, q2, r2, n1, n2, sp2, sq2, sr2);
      }
      else if (sr1 < 0) {
        ans = itt_canon1(r1, p1, q1, p2, r2, q2, n1, n2, sp2, sr2, sq2);
      }
      else {
        if (dbg_level > 0) {
          std::cout << "triangles are coplanar\n";
        }
        ans = ITT_value(ICOPLANAR);
      }
    }
  }
  if (ans.kind == ICOPLANAR) {
    ans.t_source = t2;
  }
  return ans;
}

struct CDT_data {
  planeq t_plane;
  Vector<mpq2> vert;
  Vector<std::pair<int, int>> edge;
  Vector<Vector<int>> face;
  Vector<int> input_face;        /* Parallels face, gives id from input TMesh of input face. */
  Vector<bool> is_reversed;      /* Parallels face, says if input face orientation is opposite. */
  CDT_result<mpq_class> cdt_out; /* Result of running CDT on input with (vert, edge, face). */
  int proj_axis;
};

/* Project a 3d vert to a 2d one by eliding proj_axis. This does not create
 * degeneracies as long as the projection axis is one where the corresponding
 * component of the originating plane normal is non-zero.
 */
static mpq2 project_3d_to_2d(const mpq3 &p3d, int proj_axis)
{
  mpq2 p2d;
  switch (proj_axis) {
    case (0): {
      p2d[0] = p3d[1];
      p2d[1] = p3d[2];
    } break;
    case (1): {
      p2d[0] = p3d[0];
      p2d[1] = p3d[2];
    } break;
    case (2): {
      p2d[0] = p3d[0];
      p2d[1] = p3d[1];
    } break;
    default:
      BLI_assert(false);
  }
  return p2d;
}

/* We could dedup verts here, but CDT routine will do that anyway. */
static int prepare_need_vert(CDT_data &cd, const mpq3 &p3d)
{
  mpq2 p2d = project_3d_to_2d(p3d, cd.proj_axis);
  int v = cd.vert.append_and_get_index(p2d);
  return v;
}

/* To unproject a 2d vert that was projected along cd.proj_axis, we copy the coordinates
 * from the two axes not involved in the projection, and use the plane equation of the
 * originating 3d plane, cd.t_plane, to derive the coordinate of the projected axis.
 * The plane equation says a point p is on the plane if dot(p, plane.n()) + plane.d() == 0.
 * Assume that the projection axis is such that plane.n()[proj_axis] != 0.
 */
static mpq3 unproject_cdt_vert(const CDT_data &cd, const mpq2 &p2d)
{
  mpq3 p3d;
  BLI_assert(cd.t_plane.n[cd.proj_axis] != 0);
  switch (cd.proj_axis) {
    case (0): {
      mpq_class num = cd.t_plane.n[1] * p2d[0] + cd.t_plane.n[2] * p2d[1] + cd.t_plane.d;
      num = -num;
      p3d[0] = num / cd.t_plane.n[0];
      p3d[1] = p2d[0];
      p3d[2] = p2d[1];
    } break;
    case (1): {
      p3d[0] = p2d[0];
      mpq_class num = cd.t_plane.n[0] * p2d[0] + cd.t_plane.n[2] * p2d[1] + cd.t_plane.d;
      num = -num;
      p3d[1] = num / cd.t_plane.n[1];
      p3d[2] = p2d[1];
    } break;
    case (2): {
      p3d[0] = p2d[0];
      p3d[1] = p2d[1];
      mpq_class num = cd.t_plane.n[0] * p2d[0] + cd.t_plane.n[1] * p2d[1] + cd.t_plane.d;
      num = -num;
      p3d[2] = num / cd.t_plane.n[2];
    } break;
    default:
      BLI_assert(false);
  }
  return p3d;
}

static void prepare_need_edge(CDT_data &cd, const mpq3 &p1, const mpq3 &p2)
{
  int v1 = prepare_need_vert(cd, p1);
  int v2 = prepare_need_vert(cd, p2);
  cd.edge.append(std::pair<int, int>(v1, v2));
}

static void prepare_need_tri(CDT_data &cd, const TMesh &tm, int t)
{
  IndexedTriangle tri = tm.tri(t);
  int v0 = prepare_need_vert(cd, tm.vert(tri.v0()));
  int v1 = prepare_need_vert(cd, tm.vert(tri.v1()));
  int v2 = prepare_need_vert(cd, tm.vert(tri.v2()));
  bool rev;
  /* How to get CCW orientation of projected tri? Note that when look down y axis
   * as opposed to x or z, the orientation of the other two axes is not right-and-up.
   */
  if (cd.t_plane[cd.proj_axis] >= 0) {
    rev = cd.proj_axis == 1;
  }
  else {
    rev = cd.proj_axis != 1;
  }
  /* If t's plane is opposite to cd.t_plane, need to reverse again. */
  if (sgn(tm.tri_plane(t).n[cd.proj_axis]) != sgn(cd.t_plane[cd.proj_axis])) {
    rev = !rev;
  }
  int cd_t = cd.face.append_and_get_index(Vector<int>());
  cd.face[cd_t].append(v0);
  if (rev) {
    cd.face[cd_t].append(v2);
    cd.face[cd_t].append(v1);
  }
  else {
    cd.face[cd_t].append(v1);
    cd.face[cd_t].append(v2);
  }
  cd.input_face.append(t);
  cd.is_reversed.append(rev);
}

static CDT_data prepare_cdt_input(const TMesh &tm, int t, const Vector<ITT_value> itts)
{
  CDT_data ans;
  BLI_assert(tm.has_planes());
  ans.t_plane = tm.tri_plane(t);
  const planeq &pl = ans.t_plane;
  ans.proj_axis = mpq3::dominant_axis(pl.n);
  prepare_need_tri(ans, tm, t);
  for (const ITT_value &itt : itts) {
    switch (itt.kind) {
      case INONE:
        break;
      case IPOINT: {
        prepare_need_vert(ans, itt.p1);
      } break;
      case ISEGMENT: {
        prepare_need_edge(ans, itt.p1, itt.p2);
      } break;
      case ICOPLANAR: {
        prepare_need_tri(ans, tm, itt.t_source);
      } break;
    }
  }
  return ans;
}

static CDT_data prepare_cdt_input_for_cluster(const TMesh &tm,
                                              const CoplanarClusterInfo &clinfo,
                                              int c,
                                              const Vector<ITT_value> itts)
{
  CDT_data ans;
  BLI_assert(c >= 0 && c < clinfo.tot_cluster());
  const CoplanarCluster &cl = clinfo.cluster(c);
  BLI_assert(cl.tot_tri() > 0);
  int t0 = cl.tri(0);
  BLI_assert(tm.has_planes());
  ans.t_plane = tm.tri_plane(t0);
  const planeq &pl = ans.t_plane;
  ans.proj_axis = mpq3::dominant_axis(pl.n);
  for (const int t : cl) {
    prepare_need_tri(ans, tm, t);
  }
  for (const ITT_value &itt : itts) {
    switch (itt.kind) {
      case IPOINT: {
        prepare_need_vert(ans, itt.p1);
      } break;
      case ISEGMENT: {
        prepare_need_edge(ans, itt.p1, itt.p2);
      } break;
      default:
        break;
    }
  }
  return ans;
}

/* Fills in cd.cdt_out with result of doing the cdt calculation on (vert, edge, face). */
static void do_cdt(CDT_data &cd)
{
  constexpr int dbg_level = 0;
  /* TODO: find a way to avoid the following copies. Maybe add a CDT_input variant (or just
   * change its signature) to use Vectors instead of Arrays for the three inputs.
   */
  CDT_input<mpq_class> cdt_in;
  cdt_in.vert = Array<mpq2>(cd.vert);
  cdt_in.edge = Array<std::pair<int, int>>(cd.edge);
  cdt_in.face = Array<Vector<int>>(cd.face);
  if (dbg_level > 0) {
    std::cout << "CDT input\nVerts:\n";
    for (uint i = 0; i < cdt_in.vert.size(); ++i) {
      std::cout << "v" << i << ": " << cdt_in.vert[i] << "\n";
    }
    std::cout << "Edges:\n";
    for (uint i = 0; i < cdt_in.edge.size(); ++i) {
      std::cout << "e" << i << ": (" << cdt_in.edge[i].first << ", " << cdt_in.edge[i].second
                << ")\n";
    }
    std::cout << "Tris\n";
    for (uint f = 0; f < cdt_in.face.size(); ++f) {
      std::cout << "f" << f << ": ";
      for (uint j = 0; j < cdt_in.face[f].size(); ++j) {
        std::cout << cdt_in.face[f][j] << " ";
      }
      std::cout << "\n";
    }
  }
  cdt_in.epsilon = 0; /* TODO: needs attention for non-exact T. */
  cd.cdt_out = BLI::delaunay_2d_calc(cdt_in, CDT_INSIDE);
  if (dbg_level > 0) {
    std::cout << "\nCDT result\nVerts:\n";
    for (uint i = 0; i < cd.cdt_out.vert.size(); ++i) {
      std::cout << "v" << i << ": " << cd.cdt_out.vert[i] << "\n";
    }
    std::cout << "Tris\n";
    for (uint f = 0; f < cd.cdt_out.face.size(); ++f) {
      std::cout << "f" << f << ": ";
      for (uint j = 0; j < cd.cdt_out.face[f].size(); ++j) {
        std::cout << cd.cdt_out.face[f][j] << " ";
      }
      std::cout << "orig: ";
      for (uint j = 0; j < cd.cdt_out.face_orig[f].size(); ++j) {
        std::cout << cd.cdt_out.face_orig[f][j] << " ";
      }
      std::cout << "\n";
    }
  }
}

/* Using the result of CDT in cd.cdt_out, extract a TMesh representing the subdivision
 * of input triangle t, which should be an element of cd.input_face.
 */

static TMesh extract_subdivided_tri(const CDT_data &cd, const TMesh &in_tm, int t)
{
  TMesh ans;

  /* We want all triangles in cdt_out that had t (as indexed in the CDT_input) as an orig. */
  /* Which output verts do we need in our answer? */
  const CDT_result<mpq_class> &cdt_out = cd.cdt_out;
  Array<bool> needvert(cdt_out.vert.size()); /* Initialized to all false. */
  Array<bool> needtri(cdt_out.face.size());  /* Initialized to all false. */
  int t_in_cdt = -1;
  for (int i = 0; i < static_cast<int>(cd.input_face.size()); ++i) {
    if (cd.input_face[i] == t) {
      t_in_cdt = i;
    }
  }
  if (t_in_cdt == -1) {
    std::cout << "Could not find " << t << " in cdt input tris\n";
    BLI_assert(false);
    return ans;
  }
  int num_needed_v = 0;
  for (uint f = 0; f < cdt_out.face.size(); ++f) {
    if (cdt_out.face_orig[f].contains(t_in_cdt)) {
      needtri[f] = true;
      for (int v : cdt_out.face[f]) {
        if (!needvert[v]) {
          needvert[v] = true;
          ++num_needed_v;
        }
      }
    }
  }
  Array<int> cdt_v_to_out_v(cdt_out.vert.size());

  for (uint cdt_v = 0; cdt_v < cdt_out.vert.size(); ++cdt_v) {
    if (needvert[cdt_v]) {
      mpq3 v3co = unproject_cdt_vert(cd, cdt_out.vert[cdt_v]);
      cdt_v_to_out_v[cdt_v] = ans.add_vert(v3co);
    }
  }
  int orig = in_tm.tri(t).orig();
  for (uint f = 0; f < cdt_out.face.size(); ++f) {
    if (needtri[f]) {
      BLI_assert(cdt_out.face[f].size() == 3);
      int v0 = cdt_out.face[f][0];
      int v1 = cdt_out.face[f][1];
      int v2 = cdt_out.face[f][2];
      BLI_assert(needvert[v0] && needvert[v1] && needvert[v2]);
      int out_v0 = cdt_v_to_out_v[v0];
      int out_v1 = cdt_v_to_out_v[v1];
      int out_v2 = cdt_v_to_out_v[v2];
      if (cd.is_reversed[t_in_cdt]) {
        ans.add_tri(out_v0, out_v2, out_v1, orig);
      }
      else {
        ans.add_tri(out_v0, out_v1, out_v2, orig);
      }
    }
  }
  return ans;
}

static TMesh calc_tri_subdivided(const TMesh &in_tm, int t)
{
  constexpr int dbg_level = 0;
  TMesh ans;

  if (dbg_level > 0) {
    std::cout << "\ncalc_tri_subdivided for tri " << t << "\n\n";
  }
  int ntri = in_tm.tot_tri();
  Vector<ITT_value> itts;
  for (int t_other = 0; t_other < ntri; ++t_other) {
    if (t_other == t) {
      continue;
    }
    /* Intersect t with t_other. */
    ITT_value itt = intersect_tri_tri(in_tm, t, t_other);
    if (dbg_level > 1) {
      std::cout << "intersect " << t << " with " << t_other << " result: " << itt << "\n";
    }
    if (itt.kind != INONE) {
      itts.append(itt);
    }
  }
  if (itts.size() == 0) {
    /* No intersections: answer is just the original triangle t. */
    ans = TMesh(in_tm, t);
  }
  else {
    /* Use CDT to subdivide the triangle. */
    CDT_data cd_data = prepare_cdt_input(in_tm, t, itts);
    do_cdt(cd_data);
    ans = extract_subdivided_tri(cd_data, in_tm, t);
  }
  if (dbg_level > 0) {
    std::cout << "\ncalc_tri_subdivided " << t << " result:\n" << ans;
  }
  return ans;
}

static CDT_data calc_cluster_subdivided(const CoplanarClusterInfo &clinfo, int c, const TMesh &tm)
{
  constexpr int dbg_level = 0;
  BLI_assert(0 <= c && c < clinfo.tot_cluster());
  const CoplanarCluster &cl = clinfo.cluster(c);
  /* Make a CDT input with triangles from C and intersects from other triangles in tm. */
  if (dbg_level > 0) {
    std::cout << "calc_cluster_subdivided for cluster " << c << " = " << cl << "\n";
  }
  /* Get vector itts of all intersections of a triangle of cl with any triangle of tm not
   * in cl and not coplanar with it (for that latter, if there were an intersection,
   * it should already be in cluster cl).
   */
  int ntri = tm.tot_tri();
  Vector<ITT_value> itts;
  for (int t_other = 0; t_other < ntri; ++t_other) {
    if (clinfo.tri_cluster(t_other) != c) {
      if (dbg_level > 0) {
        std::cout << "intersect cluster " << c << " with tri " << t_other << "\n";
      }
      for (const int t : cl) {
        ITT_value itt = intersect_tri_tri(tm, t, t_other);
        if (dbg_level > 0) {
          std::cout << "intersect tri " << t << " with tri " << t_other << " = " << itt << "\n";
        }
        if (itt.kind != INONE && itt.kind != ICOPLANAR) {
          itts.append(itt);
        }
      }
    }
  }
  /* Use CDT to subdivide the cluster triangles and the points and segs in itts. */
  CDT_data cd_data = prepare_cdt_input_for_cluster(tm, clinfo, c, itts);
  do_cdt(cd_data);
  return cd_data;
}

static TMesh union_tri_subdivides(const BLI::Array<TMesh> &tri_subdivided)
{
  TMesh ans;
  for (const TMesh &tmsub : tri_subdivided) {
    Array<int> vtrans(tmsub.tot_vert());
    for (int v = 0; v < tmsub.tot_vert(); ++v) {
      vtrans[v] = ans.add_vert(tmsub.vert(v));
    }
    for (int t = 0; t < tmsub.tot_tri(); ++t) {
      const IndexedTriangle &tri = tmsub.tri(t);
      ans.add_tri(vtrans[tri.v0()], vtrans[tri.v1()], vtrans[tri.v2()], tri.orig());
    }
  }
  return ans;
}

/* Need a canonical form of a plane so that can use as a key in a map and
 * all coplanar triangles will have the same key.
 * Make the first nonzero component of the normal be 1.
 */
static planeq canon_plane(const planeq &pl)
{
  if (pl.n[0] != 0) {
    return planeq(mpq3(1, pl.n[1] / pl.n[0], pl.n[2] / pl.n[0]), pl.d / pl.n[0]);
  }
  else if (pl.n[1] != 0) {
    return planeq(mpq3(0, 1, pl.n[2] / pl.n[1]), pl.d / pl.n[1]);
  }
  else {
    return planeq(mpq3(0, 0, 1), pl.d / pl.n[2]);
  }
}

/* Is a point in the interior of a 2d triangle or on one of its
 * edges but not either endpoint of the edge?
 * orient[pi][i] is the orientation test of the point pi against
 * the side of the triangle starting at index i.
 * Assume the triangele is non-degenerate and CCW-oriented.
 * Then answer is true if p is left of or on all three of triangle a's edges,
 * and strictly left of at least on of them.
 */
static bool non_trivially_2d_point_in_tri(const int orients[3][3], int pi)
{
  int p_left_01 = orients[pi][0];
  int p_left_12 = orients[pi][1];
  int p_left_20 = orients[pi][2];
  return (p_left_01 >= 0 && p_left_12 >= 0 && p_left_20 >= 0 &&
          (p_left_01 + p_left_12 + p_left_20) >= 2);
}

/* Given orients as defined in non_trivially_2d_intersect, do the triangles
 * overlap in a "hex" pattern? That is, the overlap region is a hexagon, which
 * one gets by having, each point of one triangle being strictly rightof one
 * edge of the other and strictly left of the other two edges; and vice versa.
 */
static bool non_trivially_2d_hex_overlap(int orients[2][3][3])
{
  for (int ab = 0; ab < 2; ++ab) {
    for (int i = 0; i < 3; ++i) {
      bool ok = orients[ab][i][0] + orients[ab][i][1] + orients[ab][i][2] == 1 &&
                orients[ab][i][0] != 0 && orients[ab][i][1] != 0 && orients[i][2] != 0;
      if (!ok) {
        return false;
      }
    }
  }
  return true;
}

/* Given orients as defined in non_trivially_2d_intersect, do the triangles
 * have one shared edge in a "folded-over" configuration?
 * As well as a shared edge, the third vertex of one triangle needs to be
 * rightof one and leftof the other two edges of the other triangle.
 */

static bool non_trivially_2d_shared_edge_overlap(int orients[2][3][3],
                                                 const mpq2 *a[3],
                                                 const mpq2 *b[3])
{
  for (int i = 0; i < 3; ++i) {
    int in = (i + 1) % 3;
    int inn = (i + 2) % 3;
    for (int j = 0; j < 3; ++j) {
      int jn = (j + 1) % 3;
      int jnn = (j + 2) % 3;
      if (*a[i] == *b[j] && *a[in] == *b[jn]) {
        /* Edge from a[i] is shared with edge from b[j]. */
        /* See if a[inn] is rightof or on one of the other edges of b.
         * If it is on, then it has to be rightof or leftof the shared edge,
         * depending on which edge it is.
         */
        if (orients[0][inn][jn] < 0 || orients[0][inn][jnn] < 0) {
          return true;
        }
        if (orients[0][inn][jn] == 0 && orients[0][inn][j] == 1) {
          return true;
        }
        if (orients[0][inn][jnn] == 0 && orients[0][inn][j] == -1) {
          return true;
        }
        /* Similarly for b[jnn]. */
        if (orients[1][jnn][in] < 0 || orients[1][jnn][inn] < 0) {
          return true;
        }
        if (orients[1][jnn][in] == 0 && orients[1][jnn][i] == 1) {
          return true;
        }
        if (orients[1][jnn][inn] == 0 && orients[1][jnn][i] == -1) {
          return true;
        }
      }
    }
  }
  return false;
}

/* Are the triangles the same, perhaps with some permutation of vertices? */
static bool same_triangles(const mpq2 *a[3], const mpq2 *b[3])
{
  for (int i = 0; i < 3; ++i) {
    if (a[0] == b[i] && a[1] == b[(i + 1) % 3] && a[2] == b[(i + 2) % 3]) {
      return true;
    }
  }
  return false;
}

/* Do 2d triangles (a[0], a[1], a[2]) and (b[0], b[1], b2[2]) intersect at more than just shared
 * vertices or a shared edge? This is true if any point of one tri is non-trivially inside the
 * other. NO: that isn't quite sufficient: there is also the case where the verts are all mutually
 * outside the other's triangle, but there is a hexagonal overlap region where they overlap.
 */

static bool non_trivially_2d_intersect(const mpq2 *a[3], const mpq2 *b[3])
{
  /* TODO: Could experiment with trying bounding box tests before these.
   * TODO: Find a less expensive way than 18 orient tests to do this.
   */
  /* orients[0][ai][bi] is orient of point a[ai] compared to seg starting at b[bi].
   * orients[1][bi][ai] is orient of point b[bi] compared to seg starting at a[ai].
   */
  int orients[2][3][3];
  for (int ab = 0; ab < 2; ++ab) {
    for (int ai = 0; ai < 3; ++ai) {
      for (int bi = 0; bi < 3; ++bi) {
        if (ab == 0) {
          orients[0][ai][bi] = mpq2::orient2d(*b[bi], *b[(bi + 1) % 3], *a[ai]);
        }
        else {
          orients[1][bi][ai] = mpq2::orient2d(*a[ai], *a[(ai + 1) % 3], *b[bi]);
        }
      }
    }
  }
  return non_trivially_2d_point_in_tri(orients[0], 0) ||
         non_trivially_2d_point_in_tri(orients[0], 1) ||
         non_trivially_2d_point_in_tri(orients[0], 2) ||
         non_trivially_2d_point_in_tri(orients[1], 0) ||
         non_trivially_2d_point_in_tri(orients[1], 1) ||
         non_trivially_2d_point_in_tri(orients[1], 2) || non_trivially_2d_hex_overlap(orients) ||
         non_trivially_2d_shared_edge_overlap(orients, a, b) || same_triangles(a, b);
  return true;
}

/* Does triangle t in tm non-trivially non-coplanar intersect any triangle
 * in CoplanarCluster cl? Assume t is known to be in the same plane as all
 * the triangles in cl, and that proj_axis is a good axis to project down
 * to solve this problem in 2d.
 */

static bool non_trivially_coplanar_intersects(const TMesh &tm,
                                              int t,
                                              const CoplanarCluster &cl,
                                              int proj_axis)
{
  const IndexedTriangle &tri = tm.tri(t);
  mpq2 v0 = project_3d_to_2d(tm.vert(tri.v0()), proj_axis);
  mpq2 v1 = project_3d_to_2d(tm.vert(tri.v1()), proj_axis);
  mpq2 v2 = project_3d_to_2d(tm.vert(tri.v2()), proj_axis);
  if (mpq2::orient2d(v0, v1, v2) != 1) {
    mpq2 tmp = v1;
    v1 = v2;
    v2 = tmp;
  }
  for (const int cl_t : cl) {
    const IndexedTriangle &cl_tri = tm.tri(cl_t);
    mpq2 ctv0 = project_3d_to_2d(tm.vert(cl_tri.v0()), proj_axis);
    mpq2 ctv1 = project_3d_to_2d(tm.vert(cl_tri.v1()), proj_axis);
    mpq2 ctv2 = project_3d_to_2d(tm.vert(cl_tri.v2()), proj_axis);
    if (mpq2::orient2d(ctv0, ctv1, ctv2) != 1) {
      mpq2 tmp = ctv1;
      ctv1 = ctv2;
      ctv2 = tmp;
    }
    const mpq2 *v[] = {&v0, &v1, &v2};
    const mpq2 *ctv[] = {&ctv0, &ctv1, &ctv2};
    if (non_trivially_2d_intersect(v, ctv)) {
      return true;
    }
  }
  return false;
}

static CoplanarClusterInfo find_clusters(const TMesh &tmesh)
{
  constexpr int dbg_level = 0;
  if (dbg_level > 0) {
    std::cout << "FIND_CLUSTERS\n";
  }
  CoplanarClusterInfo ans(tmesh.tot_tri());
  /* There can be more than one CoplanarCluster per plane. Accumulate them in
   * a Vector. We will have to merge some elements of the Vector as we discover
   * triangles that form intersection bridges between two or more clusters.
   */
  Map<planeq, Vector<CoplanarCluster>> plane_cls;
  for (int t = 0; t < tmesh.tot_tri(); ++t) {
    const planeq &tplane = tmesh.tri_plane(t);
    planeq canon_tplane = canon_plane(tplane);
    if (plane_cls.contains(canon_tplane)) {
      Vector<CoplanarCluster> &curcls = plane_cls.lookup(canon_tplane);
      int proj_axis = mpq3::dominant_axis(canon_tplane.n);
      /* Paritition curcls into those that intersect t non-trivially, and those that don't. */
      Vector<CoplanarCluster *> int_cls;
      Vector<CoplanarCluster *> no_int_cls;
      for (CoplanarCluster &cl : curcls) {
        if (non_trivially_coplanar_intersects(tmesh, t, cl, proj_axis)) {
          int_cls.append(&cl);
        }
        else {
          no_int_cls.append(&cl);
        }
      }
      if (int_cls.size() == 0) {
        /* t doesn't intersect any existing cluster in its plane, so make one just for it. */
        curcls.append(CoplanarCluster(t));
      }
      else if (int_cls.size() == 1) {
        /* t intersects exactly one existing cluster, so can add t to that cluster. */
        int_cls[0]->add_tri(t);
      }
      else {
        /* t intersections 2 or more existing clusters: need to merge them and replace all the
         * originals with the merged one in curcls.
         */
        CoplanarCluster mergecl;
        mergecl.add_tri(t);
        for (CoplanarCluster *cl : int_cls) {
          for (int t : *cl) {
            mergecl.add_tri(t);
          }
        }
        Vector<CoplanarCluster> newvec;
        newvec.append(mergecl);
        for (CoplanarCluster *cl_no_int : no_int_cls) {
          newvec.append(*cl_no_int);
        }
        plane_cls.add_override(canon_tplane, newvec);
      }
    }
    else {
      plane_cls.add_new(canon_tplane, Vector<CoplanarCluster>{CoplanarCluster(t)});
    }
  }
  /* Does this give deterministic order for cluster ids? I think so, since
   * hash for planes is on their values, not their addresses.
   */
  for (auto item : plane_cls.items()) {
    for (const CoplanarCluster &cl : item.value) {
      if (cl.tot_tri() > 1) {
        ans.add_cluster(cl);
      }
    }
  }

  return ans;
}

static TriMesh tmesh_to_trimesh(const TMesh &tm)
{
  TriMesh ans;
  ans.vert = Array<mpq3>(tm.tot_vert());
  ans.tri = Array<IndexedTriangle>(tm.tot_tri());
  for (uint v = 0; v < tm.tot_vert(); ++v) {
    ans.vert[v] = tm.vert(v);
  }
  for (uint t = 0; t < tm.tot_tri(); ++t) {
    ans.tri[t] = tm.tri(t);
  }
  return ans;
}

/* This is the main routine for calculating the self_intersection of a TriMesh. */
static TriMesh tpl_trimesh_self_intersect(const TriMesh &tm_in)
{
  constexpr int dbg_level = 1;
  if (dbg_level > 0) {
    std::cout << "\nTRIMESH_SELF_INTERSECT\n";
  }
  TMesh tmesh(tm_in, true);
  int ntri = tmesh.tot_tri();
  CoplanarClusterInfo clinfo = find_clusters(tmesh);
  if (dbg_level > 1) {
    std::cout << clinfo;
  }
  BLI::Array<CDT_data> cluster_subdivided(clinfo.tot_cluster());
  for (int c = 0; c < clinfo.tot_cluster(); ++c) {
    cluster_subdivided[c] = calc_cluster_subdivided(clinfo, c, tmesh);
  }
  BLI::Array<TMesh> tri_subdivided(ntri);
  for (int t = 0; t < ntri; ++t) {
    int c = clinfo.tri_cluster(t);
    if (c == -1) {
      tri_subdivided[t] = calc_tri_subdivided(tmesh, t);
    }
    else {
      tri_subdivided[t] = extract_subdivided_tri(cluster_subdivided[c], tmesh, t);
    }
  }
  TMesh combined = union_tri_subdivides(tri_subdivided);
  return tmesh_to_trimesh(combined);
}

TriMesh trimesh_self_intersect(const TriMesh &tm_in)
{
  return tpl_trimesh_self_intersect(tm_in);
}

static std::ostream &operator<<(std::ostream &os, const TMesh &tm)
{
  os << "TMesh\nVerts:\n";
  for (int v = 0; v < tm.tot_vert(); ++v) {
    os << " " << v << ": " << tm.vert(v) << "\n";
  }
  os << "Tris:\n";
  for (int t = 0; t < tm.tot_tri(); ++t) {
    os << " " << t << ": " << tm.tri(t) << "\n";
    if (tm.has_planes()) {
      os << "  plane: [" << tm.tri_plane(t).n << ";" << tm.tri_plane(t).d << "]\n";
    }
  }
  return os;
}

std::ostream &operator<<(std::ostream &os, const IndexedTriangle &tri)
{
  os << "tri(" << tri.v0() << "," << tri.v1() << "," << tri.v2() << ")";
  if (tri.orig() != -1) {
    os << "(o" << tri.orig() << ")";
  }
  return os;
}

std::ostream &operator<<(std::ostream &os, const TriMesh &tm)
{
  os << "TriMesh input\nVerts:\n";
  for (uint v = 0; v < tm.vert.size(); ++v) {
    os << " " << v << ": " << tm.vert[v] << "\n";
  }
  os << "Tris:\n";
  for (uint t = 0; t < tm.tri.size(); ++t) {
    os << " " << t << ": " << tm.tri[t] << "\n";
  }
  return os;
}

static std::ostream &operator<<(std::ostream &os, const CoplanarCluster &cl)
{
  os << "cl(";
  bool first = true;
  for (const int t : cl) {
    if (first) {
      first = false;
    }
    else {
      os << ",";
    }
    os << t;
  }
  os << ")";
  return os;
}

static std::ostream &operator<<(std::ostream &os, const CoplanarClusterInfo &clinfo)
{
  os << "Coplanar Cluster Info:\n";
  for (int c = 0; c < clinfo.tot_cluster(); ++c) {
    os << c << ": " << clinfo.cluster(c) << "\n";
  }
  return os;
}

static std::ostream &operator<<(std::ostream &os, const ITT_value &itt)
{
  switch (itt.kind) {
    case INONE:
      os << "none";
      break;
    case IPOINT:
      os << "point " << itt.p1;
      break;
    case ISEGMENT:
      os << "segment " << itt.p1 << " " << itt.p2;
      break;
    case ICOPLANAR:
      os << "coplanar t" << itt.t_source;
      break;
  }
  return os;
}

/* Some contrasting colors to use for distinguishing triangles. */
static const char *drawcolor[] = {
    "0.67 0.14 0.14", /* red */
    "0.16 0.29 0.84", /* blue */
    "0.11 0.41 0.08", /* green */
    "0.50 0.29 0.10", /* brown */
    "0.50 0.15 0.75", /* purple */
    "0.62 0.62 0.62", /* light grey */
    "0.50 0.77 0.49", /* light green */
    "0.61 0.68 1.00", /* light blue */
    "0.16 0.82 0.82", /* cyan */
    "1.00 0.57 0.20", /* orange */
    "1.00 0.93 0.20", /* yellow */
    "0.91 0.87 0.73", /* tan */
    "1.00 0.80 0.95", /* pink */
    "0.34 0.34 0.34"  /* dark grey */
};
static constexpr int numcolors = sizeof(drawcolor) / sizeof(drawcolor[0]);

/* See x3dom.org for an explanation of this way of embedding 3d objects in a web page. */

static const char *htmlfileheader = R"(<head>
<title>Mesh Intersection Tests</title>
<script type='text/javascript' src='http://www.x3dom.org/download/x3dom.js'> </script>
<link rel='stylesheet' type='text/cs href='http://www.x3dom.org/download/x3dom.css'></link>
</head>
)";

void write_html_trimesh(const Array<mpq3> &vert,
                        const Array<IndexedTriangle> &tri,
                        const std::string &fname,
                        const std::string &label)
{
  static bool draw_append = false;
  constexpr const char *drawfiledir = "/tmp/";
  constexpr int draw_width = 1400;
  constexpr int draw_height = 1000;
  constexpr bool draw_vert_labels = true;

  const std::string fpath = std::string(drawfiledir) + fname;

  std::ofstream f;
  if (draw_append) {
    f.open(fpath, std::ios_base::app);
  }
  else {
    f.open(fpath);
  }
  if (!f) {
    std::cout << "Could not open file " << fpath << "\n";
    return;
  }
  if (!draw_append) {
    f << htmlfileheader;
  }

  f << "<div>" << label << "</div>\n<div>\n"
    << "<x3d width='" << draw_width << "px' "
    << "height='" << draw_height << "px'>\n"
    << "<scene>\n";

  BLI::Array<bool> vused(vert.size());
  int i = 0;
  for (const IndexedTriangle &t : tri) {
    double3 dv0, dv1, dv2;
    for (int axis = 0; axis < 3; ++axis) {
      dv0[axis] = vert[t.v0()][axis].get_d();
      dv1[axis] = vert[t.v1()][axis].get_d();
      dv2[axis] = vert[t.v2()][axis].get_d();
    }
    f << "<shape>\n";
    f << "  <appearance>\n";
    f << "    <twosidedmaterial diffuseColor='" << drawcolor[i % numcolors]
      << "' separatebackcolor='false'/>\n";
    f << "  </appearance>\n";
    f << "  <triangleset>\n";
    f << "    <coordinate point='" << dv0[0] << " " << dv0[1] << " " << dv0[2] << " " << dv1[0]
      << " " << dv1[1] << " " << dv1[2] << " " << dv2[0] << " " << dv2[1] << " " << dv2[2]
      << "'/>\n";
    f << "  </triangleset>\n";
    f << "</shape>\n";
    vused[t.v0()] = true;
    vused[t.v1()] = true;
    vused[t.v2()] = true;
    ++i;
  }
  if (draw_vert_labels) {
    for (uint i = 0; i < vert.size(); ++i) {
      if (!vused[i]) {
        continue;
      }
      double3 dv(vert[i][0].get_d(), vert[i][1].get_d(), vert[i][2].get_d());
      f << "<transform translation='" << dv[0] << " " << dv[1] << " " << dv[2] << "'>\n";
      f << "<shape>\n  <appearance>\n"
        << "    <twosidedmaterial diffuseColor='0 0 0'/>\n"
        << "  </appearance>\n"
        << "  <text string='" << i << "'><fontstyle size='0.25'/></text>\n"
        << "</shape>\n</transform>\n";
    }
  }
  f << "</scene>\n</x3d>\n</div>\n";
  draw_append = true;
}

void write_obj_trimesh(const Array<mpq3> &vert,
                       const Array<IndexedTriangle> &tri,
                       const std::string &objname)
{
  constexpr const char *objdir = "/tmp/";
  if (tri.size() == 0) {
    return;
  }

  std::string fname = std::string(objdir) + objname + std::string(".obj");
  std::string matfname = std::string(objdir) + std::string("dumpobj.mtl");
  std::ofstream f;
  f.open(fname);
  if (!f) {
    std::cout << "Could not open file " << fname << "\n";
    return;
  }

  f << "mtllib dumpobj.mtl\n";

  for (const mpq3 &vco : vert) {
    double3 dv(vco[0].get_d(), vco[1].get_d(), vco[2].get_d());
    f << "v " << dv[0] << " " << dv[1] << " " << dv[2] << "\n";
  }
  int i = 0;
  for (const IndexedTriangle &t : tri) {
    int matindex = i % numcolors;
    f << "usemtl mat" + std::to_string(matindex) + "\n";
    /* OBJ files use 1-indexing for vertices. */
    f << "f " << t.v0() + 1 << " " << t.v1() + 1 << " " << t.v2() + 1 << "\n";
    ++i;
  }
  f.close();

  /* Could check if it already exists, but why bother. */
  std::ofstream mf;
  mf.open(matfname);
  if (!mf) {
    std::cout << "Could not open file " << matfname << "\n";
    return;
  }
  for (int c = 0; c < numcolors; ++c) {
    mf << "newmtl mat" + std::to_string(c) + "\n";
    mf << "Kd " << drawcolor[c] << "\n";
  }
}

};  // namespace MeshIntersect

template<> struct DefaultHash<mpq_class> {
  uint32_t operator()(const mpq_class &value) const
  {
    return DefaultHash<float>{}(static_cast<float>(value.get_d()));
  }
};

template<> struct DefaultHash<mpq3> {
  uint32_t operator()(const mpq3 &value) const
  {
    uint32_t hashx = DefaultHash<mpq_class>{}(value.x);
    uint32_t hashy = DefaultHash<mpq_class>{}(value.y);
    uint32_t hashz = DefaultHash<mpq_class>{}(value.z);
    return hashx ^ (hashy * 33) ^ (hashz * 33 * 37);
  }
};

template<> struct DefaultHash<MeshIntersect::planeq> {
  uint32_t operator()(const MeshIntersect::planeq &value) const
  {
    uint32_t hashx = DefaultHash<mpq_class>{}(value.n.x);
    uint32_t hashy = DefaultHash<mpq_class>{}(value.n.y);
    uint32_t hashz = DefaultHash<mpq_class>{}(value.n.z);
    uint32_t hashd = DefaultHash<mpq_class>{}(value.d);
    return hashx ^ (hashy * 33) ^ (hashz * 33 * 37) ^ (hashd * 33 * 37 * 39);
  }
};

}  // namespace BLI
