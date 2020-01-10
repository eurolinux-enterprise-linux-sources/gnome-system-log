/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2; -*- */
/* logview-findbar.c - find toolbar for logview
 *
 * Copyright (C) 2005 Vincent Noel <vnoel@cox.net>
 * Copyright (C) 2008 Cosimo Cecchi <cosimoc@gnome.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>
#include <math.h>

#include "logview-findbar.h"

struct _LogviewFindbarPrivate {
  GtkWidget *entry;

  GtkWidget *back_button;
  GtkWidget *forward_button;
  
  char *string;
};

enum {
  PREVIOUS,
  NEXT,
  CLOSE,
  TEXT_CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (LogviewFindbar, logview_findbar, GTK_TYPE_TOOLBAR);

#define GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), LOGVIEW_TYPE_FINDBAR, LogviewFindbarPrivate))

static void
back_button_clicked_cb (GtkToolButton *button,
                        gpointer user_data)
{
  LogviewFindbar *findbar = user_data;

  g_signal_emit (findbar, signals[PREVIOUS], 0);
}

static void
forward_button_clicked_cb (GtkToolButton *button,
                           gpointer user_data)
{
  LogviewFindbar *findbar = user_data;

  g_signal_emit (findbar, signals[NEXT], 0);
}

static void
entry_activate_cb (GtkWidget *entry,
                   gpointer user_data)
{
  LogviewFindbar *findbar = user_data;

  g_signal_emit (findbar, signals[NEXT], 0);
}

static void
entry_changed_cb (GtkEditable *editable,
                  gpointer user_data)
{
  LogviewFindbar *findbar = user_data;
  const char *text;

  text = gtk_entry_get_text (GTK_ENTRY (editable));

  if (g_strcmp0 (findbar->priv->string, text) != 0) {
    g_free (findbar->priv->string);
    findbar->priv->string = g_strdup (text);

    g_signal_emit (findbar, signals[TEXT_CHANGED], 0);
  }
}

static gboolean
entry_key_press_event_cb (GtkWidget *entry,
                          GdkEventKey *event,
                          gpointer user_data)
{
  LogviewFindbar *findbar = user_data;

  if (event->keyval == GDK_KEY_Escape) {
    g_signal_emit (findbar, signals[CLOSE], 0);
    return TRUE;
  }

  return FALSE;
}

static gint
get_icon_margin (void)
{
  gint toolbar_size, menu_size;

  gtk_icon_size_lookup (GTK_ICON_SIZE_MENU, &menu_size, NULL);
  gtk_icon_size_lookup (GTK_ICON_SIZE_LARGE_TOOLBAR, &toolbar_size, NULL);
  return (gint) floor ((toolbar_size - menu_size) / 2.0);
}

static void 
logview_findbar_init (LogviewFindbar *findbar)
{
  GtkWidget *w, *box;
  GtkToolbar *gtoolbar;
  GtkToolItem *item;
  LogviewFindbarPrivate *priv;
  
  priv = findbar->priv = GET_PRIVATE (findbar);

  gtoolbar = GTK_TOOLBAR (findbar);

  gtk_toolbar_set_style (gtoolbar, GTK_TOOLBAR_BOTH_HORIZ);

  priv->entry = gtk_search_entry_new ();

  item = gtk_tool_item_new ();
  gtk_tool_item_set_expand (GTK_TOOL_ITEM (item), TRUE);
  gtk_container_add (GTK_CONTAINER (item), priv->entry);
  gtk_toolbar_insert (gtoolbar, item, -1);
  gtk_widget_show_all (GTK_WIDGET (item));

  item = gtk_separator_tool_item_new ();
  gtk_separator_tool_item_set_draw (GTK_SEPARATOR_TOOL_ITEM (item), FALSE);
  gtk_toolbar_insert (gtoolbar, item, -1);
  gtk_widget_show (GTK_WIDGET (item));

  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_style_context_add_class (gtk_widget_get_style_context (box), "linked");

  /* "Previous" and "Next" buttons */
  priv->back_button = gtk_button_new ();
  w = gtk_image_new_from_icon_name ("go-up-symbolic", GTK_ICON_SIZE_MENU);
  g_object_set (w, "margin", get_icon_margin (), NULL);
  gtk_container_add (GTK_CONTAINER (priv->back_button), w);
  gtk_container_add (GTK_CONTAINER (box), priv->back_button);
  gtk_widget_set_tooltip_text (priv->back_button,
                               _("Find previous occurrence of the search string"));
  gtk_widget_show_all (GTK_WIDGET (priv->back_button));

  priv->forward_button = gtk_button_new ();
  w = gtk_image_new_from_icon_name ("go-down-symbolic", GTK_ICON_SIZE_MENU);
  g_object_set (w, "margin", get_icon_margin (), NULL);
  gtk_container_add (GTK_CONTAINER (priv->forward_button), w);
  gtk_container_add (GTK_CONTAINER (box), priv->forward_button);
  gtk_widget_set_tooltip_text (priv->forward_button,
                               _("Find next occurrence of the search string"));
  gtk_widget_show_all (GTK_WIDGET (priv->forward_button));

  item = gtk_tool_item_new ();
  gtk_container_add (GTK_CONTAINER (item), box);
  gtk_toolbar_insert (gtoolbar, item, -1);
  gtk_widget_show_all (GTK_WIDGET (item));

  priv->string = NULL;

  /* signal handlers */
  g_signal_connect (priv->back_button, "clicked",
                    G_CALLBACK (back_button_clicked_cb), findbar);
  g_signal_connect (priv->forward_button, "clicked",
                    G_CALLBACK (forward_button_clicked_cb), findbar);
  g_signal_connect (priv->entry, "activate",
                    G_CALLBACK (entry_activate_cb), findbar);
  g_signal_connect (priv->entry, "changed",
                    G_CALLBACK (entry_changed_cb), findbar);
  g_signal_connect (priv->entry, "key-press-event",
                    G_CALLBACK (entry_key_press_event_cb), findbar);
}

