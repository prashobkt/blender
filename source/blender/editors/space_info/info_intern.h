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
struct TextViewContext;
struct TextLine;
struct CLG_LogRecord;
struct CLG_LogRecordList;
enum eTextViewContext_LineDrawFlag;

#define INDEX_INVALID -1

/* info_ops.c */
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
                     const struct CLG_LogRecordList *log_records,
                     int mval_y);
int info_textview_height(const struct SpaceInfo *sinfo,
                         const struct ARegion *region,
                         const struct ReportList *reports,
                         const struct CLG_LogRecordList *log_records);
void info_textview_main(struct SpaceInfo *sinfo,
                        const struct ARegion *region,
                        const struct ReportList *reports,
                        const struct CLG_LogRecordList *log_records);

/* info_report.c */
bool is_report_visible(const struct Report *report, const struct SpaceInfo *sinfo);
// bool info_filter_text(const Report *report, const char *search_string);
void INFO_OT_report_select_pick(struct wmOperatorType *ot); /* report selection */
void INFO_OT_report_select_all(struct wmOperatorType *ot);
void INFO_OT_report_select_box(struct wmOperatorType *ot);

void INFO_OT_report_replay(struct wmOperatorType *ot);
void INFO_OT_report_delete(struct wmOperatorType *ot);
void INFO_OT_report_copy(struct wmOperatorType *ot);

/* info_clog.c */
void INFO_OT_clog_select_pick(struct wmOperatorType *ot); /* log selection */
void INFO_OT_clog_select_all(struct wmOperatorType *ot);
void INFO_OT_clog_select_box(struct wmOperatorType *ot);

void INFO_OT_clog_delete(struct wmOperatorType *ot);
void INFO_OT_clog_copy(struct wmOperatorType *ot);
void INFO_OT_clog_filter(struct wmOperatorType *ot);

/* info_draw_report.c */
enum eTextViewContext_LineDrawFlag report_line_draw_data(struct TextViewContext *tvc,
                                                         uchar fg[4],
                                                         uchar bg[4],
                                                         int *r_icon,
                                                         uchar r_icon_fg[4],
                                                         uchar r_icon_bg[4]);
int report_textview_begin(struct TextViewContext *tvc);
void report_textview_end(struct TextViewContext *tvc);
int report_textview_step(struct TextViewContext *tvc);
void report_textview_text_get(struct TextViewContext *tvc,
                              char **r_line,
                              int *r_len,
                              bool *owns_memory);
/* info_draw_clog.c */
char *clog_record_sprintfN(const struct CLG_LogRecord *record, const SpaceInfo *sinfo);
bool is_clog_record_visible(const struct CLG_LogRecord *record, const struct SpaceInfo *sinfo);
enum eTextViewContext_LineDrawFlag clog_line_draw_data(struct TextViewContext *tvc,
                                                       uchar fg[4],
                                                       uchar bg[4],
                                                       int *r_icon,
                                                       uchar r_icon_fg[4],
                                                       uchar r_icon_bg[4]);
int clog_textview_begin(struct TextViewContext *tvc);
void clog_textview_end(struct TextViewContext *tvc);
int clog_textview_step(struct TextViewContext *tvc);
void clog_textview_text_get(struct TextViewContext *tvc,
                            char **r_line,
                            int *r_len,
                            bool *owns_memory);

/* info_util.c */
void info_area_tag_redraw(const struct bContext *C);
bool info_match_string_filter(const char *search_pattern,
                              const char *string,
                              const bool use_match_case,
                              const bool use_match_glob,
                              const bool use_match_reverse);
