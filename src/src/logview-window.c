/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2; -*- */
/* logview-window.c - main window of logview
 *
 * Copyright (C) 1998  Cesar Miquel  <miquel@df.uba.ar>
 * Copyright (C) 2008  Cosimo Cecchi <cosimoc@gnome.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <config.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <glib/gi18n.h>

#include "logview-window.h"

#include "logview-loglist.h"
#include "logview-findbar.h"
#include "logview-prefs.h"
#include "logview-manager.h"
#include "logview-filter-manager.h"

#define SEARCH_START_MARK "lw-search-start-mark"
#define SEARCH_END_MARK "lw-search-end-mark"

typedef struct {
  PangoFontDescription *monospace_description;

  GtkWidget *header_bar;
  GtkWidget *window_content;
  GtkWidget *find_bar_revealer;
  GtkWidget *find_bar;
  GtkWidget *sidebar_scrolledwindow;
  GtkWidget *loglist;
  GtkWidget *text_view;

  GtkWidget *message_area;
  GtkWidget *message_primary;
  GtkWidget *message_secondary;

  GMenuModel *filters_placeholder;

  GtkTextTagTable *tag_table;

  int original_fontsize, fontsize;

  LogviewPrefs *prefs;
  LogviewManager *manager;

  gulong monitor_id;
  guint search_timeout_id;

  GCancellable *read_cancellable;

  guint filter_merge_id;
  GList *active_filters;
  gboolean matches_only;
  gboolean auto_scroll;
} LogviewWindowPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LogviewWindow, logview_window, GTK_TYPE_APPLICATION_WINDOW);

static void findbar_close_cb  (LogviewFindbar *findbar,
                               gpointer user_data);
static void read_new_lines_cb (LogviewLog *log,
                               const char **lines,
                               GSList *new_days,
                               GError *error,
                               gpointer user_data);

/* private helpers */

static void
populate_tag_table (GtkTextTagTable *tag_table)
{
  GtkTextTag *tag;
  
  tag = gtk_text_tag_new ("bold");
  g_object_set (tag, "weight", PANGO_WEIGHT_BOLD,
                "weight-set", TRUE, NULL);

  gtk_text_tag_table_add (tag_table, tag);

  tag = gtk_text_tag_new ("invisible");
  g_object_set (tag, "invisible", TRUE, "invisible-set", TRUE, NULL);
  gtk_text_tag_table_add (tag_table, tag);

  tag = gtk_text_tag_new ("invisible-filter");
  g_object_set (tag, "invisible", TRUE, "invisible-set", TRUE, NULL);
  gtk_text_tag_table_add (tag_table, tag); 
}

static void
populate_style_tag_table (LogviewWindow *logview)
{
  LogviewWindowPrivate *priv = logview_window_get_instance_private (logview);
  GtkTextTagTable *tag_table = priv->tag_table;
  GtkTextTag *tag;
  GtkStyleContext *context;
  GdkRGBA rgba;

  tag = gtk_text_tag_table_lookup (tag_table, "gray");

  if (tag) {
    gtk_text_tag_table_remove (tag_table, tag);
  }

  tag = gtk_text_tag_new ("gray");

  context = gtk_widget_get_style_context (priv->text_view);
  gtk_style_context_save (context);
  gtk_style_context_add_class (context, "dim-label");
  gtk_style_context_get_color (context, GTK_STATE_FLAG_NORMAL, &rgba);
  gtk_style_context_restore (context);

  g_object_set (tag, "foreground-rgba", &rgba, "foreground-set", TRUE, NULL);

  gtk_text_tag_table_add (tag_table, tag);
}

static void
_gtk_text_buffer_apply_tag_to_rectangle (GtkTextBuffer *buffer, int line_start, int line_end,
                                        int offset_start, int offset_end, char *tag_name)
{
  GtkTextIter start, end;
  int line_cur;

  gtk_text_buffer_get_iter_at_line (buffer, &start, line_start);
  gtk_text_buffer_get_iter_at_line (buffer, &end, line_start);

  for (line_cur = line_start; line_cur < line_end + 1; line_cur++) {

    if (offset_start > 0) {
      gtk_text_iter_forward_chars (&start, offset_start);
    }

    gtk_text_iter_forward_chars (&end, offset_end);

    gtk_text_buffer_apply_tag_by_name (buffer, tag_name, &start, &end);

    gtk_text_iter_forward_line (&start);
    gtk_text_iter_forward_line (&end);
  }
}

static void
logview_update_header (LogviewWindow *logview,
                       LogviewLog *active,
                       Day *selected_day)
{
  LogviewWindowPrivate *priv = logview_window_get_instance_private (logview);
  GString *string;
  GDateTime *log_time;
  gchar *title, *subtitle, *text;
  time_t timestamp;

  title = subtitle = NULL;

  if (active == NULL) {
    goto out;
  }

  if (gtk_revealer_get_reveal_child (GTK_REVEALER (priv->find_bar_revealer))) {
    title = g_strdup_printf (_("Search in \"%s\""),
                             logview_log_get_display_name (active));
    goto out;
  }

  title = g_strdup (logview_log_get_display_name (active));

  if (selected_day != NULL) {
    subtitle = logview_utils_format_date (selected_day->date);
    goto out;
  }

  timestamp = logview_log_get_timestamp (active);
  log_time = g_date_time_new_from_unix_local (timestamp);

  text = logview_utils_format_date (log_time);
  g_date_time_unref (log_time);

  /* translators: this is part of a label composed with
   * a date string, for example "updated today 23:54"
   */
  string = g_string_new (_("updated"));
  g_string_append (string, " ");
  g_string_append (string, text);
  g_free (text);

  text = g_date_time_format (log_time, "%X");
  g_string_append (string, " ");
  g_string_append (string, text);
  g_free (text);

  subtitle = g_string_free (string, FALSE);

 out:
  gtk_header_bar_set_title (GTK_HEADER_BAR (priv->header_bar), title);
  gtk_header_bar_set_subtitle (GTK_HEADER_BAR (priv->header_bar), subtitle);

  g_free (title);
  g_free (subtitle);
}

