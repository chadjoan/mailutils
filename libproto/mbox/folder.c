/* GNU Mailutils -- a suite of utilities for electronic mail
   Copyright (C) 1999, 2000, 2001, 2003, 2004, 
   2005, 2006 Free Software Foundation, Inc.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General
   Public License along with this library; if not, write to the
   Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301 USA */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <errno.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <glob.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>

#include <folder0.h>
#include <registrar0.h>

#include <mailutils/auth.h>
#include <mailutils/url.h>
#include <mailutils/stream.h>
#include <mailutils/mutil.h>
#include <mailutils/errno.h>

/* We export url parsing and the initialisation of
   the mailbox, via the register entry/record.  */

static struct _mu_record _mbox_record =
{
  MU_MBOX_PRIO,
  MU_MBOX_SCHEME,
  _url_mbox_init, /* Mailbox init.  */
  _mailbox_mbox_init, /* Mailbox init.  */
  NULL, /* Mailer init.  */
  _folder_mbox_init, /* Folder init.  */
  NULL, /* No need for an back pointer.  */
  NULL, /* _is_scheme method.  */
  NULL, /* _get_url method.  */
  NULL, /* _get_mailbox method.  */
  NULL, /* _get_mailer method.  */
  NULL  /* _get_folder method.  */
};
mu_record_t mu_mbox_record = &_mbox_record;

static int
_path_is_scheme (mu_record_t record, const char *url, int flags)
{
  int rc = 0;
  const char *path;
  
  if (url && record->scheme)
    {
      if (mu_scheme_autodetect_p (url, &path))
	/* implies if (strncmp (record->scheme, url, strlen(record->scheme)) == 0)*/
	{
	  struct stat st;
	  
	  if (stat (path, &st) < 0)
	    return MU_FOLDER_ATTRIBUTE_ALL; /* mu_mailbox_open will complain */

	  if ((flags & MU_FOLDER_ATTRIBUTE_FILE)
	      && (S_ISREG (st.st_mode) || S_ISCHR (st.st_mode)))
	    rc |= MU_FOLDER_ATTRIBUTE_FILE;
	  if ((flags & MU_FOLDER_ATTRIBUTE_DIRECTORY)
	      && S_ISDIR (st.st_mode))
	    rc |= MU_FOLDER_ATTRIBUTE_DIRECTORY;
	}
    }
  return rc;
}

static struct _mu_record _path_record =
{
  MU_PATH_PRIO,
  MU_PATH_SCHEME,
  _url_path_init,     /* Mailbox init.  */
  _mailbox_mbox_init, /* Mailbox init.  */
  NULL,               /* Mailer init.  */
  _folder_mbox_init,  /* Folder init.  */
  NULL, /* No need for an owner.  */
  _path_is_scheme, /* is_scheme method.  */
  NULL, /* get_url method.  */
  NULL, /* get_mailbox method.  */
  NULL, /* get_mailer method.  */
  NULL  /* get_folder method.  */
};
mu_record_t mu_path_record = &_path_record;

/* lsub/subscribe/unsubscribe are not needed.  */
static void folder_mbox_destroy    (mu_folder_t);
static int folder_mbox_open        (mu_folder_t, int);
static int folder_mbox_close       (mu_folder_t);
static int folder_mbox_delete      (mu_folder_t, const char *);
static int folder_mbox_rename      (mu_folder_t , const char *, const char *);
static int folder_mbox_list        (mu_folder_t, const char *, const char *,
				    size_t,
				    mu_list_t);
static int folder_mbox_subscribe   (mu_folder_t, const char *);
static int folder_mbox_unsubscribe (mu_folder_t, const char *);
static int folder_mbox_lsub        (mu_folder_t, const char *, const char *,
				    mu_list_t);


static char *get_pathname       (const char *, const char *);

static int folder_mbox_get_authority (mu_folder_t folder, mu_authority_t * pauth);

struct _fmbox
{
  char *dirname;
  char **subscribe;
  size_t sublen;
};
typedef struct _fmbox *fmbox_t;


int
_folder_mbox_init (mu_folder_t folder)
{
  fmbox_t dfolder;
  size_t name_len = 0;
  int status = 0;

  /* We create an authority so the API is uniform across the mailbox
     types. */
  status = folder_mbox_get_authority (folder, NULL);
  if (status != 0)
    return status;

  dfolder = folder->data = calloc (1, sizeof (*dfolder));
  if (dfolder == NULL)
    return ENOMEM;

  mu_url_get_path (folder->url, NULL, 0, &name_len);
  dfolder->dirname = calloc (name_len + 1, sizeof (char));
  if (dfolder->dirname == NULL)
    {
      free (dfolder);
      folder->data = NULL;
      return ENOMEM;
    }
  mu_url_get_path (folder->url, dfolder->dirname, name_len + 1, NULL);

  folder->_destroy = folder_mbox_destroy;

  folder->_open = folder_mbox_open;
  folder->_close = folder_mbox_close;

  folder->_list = folder_mbox_list;
  folder->_lsub = folder_mbox_lsub;
  folder->_subscribe = folder_mbox_subscribe;
  folder->_unsubscribe = folder_mbox_unsubscribe;
  folder->_delete = folder_mbox_delete;
  folder->_rename = folder_mbox_rename;
  return 0;
}

