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
 * The Original Code is Copyright (C) 2008, Blender Foundation
 * This is a new part of Blender
 */

/** \file
 * \ingroup edgpencil
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLI_math.h"

#include "BKE_gpencil.h"
#include "BKE_gpencil_geom.h"
#include "BKE_image.h"

#include "DNA_gpencil_types.h"
#include "DNA_image_types.h"
#include "DNA_object_types.h"

#include "IMB_imbuf.h"
#include "IMB_imbuf_types.h"

#include "gpencil_trace.h"

/**
 * Print trace bitmap for debuging
 * \param f: Output handle. Use stderr for printing
 * \param bm: Trace bitmap
 */
void ED_gpencil_trace_bm_print(FILE *f, const potrace_bitmap_t *bm)
{
  int x, y;
  int xx, yy;
  int d;
  int sw, sh;

  sw = bm->w < 79 ? bm->w : 79;
  sh = bm->w < 79 ? bm->h : bm->h * sw * 44 / (79 * bm->w);

  for (yy = sh - 1; yy >= 0; yy--) {
    for (xx = 0; xx < sw; xx++) {
      d = 0;
      for (x = xx * bm->w / sw; x < (xx + 1) * bm->w / sw; x++) {
        for (y = yy * bm->h / sh; y < (yy + 1) * bm->h / sh; y++) {
          if (BM_GET(bm, x, y)) {
            d++;
          }
        }
      }
      fputc(d ? '*' : ' ', f);
    }
    fputc('\n', f);
  }
}

/**
 * Return new un-initialized trace bitmap
 * \param w: Width in pixels
 * \param h: Height in pixels
 * \return: Trace bitmap
 */
potrace_bitmap_t *ED_gpencil_trace_bm_new(int w, int h)
{
  potrace_bitmap_t *bm;
  int dy = (w + BM_WORDBITS - 1) / BM_WORDBITS;

  bm = (potrace_bitmap_t *)MEM_mallocN(sizeof(potrace_bitmap_t), __func__);
  if (!bm) {
    return NULL;
  }
  bm->w = w;
  bm->h = h;
  bm->dy = dy;
  bm->map = (potrace_word *)calloc(h, dy * BM_WORDSIZE);
  if (!bm->map) {
    free(bm);
    return NULL;
  }

  return bm;
}

/**
 * Free a trace bitmap
 * \param bm: Trace bitmap
 */
void ED_gpencil_trace_bm_free(const potrace_bitmap_t *bm)
{
  if (bm != NULL) {
    free(bm->map);
  }
  MEM_SAFE_FREE(bm);
}

/**
 * Invert the given bitmap (Black to White)
 * \param bm: Trace bitmap
 */
void ED_gpencil_trace_bm_invert(const potrace_bitmap_t *bm)
{
  int dy = bm->dy;
  int y;
  int i;
  potrace_word *p;

  if (dy < 0) {
    dy = -dy;
  }

  for (y = 0; y < bm->h; y++) {
    p = bm_scanline(bm, y);
    for (i = 0; i < dy; i++) {
      p[i] ^= BM_ALLBITS;
    }
  }
}

/**
 * Return pixel data (rgba) at index
 * \param ibuf: ImBuf of the image
 * \param idx: Index of the pixel
 * \return: RGBA value
 */
static void pixel_at_index(const ImBuf *ibuf, const int idx, float r_col[4])
{
  BLI_assert(idx < (ibuf->x * ibuf->y));

  if (ibuf->rect_float) {
    const float *frgba = &ibuf->rect_float[idx * 4];
    copy_v4_v4(r_col, frgba);
  }
  else {
    unsigned char *cp = (unsigned char *)(ibuf->rect + idx);
    r_col[0] = (float)cp[0] / 255.0f;
    r_col[1] = (float)cp[1] / 255.0f;
    r_col[2] = (float)cp[2] / 255.0f;
    r_col[3] = (float)cp[3] / 255.0f;
  }
}

void ED_gpencil_trace_image_to_bm(ImBuf *ibuf, const potrace_bitmap_t *bm)
{
  float rgba[4];
  int pixel = 0;

  for (int y = 0; y < ibuf->y; y++) {
    for (int x = 0; x < ibuf->x; x++) {
      pixel = (ibuf->x * y) + x;
      pixel_at_index(ibuf, pixel, rgba);
      /* Get a BW color. */
      mul_v3_fl(rgba, rgba[3]);
      float color = (rgba[0] + rgba[1] + rgba[2]) / 3.0f;
      int bw = (color > 0.5f) ? 0 : 1;
      BM_PUT(bm, x, y, bw);
    }
  }
}

static void write_line(FILE *fptr, char *line)
{
  fprintf(fptr, "%s\n", line);
}

static void add_point(bGPDstroke *gps, int idx, float scale, int offset[2], float x, float y)
{
  if (gps->totpoints == 0) {
    gps->points = MEM_callocN(sizeof(bGPDspoint), "gp_stroke_points");
  }
  else {
    gps->points = MEM_recallocN(gps->points, sizeof(bGPDspoint) * (gps->totpoints + 1));
  }
  bGPDspoint *pt = &gps->points[idx];
  pt->x = (x - offset[0]) * scale;
  pt->y = 0;
  pt->z = (y - offset[1]) * scale;
  pt->pressure = 1.0f;
  pt->strength = 1.0f;

  gps->totpoints++;
}

/**
 * Convert Potrace Bitmap to Grease Pencil strokes
 * \param st: Data with traced data
 * \param ob: Target grease pencil object
 * \param gpf: Currect grease pencil frame
 * \param offset: Offset to center
 */
void ED_gpencil_trace_data_to_gp(potrace_state_t *st, Object *ob, bGPDframe *gpf, int offset[2])
{
  potrace_path_t *path = st->plist;
  int n, *tag;
  potrace_dpoint_t(*c)[3];

  FILE *fout = stderr;
  char txt[256];

  const float scale = 0.005f;
  /* Draw each curve. */
  path = st->plist;
  while (path != NULL) {
    n = path->curve.n;
    tag = path->curve.tag;
    c = path->curve.c;
    /* Create a new stroke. */
    bGPDstroke *gps = BKE_gpencil_stroke_add(gpf, 0, 0, 10, false);
    int pt_idx = 0;
    /* Move to last point that is equals to start point. */
    add_point(gps, pt_idx, scale, offset, c[n - 1][2].x, c[n - 1][2].y);
    pt_idx++;
    for (int i = 0; i < n; i++) {
      switch (tag[i]) {
        case POTRACE_CORNER:
          add_point(gps, pt_idx, scale, offset, c[i][1].x, c[i][1].y);
          pt_idx++;

          add_point(gps, pt_idx, scale, offset, c[i][2].x, c[i][2].y);
          pt_idx++;
          break;
        case POTRACE_CURVETO:
          sprintf_s(txt,
                    256,
                    "%f %f %f %f %f %f curveto",
                    c[i][0].x,
                    c[i][0].y,
                    c[i][1].x,
                    c[i][1].y,
                    c[i][2].x,
                    c[i][2].y);
          write_line(fout, txt);
          break;
      }
    }
    /* Update geometry. */
    BKE_gpencil_stroke_geometry_update(gps);

    /* At the end of a group of a positive path and its negative
     *  children, fill.
     * Sign is the char +(43) or -(45) */
#if 0
    if (path->next == NULL || path->next->sign == '+') {
      printf("-- End of group ---\n");
    }
#endif
    path = path->next;
  }
}
