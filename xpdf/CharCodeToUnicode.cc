//========================================================================
//
// CharCodeToUnicode.cc
//
// Copyright 2001-2003 Glyph & Cog, LLC
//
//========================================================================

#include <aconf.h>

#ifdef USE_GCC_PRAGMAS
#pragma implementation
#endif

#include <stdio.h>
#include <string.h>
#include "gmem.h"
#include "gmempp.h"
#include "gfile.h"
#include "GString.h"
#include "Error.h"
#include "GlobalParams.h"
#include "PSTokenizer.h"
#include "CharCodeToUnicode.h"

//------------------------------------------------------------------------

#define maxUnicodeString 8

struct CharCodeToUnicodeString {
  CharCode c;
  Unicode u[maxUnicodeString];
  int len;
};

//------------------------------------------------------------------------

struct GStringIndex {
  GString *s;
  int i;
};

static int getCharFromGString(void *data) {
  GStringIndex *idx = (GStringIndex *)data;
  if (idx->i >= idx->s->getLength()) {
    return EOF;
  }
  return idx->s->getChar(idx->i++) & 0xff;
}

static int getCharFromFile(void *data) {
  return fgetc((FILE *)data);
}

//------------------------------------------------------------------------

static int hexCharVals[256] = {
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0x
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 1x
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 2x
   0,  1,  2,  3,  4,  5,  6,  7,  8,  9, -1, -1, -1, -1, -1, -1, // 3x
  -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 4x
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 5x
  -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 6x
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 7x
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 8x
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 9x
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // Ax
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // Bx
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // Cx
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // Dx
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // Ex
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1  // Fx
};

// Parse a <len>-byte hex string <s> into *<val>.  Returns false on
// error.
static GBool parseHex(char *s, int len, Guint *val) {
  int i, x;

  *val = 0;
  for (i = 0; i < len; ++i) {
    x = hexCharVals[s[i] & 0xff];
    if (x < 0) {
      return gFalse;
    }
    *val = (*val << 4) + x;
  }
  return gTrue;
}

//------------------------------------------------------------------------

CharCodeToUnicode *CharCodeToUnicode::makeIdentityMapping() {
  return new CharCodeToUnicode();
}

CharCodeToUnicode *CharCodeToUnicode::parseCIDToUnicode(GString *fileName,
							GString *collection) {
  FILE *f;
  Unicode *mapA;
  CharCode size, mapLenA;
  char buf[64];
  Unicode u;
  CharCodeToUnicode *ctu;

  if (!(f = openFile(fileName->getCString(), "r"))) {
    error(errSyntaxError, -1, "Couldn't open cidToUnicode file '{0:t}'",
	  fileName);
    return NULL;
  }

  size = 32768;
  mapA = (Unicode *)gmallocn(size, sizeof(Unicode));
  mapLenA = 0;

  while (getLine(buf, sizeof(buf), f)) {
    if (mapLenA == size) {
      size *= 2;
      mapA = (Unicode *)greallocn(mapA, size, sizeof(Unicode));
    }
    if (sscanf(buf, "%x", &u) == 1) {
      mapA[mapLenA] = u;
    } else {
      error(errSyntaxWarning, -1,
	    "Bad line ({0:d}) in cidToUnicode file '{1:t}'",
	    (int)(mapLenA + 1), fileName);
      mapA[mapLenA] = 0;
    }
    ++mapLenA;
  }
  fclose(f);

  ctu = new CharCodeToUnicode(collection->copy(), mapA, mapLenA, gTrue,
			      NULL, 0, 0);
  gfree(mapA);
  return ctu;
}