#define DEFAULT_LOGVIEW_FONT "Monospace 10"

static void
logview_set_font (LogviewWindow *logview,
                  const char    *fontname)
{
  LogviewWindowPrivate *priv = logview_window_get_instance_private (logview);

  if (fontname == NULL)
    fontname = DEFAULT_LOGVIEW_FONT;

  if (priv->monospace_description != NULL)
    pango_font_description_free (priv->monospace_description);

  priv->monospace_description = pango_font_description_from_string (fontname);
  gtk_widget_override_font (priv->text_view, priv->monospace_description);

  /* remember the original font size */
  priv->original_fontsize = 
    pango_font_description_get_size (priv->monospace_description) / PANGO_SCALE;
}

static void
logview_set_fontsize (LogviewWindow *logview, gboolean store)
{
  LogviewWindowPrivate *priv = logview_window_get_instance_private (logview);

  pango_font_description_set_size (priv->monospace_description,
                                   (priv->fontsize) * PANGO_SCALE);
  gtk_widget_override_font (priv->text_view, priv->monospace_description);

  if (store) {
    logview_prefs_store_fontsize (priv->prefs, priv->fontsize);
  }
}

static void
logview_set_search_visible (LogviewWindow *window,
                            gboolean visible)
{
  g_action_group_change_action_state (G_ACTION_GROUP (window), "search",
                                      g_variant_new_boolean (visible));
}

static void
logview_clear_active_log_state (LogviewWindow *window,
                                LogviewLog *old_log)
{
  LogviewWindowPrivate *priv = logview_window_get_instance_private (window);

  if (priv->monitor_id) {
    g_signal_handler_disconnect (old_log, priv->monitor_id);
    priv->monitor_id = 0;
  }

  logview_set_search_visible (window, FALSE);
}

static void
findbar_close_cb (LogviewFindbar *findbar,
                  gpointer user_data)
{
  LogviewWindow *window = user_data;
  logview_set_search_visible (window, FALSE);
}

static void
logview_search_text (LogviewWindow *logview, gboolean forward)
{
  LogviewWindowPrivate *priv = logview_window_get_instance_private (logview);
  GtkTextBuffer *buffer;
  GtkTextMark *search_start, *search_end;
  GtkTextIter search, start_m, end_m;
  const char *text, *secondary;
  gboolean res, wrapped;
  gchar *primary;
  LogviewLog *active_log;

  wrapped = FALSE;

  text = logview_findbar_get_text (LOGVIEW_FINDBAR (priv->find_bar));

  if (!text || g_strcmp0 (text, "") == 0) {
    return;
  }

  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (priv->text_view));
  search_start = gtk_text_buffer_get_mark (buffer, SEARCH_START_MARK);
  search_end = gtk_text_buffer_get_mark (buffer, SEARCH_END_MARK);

  secondary = primary = NULL;

  if (!search_start) {
    /* this is our first search on the buffer, create a new search mark */
    gtk_text_buffer_get_start_iter (buffer, &search);
    search_start = gtk_text_buffer_create_mark (buffer, SEARCH_START_MARK,
                                                &search, TRUE);
    search_end = gtk_text_buffer_create_mark (buffer, SEARCH_END_MARK,
                                              &search, TRUE);
  } else {
    if (forward) {
      gtk_text_buffer_get_iter_at_mark (buffer, &search, search_end);
    } else {
      gtk_text_buffer_get_iter_at_mark (buffer, &search, search_start);
    }
  }

wrap:

  if (forward) {
    res = gtk_text_iter_forward_search (&search, text, GTK_TEXT_SEARCH_VISIBLE_ONLY, &start_m, &end_m, NULL);
  } else {
    res = gtk_text_iter_backward_search (&search, text, GTK_TEXT_SEARCH_VISIBLE_ONLY, &start_m, &end_m, NULL);
  }

  if (res) {
    gtk_text_buffer_select_range (buffer, &start_m, &end_m);
    gtk_text_buffer_move_mark (buffer, search_start, &start_m);
    gtk_text_buffer_move_mark (buffer, search_end, &end_m);

    gtk_text_view_scroll_mark_onscreen (GTK_TEXT_VIEW (priv->text_view), search_end);

    if (wrapped) {
      secondary = _("Wrapped");
    }
  } else {
    if (wrapped) {
      
      GtkTextMark *mark;
      GtkTextIter iter;

      if (gtk_text_buffer_get_has_selection (buffer)) {
        /* unselect */
        mark = gtk_text_buffer_get_mark (buffer, "insert");
        gtk_text_buffer_get_iter_at_mark (buffer, &iter, mark);
        gtk_text_buffer_move_mark_by_name (buffer, "selection_bound", &iter);
      }

      secondary = _("No matches found");
    } else {
      if (forward) {
        gtk_text_buffer_get_start_iter (buffer, &search);
      } else {
        gtk_text_buffer_get_end_iter (buffer, &search);
      }

      wrapped = TRUE;
      goto wrap;
    }
  }

  active_log = logview_manager_get_active_log (logview_manager_get ());
  if (!active_log)
    return;

  primary = g_strdup_printf (_("Search in \"%s\""),
                             logview_log_get_display_name (active_log));

  gtk_header_bar_set_title (GTK_HEADER_BAR (priv->header_bar), primary);
  gtk_header_bar_set_subtitle (GTK_HEADER_BAR (priv->header_bar), secondary);


  g_free (primary);
}

