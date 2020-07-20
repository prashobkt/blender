/* Apache License, Version 2.0 */

#include "testing/testing.h"

#include <algorithm>
#include <fstream>
#include <iostream>

#include "PIL_time.h"

#include "BLI_array.hh"
#include "BLI_math_mpq.hh"
#include "BLI_mesh_intersect.hh"
#include "BLI_mpq3.hh"
#include "BLI_vector.hh"

#define DO_REGULAR_TESTS 1
#define DO_PERF_TESTS 1

namespace blender::meshintersect {

constexpr bool DO_OBJ = true;

/* Build and hold a Mesh from a string spec. Also hold and own resources used by Mesh. */
class MeshBuilder {
 public:
  Mesh mesh;
  MArena arena;

  /* "Edge orig" indices are an encoding of <input face#, position in face of start of edge>. */
  static constexpr int MAX_FACE_LEN = 1000; /* Used for forming "orig edge" indices only. */

  static int edge_index(int face_index, int facepos)
  {
    return face_index * MAX_FACE_LEN + facepos;
  }

  static std::pair<int, int> face_and_pos_for_edge_index(int e_index)
  {
    return std::pair<int, int>(e_index / MAX_FACE_LEN, e_index % MAX_FACE_LEN);
  }

  /*
   * Spec should have form:
   *  #verts #faces
   *  mpq_class mpq_class mpq_clas [#verts lines]
   *  int int int ... [#faces lines; indices into verts for given face]
   */
  MeshBuilder(const char *spec)
  {
    std::istringstream ss(spec);
    std::string line;
    getline(ss, line);
    std::istringstream hdrss(line);
    int nv, nf;
    hdrss >> nv >> nf;
    if (nv == 0 || nf == 0) {
      return;
    }
    arena.reserve(nv, nf);
    Vector<Vertp> verts;
    Vector<Facep> faces;
    bool spec_ok = true;
    int v_index = 0;
    while (v_index < nv && spec_ok && getline(ss, line)) {
      std::istringstream iss(line);
      mpq_class p0;
      mpq_class p1;
      mpq_class p2;
      iss >> p0 >> p1 >> p2;
      spec_ok = !iss.fail();
      if (spec_ok) {
        verts.append(arena.add_or_find_vert(mpq3(p0, p1, p2), v_index));
      }
      ++v_index;
    }
    if (v_index != nv) {
      spec_ok = false;
    }
    int f_index = 0;
    while (f_index < nf && spec_ok && getline(ss, line)) {
      std::istringstream fss(line);
      Vector<Vertp> face_verts;
      Vector<int> edge_orig;
      int fpos = 0;
      while (spec_ok && fss >> v_index) {
        if (v_index < 0 || v_index >= nv) {
          spec_ok = false;
          continue;
        }
        face_verts.append(verts[v_index]);
        edge_orig.append(edge_index(f_index, fpos));
        ++fpos;
      }
      if (fpos < 3) {
        spec_ok = false;
      }
      if (spec_ok) {
        Facep facep = arena.add_face(face_verts, f_index, edge_orig);
        faces.append(facep);
      }
      ++f_index;
    }
    if (f_index != nf) {
      spec_ok = false;
    }
    if (!spec_ok) {
      std::cout << "Bad spec: " << spec;
      return;
    }
    mesh = Mesh(faces);
  }
};

/* Return a Facep in mesh with verts equal to v0, v1, and v2, in
 * some cyclic order; return nullptr if not found.
 */
static Facep find_tri_with_verts(const Mesh &mesh, Vertp v0, Vertp v1, Vertp v2)
{
  Face f_arg({v0, v1, v2}, 0, NO_INDEX, {-1, -1, -1});
  for (Facep f : mesh.faces()) {
    if (f->cyclic_equal(f_arg)) {
      return f;
    }
  }
  return nullptr;
}

/* How many instances of a triangle with v0, v1, v2 are in the mesh? */
static int count_tris_with_verts(const Mesh &mesh, Vertp v0, Vertp v1, Vertp v2)
{
  Face f_arg({v0, v1, v2}, 0, NO_INDEX, {-1, -1, -1});
  int ans = 0;
  for (Facep f : mesh.faces()) {
    if (f->cyclic_equal(f_arg)) {
      ++ans;
    }
  }
  return ans;
}

/* What is the starting position, if any, of the edge (v0, v1), in either order, in f? -1 if none.
 */
static int find_edge_pos_in_tri(Vertp v0, Vertp v1, Facep f)
{
  for (int pos : f->index_range()) {
    int nextpos = f->next_pos(pos);
    if (((*f)[pos] == v0 && (*f)[nextpos] == v1) || ((*f)[pos] == v1 && (*f)[nextpos] == v0)) {
      return static_cast<int>(pos);
    }
  }
  return -1;
}

#if DO_REGULAR_TESTS
TEST(mesh_intersect, Mesh)
{
  Vector<Vertp> verts;
  Vector<Facep> faces;
  MArena arena;

  verts.append(arena.add_or_find_vert(mpq3(0, 0, 1), 0));
  verts.append(arena.add_or_find_vert(mpq3(1, 0, 1), 1));
  verts.append(arena.add_or_find_vert(mpq3(0.5, 1, 1), 2));
  faces.append(arena.add_face(verts, 0, {10, 11, 12}));

  Mesh mesh(faces);
  Facep f = mesh.face(0);
  EXPECT_TRUE(f->is_tri());
  EXPECT_EQ(f->plane.norm, double3(0.0, 0.0, 1.0));
  EXPECT_EQ(f->plane.d, -1.0);
}

TEST(mesh_intersect, OneTri)
{
  const char *spec = R"(3 1
  0 0 0
  1 0 0
  1/2 1 0
  0 1 2
  )";