CharCodeToUnicode *CharCodeToUnicode::parseUnicodeToUnicode(
						    GString *fileName) {
  FILE *f;
  Unicode *mapA;
  CharCodeToUnicodeString *sMapA;
  CharCode size, oldSize, len, sMapSizeA, sMapLenA;
  char buf[256];
  char *tok;
  Unicode u0;
  Unicode uBuf[maxUnicodeString];
  CharCodeToUnicode *ctu;
  int line, n, i;

  if (!(f = openFile(fileName->getCString(), "r"))) {
    error(errSyntaxError, -1, "Couldn't open unicodeToUnicode file '{0:t}'",
	  fileName);
    return NULL;
  }

  size = 4096;
  mapA = (Unicode *)gmallocn(size, sizeof(Unicode));
  memset(mapA, 0, size * sizeof(Unicode));
  len = 0;
  sMapA = NULL;
  sMapSizeA = sMapLenA = 0;

  line = 0;
  while (getLine(buf, sizeof(buf), f)) {
    ++line;
    if (!(tok = strtok(buf, " \t\r\n")) ||
	!parseHex(tok, (int)strlen(tok), &u0)) {
      error(errSyntaxWarning, -1,
	    "Bad line ({0:d}) in unicodeToUnicode file '{1:t}'",
	    line, fileName);
      continue;
    }
    n = 0;
    while (n < maxUnicodeString) {
      if (!(tok = strtok(NULL, " \t\r\n"))) {
	break;
      }
      if (!parseHex(tok, (int)strlen(tok), &uBuf[n])) {
	error(errSyntaxWarning, -1,
	      "Bad line ({0:d}) in unicodeToUnicode file '{1:t}'",
	      line, fileName);
	break;
      }
      ++n;
    }
    if (n < 1) {
      error(errSyntaxWarning, -1,
	    "Bad line ({0:d}) in unicodeToUnicode file '{1:t}'",
	    line, fileName);
      continue;
    }
    if (u0 >= size) {
      oldSize = size;
      while (u0 >= size) {
	size *= 2;
      }
      mapA = (Unicode *)greallocn(mapA, size, sizeof(Unicode));
      memset(mapA + oldSize, 0, (size - oldSize) * sizeof(Unicode));
    }
    if (n == 1) {
      mapA[u0] = uBuf[0];
    } else {
      mapA[u0] = 0;
      if (sMapLenA == sMapSizeA) {
	sMapSizeA += 16;
	sMapA = (CharCodeToUnicodeString *)
	          greallocn(sMapA, sMapSizeA, sizeof(CharCodeToUnicodeString));
      }
      sMapA[sMapLenA].c = u0;
      for (i = 0; i < n; ++i) {
	sMapA[sMapLenA].u[i] = uBuf[i];
      }
      sMapA[sMapLenA].len = n;
      ++sMapLenA;
    }
    if (u0 >= len) {
      len = u0 + 1;
    }
  }
  fclose(f);

  ctu = new CharCodeToUnicode(fileName->copy(), mapA, len, gTrue,
			      sMapA, sMapLenA, sMapSizeA);
  gfree(mapA);
  return ctu;
}

CharCodeToUnicode *CharCodeToUnicode::make8BitToUnicode(Unicode *toUnicode) {
  return new CharCodeToUnicode(NULL, toUnicode, 256, gTrue, NULL, 0, 0);
}

CharCodeToUnicode *CharCodeToUnicode::make16BitToUnicode(Unicode *toUnicode) {
  return new CharCodeToUnicode(NULL, toUnicode, 65536, gTrue, NULL, 0, 0);
}

CharCodeToUnicode *CharCodeToUnicode::parseCMap(GString *buf, int nBits) {
  CharCodeToUnicode *ctu;
  GStringIndex idx;

  ctu = new CharCodeToUnicode(NULL);
  idx.s = buf;
  idx.i = 0;
  if (!ctu->parseCMap1(&getCharFromGString, &idx, nBits)) {
    delete ctu;
    return NULL;
  }
  return ctu;
}

void CharCodeToUnicode::mergeCMap(GString *buf, int nBits) {
  GStringIndex idx;

  idx.s = buf;
  idx.i = 0;
  parseCMap1(&getCharFromGString, &idx, nBits);
}