static void
findbar_previous_cb (LogviewFindbar *findbar,
                     gpointer user_data)
{
  LogviewWindow *logview = user_data;

  logview_search_text (logview, FALSE);
}

static void
findbar_next_cb (LogviewFindbar *findbar,
                 gpointer user_data)
{
  LogviewWindow *logview = user_data;

  logview_search_text (logview, TRUE);
}

static gboolean
text_changed_timeout_cb (gpointer user_data)
{
  LogviewWindow *logview = user_data;
  LogviewWindowPrivate *priv = logview_window_get_instance_private (user_data);
  GtkTextMark *search_start, *search_end;
  GtkTextIter start;
  GtkTextBuffer *buffer;

  priv->search_timeout_id = 0;

  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (priv->text_view));
  search_start = gtk_text_buffer_get_mark (buffer, SEARCH_START_MARK);
  search_end = gtk_text_buffer_get_mark (buffer, SEARCH_END_MARK);
  
  if (search_start) {
    /* reset the search mark to the start */
    gtk_text_buffer_get_start_iter (buffer, &start);
    gtk_text_buffer_move_mark (buffer, search_start, &start);
    gtk_text_buffer_move_mark (buffer, search_end, &start);
  }

  logview_search_text (logview, TRUE);

  return FALSE;
}

static void
findbar_text_changed_cb (LogviewFindbar *findbar,
                         gpointer user_data)
{
  LogviewWindow *logview = user_data;
  LogviewWindowPrivate *priv = logview_window_get_instance_private (user_data);

  if (priv->search_timeout_id != 0) {
    g_source_remove (priv->search_timeout_id);
  }

  priv->search_timeout_id = g_timeout_add (300, text_changed_timeout_cb, logview);
}

static void
filter_buffer (LogviewWindow *logview, gint start_line)
{
  LogviewWindowPrivate *priv = logview_window_get_instance_private (logview);
  GtkTextBuffer *buffer;
  GtkTextIter start, *end;
  gchar* text;
  GList* cur_filter;
  gboolean matched;
  int lines, i;

  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (priv->text_view));
  lines = gtk_text_buffer_get_line_count (buffer);

  for (i = start_line; i < lines; i++) {
    matched = FALSE;

    gtk_text_buffer_get_iter_at_line (buffer, &start, i);
    end = gtk_text_iter_copy (&start);
    gtk_text_iter_forward_line (end);

    text = gtk_text_buffer_get_text (buffer, &start, end, TRUE);

    for (cur_filter = priv->active_filters; cur_filter != NULL;
         cur_filter = g_list_next (cur_filter))
    {
      if (logview_filter_filter (LOGVIEW_FILTER (cur_filter->data), text)) {
        gtk_text_buffer_apply_tag (buffer, 
                                   logview_filter_get_tag (LOGVIEW_FILTER (cur_filter->data)),
                                   &start, end);
        matched = TRUE;
      }
    }

    g_free (text);

    if (!matched && priv->matches_only) {
      gtk_text_buffer_apply_tag_by_name (buffer, 
                                         "invisible-filter",
                                         &start, end);
    } else {
      gtk_text_buffer_remove_tag_by_name (buffer,
                                          "invisible-filter",
                                          &start, end);
    }

    gtk_text_iter_free (end);
  }
}

static void
filter_remove (LogviewWindow *logview, LogviewFilter *filter)
{
  LogviewWindowPrivate *priv = logview_window_get_instance_private (logview);
  GtkTextIter start, end;  
  GtkTextBuffer *buffer;

  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (priv->text_view));
  gtk_text_buffer_get_bounds (buffer, &start, &end);

  gtk_text_buffer_remove_tag (buffer, logview_filter_get_tag (filter),
                              &start, &end);
}

static void
filter_activate (GSimpleAction *action,
                 GVariant *parameter,
                 gpointer user_data)
{
  LogviewWindow *logview = user_data;
  LogviewWindowPrivate *priv = logview_window_get_instance_private (logview);
  const gchar* action_name, *name;
  LogviewFilter *filter;
  GVariant *state_variant;
  gboolean new_state;

  state_variant = g_action_get_state (G_ACTION (action));
  new_state = !g_variant_get_boolean (state_variant);
  g_variant_unref (state_variant);

  action_name = g_action_get_name (G_ACTION (action));
  name = action_name + strlen ("filter_");

  if (new_state) {
    priv->active_filters = g_list_append (priv->active_filters,
                                          logview_prefs_get_filter (priv->prefs,
                                                                    name));
    filter_buffer (logview, 0);
  } else {
    filter = logview_prefs_get_filter (priv->prefs, name);
    priv->active_filters = g_list_remove (priv->active_filters,
                                          filter);

    filter_remove (logview, filter);
  }

  g_simple_action_set_state (action, g_variant_new_boolean (new_state));
}

