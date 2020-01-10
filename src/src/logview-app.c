/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2; -*- */
/* logview-app.c - logview application singleton
 *
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

/* logview-app.c */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>
#include <stdlib.h>

#include "logview-app.h"

#include "logview-about.h"
#include "logview-manager.h"
#include "logview-window.h"
#include "logview-prefs.h"

struct _LogviewAppPrivate {
  LogviewPrefs *prefs;
  LogviewManager *manager;

  GtkWidget *window;
};

G_DEFINE_TYPE (LogviewApp, logview_app, GTK_TYPE_APPLICATION);

#define GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), LOGVIEW_TYPE_APP, LogviewAppPrivate))

typedef struct {
  LogviewApp *app;
  GSList *logs;
} EnumerateJob;

/* TODO: ideally we should parse configuration files in /etc/logrotate.conf
 * and all the files in /etc/logrotate.d/ and group all the logs referring
 * to the same entry under a category. Right now, we just do some
 * parsing instead, and fill with quasi-sensible defaults.
 */

/* adapted from sysklogd sources */
static GSList*
parse_syslog ()
{
  char cbuf[BUFSIZ];
  char *cline, *p;
  FILE *cf;
  GSList *logfiles = NULL;

  if ((cf = fopen ("/etc/syslog.conf", "r")) == NULL) {
    return NULL;
  }

  cline = cbuf;
  while (fgets (cline, sizeof (cbuf) - (cline - cbuf), cf) != NULL) {
    gchar **list;
    gint i;

    for (p = cline; g_ascii_isspace (*p); ++p);
    if (*p == '\0' || *p == '#' || *p == '\n')
      continue;

    list = g_strsplit_set (p, ", -\t()\n", 0);

    for (i = 0; list[i]; ++i) {
      if (*list[i] == '/' &&
          g_slist_find_custom (logfiles, list[i],
                               (GCompareFunc) g_ascii_strcasecmp) == NULL)
      {
        logfiles = g_slist_insert (logfiles,
                                   g_strdup (list[i]), 0);
      }
    }

    g_strfreev (list);
  }

  fclose (cf);

  return logfiles;
}

static void
enumerate_job_finish (EnumerateJob *job)
{
  GSList *files = job->logs;
  LogviewApp *app = job->app;

  logview_manager_add_logs_from_name_list (app->priv->manager, files, files->data);

  g_slist_foreach (files, (GFunc) g_free, NULL);
  g_slist_free (files);

  g_object_unref (job->app);
  g_slice_free (EnumerateJob, job);
}

static void
enumerate_next_files_async_cb (GObject *source,
                               GAsyncResult *res,
                               gpointer user_data)
{
  EnumerateJob *job = user_data;
  GList *enumerated_files, *l;
  GFileInfo *info;
  GSList *logs;
  const char *content_type, *name;
  char *parse_string, *container_path;
  GFileType type;
  GFile *container;

  enumerated_files = g_file_enumerator_next_files_finish (G_FILE_ENUMERATOR (source),
                                                          res, NULL);
  if (!enumerated_files) {
    enumerate_job_finish (job);
    return;
  }

  logs = job->logs;
  container = g_file_enumerator_get_container (G_FILE_ENUMERATOR (source));
  container_path = g_file_get_path (container);

  /* TODO: we don't support grouping rotated logs yet, skip gzipped files
   * and those which name contains a formatted date.
   */
  for (l = enumerated_files; l; l = l->next) {
    info = l->data;
    type = g_file_info_get_file_type (info);
    content_type = g_file_info_get_content_type (info);
    name = g_file_info_get_name (info);
    
    if (!g_file_info_get_attribute_boolean (info, "access::can-read")) {
      g_object_unref (info);
      continue;
    }

    if (type != (G_FILE_TYPE_REGULAR || G_FILE_TYPE_SYMBOLIC_LINK) ||
        !g_content_type_is_a (content_type, "text/plain"))
    {
      g_object_unref (info);
      continue;
    }

    if (g_content_type_is_a (content_type, "application/x-gzip")) {
      g_object_unref (info);
      continue;
    }

    if (g_regex_match_simple ("\\d{8}$", name, 0, 0)) {
      g_object_unref (info);
      continue;
    }

    parse_string = g_build_filename (container_path, name, NULL);

    if (g_slist_find_custom (logs, parse_string, (GCompareFunc) g_ascii_strcasecmp) == NULL) {
      logs = g_slist_append (logs, parse_string);
    } else {
      g_free (parse_string);
    }

    g_object_unref (info);
    parse_string = NULL;
  }

  g_list_free (enumerated_files);
  g_object_unref (container);
  g_free (container_path);

  job->logs = logs;

  enumerate_job_finish (job);
}

