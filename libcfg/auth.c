/* This file is part of GNU Mailutils
   Copyright (C) 2007 Free Software Foundation, Inc.

   GNU Mailutils is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 3, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "libcfg.h"
#include <mailutils/mu_auth.h>
#include <string.h>

/* FIXME: mu_auth.c should be reviewed */


/* ************************************************************************* */
/* Resource-style configuration                                              */
/* ************************************************************************* */
static int
cb_authentication (mu_cfg_locus_t *locus, void *data, char *arg)
{
  if (strcmp (arg, "clear") == 0)
    mu_authentication_clear_list ();
  else
    mu_authentication_add_module_list (arg);/*FIXME: error reporting*/
  return 0;
}

static int
cb_authorization (mu_cfg_locus_t *locus, void *data, char *arg)
{
  if (strcmp (arg, "clear") == 0)
    mu_authorization_clear_list ();
  else
    mu_authorization_add_module_list (arg);
  return 0;
}

static struct mu_cfg_param mu_auth_param[] = {
  { "authentication", mu_cfg_callback, NULL, cb_authentication },
  { "authorization", mu_cfg_callback, NULL, cb_authorization },
  { NULL }
};

/* FIXME: init? */
