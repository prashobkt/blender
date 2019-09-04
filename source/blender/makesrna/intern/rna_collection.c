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

#include "DNA_collection_types.h"

#include "BLI_utildefines.h"

#include "RNA_define.h"

#include "rna_internal.h"

#include "WM_types.h"

#ifdef RNA_RUNTIME

#  include "DNA_scene_types.h"
#  include "DNA_object_types.h"

#  include "DEG_depsgraph.h"
#  include "DEG_depsgraph_build.h"

#  include "BKE_collection.h"
#  include "BKE_global.h"
#  include "BKE_layer.h"

#  include "WM_api.h"

#  include "RNA_access.h"

static void rna_Collection_all_objects_begin(CollectionPropertyIterator *iter, PointerRNA *ptr)
{
  Collection *collection = (Collection *)ptr->data;
  ListBase collection_objects = BKE_collection_object_cache_get(collection);
  rna_iterator_listbase_begin(iter, &collection_objects, NULL);
}

static PointerRNA rna_Collection_all_objects_get(CollectionPropertyIterator *iter)
{
  ListBaseIterator *internal = &iter->internal.listbase;

  /* we are actually iterating a ObjectBase list, so override get */
  Base *base = (Base *)internal->link;
  return rna_pointer_inherit_refine(&iter->parent, &RNA_Object, base->object);
}

static void rna_Collection_objects_begin(CollectionPropertyIterator *iter, PointerRNA *ptr)
{
  Collection *collection = (Collection *)ptr->data;
  rna_iterator_listbase_begin(iter, &collection->gobject, NULL);
}

static PointerRNA rna_Collection_objects_get(CollectionPropertyIterator *iter)
{
  ListBaseIterator *internal = &iter->internal.listbase;

  /* we are actually iterating a ObjectBase list, so override get */
  CollectionObject *cob = (CollectionObject *)internal->link;
  return rna_pointer_inherit_refine(&iter->parent, &RNA_Object, cob->ob);
}

static void rna_Collection_objects_link(Collection *collection,
                                        Main *bmain,
                                        ReportList *reports,
                                        Object *object)
{
  if (!BKE_collection_object_add(bmain, collection, object)) {
    BKE_reportf(reports,
                RPT_ERROR,
                "Object '%s' already in collection '%s'",
                object->id.name + 2,
                collection->id.name + 2);
    return;
  }

  DEG_id_tag_update(&collection->id, ID_RECALC_COPY_ON_WRITE);
  DEG_relations_tag_update(bmain);
  WM_main_add_notifier(NC_OBJECT | ND_DRAW, &object->id);
}

static void rna_Collection_objects_unlink(Collection *collection,
                                          Main *bmain,
                                          ReportList *reports,
                                          Object *object)
{
  if (!BKE_collection_object_remove(bmain, collection, object, false)) {
    BKE_reportf(reports,
                RPT_ERROR,
                "Object '%s' not in collection '%s'",
                object->id.name + 2,
                collection->id.name + 2);
    return;
  }

  DEG_id_tag_update(&collection->id, ID_RECALC_COPY_ON_WRITE);
  DEG_relations_tag_update(bmain);
  WM_main_add_notifier(NC_OBJECT | ND_DRAW, &object->id);
}

static bool rna_Collection_objects_override_apply(Main *bmain,
                                                  PointerRNA *ptr_dst,
                                                  PointerRNA *UNUSED(ptr_src),
                                                  PointerRNA *UNUSED(ptr_storage),
                                                  PropertyRNA *UNUSED(prop_dst),
                                                  PropertyRNA *UNUSED(prop_src),
                                                  PropertyRNA *UNUSED(prop_storage),
                                                  const int UNUSED(len_dst),
                                                  const int UNUSED(len_src),
                                                  const int UNUSED(len_storage),
                                                  PointerRNA *ptr_item_dst,
                                                  PointerRNA *ptr_item_src,
                                                  PointerRNA *UNUSED(ptr_item_storage),
                                                  IDOverrideLibraryPropertyOperation *opop)
{
  BLI_assert(opop->operation == IDOVERRIDE_LIBRARY_OP_REPLACE &&
             "Unsupported RNA override operation on collections' objects");
  UNUSED_VARS_NDEBUG(opop);

  Collection *coll_dst = (Collection *)ptr_dst->owner_id;

  if (ptr_item_dst->type == NULL || ptr_item_src->type == NULL) {
    BLI_assert(0 && "invalid source or destination object.");
    return false;
  }

  Object *ob_dst = ptr_item_dst->data;
  Object *ob_src = ptr_item_src->data;

  CollectionObject *cob_dst = BLI_findptr(
      &coll_dst->gobject, ob_dst, offsetof(CollectionObject, ob));

  if (cob_dst == NULL) {
    BLI_assert(0 && "Could not find destination object in destination collection!");
    return false;
  }

  /* XXX TODO We most certainly rather want to have a 'swap object pointer in collection'
   * util in BKE_collection. This is only temp quick dirty test! */
  id_us_min(&cob_dst->ob->id);
  cob_dst->ob = ob_src;
  id_us_plus(&cob_dst->ob->id);

  if (BKE_collection_is_in_scene(coll_dst)) {
    BKE_main_collection_sync(bmain);
  }

  return true;
}

