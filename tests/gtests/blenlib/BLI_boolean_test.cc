/* Apache License, Version 2.0 */

#include "testing/testing.h"

#include <fstream>
#include <iostream>
#include <sstream>

#include "MEM_guardedalloc.h"
#include "gmpxx.h"

#include "BLI_array.hh"
#include "BLI_boolean.h"
#include "BLI_mpq3.hh"
#include "BLI_vector.hh"

/* Class that can make a Boolean_trimesh_input from a string spec.
 * The spec has #verts #tris on the first line, then all the vert coords,
 * then all the tris as vert index triples.
 */
class BT_input {
 public:
  BT_input(const char *spec)
  {
    std::istringstream ss(spec);
    std::string line;
    getline(ss, line);
    std::istringstream hdrss(line);
    m_bti.vert_coord = nullptr;
    m_bti.tri = nullptr;
    hdrss >> m_bti.vert_len >> m_bti.tri_len;
    if (m_bti.vert_len > 0 && m_bti.tri_len > 0) {
      m_bti.vert_coord = new float[m_bti.vert_len][3];
      m_bti.tri = new int[m_bti.tri_len][3];
      int i = 0;
      while (i < m_bti.vert_len && getline(ss, line)) {
        std::istringstream iss(line);
        iss >> m_bti.vert_coord[i][0] >> m_bti.vert_coord[i][1] >> m_bti.vert_coord[i][2];
        ++i;
      }
      i = 0;
      while (i < m_bti.tri_len && getline(ss, line)) {
        std::istringstream tss(line);
        tss >> m_bti.tri[i][0] >> m_bti.tri[i][1] >> m_bti.tri[i][2];
        ++i;
      }
    }
  }

  ~BT_input()
  {
    delete[] m_bti.vert_coord;
    delete[] m_bti.tri;
  }

  Boolean_trimesh_input *input()
  {
    return &m_bti;
  }

 private:
  Boolean_trimesh_input m_bti;
};

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

static void write_obj(const Boolean_trimesh_output *out, const std::string objname)
{
  constexpr const char *objdir = "/tmp/";
  if (out->tri_len == 0) {
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

  for (int v = 0; v < out->vert_len; ++v) {
    float *co = out->vert_coord[v];
    f << "v " << co[0] << " " << co[1] << " " << co[2] << "\n";
  }

  for (int i = 0; i < out->tri_len; ++i) {
    int matindex = i % numcolors;
    f << "usemtl mat" + std::to_string(matindex) + "\n";
    /* OBJ files use 1-indexing for vertices. */
    int *tri = out->tri[i];
    f << "f " << tri[0] + 1 << " " << tri[1] + 1 << " " << tri[2] + 1 << "\n";
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

constexpr bool DO_OBJ = true;

TEST(eboolean, Empty)
{
  Boolean_trimesh_input in;
  in.vert_len = 0;
  in.tri_len = 0;
  in.vert_coord = NULL;
  in.tri = NULL;
  Boolean_trimesh_output *out = BLI_boolean_trimesh(&in, BOOLEAN_NONE);
  EXPECT_EQ(out->vert_len, 0);
  EXPECT_EQ(out->tri_len, 0);
  BLI_boolean_trimesh_free(out);
}

TEST(eboolean, TetTet)
{
  const char *spec = R"(8 8
  0.0 0.0 0.0
  2.0 0.0 0.0
  1.0 2.0 0.0
  1.0 1.0 2.0
  0.0 0.0 1.0
  2.0 0.0 1.0
  1.0 2.0 1.0
  1.0 1.0 3.0
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
  Boolean_trimesh_output *out = BLI_boolean_trimesh(bti.input(), BOOLEAN_NONE);
  EXPECT_EQ(out->vert_len, 11);
  EXPECT_EQ(out->tri_len, 20);
  BLI_boolean_trimesh_free(out);
  if (DO_OBJ) {
    write_obj(out, "tettet");
  }

  Boolean_trimesh_output *out2 = BLI_boolean_trimesh(bti.input(), BOOLEAN_UNION);
  EXPECT_EQ(out2->vert_len, 10);
  EXPECT_EQ(out2->tri_len, 16);
  if (DO_OBJ) {
    write_obj(out2, "tettet_union");
  }
  BLI_boolean_trimesh_free(out2);
}

TEST(eboolean, TetTet2)
{
  const char *spec = R"(8 8
  0.0 1.0 -1.0
  0.875 -0.5 -1.0
  -0.875 -0.5 -1.0
  0.0 0.0 1.0
  0.0 1.0 0.0
  0.875 -0.5 0.0
  -0.875 -0.5 0.0
  0.0 0.0 2.0
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
  Boolean_trimesh_output *out = BLI_boolean_trimesh(bti.input(), BOOLEAN_UNION);
  EXPECT_EQ(out->vert_len, 10);
  EXPECT_EQ(out->tri_len, 16);
  if (DO_OBJ) {
    write_obj(out, "tettet2_union");
  }
  BLI_boolean_trimesh_free(out);
}
