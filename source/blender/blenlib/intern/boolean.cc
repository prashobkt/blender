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

#include <iostream>

#include "gmpxx.h"

#include "BLI_array.hh"
#include "BLI_assert.h"
#include "BLI_hash.hh"
#include "BLI_map.hh"
#include "BLI_mesh_intersect.hh"
#include "BLI_mpq3.hh"
#include "BLI_stack.hh"
#include "BLI_vector.hh"

#include "BLI_boolean.h"

namespace BLI {
namespace MeshIntersect {

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

 private:
  int m_v[2]{-1, -1};
};

static std::ostream &operator<<(std::ostream &os, const Edge &e)
{
  os << "(" << e.v0() << "," << e.v1() << ")";
  return os;
}
static std::ostream &operator<<(std::ostream &os, const Vector<int> &ivec)
{
  for (uint i = 0; i < ivec.size(); ++i) {
    os << ivec[i];
    if (i != ivec.size() - 1) {
      std::cout << " ";
    }
  }
  return os;
}

class TriMeshTopology {
 public:
  TriMeshTopology(const TriMesh *tm);
  ~TriMeshTopology() = default;

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

 private:
  Map<Edge, Vector<int> *> m_edge_tri; /* Triangles that contain a given Edge (either order). */
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
  int ntri = static_cast<int>(tm->tri.size());
  for (int t = 0; t < ntri; ++t) {
    const IndexedTriangle &tri = tm->tri[t];
    for (int i = 0; i < 3; ++i) {
      Edge e(tri[i], tri[(i + 1) % 3]);
      auto createf = [t](Vector<int> **pvec) { *pvec = new Vector<int>{t}; };
      auto modifyf = [t](Vector<int> **pvec) { (*pvec)->append_non_duplicates(t); };
      this->m_edge_tri.add_or_modify(e, createf, modifyf);
    }
  }
  /* Debugging. */
  if (dbg_level > 0) {
    std::cout << "After TriMeshTopology construction\n";
    for (auto item : m_edge_tri.items()) {
      std::cout << item.key << ": " << *item.value << "\n";
      if (false) {
        m_edge_tri.print_table();
      }
    }
  }
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

 private:
  Vector<int> m_tri; /* Indices of triangles in the Patch. */
};

static std::ostream &operator<<(std::ostream &os, const Patch &patch)
{
  os << "Patch " << patch.tri();
  return os;
}

class PatchesInfo {
 public:
  explicit PatchesInfo(int ntri)
  {
    m_tri_patch = Array<int>(ntri, -1);
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
  int tot_patch() const
  {
    return static_cast<int>(m_patch.size());
  }

 private:
  Vector<Patch> m_patch;
  Array<int> m_tri_patch; /* Patch index for corresponding triangle. */
};

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
          if (t_other != -1 && !pinfo.tri_is_assigned(t_other)) {
            cur_patch_grow.push(t_other);
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
    std::cout << "\ntriangle map\n";
    for (int t = 0; t < static_cast<int>(tm.tri.size()); ++t) {
      std::cout << t << ": patch " << pinfo.tri_patch(t) << "\n";
    }
  }
  return pinfo;
}

static TriMesh self_boolean(const TriMesh &tm_in, int bool_optype)
{
  constexpr int dbg_level = 0;
  if (dbg_level > 0) {
    std::cout << "\nSELF_BOOLEAN op=" << bool_optype << "\n";
  }
  TriMesh tm_si = trimesh_self_intersect(tm_in);
  TriMeshTopology tm_si_topo(&tm_si);
  PatchesInfo pinfo = find_patches(tm_si, tm_si_topo);
  return tm_si;
}

}  // namespace MeshIntersect

template<> struct DefaultHash<MeshIntersect::Edge> {
  uint32_t operator()(const MeshIntersect::Edge &value) const
  {
    uint32_t hash0 = DefaultHash<int>{}(value.v0());
    uint32_t hash1 = DefaultHash<int>{}(value.v1());
    return hash0 ^ (hash1 * 33);
  }
};

}  // namespace BLI

extern "C" Boolean_trimesh_output *BLI_boolean_trimesh(const Boolean_trimesh_input *input,
                                                       int bool_optype)
{
  constexpr int dbg_level = 1;
  BLI::MeshIntersect::TriMesh tm_in;
  tm_in.vert = BLI::Array<BLI::mpq3>(input->vert_len);
  for (int v = 0; v < input->vert_len; ++v) {
    tm_in.vert[v] = BLI::mpq3(
        input->vert_coord[v][0], input->vert_coord[v][1], input->vert_coord[v][2]);
  }
  tm_in.tri = BLI::Array<BLI::MeshIntersect::IndexedTriangle>(input->tri_len);
  for (int t = 0; t < input->tri_len; ++t) {
    tm_in.tri[t] = BLI::MeshIntersect::IndexedTriangle(
        input->tri[t][0], input->tri[t][1], input->tri[t][2], t);
  }
  BLI::MeshIntersect::TriMesh tm_out = self_boolean(tm_in, bool_optype);
  if (dbg_level > 0) {
    BLI::MeshIntersect::write_html_trimesh(
        tm_out.vert, tm_out.tri, "mesh_boolean_test.html", "after self_boolean");
    BLI::MeshIntersect::write_obj_trimesh(tm_out.vert, tm_out.tri, "test_tettet");
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