  MeshBuilder mb(spec);
  Mesh imesh = trimesh_self_intersect(mb.mesh, &mb.arena);
  imesh.populate_vert();
  EXPECT_EQ(imesh.vert_size(), 3);
  EXPECT_EQ(imesh.face_size(), 1);
  const Face f_in = *mb.mesh.face(0);
  const Face f_out = *imesh.face(0);
  EXPECT_EQ(f_in.orig, f_out.orig);
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(f_in[i], f_out[i]);
    EXPECT_EQ(f_in.edge_orig[i], f_out.edge_orig[i]);
  }
}

TEST(mesh_intersect, TriTri)
{
  const char *spec = R"(6 2
  0 0 0
  4 0 0
  0 4 0
  1 0 0
  2 0 0
  1 1 0
  0 1 2
  3 4 5
  )";

  /* Second triangle is smaller and congruent to first, resting on same base, partway along. */
  MeshBuilder mb(spec);
  Mesh out = trimesh_self_intersect(mb.mesh, &mb.arena);
  out.populate_vert();
  EXPECT_EQ(out.vert_size(), 6);
  EXPECT_EQ(out.face_size(), 6);
  if (out.vert_size() == 6 && out.face_size() == 6) {
    Vertp v0 = mb.arena.find_vert(mpq3(0, 0, 0));
    Vertp v1 = mb.arena.find_vert(mpq3(4, 0, 0));
    Vertp v2 = mb.arena.find_vert(mpq3(0, 4, 0));
    Vertp v3 = mb.arena.find_vert(mpq3(1, 0, 0));
    Vertp v4 = mb.arena.find_vert(mpq3(2, 0, 0));
    Vertp v5 = mb.arena.find_vert(mpq3(1, 1, 0));
    EXPECT_TRUE(v0 != nullptr && v1 != nullptr && v2 != nullptr);
    EXPECT_TRUE(v3 != nullptr && v4 != nullptr && v5 != nullptr);
    if (v0 != nullptr && v1 != nullptr && v2 != nullptr && v3 != nullptr && v4 != nullptr &&
        v5 != nullptr) {
      EXPECT_EQ(v0->orig, 0);
      EXPECT_EQ(v1->orig, 1);
      Facep f0 = find_tri_with_verts(out, v4, v1, v5);
      Facep f1 = find_tri_with_verts(out, v3, v4, v5);
      Facep f2 = find_tri_with_verts(out, v0, v3, v5);
      Facep f3 = find_tri_with_verts(out, v0, v5, v2);
      Facep f4 = find_tri_with_verts(out, v5, v1, v2);
      EXPECT_TRUE(f0 != nullptr && f1 != nullptr && f2 != nullptr && f3 != nullptr &&
                  f4 != nullptr);
      /* For boolean to work right, there need to be two copies of the smaller triangle in the
       * output. */
      EXPECT_EQ(count_tris_with_verts(out, v3, v4, v5), 2);
      if (f0 != nullptr && f1 != nullptr && f2 != nullptr && f3 != nullptr && f4 != nullptr) {
        EXPECT_EQ(f0->orig, 0);
        EXPECT_TRUE(f1->orig == 0 || f1->orig == 1);
        EXPECT_EQ(f2->orig, 0);
        EXPECT_EQ(f3->orig, 0);
        EXPECT_EQ(f4->orig, 0);
      }
      EXPECT_TRUE(f0->plane.norm[0] == 0.0 && f0->plane.norm[1] == 0.0 &&
                  f0->plane.norm[2] > 0.0 && f0->plane.d == 0.0);
      int e03 = find_edge_pos_in_tri(v0, v3, f2);
      int e34 = find_edge_pos_in_tri(v3, v4, f1);
      int e45 = find_edge_pos_in_tri(v4, v5, f1);
      int e05 = find_edge_pos_in_tri(v0, v5, f3);
      int e15 = find_edge_pos_in_tri(v1, v5, f0);
      EXPECT_TRUE(e03 != -1 && e34 != -1 && e45 != -1 && e05 != -1 && e15 != -1);
      if (e03 != -1 && e34 != -1 && e45 != -1 && e05 != -1 && e15 != -1) {
        EXPECT_EQ(f2->edge_orig[e03], 0);
        EXPECT_TRUE(f1->edge_orig[e34] == 0 ||
                    f1->edge_orig[e34] == 1 * MeshBuilder::MAX_FACE_LEN);
        EXPECT_EQ(f1->edge_orig[e45], 1 * MeshBuilder::MAX_FACE_LEN + 1);
        EXPECT_EQ(f3->edge_orig[e05], NO_INDEX);
        EXPECT_EQ(f0->edge_orig[e15], NO_INDEX);
      }
    }
  }
  if (DO_OBJ) {
    write_obj_mesh(out, "tritri");
  }
}

