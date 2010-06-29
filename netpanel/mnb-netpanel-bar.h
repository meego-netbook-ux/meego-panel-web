/* mwb-netpanel-bar.h */
/*
 * Copyright (c) 2009 Intel Corp.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU Lesser General Public License,
 * version 2.1, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

/* Borrowed from the meego-web-browser project */

#ifndef _MNB_NETPANEL_BAR_H
#define _MNB_NETPANEL_BAR_H

#include <glib-object.h>
#include <clutter/clutter.h>
#include <meego-panel/mpl-entry.h>

G_BEGIN_DECLS

#define MNB_TYPE_NETPANEL_BAR mnb_netpanel_bar_get_type()

#define MNB_NETPANEL_BAR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  MNB_TYPE_NETPANEL_BAR, MnbNetpanelBar))

#define MNB_NETPANEL_BAR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  MNB_TYPE_NETPANEL_BAR, MnbNetpanelBarClass))

#define MWB_IS_NETPANEL_BAR(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  MNB_TYPE_NETPANEL_BAR))

#define MWB_IS_NETPANEL_BAR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  MNB_TYPE_NETPANEL_BAR))

#define MNB_NETPANEL_BAR_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  MNB_TYPE_NETPANEL_BAR, MnbNetpanelBarClass))

typedef struct _MnbNetpanelBarPrivate MnbNetpanelBarPrivate;

typedef struct {
  MplEntry parent;
  
  MnbNetpanelBarPrivate *priv;
} MnbNetpanelBar;

typedef struct {
  MplEntryClass parent_class;

  void (* go)                 (MnbNetpanelBar *netpanel_bar, const gchar *url);
} MnbNetpanelBarClass;

GType mnb_netpanel_bar_get_type (void);

MxWidget *mnb_netpanel_bar_new (const gchar *label);

void mnb_netpanel_bar_focus (MnbNetpanelBar *netpanel_bar);

gboolean mnb_netpanel_bar_check_for_search (MnbNetpanelBar *netpanel_bar,
                                            const gchar *url);

void mnb_netpanel_bar_button_press_cb(GObject         *obj,
                                      ClutterKeyEvent *event,
                                      MnbNetpanelBar  *self);

G_END_DECLS

#endif /* _MNB_NETPANEL_BAR_H */
