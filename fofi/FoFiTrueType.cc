//========================================================================
//
// FoFiTrueType.cc
//
// Copyright 1999-2003 Glyph & Cog, LLC
//
//========================================================================

#include <aconf.h>

#ifdef USE_GCC_PRAGMAS
#pragma implementation
#endif

#include <stdlib.h>
#include <string.h>
#if HAVE_STD_SORT
#include <algorithm>
#endif
#include "gtypes.h"
#include "gmem.h"
#include "gmempp.h"
#include "GString.h"
#include "GHash.h"
#include "FoFiType1C.h"
#include "FoFiTrueType.h"

//
// Terminology
// -----------
//
// character code = number used as an element of a text string
//
// character name = glyph name = name for a particular glyph within a
//                  font
//
// glyph index = GID = position (within some internal table in the font)
//               where the instructions to draw a particular glyph are
//               stored
//
// Type 1 fonts
// ------------
//
// Type 1 fonts contain:
//
// Encoding: array of glyph names, maps char codes to glyph names
//
//           Encoding[charCode] = charName
//
// CharStrings: dictionary of instructions, keyed by character names,
//              maps character name to glyph data
//
//              CharStrings[charName] = glyphData
//
// TrueType fonts
// --------------
//
// TrueType fonts contain:
//
// 'cmap' table: mapping from character code to glyph index; there may
//               be multiple cmaps in a TrueType font
//
//               cmap[charCode] = gid
//
// 'post' table: mapping from glyph index to glyph name
//
//               post[gid] = glyphName
//
// Type 42 fonts
// -------------
//
// Type 42 fonts contain:
//
// Encoding: array of glyph names, maps char codes to glyph names
//
//           Encoding[charCode] = charName
//
// CharStrings: dictionary of glyph indexes, keyed by character names,
//              maps character name to glyph index
//
//              CharStrings[charName] = gid
//

//------------------------------------------------------------------------

#define ttcfTag 0x74746366

//------------------------------------------------------------------------

struct TrueTypeTable {
  Guint tag;
  Guint checksum;
  int offset;
  int origOffset;
  int len;
};

struct TrueTypeCmap {
  int platform;
  int encoding;
  int offset;
  int len;
  int fmt;
};

struct TrueTypeLoca {
  int idx;
  int origOffset;
  int newOffset;
  int len;
};

#define cmapTag 0x636d6170
#define glyfTag 0x676c7966
#define headTag 0x68656164
#define hheaTag 0x68686561
#define hmtxTag 0x686d7478
#define locaTag 0x6c6f6361
#define nameTag 0x6e616d65
#define os2Tag  0x4f532f32
#define postTag 0x706f7374

#if HAVE_STD_SORT

struct cmpTrueTypeLocaOffsetFunctor {
  bool operator()(const TrueTypeLoca &loca1, const TrueTypeLoca &loca2) {
    if (loca1.origOffset == loca2.origOffset) {
      return loca1.idx < loca2.idx;
    }
    return loca1.origOffset < loca2.origOffset;
  }
};

struct cmpTrueTypeLocaIdxFunctor {
  bool operator()(const TrueTypeLoca &loca1, const TrueTypeLoca &loca2) {
    return loca1.idx < loca2.idx;
  }
};

struct cmpTrueTypeTableTagFunctor {
  bool operator()(const TrueTypeTable &tab1, const TrueTypeTable &tab2) {
    return tab1.tag < tab2.tag;
  }
};

#else // HAVE_STD_SORT

static int cmpTrueTypeLocaOffset(const void *p1, const void *p2) {
  TrueTypeLoca *loca1 = (TrueTypeLoca *)p1;
  TrueTypeLoca *loca2 = (TrueTypeLoca *)p2;

  if (loca1->origOffset == loca2->origOffset) {
    return loca1->idx - loca2->idx;
  }
  return loca1->origOffset - loca2->origOffset;
}

static int cmpTrueTypeLocaIdx(const void *p1, const void *p2) {
  TrueTypeLoca *loca1 = (TrueTypeLoca *)p1;
  TrueTypeLoca *loca2 = (TrueTypeLoca *)p2;

  return loca1->idx - loca2->idx;
}

static int cmpTrueTypeTableTag(const void *p1, const void *p2) {
  TrueTypeTable *tab1 = (TrueTypeTable *)p1;
  TrueTypeTable *tab2 = (TrueTypeTable *)p2;

  return (int)tab1->tag - (int)tab2->tag;
}

#endif // HAVE_STD_SORT

//------------------------------------------------------------------------

struct T42Table {
  const char *tag;		// 4-byte tag
  GBool required;		// required by the TrueType spec?
};

// TrueType tables to be embedded in Type 42 fonts.
// NB: the table names must be in alphabetical order here.
#define nT42Tables 11
static T42Table t42Tables[nT42Tables] = {
  { "cvt ", gTrue  },
  { "fpgm", gTrue  },
  { "glyf", gTrue  },
  { "head", gTrue  },
  { "hhea", gTrue  },
  { "hmtx", gTrue  },
  { "loca", gTrue  },
  { "maxp", gTrue  },
  { "prep", gTrue  },
  { "vhea", gFalse },
  { "vmtx", gFalse }
};
#define t42HeadTable  3
#define t42LocaTable  6
#define t42GlyfTable  2
#define t42VheaTable  9
#define t42VmtxTable 10

//------------------------------------------------------------------------

// Glyph names in some arbitrary standard order that Apple uses for
// their TrueType fonts.
static const char *macGlyphNames[258] = {
  ".notdef",        "null",           "CR",             "space",
  "exclam",         "quotedbl",       "numbersign",     "dollar",
  "percent",        "ampersand",      "quotesingle",    "parenleft",
  "parenright",     "asterisk",       "plus",           "comma",
  "hyphen",         "period",         "slash",          "zero",
  "one",            "two",            "three",          "four",
  "five",           "six",            "seven",          "eight",
  "nine",           "colon",          "semicolon",      "less",
  "equal",          "greater",        "question",       "at",
  "A",              "B",              "C",              "D",
  "E",              "F",              "G",              "H",
  "I",              "J",              "K",              "L",
  "M",              "N",              "O",              "P",
  "Q",              "R",              "S",              "T",
  "U",              "V",              "W",              "X",
  "Y",              "Z",              "bracketleft",    "backslash",
  "bracketright",   "asciicircum",    "underscore",     "grave",
  "a",              "b",              "c",              "d",
  "e",              "f",              "g",              "h",
  "i",              "j",              "k",              "l",
  "m",              "n",              "o",              "p",
  "q",              "r",              "s",              "t",
  "u",              "v",              "w",              "x",
  "y",              "z",              "braceleft",      "bar",
  "braceright",     "asciitilde",     "Adieresis",      "Aring",
  "Ccedilla",       "Eacute",         "Ntilde",         "Odieresis",
  "Udieresis",      "aacute",         "agrave",         "acircumflex",
  "adieresis",      "atilde",         "aring",          "ccedilla",
  "eacute",         "egrave",         "ecircumflex",    "edieresis",
  "iacute",         "igrave",         "icircumflex",    "idieresis",
  "ntilde",         "oacute",         "ograve",         "ocircumflex",
  "odieresis",      "otilde",         "uacute",         "ugrave",
  "ucircumflex",    "udieresis",      "dagger",         "degree",
  "cent",           "sterling",       "section",        "bullet",
  "paragraph",      "germandbls",     "registered",     "copyright",
  "trademark",      "acute",          "dieresis",       "notequal",
  "AE",             "Oslash",         "infinity",       "plusminus",
  "lessequal",      "greaterequal",   "yen",            "mu1",
  "partialdiff",    "summation",      "product",        "pi",
  "integral",       "ordfeminine",    "ordmasculine",   "Ohm",
  "ae",             "oslash",         "questiondown",   "exclamdown",
  "logicalnot",     "radical",        "florin",         "approxequal",
  "increment",      "guillemotleft",  "guillemotright", "ellipsis",
  "nbspace",        "Agrave",         "Atilde",         "Otilde",
  "OE",             "oe",             "endash",         "emdash",
  "quotedblleft",   "quotedblright",  "quoteleft",      "quoteright",
  "divide",         "lozenge",        "ydieresis",      "Ydieresis",
  "fraction",       "currency",       "guilsinglleft",  "guilsinglright",
  "fi",             "fl",             "daggerdbl",      "periodcentered",
  "quotesinglbase", "quotedblbase",   "perthousand",    "Acircumflex",
  "Ecircumflex",    "Aacute",         "Edieresis",      "Egrave",
  "Iacute",         "Icircumflex",    "Idieresis",      "Igrave",
  "Oacute",         "Ocircumflex",    "applelogo",      "Ograve",
  "Uacute",         "Ucircumflex",    "Ugrave",         "dotlessi",
  "circumflex",     "tilde",          "overscore",      "breve",
  "dotaccent",      "ring",           "cedilla",        "hungarumlaut",
  "ogonek",         "caron",          "Lslash",         "lslash",
  "Scaron",         "scaron",         "Zcaron",         "zcaron",
  "brokenbar",      "Eth",            "eth",            "Yacute",
  "yacute",         "Thorn",          "thorn",          "minus",
  "multiply",       "onesuperior",    "twosuperior",    "threesuperior",
  "onehalf",        "onequarter",     "threequarters",  "franc",
  "Gbreve",         "gbreve",         "Idot",           "Scedilla",
  "scedilla",       "Cacute",         "cacute",         "Ccaron",
  "ccaron",         "dmacron"
};

//------------------------------------------------------------------------
// FoFiTrueType
//------------------------------------------------------------------------

FoFiTrueType *FoFiTrueType::make(char *fileA, int lenA, int fontNum,
				 GBool allowHeadlessCFF) {
  FoFiTrueType *ff;

  ff = new FoFiTrueType(fileA, lenA, gFalse, fontNum, gFalse, allowHeadlessCFF);
  if (!ff->parsedOk) {
    delete ff;
    return NULL;
  }
  return ff;
}

FoFiTrueType *FoFiTrueType::load(char *fileName, int fontNum,
				 GBool allowHeadlessCFF) {
  FoFiTrueType *ff;
  char *fileA;
  int lenA, n;
  GBool isDfontA;

  if (!(fileA = FoFiBase::readFile(fileName, &lenA))) {
    return NULL;
  }
  n = (int)strlen(fileName);
  isDfontA = n >= 6 && !strcmp(fileName + n - 6, ".dfont");
  ff = new FoFiTrueType(fileA, lenA, gTrue, fontNum, isDfontA,
			allowHeadlessCFF);
  if (!ff->parsedOk) {
    delete ff;
    return NULL;
  }
  return ff;
}

FoFiTrueType::FoFiTrueType(char *fileA, int lenA, GBool freeFileDataA,
			   int fontNum, GBool isDfontA,
			   GBool allowHeadlessCFF):
  FoFiBase(fileA, lenA, freeFileDataA)
{
  tables = NULL;
  nTables = 0;
  cmaps = NULL;
  nCmaps = 0;
  nameToGID = NULL;
  isDfont = isDfontA;
  isTTC = gFalse;
  parsedOk = gFalse;

  parse(fontNum, allowHeadlessCFF);
}

FoFiTrueType::~FoFiTrueType() {
  gfree(tables);
  gfree(cmaps);
  if (nameToGID) {
    delete nameToGID;
  }
}

int FoFiTrueType::getNumCmaps() {
  return nCmaps;
}

int FoFiTrueType::getCmapPlatform(int i) {
  return cmaps[i].platform;
}

int FoFiTrueType::getCmapEncoding(int i) {
  return cmaps[i].encoding;
}

int FoFiTrueType::findCmap(int platform, int encoding) {
  int i;

  for (i = 0; i < nCmaps; ++i) {
    if (cmaps[i].platform == platform && cmaps[i].encoding == encoding) {
      return i;
    }
  }
  return -1;
}