static void
update_filter_menu (LogviewWindow *window)
{
  LogviewWindowPrivate *priv = logview_window_get_instance_private (window);
  GVariant *variant;
  GtkTextTagTable *table;
  GtkTextTag *tag;
  gint idx;
  gchar *name, *action_name, *menu_action_name;
  GList *filters, *l;
  GSimpleAction *action;

  table = priv->tag_table;

  for (idx = g_menu_model_get_n_items (priv->filters_placeholder); idx > 0; idx--)
    {
      variant = g_menu_model_get_item_attribute_value (priv->filters_placeholder,
                                                       idx - 1, G_MENU_ATTRIBUTE_ACTION, NULL);
      name = (gchar *) g_variant_get_string (variant, NULL) + strlen ("win.filter_");
      action_name = (gchar *) g_variant_get_string (variant, NULL) + strlen ("win.");

      tag = gtk_text_tag_table_lookup (table, name);
      gtk_text_tag_table_remove (table, tag);

      g_menu_remove (G_MENU (priv->filters_placeholder), idx - 1);
      g_action_map_remove_action (G_ACTION_MAP (window), action_name);

      g_variant_unref (variant);
    }

  filters = logview_prefs_get_filters (logview_prefs_get ());

  for (l = filters; l != NULL; l = g_list_next (l)) {
    g_object_get (l->data, "name", &name, NULL);
    action_name = g_strconcat ("filter_", name, NULL);

    action = g_simple_action_new_stateful (action_name, NULL, g_variant_new_boolean (FALSE));
    g_action_map_add_action (G_ACTION_MAP (window), G_ACTION (action));

    menu_action_name = g_strconcat ("win.", action_name, NULL);
    g_menu_append (G_MENU (priv->filters_placeholder), name, menu_action_name);
    g_signal_connect (action, "activate",
                      G_CALLBACK (filter_activate), window);

    gtk_text_tag_table_add (table, 
                            logview_filter_get_tag (LOGVIEW_FILTER (l->data)));

    g_object_unref (action);
    g_free (name);
    g_free (action_name);
    g_free (menu_action_name);
  }

  g_list_free (filters);
}

static gboolean 
window_size_changed_cb (GtkWidget *widget, GdkEventConfigure *event, 
                        gpointer data)
{
  LogviewWindow *window = data;
  LogviewWindowPrivate *priv = logview_window_get_instance_private (window);

  logview_prefs_store_window_size (priv->prefs,
                                   event->width, event->height);

  return FALSE;
}

static void
real_select_day (LogviewWindow *logview,
                 int first_line, int last_line)
{
  LogviewWindowPrivate *priv = logview_window_get_instance_private (logview);
  GtkTextBuffer *buffer;
  GtkTextIter start_iter, end_iter, start_vis, end_vis;
  GdkRectangle visible_rect;

  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (priv->text_view));

  gtk_text_buffer_get_bounds (buffer, &start_iter, &end_iter);
  gtk_text_buffer_get_iter_at_line (buffer, &start_vis, first_line);
  gtk_text_buffer_get_iter_at_line (buffer, &end_vis, last_line + 1);

  /* clear all previous invisible tags */
  gtk_text_buffer_remove_tag_by_name (buffer, "invisible",
                                      &start_iter, &end_iter);

  gtk_text_buffer_apply_tag_by_name (buffer, "invisible",
                                     &start_iter, &start_vis);
  gtk_text_buffer_apply_tag_by_name (buffer, "invisible",
                                     &end_vis, &end_iter);

  /* FIXME: why is this needed to update the view when selecting a day back? */
  gtk_text_view_get_visible_rect (GTK_TEXT_VIEW (priv->text_view),
                                  &visible_rect);
  gdk_window_invalidate_rect (gtk_widget_get_window (priv->text_view),
                              &visible_rect, TRUE);
}

static void
loglist_day_selected_cb (LogviewLoglist *loglist,
                         Day *day,
                         gpointer user_data)
{
  LogviewWindow *logview = user_data;
  LogviewLog *active_log = logview_manager_get_active_log (logview_manager_get ());

  real_select_day (logview, day->first_line, day->last_line);
  logview_update_header (logview, active_log, day);
}

static void
loglist_day_cleared_cb (LogviewLoglist *loglist,
                        gpointer user_data)
{
  LogviewWindow *logview = user_data;
  LogviewWindowPrivate *priv = logview_window_get_instance_private (user_data);
  GtkTextBuffer *buffer;
  GtkTextIter start, end;
  LogviewLog *active_log = logview_manager_get_active_log (logview_manager_get ());

  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (priv->text_view));
  gtk_text_buffer_get_bounds (buffer, &start, &end);

  /* clear all previous invisible tags */
  gtk_text_buffer_remove_tag_by_name (buffer, "invisible",
                                      &start, &end);

  logview_update_header (logview, active_log, NULL);
}

static void
logview_window_schedule_log_read (LogviewWindow *window,
                                  LogviewLog *log)
{
  LogviewWindowPrivate *priv = logview_window_get_instance_private (window);

  if (priv->read_cancellable != NULL) {
    g_cancellable_cancel (priv->read_cancellable);
    g_clear_object (&priv->read_cancellable);
  }

  priv->read_cancellable = g_cancellable_new ();
  logview_log_read_new_lines (log,
                              priv->read_cancellable,
                              (LogviewNewLinesCallback) read_new_lines_cb,
                              window);
}

static void
log_monitor_changed_cb (LogviewLog *log,
                        gpointer user_data)
{
  LogviewWindow *window = user_data;

  /* reschedule a read */
  logview_window_schedule_log_read (window, log);
}

static void
paint_timestamps (GtkTextBuffer *buffer, int old_line_count,
                  GSList *days)
{
  GSList *l;

  for (l = days; l; l = l->next) {
    Day *day = l->data;

    _gtk_text_buffer_apply_tag_to_rectangle (buffer,
                                             old_line_count + day->first_line - 1,
                                             old_line_count + day->last_line,
                                             0, day->timestamp_len, "gray");
  }
}

