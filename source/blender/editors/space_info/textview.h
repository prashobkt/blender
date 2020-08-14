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

#pragma once

typedef struct TextViewContextLine {
  /* keep in sync with TextLine for seamless casting (mainly for syntax highlighting) */
  struct TextViewContextLine *next, *prev;

  char *line;
  /** May be NULL if syntax is off or not yet formatted. */
  char *format;
  /** Blen unused. */
  int len;
  char _pad0[4];
  /* TextLine end */

  char owns_line;
  char _pad1[7];
} TextViewContextLine;

enum eTextViewContext_LineDrawFlag {
  TVC_LINE_FG_SIMPLE = (1 << 0),
  TVC_LINE_BG = (1 << 1),
  TVC_LINE_ICON = (1 << 2),
  TVC_LINE_ICON_FG = (1 << 3),
  TVC_LINE_ICON_BG = (1 << 4),
  /** Indicate that this syntax for this line should be computed separately
   * text_format_draw_font_color, FMT_TYPE_SYMBOL, TH_SYNTAX_S */
  TVC_LINE_FG_SYNTAX_START = (1 << 5),
  TVC_LINE_FG_SYNTAX_END = (1 << 6),
  TVC_LINE_FG_SYNTAX_PYTHON = (1 << 7),
  /* Add more syntax types as needed */
};

#define TVC_LINE_FG_SYNTAX TVC_LINE_FG_SYNTAX_PYTHON

typedef struct TextViewContext {
  /** Font size scaled by the interface size. */
  int lheight;
  /** Text selection, when a selection range is in use. */
  int sel_start, sel_end;

  int row_vpadding;

  /** Area to draw text: (0, 0, winx, winy) with a margin applied and scroll-bar subtracted. */
  rcti draw_rect;
  /** Area to draw text background colors (extending beyond text in some cases). */
  rcti draw_rect_outer;

  /** Scroll offset in pixels. */
  int scroll_ymin, scroll_ymax;

  /* callbacks */
  int (*begin)(struct TextViewContext *tvc);
  void (*end)(struct TextViewContext *tvc);
  const void *arg1;
  const void *arg2;

  /* iterator */
  int (*step)(struct TextViewContext *tvcl);

  void (*text_get)(struct TextViewContext *tvc, char **r_line, int *r_len, bool *owns_memory);
  enum eTextViewContext_LineDrawFlag (*line_draw_data)(struct TextViewContext *tvc,
                                                       uchar fg[4],
                                                       uchar bg[4],
                                                       int *r_icon,
                                                       uchar r_icon_fg[4],
                                                       uchar r_icon_bg[4]);
  void (*draw_cursor)(struct TextViewContext *tvc, int cwidth, int columns);
  /* constant theme colors */
  void (*const_colors)(struct TextViewContext *tvc, unsigned char bg_sel[4]);
  void (*syntax_colors)(struct TextViewContext *tvc, unsigned char bg_sel[4]);
  const void *iter;
  int iter_index;
  short tabnumber;
} TextViewContext;

int textview_draw(struct TextViewContext *tvc,
                  const bool do_draw,
                  const int mval_init[2],
                  void **r_mval_pick_item,
                  int *r_mval_pick_offset);
