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

struct bGPDlayer;
struct bGPDframe;
struct bGPDstroke;
struct MaterialGPencilStyle;

namespace blender::io::gpencil {

class GpencilExporter {

 public:
  GpencilExporter(const struct GpencilExportParams *iparams);
  virtual bool write(std::string actual_frame, const bool newpage, const bool savepage) = 0;

  void set_frame_number(int value);
  void set_frame_offset(float value[2]);
  void set_frame_ratio(float value[2]);
  void set_frame_box(float value[2]);

 protected:
  bool invert_axis_[2];
  float diff_mat_[4][4];
  GpencilExportParams params_;
  char out_filename_[FILE_MAX];

  /* Used for sorting objects. */
  struct ObjectZ {
    float zdepth;
    struct Object *ob;
  };

  std::list<ObjectZ> ob_list_;

  /* Data for easy access. */
  struct Depsgraph *depsgraph;
  struct bGPdata *gpd;
  struct Main *bmain;
  struct RegionView3D *rv3d;

  int winx_, winy_;
  int render_x_, render_y_;
  float camera_ratio_;
  float offset_[2];
  rctf camera_rect_;
  float frame_box_[2];
  float frame_offset_[2];
  float frame_ratio_[2];
  int cfra_;

  float stroke_color_[4], fill_color_[4];

  /* Geometry functions. */
  bool gpencil_3d_point_to_screen_space(const float co[3], float r_co[2]);

  bool is_stroke_thickness_constant(struct bGPDstroke *gps);
  float stroke_average_pressure_get(struct bGPDstroke *gps);
  float stroke_point_radius_get(struct bGPDstroke *gps);
  void selected_objects_boundbox(void);

  std::string rgb_to_hex(float color[3]);
  void rgb_to_grayscale(float color[3]);
  std::string to_lower_string(char *input_text);

  struct bGPDlayer *gpl_current_get(void);
  struct bGPDframe *gpf_current_get(void);
  struct bGPDstroke *gps_current_get(void);
  struct MaterialGPencilStyle *gp_style_current_get(void);
  bool gp_style_is_stroke(void);
  bool gp_style_is_fill(void);
  float stroke_average_opacity(void);
  bool is_camera_mode(void);
  bool is_bound_mode(void);

  void gpl_current_set(struct bGPDlayer *gpl);
  void gpf_current_set(struct bGPDframe *gpf);
  void gps_current_set(struct Object *ob, struct bGPDstroke *gps, const bool set_colors);

  void get_select_boundbox(rctf *boundbox);

 private:
  struct bGPDlayer *gpl_cur;
  struct bGPDframe *gpf_cur;
  struct bGPDstroke *gps_cur;
  struct MaterialGPencilStyle *gp_style;
  bool is_stroke;
  bool is_fill;
  float avg_opacity;
  bool is_camera;
  rcti select_box;

  void set_out_filename(char *filename);
  void create_object_list(void);
};

}  // namespace blender::io::gpencil