static void
read_new_lines_cb (LogviewLog *log,
                   const char **lines,
                   GSList *new_days,
                   GError *error,
                   gpointer user_data)
{
  LogviewWindow *window = user_data;
  LogviewWindowPrivate *priv = logview_window_get_instance_private (window);
  GtkTextBuffer *buffer;
  gboolean boldify = FALSE;
  int i, old_line_count, filter_start_line;
  GtkTextIter iter, start;
  GtkTextMark *mark;
  char *converted, *primary;
  gsize len;

  if (error != NULL) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      primary = g_strdup_printf (_("Can't read from \"%s\""),
                                 logview_log_get_display_name (log));
      logview_window_add_error (window, primary, error->message);
      g_free (primary);
    }

    return;
  }

  if (lines == NULL) {
    /* there's no error, but no lines have been read */
    return;
  }

  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (priv->text_view));
  old_line_count = gtk_text_buffer_get_line_count (buffer);
  filter_start_line = old_line_count > 0 ? (old_line_count - 1) : 0;

  if (gtk_text_buffer_get_char_count (buffer) != 0) {
    boldify = TRUE;
  }

  gtk_text_buffer_get_end_iter (buffer, &iter);

  if (boldify) {
    mark = gtk_text_buffer_create_mark (buffer, NULL, &iter, TRUE);
  }

  for (i = 0; lines[i]; i++) {
    len = strlen (lines[i]);

    if (!g_utf8_validate (lines[i], len, NULL)) {
      converted = g_locale_to_utf8 (lines[i], (gssize) len, NULL, &len, NULL);
      gtk_text_buffer_insert (buffer, &iter, converted, len);
      g_free (converted);
    } else {
      gtk_text_buffer_insert (buffer, &iter, lines[i], strlen (lines[i]));
    }

    gtk_text_iter_forward_to_end (&iter);
    gtk_text_buffer_insert (buffer, &iter, "\n", 1);
    gtk_text_iter_forward_char (&iter);
  }

  if (boldify) {
    gtk_text_buffer_get_iter_at_mark (buffer, &start, mark);
    gtk_text_buffer_apply_tag_by_name (buffer, "bold", &start, &iter);
    gtk_text_buffer_delete_mark (buffer, mark);
  }
  filter_buffer (window, filter_start_line);

  if (priv->auto_scroll) {
    gtk_text_view_scroll_to_iter (GTK_TEXT_VIEW (priv->text_view),
                                  &iter, 0.0, FALSE, 0.0, 0.0);
  }

  paint_timestamps (buffer, old_line_count, new_days);

  logview_update_header (window, log, NULL);
  logview_loglist_update_lines (LOGVIEW_LOGLIST (priv->loglist), log);
}

static void
active_log_changed_cb (LogviewManager *manager,
                       LogviewLog *log,
                       LogviewLog *old_log,
                       gpointer data)
{
  LogviewWindow *window = data;
  LogviewWindowPrivate *priv = logview_window_get_instance_private (window);
  const char **lines;
  GtkTextBuffer *buffer;

  logview_clear_active_log_state (window, old_log);
  logview_update_header (window, log, NULL);

  lines = logview_log_get_cached_lines (log);
  buffer = gtk_text_buffer_new (priv->tag_table);

  if (lines != NULL) {
    int i;
    GtkTextIter iter;

    /* update the text view to show the current lines */
    gtk_text_buffer_get_end_iter (buffer, &iter);

    for (i = 0; lines[i]; i++) {
      gtk_text_buffer_insert (buffer, &iter, lines[i], strlen (lines[i]));
      gtk_text_iter_forward_to_end (&iter);
      gtk_text_buffer_insert (buffer, &iter, "\n", 1);
      gtk_text_iter_forward_char (&iter);
    }

    paint_timestamps (buffer, 1, logview_log_get_days_for_cached_lines (log));
  }

  priv->monitor_id =
    g_signal_connect (log, "log-changed",
                      G_CALLBACK (log_monitor_changed_cb), window);

  if (lines == NULL || logview_log_has_new_lines (log)) {
    /* read the new lines */
    logview_window_schedule_log_read (window, log);
  }

  /* we set the buffer to the view anyway;
   * if there are no lines it will be empty for the duration of the thread
   * and will help us to distinguish the two cases of the following if
   * cause in the callback.
   */
  gtk_text_view_set_buffer (GTK_TEXT_VIEW (priv->text_view), buffer);
  g_object_unref (buffer);
}

static void
font_changed_cb (LogviewPrefs *prefs,
                 const char *font_name,
                 gpointer user_data)
{
  LogviewWindow *window = user_data;

  logview_set_font (window, font_name);
}

/* adapted from GEdit */

