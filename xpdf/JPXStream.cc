//========================================================================
//
// JPXStream.cc
//
// Copyright 2002-2003 Glyph & Cog, LLC
//
//========================================================================

#include <aconf.h>

#ifdef USE_GCC_PRAGMAS
#pragma implementation
#endif

#include <limits.h>
#include "gmem.h"
#include "gmempp.h"
#include "Error.h"
#include "JArithmeticDecoder.h"
#include "JPXStream.h"

//~ to do:
//  - ROI
//  - progression order changes
//  - packed packet headers
//  - support for palettes, channel maps, etc.
//  - make sure all needed JP2/JPX subboxes are parsed (readBoxes)
//  - can we assume that QCC segments must come after the QCD segment?
//  - handle tilePartToEOC in readTilePartData
//  - in coefficient decoding (readCodeBlockData):
//    - selective arithmetic coding bypass
//      (this also affects reading the cb->dataLen array)
//    - coeffs longer than 31 bits (should just ignore the extra bits?)
//  - handle boxes larger than 2^32 bytes
//  - the fixed-point arithmetic won't handle 16-bit pixels

//------------------------------------------------------------------------

// number of contexts for the arithmetic decoder
#define jpxNContexts        19

#define jpxContextSigProp    0	// 0 - 8: significance prop and cleanup
#define jpxContextSign       9	// 9 - 13: sign
#define jpxContextMagRef    14	// 14 - 16: magnitude refinement
#define jpxContextRunLength 17	// cleanup: run length
#define jpxContextUniform   18	// cleanup: first signif coeff

//------------------------------------------------------------------------

#define jpxPassSigProp       0
#define jpxPassMagRef        1
#define jpxPassCleanup       2

//------------------------------------------------------------------------

// arithmetic decoder context for the significance propagation and
// cleanup passes:
//     [horiz][vert][diag][subband]
// where subband = 0 for HL
//               = 1 for LH and LL
//               = 2 for HH
static Guint sigPropContext[3][3][5][3] = {
  {{{ 0, 0, 0 },   // horiz=0, vert=0, diag=0
    { 1, 1, 3 },   // horiz=0, vert=0, diag=1
    { 2, 2, 6 },   // horiz=0, vert=0, diag=2
    { 2, 2, 8 },   // horiz=0, vert=0, diag=3
    { 2, 2, 8 }},  // horiz=0, vert=0, diag=4
   {{ 5, 3, 1 },   // horiz=0, vert=1, diag=0
    { 6, 3, 4 },   // horiz=0, vert=1, diag=1
    { 6, 3, 7 },   // horiz=0, vert=1, diag=2
    { 6, 3, 8 },   // horiz=0, vert=1, diag=3
    { 6, 3, 8 }},  // horiz=0, vert=1, diag=4
   {{ 8, 4, 2 },   // horiz=0, vert=2, diag=0
    { 8, 4, 5 },   // horiz=0, vert=2, diag=1
    { 8, 4, 7 },   // horiz=0, vert=2, diag=2
    { 8, 4, 8 },   // horiz=0, vert=2, diag=3
    { 8, 4, 8 }}}, // horiz=0, vert=2, diag=4
  {{{ 3, 5, 1 },   // horiz=1, vert=0, diag=0
    { 3, 6, 4 },   // horiz=1, vert=0, diag=1
    { 3, 6, 7 },   // horiz=1, vert=0, diag=2
    { 3, 6, 8 },   // horiz=1, vert=0, diag=3
    { 3, 6, 8 }},  // horiz=1, vert=0, diag=4
   {{ 7, 7, 2 },   // horiz=1, vert=1, diag=0
    { 7, 7, 5 },   // horiz=1, vert=1, diag=1
    { 7, 7, 7 },   // horiz=1, vert=1, diag=2
    { 7, 7, 8 },   // horiz=1, vert=1, diag=3
    { 7, 7, 8 }},  // horiz=1, vert=1, diag=4
   {{ 8, 7, 2 },   // horiz=1, vert=2, diag=0
    { 8, 7, 5 },   // horiz=1, vert=2, diag=1
    { 8, 7, 7 },   // horiz=1, vert=2, diag=2
    { 8, 7, 8 },   // horiz=1, vert=2, diag=3
    { 8, 7, 8 }}}, // horiz=1, vert=2, diag=4
  {{{ 4, 8, 2 },   // horiz=2, vert=0, diag=0
    { 4, 8, 5 },   // horiz=2, vert=0, diag=1
    { 4, 8, 7 },   // horiz=2, vert=0, diag=2
    { 4, 8, 8 },   // horiz=2, vert=0, diag=3
    { 4, 8, 8 }},  // horiz=2, vert=0, diag=4
   {{ 7, 8, 2 },   // horiz=2, vert=1, diag=0
    { 7, 8, 5 },   // horiz=2, vert=1, diag=1
    { 7, 8, 7 },   // horiz=2, vert=1, diag=2
    { 7, 8, 8 },   // horiz=2, vert=1, diag=3
    { 7, 8, 8 }},  // horiz=2, vert=1, diag=4
   {{ 8, 8, 2 },   // horiz=2, vert=2, diag=0
    { 8, 8, 5 },   // horiz=2, vert=2, diag=1
    { 8, 8, 7 },   // horiz=2, vert=2, diag=2
    { 8, 8, 8 },   // horiz=2, vert=2, diag=3
    { 8, 8, 8 }}}  // horiz=2, vert=2, diag=4
};

// arithmetic decoder context and xor bit for the sign bit in the
// significance propagation pass:
//     [horiz][vert][k]
// where horiz/vert are offset by 2 (i.e., range is -2 .. 2)
// and k = 0 for the context
//       = 1 for the xor bit
static Guint signContext[5][5][2] = {
  {{ 13, 1 },  // horiz=-2, vert=-2
   { 13, 1 },  // horiz=-2, vert=-1
   { 12, 1 },  // horiz=-2, vert= 0
   { 11, 1 },  // horiz=-2, vert=+1
   { 11, 1 }}, // horiz=-2, vert=+2
  {{ 13, 1 },  // horiz=-1, vert=-2
   { 13, 1 },  // horiz=-1, vert=-1
   { 12, 1 },  // horiz=-1, vert= 0
   { 11, 1 },  // horiz=-1, vert=+1
   { 11, 1 }}, // horiz=-1, vert=+2
  {{ 10, 1 },  // horiz= 0, vert=-2
   { 10, 1 },  // horiz= 0, vert=-1
   {  9, 0 },  // horiz= 0, vert= 0
   { 10, 0 },  // horiz= 0, vert=+1
   { 10, 0 }}, // horiz= 0, vert=+2
  {{ 11, 0 },  // horiz=+1, vert=-2
   { 11, 0 },  // horiz=+1, vert=-1
   { 12, 0 },  // horiz=+1, vert= 0
   { 13, 0 },  // horiz=+1, vert=+1
   { 13, 0 }}, // horiz=+1, vert=+2
  {{ 11, 0 },  // horiz=+2, vert=-2
   { 11, 0 },  // horiz=+2, vert=-1
   { 12, 0 },  // horiz=+2, vert= 0
   { 13, 0 },  // horiz=+2, vert=+1
   { 13, 0 }}, // horiz=+2, vert=+2
};

//------------------------------------------------------------------------

// constants used in the IDWT
#define idwtAlpha  -1.586134342059924
#define idwtBeta   -0.052980118572961
#define idwtGamma   0.882911075530934
#define idwtDelta   0.443506852043971
#define idwtKappa   1.230174104914001
#define idwtIKappa  (1.0 / idwtKappa)

// sum of the sample size (number of bits) and the number of bits to
// the right of the decimal point for the fixed point arithmetic used
// in the IDWT
#define fracBits 24

//------------------------------------------------------------------------

// floor(x / y)
#define jpxFloorDiv(x, y) ((x) / (y))

// floor(x / 2^y)
#define jpxFloorDivPow2(x, y) ((x) >> (y))

// ceil(x / y)
#define jpxCeilDiv(x, y) (((x) + (y) - 1) / (y))

// ceil(x / 2^y)
#define jpxCeilDivPow2(x, y) (((x) + (1 << (y)) - 1) >> (y))

//------------------------------------------------------------------------

#if 1 //----- disable coverage tracking

#define cover(idx)

#else //----- enable coverage tracking

class JPXCover {
public:

  JPXCover(int sizeA);
  ~JPXCover();
  void incr(int idx);

private:

  int size, used;
  int *data;
};

JPXCover::JPXCover(int sizeA) {
  size = sizeA;
  used = -1;
  data = (int *)gmallocn(size, sizeof(int));
  memset(data, 0, size * sizeof(int));
}

JPXCover::~JPXCover() {
  int i;

  printf("JPX coverage:\n");
  for (i = 0; i <= used; ++i) {
    printf("  %4d: %8d\n", i, data[i]);
  }
  gfree(data);
}

void JPXCover::incr(int idx) {
  if (idx < size) {
    ++data[idx];
    if (idx > used) {
      used = idx;
    }
  }
}

JPXCover jpxCover(150);

#define cover(idx) jpxCover.incr(idx)

#endif //----- coverage tracking

//------------------------------------------------------------------------

JPXStream::JPXStream(Stream *strA):
  FilterStream(strA)
{
  bufStr = new BufStream(str, 3);

  decoded = gFalse;
  nComps = 0;
  bpc = NULL;
  width = height = 0;
  reduction = 0;
  haveCS = gFalse;

  palette.bpc = NULL;
  palette.c = NULL;
  havePalette = gFalse;

  compMap.comp = NULL;
  compMap.type = NULL;
  compMap.pComp = NULL;
  haveCompMap = gFalse;

  channelDefn.idx = NULL;
  channelDefn.type = NULL;
  channelDefn.assoc = NULL;
  haveChannelDefn = gFalse;

  img.tiles = NULL;

  bitBuf = 0;
  bitBufLen = 0;
  bitBufSkip = gFalse;
  byteCount = 0;
}

JPXStream::~JPXStream() {
  close();
  delete bufStr;
}

Stream *JPXStream::copy() {
  return new JPXStream(str->copy());
}

void JPXStream::reset() {
  img.ySize = 0;
  bufStr->reset();
  decoded = gFalse;
}

void JPXStream::close() {
  JPXTile *tile;
  JPXTileComp *tileComp;
  JPXResLevel *resLevel;
  JPXPrecinct *precinct;
  JPXSubband *subband;
  JPXCodeBlock *cb;
  Guint comp, i, k, r, pre, sb;

  gfree(bpc);
  bpc = NULL;
  if (havePalette) {
    gfree(palette.bpc);
    gfree(palette.c);
    havePalette = gFalse;
  }
  if (haveCompMap) {
    gfree(compMap.comp);
    gfree(compMap.type);
    gfree(compMap.pComp);
    haveCompMap = gFalse;
  }
  if (haveChannelDefn) {
    gfree(channelDefn.idx);
    gfree(channelDefn.type);
    gfree(channelDefn.assoc);
    haveChannelDefn = gFalse;
  }

  if (img.tiles) {
    for (i = 0; i < img.nXTiles * img.nYTiles; ++i) {
      tile = &img.tiles[i];
      if (tile->tileComps) {
	for (comp = 0; comp < img.nComps; ++comp) {
	  tileComp = &tile->tileComps[comp];
	  gfree(tileComp->quantSteps);
	  gfree(tileComp->data);
	  gfree(tileComp->buf);
	  if (tileComp->resLevels) {
	    for (r = 0; r <= tileComp->nDecompLevels; ++r) {
	      resLevel = &tileComp->resLevels[r];
	      if (resLevel->precincts) {
		for (pre = 0; pre < resLevel->nPrecincts; ++pre) {
		  precinct = &resLevel->precincts[pre];
		  if (precinct->subbands) {
		    for (sb = 0; sb < (Guint)(r == 0 ? 1 : 3); ++sb) {
		      subband = &precinct->subbands[sb];
		      gfree(subband->inclusion);
		      gfree(subband->zeroBitPlane);
		      if (subband->cbs) {
			for (k = 0; k < subband->nXCBs * subband->nYCBs; ++k) {
			  cb = &subband->cbs[k];
			  gfree(cb->dataLen);
			  gfree(cb->touched);
			  if (cb->arithDecoder) {
			    delete cb->arithDecoder;
			  }
			  if (cb->stats) {
			    delete cb->stats;
			  }
			}
			gfree(subband->cbs);
		      }
		    }
		    gfree(precinct->subbands);
		  }
		}
		gfree(resLevel->precincts);
	      }
	    }
	    gfree(tileComp->resLevels);
	  }
	}
	gfree(tile->tileComps);
      }
    }
    gfree(img.tiles);
    img.tiles = NULL;
  }
  bufStr->close();
}

void JPXStream::decodeImage() {
  if (readBoxes() == jpxDecodeFatalError) {
    // readBoxes reported an error, so we go immediately to EOF
    curY = img.ySize >> reduction;
  } else {
    curY = img.yOffsetR;
  }
  curX = img.xOffsetR;
  curComp = 0;
  readBufLen = 0;
  decoded = gTrue;
}

int JPXStream::getChar() {
  int c;

  if (!decoded) {
    decodeImage();
  }
  if (readBufLen < 8) {
    fillReadBuf();
  }
  if (readBufLen == 8) {
    c = readBuf & 0xff;
    readBufLen = 0;
  } else if (readBufLen > 8) {
    c = (readBuf >> (readBufLen - 8)) & 0xff;
    readBufLen -= 8;
  } else if (readBufLen == 0) {
    c = EOF;
  } else {
    c = (readBuf << (8 - readBufLen)) & 0xff;
    readBufLen = 0;
  }
  return c;
}

int JPXStream::lookChar() {
  int c;

  if (!decoded) {
    decodeImage();
  }
  if (readBufLen < 8) {
    fillReadBuf();
  }
  if (readBufLen == 8) {
    c = readBuf & 0xff;
  } else if (readBufLen > 8) {
    c = (readBuf >> (readBufLen - 8)) & 0xff;
  } else if (readBufLen == 0) {
    c = EOF;
  } else {
    c = (readBuf << (8 - readBufLen)) & 0xff;
  }
  return c;
}

