
#ifndef TETGEN_API_H
#define TETGEN_API_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TetGenRemeshData {
  float *in_verts;
  unsigned int *in_faces;
  int in_totfaces;
  int in_totverts;

  float *out_verts;
  unsigned int *out_facets;
  unsigned int *out_tets;
  int out_totverts;
  int out_totfacets;
  int out_tottets;
} TetGenRemeshData;

void init_tetgenremeshdata(TetGenRemeshData *data);

// Returns true on success
bool tetgen_resmesh(TetGenRemeshData *tg);

#ifdef __cplusplus
}
#endif

#endif  // TETGEN_API_H
