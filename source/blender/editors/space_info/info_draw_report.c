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
#include <MEM_guardedalloc.h>
#include <limits.h>
#include <string.h>

#include "BKE_report.h"
#include "BLI_utildefines.h"
#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "GPU_framebuffer.h"
#include "UI_interface.h"
#include "UI_resources.h"
#include "UI_view2d.h"
#include "info_intern.h"
#include "textview.h"

#include "../space_text/text_format.h"

#define TABNUMBER 4

enum eTextViewContext_LineFlag report_line_draw_data(TextViewContext *tvc,
                                                     TextLine *text_line,
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
    TextFormatType *py_formatter = ED_text_format_get_by_extension("py");
    py_formatter->format_line(text_line, TABNUMBER, false);
    data_flag = TVC_LINE_FG_COMPLEX;
  }
  else {
    /* Same text color no matter what type of report. */
    UI_GetThemeColor4ubv((report->flag & RPT_SELECT) ? TH_INFO_SELECTED_TEXT : TH_TEXT, fg);
    data_flag = TVC_LINE_FG_SIMPLE;
  }

  /* Zebra striping for background, only for deselected reports. */
  if (report->flag & RPT_SELECT) {
    int bg_id = (report == active_report) ? TH_INFO_ACTIVE : TH_INFO_SELECTED;
    UI_GetThemeColor4ubv(bg_id, bg);
  }
  else {
    if (tvc->iter_tmp % 2) {
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

  if (tvc->iter_char_begin != 0) {
    *r_icon = ICON_NONE;
  }
  else if (report->type & RPT_ERROR_ALL) {
    icon_fg_id = TH_INFO_ERROR_TEXT;
    icon_bg_id = TH_INFO_ERROR;
    *r_icon = ICON_CANCEL;
  }
  else if (report->type & RPT_WARNING_ALL) {
    icon_fg_id = TH_INFO_WARNING_TEXT;
    icon_bg_id = TH_INFO_WARNING;
    *r_icon = ICON_ERROR;
  }
  else if (report->type & RPT_INFO_ALL) {
    icon_fg_id = TH_INFO_INFO_TEXT;
    icon_bg_id = TH_INFO_INFO;
    *r_icon = ICON_INFO;
  }
  else if (report->type & RPT_DEBUG_ALL) {
    icon_fg_id = TH_INFO_DEBUG_TEXT;
    icon_bg_id = TH_INFO_DEBUG;
    *r_icon = ICON_SYSTEM;
  }
  else if (report->type & RPT_PROPERTY_ALL) {
    icon_fg_id = TH_INFO_PROPERTY_TEXT;
    icon_bg_id = TH_INFO_PROPERTY;
    *r_icon = ICON_OPTIONS;
  }
  else if (report->type & RPT_OPERATOR_ALL) {
    icon_fg_id = TH_INFO_OPERATOR_TEXT;
    icon_bg_id = TH_INFO_OPERATOR;
    *r_icon = ICON_CHECKMARK;
  }
  else {
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

static void report_textview_init__internal(TextViewContext *tvc)
{
  const Report *report = tvc->iter;
  const char *str = report->message;
  for (int i = tvc->iter_char_end - 1; i >= 0; i -= 1) {
    if (str[i] == '\n') {
      tvc->iter_char_begin = i + 1;
      return;
    }
  }
  tvc->iter_char_begin = 0;
}

static int report_textview_skip__internal(TextViewContext *tvc)
{
  const SpaceInfo *sinfo = tvc->arg1;
  const int report_mask = info_report_mask(sinfo);
  while (tvc->iter &&
         !IS_REPORT_VISIBLE((const Report *)tvc->iter, report_mask, sinfo->search_string)) {
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

  tvc->iter_tmp = 0;
  if (tvc->iter && report_textview_skip__internal(tvc)) {
    /* init the newline iterator */
    const Report *report = tvc->iter;
    tvc->iter_char_end = report->len;
    report_textview_init__internal(tvc);

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
  /* simple case, but no newline support */
  const Report *report = tvc->iter;

  if (tvc->iter_char_begin <= 0) {
    tvc->iter = (void *)((Link *)tvc->iter)->prev;
    if (tvc->iter && report_textview_skip__internal(tvc)) {
      tvc->iter_tmp++;

      report = tvc->iter;
      tvc->iter_char_end = report->len; /* reset start */
      report_textview_init__internal(tvc);

      return true;
    }
    return false;
  }

  /* step to the next newline */
  tvc->iter_char_end = tvc->iter_char_begin - 1;
  report_textview_init__internal(tvc);

  return true;
}

void report_textview_line_get(TextViewContext *tvc, ListBase *text_lines)
{
  const Report *report = tvc->iter;
  TextLine *text_line = MEM_callocN(sizeof(*text_line), __func__);
  text_line->line = report->message + tvc->iter_char_begin;
  text_line->len = tvc->iter_char_end - tvc->iter_char_begin;
  BLI_addhead(text_lines, text_line);
}