int FoFiTrueType::mapCodeToGID(int i, int c) {
  int gid;
  int segCnt, segEnd, segStart, segDelta, segOffset;
  int cmapFirst, cmapLen;
  int pos, a, b, m;
  GBool ok;

  if (i < 0 || i >= nCmaps) {
    return 0;
  }
  ok = gTrue;
  pos = cmaps[i].offset;
  switch (cmaps[i].fmt) {
  case 0:
    if (c < 0 || c >= cmaps[i].len - 6) {
      return 0;
    }
    gid = getU8(pos + 6 + c, &ok);
    break;
  case 2:
    // this only handles single-byte codes
    if (c < 0 || c > 0xff) {
      return 0;
    }
    // check that: subHeaderKeys[0] = 0
    //             subHeaders[0].firstCode = 0
    //             subHeaders[0].entryCount = 256
    //             subHeaders[0].idDelta = 0
    if (getU16BE(pos + 6, &ok) != 0 ||
	getU16BE(pos + 518 + 0, &ok) != 0 ||
	getU16BE(pos + 518 + 2, &ok) != 256 ||
	getU16BE(pos + 518 + 4, &ok) != 0) {
      return 0;
    }
    // subHeaders[0].idRangeOffset is a byte offset from itself
    pos = pos + 518 + 6 + getU16BE(pos + 518 + 6, &ok);
    gid = getU16BE(pos + 2 * c, &ok);
    break;
  case 4:
    segCnt = getU16BE(pos + 6, &ok) / 2;
    a = -1;
    b = segCnt - 1;
    segEnd = getU16BE(pos + 14 + 2*b, &ok);
    if (c > segEnd) {
      // malformed font -- the TrueType spec requires the last segEnd
      // to be 0xffff
      return 0;
    }
    // invariant: seg[a].end < code <= seg[b].end
    while (b - a > 1 && ok) {
      m = (a + b) / 2;
      segEnd = getU16BE(pos + 14 + 2*m, &ok);
      if (segEnd < c) {
	a = m;
      } else {
	b = m;
      }
    }
    segStart = getU16BE(pos + 16 + 2*segCnt + 2*b, &ok);
    segDelta = getU16BE(pos + 16 + 4*segCnt + 2*b, &ok);
    segOffset = getU16BE(pos + 16 + 6*segCnt + 2*b, &ok);
    if (c < segStart) {
      return 0;
    }
    if (segOffset == 0) {
      gid = (c + segDelta) & 0xffff;
    } else {
      gid = getU16BE(pos + 16 + 6*segCnt + 2*b +
		     segOffset + 2 * (c - segStart), &ok);
      if (gid != 0) {
	gid = (gid + segDelta) & 0xffff;
      }
    }
    break;
  case 6:
    cmapFirst = getU16BE(pos + 6, &ok);
    cmapLen = getU16BE(pos + 8, &ok);
    if (c < cmapFirst || c >= cmapFirst + cmapLen) {
      return 0;
    }
    gid = getU16BE(pos + 10 + 2 * (c - cmapFirst), &ok);
    break;
  default:
    return 0;
  }
  if (!ok) {
    return 0;
  }
  return gid;
}

int FoFiTrueType::mapNameToGID(char *name) {
  if (!nameToGID) {
    return 0;
  }
  return nameToGID->lookupInt(name);
}

GBool FoFiTrueType::getCFFBlock(char **start, int *length) {
  int i;

  if (!openTypeCFF) {
    return gFalse;
  }
  i = seekTable("CFF ");
  if (!checkRegion(tables[i].offset, tables[i].len)) {
    return gFalse;
  }
  *start = (char *)file + tables[i].offset;
  *length = tables[i].len;
  return gTrue;
}

int *FoFiTrueType::getCIDToGIDMap(int *nCIDs) {
  char *start;
  int length;
  FoFiType1C *ff;
  int *map;

  *nCIDs = 0;
  if (!getCFFBlock(&start, &length)) {
    return NULL;
  }
  if (!(ff = FoFiType1C::make(start, length))) {
    return NULL;
  }
  map = ff->getCIDToGIDMap(nCIDs);
  delete ff;
  return map;
}

int FoFiTrueType::getEmbeddingRights() {
  int i, fsType;
  GBool ok;

  if ((i = seekTable("OS/2")) < 0) {
    return 4;
  }
  ok = gTrue;
  fsType = getU16BE(tables[i].offset + 8, &ok);
  if (!ok) {
    return 4;
  }
  if (fsType & 0x0008) {
    return 2;
  }
  if (fsType & 0x0004) {
    return 1;
  }
  if (fsType & 0x0002) {
    return 0;
  }
  return 3;
}

void FoFiTrueType::getFontMatrix(double *mat) {
  char *start;
  int length;
  FoFiType1C *ff;

  if (!getCFFBlock(&start, &length)) {
    return;
  }
  if (!(ff = FoFiType1C::make(start, length))) {
    return;
  }
  ff->getFontMatrix(mat);
  delete ff;
}

struct FoFiTrueTypeTrickyFont {
  int cvtLen;
  Guint cvtChecksum;
  int fpgmLen;
  Guint fpgmChecksum;
  int prepLen;
  Guint prepChecksum;
};

// This data was taken from the FreeType project.
static FoFiTrueTypeTrickyFont trickyFonts[] = {
  { 0x000002e4, 0x05bcf058,	// MingLiU 1995
    0x000087c4, 0x28233bf1,
    0x000001e1, 0xa344a1ea },
  { 0x000002e4, 0x05bcf058,	// MingLiU 1996-
    0x000087c4, 0x28233bf1,
    0x000001e1, 0xa344a1eb },
  { 0x00000350,	0x12c3ebb2,	// DFGothic-EB
    0x000087a7, 0xb680ee64,
    0x00000758, 0xce939563 },
  { 0x00000350, 0x11e5ead4,	// DFGyoSho-Lt
    0x0000bc85, 0xce5956e9,
    0x00000045, 0x8272f416 },
  { 0x00000350, 0x1262eb4e,	// DFHSGothic-W5
    0x00007940, 0xe86a5d64,
    0x000005ff, 0x7850f729 },
  { 0x00000350, 0x122deb0a,	// DFHSMincho-W3
    0x0000859b, 0x3d16328a,
    0x000002cb, 0xa93fc33b },
  { 0x00000350, 0x125feb26,	// DFHSMincho-W7
    0x00007ee1, 0xa5acc982,
    0x0000041f, 0x90999196 },
  { 0x00000350, 0x11e5ead4,	// DFKaiShu
    0x00009063, 0x5a30ca3b,
    0x0000007e, 0x13a42602 },
  { 0x00000350, 0x11e5ead4,	// DFKaiShu2
    0x00008998, 0xa6e78c01,
    0x0000007e, 0x13a42602 },
  { 0x00000008, 0xfffbfffc,	// HuaTianKaiTi
    0x0000bea2, 0x9c9e48b8,
    0x00000008, 0x70020112 },
  { 0x00000008, 0xfffbfffc,	// HuaTianSongTi
    0x00017c39, 0x0a5a0483,
    0x00000008, 0x70020112 },
  { 0x00000000, 0x00000000,	// NEC fadpop7.ttf
    0x000000e5, 0x40c92555,
    0x0000117c, 0xa39b58e3 },
  { 0x00000000, 0x00000000,	// NEC fadrei5.ttf
    0x000000e5, 0x33c41652,
    0x00000f6a, 0x26d6c52a },
  { 0x00000000, 0x00000000,	// NEC fangot7.ttf
    0x0000019d, 0x6db1651d,
    0x00002492, 0x6c6e4b03 },
  { 0x00000000, 0x00000000,	// NEC fangyo5.ttf
    0x000000e5, 0x40c92555,
    0x0000117c, 0xde51fad0 },
  { 0x00000000, 0x00000000,	// NEC fankyo5.ttf
    0x000000e5, 0x85e47664,
    0x00001caa, 0xa6c62831 },
  { 0x00000000, 0x00000000,	// NEC fanrgo5.ttf
    0x0000019d, 0x2d891cfd,
    0x00001de8, 0xa0604633 },
  { 0x00000000, 0x00000000,	// NEC fangot5.ttc
    0x000001cb, 0x40aa774c,
    0x00001f9a, 0x9b5caa96 },
  { 0x00000000, 0x00000000,	// NEC fanmin3.ttc
    0x00000141, 0x0d3de9cb,
    0x00002280, 0xd4127766 },
  { 0x00000000, 0x00000000,	// NEC FA-Gothic, 1996
    0x000001f0, 0x4a692698,
    0x00001fca, 0x340d4346 },
  { 0x00000000, 0x00000000,	// NEC FA-Minchou, 1996
    0x00000166, 0xcd34c604,
    0x000022b0, 0x6cf31046 },
  { 0x00000000, 0x00000000,	// NEC FA-RoundGothicB, 1996
    0x0000019d, 0x5da75315,
    0x000022e0, 0x40745a5f },
  { 0x00000000, 0x00000000,	// NEC FA-RoundGothicM, 1996
    0x000001c2, 0xf055fc48,
    0x00001e18, 0x3900ded3 },
  { 0x00000060, 0x00170003,	// MINGLI, 1992
    0x000058aa, 0xdbb4306e,
    0x00000035, 0xd643482a }
};

#define nTrickyFonts ((int)(sizeof(trickyFonts) / sizeof(FoFiTrueTypeTrickyFont)))

// This looks at the length and checksum values for the cvt, fpgm, and
// prep tables.  The theory is that those tables will be the same,
// even if the font is renamed and/or subsetted.
GBool FoFiTrueType::checkForTrickyCJK() {
  int cvtIdx, fpgmIdx, prepIdx;
  int cvtLen, fpgmLen, prepLen;
  Guint cvtChecksum, fpgmChecksum, prepChecksum;
  int i;

  if ((cvtIdx = seekTable("cvt ")) >= 0) {
    cvtLen = tables[cvtIdx].len;
    cvtChecksum = tables[cvtIdx].checksum;
  } else {
    cvtLen = 0;
    cvtChecksum = 0;
  }
  if ((fpgmIdx = seekTable("fpgm")) >= 0) {
    fpgmLen = tables[fpgmIdx].len;
    fpgmChecksum = tables[fpgmIdx].checksum;
  } else {
    fpgmLen = 0;
    fpgmChecksum = 0;
  }
  if ((prepIdx = seekTable("prep")) >= 0) {
    prepLen = tables[prepIdx].len;
    prepChecksum = tables[prepIdx].checksum;
  } else {
    prepLen = 0;
    prepChecksum = 0;
  }

  for (i = 0; i < nTrickyFonts; ++i) {
    if (cvtLen == trickyFonts[i].cvtLen &&
	cvtChecksum == trickyFonts[i].cvtChecksum &&
	fpgmLen == trickyFonts[i].fpgmLen &&
	fpgmChecksum == trickyFonts[i].fpgmChecksum &&
	prepLen == trickyFonts[i].prepLen &&
	prepChecksum == trickyFonts[i].prepChecksum) {
      return gTrue;
    }
  }
  return gFalse;
}

void FoFiTrueType::convertToType42(char *psName, char **encoding,
				   int *codeToGID,
				   FoFiOutputFunc outputFunc,
				   void *outputStream) {
  GString *buf;
  int maxUsedGlyph;
  GBool ok;

  if (openTypeCFF) {
    return;
  }

  // write the header
  ok = gTrue;
  buf = GString::format("%!PS-TrueTypeFont-{0:2g}\n",
			(double)getS32BE(0, &ok) / 65536.0);
  (*outputFunc)(outputStream, buf->getCString(), buf->getLength());
  delete buf;

  // begin the font dictionary
  (*outputFunc)(outputStream, "10 dict begin\n", 14);
  (*outputFunc)(outputStream, "/FontName /", 11);
  (*outputFunc)(outputStream, psName, (int)strlen(psName));
  (*outputFunc)(outputStream, " def\n", 5);
  (*outputFunc)(outputStream, "/FontType 42 def\n", 17);
  (*outputFunc)(outputStream, "/FontMatrix [1 0 0 1 0 0] def\n", 30);
  buf = GString::format("/FontBBox [{0:d} {1:d} {2:d} {3:d}] def\n",
			bbox[0], bbox[1], bbox[2], bbox[3]);
  (*outputFunc)(outputStream, buf->getCString(), buf->getLength());
  delete buf;
  (*outputFunc)(outputStream, "/PaintType 0 def\n", 17);

  // write the guts of the dictionary
  cvtEncoding(encoding, outputFunc, outputStream);
  cvtCharStrings(encoding, codeToGID, outputFunc, outputStream);
  cvtSfnts(outputFunc, outputStream, NULL, gFalse, &maxUsedGlyph);

  // end the dictionary and define the font
  (*outputFunc)(outputStream, "FontName currentdict end definefont pop\n", 40);
}

