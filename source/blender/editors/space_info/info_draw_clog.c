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
#include <BLI_dynstr.h>
#include <CLG_log.h>
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

enum eTextViewContext_LineFlag clog_line_draw_data(struct TextViewContext *tvc,
                                                   struct TextLine *UNUSED(text_line),
                                                   uchar fg[4],
                                                   uchar bg[4],
                                                   int *r_icon,
                                                   uchar r_icon_fg[4],
                                                   uchar r_icon_bg[4])
{
  const CLG_LogRecord *record = tvc->iter;
  int data_flag = 0;

  /* Same text color no matter what type of record. */
  UI_GetThemeColor4ubv(TH_TEXT, fg);
  data_flag = TVC_LINE_FG_SIMPLE;

  /* Zebra striping for background, only for deselected records. */
  if (tvc->iter_tmp % 2) {
    UI_GetThemeColor4ubv(TH_BACK, bg);
  }
  else {
    float col_alternating[4];
    UI_GetThemeColor4fv(TH_ROW_ALTERNATE, col_alternating);
    UI_GetThemeColorBlend4ubv(TH_BACK, TH_ROW_ALTERNATE, col_alternating[3], bg);
  }

  /* Icon color and background depend of record type. */
  int icon_fg_id;
  int icon_bg_id;

  if (tvc->iter_char_begin != 0) {
    *r_icon = ICON_NONE;
  }
  else if (record->severity == CLG_SEVERITY_FATAL) {
    icon_fg_id = TH_INFO_ERROR_TEXT;
    icon_bg_id = TH_INFO_ERROR;
    *r_icon = ICON_X;
  }
  else if (record->severity == CLG_SEVERITY_ERROR) {
    icon_fg_id = TH_INFO_ERROR_TEXT;
    icon_bg_id = TH_INFO_ERROR;
    *r_icon = ICON_CANCEL;
  }
  else if (record->severity == CLG_SEVERITY_WARN) {
    icon_fg_id = TH_INFO_WARNING_TEXT;
    icon_bg_id = TH_INFO_WARNING;
    *r_icon = ICON_ERROR;
  }
  else if (record->severity == CLG_SEVERITY_INFO) {
    icon_fg_id = TH_INFO_INFO_TEXT;
    icon_bg_id = TH_INFO_INFO;
    *r_icon = ICON_INFO;
  }
  else if (record->severity == CLG_SEVERITY_VERBOSE) {
    icon_fg_id = TH_INFO_DEBUG_TEXT;
    icon_bg_id = TH_INFO_DEBUG;
    *r_icon = ICON_PROPERTIES;
  }
  else if (record->severity == CLG_SEVERITY_DEBUG) {
    icon_fg_id = TH_INFO_PROPERTY_TEXT;
    icon_bg_id = TH_INFO_PROPERTY;
    *r_icon = ICON_SYSTEM;
  }
  else {
    *r_icon = ICON_NONE;
  }

  /* how to implement selection with logs?
    if (record->flag & RPT_SELECT) {
      icon_fg_id = TH_INFO_SELECTED;
      icon_bg_id = TH_INFO_SELECTED_TEXT;
    }
  */

  if (*r_icon != ICON_NONE) {
    UI_GetThemeColor4ubv(icon_fg_id, r_icon_fg);
    UI_GetThemeColor4ubv(icon_bg_id, r_icon_bg);
    return data_flag | TVC_LINE_BG | TVC_LINE_ICON | TVC_LINE_ICON_FG | TVC_LINE_ICON_BG;
  }

  return data_flag | TVC_LINE_BG;
}

static bool is_log_visible(const CLG_LogRecord *record, const SpaceInfo *sinfo)
{
  /* TODO (grzelins) */
  UNUSED_VARS(record, sinfo);
  return true;
}

static int report_textview_skip__internal(TextViewContext *tvc)
{
  const SpaceInfo *sinfo = tvc->arg1;
  while (tvc->iter && !is_log_visible(tvc->iter, sinfo)) {
    tvc->iter = (void *)((Link *)tvc->iter)->prev;
  }
  return (tvc->iter != NULL);
}

