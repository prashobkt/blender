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
 * The Original Code is Copyright (C) 2019 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup editors
 */

#include "BLI_linklist.h"
#include "BLI_listbase.h"
#include "BLI_math.h"

#include "BKE_customdata.h"
#include "BKE_object.h"

#include "DEG_depsgraph_query.h"

#include "DNA_camera_types.h"
#include "DNA_lineart_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_scene_types.h"

#include "ED_lineart.h"

#include "bmesh.h"

#include "lineart_intern.h"

#include <math.h>

#define LRT_OTHER_RV(rl, rv) ((rv) == (rl)->l ? (rl)->r : (rl)->l)

static LineartRenderLine *lineart_line_get_connected(LineartBoundingArea *ba,
                                                     LineartRenderVert *rv,
                                                     LineartRenderVert **new_rv,
                                                     int match_flag)
{
  LinkData *lip;
  LineartRenderLine *nrl;

  for (lip = ba->linked_lines.first; lip; lip = lip->next) {
    nrl = lip->data;

    if ((!(nrl->flags & LRT_EDGE_FLAG_ALL_TYPE)) || (nrl->flags & LRT_EDGE_FLAG_CHAIN_PICKED)) {
      continue;
    }

    if (match_flag && ((nrl->flags & LRT_EDGE_FLAG_ALL_TYPE) & match_flag) == 0) {
      continue;
    }

    /*  always chain connected lines for now. */
    /*  simplification will take care of the sharp points. */
    /*  if(cosine whatever) continue; */

    if (rv != nrl->l && rv != nrl->r) {
      if (nrl->flags & LRT_EDGE_FLAG_INTERSECTION) {
        if (rv->fbcoord[0] == nrl->l->fbcoord[0] && rv->fbcoord[1] == nrl->l->fbcoord[1]) {
          *new_rv = LRT_OTHER_RV(nrl, nrl->l);
          return nrl;
        }
        else {
          if (rv->fbcoord[0] == nrl->r->fbcoord[0] && rv->fbcoord[1] == nrl->r->fbcoord[1]) {
            *new_rv = LRT_OTHER_RV(nrl, nrl->r);
            return nrl;
          }
        }
      }
      continue;
    }

    *new_rv = LRT_OTHER_RV(nrl, rv);
    return nrl;
  }

  return 0;
}

static LineartRenderLineChain *lineart_chain_create(LineartRenderBuffer *rb)
{
  LineartRenderLineChain *rlc;
  rlc = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLineChain));

  BLI_addtail(&rb->chains, rlc);

  return rlc;
}

static bool lineart_point_overlapping(LineartRenderLineChainItem *rlci,
                                      float x,
                                      float y,
                                      double threshold)
{
  if (!rlci) {
    return false;
  }
  if (((rlci->pos[0] + threshold) >= x) && ((rlci->pos[0] - threshold) <= x) &&
      ((rlci->pos[1] + threshold) >= y) && ((rlci->pos[1] - threshold) <= y)) {
    return true;
  }
  return false;
}

static LineartRenderLineChainItem *lineart_chain_append_point(LineartRenderBuffer *rb,
                                                              LineartRenderLineChain *rlc,
                                                              float x,
                                                              float y,
                                                              float gx,
                                                              float gy,
                                                              float gz,
                                                              float *normal,
                                                              char type,
                                                              int level)
{
  LineartRenderLineChainItem *rlci;

  if (lineart_point_overlapping(rlc->chain.last, x, y, 1e-5)) {
    /* Because segment type is determined by the leading chain point, so we need to ensure the
     * type and occlusion is correct after omitting overlapping point*/
    LineartRenderLineChainItem *old_rlci = rlc->chain.last;
    old_rlci->line_type = type;
    old_rlci->occlusion = level;
    return old_rlci;
  }

  rlci = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLineChainItem));

  rlci->pos[0] = x;
  rlci->pos[1] = y;
  rlci->gpos[0] = gx;
  rlci->gpos[1] = gy;
  rlci->gpos[2] = gz;
  copy_v3_v3(rlci->normal, normal);
  rlci->line_type = type & LRT_EDGE_FLAG_ALL_TYPE;
  rlci->occlusion = level;
  BLI_addtail(&rlc->chain, rlci);

  /*  printf("a %f,%f %d\n", x, y, level); */

  return rlci;
}

