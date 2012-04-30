# Copyright (c) 2012 David Vossel <dvossel@redhat.com>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
# 
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
# 
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
#
#

MAINTAINERCLEANFILES = Makefile.in

INCLUDES         = -I matahari/ -I$(top_builddir)/include

lib_LTLIBRARIES = libpservice.la

libpservice_la_SOURCES = services.c services_linux.c utilities.c utilities_linux.c
libpservice_la_LDFLAGS = -version-info 1:0:0
libpservice_la_LIBADD = -lsigar

AM_CFLAGS = $(INCLUDES)