GBool CharCodeToUnicode::parseCMap1(int (*getCharFunc)(void *), void *data,
				    int nBits) {
  PSTokenizer *pst;
  char tok1[256], tok2[256], tok3[256];
  int n1, n2, n3;
  CharCode i;
  CharCode maxCode, code1, code2;
  GString *name;
  FILE *f;
  GBool ok;

  ok = gFalse;
  maxCode = (nBits == 8) ? 0xff : (nBits == 16) ? 0xffff : 0xffffffff;
  pst = new PSTokenizer(getCharFunc, data);
  pst->getToken(tok1, sizeof(tok1), &n1);
  while (pst->getToken(tok2, sizeof(tok2), &n2)) {
    if (!strcmp(tok2, "usecmap")) {
      if (tok1[0] == '/') {
	name = new GString(tok1 + 1);
	if ((f = globalParams->findToUnicodeFile(name))) {
	  if (parseCMap1(&getCharFromFile, f, nBits)) {
	    ok = gTrue;
	  }
	  fclose(f);
	} else {
	  error(errSyntaxError, -1,
		"Couldn't find ToUnicode CMap file for '{1:t}'",
		name);
	}
	delete name;
      }
      pst->getToken(tok1, sizeof(tok1), &n1);
    } else if (!strcmp(tok2, "beginbfchar")) {
      while (pst->getToken(tok1, sizeof(tok1), &n1)) {
	if (!strcmp(tok1, "endbfchar")) {
	  break;
	}
	if (!pst->getToken(tok2, sizeof(tok2), &n2) ||
	    !strcmp(tok2, "endbfchar")) {
	  error(errSyntaxWarning, -1,
		"Illegal entry in bfchar block in ToUnicode CMap");
	  break;
	}
	if (!(tok1[0] == '<' && tok1[n1 - 1] == '>' &&
	      tok2[0] == '<' && tok2[n2 - 1] == '>')) {
	  error(errSyntaxWarning, -1,
		"Illegal entry in bfchar block in ToUnicode CMap");
	  continue;
	}
	tok1[n1 - 1] = tok2[n2 - 1] = '\0';
	if (!parseHex(tok1 + 1, n1 - 2, &code1)) {
	  error(errSyntaxWarning, -1,
		"Illegal entry in bfchar block in ToUnicode CMap");
	  continue;
	}
	if (code1 > maxCode) {
	  error(errSyntaxWarning, -1,
		"Invalid entry in bfchar block in ToUnicode CMap");
	}
	addMapping(code1, tok2 + 1, n2 - 2, 0);
	ok = gTrue;
      }
      pst->getToken(tok1, sizeof(tok1), &n1);
    } else if (!strcmp(tok2, "beginbfrange")) {
      while (pst->getToken(tok1, sizeof(tok1), &n1)) {
	if (!strcmp(tok1, "endbfrange")) {
	  break;
	}
	if (!pst->getToken(tok2, sizeof(tok2), &n2) ||
	    !strcmp(tok2, "endbfrange") ||
	    !pst->getToken(tok3, sizeof(tok3), &n3) ||
	    !strcmp(tok3, "endbfrange")) {
	  error(errSyntaxWarning, -1,
		"Illegal entry in bfrange block in ToUnicode CMap");
	  break;
	}
	if (!(tok1[0] == '<' && tok1[n1 - 1] == '>' &&
	      tok2[0] == '<' && tok2[n2 - 1] == '>')) {
	  error(errSyntaxWarning,
		-1, "Illegal entry in bfrange block in ToUnicode CMap");
	  continue;
	}
	tok1[n1 - 1] = tok2[n2 - 1] = '\0';
	if (!parseHex(tok1 + 1, n1 - 2, &code1) ||
	    !parseHex(tok2 + 1, n2 - 2, &code2)) {
	  error(errSyntaxWarning, -1,
		"Illegal entry in bfrange block in ToUnicode CMap");
	  continue;
	}
	if (code1 > maxCode || code2 > maxCode) {
	  error(errSyntaxWarning, -1,
		"Invalid entry in bfrange block in ToUnicode CMap");
	  if (code2 > maxCode) {
	    code2 = maxCode;
	  }
	}
	if (!strcmp(tok3, "[")) {
	  i = 0;
	  while (pst->getToken(tok1, sizeof(tok1), &n1)) {
	    if (!strcmp(tok1, "]")) {
	      break;
	    }
	    if (tok1[0] == '<' && tok1[n1 - 1] == '>') {
	      if (code1 + i <= code2) {
		tok1[n1 - 1] = '\0';
		addMapping(code1 + i, tok1 + 1, n1 - 2, 0);
		ok = gTrue;
	      }
	    } else {
	      error(errSyntaxWarning, -1,
		    "Illegal entry in bfrange block in ToUnicode CMap");
	    }
	    ++i;
	  }
	} else if (tok3[0] == '<' && tok3[n3 - 1] == '>') {
	  tok3[n3 - 1] = '\0';
	  for (i = 0; code1 <= code2; ++code1, ++i) {
	    addMapping(code1, tok3 + 1, n3 - 2, i);
	    ok = gTrue;
	  }
	} else {
	  error(errSyntaxWarning, -1,
		"Illegal entry in bfrange block in ToUnicode CMap");
	}
      }
      pst->getToken(tok1, sizeof(tok1), &n1);
    } else if (!strcmp(tok2, "begincidchar")) {
      // the begincidchar operator is not allowed in ToUnicode CMaps,
      // but some buggy PDF generators incorrectly use
      // code-to-CID-type CMaps here
      error(errSyntaxWarning, -1,
	    "Invalid 'begincidchar' operator in ToUnicode CMap");
      while (pst->getToken(tok1, sizeof(tok1), &n1)) {
	if (!strcmp(tok1, "endcidchar")) {
	  break;
	}
	if (!pst->getToken(tok2, sizeof(tok2), &n2) ||
	    !strcmp(tok2, "endcidchar")) {
	  error(errSyntaxWarning, -1,
		"Illegal entry in cidchar block in ToUnicode CMap");
	  break;
	}
	if (!(tok1[0] == '<' && tok1[n1 - 1] == '>')) {
	  error(errSyntaxWarning, -1,
		"Illegal entry in cidchar block in ToUnicode CMap");
	  continue;
	}
	tok1[n1 - 1] = '\0';
	if (!parseHex(tok1 + 1, n1 - 2, &code1)) {
	  error(errSyntaxWarning, -1,
		"Illegal entry in cidchar block in ToUnicode CMap");
	  continue;
	}
	if (code1 > maxCode) {
	  error(errSyntaxWarning, -1,
		"Invalid entry in cidchar block in ToUnicode CMap");
	}
	addMappingInt(code1, atoi(tok2));
	ok = gTrue;
      }
      pst->getToken(tok1, sizeof(tok1), &n1);
    } else if (!strcmp(tok2, "begincidrange")) {
      // the begincidrange operator is not allowed in ToUnicode CMaps,
      // but some buggy PDF generators incorrectly use
      // code-to-CID-type CMaps here
      error(errSyntaxWarning, -1,
	    "Invalid 'begincidrange' operator in ToUnicode CMap");
      while (pst->getToken(tok1, sizeof(tok1), &n1)) {
	if (!strcmp(tok1, "endcidrange")) {
	  break;
	}
	if (!pst->getToken(tok2, sizeof(tok2), &n2) ||
	    !strcmp(tok2, "endcidrange") ||
	    !pst->getToken(tok3, sizeof(tok3), &n3) ||
	    !strcmp(tok3, "endcidrange")) {
	  error(errSyntaxWarning, -1,
		"Illegal entry in cidrange block in ToUnicode CMap");
	  break;
	}
	if (!(tok1[0] == '<' && tok1[n1 - 1] == '>' &&
	      tok2[0] == '<' && tok2[n2 - 1] == '>')) {
	  error(errSyntaxWarning,
		-1, "Illegal entry in cidrange block in ToUnicode CMap");
	  continue;
	}
	tok1[n1 - 1] = tok2[n2 - 1] = '\0';
	if (!parseHex(tok1 + 1, n1 - 2, &code1) ||
	    !parseHex(tok2 + 1, n2 - 2, &code2)) {
	  error(errSyntaxWarning, -1,
		"Illegal entry in cidrange block in ToUnicode CMap");
	  continue;
	}
	if (code1 > maxCode || code2 > maxCode) {
	  error(errSyntaxWarning, -1,
		"Invalid entry in cidrange block in ToUnicode CMap");
	  if (code2 > maxCode) {
	    code2 = maxCode;
	  }
	}
	for (i = atoi(tok3); code1 <= code2; ++code1, ++i) {
	  addMappingInt(code1, i);
	  ok = gTrue;
	}
      }
      pst->getToken(tok1, sizeof(tok1), &n1);
    } else {
      strcpy(tok1, tok2);
    }
  }
  delete pst;
  return ok;
}

