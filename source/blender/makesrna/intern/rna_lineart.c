/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
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
 * Contributor(s): Blender Foundation (2008).
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/makesrna/intern/rna_lineart.c
 *  \ingroup RNA
 */

#include <stdio.h>
#include <stdlib.h>

#include "BLI_string_utils.h"
#include "BLI_utildefines.h"

#include "RNA_define.h"
#include "RNA_enum_types.h"

#include "rna_internal.h"

#include "DNA_lineart_types.h"
#include "DNA_material_types.h"
#include "DNA_texture_types.h"

#include "WM_api.h"
#include "WM_types.h"

#ifdef RNA_RUNTIME

#else

static void rna_def_lineart_line_layer(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  static const EnumPropertyItem rna_enum_lineart_normal_mode[] = {
      {LRT_NORMAL_DIRECTIONAL,
       "DIRECTIONAL",
       0,
       "Directional",
       "Use directional vector to control line width"},
      /* Seems working... */
      {LRT_NORMAL_POINT, "POINT", 0, "Point", "Use Point Light Style"},
      {0, NULL, 0, NULL, NULL}};

  srna = RNA_def_struct(brna, "LineartLineLayer", NULL);
  RNA_def_struct_sdna(srna, "LineartLineLayer");
  RNA_def_struct_ui_text(srna, "Line Layer", "LineartLineLayer");

  prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
  RNA_def_property_ui_text(prop, "Name", "Name of this layer");

  prop = RNA_def_property(srna, "normal_enabled", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flags", LRT_LINE_LAYER_NORMAL_ENABLED);
  RNA_def_property_ui_text(prop, "Enabled", "Enable normal controlled line weight");

  prop = RNA_def_property(srna, "normal_mode", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_items(prop, rna_enum_lineart_normal_mode);
  RNA_def_property_ui_text(prop, "Normal", "Normal controlled line weight");

  prop = RNA_def_property(srna, "normal_effect_inverse", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flags", LRT_LINE_LAYER_NORMAL_INVERSE);
  RNA_def_property_ui_text(prop, "Inverse", "Inverse normal effect");

  prop = RNA_def_property(
      srna, "normal_ramp_begin", PROP_FLOAT, PROP_FACTOR); /* begin is least strength */
  RNA_def_property_float_default(prop, 0.0f);
  RNA_def_property_ui_text(prop, "Ramp Begin", "Normal ramp begin value");
  RNA_def_property_ui_range(prop, 0.0f, 1.0f, 0.05, 2);

  prop = RNA_def_property(srna, "normal_ramp_end", PROP_FLOAT, PROP_FACTOR);
  RNA_def_property_float_default(prop, 1.0f);
  RNA_def_property_ui_text(prop, "Ramp End", "Normal ramp end value");
  RNA_def_property_ui_range(prop, 0.0f, 1.0f, 0.05, 2);

  prop = RNA_def_property(
      srna, "normal_thickness_start", PROP_FLOAT, PROP_NONE); /* begin is least strength */
  RNA_def_property_float_default(prop, 0.2f);
  RNA_def_property_ui_text(prop, "Thickness Begin", "Normal thickness begin value");
  RNA_def_property_ui_range(prop, 0.0f, 5.0f, 0.05, 2);

  prop = RNA_def_property(srna, "normal_thickness_end", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_default(prop, 1.5f);
  RNA_def_property_ui_text(prop, "Thickness End", "Normal thickness end value");
  RNA_def_property_ui_range(prop, 0.0f, 5.0f, 0.05, 2);

  prop = RNA_def_property(srna, "normal_control_object", PROP_POINTER, PROP_NONE);
  RNA_def_property_struct_type(prop, "Object");
  RNA_def_property_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Object", "Normal style control object");

  prop = RNA_def_property(srna, "use_same_style", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flags", LRT_LINE_LAYER_USE_SAME_STYLE);
  RNA_def_property_boolean_default(prop, true);
  RNA_def_property_ui_text(prop, "Same Style", "Use same styles for multiple line types");

  prop = RNA_def_property(srna, "use_multiple_levels", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flags", LRT_LINE_LAYER_USE_MULTIPLE_LEVELS);
  RNA_def_property_ui_text(
      prop, "Use Multiple Levels", "Select lines from multiple occlusion levels");

  /* types */
  prop = RNA_def_property(srna, "contour", PROP_POINTER, PROP_NONE);
  RNA_def_property_struct_type(prop, "LineartLineType");
  RNA_def_property_ui_text(prop, "Contour", "Contour line type");

  prop = RNA_def_property(srna, "crease", PROP_POINTER, PROP_NONE);
  RNA_def_property_struct_type(prop, "LineartLineType");
  RNA_def_property_ui_text(prop, "Crease", "Creaseline type");

  prop = RNA_def_property(srna, "edge_mark", PROP_POINTER, PROP_NONE);
  RNA_def_property_struct_type(prop, "LineartLineType");
  RNA_def_property_ui_text(prop, "Edge Mark", "Edge mark line type");

  prop = RNA_def_property(srna, "material_separate", PROP_POINTER, PROP_NONE);
  RNA_def_property_struct_type(prop, "LineartLineType");
  RNA_def_property_ui_text(prop, "Material Separate", "Material separate line type");

  prop = RNA_def_property(srna, "intersection", PROP_POINTER, PROP_NONE);
  RNA_def_property_struct_type(prop, "LineartLineType");
  RNA_def_property_ui_text(prop, "Intersection", "Intersection line type");

  prop = RNA_def_property(srna, "level_start", PROP_INT, PROP_NONE);
  RNA_def_property_int_default(prop, 0);
  RNA_def_property_ui_text(prop, "Level Start", "Occlusion level start");
  RNA_def_property_range(prop, 0, 128);

  prop = RNA_def_property(srna, "level_end", PROP_INT, PROP_NONE);
  RNA_def_property_int_default(prop, 0);
  RNA_def_property_ui_text(prop, "Level End", "Occlusion level end");
  RNA_def_property_range(prop, 0, 128);

  prop = RNA_def_property(srna, "thickness", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_default(prop, 1.0f);
  RNA_def_property_ui_text(prop, "Thickness", "Master Thickness");
  RNA_def_property_ui_range(prop, 0.0f, 100.0f, 0.1, 2);

  prop = RNA_def_property(srna, "color", PROP_FLOAT, PROP_COLOR);
  RNA_def_property_float_default(prop, 1.0f);
  RNA_def_property_array(prop, 3);
  RNA_def_property_ui_text(prop, "Color", "Master Color");
  RNA_def_property_ui_range(prop, 0.0f, 1.0f, 0.1, 2);
}

static void rna_def_lineart_line_type(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "LineartLineType", NULL);
  RNA_def_struct_sdna(srna, "LineartLineType");
  RNA_def_struct_ui_text(srna, "Line Type", "LineartLineType");

  prop = RNA_def_property(srna, "use", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_ui_text(prop, "Use", "This line type is enabled");

  prop = RNA_def_property(srna, "thickness", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_default(prop, 1.0f);
  RNA_def_property_ui_text(prop, "Thickness", "Relative thickness to master");
  RNA_def_property_ui_range(prop, 0.0f, 2.0f, 0.01, 2);

  prop = RNA_def_property(srna, "color", PROP_FLOAT, PROP_COLOR);
  RNA_def_property_float_default(prop, 1.0f);
  RNA_def_property_array(prop, 3);
  RNA_def_property_ui_text(prop, "Color", "Color of this line type");
}

void RNA_def_lineart(BlenderRNA *brna)
{
  rna_def_lineart_line_type(brna);
  rna_def_lineart_line_layer(brna);
}

#endif
