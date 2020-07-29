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
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 */

/** \file
 * \ingroup bke
 */

#include <CLG_log.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_dynstr.h"
#include "BLI_utildefines.h"

#include "BLT_translation.h"

#include "BKE_global.h" /* G.background only */
#include "BKE_report.h"

static CLG_LogRef LOG = {"bke.report"};

const char *BKE_report_type_str(ReportType type)
{
  switch (type) {
    case RPT_INFO:
      return TIP_("Info");
    case RPT_OPERATOR:
      return TIP_("Operator");
    case RPT_PROPERTY:
      return TIP_("Property");
    case RPT_WARNING:
      return TIP_("Warning");
    case RPT_ERROR:
      return TIP_("Error");
    case RPT_ERROR_INVALID_INPUT:
      return TIP_("Invalid Input Error");
    case RPT_ERROR_INVALID_CONTEXT:
      return TIP_("Invalid Context Error");
    case RPT_ERROR_OUT_OF_MEMORY:
      return TIP_("Out Of Memory Error");
    default:
      return TIP_("Undefined Type");
  }
}

static enum CLG_Severity report_type_to_severity(ReportType type)
{
  switch (type) {
    case RPT_INFO:
      return CLG_SEVERITY_INFO;
    case RPT_OPERATOR:
      return CLG_SEVERITY_INFO;
    case RPT_PROPERTY:
      return CLG_SEVERITY_VERBOSE;
    case RPT_WARNING:
      return CLG_SEVERITY_INFO;
    case RPT_ERROR:
      return CLG_SEVERITY_ERROR;
    case RPT_ERROR_INVALID_INPUT:
      return CLG_SEVERITY_ERROR;
    case RPT_ERROR_INVALID_CONTEXT:
      return CLG_SEVERITY_ERROR;
    case RPT_ERROR_OUT_OF_MEMORY:
      return CLG_SEVERITY_ERROR;
    default:
      return CLG_SEVERITY_INFO;
  }
}

void BKE_reports_init(ReportList *reports, int flag)
{
  if (!reports) {
    return;
  }

  memset(reports, 0, sizeof(ReportList));

  reports->printlevel = RPT_ERROR;
  reports->flag = flag;
}

/**
 * Only frees the list \a reports.
 * To make displayed reports disappear, either remove window-manager reports
 * (wmWindowManager.reports, or CTX_wm_reports()), or use #WM_report_banners_cancel().
 */
void BKE_reports_clear(ReportList *reports)
{
  Report *report, *report_next;

  if (!reports) {
    return;
  }

  report = reports->list.first;

  while (report) {
    report_next = report->next;
    MEM_freeN((void *)report->message);
    MEM_freeN(report);
    report = report_next;
  }

  BLI_listbase_clear(&reports->list);
}

/** deep copy of reports */
ReportList *BKE_reports_duplicate(ReportList *reports)
{
  Report *report = reports->list.first, *report_next, *report_dup;
  ReportList *reports_new = MEM_dupallocN(reports);
  BLI_listbase_clear(&reports_new->list);

  while (report) {
    report_next = report->next;
    report_dup = MEM_dupallocN(report);
    report_dup->message = MEM_dupallocN(report->message);
    BLI_addtail(&reports_new->list, report_dup);
    report = report_next;
  }

  // TODO (grzelins) learn how to duplicate timer
  // reports_new->reporttimer

  return reports_new;
}

void BKE_report_format(ReportList *reports, ReportType type, int flags, const char *_message)
{
  Report *report;
  int len;
  const char *message = TIP_(_message);

  CLOG_AT_SEVERITY(&LOG,
                   report_type_to_severity(type),
                   0,
                   "ReportList(%p):%s: %s",
                   reports,
                   BKE_report_type_str(type),
                   message);

  if (reports) {
    char *message_alloc;
    report = MEM_callocN(sizeof(Report), "Report");
    report->type = type;
    report->flag = flags;
    report->typestr = BKE_report_type_str(type);

    len = strlen(message);
    message_alloc = MEM_mallocN(sizeof(char) * (len + 1), "ReportMessage");
    memcpy(message_alloc, message, sizeof(char) * (len + 1));
    report->message = message_alloc;
    report->len = len;
    BLI_addtail(&reports->list, report);
  }
}

void BKE_report(ReportList *reports, ReportType type, const char *_message)
{
  BKE_report_format(reports, type, 0, _message);
}

void BKE_reportf_format(ReportList *reports, ReportType type, int flags, const char *_format, ...)
{
  va_list args;
  const char *format = TIP_(_format);
  DynStr *message = BLI_dynstr_new();

  va_start(args, _format);
  BLI_dynstr_vappendf(message, format, args);
  va_end(args);

  char *cstring = BLI_dynstr_get_cstring(message);

  BKE_report_format(reports, type, flags, cstring);

  MEM_freeN(cstring);
  BLI_dynstr_free(message);
}

