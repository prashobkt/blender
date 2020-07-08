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

/** \file
 * \ingroup RNA
 */

#include <stdlib.h>

#include "RNA_define.h"
#include "RNA_enum_types.h"

#include "DNA_asset_types.h"

#include "rna_internal.h"

#ifdef RNA_RUNTIME

#  include "BKE_asset.h"

#  include "RNA_access.h"

static CustomTag *rna_Asset_tag_new(Asset *asset, ReportList *reports, const char *name)
{
  struct CustomTagEnsureResult result = BKE_asset_tag_ensure(asset, name);

  if (!result.is_new) {
    BKE_reportf(reports,
                RPT_WARNING,
                "Tag '%s' already present in asset '%s'",
                result.tag->name,
                asset->id.name + 2);
    /* Report, but still return valid item. */
  }

  return result.tag;
}

static void rna_Asset_tag_remove(Asset *asset, ReportList *reports, PointerRNA *tag_ptr)
{
  CustomTag *tag = tag_ptr->data;
  if (BLI_findindex(&asset->tags, tag) == -1) {
    BKE_reportf(
        reports, RPT_ERROR, "Tag '%s' not found in asset '%s'", tag->name, asset->id.name + 2);
    return;
  }

  BKE_asset_tag_remove(asset, tag);
  RNA_POINTER_INVALIDATE(tag_ptr);
}

static void rna_Asset_description_get(PointerRNA *ptr, char *value)
{
  Asset *asset = ptr->data;

  if (asset->description) {
    strcpy(value, asset->description);
  }
  else {
    value[0] = '\0';
  }
}

static int rna_Asset_description_length(PointerRNA *ptr)
{
  Asset *asset = ptr->data;
  return asset->description ? strlen(asset->description) : 0;
}

static void rna_Asset_description_set(PointerRNA *ptr, const char *value)
{
  Asset *asset = ptr->data;

  if (asset->description) {
    MEM_freeN(asset->description);
  }

  if (value[0]) {
    asset->description = BLI_strdup(value);
  }
  else {
    asset->description = NULL;
  }
}

#else

static void rna_def_custom_tag(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "CustomTag", NULL);
  RNA_def_struct_ui_text(srna, "Custom Tag", "User defined tag (name token)");

  prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
  RNA_def_property_string_maxlength(prop, MAX_NAME);
  RNA_def_property_ui_text(prop, "Name", "The identifier that makes up this tag");
  RNA_def_struct_name_property(srna, prop);
}

static void rna_def_asset_custom_tags_api(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;

  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "CustomTags");
  srna = RNA_def_struct(brna, "CustomTags", NULL);
  RNA_def_struct_sdna(srna, "Asset");
  RNA_def_struct_ui_text(srna, "Asset Tags", "Collection of custom asset tags");

  /* Tag collection */
  func = RNA_def_function(srna, "new", "rna_Asset_tag_new");
  RNA_def_function_ui_description(func, "Add a new tag to this asset");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  parm = RNA_def_string(func, "name", NULL, MAX_NAME, "Name", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  /* return type */
  parm = RNA_def_pointer(func, "tag", "CustomTag", "", "New tag");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_Asset_tag_remove");
  RNA_def_function_ui_description(func, "Remove an existing tag from this asset");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  /* tag to remove */
  parm = RNA_def_pointer(func, "tag", "CustomTag", "", "Removed tag");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
}

static void rna_def_asset(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "Asset", "ID");
  RNA_def_struct_ui_text(srna, "Asset", "Asset data-block");
  //  RNA_def_struct_ui_icon(srna, ICON_ASSET); /* TODO: Icon doesn't exist!. */

  prop = RNA_def_property(srna, "description", PROP_STRING, PROP_NONE);
  RNA_def_property_string_funcs(prop,
                                "rna_Asset_description_get",
                                "rna_Asset_description_length",
                                "rna_Asset_description_set");
  RNA_def_property_ui_text(
      prop, "Description", "A description of the asset to be displayed for the user");

  prop = RNA_def_property(srna, "tags", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_struct_type(prop, "CustomTag");
  RNA_def_property_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop,
                           "Tags",
                           "Custom tags (name tokens) for the asset, used for filtering and "
                           "general asset management");
  rna_def_asset_custom_tags_api(brna, prop);
}

void RNA_def_asset(BlenderRNA *brna)
{
  RNA_define_animate_sdna(false);

  rna_def_custom_tag(brna);
  rna_def_asset(brna);

  RNA_define_animate_sdna(true);
}

#endif
