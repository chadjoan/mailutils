## This file is part of GNU Mailutils.
## Copyright (C) 2002-2005, 2007, 2009-2012, 2014-2016 Free Software
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

YLWRAP = $(SHELL) $(mu_aux_dir)/gylwrap
AM_YFLAGS = -dtv
#AM_LEXFLAGS=-dvp
AM_CPPFLAGS =\
 @MU_LIB_COMMON_INCLUDES@\
 -DMU_SIEVE_MODDIR=\"@MU_SIEVE_MODDIR@\"

SUBDIRS = . extensions

lib_LTLIBRARIES = libmu_sieve.la

libmu_sieve_la_SOURCES = \
 actions.c\
 conf.c\
 comparator.c\
 encoded.c\
 environment.c\
 load.c\
 mem.c\
 prog.c\
 registry.c\
 relational.c\
 require.c\
 runtime.c\
 sieve-gram.c\
 sieve-gram.h\
 sieve-lex.c\
 strexp.c\
 string.c\
 tests.c\
 util.c\
 variables.c
libmu_sieve_la_LIBADD = ${MU_LIB_MAILUTILS} @LTDL_LIB@
libmu_sieve_la_LDFLAGS = -version-info @VI_CURRENT@:@VI_REVISION@:@VI_AGE@

noinst_HEADERS = sieve-priv.h

BUILT_SOURCES = \
  sieve-gram.c sieve-gram.h \
  sieve-lex.c

MAINTAINERCLEANFILES=$(BUILT_SOURCES)

EXTRA_DIST = sieve.y sieve.l

sieve-lex.c: $(srcdir)/sieve.l sieve-gram.h
	$(YLWRAP) "$(LEX) $(AM_LEXFLAGS) $(LEXFLAGS)" \
                  $(srcdir)/sieve.l lex.yy.c sieve-lex.c \
                  -- -yy mu_sieve_yy

sieve-gram.c sieve-gram.h: $(srcdir)/sieve.y
	$(YLWRAP) "$(YACC) $(AM_YFLAGS) $(YFLAGS)" $(srcdir)/sieve.y \
	          y.tab.c sieve-gram.c y.tab.h sieve-gram.h y.output y.output \
                  -- -yy mu_sieve_yy