static void rna_Collection_children_begin(CollectionPropertyIterator *iter, PointerRNA *ptr)
{
  Collection *collection = (Collection *)ptr->data;
  rna_iterator_listbase_begin(iter, &collection->children, NULL);
}

static PointerRNA rna_Collection_children_get(CollectionPropertyIterator *iter)
{
  ListBaseIterator *internal = &iter->internal.listbase;

  /* we are actually iterating a CollectionChild list, so override get */
  CollectionChild *child = (CollectionChild *)internal->link;
  return rna_pointer_inherit_refine(&iter->parent, &RNA_Collection, child->collection);
}

static void rna_Collection_children_link(Collection *collection,
                                         Main *bmain,
                                         ReportList *reports,
                                         Collection *child)
{
  if (!BKE_collection_child_add(bmain, collection, child)) {
    BKE_reportf(reports,
                RPT_ERROR,
                "Collection '%s' already in collection '%s'",
                child->id.name + 2,
                collection->id.name + 2);
    return;
  }

  DEG_id_tag_update(&collection->id, ID_RECALC_COPY_ON_WRITE);
  DEG_relations_tag_update(bmain);
  WM_main_add_notifier(NC_OBJECT | ND_DRAW, &child->id);
}

static void rna_Collection_children_unlink(Collection *collection,
                                           Main *bmain,
                                           ReportList *reports,
                                           Collection *child)
{
  if (!BKE_collection_child_remove(bmain, collection, child)) {
    BKE_reportf(reports,
                RPT_ERROR,
                "Collection '%s' not in collection '%s'",
                child->id.name + 2,
                collection->id.name + 2);
    return;
  }

  DEG_id_tag_update(&collection->id, ID_RECALC_COPY_ON_WRITE);
  DEG_relations_tag_update(bmain);
  WM_main_add_notifier(NC_OBJECT | ND_DRAW, &child->id);
}

static bool rna_Collection_children_override_apply(Main *bmain,
                                                   PointerRNA *ptr_dst,
                                                   PointerRNA *UNUSED(ptr_src),
                                                   PointerRNA *UNUSED(ptr_storage),
                                                   PropertyRNA *UNUSED(prop_dst),
                                                   PropertyRNA *UNUSED(prop_src),
                                                   PropertyRNA *UNUSED(prop_storage),
                                                   const int UNUSED(len_dst),
                                                   const int UNUSED(len_src),
                                                   const int UNUSED(len_storage),
                                                   PointerRNA *ptr_item_dst,
                                                   PointerRNA *ptr_item_src,
                                                   PointerRNA *UNUSED(ptr_item_storage),
                                                   IDOverrideLibraryPropertyOperation *opop)
{
  BLI_assert(opop->operation == IDOVERRIDE_LIBRARY_OP_REPLACE &&
             "Unsupported RNA override operation on collections' children");
  UNUSED_VARS_NDEBUG(opop);

  Collection *coll_dst = (Collection *)ptr_dst->owner_id;

  if (ptr_item_dst->type == NULL || ptr_item_src->type == NULL) {
    BLI_assert(0 && "invalid source or destination sub-collection.");
    return false;
  }

  Collection *subcoll_dst = ptr_item_dst->data;
  Collection *subcoll_src = ptr_item_src->data;

  CollectionChild *collchild_dst = BLI_findptr(
      &coll_dst->children, subcoll_dst, offsetof(CollectionChild, collection));

  if (collchild_dst == NULL) {
    BLI_assert(0 && "Could not find destination sub-collection in destination collection!");
    return false;
  }

  /* XXX TODO We most certainly rather want to have a 'swap object pointer in collection'
   * util in BKE_collection. This is only temp quick dirty test! */
  id_us_min(&collchild_dst->collection->id);
  collchild_dst->collection = subcoll_src;
  id_us_plus(&collchild_dst->collection->id);

  BKE_collection_object_cache_free(coll_dst);
  BKE_main_collection_sync(bmain);

  return true;
}