void JPXStream::fillReadBuf() {
  JPXTileComp *tileComp;
  Guint tileIdx, tx, ty;
  int pix, pixBits, k;
  GBool eol;

  do {
    if (curY >= (img.ySize >> reduction)) {
      return;
    }
    tileIdx = (((curY << reduction) - img.yTileOffset) / img.yTileSize)
                * img.nXTiles
              + ((curX << reduction) - img.xTileOffset) / img.xTileSize;
#if 1 //~ ignore the palette, assume the PDF ColorSpace object is valid
    tileComp = &img.tiles[tileIdx].tileComps[curComp];
#else
    tileComp = &img.tiles[tileIdx].tileComps[havePalette ? 0 : curComp];
#endif
    tx = jpxFloorDiv(curX, tileComp->hSep);
    if (tx < tileComp->x0r) {
      tx = 0;
    } else {
      tx -= tileComp->x0r;
    }
    ty = jpxFloorDiv(curY, tileComp->vSep);
    if (ty < tileComp->y0r) {
      ty  = 0;
    } else {
      ty -= tileComp->y0r;
    }
    pix = (int)tileComp->data[ty * tileComp->w + tx];
    pixBits = tileComp->prec;
    eol = gFalse;
#if 1 //~ ignore the palette, assume the PDF ColorSpace object is valid
    if (++curComp == img.nComps) {
#else
    if (havePalette) {
      if (pix >= 0 && pix < palette.nEntries) {
	pix = palette.c[pix * palette.nComps + curComp];
      } else {
	pix = 0;
      }
      pixBits = palette.bpc[curComp];
    }
    if (++curComp == (Guint)(havePalette ? palette.nComps : img.nComps)) {
#endif
      curComp = 0;
      if (++curX == (img.xSize >> reduction)) {
	curX = img.xOffsetR;
	++curY;
	eol = gTrue;
      }
    }
    if (pixBits == 8) {
      readBuf = (readBuf << 8) | (pix & 0xff);
    } else {
      readBuf = (readBuf << pixBits) | (pix & ((1 << pixBits) - 1));
    }
    readBufLen += pixBits;
    if (eol && (k = readBufLen & 7)) {
      readBuf <<= 8 - k;
      readBufLen += 8 - k;
    }
  } while (readBufLen < 8);
}

GString *JPXStream::getPSFilter(int psLevel, const char *indent,
				GBool okToReadStream) {
  return NULL;
}

GBool JPXStream::isBinary(GBool last) {
  return str->isBinary(gTrue);
}

void JPXStream::getImageParams(int *bitsPerComponent,
			       StreamColorSpaceMode *csMode) {
  Guint boxType, boxLen, dataLen, csEnum;
  Guint bpc1, dummy;
  int csMeth, csPrec, csPrec1, dummy2;
  StreamColorSpaceMode csMode1;
  GBool haveBPC, haveCSMode;

  csPrec = 0; // make gcc happy
  haveBPC = haveCSMode = gFalse;
  bufStr->reset();
  if (bufStr->lookChar() == 0xff) {
    getImageParams2(bitsPerComponent, csMode);
  } else {
    while (readBoxHdr(&boxType, &boxLen, &dataLen)) {
      if (boxType == 0x6a703268) { // JP2 header
	cover(0);
	// skip the superbox
      } else if (boxType == 0x69686472) { // image header
	cover(1);
	if (readULong(&dummy) &&
	    readULong(&dummy) &&
	    readUWord(&dummy) &&
	    readUByte(&bpc1) &&
	    readUByte(&dummy) &&
	    readUByte(&dummy) &&
	    readUByte(&dummy)) {
	  *bitsPerComponent = bpc1 + 1;
	  haveBPC = gTrue;
	}
      } else if (boxType == 0x636F6C72) { // color specification
	cover(2);
	if (readByte(&csMeth) &&
	    readByte(&csPrec1) &&
	    readByte(&dummy2)) {
	  if (csMeth == 1) {
	    if (readULong(&csEnum)) {
	      csMode1 = streamCSNone;
	      if (csEnum == jpxCSBiLevel ||
		  csEnum == jpxCSGrayscale) {
		csMode1 = streamCSDeviceGray;
	      } else if (csEnum == jpxCSCMYK) {
		csMode1 = streamCSDeviceCMYK;
	      } else if (csEnum == jpxCSsRGB ||
			 csEnum == jpxCSCISesRGB ||
			 csEnum == jpxCSROMMRGB) {
		csMode1 = streamCSDeviceRGB;
	      }
	      if (csMode1 != streamCSNone &&
		  (!haveCSMode || csPrec1 > csPrec)) {
		*csMode = csMode1;
		csPrec = csPrec1;
		haveCSMode = gTrue;
	      }
	      if (dataLen > 7) {
		bufStr->discardChars(dataLen - 7);
	      }
	    }
	  } else {
	    if (dataLen > 3) {
	      bufStr->discardChars(dataLen - 3);
	    }
	  }
	}
      } else if (boxType == 0x6A703263) { // codestream
	cover(3);
	if (!(haveBPC && haveCSMode)) {
	  getImageParams2(bitsPerComponent, csMode);
	}
	break;
      } else {
	cover(4);
	bufStr->discardChars(dataLen);
      }
    }
  }
  bufStr->close();
}

// Get image parameters from the codestream.
void JPXStream::getImageParams2(int *bitsPerComponent,
				StreamColorSpaceMode *csMode) {
  int segType;
  Guint segLen, nComps1, bpc1, dummy;

  while (readMarkerHdr(&segType, &segLen)) {
    if (segType == 0x51) { // SIZ - image and tile size
      cover(5);
      if (readUWord(&dummy) &&
	  readULong(&dummy) &&
	  readULong(&dummy) &&
	  readULong(&dummy) &&
	  readULong(&dummy) &&
	  readULong(&dummy) &&
	  readULong(&dummy) &&
	  readULong(&dummy) &&
	  readULong(&dummy) &&
	  readUWord(&nComps1) &&
	  readUByte(&bpc1)) {
	*bitsPerComponent = (bpc1 & 0x7f) + 1;
	// if there's no color space info, take a guess
	if (nComps1 == 1) {
	  *csMode = streamCSDeviceGray;
	} else if (nComps1 == 3) {
	  *csMode = streamCSDeviceRGB;
	} else if (nComps1 == 4) {
	  *csMode = streamCSDeviceCMYK;
	}
      }
      break;
    } else {
      cover(6);
      if (segLen > 2) {
	bufStr->discardChars(segLen - 2);
      }
    }
  }
}

JPXDecodeResult JPXStream::readBoxes() {
  JPXDecodeResult result;
  GBool haveCodestream;
  Guint boxType, boxLen, dataLen;
  Guint w, h, n, bpc1, compression, unknownColorspace, ipr;
  Guint i, j;

  haveImgHdr = gFalse;
  haveCodestream = gFalse;

  // check for a naked JPEG 2000 codestream (without the JP2/JPX
  // wrapper) -- this appears to be a violation of the PDF spec, but
  // Acrobat allows it
  if (bufStr->lookChar() == 0xff) {
    cover(7);
    error(errSyntaxWarning, getPos(),
	  "Naked JPEG 2000 codestream, missing JP2/JPX wrapper");
    if ((result = readCodestream(0)) == jpxDecodeFatalError) {
      return result;
    }
    nComps = img.nComps;
    bpc = (Guint *)gmallocn(nComps, sizeof(Guint));
    for (i = 0; i < nComps; ++i) {
      bpc[i] = img.tiles[0].tileComps[i].prec;
    }
    width = img.xSize - img.xOffset;
    height = img.ySize - img.yOffset;
    return result;
  }

  while (readBoxHdr(&boxType, &boxLen, &dataLen)) {
    switch (boxType) {
    case 0x6a703268:		// JP2 header
      // this is a grouping box ('superbox') which has no real
      // contents and doesn't appear to be used consistently, i.e.,
      // some things which should be subboxes of the JP2 header box
      // show up outside of it - so we simply ignore the JP2 header
      // box
      cover(8);
      break;
    case 0x69686472:		// image header
      cover(9);
      if (!readULong(&h) ||
	  !readULong(&w) ||
	  !readUWord(&n) ||
	  !readUByte(&bpc1) ||
	  !readUByte(&compression) ||
	  !readUByte(&unknownColorspace) ||
	  !readUByte(&ipr)) {
	error(errSyntaxError, getPos(), "Unexpected EOF in JPX stream");
	return jpxDecodeFatalError;
      }
      if (compression != 7) {
	error(errSyntaxError, getPos(),
	      "Unknown compression type in JPX stream");
	return jpxDecodeFatalError;
      }
      height = h;
      width = w;
      nComps = n;
      bpc = (Guint *)gmallocn(nComps, sizeof(Guint));
      for (i = 0; i < nComps; ++i) {
	bpc[i] = bpc1;
      }
      haveImgHdr = gTrue;
      break;
    case 0x62706363:		// bits per component
      cover(10);
      if (!haveImgHdr) {
	error(errSyntaxError, getPos(),
	      "Found bits per component box before image header box in JPX stream");
	return jpxDecodeFatalError;
      }
      if (dataLen != nComps) {
	error(errSyntaxError, getPos(),
	      "Invalid bits per component box in JPX stream");
	return jpxDecodeFatalError;
      }
      for (i = 0; i < nComps; ++i) {
	if (!readUByte(&bpc[i])) {
	  error(errSyntaxError, getPos(), "Unexpected EOF in JPX stream");
	  return jpxDecodeFatalError;
	}
      }
      break;
    case 0x636F6C72:		// color specification
      cover(11);
      if (!readColorSpecBox(dataLen)) {
	return jpxDecodeFatalError;
      }
      break;
    case 0x70636c72:		// palette
      cover(12);
      if (!readUWord(&palette.nEntries) ||
	  !readUByte(&palette.nComps)) {
	error(errSyntaxError, getPos(), "Unexpected EOF in JPX stream");
	return jpxDecodeFatalError;
      }
      havePalette = gTrue;
      palette.bpc = (Guint *)gmallocn(palette.nComps, sizeof(Guint));
      palette.c =
          (int *)gmallocn(palette.nEntries * palette.nComps, sizeof(int));
      for (i = 0; i < palette.nComps; ++i) {
	if (!readUByte(&palette.bpc[i])) {
	  error(errSyntaxError, getPos(), "Unexpected EOF in JPX stream");
	  return jpxDecodeFatalError;
	}
	++palette.bpc[i];
      }
      for (i = 0; i < palette.nEntries; ++i) {
	for (j = 0; j < palette.nComps; ++j) {
	  if (!readNBytes(((palette.bpc[j] & 0x7f) + 7) >> 3,
			  (palette.bpc[j] & 0x80) ? gTrue : gFalse,
			  &palette.c[i * palette.nComps + j])) {
	    error(errSyntaxError, getPos(), "Unexpected EOF in JPX stream");
	    return jpxDecodeFatalError;
	  }
	}
      }
      break;
    case 0x636d6170:		// component mapping
      cover(13);
      haveCompMap = gTrue;
      compMap.nChannels = dataLen / 4;
      compMap.comp = (Guint *)gmallocn(compMap.nChannels, sizeof(Guint));
      compMap.type = (Guint *)gmallocn(compMap.nChannels, sizeof(Guint));
      compMap.pComp = (Guint *)gmallocn(compMap.nChannels, sizeof(Guint));
      for (i = 0; i < compMap.nChannels; ++i) {
	if (!readUWord(&compMap.comp[i]) ||
	    !readUByte(&compMap.type[i]) ||
	    !readUByte(&compMap.pComp[i])) {
	  error(errSyntaxError, getPos(), "Unexpected EOF in JPX stream");
	  return jpxDecodeFatalError;
	}
      }
      break;
    case 0x63646566:		// channel definition
      cover(14);
      if (!readUWord(&channelDefn.nChannels)) {
	error(errSyntaxError, getPos(), "Unexpected EOF in JPX stream");
	return jpxDecodeFatalError;
      }
      haveChannelDefn = gTrue;
      channelDefn.idx =
	  (Guint *)gmallocn(channelDefn.nChannels, sizeof(Guint));
      channelDefn.type =
	  (Guint *)gmallocn(channelDefn.nChannels, sizeof(Guint));
      channelDefn.assoc =
	  (Guint *)gmallocn(channelDefn.nChannels, sizeof(Guint));
      for (i = 0; i < channelDefn.nChannels; ++i) {
	if (!readUWord(&channelDefn.idx[i]) ||
	    !readUWord(&channelDefn.type[i]) ||
	    !readUWord(&channelDefn.assoc[i])) {
	  error(errSyntaxError, getPos(), "Unexpected EOF in JPX stream");
	  return jpxDecodeFatalError;
	}
      }
      break;
    case 0x6A703263:		// contiguous codestream
      cover(15);
      if (!bpc) {
	error(errSyntaxError, getPos(),
	      "JPX stream is missing the image header box");
      }
      if (!haveCS) {
	error(errSyntaxError, getPos(),
	      "JPX stream has no supported color spec");
      }
      if ((result = readCodestream(dataLen)) != jpxDecodeOk) {
	return result;
      }
      haveCodestream = gTrue;
      break;
    default:
      cover(16);
      if (bufStr->discardChars(dataLen) != dataLen) {
	error(errSyntaxError, getPos(), "Unexpected EOF in JPX stream");
	return haveCodestream ? jpxDecodeNonFatalError : jpxDecodeFatalError;
      }
      break;
    }
  }
  return jpxDecodeOk;
}

GBool JPXStream::readColorSpecBox(Guint dataLen) {
  JPXColorSpec newCS;
  Guint csApprox, csEnum;
  GBool ok;

  ok = gFalse;
  if (!readUByte(&newCS.meth) ||
      !readByte(&newCS.prec) ||
      !readUByte(&csApprox)) {
    goto err;
  }
  switch (newCS.meth) {
  case 1:			// enumerated colorspace
    cover(17);
    if (!readULong(&csEnum)) {
      goto err;
    }
    newCS.enumerated.type = (JPXColorSpaceType)csEnum;
    switch (newCS.enumerated.type) {
    case jpxCSBiLevel:
      ok = gTrue;
      break;
    case jpxCSYCbCr1:
      ok = gTrue;
      break;
    case jpxCSYCbCr2:
      ok = gTrue;
      break;
    case jpxCSYCBCr3:
      ok = gTrue;
      break;
    case jpxCSPhotoYCC:
      ok = gTrue;
      break;
    case jpxCSCMY:
      ok = gTrue;
      break;
    case jpxCSCMYK:
      ok = gTrue;
      break;
    case jpxCSYCCK:
      ok = gTrue;
      break;
    case jpxCSCIELab:
      if (dataLen == 7 + 7*4) {
	if (!readULong(&newCS.enumerated.cieLab.rl) ||
	    !readULong(&newCS.enumerated.cieLab.ol) ||
	    !readULong(&newCS.enumerated.cieLab.ra) ||
	    !readULong(&newCS.enumerated.cieLab.oa) ||
	    !readULong(&newCS.enumerated.cieLab.rb) ||
	    !readULong(&newCS.enumerated.cieLab.ob) ||
	    !readULong(&newCS.enumerated.cieLab.il)) {
	  goto err;
	}
      } else if (dataLen == 7) {
	//~ this assumes the 8-bit case
	cover(92);
	newCS.enumerated.cieLab.rl = 100;
	newCS.enumerated.cieLab.ol = 0;
	newCS.enumerated.cieLab.ra = 255;
	newCS.enumerated.cieLab.oa = 128;
	newCS.enumerated.cieLab.rb = 255;
	newCS.enumerated.cieLab.ob = 96;
	newCS.enumerated.cieLab.il = 0x00443530;
      } else {
	goto err;
      }
      ok = gTrue;
      break;
    case jpxCSsRGB:
      ok = gTrue;
      break;
    case jpxCSGrayscale:
      ok = gTrue;
      break;
    case jpxCSBiLevel2:
      ok = gTrue;
      break;
    case jpxCSCIEJab:
      // not allowed in PDF
      goto err;
    case jpxCSCISesRGB:
      ok = gTrue;
      break;
    case jpxCSROMMRGB:
      ok = gTrue;
      break;
    case jpxCSsRGBYCbCr:
      ok = gTrue;
      break;
    case jpxCSYPbPr1125:
      ok = gTrue;
      break;
    case jpxCSYPbPr1250:
      ok = gTrue;
      break;
    default:
      goto err;
    }
    break;
  case 2:			// restricted ICC profile
  case 3: 			// any ICC profile (JPX)
  case 4:			// vendor color (JPX)
    cover(18);
    if (dataLen > 3 &&
	bufStr->discardChars(dataLen - 3) != dataLen - 3) {
      goto err;
    }
    break;
  }

  if (ok && (!haveCS || newCS.prec > cs.prec)) {
    cs = newCS;
    haveCS = gTrue;
  }

  return gTrue;

 err:
  error(errSyntaxError, getPos(), "Error in JPX color spec");
  return gFalse;
}

JPXDecodeResult JPXStream::readCodestream(Guint len) {
  JPXTile *tile;
  JPXTileComp *tileComp;
  int segType;
  GBool haveSIZ, haveCOD, haveQCD, haveSOT, ok;
  Guint style, progOrder, nLayers, multiComp, nDecompLevels;
  Guint codeBlockW, codeBlockH, codeBlockStyle, transform;
  Guint precinctSize;
  Guint segLen, capabilities, comp, i, j, r;

  //----- main header
  haveSIZ = haveCOD = haveQCD = haveSOT = gFalse;
  do {
    if (!readMarkerHdr(&segType, &segLen)) {
      error(errSyntaxError, getPos(), "Error in JPX codestream");
      return jpxDecodeFatalError;
    }
    switch (segType) {
    case 0x4f:			// SOC - start of codestream
      // marker only
      cover(19);
      break;
    case 0x51:			// SIZ - image and tile size
      cover(20);
      if (haveSIZ) {
	error(errSyntaxError, getPos(),
	      "Duplicate SIZ marker segment in JPX stream");
	return jpxDecodeFatalError;
      }
      if (!readUWord(&capabilities) ||
	  !readULong(&img.xSize) ||
	  !readULong(&img.ySize) ||
	  !readULong(&img.xOffset) ||
	  !readULong(&img.yOffset) ||
	  !readULong(&img.xTileSize) ||
	  !readULong(&img.yTileSize) ||
	  !readULong(&img.xTileOffset) ||
	  !readULong(&img.yTileOffset) ||
	  !readUWord(&img.nComps)) {
	error(errSyntaxError, getPos(), "Error in JPX SIZ marker segment");
	return jpxDecodeFatalError;
      }
      if (haveImgHdr && img.nComps != nComps) {
	error(errSyntaxError, getPos(),
	      "Different number of components in JPX SIZ marker segment");
	return jpxDecodeFatalError;
      }
      if (img.xSize == 0 || img.ySize == 0 ||
	  img.xOffset >= img.xSize || img.yOffset >= img.ySize ||
	  img.xTileSize == 0 || img.yTileSize == 0 ||
	  img.xTileOffset > img.xOffset ||
	  img.yTileOffset > img.yOffset ||
	  img.xTileSize + img.xTileOffset <= img.xOffset ||
	  img.yTileSize + img.yTileOffset <= img.yOffset ||
	  img.nComps == 0) {
	error(errSyntaxError, getPos(), "Error in JPX SIZ marker segment");
	return jpxDecodeFatalError;
      }
      img.xSizeR = jpxCeilDivPow2(img.xSize, reduction);
      img.ySizeR = jpxCeilDivPow2(img.ySize, reduction);
      img.xOffsetR = jpxCeilDivPow2(img.xOffset, reduction);
      img.yOffsetR = jpxCeilDivPow2(img.yOffset, reduction);
      img.nXTiles = (img.xSize - img.xTileOffset + img.xTileSize - 1)
	            / img.xTileSize;
      img.nYTiles = (img.ySize - img.yTileOffset + img.yTileSize - 1)
	            / img.yTileSize;
      // check for overflow before allocating memory
      if (img.nXTiles <= 0 || img.nYTiles <= 0 ||
	  img.nXTiles >= INT_MAX / img.nYTiles) {
	error(errSyntaxError, getPos(),
	      "Bad tile count in JPX SIZ marker segment");
	return jpxDecodeFatalError;
      }
      img.tiles = (JPXTile *)gmallocn(img.nXTiles * img.nYTiles,
				      sizeof(JPXTile));
      for (i = 0; i < img.nXTiles * img.nYTiles; ++i) {
	img.tiles[i].init = gFalse;
	img.tiles[i].nextTilePart = 0;
	img.tiles[i].tileComps = NULL;
      }
      for (i = 0; i < img.nXTiles * img.nYTiles; ++i) {
	img.tiles[i].tileComps = (JPXTileComp *)gmallocn(img.nComps,
							 sizeof(JPXTileComp));
	for (comp = 0; comp < img.nComps; ++comp) {
	  img.tiles[i].tileComps[comp].quantSteps = NULL;
	  img.tiles[i].tileComps[comp].data = NULL;
	  img.tiles[i].tileComps[comp].buf = NULL;
	  img.tiles[i].tileComps[comp].resLevels = NULL;
	}
      }
      for (comp = 0; comp < img.nComps; ++comp) {
	if (!readUByte(&img.tiles[0].tileComps[comp].prec) ||
	    !readUByte(&img.tiles[0].tileComps[comp].hSep) ||
	    !readUByte(&img.tiles[0].tileComps[comp].vSep)) {
	  error(errSyntaxError, getPos(), "Error in JPX SIZ marker segment");
	  return jpxDecodeFatalError;
	}
	if (img.tiles[0].tileComps[comp].hSep == 0 ||
	    img.tiles[0].tileComps[comp].vSep == 0) {
	  error(errSyntaxError, getPos(), "Error in JPX SIZ marker segment");
	  return jpxDecodeFatalError;
	}
	img.tiles[0].tileComps[comp].sgned =
	    (img.tiles[0].tileComps[comp].prec & 0x80) ? gTrue : gFalse;
	img.tiles[0].tileComps[comp].prec =
	    (img.tiles[0].tileComps[comp].prec & 0x7f) + 1;
	for (i = 1; i < img.nXTiles * img.nYTiles; ++i) {
	  img.tiles[i].tileComps[comp] = img.tiles[0].tileComps[comp];
	}
      }
      haveSIZ = gTrue;
      break;
    case 0x52:			// COD - coding style default
      cover(21);
      if (!haveSIZ) {
	error(errSyntaxError, getPos(),
	      "JPX COD marker segment before SIZ segment");
	return jpxDecodeFatalError;
      }
      if (!readUByte(&style) ||
	  !readUByte(&progOrder) ||
	  !readUWord(&nLayers) ||
	  !readUByte(&multiComp) ||
	  !readUByte(&nDecompLevels) ||
	  !readUByte(&codeBlockW) ||
	  !readUByte(&codeBlockH) ||
	  !readUByte(&codeBlockStyle) ||
	  !readUByte(&transform)) {
	error(errSyntaxError, getPos(), "Error in JPX COD marker segment");
	return jpxDecodeFatalError;
      }
      if (nDecompLevels < 1 ||
	  nDecompLevels > 31 ||
	  codeBlockW > 8 ||
	  codeBlockH > 8) {
	error(errSyntaxError, getPos(), "Error in JPX COD marker segment");
	return jpxDecodeFatalError;
      }
      codeBlockW += 2;
      codeBlockH += 2;
      for (i = 0; i < img.nXTiles * img.nYTiles; ++i) {
	img.tiles[i].progOrder = progOrder;
	img.tiles[i].nLayers = nLayers;
	img.tiles[i].multiComp = multiComp;
	for (comp = 0; comp < img.nComps; ++comp) {
	  img.tiles[i].tileComps[comp].style = style;
	  img.tiles[i].tileComps[comp].nDecompLevels = nDecompLevels;
	  img.tiles[i].tileComps[comp].codeBlockW = codeBlockW;
	  img.tiles[i].tileComps[comp].codeBlockH = codeBlockH;
	  img.tiles[i].tileComps[comp].codeBlockStyle = codeBlockStyle;
	  img.tiles[i].tileComps[comp].transform = transform;
	  img.tiles[i].tileComps[comp].resLevels =
	      (JPXResLevel *)gmallocn(nDecompLevels + 1, sizeof(JPXResLevel));
	  for (r = 0; r <= nDecompLevels; ++r) {
	    img.tiles[i].tileComps[comp].resLevels[r].precincts = NULL;
	  }
	}
      }
      for (r = 0; r <= nDecompLevels; ++r) {
	if (style & 0x01) {
	  cover(91);
	  if (!readUByte(&precinctSize)) {
	    error(errSyntaxError, getPos(), "Error in JPX COD marker segment");
	    return jpxDecodeFatalError;
	  }
	  if (r > 0 && ((precinctSize & 0x0f) == 0 ||
			(precinctSize & 0xf0) == 0)) {
	    error(errSyntaxError, getPos(),
		  "Invalid precinct size in JPX COD marker segment");
	    return jpxDecodeFatalError;
	  }
	  img.tiles[0].tileComps[0].resLevels[r].precinctWidth =
	      precinctSize & 0x0f;
	  img.tiles[0].tileComps[0].resLevels[r].precinctHeight =
	      (precinctSize >> 4) & 0x0f;
	} else {
	  img.tiles[0].tileComps[0].resLevels[r].precinctWidth = 15;
	  img.tiles[0].tileComps[0].resLevels[r].precinctHeight = 15;
	}
      }
      for (i = 0; i < img.nXTiles * img.nYTiles; ++i) {
	for (comp = 0; comp < img.nComps; ++comp) {
	  if (!(i == 0 && comp == 0)) {
	    for (r = 0; r <= nDecompLevels; ++r) {
	      img.tiles[i].tileComps[comp].resLevels[r].precinctWidth =
		  img.tiles[0].tileComps[0].resLevels[r].precinctWidth;
	      img.tiles[i].tileComps[comp].resLevels[r].precinctHeight =
		  img.tiles[0].tileComps[0].resLevels[r].precinctHeight;
	    }
	  }
	}
      }
      haveCOD = gTrue;
      break;
    case 0x53:			// COC - coding style component
      cover(22);
      if (!haveCOD) {
	error(errSyntaxError, getPos(),
	      "JPX COC marker segment before COD segment");
	return jpxDecodeFatalError;
      }
      if ((img.nComps > 256 && !readUWord(&comp)) ||
	  (img.nComps <= 256 && !readUByte(&comp)) ||
	  comp >= img.nComps ||
	  !readUByte(&style) ||
	  !readUByte(&nDecompLevels) ||
	  !readUByte(&codeBlockW) ||
	  !readUByte(&codeBlockH) ||
	  !readUByte(&codeBlockStyle) ||
	  !readUByte(&transform)) {
	error(errSyntaxError, getPos(), "Error in JPX COC marker segment");
	return jpxDecodeFatalError;
      }
      if (nDecompLevels < 1 ||
	  nDecompLevels > 31 ||
	  codeBlockW > 8 ||
	  codeBlockH > 8) {
	error(errSyntaxError, getPos(), "Error in JPX COC marker segment");
	return jpxDecodeFatalError;
      }
      style = (img.tiles[0].tileComps[comp].style & ~1) | (style & 1);
      codeBlockW += 2;
      codeBlockH += 2;
      for (i = 0; i < img.nXTiles * img.nYTiles; ++i) {
	img.tiles[i].tileComps[comp].style = style;
	img.tiles[i].tileComps[comp].nDecompLevels = nDecompLevels;
	img.tiles[i].tileComps[comp].codeBlockW = codeBlockW;
	img.tiles[i].tileComps[comp].codeBlockH = codeBlockH;
	img.tiles[i].tileComps[comp].codeBlockStyle = codeBlockStyle;
	img.tiles[i].tileComps[comp].transform = transform;
	img.tiles[i].tileComps[comp].resLevels =
	    (JPXResLevel *)greallocn(
		     img.tiles[i].tileComps[comp].resLevels,
		     nDecompLevels + 1,
		     sizeof(JPXResLevel));
	for (r = 0; r <= nDecompLevels; ++r) {
	  img.tiles[i].tileComps[comp].resLevels[r].precincts = NULL;
	}
      }
      for (r = 0; r <= nDecompLevels; ++r) {
	if (style & 0x01) {
	  if (!readUByte(&precinctSize)) {
	    error(errSyntaxError, getPos(), "Error in JPX COD marker segment");
	    return jpxDecodeFatalError;
	  }
	  if (r > 0 && ((precinctSize & 0x0f) == 0 ||
			(precinctSize & 0xf0) == 0)) {
	    error(errSyntaxError, getPos(),
		  "Invalid precinct size in JPX COD marker segment");
	    return jpxDecodeFatalError;
	  }
	  img.tiles[0].tileComps[comp].resLevels[r].precinctWidth =
	      precinctSize & 0x0f;
	  img.tiles[0].tileComps[comp].resLevels[r].precinctHeight =
	      (precinctSize >> 4) & 0x0f;
	} else {
	  img.tiles[0].tileComps[comp].resLevels[r].precinctWidth = 15;
	  img.tiles[0].tileComps[comp].resLevels[r].precinctHeight = 15;
	}
      }
      for (i = 1; i < img.nXTiles * img.nYTiles; ++i) {
	for (r = 0; r <= img.tiles[i].tileComps[comp].nDecompLevels; ++r) {
	  img.tiles[i].tileComps[comp].resLevels[r].precinctWidth =
	      img.tiles[0].tileComps[comp].resLevels[r].precinctWidth;
	  img.tiles[i].tileComps[comp].resLevels[r].precinctHeight =
	      img.tiles[0].tileComps[comp].resLevels[r].precinctHeight;
	}
      }
      break;
    case 0x5c:			// QCD - quantization default
      cover(23);
      if (!haveSIZ) {
	error(errSyntaxError, getPos(),
	      "JPX QCD marker segment before SIZ segment");
	return jpxDecodeFatalError;
      }
      if (!readUByte(&img.tiles[0].tileComps[0].quantStyle)) {
	error(errSyntaxError, getPos(), "Error in JPX QCD marker segment");
	return jpxDecodeFatalError;
      }
      if ((img.tiles[0].tileComps[0].quantStyle & 0x1f) == 0x00) {
	if (segLen <= 3) {
	  error(errSyntaxError, getPos(), "Error in JPX QCD marker segment");
	  return jpxDecodeFatalError;
	}
	img.tiles[0].tileComps[0].nQuantSteps = segLen - 3;
	img.tiles[0].tileComps[0].quantSteps =
	    (Guint *)greallocn(img.tiles[0].tileComps[0].quantSteps,
			       img.tiles[0].tileComps[0].nQuantSteps,
			       sizeof(Guint));
	for (i = 0; i < img.tiles[0].tileComps[0].nQuantSteps; ++i) {
	  if (!readUByte(&img.tiles[0].tileComps[0].quantSteps[i])) {
	    error(errSyntaxError, getPos(), "Error in JPX QCD marker segment");
	    return jpxDecodeFatalError;
	  }
	}
      } else if ((img.tiles[0].tileComps[0].quantStyle & 0x1f) == 0x01) {
	img.tiles[0].tileComps[0].nQuantSteps = 1;
	img.tiles[0].tileComps[0].quantSteps =
	    (Guint *)greallocn(img.tiles[0].tileComps[0].quantSteps,
			       img.tiles[0].tileComps[0].nQuantSteps,
			       sizeof(Guint));
	if (!readUWord(&img.tiles[0].tileComps[0].quantSteps[0])) {
	  error(errSyntaxError, getPos(), "Error in JPX QCD marker segment");
	  return jpxDecodeFatalError;
	}
      } else if ((img.tiles[0].tileComps[0].quantStyle & 0x1f) == 0x02) {
	if (segLen < 5) {
	  error(errSyntaxError, getPos(), "Error in JPX QCD marker segment");
	  return jpxDecodeFatalError;
	}
	img.tiles[0].tileComps[0].nQuantSteps = (segLen - 3) / 2;
	img.tiles[0].tileComps[0].quantSteps =
	    (Guint *)greallocn(img.tiles[0].tileComps[0].quantSteps,
			       img.tiles[0].tileComps[0].nQuantSteps,
			       sizeof(Guint));
	for (i = 0; i < img.tiles[0].tileComps[0].nQuantSteps; ++i) {
	  if (!readUWord(&img.tiles[0].tileComps[0].quantSteps[i])) {
	    error(errSyntaxError, getPos(), "Error in JPX QCD marker segment");
	    return jpxDecodeFatalError;
	  }
	}
      } else {
	error(errSyntaxError, getPos(), "Error in JPX QCD marker segment");
	return jpxDecodeFatalError;
      }
      for (i = 0; i < img.nXTiles * img.nYTiles; ++i) {
	for (comp = 0; comp < img.nComps; ++comp) {
	  if (!(i == 0 && comp == 0)) {
	    img.tiles[i].tileComps[comp].quantStyle =
	        img.tiles[0].tileComps[0].quantStyle;
	    img.tiles[i].tileComps[comp].nQuantSteps =
	        img.tiles[0].tileComps[0].nQuantSteps;
	    img.tiles[i].tileComps[comp].quantSteps = 
	        (Guint *)greallocn(img.tiles[i].tileComps[comp].quantSteps,
				   img.tiles[0].tileComps[0].nQuantSteps,
				   sizeof(Guint));
	    for (j = 0; j < img.tiles[0].tileComps[0].nQuantSteps; ++j) {
	      img.tiles[i].tileComps[comp].quantSteps[j] =
		  img.tiles[0].tileComps[0].quantSteps[j];
	    }
	  }
	}
      }
      haveQCD = gTrue;
      break;
    case 0x5d:			// QCC - quantization component
      cover(24);
      if (!haveQCD) {
	error(errSyntaxError, getPos(),
	      "JPX QCC marker segment before QCD segment");
	return jpxDecodeFatalError;
      }
      if ((img.nComps > 256 && !readUWord(&comp)) ||
	  (img.nComps <= 256 && !readUByte(&comp)) ||
	  comp >= img.nComps ||
	  !readUByte(&img.tiles[0].tileComps[comp].quantStyle)) {
	error(errSyntaxError, getPos(), "Error in JPX QCC marker segment");
	return jpxDecodeFatalError;
      }
      if ((img.tiles[0].tileComps[comp].quantStyle & 0x1f) == 0x00) {
	if (segLen <= (img.nComps > 256 ? 5U : 4U)) {
	  error(errSyntaxError, getPos(), "Error in JPX QCC marker segment");
	  return jpxDecodeFatalError;
	}
	img.tiles[0].tileComps[comp].nQuantSteps =
	    segLen - (img.nComps > 256 ? 5 : 4);
	img.tiles[0].tileComps[comp].quantSteps =
	    (Guint *)greallocn(img.tiles[0].tileComps[comp].quantSteps,
			       img.tiles[0].tileComps[comp].nQuantSteps,
			       sizeof(Guint));
	for (i = 0; i < img.tiles[0].tileComps[comp].nQuantSteps; ++i) {
	  if (!readUByte(&img.tiles[0].tileComps[comp].quantSteps[i])) {
	    error(errSyntaxError, getPos(), "Error in JPX QCC marker segment");
	    return jpxDecodeFatalError;
	  }
	}
      } else if ((img.tiles[0].tileComps[comp].quantStyle & 0x1f) == 0x01) {
	img.tiles[0].tileComps[comp].nQuantSteps = 1;
	img.tiles[0].tileComps[comp].quantSteps =
	    (Guint *)greallocn(img.tiles[0].tileComps[comp].quantSteps,
			       img.tiles[0].tileComps[comp].nQuantSteps,
			       sizeof(Guint));
	if (!readUWord(&img.tiles[0].tileComps[comp].quantSteps[0])) {
	  error(errSyntaxError, getPos(), "Error in JPX QCC marker segment");
	  return jpxDecodeFatalError;
	}
      } else if ((img.tiles[0].tileComps[comp].quantStyle & 0x1f) == 0x02) {
	if (segLen < (img.nComps > 256 ? 5U : 4U) + 2) {
	  error(errSyntaxError, getPos(), "Error in JPX QCC marker segment");
	  return jpxDecodeFatalError;
	}
	img.tiles[0].tileComps[comp].nQuantSteps =
	    (segLen - (img.nComps > 256 ? 5 : 4)) / 2;
	img.tiles[0].tileComps[comp].quantSteps =
	    (Guint *)greallocn(img.tiles[0].tileComps[comp].quantSteps,
			       img.tiles[0].tileComps[comp].nQuantSteps,
			       sizeof(Guint));
	for (i = 0; i < img.tiles[0].tileComps[comp].nQuantSteps; ++i) {
	  if (!readUWord(&img.tiles[0].tileComps[comp].quantSteps[i])) {
	    error(errSyntaxError, getPos(), "Error in JPX QCD marker segment");
	    return jpxDecodeFatalError;
	  }
	}
      } else {
	error(errSyntaxError, getPos(), "Error in JPX QCC marker segment");
	return jpxDecodeFatalError;
      }
      for (i = 1; i < img.nXTiles * img.nYTiles; ++i) {
	img.tiles[i].tileComps[comp].quantStyle =
	    img.tiles[0].tileComps[comp].quantStyle;
	img.tiles[i].tileComps[comp].nQuantSteps =
	    img.tiles[0].tileComps[comp].nQuantSteps;
	img.tiles[i].tileComps[comp].quantSteps = 
	    (Guint *)greallocn(img.tiles[i].tileComps[comp].quantSteps,
			       img.tiles[0].tileComps[comp].nQuantSteps,
			       sizeof(Guint));
	for (j = 0; j < img.tiles[0].tileComps[comp].nQuantSteps; ++j) {
	  img.tiles[i].tileComps[comp].quantSteps[j] =
	      img.tiles[0].tileComps[comp].quantSteps[j];
	}
      }
      break;
    case 0x5e:			// RGN - region of interest
      cover(25);
#if 1 //~ ROI is unimplemented
      error(errUnimplemented, -1, "got a JPX RGN segment");
      if (segLen > 2 &&
	  bufStr->discardChars(segLen - 2) != segLen - 2) {
	error(errSyntaxError, getPos(), "Error in JPX RGN marker segment");
	return jpxDecodeFatalError;
      }
#else
      if ((img.nComps > 256 && !readUWord(&comp)) ||
	  (img.nComps <= 256 && !readUByte(&comp)) ||
	  comp >= img.nComps ||
	  !readUByte(&compInfo[comp].defROI.style) ||
	  !readUByte(&compInfo[comp].defROI.shift)) {
	error(errSyntaxError, getPos(), "Error in JPX RGN marker segment");
	return jpxDecodeFatalError;
      }
#endif
      break;
    case 0x5f:			// POC - progression order change
      cover(26);
#if 1 //~ progression order changes are unimplemented
      error(errUnimplemented, -1, "got a JPX POC segment");
      if (segLen > 2 &&
	  bufStr->discardChars(segLen - 2) != segLen - 2) {
	error(errSyntaxError, getPos(), "Error in JPX POC marker segment");
	return jpxDecodeFatalError;
      }
#else
      nProgs = (segLen - 2) / (img.nComps > 256 ? 9 : 7);
      progs = (JPXProgOrder *)gmallocn(nProgs, sizeof(JPXProgOrder));
      for (i = 0; i < nProgs; ++i) {
	if (!readUByte(&progs[i].startRes) ||
	    !(img.nComps > 256 && readUWord(&progs[i].startComp)) ||
	    !(img.nComps <= 256 && readUByte(&progs[i].startComp)) ||
	    !readUWord(&progs[i].endLayer) ||
	    !readUByte(&progs[i].endRes) ||
	    !(img.nComps > 256 && readUWord(&progs[i].endComp)) ||
	    !(img.nComps <= 256 && readUByte(&progs[i].endComp)) ||
	    !readUByte(&progs[i].progOrder)) {
	  error(errSyntaxError, getPos(), "Error in JPX POC marker segment");
	  return jpxDecodeFatalError;
	}
      }
#endif
      break;
    case 0x60:			// PPM - packed packet headers, main header
      cover(27);
#if 1 //~ packed packet headers are unimplemented
      error(errUnimplemented, -1, "Got a JPX PPM segment");
      if (segLen > 2 &&
	  bufStr->discardChars(segLen - 2) != segLen - 2) {
	error(errSyntaxError, getPos(), "Error in JPX PPM marker segment");
	return jpxDecodeFatalError;
      }
#endif
      break;
    case 0x55:			// TLM - tile-part lengths
      // skipped
      cover(28);
      if (segLen > 2 &&
	  bufStr->discardChars(segLen - 2) != segLen - 2) {
	error(errSyntaxError, getPos(), "Error in JPX TLM marker segment");
	return jpxDecodeFatalError;
      }
      break;
    case 0x57:			// PLM - packet length, main header
      // skipped
      cover(29);
      if (segLen > 2 &&
	  bufStr->discardChars(segLen - 2) != segLen - 2) {
	error(errSyntaxError, getPos(), "Error in JPX PLM marker segment");
	return jpxDecodeFatalError;
      }
      break;
    case 0x63:			// CRG - component registration
      // skipped
      cover(30);
      if (segLen > 2 &&
	  bufStr->discardChars(segLen - 2) != segLen - 2) {
	error(errSyntaxError, getPos(), "Error in JPX CRG marker segment");
	return jpxDecodeFatalError;
      }
      break;
    case 0x64:			// COM - comment
      // skipped
      cover(31);
      if (segLen > 2 &&
	  bufStr->discardChars(segLen - 2) != segLen - 2) {
	error(errSyntaxError, getPos(), "Error in JPX COM marker segment");
	return jpxDecodeFatalError;
      }
      break;
    case 0x90:			// SOT - start of tile
      cover(32);
      haveSOT = gTrue;
      break;
    default:
      cover(33);
      error(errSyntaxError, getPos(),
	    "Unknown marker segment {0:02x} in JPX stream", segType);
      if (segLen > 2) {
	bufStr->discardChars(segLen - 2);
      }
      break;
    }
  } while (!haveSOT);

  if (!haveSIZ) {
    error(errSyntaxError, getPos(),
	  "Missing SIZ marker segment in JPX stream");
    return jpxDecodeFatalError;
  }
  if (!haveCOD) {
    error(errSyntaxError, getPos(),
	  "Missing COD marker segment in JPX stream");
    return jpxDecodeFatalError;
  }
  if (!haveQCD) {
    error(errSyntaxError, getPos(),
	  "Missing QCD marker segment in JPX stream");
    return jpxDecodeFatalError;
  }

  //----- read the tile-parts
  ok = gTrue;
  while (1) {
    if (!readTilePart()) {
      ok = gFalse;
      break;
    }
    if (!readMarkerHdr(&segType, &segLen)) {
      error(errSyntaxError, getPos(), "Error in JPX codestream");
      ok = gFalse;
      break;
    }
    if (segType != 0x90) {	// SOT - start of tile
      break;
    }
  }

  if (segType != 0xd9) {	// EOC - end of codestream
    error(errSyntaxError, getPos(), "Missing EOC marker in JPX codestream");
    ok = gFalse;
  }

  //----- finish decoding the image
  for (i = 0; i < img.nXTiles * img.nYTiles; ++i) {
    tile = &img.tiles[i];
    if (!tile->init) {
      error(errSyntaxError, getPos(), "Uninitialized tile in JPX codestream");
      return jpxDecodeFatalError;
    }
    for (comp = 0; comp < img.nComps; ++comp) {
      tileComp = &tile->tileComps[comp];
      inverseTransform(tileComp);
    }
    if (!inverseMultiCompAndDC(tile)) {
      return jpxDecodeFatalError;
    }
  }

  //~ can free memory below tileComps here, and also tileComp.buf

  return ok ? jpxDecodeOk : jpxDecodeNonFatalError;
}

GBool JPXStream::readTilePart() {
  JPXTile *tile;
  JPXTileComp *tileComp;
  JPXResLevel *resLevel;
  JPXPrecinct *precinct;
  JPXSubband *subband;
  JPXCodeBlock *cb;
  int *sbCoeffs;
  GBool haveSOD;
  Guint tileIdx, tilePartLen, tilePartIdx, nTileParts;
  GBool tilePartToEOC;
  Guint style, progOrder, nLayers, multiComp, nDecompLevels;
  Guint codeBlockW, codeBlockH, codeBlockStyle, transform;
  Guint precinctSize, qStyle;
  Guint px0, py0, px1, py1;
  Guint preCol0, preCol1, preRow0, preRow1, preCol, preRow;
  Guint cbCol0, cbCol1, cbRow0, cbRow1, cbX, cbY;
  Guint n, nSBs, nx, ny, comp, segLen;
  Guint i, j, k, r, pre, sb, cbi, cbj;
  int segType, level;

  // process the SOT marker segment
  if (!readUWord(&tileIdx) ||
      !readULong(&tilePartLen) ||
      !readUByte(&tilePartIdx) ||
      !readUByte(&nTileParts)) {
    error(errSyntaxError, getPos(), "Error in JPX SOT marker segment");
    return gFalse;
  }

  // check tileIdx and tilePartIdx
  // (this ignores nTileParts, because some encoders get it wrong)
  if (tileIdx >= img.nXTiles * img.nYTiles ||
      tilePartIdx != img.tiles[tileIdx].nextTilePart ||
      (tilePartIdx > 0 && !img.tiles[tileIdx].init) ||
      (tilePartIdx == 0 && img.tiles[tileIdx].init)) {
    error(errSyntaxError, getPos(), "Weird tile-part header in JPX stream");
    return gFalse;
  }
  ++img.tiles[tileIdx].nextTilePart;

  tilePartToEOC = tilePartLen == 0;
  tilePartLen -= 12; // subtract size of SOT segment

  haveSOD = gFalse;
  do {
    if (!readMarkerHdr(&segType, &segLen)) {
      error(errSyntaxError, getPos(), "Error in JPX tile-part codestream");
      return gFalse;
    }
    tilePartLen -= 2 + segLen;
    switch (segType) {
    case 0x52:			// COD - coding style default
      cover(34);
      if (tilePartIdx != 0) {
	error(errSyntaxError, getPos(), "Extraneous JPX COD marker segment");
	return gFalse;
      }
      if (!readUByte(&style) ||
	  !readUByte(&progOrder) ||
	  !readUWord(&nLayers) ||
	  !readUByte(&multiComp) ||
	  !readUByte(&nDecompLevels) ||
	  !readUByte(&codeBlockW) ||
	  !readUByte(&codeBlockH) ||
	  !readUByte(&codeBlockStyle) ||
	  !readUByte(&transform)) {
	error(errSyntaxError, getPos(), "Error in JPX COD marker segment");
	return gFalse;
      }
      if (nDecompLevels < 1 ||
	  nDecompLevels > 31 ||
	  codeBlockW > 8 ||
	  codeBlockH > 8) {
	error(errSyntaxError, getPos(), "Error in JPX COD marker segment");
	return gFalse;
      }
      codeBlockW += 2;
      codeBlockH += 2;
      img.tiles[tileIdx].progOrder = progOrder;
      img.tiles[tileIdx].nLayers = nLayers;
      img.tiles[tileIdx].multiComp = multiComp;
      for (comp = 0; comp < img.nComps; ++comp) {
	img.tiles[tileIdx].tileComps[comp].style = style;
	img.tiles[tileIdx].tileComps[comp].nDecompLevels = nDecompLevels;
	img.tiles[tileIdx].tileComps[comp].codeBlockW = codeBlockW;
	img.tiles[tileIdx].tileComps[comp].codeBlockH = codeBlockH;
	img.tiles[tileIdx].tileComps[comp].codeBlockStyle = codeBlockStyle;
	img.tiles[tileIdx].tileComps[comp].transform = transform;
	img.tiles[tileIdx].tileComps[comp].resLevels =
	    (JPXResLevel *)greallocn(
		     img.tiles[tileIdx].tileComps[comp].resLevels,
		     nDecompLevels + 1,
		     sizeof(JPXResLevel));
	for (r = 0; r <= nDecompLevels; ++r) {
	  img.tiles[tileIdx].tileComps[comp].resLevels[r].precincts = NULL;
	}
      }
      for (r = 0; r <= nDecompLevels; ++r) {
	if (style & 0x01) {
	  if (!readUByte(&precinctSize)) {
	    error(errSyntaxError, getPos(), "Error in JPX COD marker segment");
	    return gFalse;
	  }
	  if (r > 0 && ((precinctSize & 0x0f) == 0 ||
			(precinctSize & 0xf0) == 0)) {
	    error(errSyntaxError, getPos(),
		  "Invalid precinct size in JPX COD marker segment");
	    return gFalse;
	  }
	  img.tiles[tileIdx].tileComps[0].resLevels[r].precinctWidth =
	      precinctSize & 0x0f;
	  img.tiles[tileIdx].tileComps[0].resLevels[r].precinctHeight =
	      (precinctSize >> 4) & 0x0f;
	} else {
	  img.tiles[tileIdx].tileComps[0].resLevels[r].precinctWidth = 15;
	  img.tiles[tileIdx].tileComps[0].resLevels[r].precinctHeight = 15;
	}
      }
      for (comp = 1; comp < img.nComps; ++comp) {
	for (r = 0; r <= nDecompLevels; ++r) {
	  img.tiles[tileIdx].tileComps[comp].resLevels[r].precinctWidth =
	      img.tiles[tileIdx].tileComps[0].resLevels[r].precinctWidth;
	  img.tiles[tileIdx].tileComps[comp].resLevels[r].precinctHeight =
	      img.tiles[tileIdx].tileComps[0].resLevels[r].precinctHeight;
	}
      }
      break;
    case 0x53:			// COC - coding style component
      cover(35);
      if (tilePartIdx != 0) {
	error(errSyntaxError, getPos(), "Extraneous JPX COC marker segment");
	return gFalse;
      }
      if ((img.nComps > 256 && !readUWord(&comp)) ||
	  (img.nComps <= 256 && !readUByte(&comp)) ||
	  comp >= img.nComps ||
	  !readUByte(&style) ||
	  !readUByte(&nDecompLevels) ||
	  !readUByte(&codeBlockW) ||
	  !readUByte(&codeBlockH) ||
	  !readUByte(&codeBlockStyle) ||
	  !readUByte(&transform)) {
	error(errSyntaxError, getPos(), "Error in JPX COC marker segment");
	return gFalse;
      }
      if (nDecompLevels < 1 ||
	  nDecompLevels > 31 ||
	  codeBlockW > 8 ||
	  codeBlockH > 8) {
	error(errSyntaxError, getPos(), "Error in JPX COC marker segment");
	return gFalse;
      }
      img.tiles[tileIdx].tileComps[comp].style =
	  (img.tiles[tileIdx].tileComps[comp].style & ~1) | (style & 1);
      img.tiles[tileIdx].tileComps[comp].nDecompLevels = nDecompLevels;
      img.tiles[tileIdx].tileComps[comp].codeBlockW = codeBlockW + 2;
      img.tiles[tileIdx].tileComps[comp].codeBlockH = codeBlockH + 2;
      img.tiles[tileIdx].tileComps[comp].codeBlockStyle = codeBlockStyle;
      img.tiles[tileIdx].tileComps[comp].transform = transform;
      img.tiles[tileIdx].tileComps[comp].resLevels =
	  (JPXResLevel *)greallocn(
		     img.tiles[tileIdx].tileComps[comp].resLevels,
		     nDecompLevels + 1,
		     sizeof(JPXResLevel));
      for (r = 0; r <= nDecompLevels; ++r) {
	img.tiles[tileIdx].tileComps[comp].resLevels[r].precincts = NULL;
      }
      for (r = 0; r <= nDecompLevels; ++r) {
	if (style & 0x01) {
	  if (!readUByte(&precinctSize)) {
	    error(errSyntaxError, getPos(), "Error in JPX COD marker segment");
	    return gFalse;
	  }
	  if (r > 0 && ((precinctSize & 0x0f) == 0 ||
			(precinctSize & 0xf0) == 0)) {
	    error(errSyntaxError, getPos(),
		  "Invalid precinct size in JPX COD marker segment");
	    return gFalse;
	  }
	  img.tiles[tileIdx].tileComps[comp].resLevels[r].precinctWidth =
	      precinctSize & 0x0f;
	  img.tiles[tileIdx].tileComps[comp].resLevels[r].precinctHeight =
	      (precinctSize >> 4) & 0x0f;
	} else {
	  img.tiles[tileIdx].tileComps[comp].resLevels[r].precinctWidth = 15;
	  img.tiles[tileIdx].tileComps[comp].resLevels[r].precinctHeight = 15;
	}
      }
      break;
    case 0x5c:			// QCD - quantization default
      cover(36);
      if (tilePartIdx != 0) {
	error(errSyntaxError, getPos(), "Extraneous JPX QCD marker segment");
	return gFalse;
      }
      if (!readUByte(&img.tiles[tileIdx].tileComps[0].quantStyle)) {
	error(errSyntaxError, getPos(), "Error in JPX QCD marker segment");
	return gFalse;
      }
      if ((img.tiles[tileIdx].tileComps[0].quantStyle & 0x1f) == 0x00) {
	if (segLen <= 3) {
	  error(errSyntaxError, getPos(), "Error in JPX QCD marker segment");
	  return gFalse;
	}
	img.tiles[tileIdx].tileComps[0].nQuantSteps = segLen - 3;
	img.tiles[tileIdx].tileComps[0].quantSteps =
	    (Guint *)greallocn(img.tiles[tileIdx].tileComps[0].quantSteps,
			       img.tiles[tileIdx].tileComps[0].nQuantSteps,
			       sizeof(Guint));
	for (i = 0; i < img.tiles[tileIdx].tileComps[0].nQuantSteps; ++i) {
	  if (!readUByte(&img.tiles[tileIdx].tileComps[0].quantSteps[i])) {
	    error(errSyntaxError, getPos(), "Error in JPX QCD marker segment");
	    return gFalse;
	  }
	}
      } else if ((img.tiles[tileIdx].tileComps[0].quantStyle & 0x1f) == 0x01) {
	img.tiles[tileIdx].tileComps[0].nQuantSteps = 1;
	img.tiles[tileIdx].tileComps[0].quantSteps =
	    (Guint *)greallocn(img.tiles[tileIdx].tileComps[0].quantSteps,
			       img.tiles[tileIdx].tileComps[0].nQuantSteps,
			       sizeof(Guint));
	if (!readUWord(&img.tiles[tileIdx].tileComps[0].quantSteps[0])) {
	  error(errSyntaxError, getPos(), "Error in JPX QCD marker segment");
	  return gFalse;
	}
      } else if ((img.tiles[tileIdx].tileComps[0].quantStyle & 0x1f) == 0x02) {
	if (segLen < 5) {
	  error(errSyntaxError, getPos(), "Error in JPX QCD marker segment");
	  return gFalse;
	}
	img.tiles[tileIdx].tileComps[0].nQuantSteps = (segLen - 3) / 2;
	img.tiles[tileIdx].tileComps[0].quantSteps =
	    (Guint *)greallocn(img.tiles[tileIdx].tileComps[0].quantSteps,
			       img.tiles[tileIdx].tileComps[0].nQuantSteps,
			       sizeof(Guint));
	for (i = 0; i < img.tiles[tileIdx].tileComps[0].nQuantSteps; ++i) {
	  if (!readUWord(&img.tiles[tileIdx].tileComps[0].quantSteps[i])) {
	    error(errSyntaxError, getPos(), "Error in JPX QCD marker segment");
	    return gFalse;
	  }
	}
      } else {
	error(errSyntaxError, getPos(), "Error in JPX QCD marker segment");
	return gFalse;
      }
      for (comp = 1; comp < img.nComps; ++comp) {
	img.tiles[tileIdx].tileComps[comp].quantStyle =
	    img.tiles[tileIdx].tileComps[0].quantStyle;
	img.tiles[tileIdx].tileComps[comp].nQuantSteps =
	    img.tiles[tileIdx].tileComps[0].nQuantSteps;
	img.tiles[tileIdx].tileComps[comp].quantSteps = 
	    (Guint *)greallocn(img.tiles[tileIdx].tileComps[comp].quantSteps,
			       img.tiles[tileIdx].tileComps[0].nQuantSteps,
			       sizeof(Guint));
	for (j = 0; j < img.tiles[tileIdx].tileComps[0].nQuantSteps; ++j) {
	  img.tiles[tileIdx].tileComps[comp].quantSteps[j] =
	      img.tiles[tileIdx].tileComps[0].quantSteps[j];
	}
      }
      break;
    case 0x5d:			// QCC - quantization component
      cover(37);
      if (tilePartIdx != 0) {
	error(errSyntaxError, getPos(), "Extraneous JPX QCC marker segment");
	return gFalse;
      }
      if ((img.nComps > 256 && !readUWord(&comp)) ||
	  (img.nComps <= 256 && !readUByte(&comp)) ||
	  comp >= img.nComps ||
	  !readUByte(&img.tiles[tileIdx].tileComps[comp].quantStyle)) {
	error(errSyntaxError, getPos(), "Error in JPX QCC marker segment");
	return gFalse;
      }
      if ((img.tiles[tileIdx].tileComps[comp].quantStyle & 0x1f) == 0x00) {
	if (segLen <= (img.nComps > 256 ? 5U : 4U)) {
	  error(errSyntaxError, getPos(), "Error in JPX QCC marker segment");
	  return gFalse;
	}
	img.tiles[tileIdx].tileComps[comp].nQuantSteps =
	    segLen - (img.nComps > 256 ? 5 : 4);
	img.tiles[tileIdx].tileComps[comp].quantSteps =
	    (Guint *)greallocn(img.tiles[tileIdx].tileComps[comp].quantSteps,
			       img.tiles[tileIdx].tileComps[comp].nQuantSteps,
			       sizeof(Guint));
	for (i = 0; i < img.tiles[tileIdx].tileComps[comp].nQuantSteps; ++i) {
	  if (!readUByte(&img.tiles[tileIdx].tileComps[comp].quantSteps[i])) {
	    error(errSyntaxError, getPos(), "Error in JPX QCC marker segment");
	    return gFalse;
	  }
	}
      } else if ((img.tiles[tileIdx].tileComps[comp].quantStyle & 0x1f)
		 == 0x01) {
	img.tiles[tileIdx].tileComps[comp].nQuantSteps = 1;
	img.tiles[tileIdx].tileComps[comp].quantSteps =
	    (Guint *)greallocn(img.tiles[tileIdx].tileComps[comp].quantSteps,
			       img.tiles[tileIdx].tileComps[comp].nQuantSteps,
			       sizeof(Guint));
	if (!readUWord(&img.tiles[tileIdx].tileComps[comp].quantSteps[0])) {
	  error(errSyntaxError, getPos(), "Error in JPX QCC marker segment");
	  return gFalse;
	}
      } else if ((img.tiles[tileIdx].tileComps[comp].quantStyle & 0x1f)
		 == 0x02) {
	if (segLen < (img.nComps > 256 ? 5U : 4U) + 2) {
	  error(errSyntaxError, getPos(), "Error in JPX QCC marker segment");
	  return gFalse;
	}
	img.tiles[tileIdx].tileComps[comp].nQuantSteps =
	    (segLen - (img.nComps > 256 ? 5 : 4)) / 2;
	img.tiles[tileIdx].tileComps[comp].quantSteps =
	    (Guint *)greallocn(img.tiles[tileIdx].tileComps[comp].quantSteps,
			       img.tiles[tileIdx].tileComps[comp].nQuantSteps,
			       sizeof(Guint));
	for (i = 0; i < img.tiles[tileIdx].tileComps[comp].nQuantSteps; ++i) {
	  if (!readUWord(&img.tiles[tileIdx].tileComps[comp].quantSteps[i])) {
	    error(errSyntaxError, getPos(), "Error in JPX QCD marker segment");
	    return gFalse;
	  }
	}
      } else {
	error(errSyntaxError, getPos(), "Error in JPX QCC marker segment");
	return gFalse;
      }
      break;
    case 0x5e:			// RGN - region of interest
      cover(38);
      if (tilePartIdx != 0) {
	error(errSyntaxError, getPos(), "Extraneous JPX RGN marker segment");
	return gFalse;
      }
#if 1 //~ ROI is unimplemented
      error(errUnimplemented, -1, "Got a JPX RGN segment");
      if (segLen > 2 &&
	  bufStr->discardChars(segLen - 2) != segLen - 2) {
	error(errSyntaxError, getPos(), "Error in JPX RGN marker segment");
	return gFalse;
      }
#else
      if ((img.nComps > 256 && !readUWord(&comp)) ||
	  (img.nComps <= 256 && !readUByte(&comp)) ||
	  comp >= img.nComps ||
	  !readUByte(&compInfo[comp].roi.style) ||
	  !readUByte(&compInfo[comp].roi.shift)) {
	error(errSyntaxError, getPos(), "Error in JPX RGN marker segment");
	return gFalse;
      }
#endif
      break;
    case 0x5f:			// POC - progression order change
      cover(39);
#if 1 //~ progression order changes are unimplemented
      error(errUnimplemented, -1, "Got a JPX POC segment");
      if (segLen > 2 &&
	  bufStr->discardChars(segLen - 2) != segLen - 2) {
	error(errSyntaxError, getPos(), "Error in JPX POC marker segment");
	return gFalse;
      }
#else
      nTileProgs = (segLen - 2) / (img.nComps > 256 ? 9 : 7);
      tileProgs = (JPXProgOrder *)gmallocn(nTileProgs, sizeof(JPXProgOrder));
      for (i = 0; i < nTileProgs; ++i) {
	if (!readUByte(&tileProgs[i].startRes) ||
	    !(img.nComps > 256 && readUWord(&tileProgs[i].startComp)) ||
	    !(img.nComps <= 256 && readUByte(&tileProgs[i].startComp)) ||
	    !readUWord(&tileProgs[i].endLayer) ||
	    !readUByte(&tileProgs[i].endRes) ||
	    !(img.nComps > 256 && readUWord(&tileProgs[i].endComp)) ||
	    !(img.nComps <= 256 && readUByte(&tileProgs[i].endComp)) ||
	    !readUByte(&tileProgs[i].progOrder)) {
	  error(errSyntaxError, getPos(), "Error in JPX POC marker segment");
	  return gFalse;
	}
      }
#endif
      break;
    case 0x61:			// PPT - packed packet headers, tile-part hdr
      cover(40);
#if 1 //~ packed packet headers are unimplemented
      error(errUnimplemented, -1, "Got a JPX PPT segment");
      if (segLen > 2 &&
	  bufStr->discardChars(segLen - 2) != segLen - 2) {
	error(errSyntaxError, getPos(), "Error in JPX PPT marker segment");
	return gFalse;
      }
#endif
    case 0x58:			// PLT - packet length, tile-part header
      // skipped
      cover(41);
      if (segLen > 2 &&
	  bufStr->discardChars(segLen - 2) != segLen - 2) {
	error(errSyntaxError, getPos(), "Error in JPX PLT marker segment");
	return gFalse;
      }
      break;
    case 0x64:			// COM - comment
      // skipped
      cover(42);
      if (segLen > 2 &&
	  bufStr->discardChars(segLen - 2) != segLen - 2) {
	error(errSyntaxError, getPos(), "Error in JPX COM marker segment");
	return gFalse;
      }
      break;
    case 0x93:			// SOD - start of data
      cover(43);
      haveSOD = gTrue;
      break;
    default:
      cover(44);
      error(errSyntaxError, getPos(),
	    "Unknown marker segment {0:02x} in JPX tile-part stream",
	    segType);
      if (segLen > 2) {
	bufStr->discardChars(segLen - 2);
      }
      break;
    }
  } while (!haveSOD);

  for (comp = 0; comp < img.nComps; ++comp) {
    tileComp = &img.tiles[tileIdx].tileComps[comp];
    qStyle = tileComp->quantStyle & 0x1f;
    if ((qStyle == 0 && tileComp->nQuantSteps < 3 * tileComp->nDecompLevels + 1) ||
	(qStyle == 1 && tileComp->nQuantSteps < 1) ||
	(qStyle == 2 && tileComp->nQuantSteps < 3 * tileComp->nDecompLevels + 1)) {
      error(errSyntaxError, getPos(), "Too few quant steps in JPX tile part");
      return gFalse;
    }
  }

  //----- initialize the tile, precincts, and code-blocks
  if (tilePartIdx == 0) {
    tile = &img.tiles[tileIdx];
    i = tileIdx / img.nXTiles;
    j = tileIdx % img.nXTiles;
    if ((tile->x0 = img.xTileOffset + j * img.xTileSize) < img.xOffset) {
      tile->x0 = img.xOffset;
    }
    if ((tile->y0 = img.yTileOffset + i * img.yTileSize) < img.yOffset) {
      tile->y0 = img.yOffset;
    }
    if ((tile->x1 = img.xTileOffset + (j + 1) * img.xTileSize) > img.xSize) {
      tile->x1 = img.xSize;
    }
    if ((tile->y1 = img.yTileOffset + (i + 1) * img.yTileSize) > img.ySize) {
      tile->y1 = img.ySize;
    }
    tile->comp = 0;
    tile->res = 0;
    tile->precinct = 0;
    tile->layer = 0;
    tile->done = gFalse;
    tile->maxNDecompLevels = 0;
    tile->maxNPrecincts = 0;
    for (comp = 0; comp < img.nComps; ++comp) {
      tileComp = &tile->tileComps[comp];
      if (tileComp->nDecompLevels > tile->maxNDecompLevels) {
	tile->maxNDecompLevels = tileComp->nDecompLevels;
      }
      tileComp->x0 = jpxCeilDiv(tile->x0, tileComp->hSep);
      tileComp->y0 = jpxCeilDiv(tile->y0, tileComp->vSep);
      tileComp->x1 = jpxCeilDiv(tile->x1, tileComp->hSep);
      tileComp->y1 = jpxCeilDiv(tile->y1, tileComp->vSep);
      tileComp->x0r = jpxCeilDivPow2(tileComp->x0, reduction);
      tileComp->w = jpxCeilDivPow2(tileComp->x1, reduction) - tileComp->x0r;
      tileComp->y0r = jpxCeilDivPow2(tileComp->y0, reduction);
      tileComp->h = jpxCeilDivPow2(tileComp->y1, reduction) - tileComp->y0r;
      if (tileComp->w == 0 || tileComp->h == 0 ||
	  tileComp->w > INT_MAX / tileComp->h) {
	error(errSyntaxError, getPos(),
	      "Invalid tile size or sample separation in JPX stream");
	return gFalse;
      }
      tileComp->data = (int *)gmallocn(tileComp->w * tileComp->h, sizeof(int));
      if (tileComp->x1 - tileComp->x0 > tileComp->y1 - tileComp->y0) {
	n = tileComp->x1 - tileComp->x0;
      } else {
	n = tileComp->y1 - tileComp->y0;
      }
      tileComp->buf = (int *)gmallocn(n + 8, sizeof(int));
      for (r = 0; r <= tileComp->nDecompLevels; ++r) {
	resLevel = &tileComp->resLevels[r];
	resLevel->x0 = jpxCeilDivPow2(tileComp->x0,
				      tileComp->nDecompLevels - r);
	resLevel->y0 = jpxCeilDivPow2(tileComp->y0,
				      tileComp->nDecompLevels - r);
	resLevel->x1 = jpxCeilDivPow2(tileComp->x1,
				      tileComp->nDecompLevels - r);
	resLevel->y1 = jpxCeilDivPow2(tileComp->y1,
				      tileComp->nDecompLevels - r);
	resLevel->codeBlockW = r == 0 ? resLevel->precinctWidth
	                              : resLevel->precinctWidth - 1;
	if (resLevel->codeBlockW > tileComp->codeBlockW) {
	  resLevel->codeBlockW = tileComp->codeBlockW;
	}
	resLevel->cbW = 1 << resLevel->codeBlockW;
	resLevel->codeBlockH = r == 0 ? resLevel->precinctHeight
	                              : resLevel->precinctHeight - 1;
	if (resLevel->codeBlockH > tileComp->codeBlockH) {
	  resLevel->codeBlockH = tileComp->codeBlockH;
	}
	resLevel->cbH = 1 << resLevel->codeBlockH;
	// the JPEG 2000 spec says that packets for empty res levels
	// should all be present in the codestream (B.6, B.9, B.10),
	// but it appears that encoders drop packets if the res level
	// AND the subbands are all completely empty
	resLevel->empty = resLevel->x0 == resLevel->x1 ||
	                  resLevel->y0 == resLevel->y1;
	if (r == 0) {
	  nSBs = 1;
	  resLevel->bx0[0] = resLevel->x0;
	  resLevel->by0[0] = resLevel->y0;
	  resLevel->bx1[0] = resLevel->x1;
	  resLevel->by1[0] = resLevel->y1;
	  resLevel->empty = resLevel->empty &&
	                    (resLevel->bx0[0] == resLevel->bx1[0] ||
			     resLevel->by0[0] == resLevel->by1[0]);
	} else {
	  nSBs = 3;
	  resLevel->bx0[0] = jpxCeilDivPow2(resLevel->x0 - 1, 1);
	  resLevel->by0[0] = jpxCeilDivPow2(resLevel->y0, 1);
	  resLevel->bx1[0] = jpxCeilDivPow2(resLevel->x1 - 1, 1);
	  resLevel->by1[0] = jpxCeilDivPow2(resLevel->y1, 1);
	  resLevel->bx0[1] = jpxCeilDivPow2(resLevel->x0, 1);
	  resLevel->by0[1] = jpxCeilDivPow2(resLevel->y0 - 1, 1);
	  resLevel->bx1[1] = jpxCeilDivPow2(resLevel->x1, 1);
	  resLevel->by1[1] = jpxCeilDivPow2(resLevel->y1 - 1, 1);
	  resLevel->bx0[2] = jpxCeilDivPow2(resLevel->x0 - 1, 1);
	  resLevel->by0[2] = jpxCeilDivPow2(resLevel->y0 - 1, 1);
	  resLevel->bx1[2] = jpxCeilDivPow2(resLevel->x1 - 1, 1);
	  resLevel->by1[2] = jpxCeilDivPow2(resLevel->y1 - 1, 1);
	  resLevel->empty = resLevel->empty &&
	                    (resLevel->bx0[0] == resLevel->bx1[0] ||
			     resLevel->by0[0] == resLevel->by1[0]) &&
	                    (resLevel->bx0[1] == resLevel->bx1[1] ||
			     resLevel->by0[1] == resLevel->by1[1]) &&
	                    (resLevel->bx0[2] == resLevel->bx1[2] ||
			     resLevel->by0[2] == resLevel->by1[2]);
	}
	preCol0 = jpxFloorDivPow2(resLevel->x0, resLevel->precinctWidth);
	preCol1 = jpxCeilDivPow2(resLevel->x1, resLevel->precinctWidth);
	preRow0 = jpxFloorDivPow2(resLevel->y0, resLevel->precinctHeight);
	preRow1 = jpxCeilDivPow2(resLevel->y1, resLevel->precinctHeight);
	resLevel->nPrecincts = (preCol1 - preCol0) * (preRow1 - preRow0);
	resLevel->precincts = (JPXPrecinct *)gmallocn(resLevel->nPrecincts,
						      sizeof(JPXPrecinct));
	if (resLevel->nPrecincts > tile->maxNPrecincts) {
	  tile->maxNPrecincts = resLevel->nPrecincts;
	}
	for (pre = 0; pre < resLevel->nPrecincts; ++pre) {
	  resLevel->precincts[pre].subbands = NULL;
	}
	precinct = resLevel->precincts;
	for (preRow = preRow0; preRow < preRow1; ++preRow) {
	  for (preCol = preCol0; preCol < preCol1; ++preCol) {
	    precinct->subbands =
	        (JPXSubband *)gmallocn(nSBs, sizeof(JPXSubband));
	    for (sb = 0; sb < nSBs; ++sb) {
	      precinct->subbands[sb].inclusion = NULL;
	      precinct->subbands[sb].zeroBitPlane = NULL;
	      precinct->subbands[sb].cbs = NULL;
	    }
	    for (sb = 0; sb < nSBs; ++sb) {
	      subband = &precinct->subbands[sb];
	      if (r == 0) {
		px0 = preCol << resLevel->precinctWidth;
		px1 = (preCol + 1) << resLevel->precinctWidth;
		py0 = preRow << resLevel->precinctHeight;
		py1 = (preRow + 1) << resLevel->precinctHeight;
	      } else {
		px0 = preCol << (resLevel->precinctWidth - 1);
		px1 = (preCol + 1) << (resLevel->precinctWidth - 1);
		py0 = preRow << (resLevel->precinctHeight - 1);
		py1 = (preRow + 1) << (resLevel->precinctHeight - 1);
	      }
	      if (px0 < resLevel->bx0[sb]) {
		px0 = resLevel->bx0[sb];
	      }
	      if (px1 > resLevel->bx1[sb]) {
		px1 = resLevel->bx1[sb];
	      }
	      if (py0 < resLevel->by0[sb]) {
		py0 = resLevel->by0[sb];
	      }
	      if (py1 > resLevel->by1[sb]) {
		py1 = resLevel->by1[sb];
	      }
	      if (r == 0) { // (NL)LL
		sbCoeffs = tileComp->data;
	      } else if (sb == 0) { // (NL-r+1)HL
		sbCoeffs = tileComp->data
                           + resLevel->bx1[1] - resLevel->bx0[1];
	      } else if (sb == 1) { // (NL-r+1)LH
		sbCoeffs = tileComp->data
		           + (resLevel->by1[0] - resLevel->by0[0]) * tileComp->w;
	      } else { // (NL-r+1)HH
		sbCoeffs = tileComp->data
		           + (resLevel->by1[0] - resLevel->by0[0]) * tileComp->w
		           + (resLevel->bx1[1] - resLevel->bx0[1]);
	      }
	      cbCol0 = jpxFloorDivPow2(px0, resLevel->codeBlockW);
	      cbCol1 = jpxCeilDivPow2(px1, resLevel->codeBlockW);
	      cbRow0 = jpxFloorDivPow2(py0, resLevel->codeBlockH);
	      cbRow1 = jpxCeilDivPow2(py1, resLevel->codeBlockH);
	      subband->nXCBs = cbCol1 - cbCol0;
	      subband->nYCBs = cbRow1 - cbRow0;
	      n = subband->nXCBs > subband->nYCBs ? subband->nXCBs
	                                          : subband->nYCBs;
	      for (subband->maxTTLevel = 0, --n;
		   n;
		   ++subband->maxTTLevel, n >>= 1) ;
	      n = 0;
	      for (level = subband->maxTTLevel; level >= 0; --level) {
		nx = jpxCeilDivPow2(subband->nXCBs, level);
		ny = jpxCeilDivPow2(subband->nYCBs, level);
		n += nx * ny;
	      }
	      subband->inclusion =
	          (JPXTagTreeNode *)gmallocn(n, sizeof(JPXTagTreeNode));
	      subband->zeroBitPlane =
	          (JPXTagTreeNode *)gmallocn(n, sizeof(JPXTagTreeNode));
	      for (k = 0; k < n; ++k) {
		subband->inclusion[k].finished = gFalse;
		subband->inclusion[k].val = 0;
		subband->zeroBitPlane[k].finished = gFalse;
		subband->zeroBitPlane[k].val = 0;
	      }
	      subband->cbs = (JPXCodeBlock *)gmallocn(subband->nXCBs *
						        subband->nYCBs,
						      sizeof(JPXCodeBlock));
	      for (k = 0; k < subband->nXCBs * subband->nYCBs; ++k) {
		subband->cbs[k].dataLen = NULL;
		subband->cbs[k].touched = NULL;
		subband->cbs[k].arithDecoder = NULL;
		subband->cbs[k].stats = NULL;
	      }
	      cb = subband->cbs;
	      for (cbY = cbRow0; cbY < cbRow1; ++cbY) {
		for (cbX = cbCol0; cbX < cbCol1; ++cbX) {
		  cb->x0 = cbX << resLevel->codeBlockW;
		  cb->x1 = cb->x0 + resLevel->cbW;
		  if (cb->x0 < px0) {
		    cb->x0 = px0;
		  }
		  if (cb->x1 > px1) {
		    cb->x1 = px1;
		  }
		  cb->y0 = cbY << resLevel->codeBlockH;
		  cb->y1 = cb->y0 + resLevel->cbH;
		  if (cb->y0 < py0) {
		    cb->y0 = py0;
		  }
		  if (cb->y1 > py1) {
		    cb->y1 = py1;
		  }
		  cb->seen = gFalse;
		  cb->lBlock = 3;
		  cb->nextPass = jpxPassCleanup;
		  cb->nZeroBitPlanes = 0;
		  cb->dataLenSize = 1;
		  cb->dataLen = (Guint *)gmalloc(sizeof(Guint));
		  if (r <= tileComp->nDecompLevels - reduction) {
		    cb->coeffs = sbCoeffs
		                 + (cb->y0 - resLevel->by0[sb]) * tileComp->w
		                 + (cb->x0 - resLevel->bx0[sb]);
		    cb->touched = (char *)gmalloc(1 << (resLevel->codeBlockW
							+ resLevel->codeBlockH));
		    cb->len = 0;
		    for (cbj = 0; cbj < cb->y1 - cb->y0; ++cbj) {
		      for (cbi = 0; cbi < cb->x1 - cb->x0; ++cbi) {
			cb->coeffs[cbj * tileComp->w + cbi] = 0;
		      }
		    }
		    memset(cb->touched, 0,
			   ((size_t)1 << (resLevel->codeBlockW
					  + resLevel->codeBlockH)));
		  } else {
		    cb->coeffs = NULL;
		    cb->touched = NULL;
		    cb->len = 0;
		  }
		  ++cb;
		}
	      }
	    }
	    ++precinct;
	  }
	}
      }
    }
    tile->init = gTrue;
  }

  return readTilePartData(tileIdx, tilePartLen, tilePartToEOC);
}

GBool JPXStream::readTilePartData(Guint tileIdx,
				  Guint tilePartLen, GBool tilePartToEOC) {
  JPXTile *tile;
  JPXTileComp *tileComp;
  JPXResLevel *resLevel;
  JPXPrecinct *precinct;
  JPXSubband *subband;
  JPXCodeBlock *cb;
  Guint ttVal;
  Guint bits, cbX, cbY, nx, ny, i, j, n, sb;
  int level;

  tile = &img.tiles[tileIdx];

  // read all packets from this tile-part
  while (1) {

    // if the tile is finished, skip any remaining data
    if (tile->done) {
      bufStr->discardChars(tilePartLen);
      return gTrue;
    }

    if (tilePartToEOC) {
      //~ peek for an EOC marker
      cover(93);
    } else if (tilePartLen == 0) {
      break;
    }

    tileComp = &tile->tileComps[tile->comp];
    resLevel = &tileComp->resLevels[tile->res];
    precinct = &resLevel->precincts[tile->precinct];

    if (resLevel->empty) {
      goto nextPacket;
    }

    //----- packet header

    // setup
    startBitBuf(tilePartLen);
    if (tileComp->style & 0x02) {
      skipSOP();
    }

    // zero-length flag
    if (!readBits(1, &bits)) {
      goto err;
    }
    if (!bits) {
      // packet is empty -- clear all code-block inclusion flags
      cover(45);
      for (sb = 0; sb < (Guint)(tile->res == 0 ? 1 : 3); ++sb) {
	subband = &precinct->subbands[sb];
	for (cbY = 0; cbY < subband->nYCBs; ++cbY) {
	  for (cbX = 0; cbX < subband->nXCBs; ++cbX) {
	    cb = &subband->cbs[cbY * subband->nXCBs + cbX];
	    cb->included = gFalse;
	  }
	}
      }
    } else {

      for (sb = 0; sb < (Guint)(tile->res == 0 ? 1 : 3); ++sb) {
	subband = &precinct->subbands[sb];
	for (cbY = 0; cbY < subband->nYCBs; ++cbY) {
	  for (cbX = 0; cbX < subband->nXCBs; ++cbX) {
	    cb = &subband->cbs[cbY * subband->nXCBs + cbX];

	    // skip code-blocks with no coefficients
	    if (cb->x0 >= cb->x1 || cb->y0 >= cb->y1) {
	      cover(46);
	      cb->included = gFalse;
	      continue;
	    }

	    // code-block inclusion
	    if (cb->seen) {
	      cover(47);
	      if (!readBits(1, &cb->included)) {
		goto err;
	      }
	    } else {
	      cover(48);
	      ttVal = 0;
	      i = 0;
	      for (level = subband->maxTTLevel; level >= 0; --level) {
		nx = jpxCeilDivPow2(subband->nXCBs, level);
		ny = jpxCeilDivPow2(subband->nYCBs, level);
		j = i + (cbY >> level) * nx + (cbX >> level);
		if (!subband->inclusion[j].finished &&
		    !subband->inclusion[j].val) {
		  subband->inclusion[j].val = ttVal;
		} else {
		  ttVal = subband->inclusion[j].val;
		}
		while (!subband->inclusion[j].finished &&
		       ttVal <= tile->layer) {
		  if (!readBits(1, &bits)) {
		    goto err;
		  }
		  if (bits == 1) {
		    subband->inclusion[j].finished = gTrue;
		  } else {
		    ++ttVal;
		  }
		}
		subband->inclusion[j].val = ttVal;
		if (ttVal > tile->layer) {
		  break;
		}
		i += nx * ny;
	      }
	      cb->included = level < 0;
	    }

	    if (cb->included) {
	      cover(49);

	      // zero bit-plane count
	      if (!cb->seen) {
		cover(50);
		ttVal = 0;
		i = 0;
		for (level = subband->maxTTLevel; level >= 0; --level) {
		  nx = jpxCeilDivPow2(subband->nXCBs, level);
		  ny = jpxCeilDivPow2(subband->nYCBs, level);
		  j = i + (cbY >> level) * nx + (cbX >> level);
		  if (!subband->zeroBitPlane[j].finished &&
		      !subband->zeroBitPlane[j].val) {
		    subband->zeroBitPlane[j].val = ttVal;
		  } else {
		    ttVal = subband->zeroBitPlane[j].val;
		  }
		  while (!subband->zeroBitPlane[j].finished) {
		    if (!readBits(1, &bits)) {
		      goto err;
		    }
		    if (bits == 1) {
		      subband->zeroBitPlane[j].finished = gTrue;
		    } else {
		      ++ttVal;
		    }
		  }
		  subband->zeroBitPlane[j].val = ttVal;
		  i += nx * ny;
		}
		cb->nZeroBitPlanes = ttVal;
	      }

	      // number of coding passes
	      if (!readBits(1, &bits)) {
		goto err;
	      }
	      if (bits == 0) {
		cover(51);
		cb->nCodingPasses = 1;
	      } else {
		if (!readBits(1, &bits)) {
		  goto err;
		}
		if (bits == 0) {
		  cover(52);
		  cb->nCodingPasses = 2;
		} else {
		  cover(53);
		  if (!readBits(2, &bits)) {
		    goto err;
		  }
		  if (bits < 3) {
		    cover(54);
		    cb->nCodingPasses = 3 + bits;
		  } else {
		    cover(55);
		    if (!readBits(5, &bits)) {
		      goto err;
		    }
		    if (bits < 31) {
		      cover(56);
		      cb->nCodingPasses = 6 + bits;
		    } else {
		      cover(57);
		      if (!readBits(7, &bits)) {
			goto err;
		      }
		      cb->nCodingPasses = 37 + bits;
		    }
		  }
		}
	      }

	      // update Lblock
	      while (1) {
		if (!readBits(1, &bits)) {
		  goto err;
		}
		if (!bits) {
		  break;
		}
		++cb->lBlock;
	      }

	      // one codeword segment for each of the coding passes
	      if (tileComp->codeBlockStyle & 0x04) {
		if (cb->nCodingPasses > cb->dataLenSize) {
		  cb->dataLenSize = cb->nCodingPasses;
		  cb->dataLen = (Guint *)greallocn(cb->dataLen,
						   cb->dataLenSize,
						   sizeof(Guint));
		}

		// read the lengths
		for (i = 0; i < cb->nCodingPasses; ++i) {
		  if (!readBits(cb->lBlock, &cb->dataLen[i])) {
		    goto err;
		  }
		}

	      // one codeword segment for all of the coding passes
	      } else {

		// read the length
		for (n = cb->lBlock, i = cb->nCodingPasses >> 1;
		     i;
		     ++n, i >>= 1) ;
		if (!readBits(n, &cb->dataLen[0])) {
		  goto err;
		}
	      }
	    }
	  }
	}
      }
    }
    if (tileComp->style & 0x04) {
      skipEPH();
    }
    tilePartLen = finishBitBuf();

    //----- packet data

    for (sb = 0; sb < (Guint)(tile->res == 0 ? 1 : 3); ++sb) {
      subband = &precinct->subbands[sb];
      for (cbY = 0; cbY < subband->nYCBs; ++cbY) {
	for (cbX = 0; cbX < subband->nXCBs; ++cbX) {
	  cb = &subband->cbs[cbY * subband->nXCBs + cbX];
	  if (cb->included) {
	    if (!readCodeBlockData(tileComp, resLevel, precinct, subband,
				   tile->res, sb, cb)) {
	      return gFalse;
	    }
	    if (tileComp->codeBlockStyle & 0x04) {
	      for (i = 0; i < cb->nCodingPasses; ++i) {
		tilePartLen -= cb->dataLen[i];
	      }
	    } else {
	      tilePartLen -= cb->dataLen[0];
	    }
	    cb->seen = gTrue;
	  }
	}
      }
    }

    //----- next packet

  nextPacket:
    switch (tile->progOrder) {
    case 0: // layer, resolution level, component, precinct
      cover(58);
      do {
	if (++tile->precinct == tile->maxNPrecincts) {
	  tile->precinct = 0;
	  if (++tile->comp == img.nComps) {
	    tile->comp = 0;
	    if (++tile->res == tile->maxNDecompLevels + 1) {
	      tile->res = 0;
	      if (++tile->layer == tile->nLayers) {
		tile->layer = 0;
		tile->done = gTrue;
	      }
	    }
	  }
	}
      } while (!tile->done &&
	       (tile->res > tile->tileComps[tile->comp].nDecompLevels ||
		tile->precinct >= tile->tileComps[tile->comp]
	                                  .resLevels[tile->res].nPrecincts));
      break;
    case 1: // resolution level, layer, component, precinct
      cover(59);
      do {
	if (++tile->precinct == tile->maxNPrecincts) {
	  tile->precinct = 0;
	  if (++tile->comp == img.nComps) {
	    tile->comp = 0;
	    if (++tile->layer == tile->nLayers) {
	      tile->layer = 0;
	      if (++tile->res == tile->maxNDecompLevels + 1) {
		tile->res = 0;
		tile->done = gTrue;
	      }
	    }
	  }
	}
      } while (!tile->done &&
	       (tile->res > tile->tileComps[tile->comp].nDecompLevels ||
		tile->precinct >= tile->tileComps[tile->comp]
	                                  .resLevels[tile->res].nPrecincts));
      break;
    case 2: // resolution level, precinct, component, layer
      cover(60);
      //~ this is incorrect if there are subsampled components (?)
      do {
	if (++tile->layer == tile->nLayers) {
	  tile->layer = 0;
	  if (++tile->comp == img.nComps) {
	    tile->comp = 0;
	    if (++tile->precinct == tile->maxNPrecincts) {
	      tile->precinct = 0;
	      if (++tile->res == tile->maxNDecompLevels + 1) {
		tile->res = 0;
		tile->done = gTrue;
	      }
	    }
	  }
	}
      } while (!tile->done &&
	       (tile->res > tile->tileComps[tile->comp].nDecompLevels ||
		tile->precinct >= tile->tileComps[tile->comp]
	                                  .resLevels[tile->res].nPrecincts));
      break;
    case 3: // precinct, component, resolution level, layer
      cover(61);
      //~ this is incorrect if there are subsampled components (?)
      do {
	if (++tile->layer == tile->nLayers) {
	  tile->layer = 0;
	  if (++tile->res == tile->maxNDecompLevels + 1) {
	    tile->res = 0;
	    if (++tile->comp == img.nComps) {
	      tile->comp = 0;
	      if (++tile->precinct == tile->maxNPrecincts) {
		tile->precinct = 0;
		tile->done = gTrue;
	      }
	    }
	  }
	}
      } while (!tile->done &&
	       (tile->res > tile->tileComps[tile->comp].nDecompLevels ||
		tile->precinct >= tile->tileComps[tile->comp]
	                                  .resLevels[tile->res].nPrecincts));
      break;
    case 4: // component, precinct, resolution level, layer
      cover(62);
      do {
	if (++tile->layer == tile->nLayers) {
	  tile->layer = 0;
	  if (++tile->res == tile->maxNDecompLevels + 1) {
	    tile->res = 0;
	    if (++tile->precinct == tile->maxNPrecincts) {
	      tile->precinct = 0;
	      if (++tile->comp == img.nComps) {
		tile->comp = 0;
		tile->done = gTrue;
	      }
	    }
	  }
	}
      } while (!tile->done &&
	       (tile->res > tile->tileComps[tile->comp].nDecompLevels ||
		tile->precinct >= tile->tileComps[tile->comp]
	                                  .resLevels[tile->res].nPrecincts));
      break;
    }
  }

  return gTrue;

 err:
  error(errSyntaxError, getPos(), "Error in JPX stream");
  return gFalse;
}

GBool JPXStream::readCodeBlockData(JPXTileComp *tileComp,
				   JPXResLevel *resLevel,
				   JPXPrecinct *precinct,
				   JPXSubband *subband,
				   Guint res, Guint sb,
				   JPXCodeBlock *cb) {
  int *coeff0, *coeff1, *coeff;
  char *touched0, *touched1, *touched;
  Guint horiz, vert, diag, all, cx, xorBit;
  int horizSign, vertSign, bit;
  int segSym;
  Guint n, i, x, y0, y1;

  if (res > tileComp->nDecompLevels - reduction) {
    // skip the codeblock data
    if (tileComp->codeBlockStyle & 0x04) {
      n = 0;
      for (i = 0; i < cb->nCodingPasses; ++i) {
	n += cb->dataLen[i];
      }
    } else {
      n = cb->dataLen[0];
    }
    bufStr->discardChars(n);
    return gTrue;
  }

  if (cb->arithDecoder) {
    cover(63);
    cb->arithDecoder->restart(cb->dataLen[0]);
  } else {
    cover(64);
    cb->arithDecoder = new JArithmeticDecoder();
    cb->arithDecoder->setStream(bufStr, cb->dataLen[0]);
    cb->arithDecoder->start();
    cb->stats = new JArithmeticDecoderStats(jpxNContexts);
    cb->stats->setEntry(jpxContextSigProp, 4, 0);
    cb->stats->setEntry(jpxContextRunLength, 3, 0);
    cb->stats->setEntry(jpxContextUniform, 46, 0);
  }

  for (i = 0; i < cb->nCodingPasses; ++i) {
    if ((tileComp->codeBlockStyle & 0x04) && i > 0) {
      cb->arithDecoder->setStream(bufStr, cb->dataLen[i]);
      cb->arithDecoder->start();
    }

    switch (cb->nextPass) {

    //----- significance propagation pass
    case jpxPassSigProp:
      cover(65);
      for (y0 = cb->y0, coeff0 = cb->coeffs, touched0 = cb->touched;
	   y0 < cb->y1;
	   y0 += 4, coeff0 += 4 * tileComp->w,
	     touched0 += 4 << resLevel->codeBlockW) {
	for (x = cb->x0, coeff1 = coeff0, touched1 = touched0;
	     x < cb->x1;
	     ++x, ++coeff1, ++touched1) {
	  for (y1 = 0, coeff = coeff1, touched = touched1;
	       y1 < 4 && y0+y1 < cb->y1;
	       ++y1, coeff += tileComp->w, touched += resLevel->cbW) {
	    if (!*coeff) {
	      horiz = vert = diag = 0;
	      horizSign = vertSign = 2;
	      if (x > cb->x0) {
		if (coeff[-1]) {
		  ++horiz;
		  horizSign += coeff[-1] < 0 ? -1 : 1;
		}
		if (y0+y1 > cb->y0) {
		  diag += coeff[-(int)tileComp->w - 1] ? 1 : 0;
		}
		if (y0+y1 < cb->y1 - 1 &&
		    (!(tileComp->codeBlockStyle & 0x08) || y1 < 3)) {
		  diag += coeff[tileComp->w - 1] ? 1 : 0;
		}
	      }
	      if (x < cb->x1 - 1) {
		if (coeff[1]) {
		  ++horiz;
		  horizSign += coeff[1] < 0 ? -1 : 1;
		}
		if (y0+y1 > cb->y0) {
		  diag += coeff[-(int)tileComp->w + 1] ? 1 : 0;
		}
		if (y0+y1 < cb->y1 - 1 &&
		    (!(tileComp->codeBlockStyle & 0x08) || y1 < 3)) {
		  diag += coeff[tileComp->w + 1] ? 1 : 0;
		}
	      }
	      if (y0+y1 > cb->y0) {
		if (coeff[-(int)tileComp->w]) {
		  ++vert;
		  vertSign += coeff[-(int)tileComp->w] < 0 ? -1 : 1;
		}
	      }
	      if (y0+y1 < cb->y1 - 1 &&
		  (!(tileComp->codeBlockStyle & 0x08) || y1 < 3)) {
		if (coeff[tileComp->w]) {
		  ++vert;
		  vertSign += coeff[tileComp->w] < 0 ? -1 : 1;
		}
	      }
	      cx = sigPropContext[horiz][vert][diag][res == 0 ? 1 : sb];
	      if (cx != 0) {
		if (cb->arithDecoder->decodeBit(cx, cb->stats)) {
		  cx = signContext[horizSign][vertSign][0];
		  xorBit = signContext[horizSign][vertSign][1];
		  if (cb->arithDecoder->decodeBit(cx, cb->stats) ^ xorBit) {
		    *coeff = -1;
		  } else {
		    *coeff = 1;
		  }
		}
		*touched = 1;
	      }
	    }
	  }
	}
      }
      ++cb->nextPass;
      break;

    //----- magnitude refinement pass
    case jpxPassMagRef:
      cover(66);
      for (y0 = cb->y0, coeff0 = cb->coeffs, touched0 = cb->touched;
	   y0 < cb->y1;
	   y0 += 4, coeff0 += 4 * tileComp->w,
	     touched0 += 4 << resLevel->codeBlockW) {
	for (x = cb->x0, coeff1 = coeff0, touched1 = touched0;
	     x < cb->x1;
	     ++x, ++coeff1, ++touched1) {
	  for (y1 = 0, coeff = coeff1, touched = touched1;
	       y1 < 4 && y0+y1 < cb->y1;
	       ++y1, coeff += tileComp->w, touched += resLevel->cbW) {
	    if (*coeff && !*touched) {
	      if (*coeff == 1 || *coeff == -1) {
		all = 0;
		if (x > cb->x0) {
		  all += coeff[-1] ? 1 : 0;
		  if (y0+y1 > cb->y0) {
		    all += coeff[-(int)tileComp->w - 1] ? 1 : 0;
		  }
		  if (y0+y1 < cb->y1 - 1 &&
		      (!(tileComp->codeBlockStyle & 0x08) || y1 < 3)) {
		    all += coeff[tileComp->w - 1] ? 1 : 0;
		  }
		}
		if (x < cb->x1 - 1) {
		  all += coeff[1] ? 1 : 0;
		  if (y0+y1 > cb->y0) {
		    all += coeff[-(int)tileComp->w + 1] ? 1 : 0;
		  }
		  if (y0+y1 < cb->y1 - 1 &&
		      (!(tileComp->codeBlockStyle & 0x08) || y1 < 3)) {
		    all += coeff[tileComp->w + 1] ? 1 : 0;
		  }
		}
		if (y0+y1 > cb->y0) {
		  all += coeff[-(int)tileComp->w] ? 1 : 0;
		}
		if (y0+y1 < cb->y1 - 1 &&
		    (!(tileComp->codeBlockStyle & 0x08) || y1 < 3)) {
		  all += coeff[tileComp->w] ? 1 : 0;
		}
		cx = all ? 15 : 14;
	      } else {
		cx = 16;
	      }
	      bit = cb->arithDecoder->decodeBit(cx, cb->stats);
	      if (*coeff < 0) {
		*coeff = (*coeff << 1) - bit;
	      } else {
		*coeff = (*coeff << 1) + bit;
	      }
	      *touched = 1;
	    }
	  }
	}
      }
      ++cb->nextPass;
      break;

    //----- cleanup pass
    case jpxPassCleanup:
      cover(67);
      for (y0 = cb->y0, coeff0 = cb->coeffs, touched0 = cb->touched;
	   y0 < cb->y1;
	   y0 += 4, coeff0 += 4 * tileComp->w,
	     touched0 += 4 << resLevel->codeBlockW) {
	for (x = cb->x0, coeff1 = coeff0, touched1 = touched0;
	     x < cb->x1;
	     ++x, ++coeff1, ++touched1) {
	  y1 = 0;
	  if (y0 + 3 < cb->y1 &&
	      !(*touched1) &&
	      !(touched1[resLevel->cbW]) &&
	      !(touched1[2 * resLevel->cbW]) &&
	      !(touched1[3 * resLevel->cbW]) &&
	      (x == cb->x0 || y0 == cb->y0 ||
	       !coeff1[-(int)tileComp->w - 1]) &&
	      (y0 == cb->y0 ||
	       !coeff1[-(int)tileComp->w]) &&
	      (x == cb->x1 - 1 || y0 == cb->y0 ||
	       !coeff1[-(int)tileComp->w + 1]) &&
	      (x == cb->x0 ||
	       (!coeff1[-1] &&
		!coeff1[tileComp->w - 1] &&
		!coeff1[2 * tileComp->w - 1] && 
		!coeff1[3 * tileComp->w - 1])) &&
	      (x == cb->x1 - 1 ||
	       (!coeff1[1] &&
		!coeff1[tileComp->w + 1] &&
		!coeff1[2 * tileComp->w + 1] &&
		!coeff1[3 * tileComp->w + 1])) &&
	      ((tileComp->codeBlockStyle & 0x08) ||
	       ((x == cb->x0 || y0+4 == cb->y1 ||
		 !coeff1[4 * tileComp->w - 1]) &&
		(y0+4 == cb->y1 ||
		 !coeff1[4 * tileComp->w]) &&
		(x == cb->x1 - 1 || y0+4 == cb->y1 ||
		 !coeff1[4 * tileComp->w + 1])))) {
	    if (cb->arithDecoder->decodeBit(jpxContextRunLength, cb->stats)) {
	      y1 = cb->arithDecoder->decodeBit(jpxContextUniform, cb->stats);
	      y1 = (y1 << 1) |
		   cb->arithDecoder->decodeBit(jpxContextUniform, cb->stats);
	      coeff = &coeff1[y1 * tileComp->w];
	      cx = signContext[2][2][0];
	      xorBit = signContext[2][2][1];
	      if (cb->arithDecoder->decodeBit(cx, cb->stats) ^ xorBit) {
		*coeff = -1;
	      } else {
		*coeff = 1;
	      }
	      ++y1;
	    } else {
	      y1 = 4;
	    }
	  }
	  for (coeff = &coeff1[y1 * tileComp->w],
		 touched = &touched1[y1 << resLevel->codeBlockW];
	       y1 < 4 && y0 + y1 < cb->y1;
	       ++y1, coeff += tileComp->w, touched += resLevel->cbW) {
	    if (!*touched) {
	      horiz = vert = diag = 0;
	      horizSign = vertSign = 2;
	      if (x > cb->x0) {
		if (coeff[-1]) {
		  ++horiz;
		  horizSign += coeff[-1] < 0 ? -1 : 1;
		}
		if (y0+y1 > cb->y0) {
		  diag += coeff[-(int)tileComp->w - 1] ? 1 : 0;
		}
		if (y0+y1 < cb->y1 - 1 &&
		    (!(tileComp->codeBlockStyle & 0x08) || y1 < 3)) {
		  diag += coeff[tileComp->w - 1] ? 1 : 0;
		}
	      }
	      if (x < cb->x1 - 1) {
		if (coeff[1]) {
		  ++horiz;
		  horizSign += coeff[1] < 0 ? -1 : 1;
		}
		if (y0+y1 > cb->y0) {
		  diag += coeff[-(int)tileComp->w + 1] ? 1 : 0;
		}
		if (y0+y1 < cb->y1 - 1 &&
		    (!(tileComp->codeBlockStyle & 0x08) || y1 < 3)) {
		  diag += coeff[tileComp->w + 1] ? 1 : 0;
		}
	      }
	      if (y0+y1 > cb->y0) {
		if (coeff[-(int)tileComp->w]) {
		  ++vert;
		  vertSign += coeff[-(int)tileComp->w] < 0 ? -1 : 1;
		}
	      }
	      if (y0+y1 < cb->y1 - 1 &&
		  (!(tileComp->codeBlockStyle & 0x08) || y1 < 3)) {
		if (coeff[tileComp->w]) {
		  ++vert;
		  vertSign += coeff[tileComp->w] < 0 ? -1 : 1;
		}
	      }
	      cx = sigPropContext[horiz][vert][diag][res == 0 ? 1 : sb];
	      if (cb->arithDecoder->decodeBit(cx, cb->stats)) {
		cx = signContext[horizSign][vertSign][0];
		xorBit = signContext[horizSign][vertSign][1];
		if (cb->arithDecoder->decodeBit(cx, cb->stats) ^ xorBit) {
		  *coeff = -1;
		} else {
		  *coeff = 1;
		}
	      }
	    } else {
	      *touched = 0;
	    }
	  }
	}
      }
      ++cb->len;
      // look for a segmentation symbol
      if (tileComp->codeBlockStyle & 0x20) {
	segSym = cb->arithDecoder->decodeBit(jpxContextUniform,
					     cb->stats) << 3;
	segSym |= cb->arithDecoder->decodeBit(jpxContextUniform,
					      cb->stats) << 2;
	segSym |= cb->arithDecoder->decodeBit(jpxContextUniform,
					      cb->stats) << 1;
	segSym |= cb->arithDecoder->decodeBit(jpxContextUniform,
					      cb->stats);
	if (segSym != 0x0a) {
	  // in theory this should be a fatal error, but it seems to
	  // be problematic
	  error(errSyntaxWarning, getPos(),
		"Missing or invalid segmentation symbol in JPX stream");
	}
      }
      cb->nextPass = jpxPassSigProp;
      break;
    }

    if (tileComp->codeBlockStyle & 0x02) {
      cb->stats->reset();
      cb->stats->setEntry(jpxContextSigProp, 4, 0);
      cb->stats->setEntry(jpxContextRunLength, 3, 0);
      cb->stats->setEntry(jpxContextUniform, 46, 0);
    }

    if (tileComp->codeBlockStyle & 0x04) {
      cb->arithDecoder->cleanup();
    }
  }

  cb->arithDecoder->cleanup();
  return gTrue;
}

// Inverse quantization, and wavelet transform (IDWT).  This also does
// the initial shift to convert to fixed point format.
void JPXStream::inverseTransform(JPXTileComp *tileComp) {
  JPXResLevel *resLevel;
  JPXPrecinct *precinct;
  JPXSubband *subband;
  JPXCodeBlock *cb;
  int *coeff0, *coeff;
  char *touched0, *touched;
  Guint qStyle, guard, eps, shift;
  int shift2;
  double mu;
  int val;
  Guint r, pre, cbX, cbY, x, y;

  cover(68);

  //----- (NL)LL subband (resolution level 0)

  resLevel = &tileComp->resLevels[0];

  // i-quant parameters
  qStyle = tileComp->quantStyle & 0x1f;
  guard = (tileComp->quantStyle >> 5) & 7;
  if (qStyle == 0) {
    cover(69);
    eps = (tileComp->quantSteps[0] >> 3) & 0x1f;
    shift = guard + eps - 1;
    mu = 0; // make gcc happy
  } else {
    cover(70);
    shift = guard - 1 + tileComp->prec;
    mu = (double)(0x800 + (tileComp->quantSteps[0] & 0x7ff)) / 2048.0;
  }
  if (tileComp->transform == 0) {
    cover(71);
    shift += fracBits - tileComp->prec;
  }

  // do fixed point adjustment and dequantization on (NL)LL
  for (pre = 0; pre < resLevel->nPrecincts; ++pre) {
    precinct = &resLevel->precincts[pre];
    subband = &precinct->subbands[0];
    cb = subband->cbs;
    for (cbY = 0; cbY < subband->nYCBs; ++cbY) {
      for (cbX = 0; cbX < subband->nXCBs; ++cbX) {
	for (y = cb->y0, coeff0 = cb->coeffs, touched0 = cb->touched;
	     y < cb->y1;
	     ++y, coeff0 += tileComp->w, touched0 += resLevel->cbW) {
	  for (x = cb->x0, coeff = coeff0, touched = touched0;
	       x < cb->x1;
	       ++x, ++coeff, ++touched) {
	    val = *coeff;
	    if (val != 0) {
	      shift2 = shift - (cb->nZeroBitPlanes + cb->len + *touched);
	      if (shift2 > 0) {
		cover(94);
		if (val < 0) {
		  val = (val << shift2) - (1 << (shift2 - 1));
		} else {
		  val = (val << shift2) + (1 << (shift2 - 1));
		}
	      } else {
		cover(95);
		val >>= -shift2;
	      }
	      if (qStyle == 0) {
		cover(96);
		if (tileComp->transform == 0) {
		  cover(97);
		  val &= -1 << (fracBits - tileComp->prec);
		}
	      } else {
		cover(98);
		val = (int)((double)val * mu);
	      }
	    }
	    *coeff = val;
	  }
	}
	++cb;
      }
    }
  }

  //----- IDWT for each level

  for (r = 1; r <= tileComp->nDecompLevels - reduction; ++r) {
    resLevel = &tileComp->resLevels[r];

    // (n)LL is already in the upper-left corner of the
    // tile-component data array -- interleave with (n)HL/LH/HH
    // and inverse transform to get (n-1)LL, which will be stored
    // in the upper-left corner of the tile-component data array
    inverseTransformLevel(tileComp, r, resLevel);
  }
}

// Do one level of the inverse transform:
// - take (n)LL, (n)HL, (n)LH, and (n)HH from the upper-left corner
//   of the tile-component data array
// - leave the resulting (n-1)LL in the same place
void JPXStream::inverseTransformLevel(JPXTileComp *tileComp,
				      Guint r, JPXResLevel *resLevel) {
  JPXPrecinct *precinct;
  JPXSubband *subband;
  JPXCodeBlock *cb;
  int *coeff0, *coeff;
  char *touched0, *touched;
  Guint qStyle, guard, eps, shift, t;
  int shift2;
  double mu;
  int val;
  int *dataPtr, *bufPtr;
  Guint nx1, nx2, ny1, ny2, offset;
  Guint x, y, sb, pre, cbX, cbY;

  qStyle = tileComp->quantStyle & 0x1f;
  guard = (tileComp->quantStyle >> 5) & 7;

  //----- compute subband bounds

  //    0   nx1  nx2
  //    |    |    |
  //    v    v    v
  //   +----+----+
  //   | LL | HL | <- 0
  //   +----+----+
  //   | LH | HH | <- ny1
  //   +----+----+
  //               <- ny2
  nx1 = resLevel->bx1[1] - resLevel->bx0[1];
  nx2 = nx1 + resLevel->bx1[0] - resLevel->bx0[0];
  ny1 = resLevel->by1[0] - resLevel->by0[0];
  ny2 = ny1 + resLevel->by1[1] - resLevel->by0[1];
  if (nx2 == 0 || ny2 == 0) {
    return;
  }

  //----- fixed-point adjustment and dequantization

  for (sb = 0; sb < 3; ++sb) {

    // i-quant parameters
    if (qStyle == 0) {
      cover(100);
      eps = (tileComp->quantSteps[3*r - 2 + sb] >> 3) & 0x1f;
      shift = guard + eps - 1;
      mu = 0; // make gcc happy
    } else {
      cover(101);
      shift = guard + tileComp->prec;
      if (sb == 2) {
	cover(102);
	++shift;
      }
      t = tileComp->quantSteps[qStyle == 1 ? 0 : (3*r - 2 + sb)];
      mu = (double)(0x800 + (t & 0x7ff)) / 2048.0;
    }
    if (tileComp->transform == 0) {
      cover(103);
      shift += fracBits - tileComp->prec;
    }

    // fixed point adjustment and dequantization

    for (pre = 0; pre < resLevel->nPrecincts; ++pre) {
      precinct = &resLevel->precincts[pre];
      subband = &precinct->subbands[sb];
      cb = subband->cbs;
      for (cbY = 0; cbY < subband->nYCBs; ++cbY) {
	for (cbX = 0; cbX < subband->nXCBs; ++cbX) {
	  for (y = cb->y0, coeff0 = cb->coeffs, touched0 = cb->touched;
	       y < cb->y1;
	       ++y, coeff0 += tileComp->w, touched0 += resLevel->cbW) {
	    for (x = cb->x0, coeff = coeff0, touched = touched0;
		 x < cb->x1;
		 ++x, ++coeff, ++touched) {
	      val = *coeff;
	      if (val != 0) {
		shift2 = shift - (cb->nZeroBitPlanes + cb->len + *touched);
		if (shift2 > 0) {
		  cover(74);
		  if (val < 0) {
		    val = (val << shift2) - (1 << (shift2 - 1));
		  } else {
		    val = (val << shift2) + (1 << (shift2 - 1));
		  }
		} else {
		  cover(75);
		  val >>= -shift2;
		}
		if (qStyle == 0) {
		  cover(76);
		  if (tileComp->transform == 0) {
		    val &= -1 << (fracBits - tileComp->prec);
		  }
		} else {
		  cover(77);
		  val = (int)((double)val * mu);
		}
	      }
	      *coeff = val;
	    }
	  }
	  ++cb;
	}
      }
    }
  }

  //----- inverse transform

  // horizontal (row) transforms
  offset = 3 + (resLevel->x0 & 1);
  for (y = 0, dataPtr = tileComp->data; y < ny2; ++y, dataPtr += tileComp->w) {
    if (resLevel->bx0[0] == resLevel->bx0[1]) {
      // fetch LL/LH
      for (x = 0, bufPtr = tileComp->buf + offset;
	   x < nx1;
	   ++x, bufPtr += 2) {
	*bufPtr = dataPtr[x];
      }
      // fetch HL/HH
      for (x = nx1, bufPtr = tileComp->buf + offset + 1;
	   x < nx2;
	   ++x, bufPtr += 2) {
	*bufPtr = dataPtr[x];
      }
    } else {
      // fetch LL/LH
      for (x = 0, bufPtr = tileComp->buf + offset + 1;
	   x < nx1;
	   ++x, bufPtr += 2) {
	*bufPtr = dataPtr[x];
      }
      // fetch HL/HH
      for (x = nx1, bufPtr = tileComp->buf + offset;
	   x < nx2;
	   ++x, bufPtr += 2) {
	*bufPtr = dataPtr[x];
      }
    }
    inverseTransform1D(tileComp, tileComp->buf, offset, nx2);
    for (x = 0, bufPtr = tileComp->buf + offset; x < nx2; ++x, ++bufPtr) {
      dataPtr[x] = *bufPtr;
    }
  }

  // vertical (column) transforms
  offset = 3 + (resLevel->y0 & 1);
  for (x = 0, dataPtr = tileComp->data; x < nx2; ++x, ++dataPtr) {
    if (resLevel->by0[0] == resLevel->by0[1]) {
      // fetch LL/HL
      for (y = 0, bufPtr = tileComp->buf + offset;
	   y < ny1;
	   ++y, bufPtr += 2) {
	*bufPtr = dataPtr[y * tileComp->w];
      }
      // fetch LH/HH
      for (y = ny1, bufPtr = tileComp->buf + offset + 1;
	   y < ny2;
	   ++y, bufPtr += 2) {
	*bufPtr = dataPtr[y * tileComp->w];
      }
    } else {
      // fetch LL/HL
      for (y = 0, bufPtr = tileComp->buf + offset + 1;
	   y < ny1;
	   ++y, bufPtr += 2) {
	*bufPtr = dataPtr[y * tileComp->w];
      }
      // fetch LH/HH
      for (y = ny1, bufPtr = tileComp->buf + offset;
	   y < ny2;
	   ++y, bufPtr += 2) {
	*bufPtr = dataPtr[y * tileComp->w];
      }
    }
    inverseTransform1D(tileComp, tileComp->buf, offset, ny2);
    for (y = 0, bufPtr = tileComp->buf + offset; y < ny2; ++y, ++bufPtr) {
      dataPtr[y * tileComp->w] = *bufPtr;
    }
  }
}

void JPXStream::inverseTransform1D(JPXTileComp *tileComp, int *data,
				   Guint offset, Guint n) {
  Guint end, i;

  //----- special case for length = 1
  if (n == 1) {
    cover(79);
    if (offset == 4) {
      cover(104);
      *data >>= 1;
    }

  } else {
    cover(80);

    end = offset + n;

    //----- extend right
    data[end] = data[end - 2];
    if (n == 2) {
      cover(81);
      data[end+1] = data[offset + 1];
      data[end+2] = data[offset];
      data[end+3] = data[offset + 1];
    } else {
      cover(82);
      data[end+1] = data[end - 3];
      if (n == 3) {
	cover(105);
	data[end+2] = data[offset + 1];
	data[end+3] = data[offset + 2];
      } else {
	cover(106);
	data[end+2] = data[end - 4];
	if (n == 4) {
	  cover(107);
	  data[end+3] = data[offset + 1];
	} else {
	  cover(108);
	  data[end+3] = data[end - 5];
	}
      }
    }

    //----- extend left
    data[offset - 1] = data[offset + 1];
    data[offset - 2] = data[offset + 2];
    data[offset - 3] = data[offset + 3];
    if (offset == 4) {
      cover(83);
      data[0] = data[offset + 4];
    }

    //----- 9-7 irreversible filter

    if (tileComp->transform == 0) {
      cover(84);
      // step 1 (even)
      for (i = 1; i <= end + 2; i += 2) {
	data[i] = (int)(idwtKappa * data[i]);
      }
      // step 2 (odd)
      for (i = 0; i <= end + 3; i += 2) {
	data[i] = (int)(idwtIKappa * data[i]);
      }
      // step 3 (even)
      for (i = 1; i <= end + 2; i += 2) {
	data[i] = (int)(data[i] - idwtDelta * (data[i-1] + data[i+1]));
      }
      // step 4 (odd)
      for (i = 2; i <= end + 1; i += 2) {
	data[i] = (int)(data[i] - idwtGamma * (data[i-1] + data[i+1]));
      }
      // step 5 (even)
      for (i = 3; i <= end; i += 2) {
	data[i] = (int)(data[i] - idwtBeta * (data[i-1] + data[i+1]));
      }
      // step 6 (odd)
      for (i = 4; i <= end - 1; i += 2) {
	data[i] = (int)(data[i] - idwtAlpha * (data[i-1] + data[i+1]));
      }

    //----- 5-3 reversible filter

    } else {
      cover(85);
      // step 1 (even)
      for (i = 3; i <= end; i += 2) {
	data[i] -= (data[i-1] + data[i+1] + 2) >> 2;
      }
      // step 2 (odd)
      for (i = 4; i < end; i += 2) {
	data[i] += (data[i-1] + data[i+1]) >> 1;
      }
    }
  }
}

// Inverse multi-component transform and DC level shift.  This also
// converts fixed point samples back to integers.
GBool JPXStream::inverseMultiCompAndDC(JPXTile *tile) {
  JPXTileComp *tileComp;
  int coeff, d0, d1, d2, t, minVal, maxVal, zeroVal;
  int *dataPtr;
  Guint j, comp, x, y;

  //----- inverse multi-component transform

  if (tile->multiComp == 1) {
    cover(86);
    if (img.nComps < 3 ||
	tile->tileComps[0].hSep != tile->tileComps[1].hSep ||
	tile->tileComps[0].vSep != tile->tileComps[1].vSep ||
	tile->tileComps[1].hSep != tile->tileComps[2].hSep ||
	tile->tileComps[1].vSep != tile->tileComps[2].vSep) {
      return gFalse;
    }

    // inverse irreversible multiple component transform
    if (tile->tileComps[0].transform == 0) {
      cover(87);
      j = 0;
      for (y = 0; y < tile->tileComps[0].h; ++y) {
	for (x = 0; x < tile->tileComps[0].w; ++x) {
	  d0 = tile->tileComps[0].data[j];
	  d1 = tile->tileComps[1].data[j];
	  d2 = tile->tileComps[2].data[j];
	  tile->tileComps[0].data[j] = (int)(d0 + 1.402 * d2 + 0.5);
	  tile->tileComps[1].data[j] =
	      (int)(d0 - 0.34413 * d1 - 0.71414 * d2 + 0.5);
	  tile->tileComps[2].data[j] = (int)(d0 + 1.772 * d1 + 0.5);
	  ++j;
	}
      }

    // inverse reversible multiple component transform
    } else {
      cover(88);
      j = 0;
      for (y = 0; y < tile->tileComps[0].h; ++y) {
	for (x = 0; x < tile->tileComps[0].w; ++x) {
	  d0 = tile->tileComps[0].data[j];
	  d1 = tile->tileComps[1].data[j];
	  d2 = tile->tileComps[2].data[j];
	  tile->tileComps[1].data[j] = t = d0 - ((d2 + d1) >> 2);
	  tile->tileComps[0].data[j] = d2 + t;
	  tile->tileComps[2].data[j] = d1 + t;
	  ++j;
	}
      }
    }
  }

  //----- DC level shift
  for (comp = 0; comp < img.nComps; ++comp) {
    tileComp = &tile->tileComps[comp];

    // signed: clip
    if (tileComp->sgned) {
      cover(89);
      minVal = -(1 << (tileComp->prec - 1));
      maxVal = (1 << (tileComp->prec - 1)) - 1;
      dataPtr = tileComp->data;
      for (y = 0; y < tileComp->h; ++y) {
	for (x = 0; x < tileComp->w; ++x) {
	  coeff = *dataPtr;
	  if (tileComp->transform == 0) {
	    cover(109);
	    coeff >>= fracBits - tileComp->prec;
	  }
	  if (coeff < minVal) {
	    cover(110);
	    coeff = minVal;
	  } else if (coeff > maxVal) {
	    cover(111);
	    coeff = maxVal;
	  }
	  *dataPtr++ = coeff;
	}
      }

    // unsigned: inverse DC level shift and clip
    } else {
      cover(90);
      maxVal = (1 << tileComp->prec) - 1;
      zeroVal = 1 << (tileComp->prec - 1);
      dataPtr = tileComp->data;
      for (y = 0; y < tileComp->h; ++y) {
	for (x = 0; x < tileComp->w; ++x) {
	  coeff = *dataPtr;
	  if (tileComp->transform == 0) {
	    cover(112);
	    coeff >>= fracBits - tileComp->prec;
	  }
	  coeff += zeroVal;
	  if (coeff < 0) {
	    cover(113);
	    coeff = 0;
	  } else if (coeff > maxVal) {
	    cover(114);
	    coeff = maxVal;
	  }
	  *dataPtr++ = coeff;
	}
      }
    }
  }

  return gTrue;
}

GBool JPXStream::readBoxHdr(Guint *boxType, Guint *boxLen, Guint *dataLen) {
  Guint len, lenH;

  if (!readULong(&len) ||
      !readULong(boxType)) {
    return gFalse;
  }
  if (len == 1) {
    if (!readULong(&lenH) || !readULong(&len)) {
      return gFalse;
    }
    if (lenH) {
      error(errSyntaxError, getPos(),
	    "JPX stream contains a box larger than 2^32 bytes");
      return gFalse;
    }
    *boxLen = len;
    *dataLen = len - 16;
  } else if (len == 0) {
    *boxLen = 0;
    *dataLen = 0;
  } else {
    *boxLen = len;
    *dataLen = len - 8;
  }
  return gTrue;
}

int JPXStream::readMarkerHdr(int *segType, Guint *segLen) {
  int c;

  do {
    do {
      if ((c = bufStr->getChar()) == EOF) {
	return gFalse;
      }
    } while (c != 0xff);
    do {
      if ((c = bufStr->getChar()) == EOF) {
	return gFalse;
      }
    } while (c == 0xff);
  } while (c == 0x00);
  *segType = c;
  if ((c >= 0x30 && c <= 0x3f) ||
      c == 0x4f || c == 0x92 || c == 0x93 || c == 0xd9) {
    *segLen = 0;
    return gTrue;
  }
  return readUWord(segLen);
}

GBool JPXStream::readUByte(Guint *x) {
  int c0;

  if ((c0 = bufStr->getChar()) == EOF) {
    return gFalse;
  }
  *x = (Guint)c0;
  return gTrue;
}

GBool JPXStream::readByte(int *x) {
 int c0;

  if ((c0 = bufStr->getChar()) == EOF) {
    return gFalse;
  }
  *x = c0;
  if (c0 & 0x80) {
    *x |= -1 - 0xff;
  }
  return gTrue;
}

GBool JPXStream::readUWord(Guint *x) {
  int c0, c1;

  if ((c0 = bufStr->getChar()) == EOF ||
      (c1 = bufStr->getChar()) == EOF) {
    return gFalse;
  }
  *x = (Guint)((c0 << 8) | c1);
  return gTrue;
}

GBool JPXStream::readULong(Guint *x) {
  int c0, c1, c2, c3;

  if ((c0 = bufStr->getChar()) == EOF ||
      (c1 = bufStr->getChar()) == EOF ||
      (c2 = bufStr->getChar()) == EOF ||
      (c3 = bufStr->getChar()) == EOF) {
    return gFalse;
  }
  *x = (Guint)((c0 << 24) | (c1 << 16) | (c2 << 8) | c3);
  return gTrue;
}

GBool JPXStream::readNBytes(int nBytes, GBool signd, int *x) {
  int y, c, i;

  y = 0;
  for (i = 0; i < nBytes; ++i) {
    if ((c = bufStr->getChar()) == EOF) {
      return gFalse;
    }
    y = (y << 8) + c;
  }
  if (signd) {
    if (y & (1 << (8 * nBytes - 1))) {
      y |= -1 << (8 * nBytes);
    }
  }
  *x = y;
  return gTrue;
}

void JPXStream::startBitBuf(Guint byteCountA) {
  bitBufLen = 0;
  bitBufSkip = gFalse;
  byteCount = byteCountA;
}

GBool JPXStream::readBits(int nBits, Guint *x) {
  int c;

  while (bitBufLen < nBits) {
    if (byteCount == 0 || (c = bufStr->getChar()) == EOF) {
      return gFalse;
    }
    --byteCount;
    if (bitBufSkip) {
      bitBuf = (bitBuf << 7) | (c & 0x7f);
      bitBufLen += 7;
    } else {
      bitBuf = (bitBuf << 8) | (c & 0xff);
      bitBufLen += 8;
    }
    bitBufSkip = c == 0xff;
  }
  *x = (bitBuf >> (bitBufLen - nBits)) & ((1 << nBits) - 1);
  bitBufLen -= nBits;
  return gTrue;
}

void JPXStream::skipSOP() {
  // SOP occurs at the start of the packet header, so we don't need to
  // worry about bit-stuff prior to it
  if (byteCount >= 6 &&
      bufStr->lookChar(0) == 0xff &&
      bufStr->lookChar(1) == 0x91) {
    bufStr->discardChars(6);
    byteCount -= 6;
    bitBufLen = 0;
    bitBufSkip = gFalse;
  }
}

void JPXStream::skipEPH() {
  int k;

  k = bitBufSkip ? 1 : 0;
  if (byteCount >= (Guint)(k + 2) &&
      bufStr->lookChar(k) == 0xff &&
      bufStr->lookChar(k + 1) == 0x92) {
    bufStr->discardChars(k + 2);
    byteCount -= k + 2;
    bitBufLen = 0;
    bitBufSkip = gFalse;
  }
}

Guint JPXStream::finishBitBuf() {
  if (bitBufSkip) {
    bufStr->getChar();
    --byteCount;
  }
  return byteCount;
}