void
folder_mbox_destroy (mu_folder_t folder)
{
  if (folder->data)
    {
      fmbox_t fmbox = folder->data;
      if (fmbox->dirname)
	free (fmbox->dirname);
      if (fmbox->subscribe)
	free (fmbox->subscribe);
      free (folder->data);
      folder->data = NULL;
    }
}

/* Noop. */
static int
folder_mbox_open (mu_folder_t folder, int flags ARG_UNUSED)
{
  fmbox_t fmbox = folder->data;
  if (flags & MU_STREAM_CREAT)
    {
      return (mkdir (fmbox->dirname, S_IRWXU) == 0) ? 0 : errno;
    }
  return 0;
}

/*  Noop.  */
static int
folder_mbox_close (mu_folder_t folder ARG_UNUSED)
{
  return 0;
}

static int
folder_mbox_delete (mu_folder_t folder, const char *filename)
{
  fmbox_t fmbox = folder->data;
  if (filename)
    {
      int status = 0;
      char *pathname = get_pathname (fmbox->dirname, filename);
      if (pathname)
	{
	  if (remove (pathname) != 0)
	    status = errno;
	  free (pathname);
	}
      else
	status = ENOMEM;
      return status;
    }
  return EINVAL;
}

static int
folder_mbox_rename (mu_folder_t folder, const char *oldpath, const char *newpath)
{
  fmbox_t fmbox = folder->data;
  if (oldpath && newpath)
    {
      int status = 0;
      char *pathold = get_pathname (fmbox->dirname, oldpath);
      if (pathold)
	{
	  char *pathnew = get_pathname (fmbox->dirname, newpath);
	  if (pathnew)
	    {
	      if (rename (pathold, pathnew) != 0)
		status = errno;
	      free (pathnew);
	    }
	  else
	    status = ENOMEM;
	  free (pathold);
	}
      else
	status = ENOMEM;
      return status;
    }
  return EINVAL;
}

struct inode_list           /* Inode/dev number list used to cut off
			       recursion */
{
  struct inode_list *next;
  ino_t inode;
  dev_t dev;
};

struct search_data
{
  mu_list_t result;
  const char *pattern;
  size_t max_level;
};

static int
inode_list_lookup (struct inode_list *list, struct stat *st)
{
  for (; list; list = list->next)
    if (list->inode == st->st_ino && list->dev == st->st_dev)
      return 1;
  return 0;
}


static int
list_helper (struct search_data *data,
	     const char *dirname, size_t level, struct inode_list *ilist)
{
  int status;
  glob_t gl;
  char *pathname;

  ++level;
  if (data->max_level && level > data->max_level)
    return 0;
  
  pathname = get_pathname (dirname, data->pattern);
  if (!pathname)
    return ENOMEM;

  memset(&gl, 0, sizeof(gl));
  status = glob (pathname, 0, NULL, &gl);
  free (pathname);

  if (status == 0)
    {
      size_t i;
      struct mu_list_response *resp;
      
      for (i = 0; i < gl.gl_pathc; i++)
	{
	  struct stat st;
	  
	  resp = malloc (sizeof (*resp));
	  if (resp == NULL)
	    {
	      status = ENOMEM;
	      break;
	    }
	  else if ((resp->name = strdup (gl.gl_pathv[i])) == NULL)
	    {
	      free (resp);
	      status = ENOMEM;
	      break;
	    }

	  resp->level = level;
	  resp->separator = '/';
	  resp->type = 0;
	  
	  mu_list_append (data->result, resp);

	  if (stat (gl.gl_pathv[i], &st) == 0)
	    {
	      resp->type = 0;
	      mu_registrar_lookup (gl.gl_pathv[i], MU_FOLDER_ATTRIBUTE_ALL,
				   NULL, &resp->type);
	      if ((resp->type & MU_FOLDER_ATTRIBUTE_DIRECTORY)
		  && !inode_list_lookup (ilist, &st))
		{
		  struct inode_list idata;
		  idata.inode = st.st_ino;
		  idata.dev   = st.st_dev;
		  idata.next  = ilist;
		  status = list_helper (data, gl.gl_pathv[i], level, &idata);
		  if (status)
		    break;
		}
	    }
	}
      globfree (&gl);
    }
  else
    {
      switch (status)
	{
	case GLOB_NOSPACE:
	  status = ENOMEM;
	  break;

	case GLOB_ABORTED:
	  status = MU_READ_ERROR;
	  break;

	case GLOB_NOMATCH:
	  if (mu_list_is_empty (data->result))
	    status = MU_ERR_NOENT;
	  else
	    status = 0;
	  break;

	case GLOB_NOSYS:
	  status = ENOSYS;
	  break;
	  
	default:
	  status = MU_ERR_FAILURE;
	  break;
	}
    }
  return status;
}