void FoFiTrueType::convertToType1(char *psName, const char **newEncoding,
				  GBool ascii, FoFiOutputFunc outputFunc,
				  void *outputStream) {
  char *start;
  int length;
  FoFiType1C *ff;

  if (!getCFFBlock(&start, &length)) {
    return;
  }
  if (!(ff = FoFiType1C::make(start, length))) {
    return;
  }
  ff->convertToType1(psName, newEncoding, ascii, outputFunc, outputStream);
  delete ff;
}

void FoFiTrueType::convertToCIDType2(char *psName,
				     int *cidMap, int nCIDs,
				     GBool needVerticalMetrics,
				     FoFiOutputFunc outputFunc,
				     void *outputStream) {
  GString *buf;
  int cid, maxUsedGlyph;
  GBool ok;
  int i, j, k;

  if (openTypeCFF) {
    return;
  }

  // write the header
  ok = gTrue;
  buf = GString::format("%!PS-TrueTypeFont-{0:2g}\n",
			(double)getS32BE(0, &ok) / 65536.0);
  (*outputFunc)(outputStream, buf->getCString(), buf->getLength());
  delete buf;

  // begin the font dictionary
  (*outputFunc)(outputStream, "20 dict begin\n", 14);
  (*outputFunc)(outputStream, "/CIDFontName /", 14);
  (*outputFunc)(outputStream, psName, (int)strlen(psName));
  (*outputFunc)(outputStream, " def\n", 5);
  (*outputFunc)(outputStream, "/CIDFontType 2 def\n", 19);
  (*outputFunc)(outputStream, "/FontType 42 def\n", 17);
  (*outputFunc)(outputStream, "/CIDSystemInfo 3 dict dup begin\n", 32);
  (*outputFunc)(outputStream, "  /Registry (Adobe) def\n", 24);
  (*outputFunc)(outputStream, "  /Ordering (Identity) def\n", 27);
  (*outputFunc)(outputStream, "  /Supplement 0 def\n", 20);
  (*outputFunc)(outputStream, "  end def\n", 10);
  (*outputFunc)(outputStream, "/GDBytes 2 def\n", 15);
  if (cidMap) {
    buf = GString::format("/CIDCount {0:d} def\n", nCIDs);
    (*outputFunc)(outputStream, buf->getCString(), buf->getLength());
    delete buf;
    if (nCIDs > 32767) {
      (*outputFunc)(outputStream, "/CIDMap [", 9);
      for (i = 0; i < nCIDs; i += 32768 - 16) {
	(*outputFunc)(outputStream, "<\n", 2);
	for (j = 0; j < 32768 - 16 && i+j < nCIDs; j += 16) {
	  (*outputFunc)(outputStream, "  ", 2);
	  for (k = 0; k < 16 && i+j+k < nCIDs; ++k) {
	    cid = cidMap[i+j+k];
	    buf = GString::format("{0:02x}{1:02x}",
				  (cid >> 8) & 0xff, cid & 0xff);
	    (*outputFunc)(outputStream, buf->getCString(), buf->getLength());
	    delete buf;
	  }
	  (*outputFunc)(outputStream, "\n", 1);
	}
	(*outputFunc)(outputStream, "  >", 3);
      }
      (*outputFunc)(outputStream, "\n", 1);
      (*outputFunc)(outputStream, "] def\n", 6);
    } else {
      (*outputFunc)(outputStream, "/CIDMap <\n", 10);
      for (i = 0; i < nCIDs; i += 16) {
	(*outputFunc)(outputStream, "  ", 2);
	for (j = 0; j < 16 && i+j < nCIDs; ++j) {
	  cid = cidMap[i+j];
	  buf = GString::format("{0:02x}{1:02x}",
				(cid >> 8) & 0xff, cid & 0xff);
	  (*outputFunc)(outputStream, buf->getCString(), buf->getLength());
	  delete buf;
	}
	(*outputFunc)(outputStream, "\n", 1);
      }
      (*outputFunc)(outputStream, "> def\n", 6);
    }
  } else {
    // direct mapping - just fill the string(s) with s[i]=i
    buf = GString::format("/CIDCount {0:d} def\n", nGlyphs);
    (*outputFunc)(outputStream, buf->getCString(), buf->getLength());
    delete buf;
    if (nGlyphs > 32767) {
      (*outputFunc)(outputStream, "/CIDMap [\n", 10);
      for (i = 0; i < nGlyphs; i += 32767) {
	j = nGlyphs - i < 32767 ? nGlyphs - i : 32767;
	buf = GString::format("  {0:d} string 0 1 {1:d} {{\n", 2 * j, j - 1);
	(*outputFunc)(outputStream, buf->getCString(), buf->getLength());
	delete buf;
	buf = GString::format("    2 copy dup 2 mul exch {0:d} add -8 bitshift put\n",
			      i);
	(*outputFunc)(outputStream, buf->getCString(), buf->getLength());
	delete buf;
	buf = GString::format("    1 index exch dup 2 mul 1 add exch {0:d} add"
			      " 255 and put\n", i);
	(*outputFunc)(outputStream, buf->getCString(), buf->getLength());
	delete buf;
	(*outputFunc)(outputStream, "  } for\n", 8);
      }
      (*outputFunc)(outputStream, "] def\n", 6);
    } else {
      buf = GString::format("/CIDMap {0:d} string\n", 2 * nGlyphs);
      (*outputFunc)(outputStream, buf->getCString(), buf->getLength());
      delete buf;
      buf = GString::format("  0 1 {0:d} {{\n", nGlyphs - 1);
      (*outputFunc)(outputStream, buf->getCString(), buf->getLength());
      delete buf;
      (*outputFunc)(outputStream,
		    "    2 copy dup 2 mul exch -8 bitshift put\n", 42);
      (*outputFunc)(outputStream,
		    "    1 index exch dup 2 mul 1 add exch 255 and put\n", 50);
      (*outputFunc)(outputStream, "  } for\n", 8);
      (*outputFunc)(outputStream, "def\n", 4);
    }
  }
  (*outputFunc)(outputStream, "/FontMatrix [1 0 0 1 0 0] def\n", 30);
  buf = GString::format("/FontBBox [{0:d} {1:d} {2:d} {3:d}] def\n",
			bbox[0], bbox[1], bbox[2], bbox[3]);
  (*outputFunc)(outputStream, buf->getCString(), buf->getLength());
  delete buf;
  (*outputFunc)(outputStream, "/PaintType 0 def\n", 17);
  (*outputFunc)(outputStream, "/Encoding [] readonly def\n", 26);
  (*outputFunc)(outputStream, "/CharStrings 1 dict dup begin\n", 30);
  (*outputFunc)(outputStream, "  /.notdef 0 def\n", 17);
  (*outputFunc)(outputStream, "  end readonly def\n", 19);

  // write the guts of the dictionary
  cvtSfnts(outputFunc, outputStream, NULL, needVerticalMetrics, &maxUsedGlyph);

  // end the dictionary and define the font
  (*outputFunc)(outputStream,
		"CIDFontName currentdict end /CIDFont defineresource pop\n",
		56);
}

void FoFiTrueType::convertToCIDType0(char *psName, int *cidMap, int nCIDs,
				     FoFiOutputFunc outputFunc,
				     void *outputStream) {
  char *start;
  int length;
  FoFiType1C *ff;

  if (!getCFFBlock(&start, &length)) {
    return;
  }
  if (!(ff = FoFiType1C::make(start, length))) {
    return;
  }
  ff->convertToCIDType0(psName, cidMap, nCIDs, outputFunc, outputStream);
  delete ff;
}

void FoFiTrueType::convertToType0(char *psName, int *cidMap, int nCIDs,
				  GBool needVerticalMetrics,
				  FoFiOutputFunc outputFunc,
				  void *outputStream) {
  GString *buf;
  GString *sfntsName;
  int maxUsedGlyph, n, i, j;

  if (openTypeCFF) {
    return;
  }

  // write the Type 42 sfnts array
  sfntsName = (new GString(psName))->append("_sfnts");
  cvtSfnts(outputFunc, outputStream, sfntsName, needVerticalMetrics,
	   &maxUsedGlyph);
  delete sfntsName;

  // write the descendant Type 42 fonts
  // (The following is a kludge: nGlyphs is the glyph count from the
  // maxp table; maxUsedGlyph is the max glyph number that has a
  // non-zero-length description, from the loca table.  The problem is
  // that some TrueType font subsets fail to change the glyph count,
  // i.e., nGlyphs is much larger than maxUsedGlyph+1, which results
  // in an unnecessarily huge Type 0 font.  But some other PDF files
  // have fonts with only zero or one used glyph, and a content stream
  // that refers to one of the unused glyphs -- this results in PS
  // errors if we simply use maxUsedGlyph+1 for the Type 0 font.  So
  // we compromise by always defining at least 256 glyphs.)
  if (cidMap) {
    n = nCIDs;
  } else if (nGlyphs > maxUsedGlyph + 256) {
    if (maxUsedGlyph <= 255) {
      n = 256;
    } else {
      n = maxUsedGlyph + 1;
    }
  } else {
    n = nGlyphs;
  }
  for (i = 0; i < n; i += 256) {
    (*outputFunc)(outputStream, "10 dict begin\n", 14);
    (*outputFunc)(outputStream, "/FontName /", 11);
    (*outputFunc)(outputStream, psName, (int)strlen(psName));
    buf = GString::format("_{0:02x} def\n", i >> 8);
    (*outputFunc)(outputStream, buf->getCString(), buf->getLength());
    delete buf;
    (*outputFunc)(outputStream, "/FontType 42 def\n", 17);
    (*outputFunc)(outputStream, "/FontMatrix [1 0 0 1 0 0] def\n", 30);
    buf = GString::format("/FontBBox [{0:d} {1:d} {2:d} {3:d}] def\n",
			  bbox[0], bbox[1], bbox[2], bbox[3]);
    (*outputFunc)(outputStream, buf->getCString(), buf->getLength());
    delete buf;
    (*outputFunc)(outputStream, "/PaintType 0 def\n", 17);
    (*outputFunc)(outputStream, "/sfnts ", 7);
    (*outputFunc)(outputStream, psName, (int)strlen(psName));
    (*outputFunc)(outputStream, "_sfnts def\n", 11);
    (*outputFunc)(outputStream, "/Encoding 256 array\n", 20);
    for (j = 0; j < 256 && i+j < n; ++j) {
      buf = GString::format("dup {0:d} /c{1:02x} put\n", j, j);
      (*outputFunc)(outputStream, buf->getCString(), buf->getLength());
      delete buf;
    }
    (*outputFunc)(outputStream, "readonly def\n", 13);
    (*outputFunc)(outputStream, "/CharStrings 257 dict dup begin\n", 32);
    (*outputFunc)(outputStream, "/.notdef 0 def\n", 15);
    for (j = 0; j < 256 && i+j < n; ++j) {
      buf = GString::format("/c{0:02x} {1:d} def\n",
			    j, cidMap ? cidMap[i+j] : i+j);
      (*outputFunc)(outputStream, buf->getCString(), buf->getLength());
      delete buf;
    }
    (*outputFunc)(outputStream, "end readonly def\n", 17);
    (*outputFunc)(outputStream,
		  "FontName currentdict end definefont pop\n", 40);
  }

  // write the Type 0 parent font
  (*outputFunc)(outputStream, "16 dict begin\n", 14);
  (*outputFunc)(outputStream, "/FontName /", 11);
  (*outputFunc)(outputStream, psName, (int)strlen(psName));
  (*outputFunc)(outputStream, " def\n", 5);
  (*outputFunc)(outputStream, "/FontType 0 def\n", 16);
  (*outputFunc)(outputStream, "/FontMatrix [1 0 0 1 0 0] def\n", 30);
  (*outputFunc)(outputStream, "/FMapType 2 def\n", 16);
  (*outputFunc)(outputStream, "/Encoding [\n", 12);
  for (i = 0; i < n; i += 256) {
    buf = GString::format("{0:d}\n", i >> 8);
    (*outputFunc)(outputStream, buf->getCString(), buf->getLength());
    delete buf;
  }
  (*outputFunc)(outputStream, "] def\n", 6);
  (*outputFunc)(outputStream, "/FDepVector [\n", 14);
  for (i = 0; i < n; i += 256) {
    (*outputFunc)(outputStream, "/", 1);
    (*outputFunc)(outputStream, psName, (int)strlen(psName));
    buf = GString::format("_{0:02x} findfont\n", i >> 8);
    (*outputFunc)(outputStream, buf->getCString(), buf->getLength());
    delete buf;
  }
  (*outputFunc)(outputStream, "] def\n", 6);
  (*outputFunc)(outputStream, "FontName currentdict end definefont pop\n", 40);
}