static void
enumerate_children_async_cb (GObject *source,
                             GAsyncResult *res,
                             gpointer user_data)
{
  EnumerateJob *job = user_data;
  GFileEnumerator *enumerator;

  enumerator = g_file_enumerate_children_finish (G_FILE (source),
                                                 res, NULL);
  if (!enumerator) {
    enumerate_job_finish (job);
    return;
  }

  g_file_enumerator_next_files_async (enumerator, G_MAXINT,
                                      G_PRIORITY_DEFAULT,
                                      NULL, enumerate_next_files_async_cb, job);  
}

static void
logview_app_first_time_initialize (LogviewApp *app)
{
  GSList *logs;
  GFile *log_dir;
  EnumerateJob *job;

  /* let's add all accessible files in /var/log and those mentioned
   * in /etc/syslog.conf.
   */

  logs = parse_syslog ();

  job = g_slice_new0 (EnumerateJob);
  job->app = g_object_ref (app);
  job->logs = logs;

  log_dir = g_file_new_for_path ("/var/log/");
  g_file_enumerate_children_async (log_dir,
                                   "standard::*,access::can-read", 0,
                                   G_PRIORITY_DEFAULT, NULL,
                                   enumerate_children_async_cb, job);

  g_object_unref (log_dir);
}

static void
do_finalize (GObject *obj)
{
  LogviewApp *app = LOGVIEW_APP (obj);

  g_object_unref (app->priv->manager);
  g_object_unref (app->priv->prefs);

  G_OBJECT_CLASS (logview_app_parent_class)->finalize (obj);
}

static void
logview_app_activate (GApplication *application)
{
  LogviewApp *app = LOGVIEW_APP (application);
  char *active_log;
  gchar **logs;

  G_APPLICATION_CLASS (logview_app_parent_class)->activate (application);

  logs = logview_prefs_get_stored_logfiles (app->priv->prefs);

  if (!logs || !logs[0]) {
    logview_app_first_time_initialize (app);
  } else {
    active_log = logview_prefs_get_active_logfile (app->priv->prefs);
    logview_manager_add_logs_from_names (app->priv->manager, logs, active_log);
    g_free (active_log);
  }

  g_strfreev (logs);

  gtk_widget_show (app->priv->window);
}

static void
action_help (GSimpleAction *action,
             GVariant *parameter,
             gpointer user_data)
{
  LogviewApp *app = user_data;
  GError *error = NULL;

  gtk_show_uri (gtk_widget_get_screen (app->priv->window),
                "help:gnome-system-log", gtk_get_current_event_time (),
                &error);

  if (error) {
    g_warning (_("There was an error displaying help: %s"), error->message);
    g_error_free (error);
  }
}

static void
action_about (GSimpleAction *action,
              GVariant *parameter,
              gpointer user_data)
{
  LogviewApp *app = user_data;

  char *license_trans = g_strjoin ("\n\n", _(logview_about_license[0]),
                                   _(logview_about_license[1]),
                                   _(logview_about_license[2]), NULL);

  gtk_show_about_dialog (GTK_WINDOW (app->priv->window),
                         "program-name",  _("System Log"),
                         "version", VERSION,
                         "copyright", "Copyright \xc2\xa9 1998-2008 Free Software Foundation, Inc.",
                         "license", license_trans,
                         "wrap-license", TRUE,
                         "comments", _("A system log viewer for GNOME."),
                         "authors", logview_about_authors,
                         "documenters", logview_about_documenters,
                         "translator_credits", strcmp (logview_about_translator_credits,
                                                       "translator-credits") != 0 ?
                                               logview_about_translator_credits : NULL,
                         "logo_icon_name", "logview",
                         NULL);
  g_free (license_trans);
}

