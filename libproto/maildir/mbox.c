/* GNU Mailutils -- a suite of utilities for electronic mail
   Copyright (C) 1999-2005, 2007-2008, 2010-2012, 2014-2016 Free
   Software Foundation, Inc.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 3 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General
   Public License along with this library.  If not, see
   <http://www.gnu.org/licenses/>. */

/* First draft by Sergey Poznyakoff */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#ifdef ENABLE_MAILDIR

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <dirent.h>
#include <limits.h>

#ifdef WITH_PTHREAD
# ifdef HAVE_PTHREAD_H
#  ifndef _XOPEN_SOURCE
#   define _XOPEN_SOURCE  500
#  endif
#  include <pthread.h>
# endif
#endif

#include <string.h>
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif

#include <mailutils/attribute.h>
#include <mailutils/body.h>
#include <mailutils/debug.h>
#include <mailutils/envelope.h>
#include <mailutils/error.h>
#include <mailutils/header.h>
#include <mailutils/message.h>
#include <mailutils/util.h>
#include <mailutils/property.h>
#include <mailutils/stream.h>
#include <mailutils/url.h>
#include <mailutils/observer.h>
#include <mailutils/errno.h>
#include <mailutils/locker.h>
#include <mailutils/sys/mailbox.h>
#include <mailutils/sys/registrar.h>
#include <mailutils/sys/amd.h>
#include <mailutils/io.h>
#include <maildir.h>

#ifndef PATH_MAX 
# define PATH_MAX _POSIX_PATH_MAX
#endif

struct _maildir_message
{
  struct _amd_message amd_message;
  char *dir;
  char *file_name;
  unsigned long uid;
};


/* Attribute handling.
   FIXME: P (Passed) is not handled */

static struct info_map {
  char letter;
  int flag;
} info_map[] = {
  { 'D', MU_ATTRIBUTE_DRAFT },
  { 'F', MU_ATTRIBUTE_FLAGGED },
  { 'P', 0 }, /* (passed): the user has resent/forwarded/bounced this
		 message to someone else. */
  { 'R', MU_ATTRIBUTE_READ },
  { 'S', MU_ATTRIBUTE_SEEN },
  { 'T', MU_ATTRIBUTE_DELETED },
  { 'a', MU_ATTRIBUTE_ANSWERED },
};
#define info_map_size (sizeof (info_map) / sizeof (info_map[0]))

/* NOTE: BUF must be at least info_map_size bytes long */
static int
flags_to_info (int flags, char *buf)
{
  struct info_map *p;
  
  for (p = info_map; p < info_map + info_map_size; p++)
    if (p->flag & flags)
      *buf++ = p->letter;
  *buf = 0;
  return 0;
}

static int
info_to_flags (char *buf)
{
  int flags = 0;
  struct info_map *p;

  for (p = info_map; p < info_map + info_map_size; p++)
    if (strchr (buf, p->letter))
      flags |= p->flag;
  return flags;
}

static char *
maildir_name_info_ptr (char *name)
{
  char *p = strchr (name, ':');
  if (p && memcmp (p + 1, "2,", 2) == 0)
    return p + 3;
  return NULL;
}


static int
maildir_message_cmp (struct _amd_message *a, struct _amd_message *b)
{
  char *name_a, *name_b;
  unsigned long na = strtoul (((struct _maildir_message *) a)->file_name,
			      &name_a, 10);
  unsigned long nb = strtoul (((struct _maildir_message *) b)->file_name,
			      &name_b, 10);
  int rc;
  
  if (na > nb)
    return 1;
  if (na < nb)
    return -1;

  /* If timestamps are equal, compare rests of the filenames up to the
     info marker */

  if (name_a && !name_b)
    return 1;
  else if (!name_a)
    {
      if (name_b)
	return -1;
      else 
	return 0;
    }
  
  for (; *name_a && *name_a != ':' && *name_b && *name_b != ':';
       name_a++, name_b++)
    if ((rc = (*name_a - *name_b)))
      return rc;

  if ((*name_a == ':' || *name_a == 0) && (*name_b == ':' || *name_b == 0))
    return 0;

  return *name_a - *name_b;
}