void FoFiTrueType::convertToType0(char *psName, int *cidMap, int nCIDs,
				  FoFiOutputFunc outputFunc,
				  void *outputStream) {
  char *start;
  int length;
  FoFiType1C *ff;

  if (!getCFFBlock(&start, &length)) {
    return;
  }
  if (!(ff = FoFiType1C::make(start, length))) {
    return;
  }
  ff->convertToType0(psName, cidMap, nCIDs, outputFunc, outputStream);
  delete ff;
}

GBool FoFiTrueType::writeTTF(FoFiOutputFunc outputFunc,
			     void *outputStream, char *name,
			     int *codeToGID, Guchar *replacementCmapTable,
			     int replacementCmapTableLen) {
  // this substitute cmap table maps char code ffff to glyph 0,
  // with tables for MacRoman and MS Unicode
  static char cmapTab[44] = {
    0, 0,			// table version number
    0, 2,			// number of encoding tables
    0, 1,			// platform ID
    0, 0,			// encoding ID
    0, 0, 0, 20,		// offset of subtable
    0, 3,			// platform ID
    0, 1,			// encoding ID
    0, 0, 0, 20,		// offset of subtable
    0, 4,			// subtable format
    0, 24,			// subtable length
    0, 0,			// subtable version
    0, 2,			// segment count * 2
    0, 2,			// 2 * 2 ^ floor(log2(segCount))
    0, 0,			// floor(log2(segCount))
    0, 0,			// 2*segCount - 2*2^floor(log2(segCount))
    (char)0xff, (char)0xff,	// endCount[0]
    0, 0,			// reserved
    (char)0xff, (char)0xff,	// startCount[0]
    0, 1,			// idDelta[0]
    0, 0			// idRangeOffset[0]
  };
  static char nameTab[8] = {
    0, 0,			// format
    0, 0,			// number of name records
    0, 6,			// offset to start of string storage
    0, 0			// pad to multiple of four bytes
  };
  static char postTab[32] = {
    0, 1, 0, 0,			// format
    0, 0, 0, 0,			// italic angle
    0, 0,			// underline position
    0, 0,			// underline thickness
    0, 0, 0, 0,			// fixed pitch
    0, 0, 0, 0,			// min Type 42 memory
    0, 0, 0, 0,			// max Type 42 memory
    0, 0, 0, 0,			// min Type 1 memory
    0, 0, 0, 0			// max Type 1 memory
  };
  static char os2Tab[86] = {
    0, 1,			// version
    0, 1,			// xAvgCharWidth
    0x01, (char)0x90,		// usWeightClass
    0, 5,			// usWidthClass
    0, 0,			// fsType
    0, 0,			// ySubscriptXSize
    0, 0,			// ySubscriptYSize
    0, 0,			// ySubscriptXOffset
    0, 0,			// ySubscriptYOffset
    0, 0,			// ySuperscriptXSize
    0, 0,			// ySuperscriptYSize
    0, 0,			// ySuperscriptXOffset
    0, 0,			// ySuperscriptYOffset
    0, 0,			// yStrikeoutSize
    0, 0,			// yStrikeoutPosition
    0, 0,			// sFamilyClass
    0, 0, 0, 0, 0,		// panose
    0, 0, 0, 0, 0,
    0, 0, 0, 0,			// ulUnicodeRange1
    0, 0, 0, 0,			// ulUnicodeRange2
    0, 0, 0, 0,			// ulUnicodeRange3
    0, 0, 0, 0,			// ulUnicodeRange4
    0, 0, 0, 0,			// achVendID
    0, 0,			// fsSelection
    0, 0,			// usFirstCharIndex
    0, 0,			// usLastCharIndex
    0, 0,			// sTypoAscender
    0, 0,			// sTypoDescender
    0, 0,			// sTypoLineGap
    0x20, 0x00,			// usWinAscent
    0x20, 0x00,			// usWinDescent
    0, 0, 0, 1,			// ulCodePageRange1
    0, 0, 0, 0			// ulCodePageRange2
  };
  GBool missingCmap, missingName, missingPost, missingOS2;
  GBool unsortedLoca, emptyCmap, badCmapLen, abbrevHMTX;
  int nZeroLengthTables, nBogusTables;
  int nHMetrics, advWidth, lsb;
  TrueTypeLoca *locaTable;
  TrueTypeTable *newTables;
  char *newNameTab, *newCmapTab, *newHHEATab, *newHMTXTab;
  int nNewTables, cmapIdx, cmapLen, glyfLen, newNameLen, newCmapLen, next;
  int newHHEALen, newHMTXLen;
  Guint locaChecksum, glyfChecksum, fileChecksum;
  char *tableDir;
  char locaBuf[4], checksumBuf[4];
  GBool ok;
  Guint t;
  int pos, i, j, k, n;

  if (openTypeCFF) {
    return gFalse;
  }

  // check for missing tables
  // (Note: if the OS/2 table is missing, the Microsoft PCL5 driver
  // will embed a PCL TrueType font with the pitch field set to zero,
  // which apparently causes divide-by-zero errors.  As far as I can
  // tell, the only important field in the OS/2 table is
  // xAvgCharWidth.)
  missingCmap = (cmapIdx = seekTable("cmap")) < 0;
  missingName = seekTable("name") < 0;
  missingPost = seekTable("post") < 0;
  missingOS2 = seekTable("OS/2") < 0;

  // read the loca table, check to see if it's sorted
  locaTable = (TrueTypeLoca *)gmallocn(nGlyphs + 1, sizeof(TrueTypeLoca));
  unsortedLoca = gFalse;
  i = seekTable("loca");
  pos = tables[i].offset;
  glyfLen = tables[seekTable("glyf")].len;
  ok = gTrue;
  for (i = 0; i <= nGlyphs; ++i) {
    if (locaFmt) {
      locaTable[i].origOffset = (int)getU32BE(pos + i*4, &ok);
    } else {
      locaTable[i].origOffset = 2 * getU16BE(pos + i*2, &ok);
    }
    if (locaTable[i].origOffset < 0 ||
	locaTable[i].origOffset > glyfLen) {
      locaTable[i].origOffset = glyfLen;
      unsortedLoca = gTrue;
    }
    if (i > 0 && locaTable[i].origOffset < locaTable[i-1].origOffset) {
      unsortedLoca = gTrue;
    }
    // glyph descriptions must be at least 12 bytes long (nContours,
    // xMin, yMin, xMax, yMax, instructionLength - two bytes each);
    // invalid glyph descriptions (even if they're never used) make
    // Windows choke, so we work around that problem here (ideally,
    // this would parse the glyph descriptions in the glyf table and
    // remove any that were invalid, but this quick test is a decent
    // start)
    if (i > 0 &&
	locaTable[i].origOffset - locaTable[i-1].origOffset > 0 &&
	locaTable[i].origOffset - locaTable[i-1].origOffset < 12) {
      locaTable[i-1].origOffset = locaTable[i].origOffset;
      unsortedLoca = gTrue;
    }
    locaTable[i].idx = i;
  }

  // check for zero-length tables and bogus tags
  nZeroLengthTables = nBogusTables = 0;
  for (i = 0; i < nTables; ++i) {
    if (tables[i].len == 0) {
      ++nZeroLengthTables;
      if (tables[i].tag == cmapTag) {
	missingCmap = gTrue;
      } else if (tables[i].tag == nameTag) {
	missingName = gTrue;
      } else if (tables[i].tag == postTag) {
	missingPost = gTrue;
      } else if (tables[i].tag == os2Tag) {
	missingOS2 = gTrue;
      }
    } else if (!(tables[i].tag & 0xe0000000) ||
	       !(tables[i].tag & 0x00e00000) ||
	       !(tables[i].tag & 0x0000e000) ||
	       !(tables[i].tag & 0x000000e0)) {
      // tags where any of the bytes are < 0x20 are probably bogus
      // (the TrueType spec uses ASCII sequences for tags) -- this
      // catches problems where the number of tables given in the
      // header is too large, and so gibberish data is read at the end
      // of the table directory
      ++nBogusTables;
    }
  }

  // check for an empty cmap table or an incorrect cmap table length
  emptyCmap = badCmapLen = gFalse;
  cmapLen = 0; // make gcc happy
  if (!missingCmap) {
    if (nCmaps == 0) {
      emptyCmap = gTrue;
    } else {
      cmapLen = cmaps[0].offset + cmaps[0].len;
      for (i = 1; i < nCmaps; ++i) {
	if (cmaps[i].offset + cmaps[i].len > cmapLen) {
	  cmapLen = cmaps[i].offset + cmaps[i].len;
	}
      }
      cmapLen -= tables[cmapIdx].offset;
      if (cmapLen > tables[cmapIdx].len) {
	badCmapLen = gTrue;
      }
    }
  }

  // check for an abbreviated hmtx table (this is completely legal,
  // but confuses the Microsoft PCL5 printer driver, which generates
  // embedded fonts with the pitch field set to zero)
  i = seekTable("hhea");
  nHMetrics = getU16BE(tables[i].offset + 34, &ok);
  abbrevHMTX = nHMetrics < nGlyphs;

  // if nothing is broken, just write the TTF file as is
  if (!missingCmap && !missingName && !missingPost && !missingOS2 &&
      !unsortedLoca && !emptyCmap && !badCmapLen && !abbrevHMTX &&
      nZeroLengthTables == 0 && nBogusTables == 0 &&
      !name && !codeToGID && !replacementCmapTable && !isDfont && !isTTC) {
    (*outputFunc)(outputStream, (char *)file, len);
    gfree(locaTable);
    return gFalse;
  }

  // sort the 'loca' table: some (non-compliant) fonts have
  // out-of-order loca tables; in order to correctly handle the case
  // where (compliant) fonts have empty entries in the middle of the
  // table, cmpTrueTypeLocaOffset uses offset as its primary sort key,
  // and idx as its secondary key (ensuring that adjacent entries with
  // the same pos value remain in the same order)
  glyfLen = 0; // make gcc happy
  if (unsortedLoca) {
#if HAVE_STD_SORT
    std::sort(locaTable, locaTable + nGlyphs + 1,
	      cmpTrueTypeLocaOffsetFunctor());
#else
    qsort(locaTable, nGlyphs + 1, sizeof(TrueTypeLoca),
	  &cmpTrueTypeLocaOffset);
#endif
    for (i = 0; i < nGlyphs; ++i) {
      locaTable[i].len = locaTable[i+1].origOffset - locaTable[i].origOffset;
    }
    locaTable[nGlyphs].len = 0;
#if HAVE_STD_SORT
    std::sort(locaTable, locaTable + nGlyphs + 1, cmpTrueTypeLocaIdxFunctor());
#else
    qsort(locaTable, nGlyphs + 1, sizeof(TrueTypeLoca),
	  &cmpTrueTypeLocaIdx);
#endif
    // if the last entry in the loca is not the max offset (size of
    // the glyf table), something is wrong -- work around the problem
    // by forcing the last sorted entry to have a zero length
    locaTable[nGlyphs].len = 0;
    pos = 0;
    for (i = 0; i <= nGlyphs; ++i) {
      locaTable[i].newOffset = pos;
      pos += locaTable[i].len;
      if (pos & 3) {
	pos += 4 - (pos & 3);
      }
    }
    glyfLen = pos;
  }

  // compute checksums for the loca and glyf tables
  locaChecksum = glyfChecksum = 0;
  if (unsortedLoca) {
    if (locaFmt) {
      for (j = 0; j <= nGlyphs; ++j) {
	locaChecksum += locaTable[j].newOffset;
      }
    } else {
      for (j = 0; j <= nGlyphs; j += 2) {
	locaChecksum += locaTable[j].newOffset << 16;
	if (j + 1 <= nGlyphs) {
	  locaChecksum += locaTable[j+1].newOffset;
	}
      }
    }
    pos = tables[seekTable("glyf")].offset;
    for (j = 0; j < nGlyphs; ++j) {
      n = locaTable[j].len;
      if (n > 0) {
	k = locaTable[j].origOffset;
	if (checkRegion(pos + k, n)) {
	  glyfChecksum += computeTableChecksum(file + pos + k, n);
	}
      }
    }
  }

  // construct the new name table
  if (name) {
    n = (int)strlen(name);
    newNameLen = (6 + 4*12 + 2 * (3*n + 7) + 3) & ~3;
    newNameTab = (char *)gmalloc(newNameLen);
    memset(newNameTab, 0, newNameLen);
    newNameTab[0] = 0;		// format selector
    newNameTab[1] = 0;
    newNameTab[2] = 0;		// number of name records
    newNameTab[3] = 4;
    newNameTab[4] = 0;		// offset to start of string storage
    newNameTab[5] = 6 + 4*12;
    next = 0;
    for (i = 0; i < 4; ++i) {
      newNameTab[6 + i*12 + 0] = 0;	// platform ID = Microsoft
      newNameTab[6 + i*12 + 1] = 3;
      newNameTab[6 + i*12 + 2] = 0;	// encoding ID = Unicode
      newNameTab[6 + i*12 + 3] = 1;
      newNameTab[6 + i*12 + 4] = 0x04;	// language ID = American English
      newNameTab[6 + i*12 + 5] = 0x09;
      newNameTab[6 + i*12 + 6] = 0;	// name ID
      newNameTab[6 + i*12 + 7] = (char)(i + 1);
      // string length
      newNameTab[6 + i*12 + 8] = (char)(i+1 == 2 ? 0 : ((2*n) >> 8));
      newNameTab[6 + i*12 + 9] = (char)(i+1 == 2 ? 14 : ((2*n) & 0xff));
      // string offset
      newNameTab[6 + i*12 + 10] = (char)(next >> 8);
      newNameTab[6 + i*12 + 11] = (char)(next & 0xff);
      if (i+1 == 2) {
	memcpy(newNameTab + 6 + 4*12 + next, "\0R\0e\0g\0u\0l\0a\0r", 14);
	next += 14;
      } else {
	for (j = 0; j < n; ++j) {
	  newNameTab[6 + 4*12 + next + 2*j] = 0;
	  newNameTab[6 + 4*12 + next + 2*j + 1] = name[j];
	}
	next += 2*n;
      }
    }
  } else {
    newNameLen = 0;
    newNameTab = NULL;
  }

  // construct the new cmap table
  if (codeToGID && !replacementCmapTable) {
    newCmapLen = 44 + 256 * 2;
    newCmapTab = (char *)gmalloc(newCmapLen);
    newCmapTab[0] = 0;		// table version number = 0
    newCmapTab[1] = 0;
    newCmapTab[2] = 0;		// number of encoding tables = 1
    newCmapTab[3] = 1;
    newCmapTab[4] = 0;		// platform ID = Microsoft
    newCmapTab[5] = 3;
    newCmapTab[6] = 0;		// encoding ID = Unicode
    newCmapTab[7] = 1;
    newCmapTab[8] = 0;		// offset of subtable
    newCmapTab[9] = 0;
    newCmapTab[10] = 0;
    newCmapTab[11] = 12;
    newCmapTab[12] = 0;		// subtable format = 4
    newCmapTab[13] = 4;
    newCmapTab[14] = 0x02;	// subtable length
    newCmapTab[15] = 0x20;
    newCmapTab[16] = 0;		// subtable version = 0
    newCmapTab[17] = 0;
    newCmapTab[18] = 0;		// segment count * 2
    newCmapTab[19] = 4;
    newCmapTab[20] = 0;		// 2 * 2 ^ floor(log2(segCount))
    newCmapTab[21] = 4;
    newCmapTab[22] = 0;		// floor(log2(segCount))
    newCmapTab[23] = 1;
    newCmapTab[24] = 0;		// 2*segCount - 2*2^floor(log2(segCount))
    newCmapTab[25] = 0;
    newCmapTab[26] = 0x00;	// endCount[0]
    newCmapTab[27] = (char)0xff;
    newCmapTab[28] = (char)0xff; // endCount[1]
    newCmapTab[29] = (char)0xff;
    newCmapTab[30] = 0;		// reserved
    newCmapTab[31] = 0;
    newCmapTab[32] = 0x00;	// startCount[0]
    newCmapTab[33] = 0x00;
    newCmapTab[34] = (char)0xff; // startCount[1]
    newCmapTab[35] = (char)0xff;
    newCmapTab[36] = 0;		// idDelta[0]
    newCmapTab[37] = 0;
    newCmapTab[38] = 0;		// idDelta[1]
    newCmapTab[39] = 1;
    newCmapTab[40] = 0;		// idRangeOffset[0]
    newCmapTab[41] = 4;
    newCmapTab[42] = 0;		// idRangeOffset[1]
    newCmapTab[43] = 0;
    for (i = 0; i < 256; ++i) {
      if (codeToGID[i] < 0) {
	//~ this may not be correct - we want this character to never be
	//~ displayed, but mapping it to the notdef glyph may result in
	//~ little boxes being displayed
	newCmapTab[44 + 2*i] = 0;
	newCmapTab[44 + 2*i + 1] = 0;
      } else {
	newCmapTab[44 + 2*i] = (char)(codeToGID[i] >> 8);
	newCmapTab[44 + 2*i + 1] = (char)(codeToGID[i] & 0xff);
      }
    }
  } else {
    newCmapLen = 0;
    newCmapTab = NULL;
  }

  // generate the new hmtx table and the updated hhea table
  if (abbrevHMTX) {
    i = seekTable("hhea");
    pos = tables[i].offset;
    newHHEALen = 36;
    newHHEATab = (char *)gmalloc(newHHEALen);
    for (i = 0; i < newHHEALen; ++i) {
      newHHEATab[i] = (char)getU8(pos++, &ok);
    }
    newHHEATab[34] = (char)(nGlyphs >> 8);
    newHHEATab[35] = (char)(nGlyphs & 0xff);
    i = seekTable("hmtx");
    pos = tables[i].offset;
    newHMTXLen = 4 * nGlyphs;
    newHMTXTab = (char *)gmalloc(newHMTXLen);
    advWidth = 0;
    for (i = 0; i < nHMetrics; ++i) {
      advWidth = getU16BE(pos, &ok);
      lsb = getU16BE(pos + 2, &ok);
      pos += 4;
      newHMTXTab[4*i    ] = (char)(advWidth >> 8);
      newHMTXTab[4*i + 1] = (char)(advWidth & 0xff);
      newHMTXTab[4*i + 2] = (char)(lsb >> 8);
      newHMTXTab[4*i + 3] = (char)(lsb & 0xff);
    }
    for (; i < nGlyphs; ++i) {
      lsb = getU16BE(pos, &ok);
      pos += 2;
      newHMTXTab[4*i    ] = (char)(advWidth >> 8);
      newHMTXTab[4*i + 1] = (char)(advWidth & 0xff);
      newHMTXTab[4*i + 2] = (char)(lsb >> 8);
      newHMTXTab[4*i + 3] = (char)(lsb & 0xff);
    }
  } else {
    newHHEATab = newHMTXTab = NULL;
    newHHEALen = newHMTXLen = 0; // make gcc happy
  }

  // construct the new table directory:
  // - keep all original tables with non-zero length
  // - fix the cmap table's length, if necessary
  // - add missing tables
  // - sort the table by tag
  // - compute new table positions, including 4-byte alignment
  // - (re)compute table checksums
  nNewTables = nTables - nZeroLengthTables - nBogusTables +
               (missingCmap ? 1 : 0) + (missingName ? 1 : 0) +
               (missingPost ? 1 : 0) + (missingOS2 ? 1 : 0);
  newTables = (TrueTypeTable *)gmallocn(nNewTables, sizeof(TrueTypeTable));
  j = 0;
  for (i = 0; i < nTables; ++i) {
    if (tables[i].len > 0 &&
	(tables[i].tag & 0xe0000000) &&
	(tables[i].tag & 0x00e00000) &&
	(tables[i].tag & 0x0000e000) &&
	(tables[i].tag & 0x000000e0)) {
      newTables[j] = tables[i];
      newTables[j].origOffset = tables[i].offset;
      if (checkRegion(tables[i].offset, tables[i].len)) {
	newTables[j].checksum =
	    computeTableChecksum(file + tables[i].offset, tables[i].len);
	if (tables[i].tag == headTag) {
	  // don't include the file checksum
	  newTables[j].checksum -= getU32BE(tables[i].offset + 8, &ok);
	}
      } else {
	// we'll write four zero bytes for this table
	newTables[j].len = 4;
      }
      if (newTables[j].tag == cmapTag && replacementCmapTable) {
	newTables[j].len = replacementCmapTableLen;
	newTables[j].checksum = computeTableChecksum(replacementCmapTable,
						     replacementCmapTableLen);
      } else if (newTables[j].tag == cmapTag && codeToGID) {
	newTables[j].len = newCmapLen;
	newTables[j].checksum = computeTableChecksum((Guchar *)newCmapTab,
						     newCmapLen);
      } else if (newTables[j].tag == cmapTag && emptyCmap) {
	newTables[j].checksum = computeTableChecksum((Guchar *)cmapTab,
						     sizeof(cmapTab));
	newTables[j].len = sizeof(cmapTab);
      } else if (newTables[j].tag == cmapTag && badCmapLen) {
	newTables[j].len = cmapLen;
      } else if (newTables[j].tag == locaTag && unsortedLoca) {
	newTables[j].len = (nGlyphs + 1) * (locaFmt ? 4 : 2);
	newTables[j].checksum = locaChecksum;
      } else if (newTables[j].tag == glyfTag && unsortedLoca) {
	newTables[j].len = glyfLen;
	newTables[j].checksum = glyfChecksum;
      } else if (newTables[j].tag == nameTag && name) {
	newTables[j].len = newNameLen;
	newTables[j].checksum = computeTableChecksum((Guchar *)newNameTab,
						     newNameLen);
      } else if (newTables[j].tag == hheaTag && abbrevHMTX) {
	newTables[j].len = newHHEALen;
	newTables[j].checksum = computeTableChecksum((Guchar *)newHHEATab,
						     newHHEALen);
      } else if (newTables[j].tag == hmtxTag && abbrevHMTX) {
	newTables[j].len = newHMTXLen;
	newTables[j].checksum = computeTableChecksum((Guchar *)newHMTXTab,
						     newHMTXLen);
      }
      ++j;
    }
  }
  if (missingCmap) {
    newTables[j].tag = cmapTag;
    if (replacementCmapTable) {
      newTables[j].checksum = computeTableChecksum(replacementCmapTable,
						   replacementCmapTableLen);
      newTables[j].len = replacementCmapTableLen;
    } else if (codeToGID) {
      newTables[j].checksum = computeTableChecksum((Guchar *)newCmapTab,
						   newCmapLen);
      newTables[j].len = newCmapLen;
    } else {
      newTables[j].checksum = computeTableChecksum((Guchar *)cmapTab,
						   sizeof(cmapTab));
      newTables[j].len = sizeof(cmapTab);
    }
    ++j;
  }
  if (missingName) {
    newTables[j].tag = nameTag;
    if (name) {
      newTables[j].checksum = computeTableChecksum((Guchar *)newNameTab,
						   newNameLen);
      newTables[j].len = newNameLen;
    } else {
      newTables[j].checksum = computeTableChecksum((Guchar *)nameTab,
						   sizeof(nameTab));
      newTables[j].len = sizeof(nameTab);
    }
    ++j;
  }
  if (missingPost) {
    newTables[j].tag = postTag;
    newTables[j].checksum = computeTableChecksum((Guchar *)postTab,
						 sizeof(postTab));
    newTables[j].len = sizeof(postTab);
    ++j;
  }
  if (missingOS2) {
    newTables[j].tag = os2Tag;
    newTables[j].checksum = computeTableChecksum((Guchar *)os2Tab,
						 sizeof(os2Tab));
    newTables[j].len = sizeof(os2Tab);
    ++j;
  }
#if HAVE_STD_SORT
  std::sort(newTables, newTables + nNewTables, cmpTrueTypeTableTagFunctor());
#else
  qsort(newTables, nNewTables, sizeof(TrueTypeTable),
	&cmpTrueTypeTableTag);
#endif
  pos = 12 + nNewTables * 16;
  for (i = 0; i < nNewTables; ++i) {
    newTables[i].offset = pos;
    pos += newTables[i].len;
    if (pos & 3) {
      pos += 4 - (pos & 3);
    }
  }

  // write the table directory
  tableDir = (char *)gmalloc(12 + nNewTables * 16);
  tableDir[0] = 0x00;					// sfnt version
  tableDir[1] = 0x01;
  tableDir[2] = 0x00;
  tableDir[3] = 0x00;
  tableDir[4] = (char)((nNewTables >> 8) & 0xff);	// numTables
  tableDir[5] = (char)(nNewTables & 0xff);
  for (i = -1, t = (Guint)nNewTables; t; ++i, t >>= 1) ;
  t = 1 << (4 + i);
  tableDir[6] = (char)((t >> 8) & 0xff);		// searchRange
  tableDir[7] = (char)(t & 0xff);
  tableDir[8] = (char)((i >> 8) & 0xff);		// entrySelector
  tableDir[9] = (char)(i & 0xff);
  t = nNewTables * 16 - t;
  tableDir[10] = (char)((t >> 8) & 0xff);		// rangeShift
  tableDir[11] = (char)(t & 0xff);
  pos = 12;
  for (i = 0; i < nNewTables; ++i) {
    tableDir[pos   ] = (char)(newTables[i].tag >> 24);
    tableDir[pos+ 1] = (char)(newTables[i].tag >> 16);
    tableDir[pos+ 2] = (char)(newTables[i].tag >>  8);
    tableDir[pos+ 3] = (char) newTables[i].tag;
    tableDir[pos+ 4] = (char)(newTables[i].checksum >> 24);
    tableDir[pos+ 5] = (char)(newTables[i].checksum >> 16);
    tableDir[pos+ 6] = (char)(newTables[i].checksum >>  8);
    tableDir[pos+ 7] = (char) newTables[i].checksum;
    tableDir[pos+ 8] = (char)(newTables[i].offset >> 24);
    tableDir[pos+ 9] = (char)(newTables[i].offset >> 16);
    tableDir[pos+10] = (char)(newTables[i].offset >>  8);
    tableDir[pos+11] = (char) newTables[i].offset;
    tableDir[pos+12] = (char)(newTables[i].len >> 24);
    tableDir[pos+13] = (char)(newTables[i].len >> 16);
    tableDir[pos+14] = (char)(newTables[i].len >>  8);
    tableDir[pos+15] = (char) newTables[i].len;
    pos += 16;
  }
  (*outputFunc)(outputStream, tableDir, 12 + nNewTables * 16);

  // compute the file checksum
  fileChecksum = computeTableChecksum((Guchar *)tableDir,
				      12 + nNewTables * 16);
  for (i = 0; i < nNewTables; ++i) {
    fileChecksum += newTables[i].checksum;
  }
  fileChecksum = 0xb1b0afba - fileChecksum;

  // write the tables
  for (i = 0; i < nNewTables; ++i) {
    if (newTables[i].tag == headTag) {
      if (checkRegion(newTables[i].origOffset, newTables[i].len)) {
	(*outputFunc)(outputStream, (char *)file + newTables[i].origOffset, 8);
	checksumBuf[0] = (char)(fileChecksum >> 24);
	checksumBuf[1] = (char)(fileChecksum >> 16);
	checksumBuf[2] = (char)(fileChecksum >> 8);
	checksumBuf[3] = (char)fileChecksum;
	(*outputFunc)(outputStream, checksumBuf, 4);
	(*outputFunc)(outputStream,
		      (char *)file + newTables[i].origOffset + 12,
		      newTables[i].len - 12);
      } else {
	for (j = 0; j < newTables[i].len; ++j) {
	  (*outputFunc)(outputStream, "\0", 1);
	}
      }
    } else if (newTables[i].tag == cmapTag && replacementCmapTable) {
      (*outputFunc)(outputStream, (char *)replacementCmapTable,
		    newTables[i].len);
    } else if (newTables[i].tag == cmapTag && codeToGID) {
      (*outputFunc)(outputStream, newCmapTab, newTables[i].len);
    } else if (newTables[i].tag == cmapTag && missingCmap) {
      (*outputFunc)(outputStream, cmapTab, newTables[i].len);
    } else if (newTables[i].tag == nameTag && name) {
      (*outputFunc)(outputStream, newNameTab, newTables[i].len);
    } else if (newTables[i].tag == nameTag && missingName) {
      (*outputFunc)(outputStream, nameTab, newTables[i].len);
    } else if (newTables[i].tag == postTag && missingPost) {
      (*outputFunc)(outputStream, postTab, newTables[i].len);
    } else if (newTables[i].tag == os2Tag && missingOS2) {
      (*outputFunc)(outputStream, os2Tab, newTables[i].len);
    } else if (newTables[i].tag == hheaTag && abbrevHMTX) {
      (*outputFunc)(outputStream, newHHEATab, newTables[i].len);
    } else if (newTables[i].tag == hmtxTag && abbrevHMTX) {
      (*outputFunc)(outputStream, newHMTXTab, newTables[i].len);
    } else if (newTables[i].tag == locaTag && unsortedLoca) {
      for (j = 0; j <= nGlyphs; ++j) {
	if (locaFmt) {
	  locaBuf[0] = (char)(locaTable[j].newOffset >> 24);
	  locaBuf[1] = (char)(locaTable[j].newOffset >> 16);
	  locaBuf[2] = (char)(locaTable[j].newOffset >>  8);
	  locaBuf[3] = (char) locaTable[j].newOffset;
	  (*outputFunc)(outputStream, locaBuf, 4);
	} else {
	  locaBuf[0] = (char)(locaTable[j].newOffset >> 9);
	  locaBuf[1] = (char)(locaTable[j].newOffset >> 1);
	  (*outputFunc)(outputStream, locaBuf, 2);
	}
      }
    } else if (newTables[i].tag == glyfTag && unsortedLoca) {
      pos = tables[seekTable("glyf")].offset;
      for (j = 0; j < nGlyphs; ++j) {
	n = locaTable[j].len;
	if (n > 0) {
	  k = locaTable[j].origOffset;
	  if (checkRegion(pos + k, n)) {
	    (*outputFunc)(outputStream, (char *)file + pos + k, n);
	  } else {
	    for (k = 0; k < n; ++k) {
	      (*outputFunc)(outputStream, "\0", 1);
	    }
	  }
	  if ((k = locaTable[j].len & 3)) {
	    (*outputFunc)(outputStream, "\0\0\0\0", 4 - k);
	  }
	}
      }
    } else {
      if (checkRegion(newTables[i].origOffset, newTables[i].len)) {
	(*outputFunc)(outputStream, (char *)file + newTables[i].origOffset,
		      newTables[i].len);
      } else {
	for (j = 0; j < newTables[i].len; ++j) {
	  (*outputFunc)(outputStream, "\0", 1);
	}
      }
    }
    if (newTables[i].len & 3) {
      (*outputFunc)(outputStream, "\0\0\0", 4 - (newTables[i].len & 3));
    }
  }

  gfree(newHMTXTab);
  gfree(newHHEATab);
  gfree(newCmapTab);
  gfree(newNameTab);
  gfree(tableDir);
  gfree(newTables);
  gfree(locaTable);
  return gTrue;
}