static void
message_area_create_error_box (LogviewWindow *window,
                               GtkWidget *message_area)
{
  LogviewWindowPrivate *priv = logview_window_get_instance_private (window);
  GtkWidget *hbox_content;
  GtkWidget *image;
  GtkWidget *vbox;
  GtkWidget *primary_label;
  GtkWidget *secondary_label;
  
  hbox_content = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_show (hbox_content);

  image = gtk_image_new_from_icon_name ("dialog-error",
                                        GTK_ICON_SIZE_DIALOG);
  gtk_widget_show (image);
  gtk_box_pack_start (GTK_BOX (hbox_content), image, FALSE, FALSE, 0);
  gtk_misc_set_alignment (GTK_MISC (image), 0.5, 0);

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_show (vbox);
  gtk_box_pack_start (GTK_BOX (hbox_content), vbox, TRUE, TRUE, 0);

  primary_label = gtk_label_new (NULL);
  gtk_widget_show (primary_label);
  gtk_box_pack_start (GTK_BOX (vbox), primary_label, TRUE, TRUE, 0);
  gtk_label_set_use_markup (GTK_LABEL (primary_label), TRUE);
  gtk_label_set_line_wrap (GTK_LABEL (primary_label), TRUE);
  gtk_misc_set_alignment (GTK_MISC (primary_label), 0, 0.5);
  gtk_widget_set_can_focus (primary_label, TRUE);
  gtk_label_set_selectable (GTK_LABEL (primary_label), TRUE);

  priv->message_primary = primary_label;

  secondary_label = gtk_label_new (NULL);
  gtk_widget_show (secondary_label);
  gtk_box_pack_start (GTK_BOX (vbox), secondary_label, TRUE, TRUE, 0);
  gtk_widget_set_can_focus (secondary_label, TRUE);
  gtk_label_set_use_markup (GTK_LABEL (secondary_label), TRUE);
  gtk_label_set_line_wrap (GTK_LABEL (secondary_label), TRUE);
  gtk_label_set_selectable (GTK_LABEL (secondary_label), TRUE);
  gtk_misc_set_alignment (GTK_MISC (secondary_label), 0, 0.5);

  priv->message_secondary = secondary_label;

  gtk_container_add
      (GTK_CONTAINER (gtk_info_bar_get_content_area
                      (GTK_INFO_BAR (message_area))),
       hbox_content);
}

static void
message_area_set_labels (LogviewWindow *window,
                         const char *primary,
                         const char *secondary)
{
  LogviewWindowPrivate *priv = logview_window_get_instance_private (window);
  char *primary_markup, *secondary_markup;

  primary_markup = g_markup_printf_escaped ("<b>%s</b>", primary);
  secondary_markup = g_markup_printf_escaped ("<small>%s</small>",
                                              secondary);

  gtk_label_set_markup (GTK_LABEL (priv->message_primary),
                        primary_markup);
  gtk_label_set_markup (GTK_LABEL (priv->message_secondary),
                        secondary_markup);

  g_free (primary_markup);
  g_free (secondary_markup);
}

static void
message_area_response_cb (GtkInfoBar *message_area,
                          int response_id, gpointer user_data)
{
  gtk_widget_hide (GTK_WIDGET (message_area));

  g_signal_handlers_disconnect_by_func (message_area,
                                        message_area_response_cb,
                                        user_data);
}

static void
logview_window_finalize (GObject *object)
{
  LogviewWindow *logview = LOGVIEW_WINDOW (object);
  LogviewWindowPrivate *priv = logview_window_get_instance_private (logview);

  if (priv->read_cancellable != NULL) {
    g_cancellable_cancel (priv->read_cancellable);
    g_clear_object (&priv->read_cancellable);
  }

  g_clear_object (&priv->filters_placeholder);
  pango_font_description_free (priv->monospace_description);

  G_OBJECT_CLASS (logview_window_parent_class)->finalize (object);
}

static void
action_matches_only_change_state (GSimpleAction *action,
                                  GVariant *state,
                                  gpointer user_data)
{
  LogviewWindow *logview = user_data;
  LogviewWindowPrivate *priv = logview_window_get_instance_private (logview);

  priv->matches_only = g_variant_get_boolean (state);
  filter_buffer (logview, 0);

  g_simple_action_set_state (action, state);
}

static void
filter_manager_response_cb (GtkDialog *dialog, 
                            gint response,
                            LogviewWindow *logview)
{
  LogviewWindowPrivate *priv = logview_window_get_instance_private (logview);

  update_filter_menu (logview);

  g_list_free (priv->active_filters);
  priv->active_filters = NULL;
}

static void
action_filters_manage (GSimpleAction *action,
                       GVariant *parameter,
                       gpointer user_data)
{
  LogviewWindow *logview = user_data;
  GtkWidget *manager;

  manager = logview_filter_manager_new ();
  g_signal_connect (manager, "response", 
                    G_CALLBACK (filter_manager_response_cb), logview);

  gtk_window_set_transient_for (GTK_WINDOW (manager),
                                GTK_WINDOW (logview));
  gtk_widget_show (GTK_WIDGET (manager));
}

static void
action_zoom_in (GSimpleAction *action,
                GVariant *parameter,
                gpointer user_data)
{
  LogviewWindow *logview = user_data;
  LogviewWindowPrivate *priv = logview_window_get_instance_private (logview);

  priv->fontsize = MIN (priv->fontsize + 1, 24);
  logview_set_fontsize (logview, TRUE);
}	

static void
action_zoom_out (GSimpleAction *action,
                 GVariant *parameter,
                 gpointer user_data)
{
  LogviewWindow *logview = user_data;
  LogviewWindowPrivate *priv = logview_window_get_instance_private (logview);

  priv->fontsize = MAX (priv->fontsize - 1, 6);
  logview_set_fontsize (logview, TRUE);
}	

static void
action_zoom_normal (GSimpleAction *action,
                    GVariant *parameter,
                    gpointer user_data)
{
  LogviewWindow *logview = user_data;
  LogviewWindowPrivate *priv = logview_window_get_instance_private (logview);

  priv->fontsize = priv->original_fontsize;
  logview_set_fontsize (logview, TRUE);
}

static void
action_select_all (GSimpleAction *action,
                   GVariant *parameter,
                   gpointer user_data)
{
  LogviewWindow *logview = user_data;
  LogviewWindowPrivate *priv = logview_window_get_instance_private (logview);
  GtkTextIter start, end;
  GtkTextBuffer *buffer;

  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (priv->text_view));

  gtk_text_buffer_get_bounds (buffer, &start, &end);
  gtk_text_buffer_select_range (buffer, &start, &end);

  gtk_widget_grab_focus (GTK_WIDGET (priv->text_view));
}