static void
do_grab_focus (GtkWidget *widget)
{
  LogviewFindbar *findbar = LOGVIEW_FINDBAR (widget);

  gtk_widget_grab_focus (findbar->priv->entry);
}

static void
do_finalize (GObject *obj)
{
  LogviewFindbar *findbar = LOGVIEW_FINDBAR (obj);

  g_free (findbar->priv->string);

  G_OBJECT_CLASS (logview_findbar_parent_class)->finalize (obj);
}

static void
logview_findbar_class_init (LogviewFindbarClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wclass = GTK_WIDGET_CLASS (klass);

  oclass->finalize = do_finalize;

  wclass->grab_focus = do_grab_focus;

  signals[PREVIOUS] = g_signal_new ("previous",
                                    G_OBJECT_CLASS_TYPE (oclass),
                                    G_SIGNAL_RUN_LAST,
                                    G_STRUCT_OFFSET (LogviewFindbarClass, previous),
                                    NULL, NULL,
                                    g_cclosure_marshal_VOID__VOID,
                                    G_TYPE_NONE, 0);

  signals[NEXT] = g_signal_new ("next",
                                G_OBJECT_CLASS_TYPE (oclass),
                                G_SIGNAL_RUN_LAST,
                                G_STRUCT_OFFSET (LogviewFindbarClass, next),
                                NULL, NULL,
                                g_cclosure_marshal_VOID__VOID,
                                G_TYPE_NONE, 0);

  signals[CLOSE] = g_signal_new ("close",
                                 G_OBJECT_CLASS_TYPE (oclass),
                                 G_SIGNAL_RUN_LAST,
                                 G_STRUCT_OFFSET (LogviewFindbarClass, close),
                                 NULL, NULL,
                                 g_cclosure_marshal_VOID__VOID,
                                 G_TYPE_NONE, 0);

  signals[TEXT_CHANGED] = g_signal_new ("text-changed",
                                        G_OBJECT_CLASS_TYPE (oclass),
                                        G_SIGNAL_RUN_LAST,
                                        G_STRUCT_OFFSET (LogviewFindbarClass, text_changed),
                                        NULL, NULL,
                                        g_cclosure_marshal_VOID__VOID,
                                        G_TYPE_NONE, 0);

  g_type_class_add_private (klass, sizeof (LogviewFindbarPrivate));
}

/* public methods */

GtkWidget *
logview_findbar_new (void)
{
  return g_object_new (LOGVIEW_TYPE_FINDBAR, NULL);
}

const char *
logview_findbar_get_text (LogviewFindbar *findbar)
{
  g_assert (LOGVIEW_IS_FINDBAR (findbar));

  return findbar->priv->string;
}