static LineartRenderLineChainItem *lineart_chain_push_point(LineartRenderBuffer *rb,
                                                            LineartRenderLineChain *rlc,
                                                            float x,
                                                            float y,
                                                            float gx,
                                                            float gy,
                                                            float gz,
                                                            float *normal,
                                                            char type,
                                                            int level)
{
  LineartRenderLineChainItem *rlci;

  if (lineart_point_overlapping(rlc->chain.first, x, y, 1e-5)) {
    return rlc->chain.first;
  }

  rlci = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLineChainItem));

  rlci->pos[0] = x;
  rlci->pos[1] = y;
  rlci->gpos[0] = gx;
  rlci->gpos[1] = gy;
  rlci->gpos[2] = gz;
  copy_v3_v3(rlci->normal, normal);
  rlci->line_type = type & LRT_EDGE_FLAG_ALL_TYPE;
  rlci->occlusion = level;
  BLI_addhead(&rlc->chain, rlci);

  /*  printf("data %f,%f %d\n", x, y, level); */

  return rlci;
}

void ED_lineart_chain_feature_lines(LineartRenderBuffer *rb)
{
  LineartRenderLineChain *rlc;
  LineartRenderLineChainItem *rlci;
  LineartRenderLine *rl;
  LineartBoundingArea *ba;
  LineartRenderLineSegment *rls;
  int last_occlusion;

  for (rl = rb->all_render_lines.first; rl; rl = rl->next) {

    if ((!(rl->flags & LRT_EDGE_FLAG_ALL_TYPE)) || (rl->flags & LRT_EDGE_FLAG_CHAIN_PICKED)) {
      continue;
    }

    rl->flags |= LRT_EDGE_FLAG_CHAIN_PICKED;

    rlc = lineart_chain_create(rb);

    rlc->object_ref = rl->object_ref; /*  can only be the same object in a chain. */
    rlc->type = (rl->flags & LRT_EDGE_FLAG_ALL_TYPE);

    LineartRenderLine *new_rl = rl;
    LineartRenderVert *new_rv;
    float N[3] = {0};

    if (rl->tl) {
      N[0] += rl->tl->gn[0];
      N[1] += rl->tl->gn[1];
      N[2] += rl->tl->gn[2];
    }
    if (rl->tr) {
      N[0] += rl->tr->gn[0];
      N[1] += rl->tr->gn[1];
      N[2] += rl->tr->gn[2];
    }
    if (rl->tl || rl->tr) {
      normalize_v3(N);
    }

    /*  step 1: grow left */
    ba = ED_lineart_get_point_bounding_area_deep(rb, rl->l->fbcoord[0], rl->l->fbcoord[1]);
    new_rv = rl->l;
    rls = rl->segments.first;
    lineart_chain_push_point(rb,
                             rlc,
                             new_rv->fbcoord[0],
                             new_rv->fbcoord[1],
                             new_rv->gloc[0],
                             new_rv->gloc[1],
                             new_rv->gloc[2],
                             N,
                             rl->flags,
                             rls->occlusion);
    while (ba && (new_rl = lineart_line_get_connected(ba, new_rv, &new_rv, rl->flags))) {
      new_rl->flags |= LRT_EDGE_FLAG_CHAIN_PICKED;

      if (new_rl->tl || new_rl->tr) {
        zero_v3(N);
        if (new_rl->tl) {
          N[0] += new_rl->tl->gn[0];
          N[1] += new_rl->tl->gn[1];
          N[2] += new_rl->tl->gn[2];
        }
        if (new_rl->tr) {
          N[0] += new_rl->tr->gn[0];
          N[1] += new_rl->tr->gn[1];
          N[2] += new_rl->tr->gn[2];
        }
        normalize_v3(N);
      }

      if (new_rv == new_rl->l) {
        for (rls = new_rl->segments.last; rls; rls = rls->prev) {
          double gpos[3], lpos[3];
          double *lfb = new_rl->l->fbcoord, *rfb = new_rl->r->fbcoord;
          double global_at = lfb[2] * rls->at / (rls->at * lfb[2] + (1 - rls->at) * rfb[2]);
          interp_v3_v3v3_db(lpos, new_rl->l->fbcoord, new_rl->r->fbcoord, rls->at);
          interp_v3_v3v3_db(gpos, new_rl->l->gloc, new_rl->r->gloc, global_at);
          lineart_chain_push_point(rb,
                                   rlc,
                                   lpos[0],
                                   lpos[1],
                                   gpos[0],
                                   gpos[1],
                                   gpos[2],
                                   N,
                                   new_rl->flags,
                                   rls->occlusion);
          last_occlusion = rls->occlusion;
        }
      }
      else if (new_rv == new_rl->r) {
        rls = new_rl->segments.first;
        last_occlusion = rls->occlusion;
        rls = rls->next;
        for (; rls; rls = rls->next) {
          double gpos[3], lpos[3];
          double *lfb = new_rl->l->fbcoord, *rfb = new_rl->r->fbcoord;
          double global_at = lfb[2] * rls->at / (rls->at * lfb[2] + (1 - rls->at) * rfb[2]);
          interp_v3_v3v3_db(lpos, new_rl->l->fbcoord, new_rl->r->fbcoord, rls->at);
          interp_v3_v3v3_db(gpos, new_rl->l->gloc, new_rl->r->gloc, global_at);
          lineart_chain_push_point(rb,
                                   rlc,
                                   lpos[0],
                                   lpos[1],
                                   gpos[0],
                                   gpos[1],
                                   gpos[2],
                                   N,
                                   new_rl->flags,
                                   last_occlusion);
          last_occlusion = rls->occlusion;
        }
        lineart_chain_push_point(rb,
                                 rlc,
                                 new_rl->r->fbcoord[0],
                                 new_rl->r->fbcoord[1],
                                 new_rl->r->gloc[0],
                                 new_rl->r->gloc[1],
                                 new_rl->r->gloc[2],
                                 N,
                                 new_rl->flags,
                                 last_occlusion);
      }
      ba = ED_lineart_get_point_bounding_area_deep(rb, new_rv->fbcoord[0], new_rv->fbcoord[1]);
    }

    /* Restore normal value */
    if (rl->tl || rl->tr) {
      zero_v3(N);
      if (rl->tl) {
        N[0] += rl->tl->gn[0];
        N[1] += rl->tl->gn[1];
        N[2] += rl->tl->gn[2];
      }
      if (rl->tr) {
        N[0] += rl->tr->gn[0];
        N[1] += rl->tr->gn[1];
        N[2] += rl->tr->gn[2];
      }
      normalize_v3(N);
    }
    /*  step 2: this line */
    rls = rl->segments.first;
    last_occlusion = ((LineartRenderLineSegment *)rls)->occlusion;
    for (rls = rls->next; rls; rls = rls->next) {
      double gpos[3], lpos[3];
      double *lfb = rl->l->fbcoord, *rfb = rl->r->fbcoord;
      double global_at = lfb[2] * rls->at / (rls->at * lfb[2] + (1 - rls->at) * rfb[2]);
      interp_v3_v3v3_db(lpos, rl->l->fbcoord, rl->r->fbcoord, rls->at);
      interp_v3_v3v3_db(gpos, rl->l->gloc, rl->r->gloc, global_at);
      lineart_chain_append_point(
          rb, rlc, lpos[0], lpos[1], gpos[0], gpos[1], gpos[2], N, rl->flags, rls->occlusion);
      last_occlusion = rls->occlusion;
    }
    lineart_chain_append_point(rb,
                               rlc,
                               rl->r->fbcoord[0],
                               rl->r->fbcoord[1],
                               rl->r->gloc[0],
                               rl->r->gloc[1],
                               rl->r->gloc[2],
                               N,
                               rl->flags,
                               last_occlusion);

    /*  step 3: grow right */
    ba = ED_lineart_get_point_bounding_area_deep(rb, rl->r->fbcoord[0], rl->r->fbcoord[1]);
    new_rv = rl->r;
    /*  below already done in step 2 */
    /*  lineart_chain_push_point(rb,rlc,new_rv->fbcoord[0],new_rv->fbcoord[1],rl->flags,0);
     */
    while (ba && (new_rl = lineart_line_get_connected(ba, new_rv, &new_rv, rl->flags))) {
      new_rl->flags |= LRT_EDGE_FLAG_CHAIN_PICKED;

      if (new_rl->tl || new_rl->tr) {
        zero_v3(N);
        if (new_rl->tl) {
          N[0] += new_rl->tl->gn[0];
          N[1] += new_rl->tl->gn[1];
          N[2] += new_rl->tl->gn[2];
        }
        if (new_rl->tr) {
          N[0] += new_rl->tr->gn[0];
          N[1] += new_rl->tr->gn[1];
          N[2] += new_rl->tr->gn[2];
        }
        normalize_v3(N);
      }

      /*  fix leading vertex type */
      rlci = rlc->chain.last;
      rlci->line_type = new_rl->flags & LRT_EDGE_FLAG_ALL_TYPE;

      if (new_rv == new_rl->l) {
        rls = new_rl->segments.last;
        last_occlusion = rls->occlusion;
        rlci->occlusion = last_occlusion; /*  fix leading vertex occlusion */
        for (rls = new_rl->segments.last; rls; rls = rls->prev) {
          double gpos[3], lpos[3];
          double *lfb = new_rl->l->fbcoord, *rfb = new_rl->r->fbcoord;
          double global_at = lfb[2] * rls->at / (rls->at * lfb[2] + (1 - rls->at) * rfb[2]);
          interp_v3_v3v3_db(lpos, new_rl->l->fbcoord, new_rl->r->fbcoord, rls->at);
          interp_v3_v3v3_db(gpos, new_rl->l->gloc, new_rl->r->gloc, global_at);
          last_occlusion = rls->prev ? rls->prev->occlusion : last_occlusion;
          lineart_chain_append_point(rb,
                                     rlc,
                                     lpos[0],
                                     lpos[1],
                                     gpos[0],
                                     gpos[1],
                                     gpos[2],
                                     N,
                                     new_rl->flags,
                                     last_occlusion);
        }
      }
      else if (new_rv == new_rl->r) {
        rls = new_rl->segments.first;
        last_occlusion = rls->occlusion;
        rlci->occlusion = last_occlusion;
        rls = rls->next;
        for (; rls; rls = rls->next) {
          double gpos[3], lpos[3];
          double *lfb = new_rl->l->fbcoord, *rfb = new_rl->r->fbcoord;
          double global_at = lfb[2] * rls->at / (rls->at * lfb[2] + (1 - rls->at) * rfb[2]);
          interp_v3_v3v3_db(lpos, new_rl->l->fbcoord, new_rl->r->fbcoord, rls->at);
          interp_v3_v3v3_db(gpos, new_rl->l->gloc, new_rl->r->gloc, global_at);
          lineart_chain_append_point(rb,
                                     rlc,
                                     lpos[0],
                                     lpos[1],
                                     gpos[0],
                                     gpos[1],
                                     gpos[2],
                                     N,
                                     new_rl->flags,
                                     rls->occlusion);
          last_occlusion = rls->occlusion;
        }
        lineart_chain_append_point(rb,
                                   rlc,
                                   new_rl->r->fbcoord[0],
                                   new_rl->r->fbcoord[1],
                                   new_rl->r->gloc[0],
                                   new_rl->r->gloc[1],
                                   new_rl->r->gloc[2],
                                   N,
                                   new_rl->flags,
                                   last_occlusion);
      }
      ba = ED_lineart_get_point_bounding_area_deep(rb, new_rv->fbcoord[0], new_rv->fbcoord[1]);
    }
  }
}

