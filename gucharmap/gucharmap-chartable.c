/*
 * Copyright (c) 2004 Noah Levitt
 * Copyright (C) 2007 Christian Persch
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
 */

#include "config.h"
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include "gucharmap-marshal.h"
#include "gucharmap-intl.h"
#include "gucharmap-chartable.h"
#include "gucharmap-chartable-private.h"
#include "gucharmap-unicode-info.h"

// disable until finished refactoring
#undef ENABLE_ACCESSIBLE

#ifdef ENABLE_ACCESSIBLE
#include "chartable_accessible.h"
#endif

struct _GucharmapChartable
{
  GtkDrawingArea parent_instance;

  /* scrollable implementation */
  GtkAdjustment *vadjustment;
  gulong vadjustment_changed_handler_id;

  /* rows and columns on a page */
  gint rows, cols;

  GdkPixmap *pixmap;

  gchar *font_name;
  PangoLayout *pango_layout;

  gint page_first_cell;
  gint active_cell;
  gint old_page_first_cell;
  gint old_active_cell;

  GtkWidget *zoom_window;
  GtkWidget *zoom_image;
  gboolean zoom_mode_enabled;

  gboolean snap_pow2_enabled;

  /* for dragging (#114534) */
  gdouble click_x, click_y; 

  GtkTargetList *target_list;

  GucharmapCodepointList *codepoint_list;
  gboolean codepoint_list_changed;
};

struct _GucharmapChartableClass
{
  GtkDrawingAreaClass parent_class;

  void    (* set_scroll_adjustments) (GucharmapChartable *chartable,
                                      GtkAdjustment      *hadjustment,
                                      GtkAdjustment      *vadjustment);

  void (* activate) (GucharmapChartable *chartable, gunichar uc);
  void (* set_active_char) (GucharmapChartable *chartable, guint ch);
  void (* status_message) (GucharmapChartable *chartable, const gchar *message);
};

enum 
{
  ACTIVATE,
  SET_ACTIVE_CHAR,
  STATUS_MESSAGE,
  NUM_SIGNALS
};

static void gucharmap_chartable_class_init (GucharmapChartableClass *klass);
static void gucharmap_chartable_finalize   (GObject *object);

static guint gucharmap_chartable_signals[NUM_SIGNALS];
static void
set_top_row (GucharmapChartable *chartable, 
             gint            row);

G_DEFINE_TYPE (GucharmapChartable, gucharmap_chartable, GTK_TYPE_DRAWING_AREA)

/* depends on directionality */
static guint
get_cell_at_rowcol (GucharmapChartable *chartable,
                    gint            row,
                    gint            col)
{
  GtkWidget *widget = GTK_WIDGET (chartable);
  if (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL)
    return chartable->page_first_cell + row * chartable->cols + (chartable->cols - col - 1);
  else
    return chartable->page_first_cell + row * chartable->cols + col;
}

/* Depends on directionality. Column 0 is the furthest left.  */
gint
_gucharmap_chartable_cell_column (GucharmapChartable *chartable,
                              guint cell)
{
  GtkWidget *widget = GTK_WIDGET (chartable);
  if (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL)
    return chartable->cols - (cell - chartable->page_first_cell) % chartable->cols - 1;
  else
    return (cell - chartable->page_first_cell) % chartable->cols;
}

/* computes the column width based solely on the font size */
static gint
bare_minimal_column_width (GucharmapChartable *chartable)
{
  GtkWidget *widget = GTK_WIDGET (chartable);
  return PANGO_PIXELS (3.0 * pango_font_description_get_size (widget->style->font_desc));
}

static gint
minimal_column_width (GucharmapChartable *chartable)
{
  GtkWidget *widget = GTK_WIDGET (chartable);
  gint total_extra_pixels;
  gint bare_minimal_width = bare_minimal_column_width (chartable);

  total_extra_pixels = widget->allocation.width - (chartable->cols * bare_minimal_width + 1);

  return bare_minimal_width + total_extra_pixels / chartable->cols;
}

/* not all columns are necessarily the same width because of padding */
gint
_gucharmap_chartable_column_width (GucharmapChartable *chartable, gint col)
{
  GtkWidget *widget = GTK_WIDGET (chartable);
  gint num_padded_columns;
  gint min_col_w = minimal_column_width (chartable);

  num_padded_columns = widget->allocation.width - (min_col_w * chartable->cols + 1);

  if (chartable->cols - col <= num_padded_columns)
    return min_col_w + 1;
  else
    return min_col_w;
}

/* calculates the position of the left end of the column (just to the right
 * of the left border) */
/* XXX: calling this repeatedly is not the most efficient, but it probably
 * is the most readable */
gint
_gucharmap_chartable_x_offset (GucharmapChartable *chartable, gint col)
{
  gint c, x;

  for (c = 0, x = 1;  c < col;  c++)
    x += _gucharmap_chartable_column_width (chartable, c);

  return x;
}

/* computes the row height based solely on the font size */
static gint
bare_minimal_row_height (GucharmapChartable *chartable)
{
  GtkWidget *widget = GTK_WIDGET (chartable);
  return PANGO_PIXELS (2.5 * pango_font_description_get_size (widget->style->font_desc));
}

static gint
minimal_row_height (GucharmapChartable *chartable)
{
  GtkWidget *widget = GTK_WIDGET (chartable);
  gint total_extra_pixels;
  gint bare_minimal_height = bare_minimal_row_height (chartable);

  total_extra_pixels = widget->allocation.height - (chartable->rows * bare_minimal_height + 1);

  return bare_minimal_height + total_extra_pixels / chartable->rows;
}

/* not all rows are necessarily the same height because of padding */
gint
_gucharmap_chartable_row_height (GucharmapChartable *chartable, gint row)
{
  GtkWidget *widget = GTK_WIDGET (chartable);
  gint num_padded_rows;
  gint min_row_h = minimal_row_height (chartable);

  num_padded_rows = widget->allocation.height - (min_row_h * chartable->rows + 1);

  if (chartable->rows - row <= num_padded_rows)
    return min_row_h + 1;
  else
    return min_row_h;
}

/* calculates the position of the top end of the row (just below the top
 * border) */
/* XXX: calling this repeatedly is not the most efficient, but it probably
 * is the most readable */
gint
_gucharmap_chartable_y_offset (GucharmapChartable *chartable, gint row)
{
  gint r, y;

  for (r = 0, y = 1;  r < row;  r++)
    y += _gucharmap_chartable_row_height (chartable, r);

  return y;
}

#ifdef ENABLE_ACCESSIBLE

static AtkObject*
chartable_accessible_factory_create_accessible (GObject *obj)
{
  GtkWidget *widget;

  g_return_val_if_fail (GTK_IS_WIDGET (obj), NULL);

  widget = GTK_WIDGET (obj);
  return chartable_accessible_new (widget);
}

#endif

static gint
compute_zoom_font_size (GucharmapChartable *chartable)
{
  GtkWidget *widget = GTK_WIDGET (chartable);
  gint screen_height;
  gdouble limit;
  gdouble scale;
  gint font_size;

  screen_height = gdk_screen_get_height (
          gtk_widget_get_screen (widget));

  limit = (0.3 * screen_height) / bare_minimal_row_height (chartable);
  scale = CLAMP (limit, 1.0, 12.0);

  font_size = pango_font_description_get_size (
          gtk_widget_get_style (widget)->font_desc);

  return scale * ((font_size > 0) ? font_size : 10 * PANGO_SCALE);
}

/* returns the font family of the last glyph item in the first line of the
 * layout; should be freed by caller */