void CharCodeToUnicode::addMapping(CharCode code, char *uStr, int n,
				   int offset) {
  CharCode oldLen, i;
  Unicode u[maxUnicodeString];
  int uLen, j;

  if (code > 0xffffff) {
    // This is an arbitrary limit to avoid integer overflow issues.
    // (I've seen CMaps with mappings for <ffffffff>.)
    return;
  }
  if ((uLen = parseUTF16String(uStr, n, u)) == 0) {
    return;
  }
  if (code >= mapLen) {
    oldLen = mapLen;
    mapLen = mapLen ? 2 * mapLen : 256;
    if (code >= mapLen) {
      mapLen = (code + 256) & ~255;
    }
    map = (Unicode *)greallocn(map, mapLen, sizeof(Unicode));
    for (i = oldLen; i < mapLen; ++i) {
      map[i] = 0;
    }
  }
  if (uLen == 1) {
    map[code] = u[0] + offset;
  } else {
    if (sMapLen >= sMapSize) {
      sMapSize = sMapSize + 16;
      sMap = (CharCodeToUnicodeString *)
	       greallocn(sMap, sMapSize, sizeof(CharCodeToUnicodeString));
    }
    map[code] = 0;
    sMap[sMapLen].c = code;
    for (j = 0; j < uLen; ++j) {
      sMap[sMapLen].u[j] = u[j];
    }
    sMap[sMapLen].u[uLen - 1] += offset;
    sMap[sMapLen].len = uLen;
    ++sMapLen;
  }
}

