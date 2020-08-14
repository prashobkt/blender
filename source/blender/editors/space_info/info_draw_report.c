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
 * \ingroup spinfo
 */

#include <BLI_blenlib.h>
#include <DNA_text_types.h>
#include <string.h>

#include "BLI_utildefines.h"
#include "DNA_space_types.h"
#include "GPU_framebuffer.h"
#include "UI_resources.h"
#include "info_intern.h"
#include "textview.h"

enum eTextViewContext_LineDrawFlag report_line_draw_data(TextViewContext *tvc,
                                                         uchar fg[4],
                                                         uchar bg[4],
                                                         int *r_icon,
                                                         uchar r_icon_fg[4],
                                                         uchar r_icon_bg[4])
{
  const Report *report = tvc->iter;
  const SpaceInfo *sinfo = tvc->arg1;
  const ReportList *reports = tvc->arg2;
  const Report *active_report = BLI_findlink((const struct ListBase *)reports,
                                             sinfo->active_index);
  int data_flag = 0;

  if (report->flag & RPT_PYTHON) {
    /* this is enough for single line syntax highlighting */
    data_flag |= TVC_LINE_FG_SYNTAX_PYTHON | TVC_LINE_FG_SYNTAX_START | TVC_LINE_FG_SYNTAX_END;
  }
  else {
    /* Same text color no matter what type of report. */
    UI_GetThemeColor4ubv((report->flag & RPT_SELECT) ? TH_INFO_SELECTED_TEXT : TH_TEXT, fg);
    data_flag |= TVC_LINE_FG_SIMPLE;
  }

  /* Zebra striping for background, only for deselected reports. */
  if (report->flag & RPT_SELECT) {
    int bg_id = (report == active_report) ? TH_INFO_ACTIVE : TH_INFO_SELECTED;
    UI_GetThemeColor4ubv(bg_id, bg);
  }
  else {
    if (tvc->iter_index % 2) {
      UI_GetThemeColor4ubv(TH_BACK, bg);
    }
    else {
      float col_alternating[4];
      UI_GetThemeColor4fv(TH_ROW_ALTERNATE, col_alternating);
      UI_GetThemeColorBlend4ubv(TH_BACK, TH_ROW_ALTERNATE, col_alternating[3], bg);
    }
  }

  /* Icon color and background depend of report type. */
  int icon_fg_id;
  int icon_bg_id;

  if (report->type & RPT_ERROR_ALL)
  {
    icon_fg_id = TH_INFO_ERROR_TEXT;
    icon_bg_id = TH_INFO_ERROR;
    *r_icon = ICON_CANCEL;
  }
  else if (report->type & RPT_WARNING_ALL)
  {
    icon_fg_id = TH_INFO_WARNING_TEXT;
    icon_bg_id = TH_INFO_WARNING;
    *r_icon = ICON_ERROR;
  }
  else if (report->type & RPT_INFO_ALL)
  {
    icon_fg_id = TH_INFO_INFO_TEXT;
    icon_bg_id = TH_INFO_INFO;
    *r_icon = ICON_INFO;
  }
  else if (report->type & RPT_DEBUG_ALL)
  {
    icon_fg_id = TH_INFO_DEBUG_TEXT;
    icon_bg_id = TH_INFO_DEBUG;
    *r_icon = ICON_SYSTEM;
  }
  else if (report->type & RPT_PROPERTY_ALL)
  {
    icon_fg_id = TH_INFO_PROPERTY_TEXT;
    icon_bg_id = TH_INFO_PROPERTY;
    *r_icon = ICON_OPTIONS;
  }
  else if (report->type & RPT_OPERATOR_ALL)
  {
    icon_fg_id = TH_INFO_OPERATOR_TEXT;
    icon_bg_id = TH_INFO_OPERATOR;
    *r_icon = ICON_CHECKMARK;
  }
  else
  {
    *r_icon = ICON_NONE;
  }

  if (report->flag & RPT_SELECT) {
    icon_fg_id = TH_INFO_SELECTED;
    icon_bg_id = TH_INFO_SELECTED_TEXT;
  }

  if (*r_icon != ICON_NONE) {
    UI_GetThemeColor4ubv(icon_fg_id, r_icon_fg);
    UI_GetThemeColor4ubv(icon_bg_id, r_icon_bg);
    return data_flag | TVC_LINE_BG | TVC_LINE_ICON | TVC_LINE_ICON_FG | TVC_LINE_ICON_BG;
  }

  return data_flag | TVC_LINE_BG;
}

static int report_textview_skip__internal(TextViewContext *tvc)
{
  const SpaceInfo *sinfo = tvc->arg1;
  while (tvc->iter && !is_report_visible((const Report *)tvc->iter, sinfo)) {
    tvc->iter = (void *)((Link *)tvc->iter)->prev;
  }
  return (tvc->iter != NULL);
}

int report_textview_begin(TextViewContext *tvc)
{
  const ReportList *reports = tvc->arg2;

  tvc->sel_start = 0;
  tvc->sel_end = 0;

  /* iterator */
  tvc->iter = reports->list.last;

  UI_ThemeClearColor(TH_BACK);
  GPU_clear(GPU_COLOR_BIT);

  if (tvc->iter && report_textview_skip__internal(tvc)) {
    return true;
  }

  return false;
}

void report_textview_end(TextViewContext *UNUSED(tvc))
{
  /* pass */
}

int report_textview_step(TextViewContext *tvc)
{
  tvc->iter = (void *)((Link *)tvc->iter)->prev;
  if (tvc->iter && report_textview_skip__internal(tvc)) {
    return true;
  }
  return false;
}

void report_textview_text_get(struct TextViewContext *tvc,
                              char **r_line,
                              int *r_len,
                              bool *owns_memory)
{
  const Report *report = tvc->iter;
  *r_line = (char *)(report->message);
  *r_len = report->len;
  *owns_memory = false;
}