static gchar *
get_font (PangoLayout *layout)
{
  PangoLayoutLine *line;
  PangoGlyphItem *glyph_item;
  PangoFont *font;
  GSList *run_node;
  gchar *family;
  PangoFontDescription *font_desc;

  line = pango_layout_get_line (layout, 0);

  /* get to the last glyph_item (the one with the character we're drawing */
  for (run_node = line->runs;  
       run_node && run_node->next;  
       run_node = run_node->next);

  if (run_node)
    {
      glyph_item = run_node->data;
      font = glyph_item->item->analysis.font;
      font_desc = pango_font_describe (font);

      family = g_strdup (pango_font_description_get_family (font_desc));

      pango_font_description_free (font_desc);
    }
  else
    family = NULL;

  return family;
}

/* font_family (if not null) gets filled in with the actual font family
 * used to draw the character */
static PangoLayout *
layout_scaled_glyph (GucharmapChartable *chartable, 
                     gunichar uc, 
                     gint font_size,
                     gchar **font_family)
{
  GtkWidget *widget = GTK_WIDGET (chartable);
  PangoFontDescription *font_desc;
  PangoLayout *layout;
  gchar buf[11];

  font_desc = pango_font_description_copy (
          gtk_widget_get_style (widget)->font_desc);
  pango_font_description_set_size (font_desc, font_size);

  layout = pango_layout_new (pango_layout_get_context (chartable->pango_layout));

  pango_layout_set_font_description (layout, font_desc);

  buf[gucharmap_unichar_to_printable_utf8 (uc, buf)] = '\0';
  pango_layout_set_text (layout, buf, -1);

  if (font_family)
    *font_family = get_font (layout);

  pango_font_description_free (font_desc);

  return layout;
}

static GdkPixmap *
create_glyph_pixmap (GucharmapChartable *chartable,
                     gunichar wc,
                     gint font_size,
                     gboolean draw_font_family)
{
  GtkWidget *widget = GTK_WIDGET (chartable);
  enum { PADDING = 8 };

  PangoLayout *pango_layout, *pango_layout2 = NULL;
  PangoRectangle char_rect, family_rect;
  gint pixmap_width, pixmap_height;
  GtkStyle *style;
  GdkPixmap *pixmap;
  gchar *family;

  /* Apply the scaling.  Unfortunately not all fonts seem to be scalable.
   * We could fall back to GdkPixbuf scaling, but that looks butt ugly :-/
   */
  pango_layout = layout_scaled_glyph (chartable, wc,
                                      font_size, &family);
  pango_layout_get_pixel_extents (pango_layout, &char_rect, NULL);

  if (draw_font_family)
    {
      if (family == NULL)
        family = g_strdup (_("[not a printable character]"));

      pango_layout2 = gtk_widget_create_pango_layout (GTK_WIDGET (chartable), family);
      pango_layout_get_pixel_extents (pango_layout2, NULL, &family_rect);

      /* Make the GdkPixmap large enough to account for possible offsets in the
       * ink extents of the glyph. */
      pixmap_width  = MAX (char_rect.width, family_rect.width)  + 2 * PADDING;
      pixmap_height = family_rect.height + char_rect.height + 4 * PADDING;
    }
  else
    {
      pixmap_width  = char_rect.width + 2 * PADDING;
      pixmap_height = char_rect.height + 2 * PADDING;
    }

  style = gtk_widget_get_style (widget);

  pixmap = gdk_pixmap_new (widget->window,
                           pixmap_width, pixmap_height, -1);

  gdk_draw_rectangle (pixmap, style->base_gc[GTK_STATE_NORMAL],
                      TRUE, 0, 0, pixmap_width, pixmap_height);

  /* Draw a rectangular border, taking char_rect offsets into account. */
  gdk_draw_rectangle (pixmap, style->fg_gc[GTK_STATE_INSENSITIVE], 
                      FALSE, 1, 1, pixmap_width - 3, pixmap_height - 3);

  /* Now draw the glyph.  The coordinates are adapted
   * in order to compensate negative char_rect offsets. */
  gdk_draw_layout (pixmap, style->text_gc[GTK_STATE_NORMAL],
                   -char_rect.x + PADDING, -char_rect.y + PADDING,
                   pango_layout);
  g_object_unref (pango_layout);

  if (draw_font_family)
    {
      gdk_draw_line (pixmap, style->dark_gc[GTK_STATE_NORMAL],
                     6 + 1, char_rect.height + 2 * PADDING,
                     pixmap_width - 3 - 6, char_rect.height + 2 * PADDING);
      gdk_draw_layout (pixmap, style->text_gc[GTK_STATE_NORMAL],
                       PADDING, pixmap_height - PADDING - family_rect.height,
                       pango_layout2);

      g_object_unref (pango_layout2);
    }

  g_free (family);

  return pixmap;
}

static void
get_appropriate_upper_left_xy (GucharmapChartable *chartable, 
                               gint width,  gint height,
                               gint x_root, gint y_root,
                               gint *x,     gint *y)
{
  gint row, col;

  row = (chartable->active_cell - chartable->page_first_cell) / chartable->cols;
  col = _gucharmap_chartable_cell_column (chartable, chartable->active_cell);

  *x = x_root;
  *y = y_root;

  if (row >= chartable->rows / 2)
    *y -= height;

  if (col >= chartable->cols / 2)
    *x -= width;
}

/* places the zoom window toward the inside of the coordinates */
static void
place_zoom_window (GucharmapChartable *chartable, gint x_root, gint y_root)
{
  GdkPixmap *pixmap;
  gint width, height, x, y;

  g_return_if_fail (chartable->zoom_window != NULL);

  gtk_image_get_pixmap (GTK_IMAGE (chartable->zoom_image), &pixmap, NULL);
  if (!pixmap)
    return;

  gdk_drawable_get_size (GDK_DRAWABLE (pixmap), &width, &height);
  get_appropriate_upper_left_xy (chartable, width, height,
                                 x_root, y_root, &x, &y);
  gtk_window_move (GTK_WINDOW (chartable->zoom_window), x, y);
}

static void
update_zoom_window (GucharmapChartable *chartable)
{
  GdkPixmap *pixmap;

  pixmap = create_glyph_pixmap (chartable,
                                gucharmap_chartable_get_active_character (chartable),
                                compute_zoom_font_size (chartable),
                                TRUE);
  gtk_image_set_from_pixmap (GTK_IMAGE (chartable->zoom_image), pixmap, NULL);
  g_object_unref (pixmap);
}

static void
make_zoom_window (GucharmapChartable *chartable)
{
  GtkWidget *widget = GTK_WIDGET (chartable);

  /* if there is already a zoom window, do nothing */
  if (chartable->zoom_window)
    return;

  chartable->zoom_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  chartable->zoom_image = gtk_image_new ();
  gtk_container_add (GTK_CONTAINER (chartable->zoom_window),
                     chartable->zoom_image);
  gtk_widget_show (chartable->zoom_image);

  gtk_window_set_resizable (GTK_WINDOW (chartable->zoom_window), FALSE);
  gtk_window_set_type_hint (GTK_WINDOW (chartable->zoom_window), 
                            GDK_WINDOW_TYPE_HINT_UTILITY);
  gtk_window_set_decorated (GTK_WINDOW (chartable->zoom_window), FALSE);

  gtk_window_set_screen (GTK_WINDOW (chartable->zoom_window),
                         gtk_widget_get_screen (widget));
}

static void
destroy_zoom_window (GucharmapChartable *chartable)
{
  if (chartable->zoom_window)
    {
      GtkWidget *widget = GTK_WIDGET (chartable);
      GtkWidget *zoom_window;

      zoom_window = chartable->zoom_window;
      chartable->zoom_window = NULL;
      chartable->zoom_image = NULL;

      gdk_window_set_cursor (widget->window, NULL);
      gtk_widget_destroy (zoom_window);
    }
}

