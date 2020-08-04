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

/* Paper Size: A4, Letter. */
static const float gpencil_export_paper_sizes[1][2] = {3508, 2480};

struct GpencilExportParams {
  bContext *C;
  ARegion *region;
  View3D *v3d;
  /** Grease pencil object. */
  struct Object *obact;
  /** Output filename.  */
  char *filename;
  /** Export mode.  */
  short mode;
  /** Start frame.  */
  double frame_start;
  /** End frame.  */
  double frame_end;
  /** Frame subfix. */
  char file_subfix[5];
  /* Current frame. */
  int framenum;
  /** Flags. */
  int flag;
  /** Select mode */
  short select;
  /** Stroke sampling. */
  float stroke_sample;
  /** Row and cols of storyboard. */
  int page_layout[2];
  /** Page type (Landscape/Portrait). */
  short page_type;
  /** Paper size in pixels. */
  float paper_size[2];
  /** Text type for each frame. */
  short text_type;
};

typedef enum eGpencilExportParams_Flag {
  /* Use Storyboard format. */
  GP_EXPORT_STORYBOARD_MODE = (1 << 0),
  /* Export Filled strokes. */
  GP_EXPORT_FILL = (1 << 1),
  /* Export normalized thickness. */
  GP_EXPORT_NORM_THICKNESS = (1 << 2),
  /* Clip camera area. */
  GP_EXPORT_CLIP_CAMERA = (1 << 3),
  /* Gray Scale. */
  GP_EXPORT_GRAY_SCALE = (1 << 4),
} eGpencilExportParams_Flag;

typedef enum eGpencilExport_Modes {
  GP_EXPORT_TO_SVG = 0,
} eGpencilExport_Modes;

typedef enum eGpencilExportSelect {
  GP_EXPORT_ACTIVE = 0,
  GP_EXPORT_SELECTED = 1,
  GP_EXPORT_VISIBLE = 2,
} eGpencilExportSelect;

typedef enum eGpencilExportPaper {
  GP_EXPORT_PAPER_LANDSCAPE = 0,
  GP_EXPORT_PAPER_PORTRAIT = 1,
} eGpencilExportPaper;

/* GpencilExportParams->text_flag. */
typedef enum eGpencilExportText {
  GP_EXPORT_TXT_NONE = 0,
  GP_EXPORT_TXT_SHOT = 1,
  GP_EXPORT_TXT_FRAME = 2,
  GP_EXPORT_TXT_SHOT_FRAME = 3,
} eGpencilExportText;

bool gpencil_io_export(struct GpencilExportParams *iparams);

#ifdef __cplusplus
}
#endif
