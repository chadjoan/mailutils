/* GNU Mailutils -- a suite of utilities for electronic mail
   Copyright (C) 2007 Free Software Foundation, Inc.

   GNU Mailutils is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   GNU Mailutils is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GNU Mailutils; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
   MA 02110-1301 USA */

#include "maidag.h"

int multiple_delivery;     /* Don't return errors when delivering to multiple
			      recipients */
int ex_quota_tempfail;     /* Return temporary failure if mailbox quota is
			      exceeded. If this variable is not set, maidag
			      will return "service unavailable" */
int exit_code = EX_OK;     /* Exit code to be used */
uid_t current_uid;         /* Current user id */

char *quotadbname = NULL;  /* Name of mailbox quota database */
char *quota_query = NULL;  /* SQL query to retrieve mailbox quota */

char *sender_address = NULL;       
char *progfile_pattern = NULL;
char *sieve_pattern = NULL;

int log_to_stderr = -1;

/* Debuggig options */
int debug_level;           /* General debugging level */ 
int debug_flags;           /* Mailutils debugging flags */
int sieve_debug_flags;     /* Sieve debugging flags */
int sieve_enable_log;      /* Enables logging of executed Sieve actions */
char *message_id_header;   /* Use the value of this header as message
			      identifier when logging Sieve actions */
mu_debug_t mudebug;        /* Mailutils debugging object */

/* For LMTP mode */
int lmtp_mode;
char *lmtp_url_string;
int reuse_lmtp_address = 1;
char *lmtp_group = "mail";

struct daemon_param daemon_param = {
  MODE_INTERACTIVE,     /* Start in interactive (inetd) mode */
  20,                   /* Default maximum number of children */
  0,                    /* No standard port */
  600,                  /* Idle timeout */
  0,                    /* No transcript by default */
  NULL                  /* No PID file by default */
};

const char *program_version = "maidag (" PACKAGE_STRING ")";
static char doc[] =
N_("GNU maildag -- the mail delivery agent")
"\v"
N_("Debug flags are:\n\
  g - guimb stack traces\n\
  T - mailutils traces (MU_DEBUG_TRACE)\n\
  P - network protocols (MU_DEBUG_PROT)\n\
  t - sieve trace (MU_SIEVE_DEBUG_TRACE)\n\
  i - sieve instructions trace (MU_SIEVE_DEBUG_INSTR)\n\
  l - sieve action logs\n\
  0-9 - Set mail.local debugging level\n");

static char args_doc[] = N_("[recipient...]");

#define STDERR_OPTION 256
#define MESSAGE_ID_HEADER_OPTION 257
#define LMTP_OPTION 258

static struct argp_option options[] = 
{
  { "from", 'f', N_("EMAIL"), 0,
    N_("Specify the sender's name") },
  { NULL, 'r', NULL, OPTION_ALIAS, NULL },
  { "sieve", 'S', N_("PATTERN"), 0,
    N_("Set name pattern for user-defined Sieve mail filters"), 0 },
  { "message-id-header", MESSAGE_ID_HEADER_OPTION, N_("STRING"), 0,
    N_("Identify messages by the value of this header when logging Sieve actions"), 0 },
#ifdef WITH_GUILE
  { "source", 's', N_("PATTERN"), 0,
    N_("Set name pattern for user-defined Scheme mail filters"), 0 },
#endif
  { "lmtp", LMTP_OPTION, N_("URL"), OPTION_ARG_OPTIONAL,
    N_("Operate in LMTP mode"), 0 },
  { "debug", 'x', N_("FLAGS"), 0,
    N_("Enable debugging"), 0 },
  { "stderr", STDERR_OPTION, NULL, 0,
    N_("Log to standard error"), 0 },
  { NULL,      0, NULL, 0, NULL, 0 }
};

static error_t parse_opt (int key, char *arg, struct argp_state *state);

static struct argp argp = {
  options,
  parse_opt,
  args_doc, 
  doc,
  NULL,
  NULL, NULL
};

static const char *argp_capa[] = {
  "daemon",
  "auth",
  "common",
  "license",
  "logging",
  "mailbox",
  "mailer",
  "sieve",
  NULL
};