#ifdef ENABLE_ACCESSIBLE

static GType
chartable_accessible_factory_get_accessible_type (void)
{
  return chartable_accessible_get_type ();
}

static void
chartable_accessible_factory_class_init (AtkObjectFactoryClass *klass)
{
#ifdef ENABLE_ACCESSIBLE
  klass->create_accessible = chartable_accessible_factory_create_accessible;
#endif
  klass->get_accessible_type = chartable_accessible_factory_get_accessible_type;

  _gucharmap_intl_ensure_initialized ();
}

static GType
chartable_accessible_factory_get_type (void)
{
  static GType t = 0;

  if (!t)
    {
      static const GTypeInfo tinfo =
      {
        sizeof (AtkObjectFactoryClass),
        NULL,
        NULL,
        (GClassInitFunc) chartable_accessible_factory_class_init,
        NULL,
        NULL,
        sizeof (AtkObjectClass),
        0,
        NULL,
        NULL
      };
      t = g_type_register_static (ATK_TYPE_OBJECT_FACTORY,
                                  "ChartableAccessibleFactory",
                                  &tinfo, 0);
    }
  return t;
}

#endif

static guint
get_last_cell (GucharmapChartable *chartable)
{
  return gucharmap_codepoint_list_get_last_index (chartable->codepoint_list);
}

static void
set_active_cell (GucharmapChartable *chartable,
                 guint           cell)
{
  chartable->old_active_cell = chartable->active_cell;
  chartable->old_page_first_cell = chartable->page_first_cell;

  chartable->active_cell = cell;

  /* update page, if necessary */
  if ((gint)cell - chartable->page_first_cell >= chartable->rows * chartable->cols || (gint)cell < chartable->page_first_cell)
    {
      /* move the page_first_cell as far as active_cell has moved */
      gint offset = (gint) chartable->active_cell - (gint) chartable->old_active_cell;
    
      if ((gint) chartable->old_page_first_cell + offset < 0)
        chartable->page_first_cell = 0;
      else if ((gint) chartable->old_page_first_cell + offset > get_last_cell (chartable) - (get_last_cell (chartable) % chartable->cols) - chartable->cols * (chartable->rows - 1))
        chartable->page_first_cell = get_last_cell (chartable) - (get_last_cell (chartable) % chartable->cols) - chartable->cols * (chartable->rows - 1);
      else
        chartable->page_first_cell = chartable->old_page_first_cell + offset;
    
      /* round down so that it's a multiple of chartable->cols */
      chartable->page_first_cell -= (chartable->page_first_cell % chartable->cols);
    
      /* go back up if we should have rounded up */
      if (chartable->active_cell - chartable->page_first_cell >= chartable->rows * chartable->cols)
        chartable->page_first_cell += chartable->cols;
    }

  g_signal_emit (chartable, gucharmap_chartable_signals[SET_ACTIVE_CHAR], 0, 
                 gucharmap_codepoint_list_get_char (chartable->codepoint_list, chartable->active_cell));
}

static void
set_active_char (GucharmapChartable *chartable,
                 gunichar        wc)
{
  guint cell = gucharmap_codepoint_list_get_index (chartable->codepoint_list, wc);
  set_active_cell (chartable, cell);
}

static void
draw_borders (GucharmapChartable *chartable)
{
  GtkWidget *widget = GTK_WIDGET (chartable);
  gint x, y, col, row;

  /* dark_gc[GTK_STATE_NORMAL] seems to be what is used to draw the borders
   * around widgets, so we use it for the lines */

  /* vertical lines */
  gdk_draw_line (chartable->pixmap,
                 widget->style->dark_gc[GTK_STATE_NORMAL],
                 0, 0, 0, widget->allocation.height - 1);
  for (col = 0, x = 0;  col < chartable->cols;  col++)
    {
      x += _gucharmap_chartable_column_width (chartable, col);
      gdk_draw_line (chartable->pixmap,
                     widget->style->dark_gc[GTK_STATE_NORMAL],
                     x, 0, x, widget->allocation.height - 1);
    }

  /* horizontal lines */
  gdk_draw_line (chartable->pixmap,
                 widget->style->dark_gc[GTK_STATE_NORMAL],
                 0, 0, widget->allocation.width - 1, 0);
  for (row = 0, y = 0;  row < chartable->rows;  row++)
    {
      y += _gucharmap_chartable_row_height (chartable, row);
      gdk_draw_line (chartable->pixmap,
                     widget->style->dark_gc[GTK_STATE_NORMAL],
                     0, y, widget->allocation.width - 1, y);
    }
}

static void
set_scrollbar_adjustment (GucharmapChartable *chartable)
{
  /* block our "value_changed" handler */
  g_signal_handler_block (G_OBJECT (chartable->vadjustment),
                          chartable->vadjustment_changed_handler_id);

  gtk_adjustment_set_value (chartable->vadjustment, 1.0 * chartable->page_first_cell / chartable->cols);

  g_signal_handler_unblock (G_OBJECT (chartable->vadjustment),
                            chartable->vadjustment_changed_handler_id);
}

/* for mouse clicks */
static gunichar
get_cell_at_xy (GucharmapChartable *chartable, 
                gint            x, 
                gint            y)
{
  gint r, c, x0, y0;
  guint cell;

  for (c = 0, x0 = 0;  x0 <= x && c < chartable->cols;  c++)
    x0 += _gucharmap_chartable_column_width (chartable, c);

  for (r = 0, y0 = 0;  y0 <= y && r < chartable->rows;  r++)
    y0 += _gucharmap_chartable_row_height (chartable, r);

  /* cell = rowcol_to_unichar (chartable, r-1, c-1); */
  cell = get_cell_at_rowcol (chartable, r-1, c-1);

  /* XXX: check this somewhere else? */
  if (cell > get_last_cell (chartable))
    return get_last_cell (chartable);

  return cell;
}

static void
draw_character (GucharmapChartable *chartable, 
                gint            row, 
                gint            col)
{
  GtkWidget *widget = GTK_WIDGET (chartable);
  gint padding_x, padding_y;
  gint char_width, char_height;
  gint square_width, square_height; 
  gunichar wc;
  guint cell;
  GdkGC *gc;
  gchar buf[10];
  gint n;

  cell = get_cell_at_rowcol (chartable, row, col);
  wc = gucharmap_codepoint_list_get_char (chartable->codepoint_list, cell);

  if (wc > UNICHAR_MAX || !gucharmap_unichar_validate (wc) || !gucharmap_unichar_isdefined (wc))
    return;

  if (GTK_WIDGET_HAS_FOCUS (widget) && (gint)cell == chartable->active_cell)
    gc = widget->style->text_gc[GTK_STATE_SELECTED];
  else if ((gint)cell == chartable->active_cell)
    gc = widget->style->text_gc[GTK_STATE_ACTIVE];
  else
    gc = widget->style->text_gc[GTK_STATE_NORMAL];

  square_width = _gucharmap_chartable_column_width (chartable, col) - 1;
  square_height = _gucharmap_chartable_row_height (chartable, row) - 1;

  n = gucharmap_unichar_to_printable_utf8 (wc, buf); 
  pango_layout_set_text (chartable->pango_layout, buf, n);

  pango_layout_get_pixel_size (chartable->pango_layout, &char_width, &char_height);

  /* (square_width - char_width)/2 is the smaller half */
  padding_x = (square_width - char_width) - (square_width - char_width)/2;
  padding_y = (square_height - char_height) - (square_height - char_height)/2;

  gdk_draw_layout (chartable->pixmap, gc,
                   _gucharmap_chartable_x_offset (chartable, col) + padding_x,
                   _gucharmap_chartable_y_offset (chartable, row) + padding_y,
                   chartable->pango_layout);
}