static LineartBoundingArea *lineart_bounding_area_get_rlci_recursive(
    LineartRenderBuffer *rb, LineartBoundingArea *root, LineartRenderLineChainItem *rlci)
{
  if (root->child == NULL) {
    return root;
  }
  else {
    LineartBoundingArea *ch = root->child;
#define IN_BOUND(ba, rlci) \
  ba.l <= rlci->pos[0] && ba.r >= rlci->pos[0] && ba.b <= rlci->pos[1] && ba.u >= rlci->pos[1]

    if (IN_BOUND(ch[0], rlci)) {
      return lineart_bounding_area_get_rlci_recursive(rb, &ch[0], rlci);
    }
    else if (IN_BOUND(ch[1], rlci)) {
      return lineart_bounding_area_get_rlci_recursive(rb, &ch[1], rlci);
    }
    else if (IN_BOUND(ch[2], rlci)) {
      return lineart_bounding_area_get_rlci_recursive(rb, &ch[2], rlci);
    }
    else if (IN_BOUND(ch[3], rlci)) {
      return lineart_bounding_area_get_rlci_recursive(rb, &ch[3], rlci);
    }
#undef IN_BOUND
  }
  return NULL;
}

static LineartBoundingArea *lineart_bounding_area_get_end_point(LineartRenderBuffer *rb,
                                                                LineartRenderLineChainItem *rlci)
{
  if (!rlci) {
    return NULL;
  }
  LineartBoundingArea *root = ED_lineart_get_point_bounding_area(rb, rlci->pos[0], rlci->pos[1]);
  if (root == NULL) {
    return NULL;
  }
  return lineart_bounding_area_get_rlci_recursive(rb, root, rlci);
}