void
msg_free (struct _amd_message *amsg)
{
  struct _maildir_message *mp = (struct _maildir_message *) amsg;
  free (mp->file_name);
}

char *
maildir_gethostname ()
{
  char hostname[256];
  char *hp;
  char *p;
  size_t s;
  
  if (gethostname (hostname, sizeof hostname) < 0)
    strcpy (hostname, "localhost");

  for (s = 0, p = hostname; *p; p++)
    if (*p == '/' || *p == ':')
      s += 4;

  if (s)
    {
      char *q;
      
      hp = malloc (strlen (hostname) + s + 1);
      for (p = hostname, q = hp; *p; p++)
	switch (*p)
	  {
	  case '/':
	    memcpy (q, "\\057", 4);
	    q += 4;
	    break;

	  case ':':
	    memcpy (q, "\\072", 4);
	    q += 4;
	    break;

	  default:
	    *q++ = *p++;
	  }
      *q = 0;
    }
  else
    hp = strdup (hostname);
  return hp;
}

int
read_random (void *buf, size_t size)
{
  int rc;
  int fd = open ("/dev/urandom", O_RDONLY);
  if (fd == -1)
    return -1;
  rc = read (fd, buf, size);
  close (fd);
  return rc != size;
}

int
maildir_mkfilename (const char *directory, const char *suffix, const char *name,
		    char **ret_name)
{
  size_t size = strlen (directory) + 1 + strlen (suffix) + 1;
  char *tmp;

  if (name)
    size += 1 + strlen (name);
  
  tmp = malloc (size);
  if (!tmp)
    return errno;
  sprintf (tmp, "%s/%s", directory, suffix);
  if (name)
    {
      strcat (tmp, "/");
      strcat (tmp, name);
    }
  *ret_name = tmp;
  return 0;
}

static int
mk_info_filename (char *directory, char *suffix, char *name, int flags,
		  char **ret_name)
{
  char fbuf[info_map_size + 1];
  char *tmp;
  int namelen;

  tmp = strchr (name, ':');
  if (!tmp)
    namelen = strlen (name);
  else
    namelen = tmp - name;

  flags_to_info (flags, fbuf);

  return mu_asprintf (ret_name, "%s/%s/%*.*s:2,%s",
		      directory, suffix, namelen, namelen, name, fbuf);
}

char *
maildir_uniq (struct _amd_data *amd, int fd)
{
  char buffer[PATH_MAX];
  int ind = 0;
#define FMT(fmt,val) do {\
  ind += snprintf(buffer+ind, sizeof buffer-ind, fmt, val); \
} while (0) 
#define PFX(pfx,fmt,val) do {\
  if (ind < sizeof buffer-1) {\
    buffer[ind++] = pfx;\
    FMT(fmt,val);\
  }\
} while (0) 
#define COPY(s) do {\
  char *p; \
  for (p = s; ind < sizeof buffer-1 && *p;) \
    buffer[ind++] = *p++;\
} while (0);
  char *hostname = maildir_gethostname ();
  struct timeval tv;
  unsigned long n;
  struct stat st;

  gettimeofday (&tv, NULL);
  FMT ("%lu", tv.tv_sec);
  COPY (".");

  if (read_random (&n, sizeof (unsigned long))) /* FIXME: 32 bits */
    PFX ('R', "%lX", n);

  if (fd > 0 && fstat (fd, &st) == 0)
    {
      PFX ('I', "%lX", (unsigned long) st.st_ino);
      PFX ('V', "%lX", (unsigned long) st.st_dev);
    }

  PFX ('M', "%lu", tv.tv_usec);
  PFX ('P', "%lu", (unsigned long) getpid ());
  PFX ('Q', "%lu", (unsigned long) amd->msg_count);
  PFX ('.', "%s", hostname);
  free (hostname);

  buffer[ind] = 0;
  
  return strdup (buffer);
}

