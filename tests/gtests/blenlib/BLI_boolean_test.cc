/* Apache License, Version 2.0 */

#include "testing/testing.h"

#include <fstream>
#include <iostream>
#include <sstream>

#include "MEM_guardedalloc.h"

#include "BLI_array.hh"
#include "BLI_boolean.hh"
#include "BLI_math_mpq.hh"
#include "BLI_mpq3.hh"
#include "BLI_vector.hh"

using blender::Array;
using blender::mpq3;
using blender::Vector;
using blender::meshintersect::boolean;
using blender::meshintersect::BOOLEAN_DIFFERENCE;
using blender::meshintersect::BOOLEAN_ISECT;
using blender::meshintersect::BOOLEAN_NONE;
using blender::meshintersect::boolean_trimesh;
using blender::meshintersect::BOOLEAN_UNION;
using blender::meshintersect::IndexedTriangle;
using blender::meshintersect::PolyMesh;
using blender::meshintersect::TriMesh;
using blender::meshintersect::write_obj_polymesh;
using blender::meshintersect::write_obj_trimesh;

/* Class that can make a TriMesh from a string spec.
 * The spec has #verts #tris on the first line, then all the vert coords,
 * then all the tris as vert index triples.
 */
class BT_input {
 public:
  TriMesh trimesh;
  BT_input(const char *spec)
  {
    std::istringstream ss(spec);
    std::string line;
    getline(ss, line);
    std::istringstream hdrss(line);
    int nv, nt;
    hdrss >> nv >> nt;
    trimesh.vert = Array<mpq3>(nv);
    trimesh.tri = Array<IndexedTriangle>(nt);
    if (nv > 0 && nt > 0) {
      int i = 0;
      while (i < nv && getline(ss, line)) {
        std::istringstream iss(line);
        iss >> trimesh.vert[i][0] >> trimesh.vert[i][1] >> trimesh.vert[i][2];
        ++i;
      }
      i = 0;
      while (i < nt && getline(ss, line)) {
        std::istringstream tss(line);
        int v0, v1, v2;
        tss >> v0 >> v1 >> v2;
        trimesh.tri[i] = IndexedTriangle(v0, v1, v2, i);
        ++i;
      }
    }
  }
};

class BP_input {
 public:
  PolyMesh polymesh;

  BP_input(const char *spec)
  {
    std::istringstream ss(spec);
    std::string line;
    getline(ss, line);
    std::istringstream hdrss(line);
    int nv, nf;
    hdrss >> nv >> nf;
    polymesh.vert = Array<mpq3>(nv);
    polymesh.face = Array<Array<int>>(nf);
    if (nv > 0 && nf > 0) {
      int i = 0;
      while (i < nv && getline(ss, line)) {
        std::istringstream iss(line);
        iss >> polymesh.vert[i][0] >> polymesh.vert[i][1] >> polymesh.vert[i][2];
        ++i;
      }
      i = 0;
      while (i < nf && getline(ss, line)) {
        std::istringstream tss(line);
        Vector<int> f;
        int v;
        while (tss >> v) {
          f.append(v);
        }
        polymesh.face[i] = Array<int>(f.size());
        std::copy(f.begin(), f.end(), polymesh.face[i].begin());
        ++i;
      }
    }
  }
};

constexpr bool DO_OBJ = true;

#if 0
TEST(boolean_trimesh, Empty)
{
  TriMesh in;
  TriMesh out = boolean_trimesh(in, BOOLEAN_NONE, 1, [](int) { return 0; });
  EXPECT_EQ(out.vert.size(), 0);
  EXPECT_EQ(out.tri.size(), 0);
}

TEST(boolean_trimesh, TetTet)
{
  const char *spec = R"(8 8
  0 0 0
  2 0 0
  1 2 0
  1 1 2
  0 0 1
  2 0 1
  1 2 1
  1 1 3
  0 2 1
  0 1 3
  1 2 3
  2 0 3
  4 6 5
  4 5 7
  5 6 7
  6 4 7
  )";

  BT_input bti(spec);
  TriMesh out = boolean_trimesh(bti.trimesh, BOOLEAN_NONE, 1, [](int) { return 0; });
  EXPECT_EQ(out.vert.size(), 11);
  EXPECT_EQ(out.tri.size(), 20);
  if (DO_OBJ) {
    write_obj_trimesh(out.vert, out.tri, "tettet");
  }

  TriMesh out2 = boolean_trimesh(bti.trimesh, BOOLEAN_UNION, 1, [](int) { return 0; });
  EXPECT_EQ(out2.vert.size(), 10);
  EXPECT_EQ(out2.tri.size(), 16);
  if (DO_OBJ) {
    write_obj_trimesh(out2.vert, out2.tri, "tettet_union");
  }
}