/*  if reduction threshold is even larger than a small bounding area, */
/*  then 1) geometry is simply too dense. */
/*       2) probably need to add it to root bounding area which has larger surface area then it
 * will */
/*       cover typical threshold values. */
static void lineart_bounding_area_link_point_recursive(LineartRenderBuffer *rb,
                                                       LineartBoundingArea *root,
                                                       LineartRenderLineChain *rlc,
                                                       LineartRenderLineChainItem *rlci)
{
  if (root->child == NULL) {
    LineartChainRegisterEntry *cre = list_append_pointer_static_sized(
        &root->linked_chains, &rb->render_data_pool, rlc, sizeof(LineartChainRegisterEntry));

    cre->rlci = rlci;

    if (rlci == rlc->chain.first) {
      cre->is_left = 1;
    }
  }
  else {
    LineartBoundingArea *ch = root->child;

#define IN_BOUND(ba, rlci) \
  ba.l <= rlci->pos[0] && ba.r >= rlci->pos[0] && ba.b <= rlci->pos[1] && ba.u >= rlci->pos[1]

    if (IN_BOUND(ch[0], rlci)) {
      lineart_bounding_area_link_point_recursive(rb, &ch[0], rlc, rlci);
    }
    else if (IN_BOUND(ch[1], rlci)) {
      lineart_bounding_area_link_point_recursive(rb, &ch[1], rlc, rlci);
    }
    else if (IN_BOUND(ch[2], rlci)) {
      lineart_bounding_area_link_point_recursive(rb, &ch[2], rlc, rlci);
    }
    else if (IN_BOUND(ch[3], rlci)) {
      lineart_bounding_area_link_point_recursive(rb, &ch[3], rlc, rlci);
    }

#undef IN_BOUND
  }
}

