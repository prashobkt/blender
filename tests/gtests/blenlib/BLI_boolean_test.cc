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

#if 0
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
#endif

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
  0 1 2
  0 3 1
  1 3 2
  2 3 0
  4 5 6
  4 7 5
  5 7 6
  6 7 4
  )";
  BT_input bti(spec);
#if 0
  Boolean_trimesh_output *out = BLI_boolean_trimesh(bti.input(), BOOLEAN_NONE);
  EXPECT_EQ(out->vert_len, 11);
  EXPECT_EQ(out->tri_len, 20);
  BLI_boolean_trimesh_free(out);
#endif
  Boolean_trimesh_output *out2 = BLI_boolean_trimesh(bti.input(), BOOLEAN_UNION);
  EXPECT_EQ(out2->vert_len, 10);
  EXPECT_EQ(out2->tri_len, 16);
  BLI_boolean_trimesh_free(out2);
}