TEST(mesh_intersect, TriTriReversed)
{
  /* Like TriTri but with triangles of opposite orientation.
   * This matters because projection to 2D will now need reversed triangles.
   */
  const char *spec = R"(6 2
  0 0 0
  4 0 0
  0 4 0
  1 0 0
  2 0 0
  1 1 0
  0 2 1
  3 5 4
  )";

  MeshBuilder mb(spec);
  Mesh out = trimesh_self_intersect(mb.mesh, &mb.arena);
  out.populate_vert();
  EXPECT_EQ(out.vert_size(), 6);
  EXPECT_EQ(out.face_size(), 6);
  if (out.vert_size() == 6 && out.face_size() == 6) {
    Vertp v0 = mb.arena.find_vert(mpq3(0, 0, 0));
    Vertp v1 = mb.arena.find_vert(mpq3(4, 0, 0));
    Vertp v2 = mb.arena.find_vert(mpq3(0, 4, 0));
    Vertp v3 = mb.arena.find_vert(mpq3(1, 0, 0));
    Vertp v4 = mb.arena.find_vert(mpq3(2, 0, 0));
    Vertp v5 = mb.arena.find_vert(mpq3(1, 1, 0));
    EXPECT_TRUE(v0 != nullptr && v1 != nullptr && v2 != nullptr);
    EXPECT_TRUE(v3 != nullptr && v4 != nullptr && v5 != nullptr);
    if (v0 != nullptr && v1 != nullptr && v2 != nullptr && v3 != nullptr && v4 != nullptr &&
        v5 != nullptr) {
      EXPECT_EQ(v0->orig, 0);
      EXPECT_EQ(v1->orig, 1);
      Facep f0 = find_tri_with_verts(out, v4, v5, v1);
      Facep f1 = find_tri_with_verts(out, v3, v5, v4);
      Facep f2 = find_tri_with_verts(out, v0, v5, v3);
      Facep f3 = find_tri_with_verts(out, v0, v2, v5);
      Facep f4 = find_tri_with_verts(out, v5, v2, v1);
      EXPECT_TRUE(f0 != nullptr && f1 != nullptr && f2 != nullptr && f3 != nullptr &&
                  f4 != nullptr);
      /* For boolean to work right, there need to be two copies of the smaller triangle in the
       * output. */
      EXPECT_EQ(count_tris_with_verts(out, v3, v5, v4), 2);
      if (f0 != nullptr && f1 != nullptr && f2 != nullptr && f3 != nullptr && f4 != nullptr) {
        EXPECT_EQ(f0->orig, 0);
        EXPECT_TRUE(f1->orig == 0 || f1->orig == 1);
        EXPECT_EQ(f2->orig, 0);
        EXPECT_EQ(f3->orig, 0);
        EXPECT_EQ(f4->orig, 0);
      }
      EXPECT_TRUE(f0->plane.norm[0] == 0.0 && f0->plane.norm[1] == 0.0 &&
                  f0->plane.norm[2] < 0.0 && f0->plane.d == 0.0);
      int e03 = find_edge_pos_in_tri(v0, v3, f2);
      int e34 = find_edge_pos_in_tri(v3, v4, f1);
      int e45 = find_edge_pos_in_tri(v4, v5, f1);
      int e05 = find_edge_pos_in_tri(v0, v5, f3);
      int e15 = find_edge_pos_in_tri(v1, v5, f0);
      EXPECT_TRUE(e03 != -1 && e34 != -1 && e45 != -1 && e05 != -1 && e15 != -1);
      if (e03 != -1 && e34 != -1 && e45 != -1 && e05 != -1 && e15 != -1) {
        EXPECT_EQ(f2->edge_orig[e03], 2);
        EXPECT_TRUE(f1->edge_orig[e34] == 2 ||
                    f1->edge_orig[e34] == 1 * MeshBuilder::MAX_FACE_LEN + 2);
        EXPECT_EQ(f1->edge_orig[e45], 1 * MeshBuilder::MAX_FACE_LEN + 1);
        EXPECT_EQ(f3->edge_orig[e05], NO_INDEX);
        EXPECT_EQ(f0->edge_orig[e15], NO_INDEX);
      }
    }
  }
  if (DO_OBJ) {
    write_obj_mesh(out, "tritrirev");
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
  Array<two_tri_test_spec> test_tris = Span<two_tri_test_spec>{
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
        MArena arena;
        arena.reserve(2 * 3, 2);
        Array<Vertp> f0_verts(3);
        Array<Vertp> f1_verts(3);
        for (int k = 0; k < 3; ++k) {
          f0_verts[k] = arena.add_or_find_vert(verts[co1_i + perms[i][k]], k);
        }
        for (int k = 0; k < 3; ++k) {
          f1_verts[k] = arena.add_or_find_vert(verts[co2_i + perms[i][k]], k + 3);
        }
        Facep f0 = arena.add_face(f0_verts, 0, {0, 1, 2});
        Facep f1 = arena.add_face(f1_verts, 1, {3, 4, 5});
        Mesh in_mesh({f0, f1});
        Mesh out_mesh = trimesh_self_intersect(in_mesh, &arena);
        out_mesh.populate_vert();
        EXPECT_EQ(out_mesh.vert_size(), test_tris[test].nv_out);
        EXPECT_EQ(out_mesh.face_size(), test_tris[test].nf_out);
        bool constexpr dump_input = true;
        if (DO_OBJ && i == 0 && j == 0) {
          if (dump_input) {
            std::string name = "test_tt_in" + std::to_string(test);
            write_obj_mesh(in_mesh, name);
          }
          std::string name = "test_tt" + std::to_string(test);
          write_obj_mesh(out_mesh, name);
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

  MeshBuilder mb(spec);
  Mesh out = trimesh_self_intersect(mb.mesh, &mb.arena);
  out.populate_vert();
  EXPECT_EQ(out.vert_size(), 16);
  EXPECT_EQ(out.face_size(), 18);
  if (DO_OBJ) {
    write_obj_mesh(out, "overlapcluster");
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

  MeshBuilder mb(spec);
  Mesh out = trimesh_self_intersect(mb.mesh, &mb.arena);
  out.populate_vert();
  EXPECT_EQ(out.vert_size(), 10);
  EXPECT_EQ(out.face_size(), 14);
  if (DO_OBJ) {
    write_obj_mesh(out, "test_tc_1");
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

  MeshBuilder mb(spec);
  Mesh out = trimesh_self_intersect(mb.mesh, &mb.arena);
  out.populate_vert();
  EXPECT_EQ(out.vert_size(), 7);
  EXPECT_EQ(out.face_size(), 8);
  if (DO_OBJ) {
    write_obj_mesh(out, "test_tc_2");
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

  MeshBuilder mb(spec);
  Mesh out = trimesh_self_intersect(mb.mesh, &mb.arena);
  out.populate_vert();
  EXPECT_EQ(out.vert_size(), 10);
  EXPECT_EQ(out.face_size(), 16);
  if (DO_OBJ) {
    write_obj_mesh(out, "test_tc_3");
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

  MeshBuilder mb(spec);
  Mesh out = trimesh_self_intersect(mb.mesh, &mb.arena);
  out.populate_vert();
  EXPECT_EQ(out.vert_size(), 11);
  EXPECT_EQ(out.face_size(), 20);
  /* Expect there to be a triangle with these three verts, oriented this way, with original face 1.
   */
  Vertp v1 = mb.arena.find_vert(mpq3(2, 0, 0));
  Vertp v4 = mb.arena.find_vert(mpq3(0.5, 0.5, 1));
  Vertp v5 = mb.arena.find_vert(mpq3(1.5, 0.5, 1));
  EXPECT_TRUE(v1 != nullptr && v4 != nullptr && v5 != nullptr);
  Facep f = mb.arena.find_face({v1, v4, v5});
  EXPECT_NE(f, nullptr);
  EXPECT_EQ(f->orig, 1);
  if (DO_OBJ) {
    write_obj_mesh(out, "test_tc_3");
  }
}
#endif

#if DO_PERF_TESTS

static void spheresphere_test(int nrings, double y_offset, bool use_self)
{
  /* Make two icospheres with nrings rings ad 2*nrings segments. */
  if (nrings < 2) {
    return;
  }
  double time_start = PIL_check_seconds_timer();
  MArena arena;
  bool triangulate = true;
  const bool nrings_even = (nrings % 2 == 0);
  int half_nrings = nrings / 2;
  int nsegs = 2 * nrings;
  const bool nsegs_even = true;
  const bool nsegs_four_divisible = (nsegs % 4 == 0);
  int half_nsegs = nrings;
  int quarter_nsegs = half_nsegs / 2;
  double radius = 1.0;
  int num_sphere_verts = nsegs * (nrings - 1) + 2;
  int num_sphere_faces = nsegs * nrings;
  int num_sphere_tris = 2 * nsegs + 2 * nsegs * (nrings - 2);
  arena.reserve(2 * 2 * num_sphere_verts,
                2 * 2 * (triangulate ? num_sphere_tris : num_sphere_faces));
  double center_y[2] = {0.0, y_offset};
  double delta_phi = 2 * M_PI / nsegs;
  double delta_theta = M_PI / nrings;
  int fid = 0;
  int vid = 0;
  Array<Facep> faces(2 * (triangulate ? num_sphere_tris : num_sphere_faces));
  Array<Vertp> verts(2 * num_sphere_verts);
  int face_start[2] = {0, (triangulate ? num_sphere_tris : num_sphere_faces)};
  int vert_start[2] = {0, num_sphere_verts};
  auto vert_index_fn = [nrings, &vert_start](int sphere, int seg, int ring) {
    if (ring == 0) { /* Top vert. */
      return vert_start[sphere] + vert_start[1] - 2;
    }
    if (ring == nrings) { /* Bottom vert. */
      return vert_start[sphere] + vert_start[1] - 1;
    }
    return vert_start[sphere] + seg * (nrings - 1) + (ring - 1);
  };
  auto face_index_fn = [nrings, &face_start](int sphere, int seg, int ring) {
    return face_start[sphere] + seg * nrings + ring;
  };
  auto tri_index_fn = [nrings, nsegs, &face_start](int sphere, int seg, int ring, int tri) {
    if (ring == 0) {
      return face_start[sphere] + seg;
    }
    if (ring < nrings - 1) {
      return face_start[sphere] + nsegs + 2 * (ring - 1) * nsegs + 2 * seg + tri;
    }
    return face_start[sphere] + nsegs + 2 * (nrings - 2) * nsegs + seg;
  };
  Array<int> eid = {0, 0, 0, 0}; /* Don't care about edge ids. */
  for (int sphere : {0, 1}) {
    /*
     * (x, y , z) is given from inclination theta and azimuth phi,
     * where 0 <= theta <= pi;  0 <= phi <= 2pi.
     * x = radius * sin(theta) cos(phi)
     * y = radius * sin(theta) sin(phi)
     * z = radius * cos(theta)
     */
    for (int s = 0; s < nsegs; ++s) {
      double phi = s * delta_phi;
      double sin_phi;
      double cos_phi;
      if (s == 0) {
        /* phi = 0. */
        sin_phi = 0.0;
        cos_phi = 1.0;
      }
      else if (nsegs_even && s == half_nsegs) {
        /* phi = pi. */
        sin_phi = 0.0;
        cos_phi = -1.0;
      }
      else if (nsegs_four_divisible && s == quarter_nsegs) {
        /* phi = pi/2. */
        sin_phi = 1.0;
        cos_phi = 0.0;
      }
      else if (nsegs_four_divisible && s == 3 * quarter_nsegs) {
        /* phi = 3pi/2. */
        sin_phi = -1.0;
        cos_phi = 0.0;
      }
      else {
        sin_phi = sin(phi);
        cos_phi = cos(phi);
      }
      for (int r = 1; r < nrings; ++r) {
        double theta = r * delta_theta;
        double r_sin_theta;
        double r_cos_theta;
        if (nrings_even && r == half_nrings) {
          /* theta = pi/2. */
          r_sin_theta = radius;
          r_cos_theta = 0.0;
        }
        else {
          r_sin_theta = radius * sin(theta);
          r_cos_theta = radius * cos(theta);
        }
        double x = r_sin_theta * cos_phi;
        double y = r_sin_theta * sin_phi + center_y[sphere];
        double z = r_cos_theta;
        Vertp v = arena.add_or_find_vert(mpq3(x, y, z), vid++);
        int vindex = vert_index_fn(sphere, s, r);
        verts[vindex] = v;
      }
    }
    Vertp vtop = arena.add_or_find_vert(mpq3(0, center_y[sphere], radius), vid++);
    Vertp vbot = arena.add_or_find_vert(mpq3(0, center_y[sphere], -radius), vid++);
    verts[vert_index_fn(sphere, 0, 0)] = vtop;
    verts[vert_index_fn(sphere, 0, nrings)] = vbot;
    for (int s = 0; s < nsegs; ++s) {
      int snext = (s + 1) % nsegs;
      for (int r = 0; r < nrings; ++r) {
        int rnext = r + 1;
        int i0 = vert_index_fn(sphere, s, r);
        int i1 = vert_index_fn(sphere, s, rnext);
        int i2 = vert_index_fn(sphere, snext, rnext);
        int i3 = vert_index_fn(sphere, snext, r);
        Facep f;
        Facep f2 = nullptr;
        if (r == 0) {
          f = arena.add_face({verts[i0], verts[i1], verts[i2]}, fid++, eid);
        }
        else if (r == nrings - 1) {
          f = arena.add_face({verts[i0], verts[i1], verts[i3]}, fid++, eid);
        }
        else {
          if (triangulate) {
            f = arena.add_face({verts[i0], verts[i1], verts[i2]}, fid++, eid);
            f2 = arena.add_face({verts[i2], verts[i3], verts[i0]}, fid++, eid);
          }
          else {
            f = arena.add_face({verts[i0], verts[i1], verts[i2], verts[i3]}, fid++, eid);
          }
        }
        if (triangulate) {
          int f_index = tri_index_fn(sphere, s, r, 0);
          faces[f_index] = f;
          if (r != 0 && r != nrings - 1) {
            int f_index2 = tri_index_fn(sphere, s, r, 1);
            faces[f_index2] = f2;
          }
        }
        else {
          int f_index = face_index_fn(sphere, s, r);
          faces[f_index] = f;
        }
      }
    }
  }
  Mesh mesh(faces);
  double time_create = PIL_check_seconds_timer();
  // write_obj_mesh(mesh, "spheresphere_in");
  Mesh out;
  if (use_self) {
    out = trimesh_self_intersect(mesh, &arena);
  }
  else {
    int nf = triangulate ? num_sphere_tris : num_sphere_faces;
    out = trimesh_nary_intersect(
        mesh, 2, [nf](int t) { return t < nf ? 0 : 1; }, false, &arena);
  }
  double time_intersect = PIL_check_seconds_timer();
  std::cout << "Create time: " << time_create - time_start << "\n";
  std::cout << "Intersect time: " << time_intersect - time_create << "\n";
  std::cout << "Total time: " << time_intersect - time_start << "\n";
  write_obj_mesh(out, "spheresphere");
}

TEST(mesh_intersect_perf, SphereSphere)
{
  spheresphere_test(64, 0.5, true);
}

#endif

}  // namespace blender::meshintersect