/* FIXME: The following two functions dump core on ENOMEM */
static int
maildir_cur_message_name (struct _amd_message *amsg, char **pname)
{
  struct _maildir_message *msg = (struct _maildir_message *) amsg;
  return maildir_mkfilename (amsg->amd->name, msg->dir, msg->file_name, pname);
}

static int
maildir_new_message_name (struct _amd_message *amsg, int flags, int expunge,
			  char **pname)
{
  struct _maildir_message *msg = (struct _maildir_message *) amsg;
  if (expunge && (flags & MU_ATTRIBUTE_DELETED))
    {
      /* Force amd.c to unlink the file. */
      *pname = NULL;
    }
  else if (strcmp (msg->dir, CURSUF) == 0)
    return mk_info_filename (amsg->amd->name, CURSUF, msg->file_name, flags,
			     pname);
  else
    return maildir_mkfilename (amsg->amd->name, msg->dir, msg->file_name,
			       pname);
  return 0;
}

static void
maildir_msg_free (struct _amd_message *amsg)
{
  struct _maildir_message *mp = (struct _maildir_message *) amsg;
  free (mp->file_name);
}

static int
maildir_message_uid (mu_message_t msg, size_t *puid)
{
  struct _maildir_message *mp = mu_message_get_owner (msg);
  if (puid)
    *puid = mp->uid;
  return 0;
}

static size_t
maildir_next_uid (struct _amd_data *amd)
{
  struct _maildir_message *msg = (struct _maildir_message *)
                                   _amd_get_message (amd, amd->msg_count);
  return (msg ? msg->uid : 0) + 1;
}

/* According to http://www.qmail.org/qmail-manual-html/man5/maildir.html 
   a file in tmp may be safely removed if it has not been accessed in 36
   hours */

static void
maildir_delete_file (char *dirname, char *filename)
{
  struct stat st;
  char *name;
  int rc;

  rc = maildir_mkfilename (dirname, filename, NULL, &name);
  if (rc)
    {
      mu_error ("maildir: failed to create file name: %s",
		mu_strerror (errno));
      return;
    }

  if (stat (name, &st) == 0)
    {
      if (time (NULL) - st.st_atime > 36 * 3600)	
	remove (name);
    }
  free (name);
}



static int
maildir_opendir (DIR **dir, char *name, int permissions)
{
  int rc = 0;
      
  *dir = opendir (name);
  if (!*dir)
    {
      if (errno == ENOENT)
	{
	  if (mkdir (name, permissions))
	    {
	      rc = errno;
	      mu_debug (MU_DEBCAT_MAILBOX, MU_DEBUG_ERROR,
			("can't create directory %s: %s",
			 name, mu_strerror (rc)));
	      return rc;
	    }
	  
	  *dir = opendir (name);
	  if (*dir)
	    return 0;
	}
      rc = errno;
      mu_debug (MU_DEBCAT_MAILBOX, MU_DEBUG_ERROR,
		("can't open directory %s: %s",
		 name, mu_strerror (rc)));
    }
  return rc;
}

static int
maildir_create (struct _amd_data *amd, int flags)
{
  static char *dirs[] = { TMPSUF, NEWSUF, CURSUF };
  int i;

  for (i = 0; i < 3; i++)
    {
      DIR *dir;
      int rc;
      char *tmpname;

      rc = maildir_mkfilename (amd->name, dirs[i], NULL, &tmpname);
      if (rc)
	return rc;
      
      rc = maildir_opendir (&dir, tmpname,
			    PERMS |
			    mu_stream_flags_to_mode (amd->mailbox->flags,
						     1));
      free (tmpname);
      if (rc)
	return rc;
      closedir (dir);
    }
  return 0;
}


#define NTRIES 30

/* Delivery to "dir/new" */