int clog_textview_begin(struct TextViewContext *tvc)
{
  const struct CLG_LogRecordList *records = tvc->arg2;

  tvc->sel_start = 0;
  tvc->sel_end = 0;

  /* iterator */
  tvc->iter = records->last;

  UI_ThemeClearColor(TH_BACK);
  GPU_clear(GPU_COLOR_BIT);

  tvc->iter_tmp = 0;
  if (tvc->iter && report_textview_skip__internal(tvc)) {
    /* init the newline iterator */
    //    const Report *report = tvc->iter;
    //    tvc->iter_char_end = report->len;
    //    report_textview_init__internal(tvc);

    return true;
  }

  return false;
}

void clog_textview_end(struct TextViewContext *UNUSED(tvc))
{
  /* pass */
}

int clog_textview_step(struct TextViewContext *tvc)
{
  const CLG_LogRecord *record = tvc->iter;
  tvc->iter = record->prev;
  return (tvc->iter != NULL);
}

void clog_textview_line_get(struct TextViewContext *tvc, struct ListBase *text_lines)
{
  const struct CLG_LogRecord *record = tvc->iter;
  TextLine *text_line = MEM_callocN(sizeof(*text_line), __func__);
  /* TODO (grzelins) before allocating memory here I must make sure tvc can free it afterwards
    const SpaceInfo *sinfo = tvc->arg1;
    const CLG_LogRecordList *records = tvc->arg2;

    DynStr *dynStr = BLI_dynstr_new();
    if (sinfo->log_format & INFO_LOG_SHOW_TIMESTAMP) {
      char timestamp_str[64];
      const uint64_t timestamp = record->timestamp;
      snprintf(timestamp_str,
               sizeof(timestamp_str),
               "%" PRIu64 ".%03u ",
               timestamp / 1000,
               (uint)(timestamp % 1000));
      BLI_dynstr_appendf(dynStr, "%s", timestamp_str);
    }
    if (sinfo->log_format & INFO_LOG_SHOW_LEVEL) {
      if (record->severity <= CLG_SEVERITY_VERBOSE) {
        BLI_dynstr_appendf(dynStr, "%s:%u ", clg_severity_as_text(record->severity),
    record->verbosity);
      }
      else {
        BLI_dynstr_appendf(dynStr, "%s ", clg_severity_as_text(record->severity));
      }
    }
    if (sinfo->log_format & INFO_LOG_SHOW_LOG_TYPE) {
      BLI_dynstr_appendf(dynStr, "(%s) ", record->type->identifier);
    }
    if (sinfo->log_format & INFO_LOG_SHOW_FILE_LINE) {
      const char *file_line = (sinfo->use_short_file_line) ? BLI_path_basename(record->file_line) :
                              record->file_line;
      BLI_dynstr_appendf(dynStr, "%s ", file_line);
    }
    if (sinfo->log_format & INFO_LOG_SHOW_FUNCTION) {
      BLI_dynstr_appendf(dynStr, "%s ", record->function);
    }
    if (sinfo->log_format & sinfo->use_log_message_new_line) {
      BLI_dynstr_append(dynStr, "\n");
    }

    BLI_dynstr_append(dynStr, record->message);
    char *cstr = BLI_dynstr_get_cstring(dynStr);
    Report *report;
    switch (record->severity) {
      case CLG_SEVERITY_DEBUG:
      case CLG_SEVERITY_VERBOSE:
        report = BKE_report_init(RPT_DEBUG, 0, cstr);
        break;
      case CLG_SEVERITY_INFO:
        report = BKE_report_init(RPT_INFO, 0, cstr);
        break;
      case CLG_SEVERITY_WARN:
        report = BKE_report_init(RPT_WARNING, 0, cstr);
        break;
      case CLG_SEVERITY_ERROR:
      case CLG_SEVERITY_FATAL:
        report = BKE_report_init(RPT_ERROR, 0, cstr);
        break;
      default:
        report = BKE_report_init(RPT_INFO, 0, cstr);
        break;
    }
    MEM_freeN(cstr);
    BLI_dynstr_free(dynStr);
  */

  text_line->line = record->message;
  text_line->len = strlen(record->message);
  //  text_line->line = record->message + tvc->iter_char_begin;
  //  text_line->len = tvc->iter_char_end - tvc->iter_char_begin;
  BLI_addhead(text_lines, text_line);
}
