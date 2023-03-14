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

CXXFLAGS += -g -DHAVE_CONFIG_H -I.. -I$(GOOSRCDIR) -I$(FOFISRCDIR) -I$(SPLASHSRCDIR) -I$(srcdir) -I/usr/X11R6/include  -Wno-write-strings

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

CXX_SRC=$(wildcard *.cc)
# If use wildcard with GNU make 4.3 or 4.4 on my OSX machine in this directory, get segmentation fault.

CXX_SRC=AcroForm.cc \
Annot.cc \
Array.cc \
BuiltinFont.cc \
BuiltinFontTables.cc \
CMap.cc \
Catalog.cc \
CharCodeToUnicode.cc \
Decrypt.cc \
Dict.cc \
DisplayState.cc \
Error.cc \
FontEncodingTables.cc \
Function.cc \
Gfx.cc \
GfxFont.cc \
GfxState.cc \
GlobalParams.cc \
HTMLGen.cc \
ImageOutputDev.cc \
JArithmeticDecoder.cc \
JBIG2Stream.cc \
JPXStream.cc \
Lexer.cc \
Link.cc \
NameToCharCode.cc \
Object.cc \
OptionalContent.cc \
Outline.cc \
OutputDev.cc \
PDF417Barcode.cc \
PDFCore.cc \
PDFDoc.cc \
PDFDocEncoding.cc \
PSOutputDev.cc \
PSTokenizer.cc \
Page.cc \
Parser.cc \
PreScanOutputDev.cc \
SecurityHandler.cc \
ShadingImage.cc \
SplashOutputDev.cc \
Stream.cc \
TextOutputDev.cc \
TextString.cc \
TileCache.cc \
TileCompositor.cc \
TileMap.cc \
UTF8.cc \
UnicodeMap.cc \
UnicodeRemapping.cc \
UnicodeTypeTable.cc \
WebFont.cc \
XFAScanner.cc \
XRef.cc \
Zoox.cc \
pdfdetach.cc \
pdffonts.cc \
pdfimages.cc \
pdfinfo.cc \
pdftohtml.cc \
pdftopng.cc \
pdftoppm.cc \
pdftops.cc \
pdftotext.cc


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
