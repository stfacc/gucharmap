#include "gucharmap-fontprops-dialog.h"

typedef struct
{
  gunichar character;
  gchar ubuf[7];

  GtkHeaderBar *header_bar;
  GtkLabel     *label;
  GtkLabel     *unicode_code_label;
} GucharmapFontpropsDialogPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GucharmapFontpropsDialog, gucharmap_fontprops_dialog, GTK_TYPE_DIALOG)

enum
{
  PROP_0,
  PROP_CHARACTER
};

static gchar *
capitalize_string (const gchar *s)
{
  gchar *t = g_strdup (s);
  gchar *r;

  for (r = t; *r != '\0'; r++)
    {
      if (r == t || g_ascii_isspace (*(r-1)))
        *r = g_ascii_toupper (*r);
      else
        *r = g_ascii_tolower (*r);
    }

  return t;
}

static void
set_character (GucharmapFontpropsDialog *dialog, gunichar wc)
{
  GucharmapFontpropsDialogPrivate *priv = gucharmap_fontprops_dialog_get_instance_private (dialog);
  gchar *str;

  if (!gucharmap_unichar_validate (wc))
    return;

  str = capitalize_string (gucharmap_get_unicode_name (wc));
  gtk_header_bar_set_title (priv->header_bar, str);
  g_free (str);

  priv->ubuf[g_unichar_to_utf8 (wc, priv->ubuf)] = '\0';

  str = g_strdup_printf ("<span size=\"100000\">%s</span>", priv->ubuf);
  gtk_label_set_markup (priv->label, str);
  g_free (str);

  str = g_strdup_printf ("U+%4.4X", wc);
  gtk_label_set_text (priv->unicode_code_label, str);
  g_free (str);
}

static void
gucharmap_fontprops_dialog_get_property (GObject    *object,
                                         guint       prop_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
  GucharmapFontpropsDialog *dialog = GUCHARMAP_FONTPROPS_DIALOG (object);
  GucharmapFontpropsDialogPrivate *priv = gucharmap_fontprops_dialog_get_instance_private (dialog);

  switch (prop_id)
    {
    case PROP_CHARACTER:
      g_value_set_uint (value, priv->character);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gucharmap_fontprops_dialog_set_property (GObject      *object,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
  GucharmapFontpropsDialog *dialog = GUCHARMAP_FONTPROPS_DIALOG (object);

  switch (prop_id)
    {
    case PROP_CHARACTER:
      set_character (dialog, g_value_get_uint (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
copy_button_clicked (GucharmapFontpropsDialog *dialog)
{
  GucharmapFontpropsDialogPrivate *priv = gucharmap_fontprops_dialog_get_instance_private (dialog);
 
  gtk_clipboard_set_text (gtk_widget_get_clipboard (GTK_WIDGET (dialog),
                                                    GDK_SELECTION_CLIPBOARD),
                          priv->ubuf, -1);
}

static void
gucharmap_fontprops_dialog_init (GucharmapFontpropsDialog *dialog)
{
  gtk_widget_init_template (GTK_WIDGET (dialog));
}

static void
gucharmap_fontprops_dialog_class_init (GucharmapFontpropsDialogClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gobject_class->get_property = gucharmap_fontprops_dialog_get_property;
  gobject_class->set_property = gucharmap_fontprops_dialog_set_property;

  g_object_class_install_property (gobject_class,
                                   PROP_CHARACTER,
                                   g_param_spec_unichar ("character",
                                                         "Unicode character",
                                                         "",
                                                         0,
                                                         G_PARAM_READWRITE));

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/charmap/ui/fontprops-dialog.ui");

  gtk_widget_class_bind_template_child_private (widget_class, GucharmapFontpropsDialog, header_bar);
  gtk_widget_class_bind_template_child_private (widget_class, GucharmapFontpropsDialog, label);
  gtk_widget_class_bind_template_child_private (widget_class, GucharmapFontpropsDialog, unicode_code_label);

  gtk_widget_class_bind_template_callback (widget_class, copy_button_clicked);
}

GtkWidget *
gucharmap_fontprops_dialog_new (GucharmapWindow *parent,
                                gunichar         wc)
{
  return g_object_new (GUCHARMAP_TYPE_FONTPROPS_DIALOG,
                       "transient-for", parent,
                       "modal", TRUE,
                       "character", wc,
                       NULL);
}
