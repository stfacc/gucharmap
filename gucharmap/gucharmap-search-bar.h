/*
 * Copyright © 2004 Noah Levitt
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

/* GucharmapSearchBar handles all aspects of searching */

#ifndef GUCHARMAP_SEARCH_BAR_H
#define GUCHARMAP_SEARCH_BAR_H

#include <gtk/gtk.h>
#include <gucharmap/gucharmap.h>
#include "gucharmap-window.h"

G_BEGIN_DECLS

#define GUCHARMAP_TYPE_SEARCH_BAR          (gucharmap_search_bar_get_type ())
#define GUCHARMAP_SEARCH_BAR(o)            (G_TYPE_CHECK_INSTANCE_CAST ((o), GUCHARMAP_TYPE_SEARCH_BAR, GucharmapSearchBar))
#define GUCHARMAP_SEARCH_BAR_CLASS(k)      (G_TYPE_CHECK_CLASS_CAST((k), GUCHARMAP_TYPE_SEARCH_BAR, GucharmapSearchBarClass))
#define GUCHARMAP_IS_SEARCH_BAR(o)         (G_TYPE_CHECK_INSTANCE_TYPE ((o), GUCHARMAP_TYPE_SEARCH_BAR))
#define GUCHARMAP_IS_SEARCH_BAR_CLASS(k)   (G_TYPE_CHECK_CLASS_TYPE ((k), GUCHARMAP_TYPE_SEARCH_BAR))
#define GUCHARMAP_SEARCH_BAR_GET_CLASS(o)  (G_TYPE_INSTANCE_GET_CLASS ((o), GUCHARMAP_TYPE_SEARCH_BAR, GucharmapSearchBarClass))

typedef struct _GucharmapSearchBar GucharmapSearchBar;
typedef struct _GucharmapSearchBarClass GucharmapSearchBarClass;

struct _GucharmapSearchBar
{
  GtkSearchBar parent;
};

struct _GucharmapSearchBarClass
{
  GtkSearchBarClass parent_class;

  /* signals */
  void (* search_start)  (void);
  void (* search_finish) (gunichar found_char);
};

typedef enum
{
  GUCHARMAP_DIRECTION_BACKWARD = -1,
  GUCHARMAP_DIRECTION_FORWARD = 1
}
GucharmapDirection;

GType       gucharmap_search_bar_get_type      (void);
GtkWidget * gucharmap_search_bar_new           (GucharmapWindow *parent);
void        gucharmap_search_bar_present       (GucharmapSearchBar *search_bar);
void        gucharmap_search_bar_start_search  (GucharmapSearchBar *search_bar,
                                                GucharmapDirection  direction);

G_END_DECLS

#endif /* #ifndef GUCHARMAP_SEARCH_BAR_H */