static void lineart_bounding_area_link_chain(LineartRenderBuffer *rb, LineartRenderLineChain *rlc)
{
  LineartRenderLineChainItem *pl = rlc->chain.first;
  LineartRenderLineChainItem *pr = rlc->chain.last;
  LineartBoundingArea *ba1 = ED_lineart_get_point_bounding_area(rb, pl->pos[0], pl->pos[1]);
  LineartBoundingArea *ba2 = ED_lineart_get_point_bounding_area(rb, pr->pos[0], pr->pos[1]);

  if (ba1) {
    lineart_bounding_area_link_point_recursive(rb, ba1, rlc, pl);
  }
  if (ba2) {
    lineart_bounding_area_link_point_recursive(rb, ba2, rlc, pr);
  }
}

void ED_lineart_chain_split_for_fixed_occlusion(LineartRenderBuffer *rb)
{
  LineartRenderLineChain *rlc, *new_rlc;
  LineartRenderLineChainItem *rlci, *next_rlci, *prev_rlci;
  ListBase swap = {0};

  swap.first = rb->chains.first;
  swap.last = rb->chains.last;

  rb->chains.last = rb->chains.first = NULL;

  while ((rlc = BLI_pophead(&swap)) != NULL) {
    rlc->next = rlc->prev = NULL;
    BLI_addtail(&rb->chains, rlc);
    LineartRenderLineChainItem *first_rlci = (LineartRenderLineChainItem *)rlc->chain.first;
    int fixed_occ = first_rlci->occlusion;
    rlc->level = fixed_occ;
    for (rlci = first_rlci->next; rlci; rlci = next_rlci) {
      next_rlci = rlci->next;
      prev_rlci = rlci->prev;
      if (rlci->occlusion != fixed_occ) {
        if (next_rlci) {
          if (lineart_point_overlapping(next_rlci, rlci->pos[0], rlci->pos[1], 1e-5)) {
            // next_rlci = next_rlci->next;
            continue;
          }
        }
        else {
          break; /* No need to split at the last point anyway.*/
        }
        new_rlc = lineart_chain_create(rb);
        new_rlc->chain.first = rlci;
        new_rlc->chain.last = rlc->chain.last;
        rlc->chain.last = rlci->prev;
        ((LineartRenderLineChainItem *)rlc->chain.last)->next = 0;
        rlci->prev = 0;

        /*  end the previous one */
        lineart_chain_append_point(rb,
                                   rlc,
                                   rlci->pos[0],
                                   rlci->pos[1],
                                   rlci->gpos[0],
                                   rlci->gpos[1],
                                   rlci->gpos[2],
                                   rlci->normal,
                                   rlci->line_type,
                                   fixed_occ);
        new_rlc->object_ref = rlc->object_ref;
        new_rlc->type = rlc->type;
        rlc = new_rlc;
        fixed_occ = rlci->occlusion;
        rlc->level = fixed_occ;
      }
    }
  }
  for (rlc = rb->chains.first; rlc; rlc = rlc->next) {
    lineart_bounding_area_link_chain(rb, rlc);
  }
}

