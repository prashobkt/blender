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
    return other.i == one.i;
  }
  friend bool operator!=(const vert_treplet &one, const vert_treplet &other)
  {
    return !(one == other);
  }
};

static std::pair<int, int> ed_key_mlen(const vert_treplet &v1, const vert_treplet &v2)
{
  if (v1.mlen > v2.mlen) {
    return {v2.i, v1.i};
  }
  return {v1.i, v2.i};
}

static bool join_segments(Vector<vert_treplet> &seg1, Vector<vert_treplet> &seg2)
{
  if (seg1.last().v == seg2.last().v) {
    Vector<vert_treplet> &temp = seg1;
    seg1 = seg2;
    seg2 = temp;
  }
  else {
    return false;
  }
  seg1.remove_last();
  seg2.extend(seg1);
  if (seg1.last().v == seg1[0].v) {
    seg1.remove_last();
  }
  seg2.clear();
  return true;
}

static void tessellate_polygon(Vector<Vector<const float3 *>> &polyLineSeq,
                               Vector<Vector<int>> &r_new_line_seq)
{
  int64_t totpoints = 0;
  /* Display #ListBase. */
  ListBase dispbase = {NULL, NULL};
  DispList *dl;
  const int64_t len_polylines{polyLineSeq.size()};

  for (int i = 0; i < len_polylines; i++) {
    Vector<const float3 *> &polyLine = polyLineSeq[i];

    const int64_t len_polypoints{polyLine.size()};
    totpoints += len_polypoints;
    if (len_polypoints > 0) { /* don't bother adding edges as polylines */
      dl = static_cast<DispList *>(MEM_callocN(sizeof(DispList), __func__));
      BLI_addtail(&dispbase, dl);
      dl->type = DL_INDEX3;
      dl->nr = len_polypoints;
      dl->type = DL_POLY;
      dl->parts = 1; /* no faces, 1 edge loop */
      dl->col = 0;   /* no material */
      dl->verts = static_cast<float *>(MEM_mallocN(sizeof(float[3]) * len_polypoints, "dl verts"));
      dl->index = static_cast<int *>(MEM_callocN(sizeof(int[3]) * len_polypoints, "dl index"));
    }
  }

  if (totpoints) {
    /* now make the list to return */
    BKE_displist_fill(&dispbase, &dispbase, NULL, false);

    /* The faces are stored in a new DisplayList
     * that's added to the head of the #ListBase. */
    dl = static_cast<DispList *>(dispbase.first);

    int *dl_face = dl->index;
    r_new_line_seq.append({dl_face[0], dl_face[1], dl_face[2]});
    BKE_displist_free(&dispbase);
  }
  else {
    /* no points, do this so scripts don't barf */
    BKE_displist_free(&dispbase); /* possible some dl was allocated */
  }
}

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

  Set<std::pair<int, int>> used_edges;
  Set<std::pair<int, int>> double_edges;

  for (const Array<int, 2> &edge : edges) {
    std::pair<int, int> edge_key = ed_key_mlen(verts[edge[0]], verts[edge[1]]);
    if (used_edges.contains(edge_key)) {
      double_edges.add(edge_key);
    }
    else {
      used_edges.add(edge_key);
    }
  }

  Vector<Vector<vert_treplet>> loop_segments;
  {
    const vert_treplet *vert_prev = &verts[0];
    Vector<vert_treplet> contex_loop{1, *vert_prev};
    loop_segments.append(contex_loop);
    for (const vert_treplet &vertex : verts) {
      if (vertex == *vert_prev) {
        continue;
      }
      if (double_edges.contains(ed_key_mlen(vertex, *vert_prev))) {
        contex_loop = {vertex};
        loop_segments.append(contex_loop);
      }
      else {
        if (!contex_loop.is_empty() && contex_loop.last() == vertex) {
        }
        else {
          contex_loop.append(vertex);
        }
      }
      vert_prev = &vertex;
    }
  }

  bool joining_segements = true;
  while (joining_segements) {
    joining_segements = false;
    const int segcount = loop_segments.size();
    for (int j = segcount - 1; j >= 0; j--) {
      Vector<vert_treplet> seg_j = loop_segments[j];
      if (seg_j.is_empty()) {
        continue;
      }
      for (int k = j - 1; k >= 0; k--) {
        if (seg_j.is_empty()) {
          break;
        }
        Vector<vert_treplet> seg_k = loop_segments[k];
        if (!seg_j.is_empty() && join_segments(seg_j, seg_k)) {
          joining_segements = true;
        }
      }
    }
  }

  for (Vector<vert_treplet> &loop : loop_segments) {
    if (!loop.is_empty() && loop[0].v == loop.last().v) {
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

  Vector<int> vert_map{face_vertex_indices.size()};
  int ii = 0;
  for (Vector<vert_treplet> &verts : loop_list) {
    if (verts.size() <= 2) {
      continue;
    }
    for (int i = 0; i < verts.size(); i++) {
      vert_map[i + ii] = verts[i].i;
    }
  }

  Vector<Vector<const float3 *>> coord_list;
  for (int i = 0; i < loop_list.size(); i++) {
    Span<vert_treplet> loop = loop_list[i];
    Vector<const float3 *> vert_ptrs;
    for (const vert_treplet &vert : loop) {
      vert_ptrs.append(&vert.v);
    }
    coord_list.append(vert_ptrs);
  }
  Vector<Vector<int>> fill;
  tessellate_polygon(coord_list, fill);

  Vector<Vector<int>> fill_indices;
  Vector<Vector<int>> fill_indices_reversed;
  for (Span<int> f : fill) {
    Vector<int> corner(f.size());
    for (const int i : f) {
      corner.append(vert_map[i]);
    }
    fill_indices.append(corner);
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
    if (flip != 1) {
      for (int i = 0; i < fill_indices.size(); i++) {
        Vector<int> rev_face(fill_indices[i].size());
        for (const int fi : fill_indices[i]) {
          rev_face.append(fi);
        }
        fill_indices_reversed.append(rev_face);
      }
    }
  }

  if (!fill_indices_reversed.is_empty()) {
    fill_indices.clear();
    return fill_indices_reversed;
  }
  return fill_indices;
}

}  // namespace blender::io::obj