// Convert a UTF-16BE hex string into a sequence of up to
// maxUnicodeString Unicode chars.
int CharCodeToUnicode::parseUTF16String(char *uStr, int n, Unicode *uOut) {
  int i = 0;
  int uLen = 0;
  while (i < n) {
    Unicode u;
    int j = n;
    if (j - i > 4) {
      j = i + 4;
    }
    if (!parseHex(uStr + i, j - i, &u)) {
      error(errSyntaxWarning, -1, "Illegal entry in ToUnicode CMap");
      return 0;
    }
    // look for a UTF-16 pair
    if (uLen > 0 && uOut[uLen-1] >= 0xd800 && uOut[uLen-1] <= 0xdbff &&
	u >= 0xdc00 && u <= 0xdfff) {
      uOut[uLen-1] = 0x10000 + ((uOut[uLen-1] & 0x03ff) << 10) + (u & 0x03ff);
    } else {
      if (uLen < maxUnicodeString) {
	uOut[uLen++] = u;
      }
    }
    i = j;
  }
  return uLen;
}

void CharCodeToUnicode::addMappingInt(CharCode code, Unicode u) {
  CharCode oldLen, i;

  if (code > 0xffffff) {
    // This is an arbitrary limit to avoid integer overflow issues.
    // (I've seen CMaps with mappings for <ffffffff>.)
    return;
  }
  if (code >= mapLen) {
    oldLen = mapLen;
    mapLen = mapLen ? 2 * mapLen : 256;
    if (code >= mapLen) {
      mapLen = (code + 256) & ~255;
    }
    map = (Unicode *)greallocn(map, mapLen, sizeof(Unicode));
    for (i = oldLen; i < mapLen; ++i) {
      map[i] = 0;
    }
  }
  map[code] = u;
}

CharCodeToUnicode::CharCodeToUnicode() {
  tag = NULL;
  map = NULL;
  mapLen = 0;
  sMap = NULL;
  sMapLen = sMapSize = 0;
  refCnt = 1;
}

CharCodeToUnicode::CharCodeToUnicode(GString *tagA) {
  CharCode i;

  tag = tagA;
  mapLen = 256;
  map = (Unicode *)gmallocn(mapLen, sizeof(Unicode));
  for (i = 0; i < mapLen; ++i) {
    map[i] = 0;
  }
  sMap = NULL;
  sMapLen = sMapSize = 0;
  refCnt = 1;
}

