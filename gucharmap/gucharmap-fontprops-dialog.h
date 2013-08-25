#ifndef GUCHARMAP_FONTPROPS_DIALOG_H
#define GUCHARMAP_FONTPROPS_DIALOG_H

#include <gtk/gtk.h>
#include "gucharmap-window.h"

G_BEGIN_DECLS

#define GUCHARMAP_TYPE_FONTPROPS_DIALOG          (gucharmap_fontprops_dialog_get_type ())
#define GUCHARMAP_FONTPROPS_DIALOG(o)            (G_TYPE_CHECK_INSTANCE_CAST ((o), GUCHARMAP_TYPE_FONTPROPS_DIALOG, GucharmapFontpropsDialog))
#define GUCHARMAP_FONTPROPS_DIALOG_CLASS(k)      (G_TYPE_CHECK_CLASS_CAST((k), GUCHARMAP_TYPE_FONTPROPS_DIALOG, GucharmapFontpropsDialogClass))
#define GUCHARMAP_IS_FONTPROPS_DIALOG(o)         (G_TYPE_CHECK_INSTANCE_TYPE ((o), GUCHARMAP_TYPE_FONTPROPS_DIALOG))
#define GUCHARMAP_IS_FONTPROPS_DIALOG_CLASS(k)   (G_TYPE_CHECK_CLASS_TYPE ((k), GUCHARMAP_TYPE_FONTPROPS_DIALOG))
#define GUCHARMAP_FONTPROPS_DIALOG_GET_CLASS(o)  (G_TYPE_INSTANCE_GET_CLASS ((o), GUCHARMAP_TYPE_FONTPROPS_DIALOG, GucharmapFontpropsDialogClass))

typedef struct _GucharmapFontpropsDialog      GucharmapFontpropsDialog;
typedef struct _GucharmapFontpropsDialogClass GucharmapFontpropsDialogClass;

struct _GucharmapFontpropsDialog
{
  GtkDialog parent_instance;
};

struct _GucharmapFontpropsDialogClass
{
  GtkDialogClass parent_class;
};

GType       gucharmap_fontprops_dialog_get_type  (void);

GtkWidget * gucharmap_fontprops_dialog_new       (GucharmapWindow *parent,
                                                  gunichar         wc);


G_END_DECLS

#endif /* #ifndef GUCHARMAP_FONTPROPS_DIALOG_H */