static int
maildir_msg_init (struct _amd_data *amd, struct _amd_message *amm)
{
  struct _maildir_message *msg = (struct _maildir_message *) amm;
  char *name, *fname;
  struct stat st;
  int i;
  int rc;
  
  name = maildir_uniq (amd, -1);
  rc = maildir_mkfilename (amd->name, NEWSUF, name, &fname);
  if (rc)
    {
      free (name);
      return rc;
    }
  
  msg->dir = TMPSUF;
  
  for (i = 0; i < NTRIES; i++)
    {
      if (stat (fname, &st) < 0 && errno == ENOENT)
	{
	  msg->uid = amd->next_uid (amd);
	  msg->file_name = name;
	  free (fname);
	  return 0;
	}
      mu_diag_output (MU_DIAG_WARNING, "cannot stat %s: %s", fname,
		      mu_strerror (errno));
      sleep (2);
    }
  free (fname);
  free (name);
  return EAGAIN;
}

static int
maildir_msg_finish_delivery (struct _amd_data *amd, struct _amd_message *amm,
			     const mu_message_t orig_msg)
{
  struct _maildir_message *msg = (struct _maildir_message *) amm;
  char *oldname, *newname;
  mu_attribute_t attr;
  int flags;
  int rc;
  
  rc = maildir_mkfilename (amd->name, TMPSUF, msg->file_name, &oldname);
  if (rc)
    return rc;
  
  if (mu_message_get_attribute (orig_msg, &attr) == 0
      && mu_attribute_is_read (attr)
      && mu_attribute_get_flags (attr, &flags) == 0)
    {
      msg->dir = CURSUF;
      rc = mk_info_filename (amd->name, CURSUF, msg->file_name, flags,
			     &newname);
    }
  else
    {
      msg->dir = NEWSUF;
      rc = maildir_mkfilename (amd->name, NEWSUF, msg->file_name, &newname);
    }

  if (rc == 0)
    {
      if (unlink (newname) && errno != ENOENT)
	{
	  rc = errno;
	  mu_debug (MU_DEBCAT_MAILBOX, MU_DEBUG_ERROR,
		    ("can't unlink %s: %s",
		     newname, mu_strerror (errno)));
	}
      else if (link (oldname, newname) == 0)
	{
	  if (unlink (oldname))
	    mu_debug (MU_DEBCAT_MAILBOX, MU_DEBUG_ERROR,
		      ("can't unlink %s: %s",
		       oldname, mu_strerror (errno)));
	}
      else
	{
	  rc = errno;
	  mu_debug (MU_DEBCAT_MAILBOX, MU_DEBUG_ERROR,
		    ("renaming %s to %s failed: %s",
		     oldname, newname, mu_strerror (rc)));
	}

    }
  
  free (oldname);
  free (newname);
  return rc;
}


/* Maldir scanning */

int
maildir_flush (struct _amd_data *amd)
{
  int rc;
  DIR *dir;
  struct dirent *entry;
  char *tmpname;

  if (!(amd->mailbox->flags & MU_STREAM_WRITE))
    return EACCES;

  rc = maildir_mkfilename (amd->name, TMPSUF, NULL, &tmpname);
  if (rc)
    return rc;

  rc = maildir_opendir (&dir, tmpname,
			PERMS |
			mu_stream_flags_to_mode (amd->mailbox->flags, 1));
  if (rc)
    {
      free (tmpname);
      return rc;
    }
      
  while ((entry = readdir (dir)))
    {
      switch (entry->d_name[0])
	{
	case '.':
	  break;

	default:
	  maildir_delete_file (tmpname, entry->d_name);
	  break;
	}
    }

  free (tmpname);

  closedir (dir);
  return 0;
}