static void rna_Collection_flag_set(PointerRNA *ptr, const bool value, const int flag)
{
  Collection *collection = (Collection *)ptr->data;

  if (collection->flag & COLLECTION_IS_MASTER) {
    return;
  }

  if (value) {
    collection->flag |= flag;
  }
  else {
    collection->flag &= ~flag;
  }
}

static void rna_Collection_hide_select_set(PointerRNA *ptr, bool value)
{
  rna_Collection_flag_set(ptr, value, COLLECTION_RESTRICT_SELECT);
}

static void rna_Collection_hide_viewport_set(PointerRNA *ptr, bool value)
{
  rna_Collection_flag_set(ptr, value, COLLECTION_RESTRICT_VIEWPORT);
}

static void rna_Collection_hide_render_set(PointerRNA *ptr, bool value)
{
  rna_Collection_flag_set(ptr, value, COLLECTION_RESTRICT_RENDER);
}

static void rna_Collection_flag_update(Main *bmain, Scene *scene, PointerRNA *ptr)
{
  Collection *collection = (Collection *)ptr->data;
  BKE_collection_object_cache_free(collection);
  BKE_main_collection_sync(bmain);

  DEG_id_tag_update(&collection->id, ID_RECALC_COPY_ON_WRITE);
  DEG_relations_tag_update(bmain);
  WM_main_add_notifier(NC_SCENE | ND_OB_SELECT, scene);
}

#else

/* collection.objects */
static void rna_def_collection_objects(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "CollectionObjects");
  srna = RNA_def_struct(brna, "CollectionObjects", NULL);
  RNA_def_struct_sdna(srna, "Collection");
  RNA_def_struct_ui_text(srna, "Collection Objects", "Collection of collection objects");

  /* add object */
  func = RNA_def_function(srna, "link", "rna_Collection_objects_link");
  RNA_def_function_flag(func, FUNC_USE_REPORTS | FUNC_USE_MAIN);
  RNA_def_function_ui_description(func, "Add this object to a collection");
  parm = RNA_def_pointer(func, "object", "Object", "", "Object to add");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);

  /* remove object */
  func = RNA_def_function(srna, "unlink", "rna_Collection_objects_unlink");
  RNA_def_function_ui_description(func, "Remove this object from a collection");
  RNA_def_function_flag(func, FUNC_USE_REPORTS | FUNC_USE_MAIN);
  parm = RNA_def_pointer(func, "object", "Object", "", "Object to remove");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
}

/* collection.children */
static void rna_def_collection_children(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "CollectionChildren");
  srna = RNA_def_struct(brna, "CollectionChildren", NULL);
  RNA_def_struct_sdna(srna, "Collection");
  RNA_def_struct_ui_text(srna, "Collection Children", "Collection of child collections");

  /* add child */
  func = RNA_def_function(srna, "link", "rna_Collection_children_link");
  RNA_def_function_flag(func, FUNC_USE_REPORTS | FUNC_USE_MAIN);
  RNA_def_function_ui_description(func, "Add this collection as child of this collection");
  parm = RNA_def_pointer(func, "child", "Collection", "", "Collection to add");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);

  /* remove child */
  func = RNA_def_function(srna, "unlink", "rna_Collection_children_unlink");
  RNA_def_function_ui_description(func, "Remove this child collection from a collection");
  RNA_def_function_flag(func, FUNC_USE_REPORTS | FUNC_USE_MAIN);
  parm = RNA_def_pointer(func, "child", "Collection", "", "Collection to remove");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
}

