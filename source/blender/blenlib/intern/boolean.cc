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

#include <algorithm>
#include <fstream>
#include <iostream>

#include "BLI_array.hh"
#include "BLI_assert.h"
#include "BLI_delaunay_2d.h"
#include "BLI_hash.hh"
#include "BLI_map.hh"
#include "BLI_math.h"
#include "BLI_math_mpq.hh"
#include "BLI_mesh_intersect.hh"
#include "BLI_mpq3.hh"
#include "BLI_set.hh"
#include "BLI_span.hh"
#include "BLI_stack.hh"
#include "BLI_vector.hh"
#include "BLI_vector_set.hh"

#include "BLI_boolean.h"

namespace blender {

namespace meshintersect {

/* Edge as two vert indices, in a canonical order (lower vert index first). */
class Edge {
 public:
  Edge() = default;
  Edge(int v0, int v1)
  {
    if (v0 <= v1) {
      m_v[0] = v0;
      m_v[1] = v1;
    }
    else {
      m_v[0] = v1;
      m_v[1] = v0;
    }
  }

  int v0() const
  {
    return m_v[0];
  }
  int v1() const
  {
    return m_v[1];
  }
  int operator[](int i) const
  {
    return m_v[i];
  }
  bool operator==(Edge other) const
  {
    return m_v[0] == other.m_v[0] && m_v[1] == other.m_v[1];
  }

  uint32_t hash() const
  {
    uint32_t v0hash = DefaultHash<int>{}(m_v[0]);
    uint32_t v1hash = DefaultHash<int>{}(m_v[1]);
    return v0hash ^ (v1hash * 33);
  }

 private:
  int m_v[2]{-1, -1};
};

static std::ostream &operator<<(std::ostream &os, const Edge &e)
{
  os << "(" << e.v0() << "," << e.v1() << ")";
  return os;
}

static std::ostream &operator<<(std::ostream &os, const Span<int> &a)
{
  for (uint i = 0; i < a.size(); ++i) {
    os << a[i];
    if (i != a.size() - 1) {
      os << " ";
    }
  }
  return os;
}

static std::ostream &operator<<(std::ostream &os, const Vector<int> &ivec)
{
  os << Span<int>(ivec);
  return os;
}

static std::ostream &operator<<(std::ostream &os, const Array<int> &iarr)
{
  os << Span<int>(iarr);
  return os;
}

class TriMeshTopology {
 public:
  TriMeshTopology(const TriMesh *tm);
  TriMeshTopology(const TriMeshTopology &other) = delete;
  TriMeshTopology(const TriMeshTopology &&other) = delete;
  ~TriMeshTopology();

  /* If e is manifold, return index of the other triangle (not t) that has it. Else return -1. */
  int other_tri_if_manifold(Edge e, int t) const
  {
    if (m_edge_tri.contains(e)) {
      auto p = m_edge_tri.lookup(e);
      if (p->size() == 2) {
        return ((*p)[0] == t) ? (*p)[1] : (*p)[0];
      }
      else {
        return -1;
      }
    }
    else {
      return -1;
    }
  }
  const Vector<int> *edge_tris(Edge e) const
  {
    return m_edge_tri.lookup_default(e, nullptr);
  }
  const Vector<Edge> &vert_edges(int v) const
  {
    return m_vert_edge[v];
  }

 private:
  Map<Edge, Vector<int> *> m_edge_tri; /* Triangles that contain a given Edge (either order). */
  Array<Vector<Edge>> m_vert_edge;     /* Edges incident on each vertex. */
};

TriMeshTopology::TriMeshTopology(const TriMesh *tm)
{
  const int dbg_level = 0;
  if (dbg_level > 0) {
    std::cout << "TriMeshTopology construction\n";
  }
  /* If everything were manifold, there would be about 3V edges (from Euler's formula). */
  const uint estimate_num_edges = 4 * tm->vert.size();
  this->m_edge_tri.reserve(estimate_num_edges);
  this->m_vert_edge = Array<Vector<Edge>>(tm->vert.size());
  int ntri = static_cast<int>(tm->tri.size());
  for (int t = 0; t < ntri; ++t) {
    const IndexedTriangle &tri = tm->tri[t];
    for (int i = 0; i < 3; ++i) {
      int v = tri[i];
      int vnext = tri[(i + 1) % 3];
      Edge e(v, vnext);
      if (!m_vert_edge[v].contains(e)) {
        m_vert_edge[v].append(e);
      }
      if (!m_vert_edge[vnext].contains(e)) {
        m_vert_edge[vnext].append(e);
      }
      auto createf = [t](Vector<int> **pvec) { *pvec = new Vector<int>{t}; };
      auto modifyf = [t](Vector<int> **pvec) { (*pvec)->append_non_duplicates(t); };
      this->m_edge_tri.add_or_modify(Edge(v, vnext), createf, modifyf);
    }
  }
  /* Debugging. */
  if (dbg_level > 0) {
    std::cout << "After TriMeshTopology construction\n";
    for (auto item : m_edge_tri.items()) {
      std::cout << "tris for edge " << item.key << ": " << *item.value << "\n";
      if (false) {
        m_edge_tri.print_stats();
      }
    }
    for (uint v = 0; v < m_vert_edge.size(); ++v) {
      std::cout << "edges for vert " << v << ": ";
      for (Edge e : m_vert_edge[v]) {
        std::cout << e << " ";
      }
      std::cout << "\n";
    }
  }
}

TriMeshTopology::~TriMeshTopology()
{
  auto deletef = [](const Edge &UNUSED(e), const Vector<int> *vec) { delete vec; };
  m_edge_tri.foreach_item(deletef);
}

/* A Patch is a maximal set of faces that share manifold edges only. */
class Patch {
 public:
  Patch() = default;

  void add_tri(int t)
  {
    m_tri.append(t);
  }

  const Vector<int> &tri() const
  {
    return m_tri;
  }

  int tot_tri() const
  {
    return static_cast<int>(m_tri.size());
  }

  int tri(int i) const
  {
    return m_tri[i];
  }

  int cell_above{-1};
  int cell_below{-1};

 private:
  Vector<int> m_tri; /* Indices of triangles in the Patch. */
};

static std::ostream &operator<<(std::ostream &os, const Patch &patch)
{
  os << "Patch " << patch.tri();
  if (patch.cell_above != -1) {
    os << " cell_above=" << patch.cell_above << " cell_below=" << patch.cell_below;
  }
  return os;
}

class PatchesInfo {
 public:
  explicit PatchesInfo(int ntri)
  {
    m_tri_patch = Array<int>(ntri, -1);
    m_pp_edge.reserve(30);
  }
  int tri_patch(int t) const
  {
    return m_tri_patch[t];
  }
  int add_patch()
  {
    int patch_index = static_cast<int>(m_patch.append_and_get_index(Patch()));
    return patch_index;
  }
  void grow_patch(int patch_index, int t)
  {
    m_tri_patch[t] = patch_index;
    m_patch[patch_index].add_tri(t);
  }
  bool tri_is_assigned(int t) const
  {
    return m_tri_patch[t] != -1;
  }
  const Patch &patch(int patch_index) const
  {
    return m_patch[patch_index];
  }
  Patch &patch(int patch_index)
  {
    return m_patch[patch_index];
  }
  int tot_patch() const
  {
    return static_cast<int>(m_patch.size());
  }
  void add_new_patch_patch_edge(int p1, int p2, Edge e)
  {
    m_pp_edge.add_new(std::pair<int, int>(p1, p2), e);
    m_pp_edge.add_new(std::pair<int, int>(p2, p1), e);
  }
  Edge patch_patch_edge(int p1, int p2)
  {
    return m_pp_edge.lookup_default(std::pair<int, int>(p1, p2), Edge(-1, -1));
  }

 private:
  Vector<Patch> m_patch;
  Array<int> m_tri_patch; /* Patch index for corresponding triangle. */
  Map<std::pair<int, int>, Edge>
      m_pp_edge; /* Shared edge for incident patches; (-1,-1) if none. */
};

static bool apply_bool_op(int bool_optype, const Array<int> &winding);

/* A Cell is a volume of 3-space, surrounded by patches.
 * We will partition all 3-space into Cells.
 * One cell, the Ambient cell, contains all other cells.
 */
class Cell {
 public:
  Cell() = default;

  void add_patch(int p)
  {
    m_patches.append(p);
  }

  const Vector<int> &patches() const
  {
    return m_patches;
  }

  const Array<int> &winding() const
  {
    return m_winding;
  }

  void init_winding(int winding_len)
  {
    m_winding = Array<int>(winding_len);
  }

  void seed_ambient_winding()
  {
    m_winding.fill(0);
    m_winding_assigned = true;
  }

  void set_winding_and_flag(const Cell &from_cell, int shape, int delta, int bool_optype)
  {
    std::copy(from_cell.winding().begin(), from_cell.winding().end(), m_winding.begin());
    m_winding[shape] += delta;
    m_winding_assigned = true;
    m_flag = apply_bool_op(bool_optype, m_winding);
  }

  bool flag() const
  {
    return m_flag;
  }

  bool winding_assigned() const
  {
    return m_winding_assigned;
  }

 private:
  Vector<int> m_patches;
  Array<int> m_winding;
  bool m_winding_assigned{false};
  bool m_flag{false};
};

static std::ostream &operator<<(std::ostream &os, const Cell &cell)
{
  os << "Cell patches " << cell.patches();
  if (cell.winding().size() > 0) {
    os << " winding " << cell.winding();
    os << " flag " << cell.flag();
  }
  return os;
}

/* Information about all the Cells. */
class CellsInfo {
 public:
  CellsInfo() = default;

  int add_cell()
  {
    uint index = m_cell.append_and_get_index(Cell());
    return static_cast<int>(index);
  }

  Cell &cell(int c)
  {
    return m_cell[c];
  }

  const Cell &cell(int c) const
  {
    return m_cell[c];
  }

  int tot_cell() const
  {
    return static_cast<int>(m_cell.size());
  }

  void init_windings(int winding_len)
  {
    for (Cell &cell : m_cell) {
      cell.init_winding(winding_len);
    }
  }

