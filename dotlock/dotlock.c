/* GNU Mailutils -- a suite of utilities for electronic mail
   Copyright (C) 1999-2002, 2005, 2007-2012, 2014-2016 Free Software
   Foundation, Inc.

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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdlib.h>
#include <unistd.h>

#include <mailutils/errno.h>
#include <mailutils/locker.h>
#include <mailutils/nls.h>
#include "mailutils/cli.h"

static const char *file;
static int unlock;
static int flags;
static int retries;
static unsigned force;
static int debug;

static void
cli_force (struct mu_parseopt *po, struct mu_option *opt, char const *arg)
{
  if (arg)
    {
      int rc;
      char *errmsg;
      rc = mu_str_to_c (arg, opt->opt_type, opt->opt_ptr, &errmsg);
      if (rc)
	{
	  if (opt->opt_long)
	    mu_parseopt_error (po, "--%s: %s", opt->opt_long,
			       errmsg ? errmsg : mu_strerror (rc));
	  else
	    mu_parseopt_error (po, "-%c: %s", opt->opt_short,
			       errmsg ? errmsg : mu_strerror (rc));
	  free (errmsg);
	  exit (po->po_exit_error);
	}
    }
  else
    *(unsigned*)opt->opt_ptr = 1;
}

static struct mu_option dotlock_options[] = {
  { "unlock", 'u', NULL, MU_OPTION_DEFAULT,
    N_("unlock"),
    mu_c_bool, &unlock },

  { "force",  'f', N_("MINUTES"), MU_OPTION_ARG_OPTIONAL,
    N_("forcibly break an existing lock older than a certain time"),
    mu_c_uint, &force, cli_force },
 
  { "retry",  'r', N_("RETRIES"), MU_OPTION_ARG_OPTIONAL,
    N_("retry the lock a few times"),
    mu_c_int, &retries },

  { "debug",  'd', NULL, MU_OPTION_DEFAULT,
    N_("print details of failure reasons to stderr"), 
    mu_c_bool, &debug },

  MU_OPTION_END
}, *options[] = { dotlock_options, NULL };

struct mu_cfg_param dotlock_cfg_param[] = {
  { "force", mu_c_time, &force, 0, NULL,
    N_("Forcibly break an existing lock older than the specified time.") },
  { "retry", mu_c_int, &retries, 0, NULL,
    N_("Number of times to retry acquiring the lock.") },
  { "debug", mu_c_bool, &debug, 0, NULL,
    N_("Print details of failure reasons to stderr.") },
  { NULL }
};

static struct mu_cli_setup cli = {
  options,
  dotlock_cfg_param,
  N_("GNU dotlock -- lock mail spool files."),
  N_("FILE"),
  NULL,
  N_("Returns 0 on success, 3 if locking the file fails because\
 it's already locked, and 1 if some other kind of error occurred."),
  MU_DL_EX_ERROR,
  MU_DL_EX_ERROR
};



char *capa[] = {
  "debug",
  NULL
};

int
main (int argc, char *argv[])
{
  mu_locker_t locker = 0;
  int err = 0;
  pid_t usergid = getgid ();
  pid_t mailgid = getegid ();

  /* Native Language Support */
  MU_APP_INIT_NLS ();

  /* Drop permissions during argument parsing. */

  if (setegid (usergid) < 0)
    return MU_DL_EX_ERROR;

  mu_cli (argc, argv, &cli, capa, NULL, &argc, &argv);

  switch (argc)
    {
    case 0:
      mu_error (_("FILE must be specified"));
      exit (MU_DL_EX_ERROR);

    case 1:
      file = argv[0];
      break;

    default:
      mu_error (_("only one FILE can be specified"));
    }
  
  if (force)
    {
      force *= 60;
      flags |= MU_LOCKER_TIME;
    }

  if (retries != 0)
    flags |= MU_LOCKER_RETRY;
  
  if ((err = mu_locker_create (&locker, file, flags)))
    {
      if (debug)
	mu_diag_funcall (MU_DIAG_ERROR, "mu_locker_create", NULL, err);
      return MU_DL_EX_ERROR;
    }

  if (force != 0)
    mu_locker_set_expire_time (locker, force);

  if (retries != 0)
    mu_locker_set_retries (locker, retries);

  if (setegid (mailgid) < 0)
    return MU_DL_EX_ERROR;

  if (unlock)
    err = mu_locker_remove_lock (locker);
  else
    err = mu_locker_lock (locker);

  setegid(usergid);

  mu_locker_destroy (&locker);

  if (debug && err)
    mu_error (unlock ? _("unlocking the file %s failed: %s") :
	      _("locking the file %s failed: %s"),
	      file, mu_strerror (err));

  switch (err)
    {
    case 0:
      err = MU_DL_EX_OK;
      break;
    case EPERM:
      err = MU_DL_EX_PERM;
      break;
    case MU_ERR_LOCK_NOT_HELD:
      err = MU_DL_EX_NEXIST;
      break;
    case MU_ERR_LOCK_CONFLICT:
      err = MU_DL_EX_EXIST;
      break;
    default:
      err = MU_DL_EX_ERROR;
      break;
    }

  return err;
}

