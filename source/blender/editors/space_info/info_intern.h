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
 * The Original Code is Copyright (C) 2008 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup spinfo
 */

#pragma once

#include "DNA_windowmanager_types.h"

/* internal exports only */

struct ReportList;
struct SpaceInfo;
struct wmOperatorType;

void FILE_OT_autopack_toggle(struct wmOperatorType *ot);
void FILE_OT_pack_all(struct wmOperatorType *ot);
void FILE_OT_unpack_all(struct wmOperatorType *ot);
void FILE_OT_unpack_item(struct wmOperatorType *ot);
void FILE_OT_pack_libraries(struct wmOperatorType *ot);
void FILE_OT_unpack_libraries(struct wmOperatorType *ot);

void FILE_OT_make_paths_relative(struct wmOperatorType *ot);
void FILE_OT_make_paths_absolute(struct wmOperatorType *ot);
void FILE_OT_report_missing_files(struct wmOperatorType *ot);
void FILE_OT_find_missing_files(struct wmOperatorType *ot);

void INFO_OT_reports_display_update(struct wmOperatorType *ot);
void INFO_OT_log_file_line_filter_add(struct wmOperatorType *ot);
void INFO_OT_log_file_line_filter_remove(struct wmOperatorType *ot);
void INFO_OT_log_function_filter_add(struct wmOperatorType *ot);
void INFO_OT_log_function_filter_remove(struct wmOperatorType *ot);
void INFO_OT_log_type_filter_add(struct wmOperatorType *ot);
void INFO_OT_log_type_filter_remove(struct wmOperatorType *ot);

/* info_draw.c */
void *info_text_pick(const struct SpaceInfo *sinfo,
                     const struct ARegion *region,
                     const struct ReportList *reports,
                     int mouse_y);
int info_textview_height(const struct SpaceInfo *sinfo,
                         const struct ARegion *region,
                         const struct ReportList *reports);
void info_textview_main(const struct SpaceInfo *sinfo,
                        const struct ARegion *region,
                        const struct ReportList *reports);

/* info_report.c */
ReportList *clog_to_report_list(struct SpaceInfo *sinfo);
int info_report_mask(const struct SpaceInfo *sinfo);
bool info_filter_text(const Report *report, const char *search_string);
void INFO_OT_select_pick(struct wmOperatorType *ot); /* report selection */
void INFO_OT_select_all(struct wmOperatorType *ot);
void INFO_OT_select_box(struct wmOperatorType *ot);

void INFO_OT_report_replay(struct wmOperatorType *ot);
void INFO_OT_report_delete(struct wmOperatorType *ot);
void INFO_OT_report_copy(struct wmOperatorType *ot);

#define IS_REPORT_VISIBLE(report, report_mask, search_string) \
  (info_filter_text(report, search_string) && ((report)->type & report_mask))
