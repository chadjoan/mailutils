## This file is part of GNU Mailutils.
## Copyright (C) 2001-2002, 2007, 2009-2012, 2014-2016 Free Software
## Foundation, Inc.
##
## GNU Mailutils is free software; you can redistribute it and/or
## modify it under the terms of the GNU General Public License as
## published by the Free Software Foundation; either version 3, or (at
## your option) any later version.
##
## GNU Mailutils is distributed in the hope that it will be useful, but
## WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
## General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with GNU Mailutils.  If not, see <http://www.gnu.org/licenses/>.

SUBDIRS = . tests
sbin_PROGRAMS = comsatd

comsatd_SOURCES = action.c comsat.c comsat.h biff.rc.h
AM_CPPFLAGS = \
 @MU_APP_COMMON_INCLUDES@\
 -DSYSCONFDIR=\"$(sysconfdir)\"

BUILT_SOURCES=biff.rc.h
EXTRA_DIST=biffrc.sed biff.rc

biff.rc.h: $(top_srcdir)/comsat/biff.rc
	$(AM_V_GEN)sed -f $(top_srcdir)/comsat/biffrc.sed \
	  $(top_srcdir)/comsat/biff.rc > biff.rc.h

comsatd_LDADD = \
 ${MU_APP_LIBRARIES}\
 ${MU_LIB_MBOX}\
 ${MU_LIB_IMAP}\
 ${MU_LIB_POP}\
 ${MU_LIB_NNTP}\
 ${MU_LIB_MH}\
 ${MU_LIB_MAILDIR}\
 ${MU_LIB_MAILUTILS}\
 @MU_COMMON_LIBRARIES@

