/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright 2011 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street - Suite 500, Boston, MA
 * 02110-1335, USA.
 */

#include "config.h"

#include <stdlib.h>

#include <meta/main.h>
#include <meta/util.h>
#include "meta-plugin-manager.h"

#include <glib.h>

static gboolean
print_version (const gchar    *option_name,
               const gchar    *value,
               gpointer        data,
               GError        **error)
{
  const int latest_year = 2011;

  g_print (_("muffin %s\n"
             "Copyright (C) 2001-%d Havoc Pennington, Red Hat, Inc., and others\n"
             "This is free software; see the source for copying conditions.\n"
             "There is NO warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n"),
           VERSION, latest_year);
  exit (0);
}

static gchar *plugin = "default";

GOptionEntry muffin_options[] = {
  {
    "version", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
    print_version,
    N_("Print version"),
    NULL
  },
  {
    "muffin-plugin", 0, 0, G_OPTION_ARG_STRING,
    &plugin,
    N_("Muffin plugin to use"),
    "PLUGINS"
  },
  { NULL }
};

int
main (int argc, char **argv)
{
  GOptionContext *ctx;
  GError *error = NULL;

  g_setenv ("CLUTTER_BACKEND", "x11", TRUE);

  ctx = meta_get_option_context ();
  g_option_context_add_main_entries (ctx, muffin_options, GETTEXT_PACKAGE);
  if (!g_option_context_parse (ctx, &argc, &argv, &error))
    {
      g_printerr ("muffin: %s\n", error->message);
      exit (1);
    }

  if (plugin)
    meta_plugin_manager_load (plugin);

  meta_init ();
  return meta_run ();
}