static void
draw_square_bg (GucharmapChartable *chartable, gint row, gint col)
{
  GtkWidget *widget = GTK_WIDGET (chartable);
  gint square_width, square_height; 
  GdkGC *gc;
  GdkColor untinted;
  guint cell;
  gunichar wc;

  cell = get_cell_at_rowcol (chartable, row, col);
  wc = gucharmap_codepoint_list_get_char (chartable->codepoint_list, cell);

  gc = gdk_gc_new (GDK_DRAWABLE (widget->window));

  if (GTK_WIDGET_HAS_FOCUS (widget) && (gint)cell == chartable->active_cell)
    untinted = widget->style->base[GTK_STATE_SELECTED];
  else if ((gint)cell == chartable->active_cell)
    untinted = widget->style->base[GTK_STATE_ACTIVE];
  else if ((gint)cell > get_last_cell (chartable))
    untinted = widget->style->dark[GTK_STATE_NORMAL];
  else if (! gucharmap_unichar_validate (wc))
    untinted = widget->style->fg[GTK_STATE_INSENSITIVE];
  else if (! gucharmap_unichar_isdefined (wc))
    untinted = widget->style->bg[GTK_STATE_INSENSITIVE];
  else
    untinted = widget->style->base[GTK_STATE_NORMAL];

  gdk_gc_set_rgb_fg_color (gc, &untinted);

  square_width = _gucharmap_chartable_column_width (chartable, col) - 1;
  square_height = _gucharmap_chartable_row_height (chartable, row) - 1;

  gdk_draw_rectangle (chartable->pixmap, gc, TRUE, 
                      _gucharmap_chartable_x_offset (chartable, col), 
		      _gucharmap_chartable_y_offset (chartable, row),
                      square_width, square_height);

  g_object_unref (gc);
}

static void
expose_square (GucharmapChartable *chartable, gint row, gint col)
{
  GtkWidget *widget = GTK_WIDGET (chartable);
  gtk_widget_queue_draw_area (widget,
                              _gucharmap_chartable_x_offset (chartable, col),
                              _gucharmap_chartable_y_offset (chartable, row),
                              _gucharmap_chartable_column_width (chartable, col) - 1,
                              _gucharmap_chartable_row_height (chartable, row) - 1);
}

static void
draw_square (GucharmapChartable *chartable, gint row, gint col)
{
  draw_square_bg (chartable, row, col);
  draw_character (chartable, row, col);
}

static void
draw_and_expose_cell (GucharmapChartable *chartable,
                      guint cell)
{
  gint row = (cell - chartable->page_first_cell) / chartable->cols;
  gint col = _gucharmap_chartable_cell_column (chartable, cell);

  if (row >= 0 && row < chartable->rows && col >= 0 && col < chartable->cols)
    {
      draw_square (chartable, row, col);
      expose_square (chartable, row, col);
    }
}

/* draws the backing store pixmap */
static void
draw_chartable_from_scratch (GucharmapChartable *chartable)
{
  GtkWidget *widget = GTK_WIDGET (chartable);
  gint row, col;

  /* drawing area may not be exposed yet when restoring last char setting
   */
  if (!GTK_WIDGET_REALIZED (chartable))
    return;

  if (chartable->pixmap == NULL)
    chartable->pixmap = gdk_pixmap_new (
	    widget->window, 
	    widget->allocation.width,
	    widget->allocation.height, -1);

  draw_borders (chartable);

  /* draw the characters */
  for (row = 0;  row < chartable->rows;  row++)
    for (col = 0;  col < chartable->cols;  col++)
      {
        draw_square_bg (chartable, row, col);
        draw_character (chartable, row, col);
      }
}

static void
copy_rows (GucharmapChartable *chartable, gint row_offset)
{
  GtkWidget *widget = GTK_WIDGET (chartable);
  gint num_padded_rows;
  gint from_row, to_row;

  num_padded_rows = widget->allocation.height -
                    (minimal_row_height (chartable) * chartable->rows + 1);

  if (ABS (row_offset) < chartable->rows - num_padded_rows)
    {
      gint num_rows, height;

      if (row_offset > 0)
        {
          from_row = row_offset;
          to_row = 0;
          num_rows = chartable->rows - num_padded_rows - from_row;
        }
      else
        {
          from_row = 0;
          to_row = -row_offset;
          num_rows = chartable->rows - num_padded_rows - to_row;
        }

      height = _gucharmap_chartable_y_offset (chartable, num_rows) 
               - _gucharmap_chartable_y_offset (chartable, 0) - 1;

      gdk_draw_drawable (
              chartable->pixmap,
              widget->style->base_gc[GTK_STATE_NORMAL],
              chartable->pixmap, 
              0, _gucharmap_chartable_y_offset (chartable, from_row), 
              0, _gucharmap_chartable_y_offset (chartable, to_row),
              widget->allocation.width, height);
    }

  if (ABS (row_offset) < num_padded_rows)
    {
      /* don't need num_rows or height, cuz we can go off the end */
      if (row_offset > 0)
        {
          from_row = chartable->rows - num_padded_rows + row_offset;
          to_row = chartable->rows - num_padded_rows;
        }
      else
        {
          from_row = chartable->rows - num_padded_rows;
          to_row = chartable->rows - num_padded_rows - row_offset;
        }

      /* it's ok to go off the end (so use allocation.height) */
      gdk_draw_drawable (
              chartable->pixmap,
              widget->style->base_gc[GTK_STATE_NORMAL],
              chartable->pixmap, 
              0, _gucharmap_chartable_y_offset (chartable, from_row), 
              0, _gucharmap_chartable_y_offset (chartable, to_row),
              widget->allocation.width, 
              widget->allocation.height);
    }
}

static void
redraw_rows (GucharmapChartable *chartable, gint row_offset)
{
  gint row, col, start_row, end_row;

  if (row_offset > 0) 
    {
      start_row = chartable->rows - row_offset;
      end_row = chartable->rows - 1;
    }
  else
    {
      start_row = 0;
      end_row = -row_offset - 1;
    }

  for (row = 0;  row <= chartable->rows;  row++)
    {
      gboolean draw_row = FALSE;

      draw_row = draw_row || (row >= start_row && row <= end_row);

      if (row + row_offset >= 0 && row + row_offset <= chartable->rows)
        {
          draw_row = draw_row || (_gucharmap_chartable_row_height (chartable, row) 
                                  != _gucharmap_chartable_row_height (chartable,  
                                                         row + row_offset));
        }

      if (draw_row)
        {
          for (col = 0;  col < chartable->cols;  col++)
            draw_square (chartable, row, col);
        }
    }
}

static void
get_root_coords_at_active_char (GucharmapChartable *chartable, 
                                gint *x_root, 
                                gint *y_root)
{
  GtkWidget *widget = GTK_WIDGET (chartable);
  gint x, y;
  gint row, col;

  gdk_window_get_origin (widget->window, &x, &y);

  row = (chartable->active_cell - chartable->page_first_cell) / chartable->cols;
  col = _gucharmap_chartable_cell_column (chartable, chartable->active_cell);

  *x_root = x + _gucharmap_chartable_x_offset (chartable, col);
  *y_root = y + _gucharmap_chartable_y_offset (chartable, row);
}

/* retunrs the coords of the innermost corner of the square */
static void
get_appropriate_active_char_corner_xy (GucharmapChartable *chartable, gint *x, gint *y)
{
  gint x0, y0;
  gint row, col;

  get_root_coords_at_active_char (chartable, &x0, &y0);

  row = (chartable->active_cell - chartable->page_first_cell) / chartable->cols;
  col = _gucharmap_chartable_cell_column (chartable, chartable->active_cell);

  *x = x0;
  *y = y0;

  if (row < chartable->rows / 2)
    *y += _gucharmap_chartable_row_height (chartable, row);

  if (col < chartable->cols / 2)
    *x += _gucharmap_chartable_column_width (chartable, col);
}

