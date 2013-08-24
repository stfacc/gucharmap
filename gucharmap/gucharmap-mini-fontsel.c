/*
 * Copyright © 2004 Noah Levitt
 * Copyright © 2008 Christian Persch
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02110-1301  USA
 */

#include <config.h>

#include <string.h>

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include "gucharmap-mini-fontsel.h"

#define I_(string) g_intern_static_string (string)

enum
{
  MIN_FONT_SIZE = 5,
  MAX_FONT_SIZE = 400,
};

enum
{
  PROP_0,
  PROP_FONT_DESC
};

enum
{
  COL_FAMILIY
};

static void gucharmap_mini_font_selection_class_init (GucharmapMiniFontSelectionClass *klass);
static void gucharmap_mini_font_selection_init       (GucharmapMiniFontSelection *fontsel);
static void gucharmap_mini_font_selection_finalize   (GObject *object);

G_DEFINE_TYPE (GucharmapMiniFontSelection, gucharmap_mini_font_selection, GTK_TYPE_DIALOG)

static void
fill_font_families (GucharmapMiniFontSelection *fontsel)
{
  GtkTreeView *tree_view = GTK_TREE_VIEW (fontsel->family);
  PangoFontFamily **families;
  int n_families, i;

  fontsel->family_store = gtk_list_store_new (1, G_TYPE_STRING);

  pango_context_list_families (
          gtk_widget_get_pango_context (GTK_WIDGET (fontsel)),
          &families, &n_families);

  for (i = 0;  i < n_families;  i++)
    {
      PangoFontFamily *family = families[i];
      GtkTreeIter iter;

      gtk_list_store_insert_with_values (fontsel->family_store,
                                         &iter,
                                         -1,
                                         COL_FAMILIY, pango_font_family_get_name (family),
                                         -1);
    }

  g_free (families);

  /* Now turn on sorting in the combo box */
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (fontsel->family_store),
                                        COL_FAMILIY,
                                        GTK_SORT_ASCENDING);

  gtk_tree_view_set_model (tree_view, GTK_TREE_MODEL (fontsel->family_store));
  g_object_unref (fontsel->family_store);
}

static void
update_font_family_tree_view (GucharmapMiniFontSelection *fontsel)
{
  GtkTreeModel *model = GTK_TREE_MODEL (fontsel->family_store);
  GtkTreeSelection *selection = gtk_tree_view_get_selection (fontsel->family);
  GtkTreeIter iter;
  const char *font_family;
  gboolean found = FALSE;

  font_family = pango_font_description_get_family (fontsel->font_desc);
  if (!font_family || !font_family[0]) {
    gtk_tree_selection_unselect_all (selection);
    return;
  }

  if (!gtk_tree_model_get_iter_first (model, &iter))
    return;

  do {
    char *family;

    gtk_tree_model_get (model, &iter, COL_FAMILIY, &family, -1);
    found = family && strcmp (family, font_family) == 0;
    g_free (family);
  } while (!found && gtk_tree_model_iter_next (model, &iter));

  if (found) {
    GtkTreePath *path = gtk_tree_model_get_path (model, &iter);
    gtk_tree_selection_select_iter (selection, &iter);
    gtk_tree_view_scroll_to_cell (fontsel->family,
                                  path,
                                  NULL,
                                  FALSE, 0, 0);
    gtk_tree_path_free (path);
  } else {
    gtk_tree_selection_unselect_all (selection);
  }
}

static void
family_changed (GtkTreeSelection *selection,
                GucharmapMiniFontSelection *fontsel)
{
  GtkTreeIter iter;
  char *family;

  if (!gtk_tree_selection_get_selected (selection, NULL, &iter))
    return;

  gtk_tree_model_get (GTK_TREE_MODEL (fontsel->family_store),
                      &iter,
                      COL_FAMILIY, &family,
                      -1);
  if (!family)
    return;

  pango_font_description_set_family (fontsel->font_desc, family);
  g_free (family);

  g_object_notify (G_OBJECT (fontsel), "font-desc");
}

/* returns font size in points */
static int
get_font_size (GucharmapMiniFontSelection *fontsel)
{
  return PANGO_PIXELS (pango_font_description_get_size (fontsel->font_desc));
}

/* size is in points */
static void
set_font_size (GucharmapMiniFontSelection *fontsel, 
               int size)
{
  size = CLAMP (size, MIN_FONT_SIZE, MAX_FONT_SIZE);
  pango_font_description_set_size (fontsel->font_desc, PANGO_SCALE * size);

  gtk_adjustment_set_value (GTK_ADJUSTMENT (fontsel->size_adj), size);

  g_object_notify (G_OBJECT (fontsel), "font-desc");
}

