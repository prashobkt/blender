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
 * The Original Code is Copyright (C) 2010 Blender Foundation.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

#ifndef __DNA_LANPR_TYPES_H__
#define __DNA_LANPR_TYPES_H__

/** \file DNA_lanpr_types.h
 *  \ingroup DNA
 */

#include "DNA_listBase.h"
#include "DNA_ID.h"
#include "DNA_collection_types.h"

struct Object;
struct Material;
struct Collection;

typedef enum LANPR_TaperSettings {
  LANPR_USE_DIFFERENT_TAPER = 0,
  LANPR_USE_SAME_TAPER = 1,
} LANPR_TaperSettings;

typedef enum LANPR_NomalEffect {
  /* Shouldn't have access to zero value. */
  /* Enable/disable is another flag. */
  LANPR_NORMAL_DIRECTIONAL = 1,
  LANPR_NORMAL_POINT = 2,
} LANPR_NomalEffect;

typedef enum LANPR_ComponentMode {
  LANPR_COMPONENT_MODE_ALL = 0,
  LANPR_COMPONENT_MODE_OBJECT = 1,
  LANPR_COMPONENT_MODE_MATERIAL = 2,
  LANPR_COMPONENT_MODE_COLLECTION = 3,
} LANPR_ComponentMode;

typedef enum LANPR_ComponentUsage {
  LANPR_COMPONENT_INCLUSIVE = 0,
  LANPR_COMPONENT_EXCLUSIVE = 1,
} LANPR_ComponentUsage;

typedef enum LANPR_ComponentLogic {
  LANPR_COMPONENT_LOGIG_OR = 0,
  LANPR_COMPONENT_LOGIC_AND = 1,
} LANPR_ComponentLogic;

struct DRWShadingGroup;

typedef struct LANPR_LineLayerComponent {
  struct LANPR_LineLayerComponent *next, *prev;

  struct Object *object_select;
  struct Material *material_select;
  struct Collection *collection_select;

  int component_mode;
  int what;

} LANPR_LineLayerComponent;

typedef struct LANPR_LineType {
  int use;
  float thickness;
  float color[4];
} LANPR_LineType;

typedef enum LANPR_LineLayerFlags {
  LANPR_LINE_LAYER_USE_SAME_STYLE = (1 << 0),
  LANPR_LINE_LAYER_USE_MULTIPLE_LEVELS = (1 << 1),
  LANPR_LINE_LAYER_NORMAL_ENABLED = (1 << 2),
  LANPR_LINE_LAYER_NORMAL_INVERSE = (1 << 3),
} LANPR_LineLayerFlags;

typedef struct LANPR_LineLayer {
  struct LANPR_LineLayer *next, *prev;

  int flags;
  int _pad1;
  int level_start;
  int level_end;

  /** To be displayed on the list */
  char name[64];

  LANPR_LineType contour;
  LANPR_LineType crease;
  LANPR_LineType edge_mark;
  LANPR_LineType material_separate;
  LANPR_LineType intersection;

  float thickness;

  float color[4];

  int normal_mode;
  float normal_ramp_begin;
  float normal_ramp_end;
  float normal_thickness_start;
  float normal_thickness_end;
  struct Object *normal_control_object;

  /** For component evaluation */
  int logic_mode;
  int _pad3;

  ListBase components;

  struct DRWShadingGroup *shgrp;
  struct GPUBatch *batch;

} LANPR_LineLayer;

#endif