int
maildir_deliver_new (struct _amd_data *amd, DIR *dir)
{
  struct dirent *entry;
  int err = 0;

  if (!(amd->mailbox->flags & MU_STREAM_WRITE))
    return EACCES;
  while ((entry = readdir (dir)))
    {
      char *oldname, *newname;
      int rc;
      
      switch (entry->d_name[0])
	{
	case '.':
	  break;

	default:
	  rc = maildir_mkfilename (amd->name, NEWSUF, entry->d_name, &oldname);
	  if (rc)
	    return rc;
	  rc = mk_info_filename (amd->name, CURSUF, entry->d_name, 0, &newname);
	  if (rc)
	    {
	      free (oldname);
	      return rc;
	    }
	  if (rename (oldname, newname))
	    {
	      err = MU_ERR_FAILURE;
	      mu_debug (MU_DEBCAT_MAILBOX, MU_DEBUG_ERROR,
			("renaming %s to %s failed: %s",
			 oldname, newname, mu_strerror (errno)));
	    }
	  free (oldname);
	  free (newname);
	}
    }
  return err;
}

static int
maildir_scan_dir (struct _amd_data *amd, DIR *dir, char *dirname)
{
  struct dirent *entry;
  struct _maildir_message *msg, key;
  char *p;
  size_t index;
  int rc = 0;
  int need_sort = 0;
  struct stat st;
  
  while ((entry = readdir (dir)))
    {
      char *fname;
      
      if (entry->d_name[0] == '.')
	continue;

      rc = maildir_mkfilename (amd->name, dirname, entry->d_name, &fname);
      if (rc)
	{
	  mu_diag_funcall (MU_DIAG_ERROR, "maildir_mkfilename",
			   entry->d_name, rc);
	  continue;
	}
      
      if (stat (fname, &st))
	{
	  rc = errno;
	  mu_debug (MU_DEBCAT_MAILBOX, MU_DEBUG_ERROR,
		    ("can't stat %s: %s", fname, mu_strerror (rc)));
	  free (fname);
	  continue;
	}

      free (fname);

      if (!S_ISREG (st.st_mode))
	continue;
      
      msg = calloc (1, sizeof (*msg));
      if (!msg)
	{
	  rc = ENOMEM;
	  break;
	}
      key.file_name = entry->d_name;
      if (!amd_msg_lookup (amd, (struct _amd_message *) &key, &index))
	continue;
      rc = _amd_message_append (amd, (struct _amd_message *) msg);
      if (rc)
	{
	  free (msg);
	  break;
	}
	      
      msg->dir = dirname;
      msg->file_name = strdup (entry->d_name);

      p = maildir_name_info_ptr (msg->file_name);
      if (p)
	msg->amd_message.attr_flags = info_to_flags (p);
      else
	msg->amd_message.attr_flags = 0;
      msg->amd_message.orig_flags = msg->amd_message.attr_flags;
      need_sort = 1;
    }

  if (rc == 0 && need_sort)
    amd_sort (amd);
  return rc;
}

static int
maildir_scan_unlocked (mu_mailbox_t mailbox, size_t *pcount, int do_notify)
{
  struct _amd_data *amd = mailbox->data;
  DIR *dir;
  int status = 0;
  char *name;
  struct stat st;
  size_t i;
  
  /* 1st phase: Flush tmp/ */
  maildir_flush (amd);

  /* 2nd phase: Scan and deliver messages from new */
  status = maildir_mkfilename (amd->name, NEWSUF, NULL, &name);
  if (status)
    return status;
  
  status = maildir_opendir (&dir, name,
			    PERMS |
			    mu_stream_flags_to_mode (mailbox->flags, 1));
  if (status == 0)
    {
      if (maildir_deliver_new (amd, dir))
	status = maildir_scan_dir (amd, dir, NEWSUF);
      closedir (dir);
    }
  free (name);

  /* 3rd phase: Scan cur/ */
  status = maildir_mkfilename (amd->name, CURSUF, NULL, &name);
  if (status)
    return status;
  
  status = maildir_opendir (&dir, name,
			    PERMS |
			    mu_stream_flags_to_mode (mailbox->flags, 1));
  if (status == 0)
    {
      status = maildir_scan_dir (amd, dir, CURSUF);
      closedir (dir);
    }
  free (name);

  for (i = 1; i <= amd->msg_count; i++)
    {
      struct _maildir_message *msg = (struct _maildir_message *)
	_amd_get_message (amd, i);
      msg->uid = i;
      if (do_notify)
	DISPATCH_ADD_MSG (mailbox, amd, i);
    }
  
  if (stat (amd->name, &st) == 0)
    amd->mtime = st.st_mtime;

  if (pcount)
    *pcount = amd->msg_count;

  return status;
}