static void
action_copy (GSimpleAction *action,
             GVariant *parameter,
             gpointer user_data)
{
  LogviewWindow *logview = user_data;
  LogviewWindowPrivate *priv = logview_window_get_instance_private (logview);
  GtkTextBuffer *buffer;
  GtkClipboard *clipboard;

  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (priv->text_view));
  clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);

  gtk_text_buffer_copy_clipboard (buffer, clipboard);

  gtk_widget_grab_focus (GTK_WIDGET (priv->text_view));
}

static void
action_close (GSimpleAction *action,
              GVariant *parameter,
              gpointer user_data)
{
  LogviewWindow *logview = user_data;
  LogviewWindowPrivate *priv = logview_window_get_instance_private (logview);

  logview_set_search_visible (logview, FALSE);
  logview_manager_close_active_log (priv->manager);
}

static void
open_file_selected_cb (GtkWidget *chooser,
                       gint response,
                       LogviewWindow *logview)
{
  LogviewWindowPrivate *priv = logview_window_get_instance_private (logview);
  GFile *f;
  char *file_uri;
  LogviewLog *log;

  gtk_widget_hide (GTK_WIDGET (chooser));
  if (response != GTK_RESPONSE_OK) {
	  return;
  }

  f = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (chooser));
  file_uri = g_file_get_uri (f);

  log = logview_manager_get_if_loaded (priv->manager, file_uri);

  g_free (file_uri);

  if (log) {
    logview_manager_set_active_log (priv->manager, log);
    g_object_unref (log);
    goto out;
  }

  logview_manager_add_log_from_gfile (priv->manager, f, TRUE);

out:
  g_object_unref (f);
}

static void
action_open (GSimpleAction *action,
             GVariant *parameter,
             gpointer user_data)
{
  LogviewWindow *logview = user_data;
  LogviewWindowPrivate *priv = logview_window_get_instance_private (logview);
  static GtkWidget *chooser = NULL;
  char *active;

  if (chooser == NULL) {
    chooser = gtk_file_chooser_dialog_new (_("Open Log"),
                                           GTK_WINDOW (logview),
                                           GTK_FILE_CHOOSER_ACTION_OPEN,
                                           _("_Cancel"), GTK_RESPONSE_CANCEL,
                                           _("_Open"), GTK_RESPONSE_OK,
                                           NULL);
    gtk_dialog_set_default_response (GTK_DIALOG (chooser), GTK_RESPONSE_OK);
    gtk_window_set_modal (GTK_WINDOW (chooser), TRUE);
    g_signal_connect (chooser, "response",
                      G_CALLBACK (open_file_selected_cb), logview);
    g_signal_connect (chooser, "destroy",
                      G_CALLBACK (gtk_widget_destroyed), &chooser);
    active = logview_prefs_get_active_logfile (priv->prefs);
    if (active != NULL) {
      gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (chooser), active);
      g_free (active);
    }
  }

  gtk_window_present (GTK_WINDOW (chooser));
}

static void
action_search_change_state (GSimpleAction *action,
                            GVariant *state,
                            gpointer user_data)
{
  LogviewWindow *logview = user_data;
  LogviewWindowPrivate *priv = logview_window_get_instance_private (logview);
  gboolean open = g_variant_get_boolean (state);
  LogviewLog *active_log = logview_manager_get_active_log (logview_manager_get ());

  gtk_revealer_set_reveal_child (GTK_REVEALER (priv->find_bar_revealer),
                                open);
  if (open) {
    gtk_widget_grab_focus (priv->find_bar);
  }

  logview_update_header (logview, active_log, NULL);

  g_simple_action_set_state (action, state);
}

static void
action_autoscroll_change_state (GSimpleAction *action,
                                GVariant *state,
                                gpointer user_data)
{
  LogviewWindow *logview = user_data;
  LogviewWindowPrivate *priv = logview_window_get_instance_private (logview);

  priv->auto_scroll = g_variant_get_boolean (state);
  g_simple_action_set_state (action, state);
}

static void
action_toggle (GSimpleAction *action,
               GVariant *parameter,
               gpointer user_data)
{
  GVariant *state;

  state = g_action_get_state (G_ACTION (action));
  g_action_change_state (G_ACTION (action), g_variant_new_boolean (!g_variant_get_boolean (state)));
  g_variant_unref (state);
}

static GActionEntry action_entries[] = {
  { "autoscroll", action_toggle, NULL, "true", action_autoscroll_change_state },
  { "search", action_toggle, NULL, "false", action_search_change_state },
  { "gear-menu", action_toggle, NULL, "false", NULL },
  { "open", action_open, NULL, NULL, NULL },
  { "close", action_close, NULL, NULL, NULL },
  { "copy", action_copy, NULL, NULL, NULL },
  { "select_all", action_select_all, NULL, NULL, NULL },
  { "zoom_in", action_zoom_in, NULL, NULL, NULL },
  { "zoom_out", action_zoom_out, NULL, NULL, NULL },
  { "zoom_normal", action_zoom_normal, NULL, NULL, NULL },
  { "filters_match", action_toggle, NULL, "false", action_matches_only_change_state },
  { "filters_manage", action_filters_manage, NULL, NULL, NULL }
};

static void
logview_window_init_actions (LogviewWindow *window)
{
  LogviewWindowPrivate *priv = logview_window_get_instance_private (window);

  g_action_map_add_action_entries (G_ACTION_MAP (window), action_entries,
                                   G_N_ELEMENTS (action_entries), window);
  priv->auto_scroll = TRUE;
}