/*  note: segment type (crease/material/contour...) is ambiguous after this. */
static void lineart_chain_connect(LineartRenderBuffer *UNUSED(rb),
                                  LineartRenderLineChain *onto,
                                  LineartRenderLineChain *sub,
                                  int reverse_1,
                                  int reverse_2)
{
  LineartRenderLineChainItem *rlci;
  if (onto->object_ref && !sub->object_ref) {
    sub->object_ref = onto->object_ref;
    sub->type = onto->type;
  }
  else if (sub->object_ref && !onto->object_ref) {
    onto->object_ref = sub->object_ref;
    onto->type = sub->type;
  }
  if (!reverse_1) {  /*  L--R L-R */
    if (reverse_2) { /*  L--R R-L */
      BLI_listbase_reverse(&sub->chain);
    }
    rlci = sub->chain.first;
    if (lineart_point_overlapping(onto->chain.last, rlci->pos[0], rlci->pos[1], 1e-5)) {
      BLI_pophead(&sub->chain);
      if (sub->chain.first == NULL) {
        return;
      }
    }
    ((LineartRenderLineChainItem *)onto->chain.last)->next = sub->chain.first;
    ((LineartRenderLineChainItem *)sub->chain.first)->prev = onto->chain.last;
    onto->chain.last = sub->chain.last;
  }
  else {              /*  L-R L--R */
    if (!reverse_2) { /*  R-L L--R */
      BLI_listbase_reverse(&sub->chain);
    }
    rlci = onto->chain.first;
    if (lineart_point_overlapping(sub->chain.last, rlci->pos[0], rlci->pos[1], 1e-5)) {
      BLI_pophead(&onto->chain);
      if (onto->chain.first == NULL) {
        return;
      }
    }
    ((LineartRenderLineChainItem *)sub->chain.last)->next = onto->chain.first;
    ((LineartRenderLineChainItem *)onto->chain.first)->prev = sub->chain.last;
    onto->chain.first = sub->chain.first;
  }
}