void FoFiTrueType::cvtEncoding(char **encoding,
			       FoFiOutputFunc outputFunc,
			       void *outputStream) {
  const char *name;
  GString *buf;
  int i;

  (*outputFunc)(outputStream, "/Encoding 256 array\n", 20);
  if (encoding) {
    for (i = 0; i < 256; ++i) {
      if (!(name = encoding[i])) {
	name = ".notdef";
      }
      buf = GString::format("dup {0:d} /", i);
      (*outputFunc)(outputStream, buf->getCString(), buf->getLength());
      delete buf;
      (*outputFunc)(outputStream, name, (int)strlen(name));
      (*outputFunc)(outputStream, " put\n", 5);
    }
  } else {
    for (i = 0; i < 256; ++i) {
      buf = GString::format("dup {0:d} /c{1:02x} put\n", i, i);
      (*outputFunc)(outputStream, buf->getCString(), buf->getLength());
      delete buf;
    }
  }
  (*outputFunc)(outputStream, "readonly def\n", 13);
}

void FoFiTrueType::cvtCharStrings(char **encoding,
				  int *codeToGID,
				  FoFiOutputFunc outputFunc,
				  void *outputStream) {
  char *name;
  GString *buf;
  char buf2[16];
  int i, k;

  // always define '.notdef'
  (*outputFunc)(outputStream, "/CharStrings 256 dict dup begin\n", 32);
  (*outputFunc)(outputStream, "/.notdef 0 def\n", 15);

  // map char name to glyph index:
  // 1. use encoding to map name to char code
  // 2. use codeToGID to map char code to glyph index
  // N.B. We do this in reverse order because font subsets can have
  //      weird encodings that use the same character name twice, and
  //      the first definition is probably the one we want.
  k = 0; // make gcc happy
  for (i = 255; i >= 0; --i) {
    if (encoding) {
      name = encoding[i];
    } else {
      snprintf(buf2, sizeof(buf2), "c%02x", i);
      name = buf2;
    }
    if (name && strcmp(name, ".notdef")) {
      k = codeToGID[i];
      // note: Distiller (maybe Adobe's PS interpreter in general)
      // doesn't like TrueType fonts that have CharStrings entries
      // which point to nonexistent glyphs, hence the (k < nGlyphs)
      // test
      if (k > 0 && k < nGlyphs) {
	(*outputFunc)(outputStream, "/", 1);
	(*outputFunc)(outputStream, name, (int)strlen(name));
	buf = GString::format(" {0:d} def\n", k);
	(*outputFunc)(outputStream, buf->getCString(), buf->getLength());
	delete buf;
      }
    }
  }

  (*outputFunc)(outputStream, "end readonly def\n", 17);
}