static void 
font_size_changed (GtkAdjustment *adjustment,
                   GucharmapMiniFontSelection *fontsel)
{
  int new_size;

  new_size = gtk_adjustment_get_value (adjustment);
  if (new_size != get_font_size (fontsel))
    set_font_size (fontsel, new_size);
}

static void
gucharmap_mini_font_selection_finalize (GObject *object)
{
  GucharmapMiniFontSelection *fontsel = GUCHARMAP_MINI_FONT_SELECTION (object);
  pango_font_description_free (fontsel->font_desc);

  G_OBJECT_CLASS (gucharmap_mini_font_selection_parent_class)->finalize (object);
}

static void
gucharmap_mini_font_selection_set_property (GObject *object,
                                            guint prop_id,
                                            const GValue *value,
                                            GParamSpec *pspec)
{
  GucharmapMiniFontSelection *mini_fontsel = GUCHARMAP_MINI_FONT_SELECTION (object);

  switch (prop_id) {
    case PROP_FONT_DESC:
      gucharmap_mini_font_selection_set_font_desc (mini_fontsel, g_value_get_boxed (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gucharmap_mini_font_selection_get_property (GObject *object,
                                            guint prop_id,
                                            GValue *value,
                                            GParamSpec *pspec)
{
  GucharmapMiniFontSelection*mini_fontsel = GUCHARMAP_MINI_FONT_SELECTION (object);

  switch (prop_id) {
    case PROP_FONT_DESC:
      g_value_set_boxed (value, gucharmap_mini_font_selection_get_font_desc (mini_fontsel));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gucharmap_mini_font_selection_class_init (GucharmapMiniFontSelectionClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gucharmap_mini_font_selection_finalize;
  gobject_class->get_property = gucharmap_mini_font_selection_get_property;
  gobject_class->set_property = gucharmap_mini_font_selection_set_property;

  g_object_class_install_property
    (gobject_class,
     PROP_FONT_DESC,
     g_param_spec_boxed ("font-desc", NULL, NULL,
                         PANGO_TYPE_FONT_DESCRIPTION,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_NAME |
                         G_PARAM_STATIC_NICK |
                         G_PARAM_STATIC_BLURB));
}

static void
bold_toggled (GtkToggleButton *toggle,
              GucharmapMiniFontSelection *fontsel)
{
  if (gtk_toggle_button_get_active (toggle))
    pango_font_description_set_weight (fontsel->font_desc, PANGO_WEIGHT_BOLD);
  else
    pango_font_description_set_weight (fontsel->font_desc, PANGO_WEIGHT_NORMAL);

  g_object_notify (G_OBJECT (fontsel), "font-desc");
}

static void
italic_toggled (GtkToggleButton *toggle,
                GucharmapMiniFontSelection *fontsel)
{
  if (gtk_toggle_button_get_active (toggle))
    pango_font_description_set_style (fontsel->font_desc, PANGO_STYLE_ITALIC);
  else
    pango_font_description_set_style (fontsel->font_desc, PANGO_STYLE_NORMAL);

  g_object_notify (G_OBJECT (fontsel), "font-desc");
}

static void
gucharmap_mini_font_selection_init (GucharmapMiniFontSelection *fontsel)
{
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;
  GtkWidget *content, *scrolled;
  GtkStyle *style;
  AtkObject *accessib;

  gtk_widget_ensure_style (GTK_WIDGET (fontsel));
  style = gtk_widget_get_style (GTK_WIDGET (fontsel));
  fontsel->font_desc = pango_font_description_copy (style->font_desc);
  fontsel->default_size = -1;

  fontsel->size_adj = gtk_adjustment_new (MIN_FONT_SIZE, 
                                          MIN_FONT_SIZE, MAX_FONT_SIZE, 1, 8, 0);

  accessib = gtk_widget_get_accessible (GTK_WIDGET (fontsel));
  atk_object_set_name (accessib, _("Font"));

  fontsel->family = gtk_tree_view_new ();

  renderer = gtk_cell_renderer_text_new ();
  column = gtk_tree_view_column_new_with_attributes ("Font Family",
                                                     renderer,
                                                     "text", COL_FAMILIY,
                                                     NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (fontsel->family), column);
  gtk_widget_show (fontsel->family);
  accessib = gtk_widget_get_accessible (fontsel->family);
  atk_object_set_name (accessib, _("Font Family"));

  fontsel->bold = gtk_toggle_button_new_with_mnemonic (GTK_STOCK_BOLD);
  gtk_button_set_use_stock (GTK_BUTTON (fontsel->bold), TRUE);
  gtk_widget_show (fontsel->bold);
  g_signal_connect (fontsel->bold, "toggled",
                    G_CALLBACK (bold_toggled), fontsel);

  fontsel->italic = gtk_toggle_button_new_with_mnemonic (GTK_STOCK_ITALIC);
  gtk_button_set_use_stock (GTK_BUTTON (fontsel->italic), TRUE);
  gtk_widget_show (fontsel->italic);
  g_signal_connect (fontsel->italic, "toggled",
                    G_CALLBACK (italic_toggled), fontsel);

  fontsel->size = gtk_spin_button_new (GTK_ADJUSTMENT (fontsel->size_adj),
                                       0, 0);
  gtk_widget_show (fontsel->size);
  accessib = gtk_widget_get_accessible (fontsel->size);
  atk_object_set_name (accessib, _("Font Size"));
  g_signal_connect (fontsel->size_adj, "value-changed",
                    G_CALLBACK (font_size_changed), fontsel);

  fill_font_families (fontsel);

  scrolled = gtk_scrolled_window_new (NULL, NULL);
  gtk_container_add (GTK_CONTAINER (scrolled), fontsel->family);

  content = gtk_dialog_get_content_area (GTK_DIALOG (fontsel));
  gtk_box_pack_start (GTK_BOX (content), scrolled, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (content), fontsel->bold, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (content), fontsel->italic, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (content), fontsel->size, FALSE, FALSE, 0);

  gtk_container_set_border_width (GTK_CONTAINER (fontsel), 6);

  gtk_widget_show_all (GTK_WIDGET (content));

  g_signal_connect (gtk_tree_view_get_selection (fontsel->family), "changed",
                    G_CALLBACK (family_changed), fontsel);
}

GtkWidget *
gucharmap_mini_font_selection_new (GtkWindow *parent)
{
  return GTK_WIDGET (g_object_new (gucharmap_mini_font_selection_get_type (), 
                                   "transient-for", parent,
                                   "modal", TRUE,
                                   NULL));
}

void
gucharmap_mini_font_selection_set_font_desc (GucharmapMiniFontSelection *fontsel,
                                             PangoFontDescription *font_desc)
{
  GObject *object = G_OBJECT (fontsel);
  PangoFontDescription *new_font_desc;
  const char *new_font_family;

  g_return_if_fail (GUCHARMAP_IS_MINI_FONT_SELECTION (fontsel));
  g_return_if_fail (font_desc != NULL);

  g_object_freeze_notify (object);

  new_font_desc = pango_font_description_copy (font_desc);
  new_font_family = pango_font_description_get_family (new_font_desc);
  if (!new_font_family) {
    pango_font_description_set_family (new_font_desc, "Sans");
    new_font_family = pango_font_description_get_family (new_font_desc);
  }

  if ((!fontsel->font_desc ||
       strcmp (pango_font_description_get_family (fontsel->font_desc), new_font_family) != 0) &&
      pango_font_description_get_size (new_font_desc) > 0)
    fontsel->default_size = pango_font_description_get_size (new_font_desc) / PANGO_SCALE;

  if (fontsel->font_desc)
    pango_font_description_free (fontsel->font_desc);
  
  fontsel->font_desc = new_font_desc;
  
  update_font_family_tree_view (fontsel);
    
  /* treat oblique and italic both as italic */
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fontsel->italic), pango_font_description_get_style (fontsel->font_desc) == PANGO_STYLE_ITALIC || pango_font_description_get_style (fontsel->font_desc) == PANGO_STYLE_OBLIQUE);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fontsel->bold), pango_font_description_get_weight (fontsel->font_desc) > PANGO_WEIGHT_NORMAL);

  gtk_adjustment_set_value (
          GTK_ADJUSTMENT (fontsel->size_adj), 
          pango_font_description_get_size (fontsel->font_desc) / PANGO_SCALE);

  g_object_notify (G_OBJECT (fontsel), "font-desc");

  g_object_thaw_notify (object);
}

PangoFontDescription *
gucharmap_mini_font_selection_get_font_desc (GucharmapMiniFontSelection *fontsel)
{
  g_return_val_if_fail (GUCHARMAP_IS_MINI_FONT_SELECTION (fontsel), NULL);

  return fontsel->font_desc;
}

void
gucharmap_mini_font_selection_change_font_size (GucharmapMiniFontSelection *fontsel,
                                                float factor)
{
  int size, new_size;

  g_return_if_fail (factor > 0.0f);

  size = get_font_size (fontsel);
  new_size = (float) size * factor;

  if (factor > 1.0f)
    new_size = MAX (new_size, size + 1);
  else if (factor < 1.0f)
    new_size = MIN (new_size, size - 1);

  set_font_size (fontsel, new_size);
}

void
gucharmap_mini_font_selection_reset_font_size (GucharmapMiniFontSelection *fontsel)
{
  if (fontsel->default_size > 0) {
    set_font_size (fontsel, fontsel->default_size);
  } else {
    GtkStyle *style;

    style = gtk_widget_get_style (GTK_WIDGET (fontsel));
    set_font_size (fontsel, pango_font_description_get_size (style->font_desc) * 2.0f / PANGO_SCALE);
  }
}