LineartChainRegisterEntry *lineart_chain_get_closest_cre(LineartRenderBuffer *rb,
                                                         LineartBoundingArea *ba,
                                                         LineartRenderLineChain *rlc,
                                                         LineartRenderLineChainItem *rlci,
                                                         int occlusion,
                                                         float dist,
                                                         int do_geometry_space)
{

  LineartChainRegisterEntry *cre, *next_cre, *closest_cre = NULL;
  for (cre = ba->linked_chains.first; cre; cre = next_cre) {
    next_cre = cre->next;
    if (cre->rlc->object_ref != rlc->object_ref) {
      if (rb->fuzzy_everything || rb->fuzzy_intersections) {
        /* if both have object_ref, then none is intersection line. */
        if (cre->rlc->object_ref && rlc->object_ref) {
          continue; /* We don't want to chain along different objects at the moment. */
        }
      }
      else {
        continue;
      }
    }
    if (cre->rlc->picked) {
      BLI_remlink(&ba->linked_chains, cre);
      continue;
    }
    if (cre->rlc == rlc || (!cre->rlc->chain.first) || (cre->rlc->level != occlusion)) {
      continue;
    }
    if (!rb->fuzzy_everything) {
      if (cre->rlc->type != rlc->type) {
        if (rb->fuzzy_intersections) {
          if (!(cre->rlc->type == LRT_EDGE_FLAG_INTERSECTION ||
                rlc->type == LRT_EDGE_FLAG_INTERSECTION)) {
            continue; /* fuzzy intersetions but no intersection line found. */
          }
        }
        else { /* line type different but no fuzzy */
          continue;
        }
      }
    }

    float new_len = do_geometry_space ? len_v3v3(cre->rlci->gpos, rlci->gpos) :
                                        len_v2v2(cre->rlci->pos, rlci->pos);
    if (new_len < dist) {
      closest_cre = cre;
      dist = new_len;
    }
  }
  return closest_cre;
}

