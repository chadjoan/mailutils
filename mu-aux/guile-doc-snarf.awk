# Copyright (C) 2002, 2006, 2007 Sergey Poznyakoff
#
# This is a snarfer for guile version 1.6
# 
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 3 of the License, or
#  (at your option) any later version.
# 
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#  
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
#  02110-1301 USA.

BEGIN {
      cname = ""
}

function flush() {
	 if (cname == "")
	    return;
	 if (arg_req + arg_opt + arg_var != numargs)
	    error(cname " incorrectly defined as taking " numargs " arguments")

	 print "\f" fname
	 print "@c snarfed from " loc_source ":" loc_line
	 printf "@deffn {Scheme procedure} %s", fname
	 # All scheme primitives follow the same naming style:
	 # SCM argument names are in upper case.
	 # So, we convert them to lower case for @deffn line and
	 # replace their occurrences in the docstring by appropriate
	 # @var{} commands.
         for (i = 1; i <= numargs; i++) {
	    printf(" %s", tolower(arglist[i]))
	    gsub(arglist[i], "@var{" tolower(arglist[i]) "}", docstring)
         }
	 print ""
	 print docstring
	 print "@end deffn\n"
	     
	 delete argpos
	 delete arglist
	 cname = ""
}

function error(s) {
	 print loc_source ":" loc_line ": " s > "/dev/stderr"
	 exit 1
}

#{print NR ": " state}

state == 0 && NF != 0 { state = 1 }		

state == 1 && $1 == "cname" { cname = $2; next }
state == 1 && $1 == "fname" { fname = substr($2,2,length($2)-2); next }
state == 1 && $1 == "type" { type = $2; next }
state == 1 && $1 == "location" { loc_source = $2; loc_line = $3; next }
state == 1 && $1 == "arglist" {
	match($0, "\\(.*\\)")
	s = substr($0,RSTART+1,RLENGTH-2)
	numargs = split(s, a, ",")
	for (i = 1; i <= numargs; i++) {
	    m = split(a[i], b, "[ \t]*")
	    if (b[1] == "") {
	       t = b[2]
	       n = b[3]
	       m--
	    } else {
	       t = b[1]
	       n = b[2]
	    }   
	    if (m > 2 || t != "SCM")
	       error(cname ": wrong argument type for arg " i " " t)
	    arglist[i] = n
	}
	next
}

state == 1 && $1 == "argsig" { arg_req = $2; arg_opt = $3; arg_var = $4; next }
state == 1 && $1 == "argpos" { argpos[$2] = $3; next }

state == 1 && / *\"/    {
        # Concatenate strings. A very simplified version, but
        # works for us
	gsub("\\\\n\" *\"", "\n")
        gsub("\"\"", "")
	gsub("\\\\n", "\n")
	match($0,"\".*\"")
	docstring = substr($0,RSTART+1,RLENGTH-2)
}

state == 1 && NF==0 { flush(); state = 0; }

END {
	flush()
}