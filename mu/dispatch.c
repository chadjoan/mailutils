/* GNU Mailutils -- a suite of utilities for electronic mail
   Copyright (C) 2010-2012, 2014-2016 Free Software Foundation, Inc.

   GNU Mailutils is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   GNU Mailutils is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GNU Mailutils.  If not, see <http://www.gnu.org/licenses/>. */

#if defined(HAVE_CONFIG_H)
# include <config.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <mailutils/alloc.h>
#include <mailutils/stream.h>
#include <mailutils/stdstream.h>
#include <mailutils/nls.h>
#include "mu.h"
#include "mu-setup.h"

struct mutool_action_tab
{
  const char *name;
  mutool_action_t action;
  const char *docstring;
};

struct mutool_action_tab mutool_action_tab[] = {
#include "mu-setup.c"
  { NULL }
};

mutool_action_t
dispatch_find_action (const char *name)
{
  struct mutool_action_tab *p;

  for (p = mutool_action_tab; p->name; p++)
    if (strcmp (p->name, name) == 0)
      return p->action;
  return NULL;
}

void
subcommand_help (mu_stream_t str)
{
  struct mutool_action_tab *p;
  unsigned margin;

  mu_stream_printf (str, "%s\n\n", _("Commands are:"));
  for (p = mutool_action_tab; p->name; p++)
    {
      margin = 2;
      mu_stream_ioctl (str, MU_IOCTL_WORDWRAPSTREAM,
		       MU_IOCTL_WORDWRAP_SET_MARGIN,
		       &margin);
      mu_stream_printf (str, "%s %s",  mu_program_name, p->name);
      margin = 29;
      mu_stream_ioctl (str, MU_IOCTL_WORDWRAPSTREAM,
		       MU_IOCTL_WORDWRAP_SET_MARGIN,
		       &margin);
      mu_stream_printf (str, "%s", gettext (p->docstring));
    }
  margin = 0;
  mu_stream_ioctl (str, MU_IOCTL_WORDWRAPSTREAM,
		   MU_IOCTL_WORDWRAP_SET_MARGIN,
		   &margin);
  mu_stream_printf (str,
		      _("\nTry `%s COMMAND --help' to get help on a particular "
		      "COMMAND.\n\n"),
		      mu_program_name);
  mu_stream_printf (str, "%s\n", _("OPTIONs are:"));
}
    
