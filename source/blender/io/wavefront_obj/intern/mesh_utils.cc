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
 *
 * The Original Code is Copyright (C) 2020 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup obj
 */

#include "mesh_utils.hh"

#include "BKE_displist.h"

#include "BLI_array.hh"
#include "BLI_set.hh"

namespace blender::io::obj {

static float manhatten_len(const float3 coord)
{
  return abs(coord[0]) + abs(coord[1]) + abs(coord[2]);
}

struct vert_treplet {
  const float3 v;
  const int i;
  const float mlen;

  vert_treplet(float3 v, int i) : v(v), i(i), mlen(manhatten_len(v))
  {
  }
  friend bool operator==(const vert_treplet &one, const vert_treplet &other)
  {
    return other.v == one.v;
  }
  friend bool operator!=(const vert_treplet &one, const vert_treplet &other)
  {
    return !(one == other);
  }
};

static std::pair<float3, float3> ed_key_mlen(const vert_treplet &v1, const vert_treplet &v2)
{
  if (v2.mlen < v1.mlen) {
    return {v2.v, v1.v};
  }
  return {v1.v, v2.v};
}

static bool join_segments(Vector<vert_treplet> *r_seg1, Vector<vert_treplet> *r_seg2)
{
  if ((*r_seg1)[0].v == r_seg2->last().v) {
    Vector<vert_treplet> *temp = r_seg1;
    r_seg1 = r_seg2;
    r_seg2 = temp;
  }
  else if (r_seg1->last().v == (*r_seg2)[0].v) {
  }
  else {
    return false;
  }
  r_seg1->remove_last();
  r_seg1->extend(*r_seg2);
  if (r_seg1->last().v == (*r_seg1)[0].v) {
    r_seg1->remove_last();
  }
  r_seg2->clear();
  return true;
}

/**
 *  A simplified version of `M_Geometry_tessellate_polygon`.
 *
 * \param polyLineSeq List of polylines.
 * \param r_new_line_seq Empty vector that fill be filled with indices of corners of triangles.
 */

static void tessellate_polygon(const Vector<Vector<float3>> &polyLineSeq,
                               Vector<Vector<int>> &r_new_line_seq)
{
  int64_t totpoints = 0;
  /* Display #ListBase. */
  ListBase dispbase = {NULL, NULL};
  DispList *dl;
  const int64_t len_polylines{polyLineSeq.size()};

  for (int i = 0; i < len_polylines; i++) {
    Span<float3> polyLine = polyLineSeq[i];

    const int64_t len_polypoints{polyLine.size()};
    totpoints += len_polypoints;
    if (len_polypoints <= 0) { /* don't bother adding edges as polylines */
      continue;
    }
    dl = static_cast<DispList *>(MEM_callocN(sizeof(DispList), __func__));
    BLI_addtail(&dispbase, dl);
    dl->type = DL_INDEX3;
    dl->nr = len_polypoints;
    dl->type = DL_POLY;
    dl->parts = 1; /* no faces, 1 edge loop */
    dl->col = 0;   /* no material */
    dl->verts = static_cast<float *>(MEM_mallocN(sizeof(float[3]) * len_polypoints, "dl verts"));
    dl->index = static_cast<int *>(MEM_callocN(sizeof(int[3]) * len_polypoints, "dl index"));
    float *fp_verts = dl->verts;
    for (int j = 0; j < len_polypoints; j++, fp_verts += 3) {
      copy_v3_v3(fp_verts, polyLine[j]);
    }
  }

  if (totpoints) {
    /* now make the list to fill */
    BKE_displist_fill(&dispbase, &dispbase, NULL, false);

    /* The faces are stored in a new DisplayList
     * that's added to the head of the #ListBase. */
    dl = static_cast<DispList *>(dispbase.first);

    for (int index = 0, *dl_face = dl->index; index < dl->parts; index++, dl_face += 3) {
      r_new_line_seq.append({dl_face[0], dl_face[1], dl_face[2]});
    }
    BKE_displist_free(&dispbase);
  }
}

/**
 * Tessellate an ngon with holes to triangles.
 *
 * \param face_vertex_indices A polygon's indices that index into the given vertex coordinate list.
 * \return List of polygons with each element containing indices of one polygon.
 */
Vector<Vector<int>> ngon_tessellate(Span<float3> vertex_coords, Span<int> face_vertex_indices)
{
  if (face_vertex_indices.is_empty()) {
    return {};
  }
  Vector<vert_treplet> verts;
  verts.reserve(face_vertex_indices.size());

  for (int i = 0; i < face_vertex_indices.size(); i++) {
    verts.append({vertex_coords[face_vertex_indices[i]], i});
  }

  Vector<Array<int, 2>> edges;
  for (int i = 0; i < face_vertex_indices.size(); i++) {
    edges.append({i, i - 1});
  }
  edges[0] = {0, static_cast<int>(face_vertex_indices.size() - 1)};

  Set<std::pair<float3, float3>> edges_double;
  {
    Set<std::pair<float3, float3>> edges_used;
    for (Span<int> edge : edges) {
      std::pair<float3, float3> edge_key{ed_key_mlen(verts[edge[0]], verts[edge[1]])};
      if (edges_used.contains(edge_key)) {
        edges_double.add(edge_key);
      }
      else {
        edges_used.add(edge_key);
      }
    }
  }

  Vector<Vector<vert_treplet>> loop_segments;
  {
    const vert_treplet *vert_prev = &verts[0];
    Vector<vert_treplet> context_loop{1, *vert_prev};
    loop_segments.append(context_loop);
    for (const vert_treplet &vertex : verts) {
      if (vertex == *vert_prev) {
        continue;
      }
      if (edges_double.contains(ed_key_mlen(vertex, *vert_prev))) {
        context_loop = {vertex};
        loop_segments.append(context_loop);
      }
      else {
        if (!context_loop.is_empty() && context_loop.last() == vertex) {
        }
        else {
          loop_segments.last().append(vertex);
          context_loop.append(vertex);
        }
      }
      vert_prev = &vertex;
    }
  }

  bool joining_segements = true;
  while (joining_segements) {
    joining_segements = false;
    for (int j = loop_segments.size() - 1; j >= 0; j--) {
      Vector<vert_treplet> &seg_j = loop_segments[j];
      if (seg_j.is_empty()) {
        continue;
      }
      for (int k = j - 1; k >= 0; k--) {
        if (seg_j.is_empty()) {
          break;
        }
        Vector<vert_treplet> &seg_k = loop_segments[k];
        if (!seg_k.is_empty() && join_segments(&seg_j, &seg_k)) {
          joining_segements = true;
        }
      }
    }
  }

  for (Vector<vert_treplet> &loop : loop_segments) {
    while (!loop.is_empty() && loop[0].v == loop.last().v) {
      loop.remove_last();
    }
  }

  Vector<Vector<vert_treplet>> loop_list;
  for (Vector<vert_treplet> &loop : loop_segments) {
    if (loop.size() > 2) {
      loop_list.append(loop);
    }
  }
  // Done with loop fixing.

  Vector<int> vert_map(face_vertex_indices.size(), 0);
  int ii = 0;
  for (Span<vert_treplet> verts : loop_list) {
    if (verts.size() <= 2) {
      continue;
    }
    for (int i = 0; i < verts.size(); i++) {
      vert_map[i + ii] = verts[i].i;
    }
    ii += verts.size();
  }

  Vector<Vector<int>> fill;
  {
    Vector<Vector<float3>> coord_list;
    for (Span<vert_treplet> loop : loop_list) {
      Vector<float3> coord;
      for (const vert_treplet &vert : loop) {
        coord.append(vert.v);
      }
      coord_list.append(coord);
    }
    tessellate_polygon(coord_list, fill);
  }

  Vector<Vector<int>> fill_indices;
  Vector<Vector<int>> fill_indices_reversed;
  for (Span<int> f : fill) {
    Vector<int> tri;
    for (const int i : f) {
      tri.append(vert_map[i]);
    }
    fill_indices.append(tri);
  }

  if (fill_indices.is_empty()) {
    std::cerr << "Warning: could not scanfill, fallback on triangle fan" << std::endl;
    for (int i = 2; i < face_vertex_indices.size(); i++) {
      fill_indices.append({0, i - 1, i});
    }
  }
  else {
    int flip = -1;
    for (Span<int> fi : fill_indices) {
      if (flip != -1) {
        break;
      }
      for (int i = 0; i < fi.size(); i++) {
        if (fi[i] == 0 && fi[i - 1] == 1) {
          flip = 0;
          break;
        }
        if (fi[i] == 1 && fi[i - 1] == 0) {
          flip = 1;
          break;
        }
      }
    }
    if (flip == 1) {
      for (int i = 0; i < fill_indices.size(); i++) {
        Span<int> fi = fill_indices[i];
        Vector<int> rev_face(fi.size());
        for (int j = 0; j < rev_face.size(); j++) {
          rev_face[j] = fi[rev_face.size() - 1 - j];
        }
        fill_indices_reversed.append(rev_face);
      }
    }
  }

  if (!fill_indices_reversed.is_empty()) {
    return fill_indices_reversed;
  }
  return fill_indices;
}

}  // namespace blender::io::obj