/* Redraws whatever needs to be redrawn, in the character table and
 * everything, and exposes what needs to be exposed. */
void
_gucharmap_chartable_redraw (GucharmapChartable *chartable, 
                         gboolean        move_zoom)
{
  GtkWidget *widget = GTK_WIDGET (chartable);
  gint row_offset;
  gboolean actives_done = FALSE;

  row_offset = ((gint) chartable->page_first_cell - (gint) chartable->old_page_first_cell) / chartable->cols;

#ifdef G_OS_WIN32

  if (row_offset != 0)
    {
      /* get around the bug in gdkdrawable-win32.c */
      /* yup, this makes it really slow */
      draw_chartable_from_scratch (chartable);
      gtk_widget_queue_draw (widget);
      actives_done = TRUE;
    }

#else /* #ifdef G_OS_WIN32 */

  if (chartable->codepoint_list_changed 
          || row_offset >= chartable->rows 
          || row_offset <= -chartable->rows)
    {
      draw_chartable_from_scratch (chartable);
      gtk_widget_queue_draw (widget);
      actives_done = TRUE;
      chartable->codepoint_list_changed = FALSE;
    }
  else if (row_offset != 0)
    {
      copy_rows (chartable, row_offset);
      redraw_rows (chartable, row_offset);
      draw_borders (chartable);
      gtk_widget_queue_draw (widget);
    }

#endif

  if (chartable->active_cell != chartable->old_active_cell)
    {
      set_scrollbar_adjustment (chartable); /* XXX */

      if (!actives_done)
        {
          draw_and_expose_cell (chartable, chartable->old_active_cell);
          draw_and_expose_cell (chartable, chartable->active_cell);
        }

      if (chartable->zoom_window)
        update_zoom_window (chartable);

      if (move_zoom && chartable->zoom_window)
        {
          gint x, y;

          get_appropriate_active_char_corner_xy (chartable, &x, &y);
          place_zoom_window (chartable, x, y);
        }
    }

  chartable->old_page_first_cell = chartable->page_first_cell;
  chartable->old_active_cell = chartable->active_cell;
}

static void
update_scrollbar_adjustment (GucharmapChartable *chartable)
{
  chartable->vadjustment->value = 1.0 * chartable->page_first_cell / chartable->cols;
  chartable->vadjustment->lower = 0.0;
  chartable->vadjustment->upper = 1.0 * ( get_last_cell (chartable) / chartable->cols + 1 );
  chartable->vadjustment->step_increment = 3.0;
  chartable->vadjustment->page_increment = 1.0 * chartable->rows;
  chartable->vadjustment->page_size = chartable->rows;

  gtk_adjustment_changed (chartable->vadjustment);
}

static void
vadjustment_value_changed_cb (GtkAdjustment *adjustment, GucharmapChartable *chartable)
{
  set_top_row (chartable, (gint) gtk_adjustment_get_value (adjustment));
  _gucharmap_chartable_redraw (chartable, TRUE);
}

/* redraws the screen from the backing pixmap */
static gint
expose_event (GtkWidget *widget, 
              GdkEventExpose *event, 
              GucharmapChartable *chartable)
{
  gdk_window_set_back_pixmap (widget->window, NULL, FALSE);

  if (chartable->pixmap == NULL)
    {
      draw_chartable_from_scratch (chartable);

      /* the zoom window may need to be redrawn and repositioned */
      if (chartable->zoom_window)
        {
          gint x, y;

          update_zoom_window (chartable);
          get_appropriate_active_char_corner_xy (chartable, &x, &y);
          place_zoom_window (chartable, x, y);
        }
    }

  gdk_draw_drawable (widget->window,
                     widget->style->fg_gc[GTK_STATE_NORMAL],
                     chartable->pixmap,
                     event->area.x, event->area.y,
                     event->area.x, event->area.y,
                     event->area.width, event->area.height);

  return FALSE;
}

static void
gucharmap_chartable_size_allocate (GtkWidget *widget,
                               GtkAllocation *allocation)
{
  GucharmapChartable *chartable = GUCHARMAP_CHARTABLE (widget);
  gint old_rows, old_cols;

  GTK_WIDGET_CLASS (gucharmap_chartable_parent_class)->size_allocate (widget, allocation);

  old_rows = chartable->rows;
  old_cols = chartable->cols;

  if (chartable->snap_pow2_enabled)
    chartable->cols = 1 << g_bit_nth_msf ((allocation->width - 1) / bare_minimal_column_width (chartable), -1);
  else
    chartable->cols = (allocation->width - 1) / bare_minimal_column_width (chartable);

  chartable->rows = (allocation->height - 1) / bare_minimal_row_height (chartable);

  /* avoid a horrible floating point exception crash */
  if (chartable->rows < 1)
    chartable->rows = 1;
  if (chartable->cols < 1)
    chartable->cols = 1;

  /* force pixmap to be redrawn on next expose event */
  if (chartable->pixmap != NULL)
    g_object_unref (chartable->pixmap);
  chartable->pixmap = NULL;

  if (chartable->rows == old_rows && chartable->cols == old_cols)
    return;

  chartable->page_first_cell = chartable->active_cell - (chartable->active_cell % chartable->cols);

  update_scrollbar_adjustment (chartable);
}

static void
gucharmap_chartable_set_adjustments (GucharmapChartable *chartable,
                                     GtkAdjustment *hadjustment,
                                     GtkAdjustment *vadjustment)
{
  if (chartable->vadjustment)
    {
      g_signal_handler_disconnect (chartable->vadjustment,
                                   chartable->vadjustment_changed_handler_id);
      chartable->vadjustment_changed_handler_id = 0;
      g_object_unref (chartable->vadjustment);
      chartable->vadjustment = NULL;
    }

  if (vadjustment)
    {
      chartable->vadjustment = g_object_ref (vadjustment);
      chartable->vadjustment_changed_handler_id =
          g_signal_connect (vadjustment, "value-changed",
                            G_CALLBACK (vadjustment_value_changed_cb),
                            chartable);
    }
  
  // FIXMEchpe what about the hadjustment ?

  // and remember to update the accessible too!!!
  // see gtkiconview.c for example
}

static void
gucharmap_chartable_finalize (GObject *object)
{
  GucharmapChartable *chartable = GUCHARMAP_CHARTABLE (object);

  g_free (chartable->font_name);

  if (chartable->pango_layout)
    g_object_unref (chartable->pango_layout);

  gtk_target_list_unref (chartable->target_list);

  G_OBJECT_CLASS (gucharmap_chartable_parent_class)->finalize (object);
}