void FoFiTrueType::cvtSfnts(FoFiOutputFunc outputFunc,
			    void *outputStream, GString *name,
			    GBool needVerticalMetrics,
			    int *maxUsedGlyph) {
  Guchar headData[54];
  TrueTypeLoca *locaTable;
  Guchar *locaData;
  TrueTypeTable newTables[nT42Tables];
  Guchar tableDir[12 + nT42Tables*16];
  GBool ok;
  Guint checksum;
  int nNewTables;
  int glyfTableLen, length, pos, glyfPos, i, j, k;
  Guchar vheaTab[36] = {
    0, 1, 0, 0,			// table version number
    0, 0,			// ascent
    0, 0,			// descent
    0, 0,			// reserved
    0, 0,			// max advance height
    0, 0,			// min top side bearing
    0, 0,			// min bottom side bearing
    0, 0,			// y max extent
    0, 0,			// caret slope rise
    0, 1,			// caret slope run
    0, 0,			// caret offset
    0, 0,			// reserved
    0, 0,			// reserved
    0, 0,			// reserved
    0, 0,			// reserved
    0, 0,			// metric data format
    0, 1			// number of advance heights in vmtx table
  };
  Guchar *vmtxTab;
  GBool needVhea, needVmtx;
  int advance;

  // construct the 'head' table, zero out the font checksum
  i = seekTable("head");
  pos = tables[i].offset;
  if (!checkRegion(pos, 54)) {
    return;
  }
  memcpy(headData, file + pos, 54);
  headData[8] = headData[9] = headData[10] = headData[11] = (Guchar)0;

  // check for a bogus loca format field in the 'head' table
  // (I've encountered fonts with loca format set to 0x0100 instead of 0x0001)
  if (locaFmt != 0 && locaFmt != 1) {
    headData[50] = 0;
    headData[51] = 1;
  }

  // read the original 'loca' table, pad entries out to 4 bytes, and
  // sort it into proper order -- some (non-compliant) fonts have
  // out-of-order loca tables; in order to correctly handle the case
  // where (compliant) fonts have empty entries in the middle of the
  // table, cmpTrueTypeLocaOffset uses offset as its primary sort key,
  // and idx as its secondary key (ensuring that adjacent entries with
  // the same pos value remain in the same order)
  //
  // NB: a glyph description containing 12 zero bytes should be a
  // valid empty glyph (from my reading of the TrueType spec), but
  // Acrobat chokes on this (which is an issue when an Xpdf-generated
  // PS file is converted back to PDF - with Ghostscript or
  // Distiller), so we drop any glyph descriptions of 12 or fewer
  // bytes -- an empty glyph description generates an empty glyph with
  // no errors
  locaTable = (TrueTypeLoca *)gmallocn(nGlyphs + 1, sizeof(TrueTypeLoca));
  i = seekTable("loca");
  pos = tables[i].offset;
  i = seekTable("glyf");
  glyfTableLen = tables[i].len;
  ok = gTrue;
  for (i = 0; i <= nGlyphs; ++i) {
    locaTable[i].idx = i;
    if (locaFmt) {
      locaTable[i].origOffset = (int)getU32BE(pos + i*4, &ok);
    } else {
      locaTable[i].origOffset = 2 * getU16BE(pos + i*2, &ok);
    }
    if (locaTable[i].origOffset < 0 ||
	locaTable[i].origOffset > glyfTableLen) {
      locaTable[i].origOffset = glyfTableLen;
    }
  }
#if HAVE_STD_SORT
  std::sort(locaTable, locaTable + nGlyphs + 1,
	    cmpTrueTypeLocaOffsetFunctor());
#else
  qsort(locaTable, nGlyphs + 1, sizeof(TrueTypeLoca),
	&cmpTrueTypeLocaOffset);
#endif
  for (i = 0; i < nGlyphs; ++i) {
    locaTable[i].len = locaTable[i+1].origOffset - locaTable[i].origOffset;
  }
  locaTable[nGlyphs].len = 0;
#if HAVE_STD_SORT
  std::sort(locaTable, locaTable + nGlyphs + 1, cmpTrueTypeLocaIdxFunctor());
#else
  qsort(locaTable, nGlyphs + 1, sizeof(TrueTypeLoca),
	&cmpTrueTypeLocaIdx);
#endif
  pos = 0;
  *maxUsedGlyph = -1;
  for (i = 0; i <= nGlyphs; ++i) {
    locaTable[i].newOffset = pos;
    if (locaTable[i].len > 12) {
      pos += locaTable[i].len;
      if (pos & 3) {
	pos += 4 - (pos & 3);
      }
      *maxUsedGlyph = i;
    }
  }

  // construct the new 'loca' table
  locaData = (Guchar *)gmallocn(nGlyphs + 1, (locaFmt ? 4 : 2));
  for (i = 0; i <= nGlyphs; ++i) {
    pos = locaTable[i].newOffset;
    if (locaFmt) {
      locaData[4*i  ] = (Guchar)(pos >> 24);
      locaData[4*i+1] = (Guchar)(pos >> 16);
      locaData[4*i+2] = (Guchar)(pos >>  8);
      locaData[4*i+3] = (Guchar) pos;
    } else {
      locaData[2*i  ] = (Guchar)(pos >> 9);
      locaData[2*i+1] = (Guchar)(pos >> 1);
    }
  }

  // count the number of tables
  nNewTables = 0;
  for (i = 0; i < nT42Tables; ++i) {
    if (t42Tables[i].required ||
	seekTable(t42Tables[i].tag) >= 0) {
      ++nNewTables;
    }
  }
  vmtxTab = NULL; // make gcc happy
  advance = 0; // make gcc happy
  if (needVerticalMetrics) {
    needVhea = seekTable("vhea") < 0;
    needVmtx = seekTable("vmtx") < 0;
    if (needVhea || needVmtx) {
      i = seekTable("head");
      advance = getU16BE(tables[i].offset + 18, &ok); // units per em
      if (needVhea) {
	++nNewTables;
      }
      if (needVmtx) {
	++nNewTables;
      }
    }
  }

  // construct the new table headers, including table checksums
  // (pad each table out to a multiple of 4 bytes)
  pos = 12 + nNewTables*16;
  k = 0;
  for (i = 0; i < nT42Tables; ++i) {
    length = -1;
    checksum = 0; // make gcc happy
    if (i == t42HeadTable) {
      length = 54;
      checksum = computeTableChecksum(headData, 54);
    } else if (i == t42LocaTable) {
      length = (nGlyphs + 1) * (locaFmt ? 4 : 2);
      checksum = computeTableChecksum(locaData, length);
    } else if (i == t42GlyfTable) {
      length = 0;
      checksum = 0;
      glyfPos = tables[seekTable("glyf")].offset;
      for (j = 0; j < nGlyphs; ++j) {
	if (locaTable[j].len > 12) {
	  length += locaTable[j].len;
	  if (length & 3) {
	    length += 4 - (length & 3);
	  }
	  if (checkRegion(glyfPos + locaTable[j].origOffset,
			  locaTable[j].len)) {
	    checksum +=
	        computeTableChecksum(file + glyfPos + locaTable[j].origOffset,
				     locaTable[j].len);
	  }
	}
      }
    } else {
      if ((j = seekTable(t42Tables[i].tag)) >= 0) {
	length = tables[j].len;
	if (checkRegion(tables[j].offset, length)) {
	  checksum = computeTableChecksum(file + tables[j].offset, length);
	}
      } else if (needVerticalMetrics && i == t42VheaTable) {
	vheaTab[10] = (Guchar)(advance / 256);    // max advance height
	vheaTab[11] = (Guchar)(advance % 256);
	length = sizeof(vheaTab);
	checksum = computeTableChecksum(vheaTab, length);
      } else if (needVerticalMetrics && i == t42VmtxTable) {
	length = 4 + (nGlyphs - 1) * 2;
	vmtxTab = (Guchar *)gmalloc(length);
	vmtxTab[0] = (Guchar)(advance / 256);
	vmtxTab[1] = (Guchar)(advance % 256);
	for (j = 2; j < length; j += 2) {
	  vmtxTab[j] = 0;
	  vmtxTab[j+1] = 0;
	}
	checksum = computeTableChecksum(vmtxTab, length);
      } else if (t42Tables[i].required) {
	//~ error(-1, "Embedded TrueType font is missing a required table ('%s')",
	//~       t42Tables[i].tag);
	length = 0;
	checksum = 0;
      }
    }
    if (length >= 0) {
      newTables[k].tag = ((t42Tables[i].tag[0] & 0xff) << 24) |
	                 ((t42Tables[i].tag[1] & 0xff) << 16) |
	                 ((t42Tables[i].tag[2] & 0xff) <<  8) |
	                  (t42Tables[i].tag[3] & 0xff);
      newTables[k].checksum = checksum;
      newTables[k].offset = pos;
      newTables[k].len = length;
      pos += length;
      if (pos & 3) {
	pos += 4 - (length & 3);
      }
      ++k;
    }
  }

  // construct the table directory
  tableDir[0] = 0x00;		// sfnt version
  tableDir[1] = 0x01;
  tableDir[2] = 0x00;
  tableDir[3] = 0x00;
  tableDir[4] = 0;		// numTables
  tableDir[5] = (Guchar)nNewTables;
  tableDir[6] = 0;		// searchRange
  tableDir[7] = (Guchar)128;
  tableDir[8] = 0;		// entrySelector
  tableDir[9] = 3;
  tableDir[10] = 0;		// rangeShift
  tableDir[11] = (Guchar)(16 * nNewTables - 128);
  pos = 12;
  for (i = 0; i < nNewTables; ++i) {
    tableDir[pos   ] = (Guchar)(newTables[i].tag >> 24);
    tableDir[pos+ 1] = (Guchar)(newTables[i].tag >> 16);
    tableDir[pos+ 2] = (Guchar)(newTables[i].tag >>  8);
    tableDir[pos+ 3] = (Guchar) newTables[i].tag;
    tableDir[pos+ 4] = (Guchar)(newTables[i].checksum >> 24);
    tableDir[pos+ 5] = (Guchar)(newTables[i].checksum >> 16);
    tableDir[pos+ 6] = (Guchar)(newTables[i].checksum >>  8);
    tableDir[pos+ 7] = (Guchar) newTables[i].checksum;
    tableDir[pos+ 8] = (Guchar)(newTables[i].offset >> 24);
    tableDir[pos+ 9] = (Guchar)(newTables[i].offset >> 16);
    tableDir[pos+10] = (Guchar)(newTables[i].offset >>  8);
    tableDir[pos+11] = (Guchar) newTables[i].offset;
    tableDir[pos+12] = (Guchar)(newTables[i].len >> 24);
    tableDir[pos+13] = (Guchar)(newTables[i].len >> 16);
    tableDir[pos+14] = (Guchar)(newTables[i].len >>  8);
    tableDir[pos+15] = (Guchar) newTables[i].len;
    pos += 16;
  }

  // compute the font checksum and store it in the head table
  checksum = computeTableChecksum(tableDir, 12 + nNewTables*16);
  for (i = 0; i < nNewTables; ++i) {
    checksum += newTables[i].checksum;
  }
  checksum = 0xb1b0afba - checksum; // because the TrueType spec says so
  headData[ 8] = (Guchar)(checksum >> 24);
  headData[ 9] = (Guchar)(checksum >> 16);
  headData[10] = (Guchar)(checksum >>  8);
  headData[11] = (Guchar) checksum;

  // start the sfnts array
  if (name) {
    (*outputFunc)(outputStream, "/", 1);
    (*outputFunc)(outputStream, name->getCString(), name->getLength());
    (*outputFunc)(outputStream, " [\n", 3);
  } else {
    (*outputFunc)(outputStream, "/sfnts [\n", 9);
  }

  // write the table directory
  dumpString(tableDir, 12 + nNewTables*16, outputFunc, outputStream);

  // write the tables
  for (i = 0; i < nNewTables; ++i) {
    if (i == t42HeadTable) {
      dumpString(headData, 54, outputFunc, outputStream);
    } else if (i == t42LocaTable) {
      length = (nGlyphs + 1) * (locaFmt ? 4 : 2);
      dumpString(locaData, length, outputFunc, outputStream);
    } else if (i == t42GlyfTable) {
      glyfPos = tables[seekTable("glyf")].offset;
      for (j = 0; j < nGlyphs; ++j) {
	if (locaTable[j].len > 12 &&
	    checkRegion(glyfPos + locaTable[j].origOffset, locaTable[j].len)) {
	  dumpString(file + glyfPos + locaTable[j].origOffset,
		     locaTable[j].len, outputFunc, outputStream);
	}
      }
    } else {
      // length == 0 means the table is missing and the error was
      // already reported during the construction of the table
      // headers
      if ((length = newTables[i].len) > 0) {
	if ((j = seekTable(t42Tables[i].tag)) >= 0 &&
	    checkRegion(tables[j].offset, tables[j].len)) {
	  dumpString(file + tables[j].offset, tables[j].len,
		     outputFunc, outputStream);
	} else if (needVerticalMetrics && i == t42VheaTable) {
	  dumpString(vheaTab, length, outputFunc, outputStream);
	} else if (needVerticalMetrics && i == t42VmtxTable) {
	  dumpString(vmtxTab, length, outputFunc, outputStream);
	}
      }
    }
  }

  // end the sfnts array
  (*outputFunc)(outputStream, "] def\n", 6);

  gfree(locaData);
  gfree(locaTable);
  if (vmtxTab) {
    gfree(vmtxTab);
  }
}