/* The listing is not recursif and we use glob() some expansion for us.
   Unfortunately glob() does not expand the '~'.  We also return
   The full pathname so it can be use to create other folders.  */
static int
folder_mbox_list (mu_folder_t folder, const char *dirname, const char *pattern,
		  size_t max_level,
		  mu_list_t flist)
{
  fmbox_t fmbox = folder->data;
  struct inode_list iroot;
  struct search_data sdata;
  
  memset (&iroot, 0, sizeof iroot);
  if (dirname == NULL || dirname[0] == '\0')
    dirname = (const char *)fmbox->dirname;

  sdata.result = flist;
  sdata.pattern = pattern;
  sdata.max_level = max_level;
  return list_helper (&sdata, dirname, 0, &iroot);
}

static int
folder_mbox_lsub (mu_folder_t folder, const char *ref ARG_UNUSED,
		  const char *name,
		  mu_list_t flist)
{
  fmbox_t fmbox = folder->data;
  int status;
  
  if (name == NULL || *name == '\0')
    name = "*";

  if (fmbox->sublen > 0)
    {      
      size_t i;

      for (i = 0; i < fmbox->sublen; i++)
	{
	  if (fmbox->subscribe[i]
	      && fnmatch (name, fmbox->subscribe[i], 0) == 0)
	    {
	      struct mu_list_response *resp;
	      resp = malloc (sizeof (*resp));
	      if (resp == NULL)
		{
		  status = ENOMEM;
		  break;
		}
	      else if ((resp->name = strdup (fmbox->subscribe[i])) == NULL)
		{
		  free (resp);
		  status = ENOMEM;
		  break;
		}
	      resp->type = MU_FOLDER_ATTRIBUTE_FILE;
	      resp->level = 0;
	      resp->separator = '/';
	    }
	}
    }
  return status;
}

static int
folder_mbox_subscribe (mu_folder_t folder, const char *name)
{
  fmbox_t fmbox = folder->data;
  char **tmp;
  size_t i;
  for (i = 0; i < fmbox->sublen; i++)
    {
      if (fmbox->subscribe[i] && strcmp (fmbox->subscribe[i], name) == 0)
	return 0;
    }
  tmp = realloc (fmbox->subscribe, (fmbox->sublen + 1) * sizeof (*tmp));
  if (tmp == NULL)
    return ENOMEM;
  fmbox->subscribe = tmp;
  fmbox->subscribe[fmbox->sublen] = strdup (name);
  if (fmbox->subscribe[fmbox->sublen] == NULL)
    return ENOMEM;
  fmbox->sublen++;
  return 0;
}

static int
folder_mbox_unsubscribe (mu_folder_t folder, const char *name)
{
  fmbox_t fmbox = folder->data;
  size_t i;
  for (i = 0; i < fmbox->sublen; i++)
    {
      if (fmbox->subscribe[i] && strcmp (fmbox->subscribe[i], name) == 0)
	{
	  free (fmbox->subscribe[i]);
	  fmbox->subscribe[i] = NULL;
	  return 0;
	}
    }
  return MU_ERR_NOENT;
}

static char *
get_pathname (const char *dirname, const char *basename)
{
  char *pathname = NULL;

  /* Skip eventual protocol designator.
     FIXME: Actually, any valid URL spec should be allowed as dirname ... */
  if (strncmp (dirname, MU_MBOX_SCHEME, MU_MBOX_SCHEME_LEN) == 0)
    dirname += MU_MBOX_SCHEME_LEN;
  else if (strncmp (dirname, MU_FILE_SCHEME, MU_FILE_SCHEME_LEN) == 0)
    dirname += MU_FILE_SCHEME_LEN;
  
  /* null basename gives dirname.  */
  if (basename == NULL)
    pathname = (dirname) ? strdup (dirname) : strdup (".");
  /* Absolute.  */
  else if (basename[0] == '/')
    pathname = strdup (basename);
  /* Relative.  */
  else
    {
      size_t baselen = strlen (basename);
      size_t dirlen = strlen (dirname);
      while (dirlen > 0 && dirname[dirlen-1] == '/')
	dirlen--;
      pathname = calloc (dirlen + baselen + 2, sizeof (char));
      if (pathname)
	{
	  memcpy (pathname, dirname, dirlen);
	  pathname[dirlen] = '/';
	  strcpy (pathname + dirlen + 1, basename);
	}
    }
  return pathname;
}

static int
folder_mbox_get_authority (mu_folder_t folder, mu_authority_t *pauth)
{
  int status = 0;
  if (folder->authority == NULL)
    {
	status = mu_authority_create_null (&folder->authority, folder);
    }
  if (!status && pauth)
    *pauth = folder->authority;
  return status;
}
