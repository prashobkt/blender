/* Apache License, Version 2.0 */

#include "testing/testing.h"

#include <algorithm>
#include <fstream>
#include <iostream>

#include "gmpxx.h"

#include "BLI_array.hh"
#include "BLI_mesh_intersect.hh"
#include "BLI_mpq3.hh"
#include "BLI_vector.hh"

using BLI::Array;
using BLI::mpq3;
using BLI::Vector;
using BLI::MeshIntersect::IndexedTriangle;
using BLI::MeshIntersect::TriMesh;
using BLI::MeshIntersect::trimesh_self_intersect;
using BLI::MeshIntersect::write_html_trimesh;

constexpr bool DO_DRAW = true;
constexpr bool DO_OBJ = true;
constexpr const char *draw_file = "mesh_intersect_test.html";

/* The spec should have the form:
 * #verts #tris-
 * mpq_class mpq_class mpq_class  [#verts lines]
 # <int> <int> <int> [#tris lines]
 */
TriMesh fill_input_from_string(const char *spec)
{
  std::istringstream ss(spec);
  std::string line;
  getline(ss, line);
  std::istringstream hdrss(line);
  int nverts, ntris;
  hdrss >> nverts >> ntris;
  if (nverts == 0) {
    return TriMesh();
  }
  Array<mpq3> verts(nverts);
  Array<IndexedTriangle> tris(ntris);
  int i = 0;
  while (i < nverts && getline(ss, line)) {
    std::istringstream iss(line);
    mpq_class p0, p1, p2;
    iss >> p0 >> p1 >> p2;
    verts[i] = mpq3(p0, p1, p2);
    i++;
  }
  i = 0;
  while (i < ntris && getline(ss, line)) {
    std::istringstream tss(line);
    int v0, v1, v2;
    tss >> v0 >> v1 >> v2;
    tris[i] = IndexedTriangle(v0, v1, v2, i);
    i++;
  }
  TriMesh ans;
  ans.vert = verts;
  ans.tri = tris;
  return ans;
}

TEST(mesh_intersect, OneTri)
{
  Array<mpq3> verts = {mpq3(0, 0, 0), mpq3(1, 0, 0), mpq3(0.5, 1, 0)};
  Array<IndexedTriangle> tris = {IndexedTriangle{0, 1, 2, -1}};
  TriMesh mesh;
  mesh.tri = tris;
  mesh.vert = verts;

  TriMesh imesh = trimesh_self_intersect(mesh);
  EXPECT_EQ(imesh.vert.size(), mesh.vert.size());
  EXPECT_EQ(imesh.tri.size(), mesh.tri.size());
  if (DO_DRAW) {
    write_html_trimesh(mesh.vert, mesh.tri, draw_file, "OneTri");
  }
}