static int
maildir_scan0 (mu_mailbox_t mailbox, size_t msgno MU_ARG_UNUSED,
	       size_t *pcount, 
	       int do_notify)
{
  struct _amd_data *amd = mailbox->data;
  int rc;
  
  if (amd == NULL)
    return EINVAL;
  if (mailbox->flags & MU_STREAM_APPEND)
    return 0;
  mu_monitor_wrlock (mailbox->monitor);
  rc = maildir_scan_unlocked (mailbox, pcount, do_notify);
  mu_monitor_unlock (mailbox->monitor);
  return rc;
}
  
static int
maildir_size_dir (struct _amd_data *amd, char *dirsuf, mu_off_t *psize)
{
  DIR *dir;
  struct dirent *entry;
  int rc = 0;
  struct stat st;
  char *name;
  
  rc = maildir_mkfilename (amd->name, dirsuf, NULL, &name);
  if (rc)
    return rc;
  dir = opendir (name);
  
  if (!dir)
    {
      rc = errno;
      mu_debug (MU_DEBCAT_MAILBOX, MU_DEBUG_ERROR,
		("can't open directory %s: %s", name, mu_strerror (rc)));
      free (name);
      if (rc == ENOENT)
	return 0;
      return rc;
    }

  while ((entry = readdir (dir)))
    {
      char *fname;
      
      if (entry->d_name[0] == '.')
	continue;

      rc = maildir_mkfilename (amd->name, dirsuf, entry->d_name, &fname);
      if (rc)
	{
	  mu_diag_funcall (MU_DIAG_ERROR, "maildir_mkfilename",
			   entry->d_name, rc);
	  continue;
	}
      
      if (stat (fname, &st))
	{
	  rc = errno;
	  mu_debug (MU_DEBCAT_MAILBOX, MU_DEBUG_ERROR,
		    ("can't stat %s: %s", fname, mu_strerror (rc)));
	  free (fname);
	  continue;
	}

      free (fname);

      if (S_ISREG (st.st_mode))
	*psize += st.st_size;
    }

  closedir (dir);
  free (name);
  
  return 0;
}

static int
maildir_size_unlocked (struct _amd_data *amd, mu_off_t *psize)
{
  mu_off_t size = 0;
  int rc;

  rc = maildir_size_dir (amd, NEWSUF, &size);
  if (rc)
    return rc;
  rc = maildir_size_dir (amd, CURSUF, &size);
  if (rc)
    return rc;
  *psize = size;
  return 0;
}

static int
maildir_size (mu_mailbox_t mailbox, mu_off_t *psize)
{
  struct _amd_data *amd = mailbox->data;
  int rc;
  
  if (amd == NULL)
    return EINVAL;

  mu_monitor_wrlock (mailbox->monitor);
  rc = maildir_size_unlocked (amd, psize);
  mu_monitor_unlock (mailbox->monitor);

  return rc;
}
  

static int
maildir_qfetch (struct _amd_data *amd, mu_message_qid_t qid)
{
  struct _maildir_message *msg;
  char *name = strrchr (qid, '/');
  char *p;
  char *dir;
  
  if (!name)
    return EINVAL;
  name++;
  if (name - qid < 4)
    return EINVAL;
  else if (memcmp (name - 4, CURSUF, sizeof (CURSUF) - 1) == 0)
    dir = CURSUF;
  else if (memcmp (name - 4, NEWSUF, sizeof (NEWSUF) - 1) == 0)
    dir = NEWSUF;
  else if (memcmp (name - 4, TMPSUF, sizeof (TMPSUF) - 1) == 0)
    dir = TMPSUF;
  else
    return EINVAL;
  
  msg = calloc (1, sizeof(*msg));
  msg->file_name = strdup (name);
  msg->dir = dir;
  
  p = maildir_name_info_ptr (msg->file_name);
  if (p)
    msg->amd_message.attr_flags = info_to_flags (p);
  else
    msg->amd_message.attr_flags = 0;
  msg->amd_message.orig_flags = msg->amd_message.attr_flags;
  msg->uid = amd->next_uid (amd);
  _amd_message_insert (amd, (struct _amd_message*) msg);
  return 0;
}


