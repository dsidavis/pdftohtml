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

CXXFLAGS +=  -g -O2 -DHAVE_CONFIG_H -I.. -I$(GOOSRCDIR) -I$(FOFISRCDIR) -I$(SPLASHSRCDIR) -I$(srcdir)           -I/usr/X11R6/include

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

CXX_SRC = $(srcdir)/Annot.cc \
	$(srcdir)/Array.cc \
	$(srcdir)/BuiltinFont.cc \
	$(srcdir)/BuiltinFontTables.cc \
	$(srcdir)/CMap.cc \
	$(srcdir)/Catalog.cc \
	$(srcdir)/CharCodeToUnicode.cc \
	$(srcdir)/Decrypt.cc \
	$(srcdir)/Dict.cc \
	$(srcdir)/Error.cc \
	$(srcdir)/FontEncodingTables.cc \
	$(srcdir)/Function.cc \
	$(srcdir)/Gfx.cc \
	$(srcdir)/GfxFont.cc \
	$(srcdir)/GfxState.cc \
	$(srcdir)/GlobalParams.cc \
	$(srcdir)/JArithmeticDecoder.cc \
	$(srcdir)/JBIG2Stream.cc \
	$(srcdir)/JPXStream.cc \
	$(srcdir)/Lexer.cc \
	$(srcdir)/Link.cc \
	$(srcdir)/NameToCharCode.cc \
	$(srcdir)/Object.cc \
	$(srcdir)/Outline.cc \
	$(srcdir)/OutputDev.cc \
	$(srcdir)/PDFDoc.cc \
	$(srcdir)/PDFDocEncoding.cc \
	$(srcdir)/PSOutputDev.cc \
	$(srcdir)/PSTokenizer.cc \
	$(srcdir)/Page.cc \
	$(srcdir)/Parser.cc \
	$(srcdir)/Stream.cc \
	$(srcdir)/UnicodeMap.cc \
	$(srcdir)/SecurityHandler.cc \
	$(srcdir)/UnicodeTypeTable.cc \
	$(srcdir)/XRef.cc 

#------------------------------------------------------------------------

XPDF_OBJS = Annot.o Array.o BuiltinFont.o BuiltinFontTables.o Catalog.o \
	CharCodeToUnicode.o CMap.o Decrypt.o Dict.o Error.o \
	FontEncodingTables.o Function.o Gfx.o GfxFont.o \
	GfxState.o GlobalParams.o JArithmeticDecoder.o JBIG2Stream.o \
	JPXStream.o Lexer.o Link.o NameToCharCode.o Object.o Outline.o \
	OutputDev.o Page.o Parser.o PDFDoc.o PDFDocEncoding.o PSOutputDev.o \
	PSTokenizer.o Stream.o UnicodeMap.o SecurityHandler.o UnicodeTypeTable.o \
	XRef.o 
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
