/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2; -*- */
/* logview-main.c - logview main
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

#include "config.h"

#include <stdlib.h>

#include <glib/gi18n.h>
#include <glib.h>

#include <gtk/gtk.h>

#include "logview-app.h"

int
main (int argc, char *argv[])
{
  LogviewApp *app;
  gint result;

  bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  app = logview_app_new ();
  result = g_application_run (G_APPLICATION (app), argc, argv);
  g_object_unref (app);

  return result;
}