/*  this only does head-tail connection. */
/*  overlapping / tiny isolated segment / loop reduction not implemented here yet. */
void ED_lineart_chain_connect(LineartRenderBuffer *rb, const int do_geometry_space)
{
  LineartRenderLineChain *rlc;
  LineartRenderLineChainItem *rlci;
  LineartBoundingArea *ba;
  LineartChainRegisterEntry *closest_cre_l, *closest_cre_r;
  float dist = do_geometry_space ? rb->chaining_geometry_threshold : rb->chaining_image_threshold;
  int occlusion;
  ListBase swap = {0};

  if ((!do_geometry_space && rb->chaining_image_threshold < 0.0001) ||
      (do_geometry_space && rb->chaining_geometry_threshold < 0.0001)) {
    return;
  }

  swap.first = rb->chains.first;
  swap.last = rb->chains.last;

  rb->chains.last = rb->chains.first = NULL;

  while ((rlc = BLI_pophead(&swap)) != NULL) {
    rlc->next = rlc->prev = NULL;
    if (rlc->picked) {
      continue;
    }
    BLI_addtail(&rb->chains, rlc);

    rlc->picked = 1;

    occlusion = ((LineartRenderLineChainItem *)rlc->chain.first)->occlusion;

    rlci = rlc->chain.last;
    while (rlci && ((ba = lineart_bounding_area_get_end_point(rb, rlci)) != NULL)) {
      if (ba->linked_chains.first == NULL) {
        break;
      }
      closest_cre_l = lineart_chain_get_closest_cre(
          rb, ba, rlc, rlci, occlusion, dist, do_geometry_space);
      if (closest_cre_l) {
        closest_cre_l->picked = 1;
        closest_cre_l->rlc->picked = 1;
        BLI_remlink(&ba->linked_chains, closest_cre_l);
        if (closest_cre_l->is_left) {
          lineart_chain_connect(rb, rlc, closest_cre_l->rlc, 0, 0);
        }
        else {
          lineart_chain_connect(rb, rlc, closest_cre_l->rlc, 0, 1);
        }
        BLI_remlink(&swap, closest_cre_l->rlc);
      }
      else {
        break;
      }
      rlci = rlc->chain.last;
    }

    rlci = rlc->chain.first;
    while (rlci && ((ba = lineart_bounding_area_get_end_point(rb, rlci)) != NULL)) {
      closest_cre_r = NULL;
      if (ba->linked_chains.first == NULL) {
        break;
      }
      closest_cre_r = lineart_chain_get_closest_cre(
          rb, ba, rlc, rlci, occlusion, dist, do_geometry_space);
      if (closest_cre_r) {
        closest_cre_r->picked = 1;
        closest_cre_r->rlc->picked = 1;
        BLI_remlink(&ba->linked_chains, closest_cre_r);
        if (closest_cre_r->is_left) {
          lineart_chain_connect(rb, rlc, closest_cre_r->rlc, 1, 0);
        }
        else {
          lineart_chain_connect(rb, rlc, closest_cre_r->rlc, 1, 1);
        }
        BLI_remlink(&swap, closest_cre_r->rlc);
      }
      else {
        break;
      }
      rlci = rlc->chain.first;
    }
  }
}

/* length is in image space */
float ED_lineart_chain_compute_length(LineartRenderLineChain *rlc)
{
  LineartRenderLineChainItem *rlci;
  float offset_accum = 0;
  float dist;
  float last_point[2];

  rlci = rlc->chain.first;
  copy_v2_v2(last_point, rlci->pos);
  for (rlci = rlc->chain.first; rlci; rlci = rlci->next) {
    dist = len_v2v2(rlci->pos, last_point);
    offset_accum += dist;
    copy_v2_v2(last_point, rlci->pos);
  }
  return offset_accum;
}

void ED_lineart_chain_discard_short(LineartRenderBuffer *rb, const float threshold)
{
  LineartRenderLineChain *rlc, *next_rlc;
  for (rlc = rb->chains.first; rlc; rlc = next_rlc) {
    next_rlc = rlc->next;
    if (ED_lineart_chain_compute_length(rlc) < threshold) {
      BLI_remlink(&rb->chains, rlc);
    }
  }
}

int ED_lineart_chain_count(const LineartRenderLineChain *rlc)
{
  LineartRenderLineChainItem *rlci;
  int count = 0;
  for (rlci = rlc->chain.first; rlci; rlci = rlci->next) {
    count++;
  }
  return count;
}

void ED_lineart_chain_clear_picked_flag(LineartRenderBuffer *rb)
{
  LineartRenderLineChain *rlc;
  if (rb == NULL) {
    return;
  }
  for (rlc = rb->chains.first; rlc; rlc = rlc->next) {
    rlc->picked = 0;
  }
}