 private:
  Vector<Cell> m_cell;
};

/* Concatenate two TriMeshes to make a new one.
 * The second one gets offset vertex indices, and offset original triangles.
 */
static TriMesh concat_trimeshes(const TriMesh &tm_a, const TriMesh &tm_b)
{
  int a_tot_v = static_cast<int>(tm_a.vert.size());
  int b_tot_v = static_cast<int>(tm_b.vert.size());
  int a_tot_t = static_cast<int>(tm_a.tri.size());
  int b_tot_t = static_cast<int>(tm_b.tri.size());
  TriMesh tm;
  tm.vert = Array<mpq3>(a_tot_v + b_tot_v);
  tm.tri = Array<IndexedTriangle>(a_tot_t + b_tot_t);
  for (int v = 0; v < a_tot_v; ++v) {
    tm.vert[v] = tm_a.vert[v];
  }
  for (int v = 0; v < b_tot_v; ++v) {
    tm.vert[v + a_tot_v] = tm_b.vert[v];
  }
  for (int t = 0; t < a_tot_t; ++t) {
    tm.tri[t] = tm_a.tri[t];
  }
  for (int t = 0; t < b_tot_t; ++t) {
    const IndexedTriangle &tri = tm_b.tri[t];
    tm.tri[t + a_tot_t] = IndexedTriangle(
        tri.v0() + a_tot_v, tri.v1() + a_tot_v, tri.v2() + a_tot_v, tri.orig() + a_tot_t);
  }
  return tm;
}

/* Partition the triangles of tm into Patches. */
static PatchesInfo find_patches(const TriMesh &tm, const TriMeshTopology &tmtopo)
{
  const int dbg_level = 0;
  if (dbg_level > 0) {
    std::cout << "\nFIND_PATCHES\n";
  }
  int ntri = static_cast<int>(tm.tri.size());
  PatchesInfo pinfo(ntri);
  /* Algorithm: Grow patches across manifold edges as long as there are unassigned triangles. */
  Stack<int> cur_patch_grow;
  for (int t = 0; t < ntri; ++t) {
    if (pinfo.tri_patch(t) == -1) {
      cur_patch_grow.push(t);
      int cur_patch_index = pinfo.add_patch();
      while (!cur_patch_grow.is_empty()) {
        int tcand = cur_patch_grow.pop();
        if (dbg_level > 1) {
          std::cout << "pop tcand = " << tcand << "; assigned = " << pinfo.tri_is_assigned(tcand)
                    << "\n";
        }
        if (pinfo.tri_is_assigned(tcand)) {
          continue;
        }
        if (dbg_level > 1) {
          std::cout << "grow patch from seed tcand=" << tcand << "\n";
        }
        pinfo.grow_patch(cur_patch_index, tcand);
        const IndexedTriangle &tri = tm.tri[tcand];
        for (int i = 0; i < 3; ++i) {
          Edge e(tri[i], tri[(i + 1) % 3]);
          int t_other = tmtopo.other_tri_if_manifold(e, tcand);
          if (dbg_level > 1) {
            std::cout << "  edge " << e << " generates t_other=" << t_other << "\n";
          }
          if (t_other != -1) {
            if (!pinfo.tri_is_assigned(t_other)) {
              if (dbg_level > 1) {
                std::cout << "    push t_other = " << t_other << "\n";
              }
              cur_patch_grow.push(t_other);
            }
          }
          else {
            /* e is non-manifold. Set any patch-patch incidences we can. */
            if (dbg_level > 1) {
              std::cout << "    e non-manifold case\n";
            }
            const Vector<int> *etris = tmtopo.edge_tris(e);
            if (etris) {
              for (uint i = 0; i < etris->size(); ++i) {
                int t_other = (*etris)[i];
                if (t_other != tcand && pinfo.tri_is_assigned(t_other)) {
                  int p_other = pinfo.tri_patch(t_other);
                  if (p_other == cur_patch_index) {
                    continue;
                  }
                  if (pinfo.patch_patch_edge(cur_patch_index, p_other) == Edge(-1, -1)) {
                    pinfo.add_new_patch_patch_edge(cur_patch_index, p_other, e);
                    if (dbg_level > 1) {
                      std::cout << "added patch_patch_edge (" << cur_patch_index << "," << p_other
                                << ") = " << e << "\n";
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  if (dbg_level > 0) {
    std::cout << "\nafter FIND_PATCHES: found " << pinfo.tot_patch() << " patches\n";
    for (int p = 0; p < pinfo.tot_patch(); ++p) {
      std::cout << p << ": " << pinfo.patch(p) << "\n";
    }
    if (dbg_level > 1) {
      std::cout << "\ntriangle map\n";
      for (int t = 0; t < static_cast<int>(tm.tri.size()); ++t) {
        std::cout << t << ": patch " << pinfo.tri_patch(t) << "\n";
      }
    }
    std::cout << "\npatch-patch incidences\n";
    for (int p1 = 0; p1 < pinfo.tot_patch(); ++p1) {
      for (int p2 = 0; p2 < pinfo.tot_patch(); ++p2) {
        Edge e = pinfo.patch_patch_edge(p1, p2);
        if (!(e == Edge(-1, -1))) {
          std::cout << "p" << p1 << " and p" << p2 << " share edge " << e << "\n";
        }
      }
    }
  }
  return pinfo;
}

/* If e is an edge in tri, return the vertex that isn't part of tri,
 * the "flap" vertex, or -1 if e is not part of tri.
 * Also, e may be reversed in tri.
 * Set *r_rev to true if it is reversed, else false.
 */
static int find_flap_vert(const IndexedTriangle &tri, const Edge e, bool *r_rev)
{
  *r_rev = false;
  int flapv;
  if (tri.v0() == e.v0()) {
    if (tri.v1() == e.v1()) {
      *r_rev = false;
      flapv = tri.v2();
    }
    else {
      if (tri.v2() != e.v1()) {
        return -1;
      }
      *r_rev = true;
      flapv = tri.v1();
    }
  }
  else if (tri.v1() == e.v0()) {
    if (tri.v2() == e.v1()) {
      *r_rev = false;
      flapv = tri.v0();
    }
    else {
      if (tri.v0() != e.v1()) {
        return -1;
      }
      *r_rev = true;
      flapv = tri.v2();
    }
  }
  else {
    if (tri.v2() != e.v0()) {
      return -1;
    }
    if (tri.v0() == e.v1()) {
      *r_rev = false;
      flapv = tri.v1();
    }
    else {
      if (tri.v1() != e.v1()) {
        return -1;
      }
      *r_rev = true;
      flapv = tri.v0();
    }
  }
  return flapv;
}

/*
 * Triangle tri and tri0 share edge e.
 * Classify tri with respet to tri0 as described in
 * sort_tris_around_edge, and return 1, 2, 3, or 4 as tri is:
 * (1) coplanar with tri0 and on same side of e
 * (2) coplanar with tri0 and on opposite side of e
 * (3) below plane of tri0
 * (4) above plane of tri0
 * For "above" and "below", we use the orientation of non-reversed
 * orientation of tri0.
 * Because of the way the intersect mesh was made, we can assume
 * that if a triangle is in class 1 then it is has the same flap vert
 * as tri0.
 * If extra_coord is not null, use then a vert index of INT_MAX should use it.
 */
static int sort_tris_class(const IndexedTriangle &tri,
                           const IndexedTriangle &tri0,
                           const TriMesh &tm,
                           const Edge e,
                           const mpq3 *extra_coord)
{
  const int dbg_level = 0;
  if (dbg_level > 0) {
    std::cout << "classify  e = " << e << "\n";
  }
  mpq3 a0 = tm.vert[tri0.v0()];
  mpq3 a1 = tm.vert[tri0.v1()];
  mpq3 a2 = tm.vert[tri0.v2()];
  bool rev, rev0;
  int flapv0 = find_flap_vert(tri0, e, &rev0);
  int flapv = find_flap_vert(tri, e, &rev);
  if (dbg_level > 0) {
    std::cout << " t0 = " << tri0.v0() << " " << tri0.v1() << " " << tri0.v2();
    std::cout << " rev0 = " << rev0 << " flapv0 = " << flapv0 << "\n";
    std::cout << " t = " << tri.v0() << " " << tri.v1() << " " << tri.v2();
    std::cout << " rev = " << rev << " flapv = " << flapv << "\n";
  }
  BLI_assert(flapv != -1 && flapv0 != -1);
  const mpq3 flap = flapv == INT_MAX ? *extra_coord : tm.vert[flapv];
  int orient = mpq3::orient3d(a0, a1, a2, flap);
  int ans;
  if (orient > 0) {
    ans = rev0 ? 4 : 3;
  }
  else if (orient < 0) {
    ans = rev0 ? 3 : 4;
  }
  else {
    ans = flapv == flapv0 ? 1 : 2;
  }
  if (dbg_level > 0) {
    std::cout << " orient "
              << " = " << orient << " ans = " << ans << "\n";
  }
  return ans;
}

/*
 * Sort the triangles, which all share edge e, as they appear
 * geometrically clockwise when looking down edge e.
 * Triangle t0 is the first triangle in the toplevel call
 * to this recursive routine. The merge step below differs
 * for the top level call and all the rest, so this distinguishes those cases.
 * Care is taken in the case of duplicate triangles to have
 * an ordering that is consistent with that which would happen
 * if another edge of the triangle were sorted around.
 *
 * We sometimes need to do this with an extra triangle that is not part of tm.
 * To accommodate this:
 * If extra_tri is non-null, then an index of INT_MAX should use it for the triangle.
 * If extra_coord is non-null,then an index of INT_MAX should use it for the coordinate.
 */
static Array<int> sort_tris_around_edge(const TriMesh &tm,
                                        const TriMeshTopology &tmtopo,
                                        const Edge e,
                                        const Span<int> &tris,
                                        const int t0,
                                        const IndexedTriangle *extra_tri,
                                        const mpq3 *extra_coord)
{
  /* Divide and conquer, quicsort-like sort.
   * Pick a triangle t0, then partition into groups:
   * (1) coplanar with t0 and on same side of e
   * (2) coplanar with t0 and on opposite side of e
   * (3) below plane of t0
   * (4) above plane of t0
   * Each group is sorted and then the sorts are merged to give the answer.
   * We don't expect the input array to be very large - should typically
   * be only 3 or 4 - so OK to make copies of arrays instead of swapping
   * around in a single array.
   */
  const int dbg_level = 0;
  if (tris.size() == 0) {
    return Array<int>();
  }
  if (dbg_level > 0) {
    if (t0 == tris[0]) {
      std::cout << "\n";
    }
    else {
      std::cout << "  ";
    }
    std::cout << "sort_tris_around_edge " << e << "\n";
    std::cout << "tris = " << tris << "\n";
  }
  Vector<int> g1{tris[0]};
  Vector<int> g2;
  Vector<int> g3;
  Vector<int> g4;
  Vector<int> *groups[] = {&g1, &g2, &g3, &g4};
  const IndexedTriangle &tri0 = tm.tri[t0];
  for (uint i = 1; i < tris.size(); ++i) {
    int t = tris[i];
    BLI_assert(t < static_cast<int>(tm.tri.size()) || extra_tri != nullptr);
    const IndexedTriangle &tri = (t == INT_MAX) ? *extra_tri : tm.tri[t];
    int group_num = sort_tris_class(tri, tri0, tm, e, extra_coord);
    groups[group_num - 1]->append(t);
  }
  if (dbg_level > 1) {
    const char *indent = (t0 == tris[0] ? "" : "  ");
    std::cout << indent << "g1 = " << g1 << "\n";
    std::cout << indent << "g2 = " << g2 << "\n";
    std::cout << indent << "g3 = " << g3 << "\n";
    std::cout << indent << "g4 = " << g4 << "\n";
  }
  if (g1.size() > 1 || g2.size() > 1) {
    /* TODO: sort according to signed triangle index. */
    std::cout << "IMPLEMENT ME\n";
    BLI_assert(false);
  }
  if (g3.size() > 1) {
    Array<int> g3tris(g3.size());
    /* TODO: avoid this by changing interface? */
    for (uint i = 0; i < g3.size(); ++i) {
      g3tris[i] = tris[g3[i]];
    }
    Array<int> g3sorted = sort_tris_around_edge(tm, tmtopo, e, g3tris, t0, extra_tri, extra_coord);
    std::copy(g3sorted.begin(), g3sorted.end(), g3.begin());
  }
  if (g4.size() > 1) {
    Array<int> g4tris(g4.size());
    for (uint i = 0; i < g4.size(); ++i) {
      g4tris[i] = tris[g4[i]];
    }
    Array<int> g4sorted = sort_tris_around_edge(tm, tmtopo, e, g4tris, t0, extra_tri, extra_coord);
    std::copy(g4sorted.begin(), g4sorted.end(), g4.begin());
  }
  uint group_tot_size = g1.size() + g2.size() + g3.size() + g4.size();
  Array<int> ans(group_tot_size);
  int *p = ans.begin();
  if (tris[0] == t0) {
    p = std::copy(g1.begin(), g1.end(), p);
    p = std::copy(g4.begin(), g4.end(), p);
    p = std::copy(g2.begin(), g2.end(), p);
    std::copy(g3.begin(), g3.end(), p);
  }
  else {
    p = std::copy(g3.begin(), g3.end(), p);
    p = std::copy(g1.begin(), g1.end(), p);
    p = std::copy(g4.begin(), g4.end(), p);
    std::copy(g2.begin(), g2.end(), p);
  }
  if (dbg_level > 0) {
    const char *indent = (t0 == tris[0] ? "" : "  ");
    std::cout << indent << "sorted tris = " << ans << "\n";
  }
  return ans;
}

/* Find the Cells around edge e.
 * This possibly makes new cells in cinfo, and sets up the
 * bipartite graph edges between cells and patches.
 * Will modify pinfo and cinfo and the patches and cells they contain.
 */
static void find_cells_from_edge(const TriMesh &tm,
                                 const TriMeshTopology &tmtopo,
                                 PatchesInfo &pinfo,
                                 CellsInfo &cinfo,
                                 const Edge e)
{
  const int dbg_level = 0;
  if (dbg_level > 0) {
    std::cout << "find_cells_from_edge " << e << "\n";
  }
  const Vector<int> *edge_tris = tmtopo.edge_tris(e);
  BLI_assert(edge_tris != nullptr);
  Array<int> sorted_tris = sort_tris_around_edge(
      tm, tmtopo, e, Span<int>(*edge_tris), (*edge_tris)[0], nullptr, nullptr);

  int n_edge_tris = static_cast<int>(edge_tris->size());
  Array<int> edge_patches(n_edge_tris);
  for (int i = 0; i < n_edge_tris; ++i) {
    edge_patches[i] = pinfo.tri_patch(sorted_tris[i]);
    if (dbg_level > 1) {
      std::cout << "edge_patches[" << i << "] = " << edge_patches[i] << "\n";
    }
  }
  for (int i = 0; i < n_edge_tris; ++i) {
    int inext = (i + 1) % n_edge_tris;
    int r_index = edge_patches[i];
    int rnext_index = edge_patches[inext];
    Patch &r = pinfo.patch(r_index);
    Patch &rnext = pinfo.patch(rnext_index);
    bool r_flipped, rnext_flipped;
    find_flap_vert(tm.tri[sorted_tris[i]], e, &r_flipped);
    find_flap_vert(tm.tri[sorted_tris[inext]], e, &rnext_flipped);
    int *r_follow_cell = r_flipped ? &r.cell_below : &r.cell_above;
    int *rnext_prev_cell = rnext_flipped ? &rnext.cell_above : &rnext.cell_below;
    if (dbg_level > 0) {
      std::cout << "process patch pair " << r_index << " " << rnext_index << "\n";
      std::cout << "  r_flipped = " << r_flipped << " rnext_flipped = " << rnext_flipped << "\n";
      std::cout << "  r_follow_cell (" << (r_flipped ? "below" : "above")
                << ") = " << *r_follow_cell << "\n";
      std::cout << "  rnext_prev_cell (" << (rnext_flipped ? "above" : "below")
                << ") = " << *rnext_prev_cell << "\n";
    }
    if (*r_follow_cell == -1 && *rnext_prev_cell == -1) {
      /* Neither is assigned: make a new cell. */
      int c = cinfo.add_cell();
      *r_follow_cell = c;
      *rnext_prev_cell = c;
      Cell &cell = cinfo.cell(c);
      cell.add_patch(r_index);
      cell.add_patch(rnext_index);
      if (dbg_level > 0) {
        std::cout << "  made new cell " << c << "\n";
        std::cout << "  p" << r_index << "." << (r_flipped ? "cell_below" : "cell_above") << " = c"
                  << c << "\n";
        std::cout << "  p" << rnext_index << "." << (rnext_flipped ? "cell_above" : "cell_below")
                  << " = c" << c << "\n";
      }
    }
    else if (*r_follow_cell != -1 && *rnext_prev_cell == -1) {
      int c = *r_follow_cell;
      *rnext_prev_cell = c;
      cinfo.cell(c).add_patch(rnext_index);
      if (dbg_level > 0) {
        std::cout << "  p" << r_index << "." << (r_flipped ? "cell_below" : "cell_above") << " = c"
                  << c << "\n";
      }
    }
    else if (*r_follow_cell == -1 && *rnext_prev_cell != -1) {
      int c = *rnext_prev_cell;
      *r_follow_cell = c;
      cinfo.cell(c).add_patch(r_index);
      if (dbg_level > 0) {
        std::cout << "  p" << rnext_index << "." << (rnext_flipped ? "cell_above" : "cwll_below")
                  << " = c" << c << "\n";
      }
    }
    else {
      if (*r_follow_cell != *rnext_prev_cell) {
        std::cout << "IMPLEMENT ME: MERGE CELLS\n";
        BLI_assert(false);
      }
    }
  }
}

/* Find the partition of 3-space into Cells.
 * This assignes the cell_above and cell_below for each Patch,
 * and figures out which cell is the Ambient one.
 */
static CellsInfo find_cells(const TriMesh &tm, const TriMeshTopology &tmtopo, PatchesInfo &pinfo)
{
  const int dbg_level = 0;
  if (dbg_level > 0) {
    std::cout << "\nFIND_CELLS\n";
  }
  CellsInfo cinfo;
  /* For each unique edge shared between patch pairs, process it. */
  Set<Edge> processed_edges;
  int np = pinfo.tot_patch();
  for (int p = 0; p < np; ++p) {
    for (int q = p + 1; q < np; ++q) {
      Edge e = pinfo.patch_patch_edge(p, q);
      if (!(e == Edge(-1, -1))) {
        if (!processed_edges.contains(e)) {
          processed_edges.add_new(e);
          find_cells_from_edge(tm, tmtopo, pinfo, cinfo, e);
        }
      }
    }
  }
  if (dbg_level > 0) {
    std::cout << "\nFIND_CELLS found " << cinfo.tot_cell() << " cells\nCells\n";
    for (int i = 0; i < cinfo.tot_cell(); ++i) {
      std::cout << i << ": " << cinfo.cell(i) << "\n";
    }
    std::cout << "Patches\n";
    for (int i = 0; i < pinfo.tot_patch(); ++i) {
      std::cout << i << ": " << pinfo.patch(i) << "\n";
    }
  }
  return cinfo;
}

/*
 * Find the ambient cell -- that is, the cell that is outside
 * all other cells.
 */
static int find_ambient_cell(const TriMesh &tm,
                             const TriMeshTopology &tmtopo,
                             const PatchesInfo pinfo)
{
  int dbg_level = 0;
  if (dbg_level > 0) {
    std::cout << "FIND_AMBIENT_CELL\n";
  }
  /* First find a vertex with the maximum x value. */
  int v_extreme = 0;
  mpq_class extreme_x = tm.vert[0].x;
  for (int i = 1; i < static_cast<int>(tm.vert.size()); ++i) {
    const mpq_class &cox = tm.vert[i].x;
    if (cox > extreme_x) {
      v_extreme = i;
      extreme_x = cox;
    }
  }
  if (dbg_level > 0) {
    std::cout << "v_extreme = " << v_extreme << "\n";
  }
  /* Find edge attached to v_extreme with max absolute slope
   * when projected onto the xy plane. That edge is guaranteed to
   * be on the convex hull of the mesh.
   */
  const Vector<Edge> &edges = tmtopo.vert_edges(v_extreme);
  const mpq_class extreme_y = tm.vert[v_extreme].y;
  Edge ehull(-1, -1);
  mpq_class max_abs_slope = -1;
  for (Edge e : edges) {
    const int v_other = (e.v0() == v_extreme) ? e.v1() : e.v0();
    const mpq3 &co_other = tm.vert[v_other];
    mpq_class delta_x = co_other.x - extreme_x;
    if (delta_x == 0) {
      /* Vertical slope. */
      ehull = e;
      break;
    }
    mpq_class abs_slope = abs((co_other.y - extreme_y) / delta_x);
    if (abs_slope > max_abs_slope) {
      ehull = e;
      max_abs_slope = abs_slope;
    }
  }
  if (dbg_level > 0) {
    std::cout << "ehull = " << ehull << " slope = " << max_abs_slope << "\n";
  }
  /* Sort triangles around ehull, including a dummy triangle that include a known point in ambient
   * cell. */
  mpq3 p_in_ambient = tm.vert[v_extreme];
  p_in_ambient.x += 1;
  const Vector<int> *ehull_edge_tris = tmtopo.edge_tris(ehull);
  const int dummy_vert = INT_MAX;
  const int dummy_tri = INT_MAX;
  IndexedTriangle dummytri = IndexedTriangle(ehull.v0(), ehull.v1(), dummy_vert, -1);
  Array<int> edge_tris(ehull_edge_tris->size() + 1);
  std::copy(ehull_edge_tris->begin(), ehull_edge_tris->end(), edge_tris.begin());
  edge_tris[edge_tris.size() - 1] = dummy_tri;
  Array<int> sorted_tris = sort_tris_around_edge(
      tm, tmtopo, ehull, edge_tris, edge_tris[0], &dummytri, &p_in_ambient);
  if (dbg_level > 0) {
    std::cout << "sorted tris = " << sorted_tris << "\n";
  }
  int *p_sorted_dummy = std::find(sorted_tris.begin(), sorted_tris.end(), dummy_tri);
  BLI_assert(p_sorted_dummy != sorted_tris.end());
  int dummy_index = p_sorted_dummy - sorted_tris.begin();
  int prev_tri = (dummy_index == 0) ? sorted_tris[sorted_tris.size() - 1] :
                                      sorted_tris[dummy_index - 1];
  int next_tri = (dummy_index == static_cast<int>(sorted_tris.size() - 1)) ?
                     sorted_tris[0] :
                     sorted_tris[dummy_index + 1];
  if (dbg_level > 0) {
    std::cout << "prev tri to dummy = " << prev_tri << ";  next tri to dummy = " << next_tri
              << "\n";
  }
  const Patch &prev_patch = pinfo.patch(pinfo.tri_patch(prev_tri));
  const Patch &next_patch = pinfo.patch(pinfo.tri_patch(next_tri));
  if (dbg_level > 0) {
    std::cout << "prev_patch = " << prev_patch << ", next_patch = " << next_patch << "\n";
  }
  BLI_assert(prev_patch.cell_above == next_patch.cell_above);
  if (dbg_level > 0) {
    std::cout << "FIND_AMBIENT_CELL returns " << prev_patch.cell_above << "\n";
  }
  return prev_patch.cell_above;
}

/* Starting with ambient cell c_ambient, with all zeros for winding numbers,
 * propagate winding numbers to all the other cells.
 * There will be a vector of nshapes winding numbers in each cell, one per
 * input shape.
 * As one crosses a patch into a new cell, the original shape (mesh part)
 * that that patch was part of dictates which winding number changes.
 * The shape_fn(triangle_number) function should return the shape that the
 * triangle is part of.
 * Also, as soon as the winding numbers for a cell are set, use bool_optype
 * to decide whether that cell is included or excluded from the boolean output.
 * If included, the cell's flag will be set to true.
 */
static void propagate_windings_and_flag(PatchesInfo &pinfo,
                                        CellsInfo &cinfo,
                                        int c_ambient,
                                        int bool_optype,
                                        int nshapes,
                                        std::function<int(int)> shape_fn)
{
  int dbg_level = 0;
  if (dbg_level > 0) {
    std::cout << "PROPAGATE_WINDINGS, ambient cell = " << c_ambient << "\n";
  }
  Cell &cell_ambient = cinfo.cell(c_ambient);
  cell_ambient.seed_ambient_winding();
  /* Use a vector as a queue. It can't grow bigger than number of cells. */
  Vector<int> queue;
  queue.reserve(cinfo.tot_cell());
  int queue_head = 0;
  queue.append(c_ambient);
  while (queue_head < static_cast<int>(queue.size())) {
    int c = queue[queue_head++];
    if (dbg_level > 1) {
      std::cout << "process cell " << c << "\n";
    }
    Cell &cell = cinfo.cell(c);
    for (int p : cell.patches()) {
      Patch &patch = pinfo.patch(p);
      bool p_above_c = patch.cell_below == c;
      int c_neighbor = p_above_c ? patch.cell_above : patch.cell_below;
      if (dbg_level > 1) {
        std::cout << "  patch " << p << " p_above_c = " << p_above_c << "\n";
        std::cout << "    c_neighbor = " << c_neighbor << "\n";
      }
      Cell &cell_neighbor = cinfo.cell(c_neighbor);
      if (!cell_neighbor.winding_assigned()) {
        int winding_delta = p_above_c ? -1 : 1;
        int t = patch.tri(0);
        int shape = shape_fn(t);
        BLI_assert(shape < nshapes);
        if (dbg_level > 1) {
          std::cout << "    representative tri " << t << ": in shape " << shape << "\n";
        }
        cell_neighbor.set_winding_and_flag(cell, shape, winding_delta, bool_optype);
        if (dbg_level > 1) {
          std::cout << "    now cell_neighbor = " << cell_neighbor << "\n";
        }
        queue.append(c_neighbor);
        BLI_assert(queue.size() <= cinfo.tot_cell());
      }
    }
  }
  if (dbg_level > 0) {
    std::cout << "\nPROPAGATE_WINDINGS result\n";
    for (int i = 0; i < cinfo.tot_cell(); ++i) {
      std::cout << i << ": " << cinfo.cell(i) << "\n";
    }
  }
}

/* Given an array of winding numbers, where the ith entry is a cell's winding
 * number with respect to input shape (mesh part) i, return true if the
 * cell should be included in the output of the boolean operation.
 *   Intersection: all the winding numbers must be nonzero.
 *   Union: at least one winding number must be nonzero.
 *   Difference (first shape minus the rest): first winding number must be nonzero
 *      and the rest must have at least one zero winding number.
 */
static bool apply_bool_op(int bool_optype, const Array<int> &winding)
{
  int nw = static_cast<int>(winding.size());
  BLI_assert(nw > 0);
  switch (bool_optype) {
    case BOOLEAN_ISECT: {
      for (int i = 0; i < nw; ++i) {
        if (winding[i] == 0) {
          return false;
        }
      }
      return true;
    } break;
    case BOOLEAN_UNION: {
      for (int i = 0; i < nw; ++i) {
        if (winding[i] != 0) {
          return true;
        }
      }
      return false;
    } break;
    case BOOLEAN_DIFFERENCE: {
      /* if nw > 2, make it shape 0 minus the union of the rest. */
      if (winding[0] == 0) {
        return false;
      }
      if (nw == 1) {
        return true;
      }
      for (int i = 1; i < nw; ++i) {
        if (winding[i] == 0) {
          return true;
        }
      }
      return false;
    } break;
    default:
      return false;
  }
}

/* Extract the output mesh from tm_subdivided and return it as a new mesh.
 * The cells in cinfo must have cells-to-be-retained flagged.
 * We keep only triangles between flagged and unflagged cells.
 * We flip the normals of any triangle that has a flagged cell above
 * and an unflagged cell below.
 * For all stacks of exact duplicate coplanar triangles, add up orientations
 * as +1 or -1 for each according to CCW vs CW. If the result is nonzero,
 * keep one copy with orientation chosen according to the dominant sign.
 */
static TriMesh extract_from_flag_diffs(const TriMesh &tm_subdivided,
                                       const PatchesInfo &pinfo,
                                       const CellsInfo &cinfo)
{
  const int dbg_level = 0;
  if (dbg_level > 0) {
    std::cout << "\nEXTRACT_FROM_FLAG_DIFFS\n";
  }
  int tri_tot = static_cast<int>(tm_subdivided.tri.size());
  int vert_tot = static_cast<int>(tm_subdivided.vert.size());
  Array<bool> need_vert(vert_tot, false);
  Array<bool> need_tri(tri_tot, false);
  Array<bool> flip_tri(tri_tot, false);
  for (int t = 0; t < tri_tot; ++t) {
    int p = pinfo.tri_patch(t);
    const Patch &patch = pinfo.patch(p);
    const Cell &cell_above = cinfo.cell(patch.cell_above);
    const Cell &cell_below = cinfo.cell(patch.cell_below);
    if (dbg_level > 0) {
      std::cout << "tri " << t << ": cell_above=" << patch.cell_above
                << " cell_below=" << patch.cell_below << "\n";
      std::cout << " flag_above=" << cell_above.flag() << " flag_below=" << cell_below.flag()
                << "\n";
    }
    if (cell_above.flag() ^ cell_below.flag()) {
      need_tri[t] = true;
      if (dbg_level > 0) {
        std::cout << "need tri " << t << "\n";
      }
      if (cell_above.flag()) {
        flip_tri[t] = true;
      }
      const IndexedTriangle &tri = tm_subdivided.tri[t];
      for (int i = 0; i < 3; ++i) {
        need_vert[tri[i]] = true;
        if (dbg_level > 0) {
          std::cout << "need vert " << tri[i] << "\n";
        }
      }
    }
  }
  auto iftrue = [](bool v) { return v; };
  int out_vert_tot = std::count_if(need_vert.begin(), need_vert.end(), iftrue);
  int out_tri_tot = std::count_if(need_tri.begin(), need_tri.end(), iftrue);
  TriMesh tm_out;
  tm_out.vert = Array<mpq3>(out_vert_tot);
  tm_out.tri = Array<IndexedTriangle>(out_tri_tot);
  Array<int> in_v_to_out_v(vert_tot);
  int out_v_index = 0;
  for (int v = 0; v < vert_tot; ++v) {
    if (need_vert[v]) {
      BLI_assert(out_v_index < out_vert_tot);
      in_v_to_out_v[v] = out_v_index;
      tm_out.vert[out_v_index++] = tm_subdivided.vert[v];
    }
    else {
      in_v_to_out_v[v] = -1;
    }
  }
  BLI_assert(out_v_index == out_vert_tot);
  int out_t_index = 0;
  for (int t = 0; t < tri_tot; ++t) {
    if (need_tri[t]) {
      BLI_assert(out_t_index < out_tri_tot);
      const IndexedTriangle &tri = tm_subdivided.tri[t];
      int v0 = in_v_to_out_v[tri.v0()];
      int v1 = in_v_to_out_v[tri.v1()];
      int v2 = in_v_to_out_v[tri.v2()];
      if (flip_tri[t]) {
        std::swap<int>(v1, v2);
      }
      tm_out.tri[out_t_index++] = IndexedTriangle(v0, v1, v2, tri.orig());
    }
  }
  return tm_out;
}

static const char *bool_optype_name(int bool_optype)
{
  switch (bool_optype) {
    case BOOLEAN_NONE:
      return "none";
      break;
    case BOOLEAN_ISECT:
      return "intersect";
      break;
    case BOOLEAN_UNION:
      return "union";
      break;
    case BOOLEAN_DIFFERENCE:
      return "difference";
    default:
      return "<unknown>";
  }
}

/*
 * This function does a boolean operation on nshapes inputs.
 * All the shapes are combined in tm_in.
 * The shape_fn function should take a triangle index in tm_in and return
 * a number in the range 0 to nshapes-1, to say which shape that triangle is in.
 */
static TriMesh nary_boolean(const TriMesh &tm_in,
                            int bool_optype,
                            int nshapes,
                            std::function<int(int)> shape_fn)
{
  constexpr int dbg_level = 0;
  if (dbg_level > 0) {
    std::cout << "BOOLEAN of " << nshapes << " operand" << (nshapes == 1 ? "" : "s")
              << " op=" << bool_optype_name(bool_optype) << "\n";
  }
  if (tm_in.vert.size() == 0 || tm_in.tri.size() == 0) {
    return TriMesh(tm_in);
  }
  TriMesh tm_si = trimesh_self_intersect(tm_in);
  /* It is possible for tm_si to be empty if all the input triangles are bogus/degenerate. */
  if (tm_si.tri.size() == 0 || bool_optype == BOOLEAN_NONE) {
    return tm_si;
  }
  auto si_shape_fn = [shape_fn, tm_si](int t) { return shape_fn(tm_si.tri[t].orig()); };
  if (dbg_level > 1) {
    write_obj_trimesh(tm_si.vert, tm_si.tri, "boolean_tm_input");
    std::cout << "boolean tm input:\n";
    for (int t = 0; t < static_cast<int>(tm_si.tri.size()); ++t) {
      std::cout << "tri " << t << " = " << tm_si.tri[t] << " shape " << si_shape_fn(t) << "\n";
    }
  }
  TriMeshTopology tm_si_topo(&tm_si);
  PatchesInfo pinfo = find_patches(tm_si, tm_si_topo);
  CellsInfo cinfo = find_cells(tm_si, tm_si_topo, pinfo);
  cinfo.init_windings(nshapes);
  int c_ambient = find_ambient_cell(tm_si, tm_si_topo, pinfo);
  if (c_ambient == -1) {
    /* TODO: find a way to propagate this error to user properly. */
    std::cout << "Could not find an ambient cell; input not valid?\n";
    return TriMesh(tm_si);
  }
  propagate_windings_and_flag(pinfo, cinfo, c_ambient, bool_optype, nshapes, si_shape_fn);
  TriMesh tm_out = extract_from_flag_diffs(tm_si, pinfo, cinfo);
  if (dbg_level > 1) {
    write_obj_trimesh(tm_out.vert, tm_out.tri, "boolean_tm_output");
  }
  return tm_out;
}

static TriMesh self_boolean(const TriMesh &tm_in, int bool_optype)
{
  return nary_boolean(tm_in, bool_optype, 1, [](int UNUSED(t)) { return 0; });
}

static TriMesh binary_boolean(const TriMesh &tm_in_a, const TriMesh &tm_in_b, int bool_optype)
{
  /* Just combine the two pieces. We can tell by original triangle number which side it came
   * from.
   */
  TriMesh tm_in = concat_trimeshes(tm_in_a, tm_in_b);
  int b_tri_start = static_cast<int>(tm_in_a.tri.size());
  auto shape_fn = [b_tri_start](int t) { return (t >= b_tri_start ? 1 : 0); };
  return nary_boolean(tm_in, bool_optype, 2, shape_fn);
}

static Array<IndexedTriangle> triangulate_poly(int orig_face,
                                               const Array<int> &face,
                                               const Array<mpq3> &vert)
{
  int flen = static_cast<int>(face.size());
  CDT_input<mpq_class> cdt_in;
  cdt_in.vert = Array<mpq2>(flen);
  cdt_in.face = Array<Vector<int>>(1);
  cdt_in.face[0].reserve(flen);
  Array<mpq3> face_verts(flen);
  for (int i = 0; i < flen; ++i) {
    cdt_in.face[0].append(i);
    face_verts[i] = vert[face[i]];
  }
  /* Project poly along dominant axis of normal to get 2d coords. */
  mpq3 poly_normal = mpq3::cross_poly(face_verts.begin(), flen);
  int axis = mpq3::dominant_axis(poly_normal);
  /* If project down y axis as opposed to x or z, the orientation
   * of the polygon will be reversed.
   */
  bool rev = (axis == 1);
  for (int i = 0; i < flen; ++i) {
    int ii = rev ? flen - i - 1 : i;
    mpq2 &p2d = cdt_in.vert[ii];
    int k = 0;
    for (int j = 0; j < 3; ++j) {
      if (j != axis) {
        p2d[k++] = face_verts[ii][j];
      }
    }
  }
  CDT_result<mpq_class> cdt_out = delaunay_2d_calc(cdt_in, CDT_INSIDE);
  int n_tris = static_cast<int>(cdt_out.face.size());
  Array<IndexedTriangle> ans(n_tris);
  for (int t = 0; t < n_tris; ++t) {
    /* Assume no input verts to CDT were merged. Not necessarily true. FIXME. */
    BLI_assert(cdt_out.vert.size() == cdt_in.vert.size());
    int v0_out = cdt_out.face[t][0];
    int v1_out = cdt_out.face[t][1];
    int v2_out = cdt_out.face[t][2];
    int v0 = face[v0_out];
    int v1 = face[v1_out];
    int v2 = face[v2_out];
    ans[t] = IndexedTriangle(v0, v1, v2, orig_face);
  }
  return ans;
}

static void triangulate_polymesh(PolyMesh &pm)
{
  int face_tot = static_cast<int>(pm.face.size());
  Array<Array<IndexedTriangle>> face_tris(face_tot);
  for (int f = 0; f < face_tot; ++f) {
    /* Tesselate face f, following plan similar to BM_face_calc_tesselation. */
    int flen = static_cast<int>(pm.face[f].size());
    if (flen == 3) {
      face_tris[f] = Array<IndexedTriangle>{
          IndexedTriangle(pm.face[f][0], pm.face[f][1], pm.face[f][2], f)};
    }
    else if (flen == 4) {
      face_tris[f] = Array<IndexedTriangle>{
          IndexedTriangle(pm.face[f][0], pm.face[f][1], pm.face[f][2], f),
          IndexedTriangle(pm.face[f][0], pm.face[f][2], pm.face[f][3], f)};
    }
    else {
      face_tris[f] = triangulate_poly(f, pm.face[f], pm.vert);
    }
  }
  pm.triangulation = face_tris;
}

/* Will add triangulation if it isn't already there. */
static TriMesh trimesh_from_polymesh(PolyMesh &pm)
{
  TriMesh ans;
  ans.vert = pm.vert;
  if (pm.triangulation.size() == 0) {
    triangulate_polymesh(pm);
  }
  const Array<Array<IndexedTriangle>> &tri_arrays = pm.triangulation;
  int tot_tri = 0;
  for (const Array<IndexedTriangle> &a : tri_arrays) {
    tot_tri += static_cast<int>(a.size());
  }
  ans.tri = Array<IndexedTriangle>(tot_tri);
  int t = 0;
  for (const Array<IndexedTriangle> &a : tri_arrays) {
    for (uint i = 0; i < a.size(); ++i) {
      ans.tri[t++] = a[i];
    }
  }

  return ans;
}

/* For Debugging. */
void write_obj_polymesh(const Array<mpq3> &vert,
                        const Array<Array<int>> &face,
                        const std::string &objname)
{
  constexpr const char *objdir = "/tmp/";
  if (face.size() == 0) {
    return;
  }

  std::string fname = std::string(objdir) + objname + std::string(".obj");
  std::ofstream f;
  f.open(fname);
  if (!f) {
    std::cout << "Could not open file " << fname << "\n";
    return;
  }

  for (const mpq3 &vco : vert) {
    double3 dv(vco[0].get_d(), vco[1].get_d(), vco[2].get_d());
    f << "v " << dv[0] << " " << dv[1] << " " << dv[2] << "\n";
  }
  int i = 0;
  for (const Array<int> &face_verts : face) {
    /* OBJ files use 1-indexing for vertices. */
    f << "f ";
    for (int v : face_verts) {
      f << v + 1 << " ";
    }
    f << "\n";
    ++i;
  }
  f.close();
}

/* If tri1 and tri2 have a common edge (in opposite orientation), return the indices into tri1 and
 * tri2 where that common edge starts. Else return (-1,-1).
 */
static std::pair<int, int> find_tris_common_edge(const IndexedTriangle &tri1,
                                                 const IndexedTriangle &tri2)
{
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      if (tri1[(i + 1) % 3] == tri2[j] && tri1[i] == tri2[(j + 1) % 3]) {
        return std::pair<int, int>(i, j);
      }
    }
  }
  return std::pair<int, int>(-1, -1);
}

struct MergeEdge {
  /* left_face and right_face are indices into FaceMergeState->face. */
  int left_face = -1;
  int right_face = -1;
  int v1 = -1;
  int v2 = -1;
  double len_squared = 0.0;
  bool dissolvable = false;

  MergeEdge() = default;

  MergeEdge(int va, int vb)
  {
    if (va < vb) {
      this->v1 = va;
      this->v2 = vb;
    }
    else {
      this->v1 = vb;
      this->v2 = va;
    }
  };
};

struct MergeFace {
  /* vert contents are vertex indices in underlying TriMesh. */
  Vector<int> vert;
  /* edge contents are edge indices in FaceMergeState, paralleling vert array. */
  Vector<int> edge;
  /* If not -1, merge_to gives an index in FaceMergeState that this is merged to. */
  int merge_to = -1;
};
struct FaceMergeState {
  Vector<MergeFace> face;
  Vector<MergeEdge> edge;
  Map<std::pair<int, int>, int> edge_map;
};

static std::ostream &operator<<(std::ostream &os, const FaceMergeState &fms)
{
  os << "faces:\n";
  for (uint f = 0; f < fms.face.size(); ++f) {
    std::cout << f << ": verts " << fms.face[f].vert << "\n";
    std::cout << "    edges " << fms.face[f].edge << "\n";
    std::cout << "    merge_to = " << fms.face[f].merge_to << "\n";
  }
  os << "\nedges:\n";
  for (uint e = 0; e < fms.edge.size(); ++e) {
    std::cout << e << ": (" << fms.edge[e].v1 << "," << fms.edge[e].v2
              << ") left=" << fms.edge[e].left_face << " right=" << fms.edge[e].right_face
              << " dis=" << fms.edge[e].dissolvable << "\n";
  }
  return os;
}

/* Does (av1,av2) overlap (bv1,bv2) at more than a single point? */
static bool segs_overlap(const mpq3 av1, const mpq3 av2, const mpq3 bv1, const mpq3 bv2)
{
  mpq3 a = av2 - av1;
  mpq3 b = bv2 - bv1;
  mpq3 ab = mpq3::cross(a, b);
  if (ab.x == 0 && ab.y == 0 && ab.z == 0) {
    /*
     * Lines containing a and b are collinear.
     * Find r and s such that bv1 = av1 + r a   and    bv2 = av1 + s a.
     * We can do this in 1D, projected onto any axis where a is not zero.
     */
    int axis = mpq3::dominant_axis(a);
    if (a[axis] == 0 || (b.x == 0 && b.y == 0 && b.z == 0)) {
      /* One or both segs is a point --> cannot intersect in more than a point. */
      return false;
    }
    mpq_class s = (bv1[axis] - av1[axis]) / a[axis];
    mpq_class r = (bv2[axis] - av1[axis]) / a[axis];
    /* Do intervals [0, 1] and [r,s] overlap nontrivially? First make r < s. */
    if (s < r) {
      SWAP(mpq_class, r, s);
    }
    if (r >= 1 || s <= 0) {
      return false;
    }
    else {
      /* We know intersection interval starts strictly before av2 and ends strictly after av1. */
      return r != s;
    }
  }
  return false;
}

/* Any edge in fms that does not overlap an edge in pm_in is dissolvable.
 * TODO: implement a much more efficient way of doing this O(n^2) algorithm!
 * Probably eventually will just plumb through edge representatives from beginning
 * to tm and can dump this altogether.
 * We set len_squared here, which we only need for dissolvable edges.
 */
static void find_dissolvable_edges(FaceMergeState *fms, const TriMesh &tm, const PolyMesh &pm_in)
{
  for (uint me_index = 0; me_index < fms->edge.size(); ++me_index) {
    MergeEdge &me = fms->edge[me_index];
    const mpq3 &me_v1 = tm.vert[me.v1];
    const mpq3 &me_v2 = tm.vert[me.v2];
    bool me_dis = true;
    for (uint pm_f_index = 0; pm_f_index < pm_in.face.size() && me_dis; ++pm_f_index) {
      const Array<int> &pm_f = pm_in.face[pm_f_index];
      int f_size = static_cast<int>(pm_f.size());
      for (int i = 0; i < f_size && me_dis; ++i) {
        int inext = (i + 1) % f_size;
        const mpq3 &pm_v1 = pm_in.vert[pm_f[i]];
        const mpq3 &pm_v2 = pm_in.vert[pm_f[inext]];
        if (segs_overlap(me_v1, me_v2, pm_v1, pm_v2)) {
          me_dis = false;
        }
      }
    }
    me.dissolvable = me_dis;
    if (me_dis) {
      mpq3 evec = me_v2 - me_v1;
      me.len_squared = evec.length_squared().get_d();
    }
  }
}

static void init_face_merge_state(FaceMergeState *fms,
                                  const Vector<int> &tris,
                                  const TriMesh &tm,
                                  const PolyMesh &pm_in)
{
  const int dbg_level = 0;
  /* Reserve enough faces and edges so that neither will have to resize. */
  fms->face.reserve(tris.size() + 1);
  fms->edge.reserve((3 * tris.size()));
  fms->edge_map.reserve(3 * tris.size());
  if (dbg_level > 0) {
    std::cout << "\nINIT_FACE_MERGE_STATE\n";
  }
  for (uint t = 0; t < tris.size(); ++t) {
    MergeFace mf;
    const IndexedTriangle &tri = tm.tri[tris[t]];
    mf.vert.append(tri.v0());
    mf.vert.append(tri.v1());
    mf.vert.append(tri.v2());
    int f = static_cast<int>(fms->face.append_and_get_index(mf));
    for (int i = 0; i < 3; ++i) {
      int inext = (i + 1) % 3;
      MergeEdge new_me(mf.vert[i], mf.vert[inext]);
      std::pair<int, int> canon_vs(new_me.v1, new_me.v2);
      int me_index = fms->edge_map.lookup_default(canon_vs, -1);
      if (me_index == -1) {
        fms->edge.append(new_me);
        me_index = static_cast<int>(fms->edge.size()) - 1;
        fms->edge_map.add_new(canon_vs, me_index);
      }
      MergeEdge &me = fms->edge[me_index];
      /* This face is left or right depending on orientation of edge. */
      if (me.v1 == mf.vert[i]) {
        BLI_assert(me.left_face == -1);
        fms->edge[me_index].left_face = f;
      }
      else {
        BLI_assert(me.right_face == -1);
        fms->edge[me_index].right_face = f;
      }
      fms->face[f].edge.append(me_index);
    }
  }
  find_dissolvable_edges(fms, tm, pm_in);
  if (dbg_level > 0) {
    std::cout << *fms;
  }
}

/* To have a valid bmesh, there are constraints on what edges can be removed.
 * We cannot remove an edge if (a) it would create two disconnected boundary parts
 * (which will happen if there's another edge sharing the same two faces);
 * or (b) it would create a face with a repeated vertex.
 */
static bool dissolve_leaves_valid_bmesh(FaceMergeState *fms,
                                        const MergeEdge &me,
                                        int me_index,
                                        const MergeFace &mf_left,
                                        const MergeFace &mf_right)
{
  int a_edge_start = mf_left.edge.first_index_of_try(me_index);
  int b_edge_start = mf_right.edge.first_index_of_try(me_index);
  BLI_assert(a_edge_start != -1 && b_edge_start != -1);
  int alen = static_cast<int>(mf_left.vert.size());
  int blen = static_cast<int>(mf_right.vert.size());
  int b_left_face = me.right_face;
  bool ok = true;
  /* Is there another edge, not me, in A's face, whose right face is B's left? */
  for (int a_e_index = (a_edge_start + 1) % alen; ok && a_e_index != a_edge_start;
       a_e_index = (a_e_index + 1) % alen) {
    const MergeEdge &a_me_cur = fms->edge[mf_left.edge[a_e_index]];
    if (a_me_cur.right_face == b_left_face) {
      ok = false;
    }
  }
  /* Is there a vert in A, not me.v1 or me.v2, that is also in B?
   * One could avoid this O(n^2) algorithm if had a structure saying which faces a vertex touches.
   */
  for (int a_v_index = 0; ok && a_v_index < alen; ++a_v_index) {
    int a_v = mf_left.vert[a_v_index];
    if (a_v != me.v1 && a_v != me.v2) {
      for (int b_v_index = 0; b_v_index < blen; ++b_v_index) {
        int b_v = mf_right.vert[b_v_index];
        if (a_v == b_v) {
          ok = false;
        }
      }
    }
  }
  return ok;
}

/* mf_left and mf_right should share a MergeEdge me, having index me_index.
 * We change mf_left to remove edge me and insert the appropriate edges of
 * mf_right in between the start and end vertices of that edge.
 * We change the left face of the spliced-in edges to be mf_left's index.
 * We mark the merge_to property of mf_right, which is now in essence deleted.
 */
static void splice_faces(
    FaceMergeState *fms, MergeEdge &me, int me_index, MergeFace &mf_left, MergeFace &mf_right)
{
  int a_edge_start = mf_left.edge.first_index_of_try(me_index);
  int b_edge_start = mf_right.edge.first_index_of_try(me_index);
  BLI_assert(a_edge_start != -1 && b_edge_start != -1);
  int alen = static_cast<int>(mf_left.vert.size());
  int blen = static_cast<int>(mf_right.vert.size());
  Vector<int> splice_vert;
  Vector<int> splice_edge;
  splice_vert.reserve(alen + blen - 2);
  splice_edge.reserve(alen + blen - 2);
  int ai = 0;
  while (ai < a_edge_start) {
    splice_vert.append(mf_left.vert[ai]);
    splice_edge.append(mf_left.edge[ai]);
    ++ai;
  }
  int bi = b_edge_start + 1;
  while (bi != b_edge_start) {
    if (bi >= blen) {
      bi = 0;
      if (bi == b_edge_start) {
        break;
      }
    }
    splice_vert.append(mf_right.vert[bi]);
    splice_edge.append(mf_right.edge[bi]);
    if (mf_right.vert[bi] == fms->edge[mf_right.edge[bi]].v1) {
      fms->edge[mf_right.edge[bi]].left_face = me.left_face;
    }
    else {
      fms->edge[mf_right.edge[bi]].right_face = me.left_face;
    }
    ++bi;
  }
  ai = a_edge_start + 1;
  while (ai < alen) {
    splice_vert.append(mf_left.vert[ai]);
    splice_edge.append(mf_left.edge[ai]);
    ++ai;
  }
  mf_right.merge_to = me.left_face;
  mf_left.vert = splice_vert;
  mf_left.edge = splice_edge;
  me.left_face = -1;
  me.right_face = -1;
}

/* Given that fms has been properly initialized to contain a set of faces that
 * together form a face or part of a face of the original PolyMesh, and that
 * it has properly recorded with faces are dissolvable, dissolve as many edges as possible.
 * We try to dissolve in decreasing order of edge length, so that it is more likely
 * that the final output doesn't have awkward looking long edges with extreme angles.
 */
static void do_dissolve(FaceMergeState *fms)
{
  const int dbg_level = 0;
  if (dbg_level > 1) {
    std::cout << "\nDO_DISSOLVE\n";
  }
  Vector<int> dissolve_edges;
  for (uint e = 0; e < fms->edge.size(); ++e) {
    if (fms->edge[e].dissolvable) {
      dissolve_edges.append(e);
    }
  }
  if (dissolve_edges.size() == 0) {
    return;
  }
  /* Things look nicer if we dissolve the longer edges first. */
  std::sort(
      dissolve_edges.begin(), dissolve_edges.end(), [fms](const int &a, const int &b) -> bool {
        return (fms->edge[a].len_squared > fms->edge[b].len_squared);
      });
  if (dbg_level > 0) {
    std::cout << "Sorted dissolvable edges: " << dissolve_edges << "\n";
  }
  for (int me_index : dissolve_edges) {
    MergeEdge &me = fms->edge[me_index];
    if (me.left_face == -1 || me.right_face == -1) {
      continue;
    }
    MergeFace &mf_left = fms->face[me.left_face];
    MergeFace &mf_right = fms->face[me.right_face];
    if (!dissolve_leaves_valid_bmesh(fms, me, me_index, mf_left, mf_right)) {
      continue;
    }
    if (dbg_level > 0) {
      std::cout << "Removing edge " << me_index << "\n";
    }
    splice_faces(fms, me, me_index, mf_left, mf_right);
    if (dbg_level > 1) {
      std::cout << "state after removal:\n";
      std::cout << *fms;
    }
  }
}

/* Given that tris form a triangulation of a face or part of a face that was in pm_in,
 * merge as many of the triangles together as possible, by dissolving the edges between them.
 * We can only dissolve triangulation edges that don't overlap real input edges, and we
 * can only dissolve them if doing so leaves the remaining faces able to create valid BMesh.
 */
static Vector<Vector<int>> merge_tris_for_face(Vector<int> tris,
                                               const TriMesh &tm,
                                               const PolyMesh &pm_in)
{
  /* Only approximately right. TODO: fixme. */
  Vector<Vector<int>> ans;
  if (tris.size() == 2) {
    /* Is this a case where quad with one diagonal remained unchanged? */
    /* TODO: could be diagonal is not dissolvable if this isn't whole original face. FIXME.
     * We could just use the code below for this case too, but this seems likely to be
     * such a common case that it is worth trying to handle specially, with less work.
     */
    const IndexedTriangle &tri1 = tm.tri[tris[0]];
    const IndexedTriangle &tri2 = tm.tri[tris[1]];
    std::pair<int, int> estarts = find_tris_common_edge(tri1, tri2);
    if (estarts.first != -1) {
      ans.append(Vector<int>());
      int i0 = estarts.first;
      int i1 = (i0 + 1) % 3;
      int i2 = (i0 + 2) % 3;
      int j2 = (estarts.second + 2) % 3;
      ans[0].append(tri1[i1]);
      ans[0].append(tri1[i2]);
      ans[0].append(tri1[i0]);
      ans[0].append(tri2[j2]);
      return ans;
    }
  }
  FaceMergeState fms;
  init_face_merge_state(&fms, tris, tm, pm_in);
  do_dissolve(&fms);
  for (uint f = 0; f < fms.face.size(); ++f) {
    const MergeFace &mf = fms.face[f];
    if (mf.merge_to == -1) {
      ans.append(Vector<int>());
      ans[ans.size() - 1] = mf.vert;
    }
  }
  return ans;
}

/* Return an array, paralleling pm_out.vert, saying which vertices can be dissolved.
 * A vertex v can be dissolved if (a) it is not an input vertex; (b) it has valence 2;
 * and (c) if v's two neighboring vertices are u and w, then (u,v,w) forms a straight line.
 * Return the number of dissolvable vertices in r_count_dissolve.
 */
static Array<bool> find_dissolve_verts(const PolyMesh &pm_out,
                                       const PolyMesh &pm_in,
                                       int *r_count_dissolve)
{
  /* Start assuming all can be dissolved, and disprove that for all the cases where it is false. */
  Array<bool> dissolve(pm_out.vert.size(), true);
  /* To test "is not an input vertex", make a set of all input vertices. */
  Set<mpq3> input_verts;
  input_verts.reserve(pm_in.vert.size());
  for (uint v_in = 0; v_in < pm_in.vert.size(); ++v_in) {
    input_verts.add(pm_in.vert[v_in]);
  }
  for (uint v_out = 0; v_out < pm_out.vert.size(); ++v_out) {
    if (input_verts.contains(pm_out.vert[v_out])) {
      dissolve[v_out] = false;
    }
  }
  Array<std::pair<int, int>> neighbors(pm_out.vert.size(), std::pair<int, int>(-1, -1));
  for (uint f = 0; f < pm_out.face.size(); ++f) {
    const Array<int> &face = pm_out.face[f];
    int flen = static_cast<int>(face.size());
    for (int i = 0; i < flen; ++i) {
      int fv = face[i];
      if (dissolve[fv]) {
        int n1 = face[(i + flen - 1) % flen];
        int n2 = face[(i + 1) % flen];
        int f_n1 = neighbors[fv].first;
        int f_n2 = neighbors[fv].second;
        if (f_n1 != -1) {
          /* Already has a neighbor in another face; can't dissolve unless they are the same. */
          if (!((n1 == f_n2 && n2 == f_n1) || (n1 == f_n1 && n2 == f_n2))) {
            /* Different neighbors, so can't dissolve. */
            dissolve[fv] = false;
          }
        }
        else {
          /* These are the first-seen neighbors. */
          neighbors[fv] = std::pair<int, int>(n1, n2);
        }
      }
    }
  }
  int count = 0;
  for (uint v_out = 0; v_out < pm_out.vert.size(); ++v_out) {
    if (dissolve[v_out]) {
      dissolve[v_out] = false; /* Will set back to true if final condition is satisfied. */
      const std::pair<int, int> &nbrs = neighbors[v_out];
      if (nbrs.first != -1) {
        BLI_assert(nbrs.second != -1);
        const mpq3 &co1 = pm_out.vert[nbrs.first];
        const mpq3 &co2 = pm_out.vert[nbrs.second];
        const mpq3 &co = pm_out.vert[v_out];
        mpq3 dir1 = co - co1;
        mpq3 dir2 = co2 - co;
        mpq3 cross = mpq3::cross(dir1, dir2);
        if (cross[0] == 0 && cross[1] == 0 && cross[2] == 0) {
          dissolve[v_out] = true;
          ++count;
        }
      }
    }
  }
  if (r_count_dissolve) {
    *r_count_dissolve = count;
  }
  return dissolve;
}

/* The dissolve array parallels the pm.vert array. Wherever it is true,
 * remove the corresponding vertex from pm.vert, and the vertices in
 * pm.faces to account for the close-up of the gaps in pm.vert.
 */
static void dissolve_verts(PolyMesh *pm, const Array<bool> dissolve)
{
  int tot_v_orig = static_cast<int>(pm->vert.size());
  Array<int> vmap(tot_v_orig);
  int v_mapped = 0;
  for (int v_orig = 0; v_orig < tot_v_orig; ++v_orig) {
    if (!dissolve[v_orig]) {
      vmap[v_orig] = v_mapped++;
    }
    else {
      vmap[v_orig] = -1;
    }
  }
  int tot_v_final = v_mapped;
  if (tot_v_final == tot_v_orig) {
    return;
  }
  Array<mpq3> vert_final(tot_v_final);
  for (int v_orig = 0; v_orig < tot_v_orig; ++v_orig) {
    int v_final = vmap[v_orig];
    if (v_final != -1) {
      vert_final[v_final] = pm->vert[v_orig];
    }
  }
  for (uint f = 0; f < pm->face.size(); ++f) {
    const Array<int> &face = pm->face[f];
    bool any_change = false;
    int flen = static_cast<int>(face.size());
    int ndeleted = 0;
    for (int i = 0; i < flen; ++i) {
      int v_mapped = vmap[face[i]];
      if (v_mapped == -1) {
        any_change = true;
        ++ndeleted;
      }
      if (v_mapped != face[i]) {
        any_change = true;
      }
    }
    if (any_change) {
      BLI_assert(flen - ndeleted >= 3);
      Array<int> new_face(flen - ndeleted);
      int new_face_i = 0;
      for (int i = 0; i < flen; ++i) {
        int v_mapped = vmap[face[i]];
        if (v_mapped != -1) {
          new_face[new_face_i++] = v_mapped;
        }
      }
      pm->face[f] = new_face;
    }
  }
  pm->vert = vert_final;
}

/* The main boolean function operates on TriMesh's and produces TriMesh's as output.
 * This function converts back into a PolyMesh, knowing that pm_in was the original PolyMesh input
 * that was converted into a TriMesh and then had the boolean operation done on it.
 * Most of the work here is to get rid of the triangulation edges and any added vertices
 * that were only added to attach now-removed triangulation edges.
 * Not all triangulation edges can be removed: if they ended up non-trivially overlapping a real
 * input edge, then we need to keep it. Also, some are necessary to make the output satisfy
 * the "valid BMesh" property: we can't produce output faces that have repeated vertices in them,
 * or have several disconnected boundaries (e.g., faces with holes).
 */
static PolyMesh polymesh_from_trimesh_with_dissolve(const TriMesh &tm_out, const PolyMesh &pm_in)
{
  const int dbg_level = 0;
  if (dbg_level > 0) {
    std::cout << "\nPOLYMESH_FROM_TRIMESH_WITH_DISSOLVE\n";
  }
  /* Gather all output triangles that are part of each input face.
   * face_output_tris[f] will be indices of triangles in tm_out
   * that have f as their original face.
   */
  int tot_in_face = static_cast<int>(pm_in.face.size());
  Array<Vector<int>> face_output_tris(tot_in_face);
  int tot_out_tri = static_cast<int>(tm_out.tri.size());
  for (int t = 0; t < tot_out_tri; ++t) {
    const IndexedTriangle &tri = tm_out.tri[t];
    int in_face = tri.orig();
    face_output_tris[in_face].append(t);
  }
  if (dbg_level > 1) {
    std::cout << "face_output_tris:\n";
    for (int f = 0; f < tot_in_face; ++f) {
      std::cout << f << ": " << face_output_tris[f];
      std::cout << " : ";
      for (uint i = 0; i < face_output_tris[f].size(); ++i) {
        const IndexedTriangle &otri = tm_out.tri[face_output_tris[f][i]];
        std::cout << otri << " ";
      }
      std::cout << "\n";
    }
  }
  /* Merge triangles that we can from face_output_tri to make faces for output.
   * face_output_face[f] will be subfaces (as vectors of vertex indices) that
   * make up whatever part of the boolean output remains of input face f.
   */
  Array<Vector<Vector<int>>> face_output_face(tot_in_face);
  int tot_out_face = 0;
  for (int in_f = 0; in_f < tot_in_face; ++in_f) {
    if (dbg_level > 1) {
      std::cout << "merge tris for face " << in_f << "\n";
    }
    face_output_face[in_f] = merge_tris_for_face(face_output_tris[in_f], tm_out, pm_in);
    tot_out_face += static_cast<int>(face_output_face[in_f].size());
  }
  PolyMesh pm_out;
  pm_out.vert = tm_out.vert;
  pm_out.face = Array<Array<int>>(tot_out_face);
  int out_f = 0;
  for (int in_f = 0; in_f < tot_in_face; ++in_f) {
    const Vector<Vector<int>> &f_faces = face_output_face[in_f];
    for (uint i = 0; i < f_faces.size(); ++i) {
      pm_out.face[out_f] = Array<int>(f_faces[i].size());
      std::copy(f_faces[i].begin(), f_faces[i].end(), pm_out.face[out_f].begin());
      ++out_f;
    }
  }
  /* Dissolve vertices that were (a) not original; and (b) now have valence 2 and
   * are between two other vertices that are exactly in line with them.
   * These were created because of triangulation edges that have been dissolved.
   */
  int count_dissolve;
  Array<bool> v_dissolve = find_dissolve_verts(pm_out, pm_in, &count_dissolve);
  if (count_dissolve > 0) {
    dissolve_verts(&pm_out, v_dissolve);
  }
  if (dbg_level > 1) {
    write_obj_polymesh(pm_out.vert, pm_out.face, "boolean_post_dissolve");
  }
  return pm_out;
}

/* Do the boolean operation bool_optype on the polygon mesh pm_in.
 * The boolean operation has nshapes input shapes. Each is a disjoint subset of the input polymesh.
 * The shape_fn argument, when applied to an input face argument, says which shape it is in
 * (should be a value from -1 to nshapes - 1: if -1, it is not part of any shape).
 * Sometimes the caller has already done a triangulation of the faces, so pm_in contains an
 * optional triangulation: parallel to each face, it gives a set of IndexedTriangles that
 * triangulate that face.
 * pm arg isn't const because we will add triangulation if it is not there. */
PolyMesh boolean(PolyMesh &pm_in, int bool_optype, int nshapes, std::function<int(int)> shape_fn)
{
  TriMesh tm_in = trimesh_from_polymesh(pm_in);
  TriMesh tm_out = nary_boolean(tm_in, bool_optype, nshapes, shape_fn);
  return polymesh_from_trimesh_with_dissolve(tm_out, pm_in);
}

}  // namespace meshintersect
}  // namespace blender

/*
 * Convert the C-style Boolean_trimesh_input into our internal C++ class for triangle meshes,
 * TriMesh.
 */
static blender::meshintersect::TriMesh trimesh_from_input(const Boolean_trimesh_input *in,
                                                          int side)
{
  constexpr int dbg_level = 0;
  BLI_assert(in != nullptr);
  blender::meshintersect::TriMesh tm_in;
  tm_in.vert = blender::Array<blender::mpq3>(in->vert_len);
  for (int v = 0; v < in->vert_len; ++v) {
    tm_in.vert[v] = blender::mpq3(
        in->vert_coord[v][0], in->vert_coord[v][1], in->vert_coord[v][2]);
  }
  tm_in.tri = blender::Array<blender::meshintersect::IndexedTriangle>(in->tri_len);
  for (int t = 0; t < in->tri_len; ++t) {
    tm_in.tri[t] = blender::meshintersect::IndexedTriangle(
        in->tri[t][0], in->tri[t][1], in->tri[t][2], t);
  }
  if (dbg_level > 0) {
    /* Output in format that can be pasted into test spec. */
    std::cout << "Input side " << side << "\n";
    std::cout << tm_in.vert.size() << " " << tm_in.tri.size() << "\n";
    for (uint v = 0; v < tm_in.vert.size(); ++v) {
      std::cout << "  " << tm_in.vert[v][0].get_d() << " " << tm_in.vert[v][1].get_d() << " "
                << tm_in.vert[v][2].get_d() << "\n";
    }
    for (uint t = 0; t < tm_in.tri.size(); ++t) {
      std::cout << "  " << tm_in.tri[t].v0() << " " << tm_in.tri[t].v1() << " "
                << tm_in.tri[t].v2() << "\n";
    }
    std::cout << "\n";
    blender::meshintersect::write_obj_trimesh(
        tm_in.vert, tm_in.tri, "boolean_input" + std::to_string(side));
  }
  return tm_in;
}

/* Do a boolean operation between one or two triangle meshes, and return the answer as another
 * triangle mesh. The in_b argument may be NULL, meaning that the caller wants a unary boolean
 * operation. If the bool_optype is BOOLEAN_NONE, this function just does the self intersection of
 * the one or two meshes. This is a C interface. The caller must call BLI_boolean_trimesh_free() on
 * the returned value when done with it.
 */
extern "C" Boolean_trimesh_output *BLI_boolean_trimesh(const Boolean_trimesh_input *in_a,
                                                       const Boolean_trimesh_input *in_b,
                                                       int bool_optype)
{
  constexpr int dbg_level = 0;
  bool is_binary = in_b != NULL;
  if (dbg_level > 0) {
    std::cout << "BLI_BOOLEAN_TRIMESH op=" << blender::meshintersect::bool_optype_name(bool_optype)
              << (is_binary ? " binary" : " unary") << "\n";
  }
  blender::meshintersect::TriMesh tm_in_a = trimesh_from_input(in_a, 0);
  blender::meshintersect::TriMesh tm_out;
  if (is_binary) {
    blender::meshintersect::TriMesh tm_in_b = trimesh_from_input(in_b, 1);
    tm_out = blender::meshintersect::binary_boolean(tm_in_a, tm_in_b, bool_optype);
  }
  else {
    tm_out = blender::meshintersect::self_boolean(tm_in_a, bool_optype);
  }
  if (dbg_level > 1) {
    blender::meshintersect::write_html_trimesh(
        tm_out.vert, tm_out.tri, "mesh_boolean_test.html", "after self_boolean");
  }
  int nv = tm_out.vert.size();
  int nt = tm_out.tri.size();
  Boolean_trimesh_output *output = static_cast<Boolean_trimesh_output *>(
      MEM_mallocN(sizeof(*output), __func__));
  output->vert_len = nv;
  output->tri_len = nt;
  output->vert_coord = static_cast<decltype(output->vert_coord)>(
      MEM_malloc_arrayN(nv, sizeof(output->vert_coord[0]), __func__));
  output->tri = static_cast<decltype(output->tri)>(
      MEM_malloc_arrayN(nt, sizeof(output->tri[0]), __func__));
  for (int v = 0; v < nv; ++v) {
    output->vert_coord[v][0] = static_cast<float>(tm_out.vert[v][0].get_d());
    output->vert_coord[v][1] = static_cast<float>(tm_out.vert[v][1].get_d());
    output->vert_coord[v][2] = static_cast<float>(tm_out.vert[v][2].get_d());
  }
  for (int t = 0; t < nt; ++t) {
    output->tri[t][0] = tm_out.tri[t].v0();
    output->tri[t][1] = tm_out.tri[t].v1();
    output->tri[t][2] = tm_out.tri[t].v2();
  }
  return output;
}

/* Free the memory used in the return value of BLI_boolean_trimesh. */
extern "C" void BLI_boolean_trimesh_free(Boolean_trimesh_output *output)
{
  MEM_freeN(output->vert_coord);
  MEM_freeN(output->tri);
  MEM_freeN(output);
}