TEST(mesh_intersect, TwoTris)
{
  Array<mpq3> verts = {
      mpq3(1, 1, 1),     mpq3(1, 4, 1),   mpq3(1, 1, 4),  /* T0 */
      mpq3(2, 2, 2),     mpq3(-3, 3, 2),  mpq3(-4, 1, 3), /* T1 */
      mpq3(2, 2, 2),     mpq3(-3, 3, 2),  mpq3(0, 3, 5),  /* T2 */
      mpq3(2, 2, 2),     mpq3(-3, 3, 2),  mpq3(0, 3, 3),  /* T3 */
      mpq3(1, 0, 0),     mpq3(2, 4, 1),   mpq3(-3, 2, 2), /* T4 */
      mpq3(0, 2, 1),     mpq3(-2, 3, 3),  mpq3(0, 1, 3),  /* T5 */
      mpq3(1.5, 2, 0.5), mpq3(-2, 3, 3),  mpq3(0, 1, 3),  /* T6 */
      mpq3(1, 0, 0),     mpq3(-2, 3, 3),  mpq3(0, 1, 3),  /* T7 */
      mpq3(1, 0, 0),     mpq3(-3, 2, 2),  mpq3(0, 1, 3),  /* T8 */
      mpq3(1, 0, 0),     mpq3(-1, 1, 1),  mpq3(0, 1, 3),  /* T9 */
      mpq3(3, -1, -1),   mpq3(-1, 1, 1),  mpq3(0, 1, 3),  /* T10 */
      mpq3(0, 0.5, 0.5), mpq3(-1, 1, 1),  mpq3(0, 1, 3),  /* T11 */
      mpq3(2, 1, 1),     mpq3(3, 5, 2),   mpq3(-2, 3, 3), /* T12 */
      mpq3(2, 1, 1),     mpq3(3, 5, 2),   mpq3(-2, 3, 4), /* T13 */
      mpq3(2, 2, 5),     mpq3(-3, 3, 5),  mpq3(0, 3, 10), /* T14 */
      mpq3(0, 0, 0),     mpq3(4, 4, 0),   mpq3(-4, 2, 4), /* T15 */
      mpq3(0, 1.5, 1),   mpq3(1, 2.5, 1), mpq3(-1, 2, 2), /* T16 */
      mpq3(3, 0, -2),    mpq3(7, 4, -2),  mpq3(-1, 2, 2), /* T17 */
      mpq3(3, 0, -2),    mpq3(3, 6, 2),   mpq3(-1, 2, 2), /* T18 */
      mpq3(7, 4, -2),    mpq3(3, 6, 2),   mpq3(-1, 2, 2), /* T19 */
      mpq3(5, 2, -2),    mpq3(1, 4, 2),   mpq3(-3, 0, 2), /* T20 */
      mpq3(2, 2, 0),     mpq3(1, 4, 2),   mpq3(-3, 0, 2), /* T21 */
      mpq3(0, 0, 0),     mpq3(4, 4, 0),   mpq3(-3, 0, 2), /* T22 */
      mpq3(0, 0, 0),     mpq3(4, 4, 0),   mpq3(-1, 2, 2), /* T23 */
      mpq3(2, 2, 0),     mpq3(4, 4, 0),   mpq3(0, 3, 2),  /* T24 */
      mpq3(0, 0, 0),     mpq3(-4, 2, 4),  mpq3(4, 4, 0),  /* T25 */
  };
  struct two_tri_test_spec {
    int t0;
    int t1;
    int nv_out;
    int nf_out;
  };
  Array<two_tri_test_spec> test_tris = {
      {0, 1, 8, 8},  /* 0: T1 pierces T0 inside at (1,11/6,13/6) and (1,11/5,2). */
      {0, 2, 8, 8},  /* 1: T2 intersects T0 inside (1,11/5,2) and edge (1,7/3,8/3). */
      {0, 3, 8, 7},  /* 2: T3 intersects T0 (1,11/5,2) and edge-edge (1,5/2,5/2). */
      {4, 5, 6, 4},  /* 3: T5 touches T4 inside (0,2,1). */
      {4, 6, 6, 3},  /* 4: T6 touches T4 on edge (3/2,2/1/2). */
      {4, 7, 5, 2},  /* 5: T7 touches T4 on vert (1,0,0). */
      {4, 8, 4, 2},  /* 6: T8 shared edge with T4 (1,0,0)(-3,2,2). */
      {4, 9, 5, 3},  /* 7: T9 edge (1,0,0)(-1,1,1) is subset of T4 edge. */
      {4, 10, 6, 4}, /* 8: T10 edge overlaps T4 edge with seg (-1,1,0)(1,0,0). */
      {4, 11, 6, 4}, /* 9: T11 edge (-1,1,1)(0,1/2,1/2) inside T4 edge. */
      {4, 12, 6, 2}, /* 10: parallel planes, not intersecting. */
      {4, 13, 6, 2}, /* 11: non-parallel planes, not intersecting, all one side. */
      {0, 14, 6, 2}, /* 12: non-paralel planes, not intersecting, alternate sides. */
      /* Following are all coplanar cases. */
      {15, 16, 6, 8},   /* 13: T16 inside T15. Note: dup'd tri is expected.  */
      {15, 17, 8, 8},   /* 14: T17 intersects one edge of T15 at (1,1,0)(3,3,0). */
      {15, 18, 10, 12}, /* 15: T18 intersects T15 at (1,1,0)(3,3,0)(3,15/4,1/2)(0,3,2). */
      {15, 19, 8, 10},  /* 16: T19 intersects T15 at (3,3,0)(0,3,2). */
      {15, 20, 12, 14}, /* 17: T20 intersects T15 on three edges, six intersects. */
      {15, 21, 10, 11}, /* 18: T21 intersects T15 on three edges, touching one. */
      {15, 22, 5, 4},   /* 19: T22 shares edge T15, one other outside. */
      {15, 23, 4, 4},   /* 20: T23 shares edge T15, one other outside. */
      {15, 24, 5, 4},   /* 21: T24 shares two edges with T15. */
      {15, 25, 3, 2},   /* 22: T25 same T15, reverse orientation. */
  };
  static int perms[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};

  const int do_only_test = -1; /* Make this negative to do all tests. */
  for (int test = 0; test < test_tris.size(); ++test) {
    if (do_only_test >= 0 && test != do_only_test) {
      continue;
    }
    int tri1_index = test_tris[test].t0;
    int tri2_index = test_tris[test].t1;
    int co1_i = 3 * tri1_index;
    int co2_i = 3 * tri2_index;

    const bool verbose = false;

    if (verbose) {
      std::cout << "\nTest " << test << ": T" << tri1_index << " intersect T" << tri2_index
                << "\n";
    }

    const bool do_all_perms = false;
    const int perm_limit = do_all_perms ? 3 : 1;

    for (int i = 0; i < perm_limit; ++i) {
      for (int j = 0; j < perm_limit; ++j) {
        if (do_all_perms) {
          std::cout << "\nperms " << i << " " << j << "\n";
        }
        TriMesh in_mesh;
        in_mesh.vert = verts;
        in_mesh.tri = Array<IndexedTriangle>(2);
        in_mesh.tri[0] = IndexedTriangle(
            co1_i + perms[i][0], co1_i + perms[i][1], co1_i + perms[i][2], 0);
        in_mesh.tri[1] = IndexedTriangle(
            co2_i + perms[j][0], co2_i + perms[j][1], co2_i + perms[j][2], 1);

        TriMesh out_mesh = trimesh_self_intersect(in_mesh);
        EXPECT_EQ(out_mesh.vert.size(), test_tris[test].nv_out);
        EXPECT_EQ(out_mesh.tri.size(), test_tris[test].nf_out);
        bool constexpr dump_input = false;
        if (DO_DRAW && i == 0 && j == 0) {
          if (dump_input) {
            std::string lab = "two tri test " + std::to_string(test) + "input";
            write_html_trimesh(in_mesh.vert, in_mesh.tri, draw_file, lab);
          }
          std::string lab = "two tri test=" + std::to_string(test);
          write_html_trimesh(out_mesh.vert, out_mesh.tri, draw_file, lab);
        }
        if (DO_OBJ && i == 0 && j == 0) {
          if (dump_input) {
            std::string name = "test_tt_in" + std::to_string(test);
            write_obj_trimesh(out_mesh.vert, in_mesh.tri, name);
          }
          std::string name = "test_tt" + std::to_string(test);
          write_obj_trimesh(out_mesh.vert, out_mesh.tri, name);
        }
      }
    }
  }
}

