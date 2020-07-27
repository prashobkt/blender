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
 */
#pragma once

/** \file
 * \ingroup bgpencil
 */

#ifdef __cplusplus
extern "C" {
#endif

struct Scene;
struct bContext;
struct ARegion;

typedef enum eGpencilExport_Modes {
  GP_EXPORT_TO_SVG = 0,
} eGpencilExport_Modes;

struct GpencilExportParams {
  bContext *C;
  ARegion *region;
  /** Grease pencil object. */
  struct Object *ob;
  /** Output filename.  */
  char *filename;
  /** Export mode.  */
  short mode;
  /** Start frame.  */
  double frame_start;
  /** End frame.  */
  double frame_end;
  /** Frame subfix. */
  char frame[5];
  /** Flags. */
  int flag;
};

typedef enum eGpencilExportParams_Flag {
  /* Export Filled strokes. */
  GP_EXPORT_FILL = (1 << 0),
  /* Export normalized thickness. */
  GP_EXPORT_NORM_THICKNESS = (1 << 1),
} eGpencilExportParams_Flag;

bool gpencil_io_export(const struct GpencilExportParams *params);

#ifdef __cplusplus
}
#endif