void FoFiTrueType::dumpString(Guchar *s, int length,
			      FoFiOutputFunc outputFunc,
			      void *outputStream) {
  GString *buf;
  int pad, i, j;

  (*outputFunc)(outputStream, "<", 1);
  for (i = 0; i < length; i += 32) {
    for (j = 0; j < 32 && i+j < length; ++j) {
      buf = GString::format("{0:02x}", s[i+j] & 0xff);
      (*outputFunc)(outputStream, buf->getCString(), buf->getLength());
      delete buf;
    }
    if (i % (65536 - 32) == 65536 - 64) {
      (*outputFunc)(outputStream, ">\n<", 3);
    } else if (i+32 < length) {
      (*outputFunc)(outputStream, "\n", 1);
    }
  }
  if (length & 3) {
    pad = 4 - (length & 3);
    for (i = 0; i < pad; ++i) {
      (*outputFunc)(outputStream, "00", 2);
    }
  }
  // add an extra zero byte because the Adobe Type 42 spec says so
  (*outputFunc)(outputStream, "00>\n", 4);
}

Guint FoFiTrueType::computeTableChecksum(Guchar *data, int length) {
  Guint checksum, word;
  int i;

  checksum = 0;
  for (i = 0; i+3 < length; i += 4) {
    word = ((data[i  ] & 0xff) << 24) +
           ((data[i+1] & 0xff) << 16) +
           ((data[i+2] & 0xff) <<  8) +
            (data[i+3] & 0xff);
    checksum += word;
  }
  if (length & 3) {
    word = 0;
    i = length & ~3;
    switch (length & 3) {
    case 3:
      word |= (data[i+2] & 0xff) <<  8;
    case 2:
      word |= (data[i+1] & 0xff) << 16;
    case 1:
      word |= (data[i  ] & 0xff) << 24;
      break;
    }
    checksum += word;
  }
  return checksum;
}