TEST(boolean_trimesh, TetTet2)
{
  const char *spec = R"(8 8
  0 1 -1
  7/8 -1/2 -1
  -7/8 -1/2 -1
  0 0 1
  0 1 0
  7/8 -1/2 0
  -7/8 -1/2 0
  0 0 2
  0 3 1
  0 1 2
  1 3 2
  2 3 0
  4 7 5
  4 5 6
  5 7 6
  6 7 4
  )";

  BT_input bti(spec);
  TriMesh out = boolean_trimesh(bti.trimesh, BOOLEAN_UNION, 1, [](int) { return 0; });
  EXPECT_EQ(out.vert.size(), 10);
  EXPECT_EQ(out.tri.size(), 16);
  if (DO_OBJ) {
    write_obj_trimesh(out.vert, out.tri, "tettet2_union");
  }
}

TEST(boolean_trimesh, CubeTet)
{
  const char *spec = R"(12 16
  -1 -1 -1
  -1 -1 1
  -1 1 -1
  -1 1 1
  1 -1 -1
  1 -1 1
  1 1 -1
  1 1 1
  0 1/2 1/2
  1/2 -1/4 1/2
  -1/2 -1/4 1/2
  0 0 3/2
  0 1 3
  0 3 2
  2 3 7
  2 7 6
  6 7 5
  6 5 4
  4 5 1
  4 1 0
  2 6 4
  2 4 0
  7 3 1
  7 1 5
  8 11 9
  8 9 10
  9 11 10
  10 11 8
  )";

  BT_input bti(spec);
  TriMesh out = boolean_trimesh(bti.trimesh, BOOLEAN_UNION, 1, [](int) { return 0; });
  EXPECT_EQ(out.vert.size(), 14);
  EXPECT_EQ(out.tri.size(), 24);
  if (DO_OBJ) {
    write_obj_trimesh(out.vert, out.tri, "cubetet_union");
  }
}

TEST(boolean_trimesh, BinaryTetTet)
{
  const char *spec = R"(8 8
  0 0 0
  2 0 0
  1 2 0
  1 1 2
  0 0 1
  2 0 1
  1 2 1
  1 1 3
  0 2 1
  0 1 3
  1 2 3
  2 0 3
  4 6 5
  4 5 7
  5 6 7
  6 4 7
  )";

  BT_input bti(spec);
  TriMesh out = boolean_trimesh(
      bti.trimesh, BOOLEAN_ISECT, 2, [](int t) { return t < 4 ? 0 : 1; });
  EXPECT_EQ(out.vert.size(), 4);
  EXPECT_EQ(out.tri.size(), 4);
  if (DO_OBJ) {
    write_obj_trimesh(out.vert, out.tri, "binary_tettet_isect");
  }
}

TEST(boolean_trimesh, TetTetCoplanar)
{
  const char *spec = R"(8 8
  0 1 0
  7/8 -1/2 0
  -7/8 -1/2 0
  0 0 1
  0 1 0
  7/8 -1/2 0
  -7/8 -1/2 0
  0 0 -1
  0 3 1
  0 1 2
  1 3 2
  2 3 0
  4 5 7
  4 6 5
  5 6 7
  6 4 7
  )";

  BT_input bti(spec);
  TriMesh out = boolean_trimesh(bti.trimesh, BOOLEAN_UNION, 1, [](int) { return 0; });
  EXPECT_EQ(out.vert.size(), 5);
  EXPECT_EQ(out.tri.size(), 6);
  if (DO_OBJ) {
    write_obj_trimesh(out.vert, out.tri, "tettet_coplanar");
  }
}

