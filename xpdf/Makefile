# Generated automatically from Makefile.in by configure.
#========================================================================
#
# Xpdf Makefile
#
# Copyright 1996 Derek B. Noonburg
#
#========================================================================

SHELL = /bin/sh

srcdir = .

GOOSRCDIR = $(srcdir)/../goo
GOOLIBDIR = ../goo
FOFISRCDIR = $(srcdir)/../fofi
FOFILIBDIR = ../fofi
SPLASHSRCDIR = $(srcdir)/../splash
SPLASHLIBDIR = ../splash

CXXFLAGS += -g -DHAVE_CONFIG_H -I.. -I$(GOOSRCDIR) -I$(FOFISRCDIR) -I$(SPLASHSRCDIR) -I$(srcdir)           -I/usr/X11R6/include  -Wno-write-strings

CC ?= cc
CXX ?= c++
AR = ar rc
RANLIB = ranlib

LIBPREFIX = lib

#------------------------------------------------------------------------

.SUFFIXES: .cc

.cc.o:
	$(CXX) $(CXXFLAGS) -c $<

#------------------------------------------------------------------------

CXX_SRC = $(wildcard *.cc)

#------------------------------------------------------------------------

XPDF_OBJS = $(patsubst %.cc,%.o,$(CXX_SRC))

$(LIBPREFIX)Xpdf.a: $(XPDF_OBJS)
	rm -f $(LIBPREFIX)Xpdf.a
	$(AR) $(LIBPREFIX)Xpdf.a $(XPDF_OBJS)
	$(RANLIB) $(LIBPREFIX)Xpdf.a

#------------------------------------------------------------------------

clean:
	rm -f $(XPDF_OBJS) $(LIBPREFIX)Xpdf.a

#------------------------------------------------------------------------

depend:
	$(CXX) $(CXXFLAGS) -MM $(CXX_SRC) >Makefile.dep
