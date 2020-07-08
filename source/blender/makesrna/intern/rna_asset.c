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
}

void RNA_def_asset(BlenderRNA *brna)
{
  rna_def_asset(brna);
}

#endif