CharCodeToUnicode::CharCodeToUnicode(GString *tagA, Unicode *mapA,
				     CharCode mapLenA, GBool copyMap,
				     CharCodeToUnicodeString *sMapA,
				     int sMapLenA, int sMapSizeA) {
  tag = tagA;
  mapLen = mapLenA;
  if (copyMap) {
    map = (Unicode *)gmallocn(mapLen, sizeof(Unicode));
    memcpy(map, mapA, mapLen * sizeof(Unicode));
  } else {
    map = mapA;
  }
  sMap = sMapA;
  sMapLen = sMapLenA;
  sMapSize = sMapSizeA;
  refCnt = 1;
}

CharCodeToUnicode::~CharCodeToUnicode() {
  if (tag) {
    delete tag;
  }
  gfree(map);
  gfree(sMap);
}

void CharCodeToUnicode::incRefCnt() {
#if MULTITHREADED
  gAtomicIncrement(&refCnt);
#else
  ++refCnt;
#endif
}

void CharCodeToUnicode::decRefCnt() {
  GBool done;

#if MULTITHREADED
  done = gAtomicDecrement(&refCnt) == 0;
#else
  done = --refCnt == 0;
#endif
  if (done) {
    delete this;
  }
}

GBool CharCodeToUnicode::match(GString *tagA) {
  return tag && !tag->cmp(tagA);
}

void CharCodeToUnicode::setMapping(CharCode c, Unicode *u, int len) {
  int i, j;

  if (!map) {
    return;
  }
  if (len == 1) {
    map[c] = u[0];
  } else {
    for (i = 0; i < sMapLen; ++i) {
      if (sMap[i].c == c) {
	break;
      }
    }
    if (i == sMapLen) {
      if (sMapLen == sMapSize) {
	sMapSize += 8;
	sMap = (CharCodeToUnicodeString *)
	         greallocn(sMap, sMapSize, sizeof(CharCodeToUnicodeString));
      }
      ++sMapLen;
    }
    map[c] = 0;
    sMap[i].c = c;
    sMap[i].len = len;
    for (j = 0; j < len && j < maxUnicodeString; ++j) {
      sMap[i].u[j] = u[j];
    }
  }
}

int CharCodeToUnicode::mapToUnicode(CharCode c, Unicode *u, int size) {
  int i, j;

  if (!map) {
    u[0] = (Unicode)c;
    return 1;
  }
  if (c >= mapLen) {
    return 0;
  }
  if (map[c]) {
    u[0] = map[c];
    return 1;
  }
  for (i = 0; i < sMapLen; ++i) {
    if (sMap[i].c == c) {
      for (j = 0; j < sMap[i].len && j < size; ++j) {
	u[j] = sMap[i].u[j];
      }
      return j;
    }
  }
  return 0;
}

//------------------------------------------------------------------------

CharCodeToUnicodeCache::CharCodeToUnicodeCache(int sizeA) {
  int i;

  size = sizeA;
  cache = (CharCodeToUnicode **)gmallocn(size, sizeof(CharCodeToUnicode *));
  for (i = 0; i < size; ++i) {
    cache[i] = NULL;
  }
}

CharCodeToUnicodeCache::~CharCodeToUnicodeCache() {
  int i;

  for (i = 0; i < size; ++i) {
    if (cache[i]) {
      cache[i]->decRefCnt();
    }
  }
  gfree(cache);
}

CharCodeToUnicode *CharCodeToUnicodeCache::getCharCodeToUnicode(GString *tag) {
  CharCodeToUnicode *ctu;
  int i, j;

  if (cache[0] && cache[0]->match(tag)) {
    cache[0]->incRefCnt();
    return cache[0];
  }
  for (i = 1; i < size; ++i) {
    if (cache[i] && cache[i]->match(tag)) {
      ctu = cache[i];
      for (j = i; j >= 1; --j) {
	cache[j] = cache[j - 1];
      }
      cache[0] = ctu;
      ctu->incRefCnt();
      return ctu;
    }
  }
  return NULL;
}

void CharCodeToUnicodeCache::add(CharCodeToUnicode *ctu) {
  int i;

  if (cache[size - 1]) {
    cache[size - 1]->decRefCnt();
  }
  for (i = size - 1; i >= 1; --i) {
    cache[i] = cache[i - 1];
  }
  cache[0] = ctu;
  ctu->incRefCnt();
}