static void
gucharmap_chartable_class_init (GucharmapChartableClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gucharmap_chartable_finalize;

  widget_class->size_allocate = gucharmap_chartable_size_allocate;
  
  klass->set_scroll_adjustments = gucharmap_chartable_set_adjustments;
  klass->activate = NULL;
  klass->set_active_char = NULL;

  widget_class->set_scroll_adjustments_signal =
    g_signal_new ("set-scroll-adjustments",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GucharmapChartableClass, set_scroll_adjustments),
                  NULL, NULL, 
                  _gucharmap_marshal_VOID__OBJECT_OBJECT,
                  G_TYPE_NONE, 2,
                  GTK_TYPE_ADJUSTMENT, GTK_TYPE_ADJUSTMENT);

  gucharmap_chartable_signals[ACTIVATE] =
      g_signal_new ("activate", gucharmap_chartable_get_type (), G_SIGNAL_RUN_FIRST,
                    G_STRUCT_OFFSET (GucharmapChartableClass, activate),
                    NULL, NULL, g_cclosure_marshal_VOID__UINT, G_TYPE_NONE, 
		    1, G_TYPE_UINT);

  gucharmap_chartable_signals[SET_ACTIVE_CHAR] =
      g_signal_new ("set-active-char", gucharmap_chartable_get_type (),
                    G_SIGNAL_RUN_FIRST,
                    G_STRUCT_OFFSET (GucharmapChartableClass, set_active_char),
                    NULL, NULL, g_cclosure_marshal_VOID__UINT, G_TYPE_NONE,
		    1, G_TYPE_UINT);

  gucharmap_chartable_signals[STATUS_MESSAGE] =
      g_signal_new ("status-message", gucharmap_chartable_get_type (), G_SIGNAL_RUN_FIRST,
                    G_STRUCT_OFFSET (GucharmapChartableClass, status_message),
                    NULL, NULL, g_cclosure_marshal_VOID__STRING, G_TYPE_NONE,
		    1, G_TYPE_STRING);
}

static void
move_home (GucharmapChartable *chartable)
{
  set_active_cell (chartable, 0);
}

static void
move_end (GucharmapChartable *chartable)
{
  set_active_cell (chartable, get_last_cell (chartable));
}

static void
move_up (GucharmapChartable *chartable)
{
  if (chartable->active_cell >= chartable->cols)
    set_active_cell (chartable, chartable->active_cell - chartable->cols);
}

static void
move_down (GucharmapChartable *chartable)
{
  if (chartable->active_cell <= get_last_cell (chartable) - chartable->cols)
    set_active_cell (chartable, chartable->active_cell + chartable->cols);
}

static void
move_cursor (GucharmapChartable *chartable, 
             gint            offset)
{
  if (chartable->active_cell + offset >= 0 && chartable->active_cell + offset <= get_last_cell (chartable))
    set_active_cell (chartable, chartable->active_cell + offset);
}

static void
move_left (GucharmapChartable *chartable)
{
  GtkWidget *widget = GTK_WIDGET (chartable);
  if (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL)
    move_cursor (chartable, 1);
  else
    move_cursor (chartable, -1);
}

static void
move_right (GucharmapChartable *chartable)
{
  GtkWidget *widget = GTK_WIDGET (chartable);
  if (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL)
    move_cursor (chartable, -1);
  else
    move_cursor (chartable, 1);
}

static void
move_page_up (GucharmapChartable *chartable)
{
  if (chartable->active_cell >= chartable->cols * chartable->rows)
    set_active_cell (chartable, chartable->active_cell - chartable->cols * chartable->rows);
  else if (chartable->active_cell > 0)
    set_active_cell (chartable, 0);
}

static void
move_page_down (GucharmapChartable *chartable)
{
  if (chartable->active_cell < get_last_cell (chartable) - chartable->cols * chartable->rows)
    set_active_cell (chartable, chartable->active_cell + chartable->cols * chartable->rows);
  else if (chartable->active_cell < get_last_cell (chartable))
    set_active_cell (chartable, get_last_cell (chartable));
}

static gint
key_release_event (GtkWidget *widget,
                   GdkEventKey *event,
                   GucharmapChartable *chartable)
{
  switch (event->keyval)
    {
      /* XXX: If the group(shift_toggle) Xkb option is set, then releasing
       * the shift key gives either ISO_Next_Group or ISO_Prev_Group. Is
       * there a better way to handle this case? */
      case GDK_Shift_L: case GDK_Shift_R:
      case GDK_ISO_Next_Group: case GDK_ISO_Prev_Group:
        gucharmap_chartable_zoom_disable (chartable);
        break;

    }

  return FALSE;
}

/* mostly for moving around in the chartable */
static gint
key_press_event (GtkWidget *widget, 
                 GdkEventKey *event, 
                 GucharmapChartable *chartable)
{
  if (event->state & (GDK_MOD1_MASK | GDK_CONTROL_MASK))
    return FALSE;

  /* move the cursor or whatever depending on which key was pressed */
  switch (event->keyval)
    {
      case GDK_Home: case GDK_KP_Home:
        move_home (chartable);
        break;

      case GDK_End: case GDK_KP_End:
        move_end (chartable);
        break;

      case GDK_Up: case GDK_KP_Up: 
      case GDK_k: case GDK_K:
        move_up (chartable);
        break;

      case GDK_Down: case GDK_KP_Down: 
      case GDK_j: case GDK_J:
        move_down (chartable);
        break;

      case GDK_Left: case GDK_KP_Left: 
      case GDK_h: case GDK_H:
        move_left (chartable);
        break;

      case GDK_Right: case GDK_KP_Right: 
      case GDK_l: case GDK_L:
        move_right (chartable);
        break;

      case GDK_Page_Up: case GDK_KP_Page_Up: 
      case GDK_b: case GDK_B:
        move_page_up (chartable);
        break;

      case GDK_Page_Down: case GDK_KP_Page_Down:
        move_page_down (chartable);
        break;

      case GDK_Shift_L: case GDK_Shift_R:
        gucharmap_chartable_zoom_enable (chartable);
        return FALSE;

      case GDK_Return: case GDK_KP_Enter: case GDK_space:
	g_signal_emit (chartable, gucharmap_chartable_signals[ACTIVATE], 0, gucharmap_chartable_get_active_character (chartable));
        return TRUE;

      /* pass on other keys, like tab and stuff that shifts focus */
      default:
        return FALSE;
    }

  _gucharmap_chartable_redraw (chartable, TRUE);

  return TRUE;
}

static void
set_top_row (GucharmapChartable *chartable, 
             gint            row)
{
  gint r, c;

  g_return_if_fail (row >= 0 && row <= get_last_cell (chartable) / chartable->cols);

  chartable->old_page_first_cell = chartable->page_first_cell;
  chartable->old_active_cell = chartable->active_cell;

  chartable->page_first_cell = row * chartable->cols;

  /* character is still on the visible page */
  if (chartable->active_cell - chartable->page_first_cell >= 0
      && chartable->active_cell - chartable->page_first_cell < chartable->rows * chartable->cols)
    return;

  c = chartable->old_active_cell % chartable->cols;

  if (chartable->page_first_cell < chartable->old_page_first_cell)
    r = chartable->rows - 1;
  else
    r = 0;

  chartable->active_cell = chartable->page_first_cell + r * chartable->cols + c;
  if (chartable->active_cell > get_last_cell (chartable))
    chartable->active_cell = get_last_cell (chartable);

  g_signal_emit (chartable, gucharmap_chartable_signals[SET_ACTIVE_CHAR], 0, 
                 gucharmap_chartable_get_active_character (chartable));
}

static void
status_message (GucharmapChartable *chartable, const gchar *message)
{
  g_signal_emit (chartable, gucharmap_chartable_signals[STATUS_MESSAGE], 0, message);
}

/*  - single click with left button: activate character under pointer
 *  - double-click with left button: add active character to text_to_copy
 *  - single-click with middle button: jump to selection_primary
 */