#define D_DEFAULT "9,s"

static void
set_debug_flags (const mu_cfg_locus_t *locus, const char *arg)
{
  while (*arg)
    {
      if (isdigit (*arg))
	debug_level = strtoul (arg, (char**)&arg, 10);
      else
	for (; *arg && *arg != ','; arg++)
	  {
	    switch (*arg)
	      {
	      case 'g':
#ifdef WITH_GUILE
		debug_guile = 1;
#endif
		break;

	      case 't':
		sieve_debug_flags |= MU_SIEVE_DEBUG_TRACE;
		break;
	  
	      case 'i':
		sieve_debug_flags |= MU_SIEVE_DEBUG_INSTR;
		break;
	  
	      case 'l':
		sieve_enable_log = 1;
		break;
	  
	      case 'T':
		debug_flags |= MU_DEBUG_TRACE;
		break;
	  
	      case 'P':
		debug_flags |= MU_DEBUG_PROT;
		break;

	      default:
		if (locus)
		  mu_error (_("%s:%d: %c is not a valid debug flag"),
			    locus->file, locus->line, *arg);
		else
		  mu_error (_("%c is not a valid debug flag"), *arg);
		break;
	      }
	  }
      if (*arg == ',')
	arg++;
      else if (locus)
	mu_error (_("%s:%d: expected comma, but found %c"),
		  locus->file, locus->line, *arg);
      else
	mu_error (_("expected comma, but found %c"), *arg);
    }
}

static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  switch (key)
    {
    case ARGP_KEY_INIT:
      state->child_inputs[0] = state->input;
      break;

    case MESSAGE_ID_HEADER_OPTION:
      message_id_header = arg;
      break;

    case LMTP_OPTION:
      lmtp_mode = 1;
      lmtp_url_string = arg;
      break;

    case 'r':
    case 'f':
      if (sender_address != NULL)
	{
	  argp_error (state, _("Multiple --from options"));
	  return EX_USAGE;
	}
      sender_address = arg;
      break;
      
#ifdef WITH_GUILE	
    case 's':
      progfile_pattern = arg;
      break;
#endif

    case 'S':
      sieve_pattern = arg;
      break;
      
    case 'x':
      set_debug_flags (NULL, arg ? arg : D_DEFAULT);
      break;

    case STDERR_OPTION:
      log_to_stderr = 1;
      break;
      
    default:
      return ARGP_ERR_UNKNOWN;

    case ARGP_KEY_ERROR:
      exit (EX_USAGE);
    }
  return 0;
}



static int
cb_debug (mu_cfg_locus_t *locus, void *data, char *arg)
{
  set_debug_flags (locus, arg);
  return 0;
}

struct mu_cfg_param maidag_cfg_param[] = {
  { "exit-multiple-delivery-success", mu_cfg_bool, &multiple_delivery },
  { "exit-quota-tempfail", mu_cfg_bool, &ex_quota_tempfail },
#ifdef USE_DBM
  { "quota-db", mu_cfg_string, &quotadbname },
#endif
#ifdef USE_SQL
  { "quota-query", mu_cfg_string, &quota_query },
#endif
  { "sieve", mu_cfg_string, &sieve_pattern },
  { "message-id-header", mu_cfg_string, &message_id_header },
#ifdef WITH_GUILE
  { "source", mu_cfg_string, &progfile_pattern },
#endif
  { "debug", mu_cfg_callback, NULL, cb_debug },
  { "stderr", mu_cfg_bool, &log_to_stderr },
/* LMTP support */
  { "lmtp", mu_cfg_bool, &lmtp_mode },
  { "group", mu_cfg_string, &lmtp_group },
  { "listen", mu_cfg_string, &lmtp_url_string },
  { "reuse-address", mu_cfg_bool, &reuse_lmtp_address },
  { NULL }
};