TEST(mesh_intersect, OverlapCluster)
{
  /* Chain of 5 overlapping coplanar tris.
   * Ordered so that clustering will make two separate clusters
   * that it will have to merge into one cluster with everything.
   */
  const char *spec = R"(15 5
  0 0 0
  1 0 0
  1/2 1 0
  1/2 0 0
  3/2 0 0
  1 1 0
  1 0 0
  2 0 0
  3/2 1 0
  3/2 0 0
  5/2 0 0
  2 1 0
  2 0 0
  3 0 0
  5/2 1 0
  0 1 2
  3 4 5
  9 10 11
  12 13 14
  6 7 8
  )";

  TriMesh in = fill_input_from_string(spec);
  TriMesh out = trimesh_self_intersect(in);
  EXPECT_EQ(out.vert.size(), 16);
  EXPECT_EQ(out.tri.size(), 18);
  if (DO_DRAW) {
    write_html_trimesh(out.vert, out.tri, draw_file, "OverlapCluster");
  }
  if (DO_OBJ) {
    write_obj_trimesh(out.vert, out.tri, "overlapcluster");
  }
}

TEST(mesh_intersect, TriCornerCross1)
{
  /* A corner formed by 3 tris, and a 4th crossing two of them. */
  const char *spec = R"(12 4
  0 0 0
  1 0 0
  0 0 1
  0 0 0
  0 1 0
  0 0 1
  0 0 0
  1 0 0
  0 1 0
  1 1 1/2
  1 -2 1/2
  -2 1 1/2
  0 1 2
  3 4 5
  6 7 8
  9 10 11
  )";

  TriMesh in = fill_input_from_string(spec);
  TriMesh out = trimesh_self_intersect(in);
  EXPECT_EQ(out.vert.size(), 10);
  EXPECT_EQ(out.tri.size(), 14);
  if (DO_DRAW) {
    write_html_trimesh(out.vert, out.tri, draw_file, "TriCornerCross1");
  }
  if (DO_OBJ) {
    write_obj_trimesh(out.vert, out.tri, "test_tc_1");
  }
}