void FoFiTrueType::parse(int fontNum, GBool allowHeadlessCFF) {
  Guint topTag;
  int offset, pos, ver, i, j, k;

  parsedOk = gTrue;

  // check for a dfont or TrueType collection (TTC)
  // offset = start of actual TrueType font file (table positions are
  //          relative to this
  // pos = position of table directory (relative to offset)
  if (isDfont) {
    parseDfont(fontNum, &offset, &pos);
  } else {
    offset = 0;
    topTag = getU32BE(0, &parsedOk);
    if (!parsedOk) {
      return;
    }
    if (topTag == ttcfTag) {
      parseTTC(fontNum, &pos);
    } else {
      pos = 0;
    }
  }
  if (!parsedOk) {
    return;
  }

  // check the sfnt version
  ver = getU32BE(offset + pos, &parsedOk);
  if (!parsedOk) {
    return;
  }
  openTypeCFF = ver == 0x4f54544f; // 'OTTO'

  // read the table directory
  nTables = getU16BE(offset + pos + 4, &parsedOk);
  if (!parsedOk) {
    return;
  }
  tables = (TrueTypeTable *)gmallocn(nTables, sizeof(TrueTypeTable));
  pos += 12;
  j = 0;
  for (i = 0; i < nTables; ++i) {
    tables[j].tag = getU32BE(offset + pos, &parsedOk);
    tables[j].checksum = getU32BE(offset + pos + 4, &parsedOk);
    tables[j].offset = offset + (int)getU32BE(offset + pos + 8, &parsedOk);
    tables[j].len = (int)getU32BE(offset + pos + 12, &parsedOk);
    if (tables[j].offset >= 0 &&
	tables[j].len >= 0 &&
	tables[j].offset + tables[j].len >= tables[j].offset &&
	tables[j].offset + tables[j].len <= len) {
      // ignore any bogus entries in the table directory
      ++j;
    }
    pos += 16;
  }
  nTables = j;
  if (!parsedOk) {
    return;
  }

  // check for the head table; allow for a head-less OpenType CFF font
  headlessCFF = gFalse;
  if (seekTable("head") < 0) {
    if (openTypeCFF && allowHeadlessCFF && seekTable("CFF ") >= 0) {
      headlessCFF = gTrue;
      nGlyphs = 0;
      bbox[0] = bbox[1] = bbox[2] = bbox[3] = 0;
      locaFmt = 0;
      return;
    }
    parsedOk = gFalse;
    return;
  }

  // check for other tables that are required by both the TrueType
  // spec and the Type 42 spec
  if (seekTable("hhea") < 0 ||
      seekTable("maxp") < 0 ||
      seekTable("hmtx") < 0 ||
      (!openTypeCFF && seekTable("loca") < 0) ||
      (!openTypeCFF && seekTable("glyf") < 0) ||
      (openTypeCFF && seekTable("CFF ") < 0)) {
    parsedOk = gFalse;
    return;
  }

  // read the cmaps
  if ((i = seekTable("cmap")) >= 0 && tables[i].len >= 4) {
    pos = tables[i].offset + 2;
    nCmaps = getU16BE(pos, &parsedOk);
    pos += 2;
    if (!parsedOk) {
      return;
    }
    cmaps = (TrueTypeCmap *)gmallocn(nCmaps, sizeof(TrueTypeCmap));
    k = 0;
    for (j = 0; j < nCmaps; ++j) {
      cmaps[k].platform = getU16BE(pos, &parsedOk);
      cmaps[k].encoding = getU16BE(pos + 2, &parsedOk);
      cmaps[k].offset = getU32BE(pos + 4, &parsedOk);
      pos += 8;
      if (cmaps[k].offset >= tables[i].len) {
	// skip any invalid subtables
	continue;
      }
      cmaps[k].offset += tables[i].offset;
      cmaps[k].fmt = getU16BE(cmaps[k].offset, &parsedOk);
      cmaps[k].len = getU16BE(cmaps[k].offset + 2, &parsedOk);
      ++k;
    }
    nCmaps = k;
    if (!parsedOk) {
      return;
    }
  }

  // get the number of glyphs from the maxp table
  i = seekTable("maxp");
  nGlyphs = getU16BE(tables[i].offset + 4, &parsedOk);
  if (!parsedOk) {
    return;
  }

  // get the bbox and loca table format from the head table
  i = seekTable("head");
  bbox[0] = getS16BE(tables[i].offset + 36, &parsedOk);
  bbox[1] = getS16BE(tables[i].offset + 38, &parsedOk);
  bbox[2] = getS16BE(tables[i].offset + 40, &parsedOk);
  bbox[3] = getS16BE(tables[i].offset + 42, &parsedOk);
  locaFmt = getS16BE(tables[i].offset + 50, &parsedOk);
  if (!parsedOk) {
    return;
  }

  // make sure the loca table is sane (correct length)
  // NB: out-of-bounds entries are handled in writeTTF()
  if (!openTypeCFF) {
    i = seekTable("loca");
    if (tables[i].len < (locaFmt ? 4 : 2)) {
      parsedOk = gFalse;
      return;
    }
    if (tables[i].len < (nGlyphs + 1) * (locaFmt ? 4 : 2)) {
      nGlyphs = tables[i].len / (locaFmt ? 4 : 2) - 1;
    }
    if (!parsedOk) {
      return;
    }
  }

  // read the post table
  readPostTable();
}

// Get the table directory position
void FoFiTrueType::parseTTC(int fontNum, int *pos) {
  int nFonts;

  isTTC = gTrue;
  nFonts = getU32BE(8, &parsedOk);
  if (!parsedOk) {
    return;
  }
  if (fontNum < 0 || fontNum >= nFonts) {
    parsedOk = gFalse;
    return;
  }
  *pos = getU32BE(12 + 4 * fontNum, &parsedOk);
}

void FoFiTrueType::parseDfont(int fontNum, int *offset, int *startPos) {
  int resMapOffset, resDataOffset;
  int resTypeListOffset, nTypes, typeTag;
  int nFonts, refListOffset, dataOffset;
  int pos, i;

  resDataOffset = getU32BE(0, &parsedOk);
  resMapOffset = getU32BE(4, &parsedOk);
  if (!parsedOk) {
    return;
  }

  resTypeListOffset = getU16BE(resMapOffset + 24, &parsedOk);
  // resNameListOffset = getU16BE(resMapOffset + 26, &parsedOk);
  nTypes = getU16BE(resMapOffset + 28, &parsedOk) + 1;
  if (!parsedOk) {
    return;
  }

  pos = 0; // make gcc happy
  for (i = 0; i < nTypes; ++i) {
    pos = resMapOffset + resTypeListOffset + 2 + 8*i;
    typeTag = getU32BE(pos, &parsedOk);
    if (!parsedOk) {
      return;
    }
    if (typeTag == 0x73666e74) { // 'sfnt'
      break;
    }
  }
  if (i >= nTypes) {
    parsedOk = gFalse;
    return;
  }
  nFonts = getU16BE(pos + 4, &parsedOk) + 1;
  refListOffset = getU16BE(pos + 6, &parsedOk);
  if (!parsedOk) {
    return;
  }
  if (fontNum < 0 || fontNum >= nFonts) {
    parsedOk = gFalse;
    return;
  }
  pos = resMapOffset + resTypeListOffset + refListOffset + 12 * fontNum;
  dataOffset = getU32BE(pos + 4, &parsedOk) & 0x00ffffff;
  if (!parsedOk) {
    return;
  }
  // the data offset points to a 4-byte length field, which we skip over
  *offset = resDataOffset + dataOffset + 4;
  *startPos = 0;
}

void FoFiTrueType::readPostTable() {
  GString *name;
  int tablePos, postFmt, stringIdx, stringPos;
  GBool ok;
  int i, j, n, m;

  ok = gTrue;
  if ((i = seekTable("post")) < 0) {
    return;
  }
  tablePos = tables[i].offset;
  postFmt = getU32BE(tablePos, &ok);
  if (!ok) {
    goto err;
  }
  if (postFmt == 0x00010000) {
    nameToGID = new GHash(gTrue);
    for (i = 0; i < 258; ++i) {
      nameToGID->add(new GString(macGlyphNames[i]), i);
    }
  } else if (postFmt == 0x00020000) {
    nameToGID = new GHash(gTrue);
    n = getU16BE(tablePos + 32, &ok);
    if (!ok) {
      goto err;
    }
    stringIdx = 0;
    stringPos = tablePos + 34 + 2*n;
    for (i = 0; i < n && i < nGlyphs; ++i) {
      ok = gTrue;
      j = getU16BE(tablePos + 34 + 2*i, &ok);
      if (j < 258) {
	nameToGID->removeInt(macGlyphNames[j]);
	nameToGID->add(new GString(macGlyphNames[j]), i);
      } else {
	j -= 258;
	if (j != stringIdx) {
	  for (stringIdx = 0, stringPos = tablePos + 34 + 2*n;
	       stringIdx < j;
	       ++stringIdx, stringPos += 1 + getU8(stringPos, &ok)) ;
	  if (!ok) {
	    continue;
	  }
	}
	m = getU8(stringPos, &ok);
	if (!ok || !checkRegion(stringPos + 1, m)) {
	  continue;
	}
	name = new GString((char *)&file[stringPos + 1], m);
	nameToGID->removeInt(name);
	nameToGID->add(name, i);
	++stringIdx;
	stringPos += 1 + m;
      }
    }
  } else if (postFmt == 0x00028000) {
    nameToGID = new GHash(gTrue);
    for (i = 0; i < nGlyphs; ++i) {
      j = getU8(tablePos + 32 + i, &ok);
      if (!ok) {
	continue;
      }
      if (j < 258) {
	nameToGID->removeInt(macGlyphNames[j]);
	nameToGID->add(new GString(macGlyphNames[j]), i);
      }
    }
  }

  return;

 err:
  if (nameToGID) {
    delete nameToGID;
    nameToGID = NULL;
  }
}

int FoFiTrueType::seekTable(const char *tag) {
  Guint tagI;
  int i;

  tagI = ((tag[0] & 0xff) << 24) |
         ((tag[1] & 0xff) << 16) |
         ((tag[2] & 0xff) << 8) |
          (tag[3] & 0xff);
  for (i = 0; i < nTables; ++i) {
    if (tables[i].tag == tagI) {
      return i;
    }
  }
  return -1;
}