static void
action_quit (GSimpleAction *action,
             GVariant *parameter,
             gpointer user_data)
{
  LogviewApp *app = user_data;
  gtk_widget_destroy (app->priv->window);
}

static GActionEntry action_entries[] = {
  { "about", action_about, NULL, NULL, NULL },
  { "help", action_help, NULL, NULL, NULL },
  { "quit", action_quit, NULL, NULL, NULL }
};

#define SIMPLE_ACCEL(app, accel, action) \
  (gtk_application_add_accelerator (GTK_APPLICATION (app), accel, action, NULL))

static void
logview_app_init_actions (LogviewApp *app)
{
  GtkBuilder *builder = gtk_builder_new ();
  GMenuModel *app_menu;

  g_action_map_add_action_entries (G_ACTION_MAP (app), action_entries,
                                   G_N_ELEMENTS (action_entries), app);

  gtk_builder_add_from_resource (builder, "/org/gnome/logview/logview-app-menu.ui", NULL);
  app_menu = G_MENU_MODEL (gtk_builder_get_object (builder, "app-menu"));
  gtk_application_set_app_menu (GTK_APPLICATION (app), app_menu);

  /* menu accel */
  SIMPLE_ACCEL (app, "F10", "win.gear-menu");

  /* action accels */
  SIMPLE_ACCEL (app, "<Control>f", "win.search");
  SIMPLE_ACCEL (app, "<Control>o", "win.open");
  SIMPLE_ACCEL (app, "<Control>w", "win.close");
  SIMPLE_ACCEL (app, "<Control>c", "win.copy");
  SIMPLE_ACCEL (app, "<Control>a", "win.select_all");
  SIMPLE_ACCEL (app, "<Control>plus", "win.zoom_in");
  SIMPLE_ACCEL (app, "<Control>minus", "win.zoom_out");
  SIMPLE_ACCEL (app, "<Control>0", "win.zoom_normal");

  /* additional zoom accels */
  SIMPLE_ACCEL (app, "<Control>KP_Add", "win.zoom_in");
  SIMPLE_ACCEL (app, "<Control>KP_Subtract", "win.zoom_out");
  SIMPLE_ACCEL (app, "<Control>KP_0", "win.zoom_normal");

  g_object_unref (builder);
  g_object_unref (app_menu);
}

static gboolean
logview_app_local_command_line (GApplication *application,
                                gchar ***arguments,
                                gint *exit_status)
{
  gchar **argv;
  gint argc, idx, len = 0;
  gchar **remaining = NULL;
  gboolean version = FALSE;
  GError *error = NULL;
  GFile **files = NULL;

  GOptionContext *context;
  const GOptionEntry entries[] = {
    { "version", '\0', 0, G_OPTION_ARG_NONE, &version,
      N_("Show the version of the program."), NULL },
    { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &remaining, NULL,  N_("[URI...]") },
    { NULL }
  };

  *exit_status = EXIT_SUCCESS;

  context = g_option_context_new (_("A system log viewer for GNOME."));
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_add_group (context, gtk_get_option_group (FALSE));

  argv = *arguments;
  argc = g_strv_length (argv);

  if (!g_option_context_parse (context, &argc, &argv, &error)) {
    /* Translators: this is a fatal error quit message printed on the
     * command line */
    g_printerr ("%s: %s\n", _("Could not parse arguments"), error->message);
    g_error_free (error);

    *exit_status = EXIT_FAILURE;
    goto out;
  }

  if (version) {
    g_print ("GNOME System Log " PACKAGE_VERSION "\n");
    goto out;
  }

  g_application_register (application, NULL, &error);

  if (error != NULL) {
    /* Translators: this is a fatal error quit message printed on the
     * command line */
    g_printerr ("%s: %s\n", _("Could not register the application"), error->message);
    g_error_free (error);

    *exit_status = EXIT_FAILURE;
    goto out;
  }

  /* Convert args to GFiles */
  if (remaining != NULL) {
    GFile *file;
    GPtrArray *file_array;

    file_array = g_ptr_array_new ();

    for (idx = 0; remaining[idx] != NULL; idx++) {
      file = g_file_new_for_commandline_arg (remaining[idx]);
      if (file != NULL)
        g_ptr_array_add (file_array, file);
    }

    len = file_array->len;
    files = (GFile **) g_ptr_array_free (file_array, FALSE);
    g_strfreev (remaining);
  }

  if (len > 0)
    g_application_open (application, files, len, "");
  else
    g_application_activate (application);

  for (idx = 0; idx < len; idx++)
    g_object_unref (files[idx]);
  g_free (files);

 out:
  g_option_context_free (context);

  return TRUE;
}