static void
logview_window_init (LogviewWindow *window)
{
  gchar *monospace_font_name;
  LogviewWindowPrivate *priv;
  int width, height;
  GError *err = NULL;
  GMenuModel *menu;

  priv = logview_window_get_instance_private (window);
  priv->prefs = logview_prefs_get ();
  priv->manager = logview_manager_get ();
  priv->monitor_id = 0;

  logview_window_init_actions (window);
  gtk_widget_init_template (GTK_WIDGET (window));

  gtk_window_set_title (GTK_WINDOW (window), _("System Log"));

  logview_prefs_get_stored_window_size (priv->prefs, &width, &height);
  gtk_window_set_default_size (GTK_WINDOW (window), width, height);

  priv->find_bar = logview_findbar_new ();
  gtk_widget_show (priv->find_bar);
  gtk_container_add (GTK_CONTAINER (priv->find_bar_revealer), priv->find_bar);

  g_signal_connect (priv->find_bar, "previous",
                    G_CALLBACK (findbar_previous_cb), window);
  g_signal_connect (priv->find_bar, "next",
                    G_CALLBACK (findbar_next_cb), window);
  g_signal_connect (priv->find_bar, "text_changed",
                    G_CALLBACK (findbar_text_changed_cb), window);
  g_signal_connect (priv->find_bar, "close",
                    G_CALLBACK (findbar_close_cb), window);

  priv->loglist = logview_loglist_new ();
  gtk_widget_show (priv->loglist);
  gtk_container_add (GTK_CONTAINER (priv->sidebar_scrolledwindow), priv->loglist);

  g_signal_connect (priv->loglist, "day_selected",
                    G_CALLBACK (loglist_day_selected_cb), window);
  g_signal_connect (priv->loglist, "day_cleared",
                    G_CALLBACK (loglist_day_cleared_cb), window);

  /* second pane: log */
  message_area_create_error_box (window, priv->message_area);
  gtk_info_bar_add_button (GTK_INFO_BAR (priv->message_area),
                           _("_Close"), GTK_RESPONSE_CLOSE);

  priv->tag_table = gtk_text_tag_table_new ();
  populate_tag_table (priv->tag_table);

  populate_style_tag_table (window);

  /* use the desktop monospace font */
  monospace_font_name = logview_prefs_get_monospace_font_name (priv->prefs);
  logview_set_font (window, monospace_font_name);
  g_free (monospace_font_name);

  /* restore saved zoom */
  priv->fontsize = logview_prefs_get_stored_fontsize (priv->prefs);

  if (priv->fontsize > 0) {
    logview_set_fontsize (window, FALSE);
  }

  /* signal handlers
   * - first is used to remember/restore the window size on quit.
   */
  g_signal_connect (window, "configure_event",
                    G_CALLBACK (window_size_changed_cb), window);
  g_signal_connect (priv->prefs, "system-font-changed",
                    G_CALLBACK (font_changed_cb), window);
  g_signal_connect (priv->manager, "active-changed",
                    G_CALLBACK (active_log_changed_cb), window);

  update_filter_menu (window);
}

static void
logview_window_class_init (LogviewWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = logview_window_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/logview/logview-window.ui");
  gtk_widget_class_bind_template_child_private (widget_class, LogviewWindow, header_bar);
  gtk_widget_class_bind_template_child_private (widget_class, LogviewWindow, window_content);
  gtk_widget_class_bind_template_child_private (widget_class, LogviewWindow, filters_placeholder);
  gtk_widget_class_bind_template_child_private (widget_class, LogviewWindow, find_bar_revealer);
  gtk_widget_class_bind_template_child_private (widget_class, LogviewWindow, message_area);
  gtk_widget_class_bind_template_child_private (widget_class, LogviewWindow, sidebar_scrolledwindow);
  gtk_widget_class_bind_template_child_private (widget_class, LogviewWindow, message_area);
  gtk_widget_class_bind_template_child_private (widget_class, LogviewWindow, text_view);
}

/* public methods */

GtkWidget *
logview_window_new (GtkApplication *application)
{
  return g_object_new (LOGVIEW_TYPE_WINDOW, 
                       "application", application,
                       NULL);
}

void
logview_window_add_error (LogviewWindow *window,
                          const char *primary,
                          const char *secondary)
{
  LogviewWindowPrivate *priv;

  g_assert (LOGVIEW_IS_WINDOW (window));
  priv = logview_window_get_instance_private (window);

  message_area_set_labels (window,
                           primary, secondary); 

  gtk_widget_show (priv->message_area);

  g_signal_connect (priv->message_area, "response",
                    G_CALLBACK (message_area_response_cb), window);
}

void
logview_window_add_errors (LogviewWindow *window,
                           GPtrArray *errors)
{
  LogviewWindowPrivate *priv = logview_window_get_instance_private (window);
  char *primary, *secondary;
  GString *str;
  char **err;
  int i;

  g_assert (LOGVIEW_IS_WINDOW (window));
  g_assert (errors->len > 1);

  primary = g_strdup (_("Could not open the following files:"));
  str = g_string_new (NULL);

  for (i = 0; i < errors->len; i++) {
    err = (char **) g_ptr_array_index (errors, i);
    g_string_append (str, err[0]);
    g_string_append (str, ": ");
    g_string_append (str, err[1]);
    g_string_append (str, "\n");
  }

  secondary = g_string_free (str, FALSE);

  message_area_set_labels (window, primary, secondary);

  gtk_widget_show (priv->message_area);

  g_signal_connect (priv->message_area, "response",
                    G_CALLBACK (message_area_response_cb), window);

  g_free (primary);
  g_free (secondary);
}
