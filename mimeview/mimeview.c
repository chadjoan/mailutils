/* GNU Mailutils -- a suite of utilities for electronic mail
   Copyright (C) 2005 Free Software Foundation, Inc.

   GNU Mailutils is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GNU Mailutils is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GNU Mailutils; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA  */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <mimeview.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

const char *program_version = "mimeview (" PACKAGE_STRING ")";
/* TRANSLATORS: Please, preserve the vertical tabulation (^K character)
   in this message */
static char doc[] = N_("GNU mimeview -- display files, using mailcap mechanism.\
Default mime.types file is ") DEFAULT_CUPS_CONFDIR "/mime.types"
N_("\n\nDebug flags are:\n\
  g - Mime.types parser traces\n\
  l - Mime.types lexical analyzer traces\n\
  0-9 - Set debugging level\n");

#define OPT_METAMAIL 256

static struct argp_option options[] = {
  {"no-ask", 'a', N_("TYPE-LIST"), OPTION_ARG_OPTIONAL,
   N_("Do not ask for confirmation before displaying files. If TYPE-LIST is given, do not ask for confirmation before displaying such files whose MIME type matches one of the patterns from TYPE-LIST"), 0},
  {"no-interactive", 'h', NULL, 0,
   N_("Disable interactive mode"), 0 },
  {"print", 0, NULL, OPTION_ALIAS, NULL, 0 },
  {"debug",  'd', N_("FLAGS"),  OPTION_ARG_OPTIONAL,
   N_("Enable debugging output"), 0},
  {"mimetypes", 't', N_("FILE"), 0,
   N_("Use this mime.types file"), 0},
  {"dry-run", 'n', NULL, 0,
   N_("Do not do anything, just print what whould be done"), 0},
  {"metamail", OPT_METAMAIL, N_("FILE"), OPTION_ARG_OPTIONAL,
   N_("Use metamail to display files"), 0},
  {0, 0, 0, 0}
};

int debug_level;       /* Debugging level set by --debug option */
static int dry_run;    /* Dry run mode */
static char *metamail; /* Name of metamail program, if requested */
static char *mimetypes_config = DEFAULT_CUPS_CONFDIR;
static char *no_ask_types;  /* List of MIME types for which no questions
			       should be asked */
static int interactive = -1; 
char *mimeview_file;       /* Name of the file to view */
FILE *mimeview_fp;     /* Its descriptor */

static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  switch (key)
    {
    case ARGP_KEY_INIT:
      mimetypes_lex_debug (0);
      mimetypes_gram_debug (0);
      if (interactive == -1)
	interactive = isatty (fileno (stdin));
      break;

    case ARGP_KEY_FINI:
      if (dry_run && !debug_level)
	debug_level = 1;
      break;

    case 'a':
      no_ask_types = arg ? arg : "*";
      setenv ("MM_NOASK", arg, 1); /* In case we are given --metamail option */
      break;
      
    case 'd':
      if (!arg)
	arg = "9";
      for (; *arg; arg++)
	{
	  switch (*arg)
	    {
	    case 'l':
	      mimetypes_lex_debug (1);
	      break;

	    case 'g':
	      mimetypes_gram_debug (1);
	      break;

	    default:
	      debug_level = *arg - '0';
	    }
	}
      break;

    case 'h':
      interactive = 0;
      break;
      
    case 'n':
      dry_run = 1;
      break;
      
    case 't':
      mimetypes_config = arg;
      break;

    case OPT_METAMAIL:
      metamail = arg ? arg : "metamail";
      break;
      
    default: 
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static struct argp argp = {
  options,
  parse_opt,
  N_("FILE [FILE ...]"),
  doc,
  NULL,
  NULL, NULL
};

static const char *capa[] = {
  "common",
  "license",
  NULL
};

static int
open_file (char *name)
{
  struct stat st;
  if (stat (name, &st))
    {
      mu_error (_("Cannot stat `%s': %s"), name, mu_strerror (errno));
      return -1;
    }
  if (!S_ISREG (st.st_mode) && !S_ISLNK (st.st_mode))
    {
      mu_error (_("Not a regular file or symbolic link: `%s'"), name);
      return -1;
    }

  mimeview_file = name;
  mimeview_fp = fopen (name, "r");
  if (mimeview_fp == NULL)
    {
      mu_error (_("Cannot open `%s': %s"), name, mu_strerror (errno));
      return -1;
    }
  return 0;
}

void
close_file ()
{
  fclose (mimeview_fp);
}

void
display_file (const char *type)
{
  int status;
  
  if (metamail)
    {
      const char *argv[7];
      
      argv[0] = "metamail";
      argv[1] = "-b";

      argv[2] = interactive ? "-p" : "-h";
      
      argv[3] = "-c";
      argv[4] = type;
      argv[5] = mimeview_file;
      argv[6] = NULL;
      
      if (debug_level)
	{
	  char *string;
	  argcv_string (6, argv, &string);
	  printf (_("Executing %s...\n"), string);
	  free (string);
	}
      
      if (!dry_run)
	mu_spawnvp (metamail, argv, &status);
    }
  else
    {
      stream_t stream;
      header_t hdr;
      char *text;

      asprintf (&text, "Content-Type: %s\n", type);
      status = header_create (&hdr, text, strlen (text), NULL);
      if (status)
	mu_error (_("Cannot create header: %s"), mu_strerror (status));
      else
	{
	  stdio_stream_create (&stream, mimeview_fp,
			       MU_STREAM_READ|MU_STREAM_SEEKABLE|MU_STREAM_NO_CLOSE);
	  stream_open (stream);
	  
	  display_stream_mailcap (mimeview_file, stream, hdr,
				  no_ask_types, interactive, dry_run,
				  debug_level);
	  
	  stream_close (stream);
	  stream_destroy (&stream, stream_get_owner (stream));

	  header_destroy (&hdr, header_get_owner (hdr));
	}
    }
}

int
main (int argc, char **argv)
{
  int index;
  
  mu_init_nls ();
  mu_argp_init (program_version, NULL);
  mu_argp_parse (&argp, &argc, &argv, 0, capa, &index, NULL);

  argc -= index;
  argv += index;

  if (argc == 0)
    {
      mu_error (_("No files given"));
      return 1;
    }

  if (mimetypes_parse (mimetypes_config))
    return 1;
  
  while (argc--)
    {
      const char *type;
      
      if (open_file (*argv++))
	continue;
      type = get_file_type ();
      DEBUG (1, ("%s: %s\n", mimeview_file, type ? type : "?"));
      if (type)
	display_file (type);
      close_file ();
    }
  
  return 0;
}