TEST(mesh_intersect, TriCornerCross2)
{
  /* A corner formed by 3 tris, and a 4th coplanar with base. */
  const char *spec = R"(12 4
  0 0 0
  1 0 0
  0 0 1
  0 0 0
  0 1 0
  0 0 1
  0 0 0
  1 0 0
  0 1 0
  1 1 0
  1 -2 0
  -2 1 0
  0 1 2
  3 4 5
  6 7 8
  9 10 11
  )";

  TriMesh in = fill_input_from_string(spec);
  TriMesh out = trimesh_self_intersect(in);
  EXPECT_EQ(out.vert.size(), 7);
  EXPECT_EQ(out.tri.size(), 8);
  if (DO_DRAW) {
    write_html_trimesh(out.vert, out.tri, draw_file, "TriCornerCross2");
  }
  if (DO_OBJ) {
    write_obj_trimesh(out.vert, out.tri, "test_tc_2");
  }
}

TEST(mesh_intersect, TriCornerCross3)
{
  /* A corner formed by 3 tris, and a 4th crossing all 3. */
  const char *spec = R"(12 4
  0 0 0
  1 0 0
  0 0 1
  0 0 0
  0 1 0
  0 0 1
  0 0 0
  1 0 0
  0 1 0
  3/2 -1/2 -1/4
  -1/2 3/2 -1/4
  -1/2 -1/2 3/4
  0 1 2
  3 4 5
  6 7 8
  9 10 11
  )";

  TriMesh in = fill_input_from_string(spec);
  TriMesh out = trimesh_self_intersect(in);
  EXPECT_EQ(out.vert.size(), 10);
  EXPECT_EQ(out.tri.size(), 16);
  if (DO_DRAW) {
    write_html_trimesh(out.vert, out.tri, draw_file, "TriCornerCross3");
  }
  if (DO_OBJ) {
    write_obj_trimesh(out.vert, out.tri, "test_tc_3");
  }
}

TEST(mesh_intersect, TetTet)
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
  0 1 2
  0 3 1
  1 3 2
  2 3 0
  4 5 6
  4 7 5
  5 7 6
  6 7 4
  )";

  TriMesh in = fill_input_from_string(spec);
  TriMesh out = trimesh_self_intersect(in);
  EXPECT_EQ(out.vert.size(), 11);
  EXPECT_EQ(out.tri.size(), 20);
  /* Expect there to be a triangle with these three verts, oriented this way, with original face 1.
   */
  const mpq3 *pv1 = std::find(out.vert.begin(), out.vert.end(), mpq3(2, 0, 0));
  const mpq3 *pv4 = std::find(out.vert.begin(), out.vert.end(), mpq3(0.5, 0.5, 1));
  const mpq3 *pv5 = std::find(out.vert.begin(), out.vert.end(), mpq3(1.5, 0.5, 1));
  EXPECT_TRUE(pv1 != out.vert.end() && pv4 != out.vert.end() && pv5 != out.vert.end());
  int v1 = pv1 - out.vert.begin();
  int v4 = pv4 - out.vert.begin();
  int v5 = pv5 - out.vert.begin();
  const IndexedTriangle *pt2 = std::find(
      out.tri.begin(), out.tri.end(), IndexedTriangle(v1, v4, v5, 1));
  EXPECT_NE(pt2, out.tri.end());
  if (DO_DRAW) {
    write_html_trimesh(out.vert, out.tri, draw_file, "TriCornerCross3");
  }
  if (DO_OBJ) {
    write_obj_trimesh(out.vert, out.tri, "test_tc_3");
  }
}