static gint
button_press_event (GtkWidget *widget, 
                    GdkEventButton *event, 
                    GucharmapChartable *chartable)
{
  /* in case we lost keyboard focus and are clicking to get it back */
  gtk_widget_grab_focus (widget);

  if (event->button == 1)
    {
      chartable->click_x = event->x;
      chartable->click_y = event->y;
    }

  /* double-click */
  if (event->button == 1 && event->type == GDK_2BUTTON_PRESS)
    {
      g_signal_emit (chartable, gucharmap_chartable_signals[ACTIVATE], 0, gucharmap_chartable_get_active_character (chartable));
    }
  /* single-click */ 
  else if (event->button == 1 && event->type == GDK_BUTTON_PRESS) 
    {
      set_active_cell (chartable, get_cell_at_xy (chartable, event->x, event->y));
      _gucharmap_chartable_redraw (chartable, TRUE);
    }
  else if (event->button == 3)
    {
      set_active_cell (chartable, get_cell_at_xy (chartable, event->x, event->y));

      make_zoom_window (chartable);
      _gucharmap_chartable_redraw (chartable, FALSE);

      if (chartable->active_cell == chartable->old_active_cell)
        update_zoom_window (chartable); 

      place_zoom_window (chartable, event->x_root, event->y_root);
      gtk_widget_show (chartable->zoom_window);
    }

  /* XXX: [need to return false so it gets drag events] */
  /* actually return true because we handle drag_begin because of
   * http://bugzilla.gnome.org/show_bug.cgi?id=114534 */
  return TRUE;
}

static gint
button_release_event (GtkWidget *widget, 
                      GdkEventButton *event, 
                      GucharmapChartable *chartable)
{
  if (!chartable->zoom_mode_enabled && event->button == 3)
    destroy_zoom_window (chartable);

  return FALSE;
}

static gint
motion_notify_event (GtkWidget *widget, 
                     GdkEventMotion *event, 
                     GucharmapChartable *chartable)
{
  if ((event->state & GDK_BUTTON1_MASK) != 0
      && gtk_drag_check_threshold (widget, 
                                   chartable->click_x, 
                                   chartable->click_y, 
                                   event->x, 
                                   event->y))
    {
      gtk_drag_begin (widget, chartable->target_list,
                      GDK_ACTION_COPY, 1, (GdkEvent *) event);
    }

  if ((event->state & GDK_BUTTON3_MASK) != 0 && chartable->zoom_window)
    {
      guint cell = get_cell_at_xy (chartable, MAX (0, event->x), MAX (0, event->y));

      if ((gint)cell != chartable->active_cell)
        {
          gtk_widget_hide (chartable->zoom_window);
          set_active_cell (chartable, cell);
          _gucharmap_chartable_redraw (chartable, FALSE);
        }

      place_zoom_window (chartable, event->x_root, event->y_root);
      gtk_widget_show (chartable->zoom_window);
    }

  return FALSE;
}

static gboolean
focus_out_event (GtkWidget *widget, 
                       GdkEventFocus *event,
                       GucharmapChartable *chartable)
{
  gucharmap_chartable_zoom_disable (chartable);

  if (chartable->pixmap != NULL)
    draw_and_expose_cell (chartable, chartable->active_cell);

  return FALSE;
}

static gboolean
focus_in_event (GtkWidget *widget, 
                GdkEventFocus *event,
                GucharmapChartable *chartable)
{
  if (chartable->pixmap != NULL)
    draw_and_expose_cell (chartable, chartable->active_cell);

  return FALSE;
}

static void
mouse_wheel_up (GucharmapChartable *chartable)
{
  if (chartable->page_first_cell > chartable->rows * chartable->cols / 2)
    set_top_row (chartable, (chartable->page_first_cell - chartable->rows * chartable->cols / 2) / chartable->cols);
  else 
    set_top_row (chartable, 0);

  _gucharmap_chartable_redraw (chartable, TRUE);
}

static void
mouse_wheel_down (GucharmapChartable *chartable)
{
  if ((gint) get_last_cell (chartable) - chartable->rows * chartable->cols < 0)
    {
      set_top_row (chartable, 0);
    }
  else if (chartable->page_first_cell + chartable->rows * chartable->cols / 2 < get_last_cell (chartable) - chartable->rows * chartable->cols)
    {
      set_top_row (chartable, (chartable->page_first_cell + chartable->rows * chartable->cols / 2) / chartable->cols);
    }
  else 
    {
      set_top_row (chartable, get_last_cell (chartable) / chartable->cols - (chartable->rows - 1) );
    }

  _gucharmap_chartable_redraw (chartable, TRUE);
}

/* mouse wheel scrolls by half a page */
static gboolean    
mouse_wheel_event (GtkWidget *widget, 
                   GdkEventScroll *event, 
                   GucharmapChartable *chartable)
{
  switch (event->direction)
    {
      case GDK_SCROLL_UP:
        mouse_wheel_up (chartable);
        break;

      case GDK_SCROLL_DOWN:
        mouse_wheel_down (chartable);
        break;

      default:
        break;
    }

  return TRUE;
}

static void
style_set (GtkWidget *widget, 
           GtkStyle *previous_style, 
           GucharmapChartable *chartable)
{
  if (chartable->pixmap != NULL)
    g_object_unref (chartable->pixmap);
  chartable->pixmap = NULL;

  gtk_widget_queue_draw (widget);
}

static void
drag_data_received (GtkWidget *widget,
                    GdkDragContext *context,
                    gint x, gint y,
                    GtkSelectionData *selection_data,
                    guint info,
                    guint time,
                    GucharmapChartable *chartable)
{
  gchar *text;
  gunichar wc;

  if (selection_data->length <= 0 || selection_data->data == NULL)
    return;

  text = (gchar *) gtk_selection_data_get_text (selection_data);

  if (text == NULL) /* XXX: say something in the statusbar? */
    return;

  wc = g_utf8_get_char_validated (text, -1);

  if (wc == (gunichar)(-2) || wc == (gunichar)(-1) || wc > UNICHAR_MAX)
    status_message (chartable, _("Unknown character, unable to identify."));
  else if (gucharmap_codepoint_list_get_index (chartable->codepoint_list, wc) == (guint)(-1))
    status_message (chartable, _("Not found."));
  else
    {
      status_message (chartable, _("Character found."));
      set_active_char (chartable, wc);
      _gucharmap_chartable_redraw (chartable, TRUE);
    }

  g_free (text);
}

static gint
compute_drag_font_size (GucharmapChartable *chartable)
{
  gint font_size;

  font_size = pango_font_description_get_size (
          gtk_widget_get_style (GTK_WIDGET (chartable))->font_desc);

  return 5 * ((font_size > 0) ? font_size : 10 * PANGO_SCALE);
}

static void
drag_begin (GtkWidget *widget, 
            GdkDragContext *context,
            GucharmapChartable *chartable)
{
  GdkPixmap *drag_icon;

  drag_icon = create_glyph_pixmap (chartable,
                                   gucharmap_chartable_get_active_character (chartable),
                                   compute_drag_font_size (chartable),
                                   FALSE);
  gtk_drag_set_icon_pixmap (context, gtk_widget_get_colormap (widget), 
                            drag_icon, NULL, -8, -8);
  g_object_unref (drag_icon);
}

static void
drag_data_get (GtkWidget *widget, 
               GdkDragContext *context,
               GtkSelectionData *selection_data,
               guint info,
               guint time,
               GucharmapChartable *chartable)

{
  gchar buf[7];
  gint n;

  n = g_unichar_to_utf8 (gucharmap_codepoint_list_get_char (chartable->codepoint_list, chartable->active_cell), buf);
  gtk_selection_data_set_text (selection_data, buf, n);
}

