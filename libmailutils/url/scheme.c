/* GNU Mailutils -- a suite of utilities for electronic mail
   Copyright (C) 2010 Free Software Foundation, Inc.

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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif

#include <mailutils/types.h>
#include <mailutils/cstr.h>
#include <mailutils/sys/url.h>

int
mu_url_set_scheme (mu_url_t url, const char *scheme)
{
  char *p;
  if (!url || !scheme)
    return EINVAL;
  p = realloc (url->scheme, strlen (scheme) + 1);
  if (!p)
    return ENOMEM;
  strcpy (url->scheme, scheme);
  return 0;
}

int
mu_url_is_scheme (mu_url_t url, const char *scheme)
{
  if (url && scheme && url->scheme 
      && mu_c_strcasecmp (url->scheme, scheme) == 0)
    return 1;

  return 0;
}
