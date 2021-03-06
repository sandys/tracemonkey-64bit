#
# ***** BEGIN LICENSE BLOCK *****
# Version: MPL 1.1/GPL 2.0/LGPL 2.1
#
# The contents of this file are subject to the Mozilla Public License Version
# 1.1 (the "License"); you may not use this file except in compliance with
# the License. You may obtain a copy of the License at
# http://www.mozilla.org/MPL/
#
# Software distributed under the License is distributed on an "AS IS" basis,
# WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
# for the specific language governing rights and limitations under the
# License.
#
# The Original Code is Mozilla Communicator client code, released
# March 31, 1998.
#
# The Initial Developer of the Original Code is
# Netscape Communications Corporation.
# Portions created by the Initial Developer are Copyright (C) 1998
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#
# Alternatively, the contents of this file may be used under the terms of
# either of the GNU General Public License Version 2 or later (the "GPL"),
# or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
# in which case the provisions of the GPL or the LGPL are applicable instead
# of those above. If you wish to allow use of your version of this file only
# under the terms of either the GPL or the LGPL, and not to allow others to
# use your version of this file under the terms of the MPL, indicate your
# decision by deleting the provisions above and replace them with the notice
# and other provisions required by the GPL or the LGPL. If you do not delete
# the provisions above, a recipient may use your version of this file under
# the terms of any one of the MPL, the GPL or the LGPL.
#
# ***** END LICENSE BLOCK *****

DEPTH		= ../../../..
topsrcdir	= ../../../..
srcdir		= .
VPATH		= .

include $(DEPTH)/config/autoconf.mk

MODULE		= java

#
# the idea is that the install phase for those modules will
# make their own signed jar files
# there are some exceptions here that we deal with
# stuff in modules and in ns/js/jsj
#
JMODS		= netscape/javascript netscape/javascript/adapters

#
# JDIRS is dependant on JAVA_DESTPATH in config/rules.m[a]k. 
# Be sure to touch that directory if you add a new directory to
# JDIRS, or else it will not build. FIXME
#
JDIRS		= $(JMODS)

JAR_JSJ		= jsj10.jar
JAR_JSJ_CLASSES	= $(JMODS)

#
# jars to build at install time
#
JARS		= $(JAR_JSJ)

include $(topsrcdir)/config/rules.mk

JAVA_SOURCEPATH	= $(topsrcdir)/js/src/liveconnect/classes

doc::
	$(JAVADOC) -d $(DIST)/doc netscape.javascript

natives_list:: FORCE
	rm -rf $@
	find . -name "*.class" -print | sed 's@\./\(.*\)\.class$$@\1@' | \
		sed 's@/@.@g' | xargs $(JVH) -natives | sort > $@

check_natives:: natives_list
	rm -f found_natives
	nm -B	../*.o \
	    | egrep "Java.*_stub" | awk '{ print $$3; }' | sort > found_natives
	diff found_natives natives_list

FORCE:
