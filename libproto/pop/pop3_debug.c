/* GNU Mailutils -- a suite of utilities for electronic mail
   Copyright (C) 2003 Free Software Foundation, Inc.

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

#include <stdlib.h>
#include <errno.h>
#include <mailutils/sys/pop3.h>

int
mu_pop3_set_debug (mu_pop3_t pop3, mu_debug_t debug)
{
  if (pop3 == NULL)
    return EINVAL;
  if (pop3->debug)
    mu_debug_destroy (&pop3->debug, NULL);
  pop3->debug = debug;
  return 0;
}

int
mu_pop3_debug_cmd (mu_pop3_t pop3)
{
  if (pop3->debug)
    mu_debug_print(pop3->debug, MU_DEBUG_PROT, "%s", pop3->io.buf);
  return 0;
}

int
mu_pop3_debug_ack (mu_pop3_t pop3)
{
  if (pop3->debug)
    {
      mu_debug_print (pop3->debug, MU_DEBUG_PROT, "%s\n", pop3->ack.buf);
    }
  return 0;
}