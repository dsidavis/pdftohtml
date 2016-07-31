
SHELL = /bin/sh

SRCDIR = .
XPDFSRCDIR = ../xpdf
XPDFLIBDIR = ../xpdf
GOOSRCDIR = ../goo
GOOLIBDIR = ../goo
FOFISRCDIR = ../fofi
FOFILIBDIR = ../fofi
SPLASHSRCDIR = ../splash
SPLASHLIBDIR = ../splash

CXXFLAGS = -g -O2 -DHAVE_CONFIG_H -DHAVE_DIRENT_H=1  -I.. -DHAVE_REWINDDIR=1 -DHAVE_POPEN=1 -I.. -I$(GOOSRCDIR) -I$(XPDFSRCDIR) -I$(FOFISRCDIR) -I$(SPLASHSRCDIR) -I$(srcdir)           -I/usr/X11R6/include

LDFLAGS = 

OTHERLIBS = 

CXX ?= c++

LIBPREFIX = lib
EXE = 

#------------------------------------------------------------------------

.SUFFIXES: .cc

.cc.o:
	$(CXX) $(CXXFLAGS) -c $<

#------------------------------------------------------------------------

CXX_SRC = \
	$(SRCDIR)/pdftohtml.cc \
	$(SRCDIR)/HtmlOutputDev.cc \
	$(SRCDIR)/HtmlFonts.cc \
	$(SRCDIR)/HtmlLinks.cc 

#------------------------------------------------------------------------

all: pdftohtml$(EXE)

#-------------------------------------------------------------------------

PDFTOHTML_OBJS = HtmlOutputDev.o HtmlFonts.o HtmlLinks.o \
    pdftohtml.o
PDFTOHTML_LIBS = -L$(GOOLIBDIR) -L$(FOFILIBDIR) -L$(SPLASHLIBDIR) -L$(XPDFLIBDIR) $(OTHERLIBS) -lXpdf -lGoo -lfofi -lsplash -lm

pdftohtml$(EXE): $(PDFTOHTML_OBJS) $(GOOLIBDIR)/$(LIBPREFIX)Goo.a
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o pdftohtml$(EXE) $(PDFTOHTML_OBJS) \
		$(PDFTOHTML_LIBS)

#-------------------------------------------------------------------------
clean:
	rm -f $(PDFTOHTML_OBJS) pdftohtml$(EXE)

#------------------------------------------------------------------------

distdepend:
	cp Makefile.in Makefile.in.bak
	sed '/^#----- dependences -----/q' Makefile.in.bak >Makefile.in
	$(CXX) $(CXXFLAGS) -MM $(CXX_SRC) >>Makefile.in