static void rna_def_collection_lanpr(BlenderRNA *brna, StructRNA *srna)
{
  PropertyRNA *prop;

  static const EnumPropertyItem rna_collection_lanpr_usage[] = {
      {COLLECTION_LANPR_INCLUDE, "INCLUDE", 0, "Include", "Collection will produce feature lines"},
      {COLLECTION_LANPR_OCCLUSION_ONLY, "OCCLUSION_ONLY", 0, "Occlusion Only", "Only use the collection to produce occlusion"},
      {COLLECTION_LANPR_EXCLUDE, "EXCLUDE", 0, "Exclude", "Don't use this collection in LANPR"},
      {0, NULL, 0, NULL, NULL}};

  srna = RNA_def_struct(brna, "CollectionLANPR", NULL);
  RNA_def_struct_sdna(srna, "CollectionLANPR");
  RNA_def_struct_ui_text(srna, "Collection LANPR Usage", "LANPR usage for this collection");

  prop = RNA_def_property(srna, "usage", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_items(prop, rna_collection_lanpr_usage);
  RNA_def_property_enum_default(prop, 0);
  RNA_def_property_ui_text(prop, "Usage", "How to use this collection in LANPR");
  RNA_def_property_update(prop, NC_SCENE, NULL);

  prop = RNA_def_property(srna, "enable_contour", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "types", COLLECTION_FEATURE_LINE_CONTOUR);
  RNA_def_property_ui_text(prop, "Contour", "Contour lines");

  prop = RNA_def_property(srna, "enable_crease", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "types", COLLECTION_FEATURE_LINE_CREASE);
  RNA_def_property_ui_text(prop, "Crease", "Crease lines");

  prop = RNA_def_property(srna, "enable_mark", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "types", COLLECTION_FEATURE_LINE_MARK);
  RNA_def_property_ui_text(prop, "Mark", "Freestyle marked edges");

  prop = RNA_def_property(srna, "enable_material", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "types", COLLECTION_FEATURE_LINE_MATERIAL);
  RNA_def_property_ui_text(prop, "Material", "Material lines");

  prop = RNA_def_property(srna, "enable_intersection", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "types", COLLECTION_FEATURE_LINE_INTERSECTION);
  RNA_def_property_ui_text(prop, "Intersection", "Intersection lines");

  prop = RNA_def_property(srna, "force", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_default(prop, 0);
  RNA_def_property_ui_text(
      prop, "Force", "Force object that has LANPR modifiers to follow collection usage flag");

  prop = RNA_def_property(srna, "target", PROP_POINTER, PROP_NONE);
  RNA_def_property_pointer_sdna(prop, NULL, "target");
  RNA_def_property_ui_text(prop, "Target", "Grease Pencil object to put the stroke result");
  RNA_def_property_pointer_funcs(prop, NULL, NULL, NULL, "rna_GPencil_object_poll");
  RNA_def_property_flag(prop, PROP_EDITABLE | PROP_ID_SELF_CHECK);

  prop = RNA_def_property(srna, "replace", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_ui_text(prop, "Replace", "Replace existing GP frames");

  prop = RNA_def_property(srna, "layer", PROP_INT, PROP_NONE);
  RNA_def_property_range(prop, 0, 100);
  RNA_def_property_ui_range(prop, 0, 100, 1, -1);
  RNA_def_property_ui_text(prop, "Layer", "Grease Pencil layer to put the results into");

  prop = RNA_def_property(srna, "material", PROP_INT, PROP_NONE);
  RNA_def_property_range(prop, 0, 100);
  RNA_def_property_ui_range(prop, 0, 100, 1, -1);
  RNA_def_property_ui_text(
      prop, "Material", "Grease Pencil material to use to generate the results");

  prop = RNA_def_property(srna, "use_multiple_levels", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "use_multiple_levels", 0);
  RNA_def_property_ui_text(prop, "Multiple", "Use multiple occlusion levels");

  prop = RNA_def_property(srna, "level_start", PROP_INT, PROP_NONE);
  RNA_def_property_range(prop, 0, 255);
  RNA_def_property_ui_range(prop, 0, 255, 1, -1);
  RNA_def_property_ui_text(prop, "Level", "Occlusion level");

  prop = RNA_def_property(srna, "level_end", PROP_INT, PROP_NONE);
  RNA_def_property_range(prop, 0, 255);
  RNA_def_property_ui_range(prop, 0, 255, 1, -1);
  RNA_def_property_ui_text(prop, "To", "Occlusion level");
}

void RNA_def_collections(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "Collection", "ID");
  RNA_def_struct_ui_text(srna, "Collection", "Collection of Object data-blocks");
  RNA_def_struct_ui_icon(srna, ICON_GROUP);
  /* This is done on save/load in readfile.c,
   * removed if no objects are in the collection and not in a scene. */
  RNA_def_struct_clear_flag(srna, STRUCT_ID_REFCOUNT);

  prop = RNA_def_property(srna, "instance_offset", PROP_FLOAT, PROP_TRANSLATION);
  RNA_def_property_ui_text(
      prop, "Instance Offset", "Offset from the origin to use when instancing");
  RNA_def_property_ui_range(prop, -10000.0, 10000.0, 10, RNA_TRANSLATION_PREC_DEFAULT);
  RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);

  prop = RNA_def_property(srna, "objects", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_struct_type(prop, "Object");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_override_funcs(prop, NULL, NULL, "rna_Collection_objects_override_apply");
  RNA_def_property_ui_text(prop, "Objects", "Objects that are directly in this collection");
  RNA_def_property_collection_funcs(prop,
                                    "rna_Collection_objects_begin",
                                    "rna_iterator_listbase_next",
                                    "rna_iterator_listbase_end",
                                    "rna_Collection_objects_get",
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL);
  rna_def_collection_objects(brna, prop);

  prop = RNA_def_property(srna, "all_objects", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_struct_type(prop, "Object");
  RNA_def_property_ui_text(
      prop, "All Objects", "Objects that are in this collection and its child collections");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_NO_COMPARISON);
  RNA_def_property_collection_funcs(prop,
                                    "rna_Collection_all_objects_begin",
                                    "rna_iterator_listbase_next",
                                    "rna_iterator_listbase_end",
                                    "rna_Collection_all_objects_get",
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL);

  prop = RNA_def_property(srna, "children", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_struct_type(prop, "Collection");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_override_funcs(prop, NULL, NULL, "rna_Collection_children_override_apply");
  RNA_def_property_ui_text(
      prop, "Children", "Collections that are immediate children of this collection");
  RNA_def_property_collection_funcs(prop,
                                    "rna_Collection_children_begin",
                                    "rna_iterator_listbase_next",
                                    "rna_iterator_listbase_end",
                                    "rna_Collection_children_get",
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL);
  rna_def_collection_children(brna, prop);

  /* Flags */
  prop = RNA_def_property(srna, "hide_select", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", COLLECTION_RESTRICT_SELECT);
  RNA_def_property_boolean_funcs(prop, NULL, "rna_Collection_hide_select_set");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
  RNA_def_property_ui_icon(prop, ICON_RESTRICT_SELECT_OFF, -1);
  RNA_def_property_ui_text(prop, "Disable Selection", "Disable selection in viewport");
  RNA_def_property_update(prop, NC_SCENE | ND_LAYER_CONTENT, "rna_Collection_flag_update");

  prop = RNA_def_property(srna, "hide_viewport", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", COLLECTION_RESTRICT_VIEWPORT);
  RNA_def_property_boolean_funcs(prop, NULL, "rna_Collection_hide_viewport_set");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
  RNA_def_property_ui_icon(prop, ICON_RESTRICT_VIEW_OFF, -1);
  RNA_def_property_ui_text(prop, "Disable in Viewports", "Globally disable in viewports");
  RNA_def_property_update(prop, NC_SCENE | ND_LAYER_CONTENT, "rna_Collection_flag_update");

  prop = RNA_def_property(srna, "hide_render", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", COLLECTION_RESTRICT_RENDER);
  RNA_def_property_boolean_funcs(prop, NULL, "rna_Collection_hide_render_set");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
  RNA_def_property_ui_icon(prop, ICON_RESTRICT_RENDER_OFF, -1);
  RNA_def_property_ui_text(prop, "Disable in Renders", "Globally disable in renders");
  RNA_def_property_update(prop, NC_SCENE | ND_LAYER_CONTENT, "rna_Collection_flag_update");

  rna_def_collection_lanpr(brna, srna);
  prop = RNA_def_property(srna, "lanpr", PROP_POINTER, PROP_NONE);
  RNA_def_property_struct_type(prop, "CollectionLANPR");
  RNA_def_property_ui_text(prop, "LANPR", "LANPR settings for the collection");
}

#endif
