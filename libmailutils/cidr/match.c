/* GNU Mailutils -- a suite of utilities for electronic mail
   Copyright (C) 2011 Free Software Foundation, Inc.

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
#include <mailutils/cidr.h>
#include <mailutils/errno.h>

int
mu_cidr_match (struct mu_cidr *a, struct mu_cidr *b)
{
  int i;

  if (a->family != b->family)
    return 1;
  for (i = 0; i < a->len; i++)
    {
      if (a->address[i] != (b->address[i] & a->netmask[i]))
	return 1;
    }
  return 0;
}
  