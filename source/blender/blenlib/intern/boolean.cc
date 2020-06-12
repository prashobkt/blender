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
#include <iostream>

#include "gmpxx.h"

#include "BLI_array.hh"
#include "BLI_assert.h"
#include "BLI_hash.hh"
#include "BLI_map.hh"
#include "BLI_math.h"
#include "BLI_mesh_intersect.hh"
#include "BLI_mpq3.hh"
#include "BLI_set.hh"
#include "BLI_span.hh"
#include "BLI_stack.hh"
#include "BLI_vector.hh"

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
  const int dbg_level = 1;
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

/* Partition the triangles of tm into Patches. */
static PatchesInfo find_patches(const TriMesh &tm, const TriMeshTopology &tmtopo)
{
  const int dbg_level = 1;
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
              cur_patch_grow.push(t_other);
            }
          }
          else {
            /* e is non-manifold. Set any patch-patch incidences we can. */
            const Vector<int> *etris = tmtopo.edge_tris(e);
            if (etris) {
              for (uint i = 0; i < etris->size(); ++i) {
                int t_other = (*etris)[i];
                if (t_other != tcand && pinfo.tri_is_assigned(t_other)) {
                  int p_other = pinfo.tri_patch(t_other);
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
    p = std::copy(g3.begin(), g3.end(), p);
    p = std::copy(g2.begin(), g2.end(), p);
    std::copy(g4.begin(), g4.end(), p);
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
        std::cout << "  assigned new cell " << c << " to both\n";
      }
    }
    else if (*r_follow_cell != -1 && *rnext_prev_cell == -1) {
      int c = *r_follow_cell;
      *rnext_prev_cell = c;
      cinfo.cell(c).add_patch(rnext_index);
      if (dbg_level > 0) {
        std::cout << "  assigned r_follow_cell " << c << " to other";
      }
    }
    else if (*r_follow_cell == -1 && *rnext_prev_cell != -1) {
      int c = *rnext_prev_cell;
      *r_follow_cell = c;
      cinfo.cell(c).add_patch(r_index);
      if (dbg_level > 0) {
        std::cout << "  assigned rnext_prev_cell " << c << " to other";
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
  const int dbg_level = 1;
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
                             const PatchesInfo pinfo,
                             const CellsInfo cinfo)
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
  int next_tri = (dummy_index == static_cast<int>(sorted_tris.size())) ?
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
 * As one crosses a patch into a new cell, the original shape (mesh part)
 * that that patch was part of dictates which winding number changes.
 * Also, as soon as the winding numbers for a cell are set, use bool_optype
 * to decide whether that cell is included or excluded from the boolean output.
 * If included, the cell's flag will be set to true.
 */
static void propagate_windings_and_flag(PatchesInfo &pinfo,
                                        CellsInfo &cinfo,
                                        int c_ambient,
                                        int bool_optype)
{
  int dbg_level = 1;
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
        int shape = 0; /* TODO: recover shape from a triangle in p. */
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
        if (winding[0] == 0) {
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

static TriMesh self_boolean(const TriMesh &tm_in, int bool_optype)
{
  constexpr int dbg_level = 0;
  if (dbg_level > 0) {
    std::cout << "\nSELF_BOOLEAN op=" << bool_optype << "\n";
  }
  if (tm_in.vert.size() == 0 || tm_in.tri.size() == 0) {
    return tm_in;
  }
  TriMesh tm_si = trimesh_self_intersect(tm_in);
  if (bool_optype == BOOLEAN_NONE) {
    return tm_si;
  }
  TriMeshTopology tm_si_topo(&tm_si);
  PatchesInfo pinfo = find_patches(tm_si, tm_si_topo);
  CellsInfo cinfo = find_cells(tm_si, tm_si_topo, pinfo);
  cinfo.init_windings(1);
  int c_ambient = find_ambient_cell(tm_si, tm_si_topo, pinfo, cinfo);
  propagate_windings_and_flag(pinfo, cinfo, c_ambient, bool_optype);
  TriMesh tm_out = extract_from_flag_diffs(tm_si, pinfo, cinfo);
  return tm_out;
}

}  // namespace meshintersect
}  // namespace blender

extern "C" Boolean_trimesh_output *BLI_boolean_trimesh(const Boolean_trimesh_input *input,
                                                       int bool_optype)
{
  constexpr int dbg_level = 1;
  blender::meshintersect::TriMesh tm_in;
  tm_in.vert = blender::Array<blender::mpq3>(input->vert_len);
  for (int v = 0; v < input->vert_len; ++v) {
    tm_in.vert[v] = blender::mpq3(
        input->vert_coord[v][0], input->vert_coord[v][1], input->vert_coord[v][2]);
  }
  tm_in.tri = blender::Array<blender::meshintersect::IndexedTriangle>(input->tri_len);
  for (int t = 0; t < input->tri_len; ++t) {
    tm_in.tri[t] = blender::meshintersect::IndexedTriangle(
        input->tri[t][0], input->tri[t][1], input->tri[t][2], t);
  }
  blender::meshintersect::TriMesh tm_out = self_boolean(tm_in, bool_optype);
  if (dbg_level > 0) {
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

extern "C" void BLI_boolean_trimesh_free(Boolean_trimesh_output *output)
{
  MEM_freeN(output->vert_coord);
  MEM_freeN(output->tri);
  MEM_freeN(output);
}