static void
logview_app_startup (GApplication *application)
{
  LogviewApp *app = LOGVIEW_APP (application);

  G_APPLICATION_CLASS (logview_app_parent_class)->startup (application);

  app->priv->window = logview_window_new (GTK_APPLICATION (app));
  logview_app_init_actions (app);
}

static void
logview_app_open (GApplication *application,
                  GFile       **files,
                  gint          n_files,
                  const gchar  *hint)
{
  LogviewApp *app = LOGVIEW_APP (application);
  gchar **logs;
  gint idx;

  logs = g_malloc0 ((n_files + 1) * sizeof (gchar *));
  for (idx = 0; idx < n_files; idx++)
    logs[idx] = g_file_get_path (files[idx]);

  logs[n_files] = NULL;
  logview_manager_add_logs_from_names (app->priv->manager, logs, logs[0]);
  g_strfreev (logs);

  gtk_widget_show (app->priv->window);
}

static void
logview_app_class_init (LogviewAppClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);
  GApplicationClass *aclass = G_APPLICATION_CLASS (klass);

  oclass->finalize = do_finalize;

  aclass->activate = logview_app_activate;
  aclass->open = logview_app_open;
  aclass->startup = logview_app_startup;
  aclass->local_command_line = logview_app_local_command_line;

  g_type_class_add_private (klass, sizeof (LogviewAppPrivate));
}

static void
logview_app_init (LogviewApp *self)
{
  LogviewAppPrivate *priv = self->priv = GET_PRIVATE (self);

  priv->prefs = logview_prefs_get ();
  priv->manager = logview_manager_get ();
}

void
logview_app_add_error (LogviewApp *app,
                       const char *file_path,
                       const char *secondary)
{
  LogviewWindow *window;
  char *primary;

  g_assert (LOGVIEW_IS_APP (app));

  window = LOGVIEW_WINDOW (app->priv->window);
  primary = g_strdup_printf (_("Impossible to open the file %s"), file_path);

  logview_window_add_error (window, primary, secondary);

  g_free (primary);
}

static void
check_error_prefs (gpointer data,
                   gpointer user_data)
{
  gchar **strings = data;
  LogviewApp *app = user_data;
  GFile *file = g_file_new_for_path (strings[0]);

  logview_prefs_remove_stored_log (app->priv->prefs, file);
  g_object_unref (file);
}

void
logview_app_add_errors (LogviewApp *app,
                        GPtrArray *errors)
{
  LogviewWindow *window;

  g_assert (LOGVIEW_IS_APP (app));

  window = LOGVIEW_WINDOW (app->priv->window);

  if (errors->len == 0) {
    return;
  }

  g_ptr_array_foreach (errors, check_error_prefs, app);

  if (errors->len == 1) {
    char **err;

    err = g_ptr_array_index (errors, 0);
    logview_window_add_error (window, err[0], err[1]);
  } else {
    logview_window_add_errors (window, errors);
  }
}

LogviewApp *
logview_app_new (void)
{
  return g_object_new (LOGVIEW_TYPE_APP,
                       "application-id", "org.gnome.Logview",
                       "flags", G_APPLICATION_HANDLES_OPEN,
                       NULL);
}
