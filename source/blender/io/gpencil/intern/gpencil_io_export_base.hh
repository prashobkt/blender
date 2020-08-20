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
 * The Original Code is Copyright (C) 2020 Blender Foundation
 * All rights reserved.
 */
#pragma once

/** \file
 * \ingroup bgpencil
 */
#include <list>
#include <string>

#include "BLI_path_util.h"

#include "DNA_defs.h"

#include "gpencil_io_exporter.h"

struct ARegion;
struct Depsgraph;
struct Main;
struct Object;
struct RegionView3D;
struct Scene;

struct bGPdata;
struct bGPDlayer;
struct bGPDframe;
struct bGPDstroke;
struct MaterialGPencilStyle;

namespace blender::io::gpencil {

class GpencilExporter {

 public:
  GpencilExporter(const struct GpencilExportParams *iparams);
  virtual bool add_newpage(void) = 0;
  virtual bool add_body(void) = 0;
  virtual bool write(std::string subfix) = 0;

  void set_frame_number(int value);
  void set_frame_offset(float value[2]);
  void set_frame_ratio(float value[2]);
  void set_frame_box(float value[2]);
  void set_shot(int value);

 protected:
  GpencilExportParams params_;

  bool invert_axis_[2];
  float diff_mat_[4][4];
  char out_filename_[FILE_MAX];

  /* Used for sorting objects. */
  struct ObjectZ {
    float zdepth;
    struct Object *ob;
  };

  /** List of included objects. */
  std::list<ObjectZ> ob_list_;

  /* Data for easy access. */
  struct Depsgraph *depsgraph;
  struct bGPdata *gpd;
  struct Main *bmain;
  struct Scene *scene;
  struct RegionView3D *rv3d;

  uint16_t winx_, winy_;
  uint16_t render_x_, render_y_;
  float camera_ratio_;
  rctf camera_rect_;

  float offset_[2];

  float frame_box_[2];
  float frame_offset_[2];
  float frame_ratio_[2];

  int cfra_;
  int shot_;

  float stroke_color_[4], fill_color_[4];

  static std::string rgb_to_hexstr(float color[3]);
  static void rgb_to_grayscale(float color[3]);
  static std::string to_lower_string(char *input_text);
  static float stroke_average_pressure_get(struct bGPDstroke *gps);
  static bool is_stroke_thickness_constant(struct bGPDstroke *gps);

  /* Geometry functions. */
  bool gpencil_3d_point_to_screen_space(const float co[3], float r_co[2]);

  float stroke_point_radius_get(struct bGPDstroke *gps);
  void create_object_list(void);

  struct MaterialGPencilStyle *gp_style_current_get(void);
  bool material_is_stroke(void);
  bool material_is_fill(void);

  bool is_camera_mode(void);

  float stroke_average_opacity_get(void);

  struct bGPDlayer *gpl_current_get(void);
  struct bGPDframe *gpf_current_get(void);
  struct bGPDstroke *gps_current_get(void);
  void gpl_current_set(struct bGPDlayer *gpl);
  void gpf_current_set(struct bGPDframe *gpf);
  void gps_current_set(struct Object *ob, struct bGPDstroke *gps, const bool set_colors);

  void selected_objects_boundbox_set(void);
  void selected_objects_boundbox_get(rctf *boundbox);
  void set_out_filename(const char *filename);

 private:
  struct bGPDlayer *gpl_cur;
  struct bGPDframe *gpf_cur;
  struct bGPDstroke *gps_cur;
  struct MaterialGPencilStyle *gp_style;
  bool is_stroke;
  bool is_fill;
  float avg_opacity;
  bool is_camera;
  rctf select_boundbox;
};

}  // namespace blender::io::gpencil