TEST(boolean_polymesh, CubeCube)
{
  const char *spec = R"(16 12
  -1 -1 -1
  -1 -1 1
  -1 1 -1
  -1 1 1
  1 -1 -1
  1 -1 1
  1 1 -1
  1 1 1
  1/2 1/2 1/2
  1/2 1/2 5/2
  1/2 5/2 1/2
  1/2 5/2 5/2
  5/2 1/2 1/2
  5/2 1/2 5/2
  5/2 5/2 1/2
  5/2 5/2 5/2
  0 1 3 2
  6 2 3 7
  4 6 7 5
  0 4 5 1
  0 2 6 4
  3 1 5 7
  8 9 11 10
  14 10 11 15
  12 14 15 13
  8 12 13 9
  8 10 14 12
  11 9 13 15
  )";

  BP_input bpi(spec);
  blender::meshintersect::PolyMesh out = blender::meshintersect::boolean(
      bpi.polymesh, BOOLEAN_UNION, 1, [](int UNUSED(t)) { return 0; });
  EXPECT_EQ(out.vert.size(), 20);
  EXPECT_EQ(out.face.size(), 12);
  if (DO_OBJ) {
    blender::meshintersect::write_obj_polymesh(out.vert, out.face, "cubecube");
  }
}

TEST(boolean_polymesh, CubeCone)
{
  const char *spec = R"(14 12
  -1 -1 -1
  -1 -1 1
  -1 1 -1
  -1 1 1
  1 -1 -1
  1 -1 1
  1 1 -1
  1 1 1
  0 1/2 3/4
  119/250 31/200 3/4
  147/500 -81/200 3/4
  0 0 7/4
  -147/500 -81/200 3/4
  -119/250 31/200 3/4
  0 1 3 2
  2 3 7 6
  6 7 5 4
  4 5 1 0
  2 6 4 0
  7 3 1 5
  8 11 9
  9 11 10
  10 11 12
  12 11 13
  13 11 8
  8 9 10 12 13)";

  BP_input bpi(spec);
  blender::meshintersect::PolyMesh out = blender::meshintersect::boolean(
      bpi.polymesh, BOOLEAN_UNION, 1, [](int UNUSED(t)) { return 0; });
  EXPECT_EQ(out.vert.size(), 14);
  EXPECT_EQ(out.face.size(), 12);
  if (DO_OBJ) {
    blender::meshintersect::write_obj_polymesh(out.vert, out.face, "cubeccone");
  }
}

TEST(boolean_polymesh, CubeCubeCoplanar)
{
  const char *spec = R"(16 12
  -1 -1 -1
  -1 -1 1
  -1 1 -1
  -1 1 1
  1 -1 -1
  1 -1 1
  1 1 -1
  1 1 1
  -1/2 -1/2 1
  -1/2 -1/2 2
  -1/2 1/2 1
  -1/2 1/2 2
  1/2 -1/2 1
  1/2 -1/2 2
  1/2 1/2 1
  1/2 1/2 2
  0 1 3 2
  2 3 7 6
  6 7 5 4
  4 5 1 0
  2 6 4 0
  7 3 1 5
  8 9 11 10
  10 11 15 14
  14 15 13 12
  12 13 9 8
  10 14 12 8
  15 11 9 13
  )";

  BP_input bpi(spec);
  blender::meshintersect::PolyMesh out = blender::meshintersect::boolean(
      bpi.polymesh, BOOLEAN_UNION, 2, [](int t) { return t < 6 ? 0 : 1; });
  EXPECT_EQ(out.vert.size(), 16);
  EXPECT_EQ(out.face.size(), 12);
  if (DO_OBJ) {
    blender::meshintersect::write_obj_polymesh(out.vert, out.face, "cubecube_coplanar");
  }
}
#endif

TEST(boolean_polymesh, TetTeTCoplanarDiff)
{
  const char *spec = R"(8 8
  0 1 0
  7/8 -1/2 0
  -7/8 -1/2 0
  0 0 1
  0 1 0
  7/8 -1/2 0
  -7/8 -1/2 0
  0 0 -1
  0 3 1
  0 1 2
  1 3 2
  2 3 0
  4 5 7
  4 6 5
  5 6 7
  6 4 7
  )";

  BP_input bpi(spec);
  blender::meshintersect::PolyMesh out = blender::meshintersect::boolean(
      bpi.polymesh, BOOLEAN_DIFFERENCE, 2, [](int t) { return t < 4 ? 0 : 1; });
  EXPECT_EQ(out.vert.size(), 4);
  EXPECT_EQ(out.face.size(), 4);
  if (DO_OBJ) {
    blender::meshintersect::write_obj_polymesh(out.vert, out.face, "tettet_coplanar_diff");
  }
}