void BKE_reportf(ReportList *reports, ReportType type, const char *_format, ...)
{
  va_list args;
  const char *format = TIP_(_format);
  DynStr *message = BLI_dynstr_new();

  va_start(args, _format);
  BLI_dynstr_vappendf(message, format, args);
  va_end(args);

  char *cstring = BLI_dynstr_get_cstring(message);

  BKE_report_format(reports, type, 0, cstring);

  MEM_freeN(cstring);
  BLI_dynstr_free(message);
}

void BKE_reports_prepend(ReportList *reports, const char *_prepend)
{
  Report *report;
  DynStr *ds;
  const char *prepend = TIP_(_prepend);

  if (!reports) {
    return;
  }

  for (report = reports->list.first; report; report = report->next) {
    ds = BLI_dynstr_new();

    BLI_dynstr_append(ds, prepend);
    BLI_dynstr_append(ds, report->message);
    MEM_freeN((void *)report->message);

    report->message = BLI_dynstr_get_cstring(ds);
    report->len = BLI_dynstr_get_len(ds);

    BLI_dynstr_free(ds);
  }
}

void BKE_reports_prependf(ReportList *reports, const char *_prepend, ...)
{
  Report *report;
  DynStr *ds;
  va_list args;
  const char *prepend = TIP_(_prepend);

  if (!reports) {
    return;
  }

  for (report = reports->list.first; report; report = report->next) {
    ds = BLI_dynstr_new();
    va_start(args, _prepend);
    BLI_dynstr_vappendf(ds, prepend, args);
    va_end(args);

    BLI_dynstr_append(ds, report->message);
    MEM_freeN((void *)report->message);

    report->message = BLI_dynstr_get_cstring(ds);
    report->len = BLI_dynstr_get_len(ds);

    BLI_dynstr_free(ds);
  }
}

ReportType BKE_report_print_level(ReportList *reports)
{
  if (!reports) {
    return RPT_ERROR;
  }

  return reports->printlevel;
}

void BKE_report_print_level_set(ReportList *reports, ReportType level)
{
  if (!reports) {
    return;
  }

  reports->printlevel = level;
}

/** return pretty printed reports with minimum level (level=0 - print all) */
char *BKE_reports_sprintfN(ReportList *reports, ReportType level)
{
  Report *report;
  DynStr *ds;
  char *cstring;

  ds = BLI_dynstr_new();

  if (reports == NULL) {
    BLI_dynstr_append(ds, "ReportList(<NULL>):");
    cstring = BLI_dynstr_get_cstring(ds);
    BLI_dynstr_free(ds);
    return cstring;
  }
  else {
    BLI_dynstr_appendf(ds, "ReportList(%p):", reports);
  }

  if (BLI_listbase_is_empty(&reports->list)) {
    BLI_dynstr_append(ds, " Empty list");
  }
  else {
    for (report = reports->list.first; report; report = report->next) {
      if (report->type >= level) {
        BLI_dynstr_appendf(ds, "%s: %s\n", report->typestr, report->message);
      }
    }
  }

  cstring = BLI_dynstr_get_cstring(ds);
  BLI_dynstr_free(ds);
  return cstring;
}

Report *BKE_reports_last_displayable(ReportList *reports)
{
  Report *report;

  for (report = reports->list.last; report; report = report->prev) {
    if (ELEM(report->type, RPT_ERROR, RPT_WARNING, RPT_INFO)) {
      return report;
    }
  }

  return NULL;
}

void BKE_reports_move(ReportList *src, ReportList *dst)
{
  Report *report;
  for (report = src->list.first; report; report = report->next) {
    BLI_addtail(&dst->list, report);
  }
  BLI_listbase_clear(&src->list);
}

bool BKE_reports_contain(ReportList *reports, ReportType level)
{
  Report *report;
  if (reports != NULL) {
    for (report = reports->list.first; report; report = report->next) {
      if (report->type >= level) {
        return true;
      }
    }
  }
  return false;
}

bool BKE_report_write_file_fp(FILE *fp, ReportList *reports, const char *header)
{
  Report *report;

  if (header) {
    fputs(header, fp);
  }

  for (report = reports->list.first; report; report = report->next) {
    fprintf((FILE *)fp, "%s  # %s\n", report->message, report->typestr);
  }

  return true;
}

bool BKE_report_write_file(const char *filepath, ReportList *reports, const char *header)
{
  FILE *fp;

  errno = 0;
  fp = BLI_fopen(filepath, "wb");
  if (fp == NULL) {
    CLOG_ERROR(&LOG,
               "Unable to save '%s': %s",
               filepath,
               errno ? strerror(errno) : "Unknown error opening file");
    return false;
  }

  BKE_report_write_file_fp(fp, reports, header);

  fclose(fp);

  return true;
}