/* Logging */
static int
_mu_debug_printer (mu_debug_t unused, size_t level, const char *fmt,
		   va_list ap)
{
  if (log_to_stderr)
    {
      fprintf (stderr, "%s: ", (level == MU_DEBUG_ERROR) ? "ERROR" : "DEBUG");
      vfprintf (stderr, fmt, ap);
      fputc ('\n', stderr);
    }
  else
    vsyslog ((level == MU_DEBUG_ERROR) ? LOG_ERR : LOG_DEBUG, fmt, ap);
  return 0;
}

static int
_sieve_debug_printer (void *unused, const char *fmt, va_list ap)
{
  if (log_to_stderr)
    {
      fprintf (stderr, "DEBUG: ");
      vfprintf (stderr, fmt, ap);
      fputc ('\n', stderr);
    }
  else
    vsyslog (LOG_DEBUG, fmt, ap);
  return 0;
}

static void
_sieve_action_log (void *user_name,
		   const mu_sieve_locus_t *locus, size_t msgno,
		   mu_message_t msg,
		   const char *action, const char *fmt, va_list ap)
{
  char *text = NULL;
  
  if (message_id_header)
    {
      mu_header_t hdr = NULL;
      char *val = NULL;
      mu_message_get_header (msg, &hdr);
      if (mu_header_aget_value (hdr, message_id_header, &val) == 0
	  || mu_header_aget_value (hdr, MU_HEADER_MESSAGE_ID, &val) == 0)
	{
	  asprintf (&text, _("%s:%lu: %s on msg %s"),
		    locus->source_file,
		    (unsigned long) locus->source_line,
		    action, val);
	  free (val);
	}
    }
  if (text == NULL)
    {
      size_t uid = 0;
      mu_message_get_uid (msg, &uid);
      asprintf (&text, _("%s:%lu: %s on msg uid %d"),
		locus->source_file,
		(unsigned long)	locus->source_line,
		action, uid);
    }
  
  if (fmt && strlen (fmt))
    {
      char *diag = NULL;
      vasprintf (&diag, fmt, ap);
      if (log_to_stderr)
	fprintf (stderr, _("(user %s) %s: %s"), (char*) user_name,
		 text, diag);
      else
	syslog (LOG_NOTICE, _("(user %s) %s: %s"), (char*) user_name,
		text, diag);
      free (diag);
    }
  else if (log_to_stderr)
    fprintf (stderr, _("(user %s) %s"), (char*) user_name, text);
  else
    syslog (LOG_NOTICE, _("(user %s) %s"), (char*) user_name, text);
  free (text);
  if (log_to_stderr)
    fputc ('\n', stderr);
}

static int
_sieve_parse_error (void *user_name, const char *filename, int lineno,
		    const char *fmt, va_list ap)
{
  char *text;
  vasprintf (&text, fmt, ap);
  if (filename)
    {
      char *loc;
      asprintf (&loc, "%s:%d: ", filename, lineno);
      if (log_to_stderr)
	fprintf (stderr, "%s: %s", loc, text);
      else
	syslog (LOG_ERR, "%s: %s", loc, text);
      free (loc);
    }
  else if (log_to_stderr)
    fprintf (stderr, _("(user %s) %s"), (char*)user_name, text);
  else
    syslog (LOG_ERR, _("(user %s) %s"), (char*)user_name, text);
  free (text);
  if (log_to_stderr)
    fputc ('\n', stderr);
  return 0;
}