static int
maildir_remove (struct _amd_data *amd)
{
  int i;
  static char *suf[3] = { NEWSUF, CURSUF, TMPSUF };
  int rc = 0;

  for (i = 0; rc == 0 && i < 3; i++)
    {
      char *name;

      rc = maildir_mkfilename (amd->name, suf[i], NULL, &name);
      if (rc)
	return rc;
      rc = amd_remove_dir (name);
      if (rc)
	mu_diag_output (MU_DIAG_WARNING,
			"removing contents of %s failed: %s", name,
			mu_strerror (rc));
      free (name);
    }
  
  return rc;
}


static int
maildir_chattr_msg (struct _amd_message *amsg, int expunge)
{
  struct _maildir_message *mp = (struct _maildir_message *) amsg;
  struct _amd_data *amd = amsg->amd;
  int rc;
  char *new_name;
  
  rc = amd->new_msg_file_name (amsg, amsg->attr_flags, expunge, &new_name);
  if (rc)
    return rc;
  if (!new_name)
    {
      if (unlink (mp->file_name))
	{
	  rc = errno;
	  mu_debug (MU_DEBCAT_MAILBOX, MU_DEBUG_ERROR,
		    ("can't unlink %s: %s",
		     mp->file_name, mu_strerror (rc)));
	}
    }
  else
    {
      char *cur_name;
      
      rc = maildir_cur_message_name (amsg, &cur_name);
      if (rc)
	{
	  free (new_name);
	  return rc;
	}
      if (rename (cur_name, new_name))
	{
	  if (errno == ENOENT)
	    mu_observable_notify (amd->mailbox->observable,
				  MU_EVT_MAILBOX_CORRUPT,
				  amd->mailbox);
	  else
	    {
	      rc = errno;
	      mu_debug (MU_DEBCAT_MAILBOX, MU_DEBUG_ERROR,
			("renaming %s to %s failed: %s",
			 cur_name, new_name, mu_strerror (rc)));
	    }
	}
      free (cur_name);
    }

  free (new_name);
  return rc;
}

     
int
_mailbox_maildir_init (mu_mailbox_t mailbox)
{
  int rc;
  struct _amd_data *amd;

  rc = amd_init_mailbox (mailbox, sizeof (struct _amd_data), &amd);
  if (rc)
    return rc;

  amd->msg_size = sizeof (struct _maildir_message);
  amd->msg_free = maildir_msg_free;
  amd->create = maildir_create;
  amd->msg_init_delivery = maildir_msg_init;
  amd->msg_finish_delivery = maildir_msg_finish_delivery;
  amd->cur_msg_file_name = maildir_cur_message_name;
  amd->new_msg_file_name = maildir_new_message_name;
  amd->scan0 = maildir_scan0;
  amd->qfetch = maildir_qfetch;
  amd->msg_cmp = maildir_message_cmp;
  amd->message_uid = maildir_message_uid;
  amd->next_uid = maildir_next_uid;
  amd->remove = maildir_remove;
  amd->chattr_msg = maildir_chattr_msg;
  amd->capabilities = MU_AMD_STATUS;
  amd->mailbox_size = maildir_size;
  
  /* Set our properties.  */
  {
    mu_property_t property = NULL;
    mu_mailbox_get_property (mailbox, &property);
    mu_property_set_value (property, "TYPE", "MAILDIR", 1);
  }

  return 0;
}
#endif