/* does all the initial construction */
static void
gucharmap_chartable_init (GucharmapChartable *chartable)
{
  GtkWidget *widget = GTK_WIDGET (chartable);
#ifdef ENABLE_ACCESSIBLE
  AtkObject *accessible;
#endif

  chartable->zoom_mode_enabled = FALSE;
  chartable->zoom_window = NULL;
  chartable->snap_pow2_enabled = FALSE;

  chartable->codepoint_list = gucharmap_codepoint_list_new (0, UNICHAR_MAX);

#ifdef ENABLE_ACCESSIBLE
  accessible = gtk_widget_get_accessible (GTK_WIDGET (chartable));
  atk_object_set_name (accessible, _("Character Table"));
#endif

  gtk_widget_set_events (widget,
          GDK_EXPOSURE_MASK | GDK_KEY_PRESS_MASK | GDK_BUTTON_PRESS_MASK |
          GDK_BUTTON_RELEASE_MASK | GDK_BUTTON3_MOTION_MASK |
          GDK_BUTTON1_MOTION_MASK | GDK_FOCUS_CHANGE_MASK | GDK_SCROLL_MASK);

  chartable->target_list = gtk_target_list_new (NULL, 0);
  gtk_target_list_add_text_targets (chartable->target_list, 0);

  g_signal_connect (G_OBJECT (widget), "expose-event",
                    G_CALLBACK (expose_event), chartable);
  g_signal_connect (G_OBJECT (widget), "key-press-event",
                    G_CALLBACK (key_press_event), chartable);
  g_signal_connect (G_OBJECT (widget), "key-release-event",
                    G_CALLBACK (key_release_event), chartable);
  g_signal_connect (G_OBJECT (widget), "button-press-event",
                    G_CALLBACK (button_press_event), chartable);
  g_signal_connect (G_OBJECT (widget), "button-release-event",
                    G_CALLBACK (button_release_event), chartable);
  g_signal_connect (G_OBJECT (widget), "motion-notify-event",
                    G_CALLBACK (motion_notify_event), chartable);
  g_signal_connect (G_OBJECT (widget), "focus-in-event",
                    G_CALLBACK (focus_in_event), chartable);
  g_signal_connect (G_OBJECT (widget), "focus-out-event",
                    G_CALLBACK (focus_out_event), chartable);
  g_signal_connect (G_OBJECT (widget), "scroll-event",
                    G_CALLBACK (mouse_wheel_event), chartable);
  g_signal_connect (G_OBJECT (widget), "style-set",
                    G_CALLBACK (style_set), chartable);

  gtk_drag_dest_set (widget, GTK_DEST_DEFAULT_ALL,
                     NULL, 0,
                     GDK_ACTION_COPY);
  gtk_drag_dest_add_text_targets (widget);

  g_signal_connect (G_OBJECT (widget), "drag-data-received",
                    G_CALLBACK (drag_data_received), chartable);

  /* XXX: gtk_drag_source_set removed due to #114534 (do it by hand) */

  g_signal_connect (G_OBJECT (widget), "drag-begin",
                    G_CALLBACK (drag_begin), chartable);

  g_signal_connect (G_OBJECT (widget), "drag-data-get",
                    G_CALLBACK (drag_data_get), chartable);

  /* this is required to get key_press events */
  GTK_WIDGET_SET_FLAGS (widget, GTK_CAN_FOCUS);

#ifdef ENABLE_ACCESSIBLE
  if (GTK_IS_ACCESSIBLE (gtk_widget_get_accessible (widget)))
    {
      /* Accessibility support is enabled */
      atk_registry_set_factory_type (atk_get_default_registry (),
                                     GTK_TYPE_DRAWING_AREA,
                                     chartable_accessible_factory_get_type ());
    }
#endif

  gtk_widget_show_all (GTK_WIDGET (chartable));

  chartable->font_name = NULL;

//  FIXME do it on style-set
  chartable->pango_layout = gtk_widget_create_pango_layout (widget, NULL);

  pango_layout_set_font_description (chartable->pango_layout,
                                     widget->style->font_desc);

  chartable->page_first_cell = 0;
  chartable->active_cell = 0;
  chartable->rows = 1;
  chartable->cols = 1;
}

GtkWidget *
gucharmap_chartable_new (void)
{
  return GTK_WIDGET (g_object_new (gucharmap_chartable_get_type (), NULL));
}

void
gucharmap_chartable_zoom_enable (GucharmapChartable *chartable)
{
  gint x, y;

  chartable->zoom_mode_enabled = TRUE;

  make_zoom_window (chartable);
  update_zoom_window (chartable);

  get_appropriate_active_char_corner_xy (chartable, &x, &y);
  place_zoom_window (chartable, x, y);

  gtk_widget_show (chartable->zoom_window);
}

void
gucharmap_chartable_zoom_disable (GucharmapChartable *chartable)
{
  chartable->zoom_mode_enabled = FALSE;
  destroy_zoom_window (chartable);
}

void 
gucharmap_chartable_set_font (GucharmapChartable *chartable, const gchar *font_name)
{
  GtkWidget *widget = GTK_WIDGET (chartable);
  PangoFontDescription *font_desc;

  /* if it's the same as the current font, do nothing */
  if (chartable->font_name != NULL
      && g_ascii_strcasecmp (chartable->font_name, font_name) == 0)
    return;
  else
    {
      g_free (chartable->font_name);
      chartable->font_name = NULL;
      chartable->font_name = g_strdup (font_name);
    }

  font_desc = pango_font_description_from_string (chartable->font_name);

  /* FIXMEchpe remove this, just assert GTK_WIDGET_REALIZED! */
  /* ensure style so that this has an effect even before it's realized */
  gtk_widget_ensure_style (widget);
  gtk_widget_modify_font (widget, font_desc);

  /* new pango layout for the new font */
  g_object_unref (chartable->pango_layout);
  chartable->pango_layout = gtk_widget_create_pango_layout (widget, NULL);

  pango_layout_set_font_description (chartable->pango_layout,
                                     widget->style->font_desc);

  pango_font_description_free (font_desc);

  /* force pixmap to be redrawn on next expose event */
  if (chartable->pixmap != NULL)
    g_object_unref (chartable->pixmap);
  chartable->pixmap = NULL;
}

gunichar 
gucharmap_chartable_get_active_character (GucharmapChartable *chartable)
{
  if (!chartable->codepoint_list)
    return 0;

  return gucharmap_codepoint_list_get_char (chartable->codepoint_list, chartable->active_cell);
}

void
gucharmap_chartable_set_active_character (GucharmapChartable *chartable, 
                                      gunichar        wc)
{
  set_active_char (chartable, wc);
  _gucharmap_chartable_redraw (chartable, TRUE);
}

void
gucharmap_chartable_set_snap_pow2 (GucharmapChartable *chartable, gboolean snap)
{
  if (snap != chartable->snap_pow2_enabled)
    {
      GtkWidget *widget = GTK_WIDGET (chartable);
      chartable->snap_pow2_enabled = snap;

      /* sends "size-allocate" */
      gtk_widget_queue_resize (widget); 
    }
}

void
gucharmap_chartable_set_codepoint_list (GucharmapChartable         *chartable,
                                    GucharmapCodepointList *list)
{
  GtkWidget *widget = GTK_WIDGET (chartable);

  if (chartable->codepoint_list)
    g_object_unref (chartable->codepoint_list);

  chartable->codepoint_list = list;
  chartable->codepoint_list_changed = TRUE;

  chartable->active_cell = 0;
  chartable->page_first_cell = 0;

  /* force pixmap to be redrawn */
  if (chartable->pixmap != NULL)
    g_object_unref (chartable->pixmap);
  chartable->pixmap = NULL;

  if (!list)
    return;

  g_signal_emit (chartable, gucharmap_chartable_signals[SET_ACTIVE_CHAR], 0, 
                 gucharmap_chartable_get_active_character (chartable));

  update_scrollbar_adjustment (chartable);

  gtk_widget_queue_draw (widget);
}

GucharmapCodepointList *
gucharmap_chartable_get_codepoint_list (GucharmapChartable *chartable)
{
  return chartable->codepoint_list;
}

gint
gucharmap_chartable_get_active_cell (GucharmapChartable *chartable)
{
  return chartable->active_cell;
}