int
sieve_test (struct mu_auth_data *auth, mu_mailbox_t mbx)
{
  int rc = 1;
  char *progfile;
    
  if (!sieve_pattern)
    return 1;

  progfile = mu_expand_path_pattern (sieve_pattern, auth->name);
  if (access (progfile, R_OK))
    {
      if (debug_level > 2)
	syslog (LOG_DEBUG, _("Access to %s failed: %m"), progfile);
    }
  else
    {
      mu_sieve_machine_t mach;
      rc = mu_sieve_machine_init (&mach, auth->name);
      if (rc)
	{
	  mu_error (_("Cannot initialize sieve machine: %s"),
		    mu_strerror (rc));
	}
      else
	{
	  mu_sieve_set_debug (mach, _sieve_debug_printer);
	  mu_sieve_set_debug_level (mach, mudebug, sieve_debug_flags);
	  mu_sieve_set_parse_error (mach, _sieve_parse_error);
	  if (sieve_enable_log)
	    mu_sieve_set_logger (mach, _sieve_action_log);
	  
	  rc = mu_sieve_compile (mach, progfile);
	  if (rc == 0)
	    {
	      mu_attribute_t attr;
	      mu_message_t msg = NULL;
		
	      mu_mailbox_get_message (mbx, 1, &msg);
	      mu_message_get_attribute (msg, &attr);
	      mu_attribute_unset_deleted (attr);
	      if (switch_user_id (auth, 1) == 0)
		{
		  chdir (auth->dir);
		
		  rc = mu_sieve_message (mach, msg);
		  if (rc == 0)
		    rc = mu_attribute_is_deleted (attr) == 0;

		  switch_user_id (auth, 0);
		  chdir ("/");
		}
	      mu_sieve_machine_destroy (&mach);
	    }
	}
    }
  free (progfile);
  return rc;
}


int
main (int argc, char *argv[])
{
  int arg_index;

  /* Preparative work: close inherited fds, force a reasonable umask
     and prepare a logging. */
  close_fds ();
  umask (0077);

  /* Native Language Support */
  mu_init_nls ();

  /* Default locker settings */
  mu_locker_set_default_flags (MU_LOCKER_PID|MU_LOCKER_RETRY,
			    mu_locker_assign);
  mu_locker_set_default_retry_timeout (1);
  mu_locker_set_default_retry_count (300);

  /* Default error code for command line errors */
  mu_argp_error_code = EX_CONFIG;
  /* Register needed modules */
  MU_AUTH_REGISTER_ALL_MODULES ();

  /* Register mailbox formats */
  mu_register_all_formats ();
  
  /* Register the supported mailers.  */
  mu_registrar_record (mu_sendmail_record);
  mu_registrar_record (mu_smtp_record);

  mu_argp_init (program_version, NULL);
  mu_sieve_argp_init ();
  
  /* Parse command line */
  mu_argp_set_config_param (maidag_cfg_param);
  mu_argp_parse (&argp, &argc, &argv, 0, argp_capa, &arg_index, &daemon_param);

  current_uid = getuid ();

  if (log_to_stderr == -1)
    log_to_stderr = (getuid () != 0);
  
  if (!log_to_stderr)
    {
      openlog ("mail.local", LOG_PID, log_facility);
      mu_error_set_print (mu_syslog_error_printer);
    }
  
  if (debug_flags)
    {
      int rc;
      
      if ((rc = mu_debug_create (&mudebug, NULL)))
	{
	  mu_error (_("mu_debug_create failed: %s\n"), mu_strerror (rc));
	  exit (EX_TEMPFAIL);
	}
      if ((rc = mu_debug_set_level (mudebug, debug_flags)))
	{
	  mu_error (_("mu_debug_set_level failed: %s\n"),
		    mu_strerror (rc));
 	  exit (EX_TEMPFAIL);
	}
      if ((rc = mu_debug_set_print (mudebug, _mu_debug_printer, NULL)))
	{
	  mu_error (_("mu_debug_set_print failed: %s\n"),
		    mu_strerror (rc));
	  exit (EX_TEMPFAIL);
	}
    }

  argc -= arg_index;
  argv += arg_index;

  if (lmtp_mode)
    {
      if (argc)
	{
	  mu_error (_("Too many arguments"));
	  return EX_USAGE;
	}
      return maidag_lmtp_server ();
    }
  else 
    {
      if (current_uid)
	{
	  static char *s_argv[2];
	  struct mu_auth_data *auth = mu_get_auth_by_uid (current_uid);

	  if (!current_uid)
	    {
	      mu_error (_("Cannot get username"));
	      return EX_UNAVAILABLE;
	    }

	  if (argc > 1 || (argc > 0 && strcmp (auth->name, argv[0])))
	    {
	      mu_error (_("Recipients given when running as non-root"));
	      return EX_USAGE;
	    }

	  s_argv[0] = auth->name;
	  argv = s_argv;
	  argc = 1;
	}
      return maidag_stdio_delivery (argc, argv);
    }
}
  
