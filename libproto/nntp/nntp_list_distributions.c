/* GNU Mailutils -- a suite of utilities for electronic mail
   Copyright (C) 2004-2005, 2007, 2010-2012, 2014-2016 Free Software
   Foundation, Inc.

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

#include <string.h>

#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <mailutils/error.h>
#include <mailutils/sys/nntp.h>

/*
  LIST xxxx command, return an iterator that contains the result.
  It is the responsability of the caller to destroy the iterator(mu_iterator_destroy).
 */
int
mu_nntp_list_distributions (mu_nntp_t nntp, const char *wildmat, mu_iterator_t *piterator)
{
  int status = 0;

  if (nntp == NULL)
    return EINVAL;
  if (piterator == NULL)
    return MU_ERR_OUT_PTR_NULL;

  switch (nntp->state)
    {
    case MU_NNTP_NO_STATE:
      if (wildmat == NULL || *wildmat == '\0')
	status = mu_nntp_writeline (nntp, "LIST DISTRIBUTIONS\r\n");
      else
	status = mu_nntp_writeline (nntp, "LIST DISTRIBUTIONS %s\r\n", wildmat);
      MU_NNTP_CHECK_ERROR (nntp, status);
      mu_nntp_debug_cmd (nntp);
      nntp->state = MU_NNTP_LIST_DISTRIBUTIONS;

    case MU_NNTP_LIST_DISTRIBUTIONS:
      status = mu_nntp_send (nntp);
      MU_NNTP_CHECK_EAGAIN (nntp, status);
      nntp->acknowledge = 0;
      nntp->state = MU_NNTP_LIST_DISTRIBUTIONS_ACK;

    case MU_NNTP_LIST_DISTRIBUTIONS_ACK:
      status = mu_nntp_response (nntp, NULL, 0, NULL);
      MU_NNTP_CHECK_EAGAIN (nntp, status);
      mu_nntp_debug_ack (nntp);
      MU_NNTP_CHECK_CODE (nntp, MU_NNTP_RESP_CODE_LIST_FOLLOW);
      status = mu_nntp_iterator_create (nntp, piterator);
      MU_NNTP_CHECK_ERROR(nntp, status);
      nntp->state = MU_NNTP_LIST_DISTRIBUTIONS_RX;

    case MU_NNTP_LIST_DISTRIBUTIONS_RX:
      break;

      /* They must deal with the error first by reopening.  */
    case MU_NNTP_ERROR:
      status = ECANCELED;
      break;

    default:
      status = EINPROGRESS;
    }

  return status;
}

int
mu_nntp_parse_list_distributions (const char *buffer, char **key, char **value)
{
  char *k;
  char *v;

  if (buffer == NULL || *buffer == '\0')
    return EINVAL;

  k = calloc(512, 1);
  if (k == NULL)
    return ENOMEM;

  v = calloc(512, 1);
  if (v == NULL)
    {
      free (k);
      return ENOMEM;
    }

  sscanf (buffer, "%511s %511s", k, v);

  if (key == NULL)
    free (k);
  else
    *key = k;

  if (value == NULL)
    free (v);
  else
    *value = v;
  return 0;
}
