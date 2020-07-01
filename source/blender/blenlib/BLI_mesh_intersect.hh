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

#ifndef __BLI_MESH_INTERSECT_HH__
#define __BLI_MESH_INTERSECT_HH__

/** \file
 * \ingroup bli
 *
 *  Work in progress on mesh intersection library function.
 */

#include <iostream>

#include "BLI_array.hh"
#include "BLI_math_mpq.hh"
#include "BLI_mpq3.hh"
#include "BLI_vector.hh"

namespace blender {

namespace meshintersect {

/* The indices are for vertices in some external space of coordinates.
 * The "orig" component is used to track how a triangle originally came
 * from some other space of triangle indices. Which we usually need,
 * and it packs nicely into this structure, so keeping it here will save
 * memory.
 */
class IndexedTriangle {
 public:
  IndexedTriangle() = default;
  IndexedTriangle(int v0, int v1, int v2, int orig) : m_v{v0, v1, v2}, m_orig{orig}
  {
  }
  IndexedTriangle(const IndexedTriangle &other)
  {
    m_v[0] = other.m_v[0];
    m_v[1] = other.m_v[1];
    m_v[2] = other.m_v[2];
    m_orig = other.m_orig;
  }
  IndexedTriangle &operator=(const IndexedTriangle &other)
  {
    if (this != &other) {
      m_v[0] = other.m_v[0];
      m_v[1] = other.m_v[1];
      m_v[2] = other.m_v[2];
      m_orig = other.m_orig;
    }
    return *this;
  }

  int v0() const
  {
    return m_v[0];
  }
  int v1() const
  {
    return m_v[1];
  }
  int v2() const
  {
    return m_v[2];
  }
  int orig() const
  {
    return m_orig;
  }
  int operator[](int i) const
  {
    return m_v[i];
  }
  int &operator[](int i)
  {
    return m_v[i];
  }
  bool operator==(const IndexedTriangle &other)
  {
    /* Let equality happen with any cyclic ordering difference, but not orientation difference. */
    return (((m_v[0] == other.m_v[0] && m_v[1] == other.m_v[1] && m_v[2] == other.m_v[2]) ||
             (m_v[0] == other.m_v[1] && m_v[1] == other.m_v[2] && m_v[2] == other.m_v[0]) ||
             (m_v[0] == other.m_v[2] && m_v[1] == other.m_v[0] && m_v[2] == other.m_v[1])) &&
            m_orig == other.m_orig);
  }

 private:
  int m_v[3]{-1, -1, -1};
  int m_orig{-1};
};

struct TriMesh {
  Array<blender::mpq3> vert;
  Array<IndexedTriangle> tri;

  TriMesh() = default;
};

/* The output will have dup vertices merged and degenerate triangles ignored.
 * If the input has overlapping coplanar triangles, then there will be
 * as many duplicates as there are overlaps in each overlapping triangular region.
 * The orig field of each IndexedTriangle will give the orig index in the input TriMesh
 * that the output triangle was a part of (input can have -1 for that field and then
 * the index in tri[] will be used as the original index).
 */
TriMesh trimesh_self_intersect(const TriMesh &tm_in);

/* For debugging output. */
std::ostream &operator<<(std::ostream &os, const IndexedTriangle &tri);

std::ostream &operator<<(std::ostream &os, const TriMesh &tm);

void write_html_trimesh(const Array<mpq3> &vert,
                        const Array<IndexedTriangle> &tri,
                        const std::string &fname,
                        const std::string &label);

void write_obj_trimesh(const Array<mpq3> &vert,
                       const Array<IndexedTriangle> &tri,
                       const std::string &objname);

} /* namespace meshintersect */

} /* namespace blender */

#endif /* __BLI_MESH_INTERSECT_HH__ */
