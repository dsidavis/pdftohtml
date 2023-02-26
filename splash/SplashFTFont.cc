//========================================================================
//
// SplashFTFont.cc
//
// Copyright 2003-2013 Glyph & Cog, LLC
//
//========================================================================

#include <aconf.h>

#if HAVE_FREETYPE_H

#ifdef USE_GCC_PRAGMAS
#pragma implementation
#endif

#include <ft2build.h>
#include FT_OUTLINE_H
#include FT_SIZES_H
#include FT_GLYPH_H
#include "gmem.h"
#include "gmempp.h"
#include "SplashMath.h"
#include "SplashGlyphBitmap.h"
#include "SplashPath.h"
#include "SplashFontEngine.h"
#include "SplashFTFontEngine.h"
#include "SplashFTFontFile.h"
#include "SplashFTFont.h"

//------------------------------------------------------------------------

static int glyphPathMoveTo(const FT_Vector *pt, void *path);
static int glyphPathLineTo(const FT_Vector *pt, void *path);
static int glyphPathConicTo(const FT_Vector *ctrl, const FT_Vector *pt,
			    void *path);
static int glyphPathCubicTo(const FT_Vector *ctrl1, const FT_Vector *ctrl2,
			    const FT_Vector *pt, void *path);

//------------------------------------------------------------------------
// SplashFTFont
//------------------------------------------------------------------------

SplashFTFont::SplashFTFont(SplashFTFontFile *fontFileA, SplashCoord *matA,
			   SplashCoord *textMatA):
  SplashFont(fontFileA, matA, textMatA, fontFileA->engine->aa)
{
  FT_Face face;
  int size, div;
  int x, y;
#if USE_FIXEDPOINT
  SplashCoord scale;
#endif

  face = fontFileA->face;
  if (FT_New_Size(face, &sizeObj)) {
    return;
  }
  face->size = sizeObj;
  size = splashRound(splashDist(0, 0, mat[2], mat[3]));
  if (size < 1) {
    size = 1;
  }
  if (FT_Set_Pixel_Sizes(face, 0, size)) {
    return;
  }
  // if the textMat values are too small, FreeType's fixed point
  // arithmetic doesn't work so well
  textScale = splashDist(0, 0, textMat[2], textMat[3]) / size;
  // avoid problems with singular (or close-to-singular) matrices
  if (textScale < 0.00001) {
    textScale = 0.00001;
  }

  div = face->bbox.xMax > 20000 ? 65536 : 1;

#if USE_FIXEDPOINT
  scale = (SplashCoord)1 / (SplashCoord)face->units_per_EM;

  // transform the four corners of the font bounding box -- the min
  // and max values form the bounding box of the transformed font
  x = (int)(mat[0] * (scale * (face->bbox.xMin / div)) +
	    mat[2] * (scale * (face->bbox.yMin / div)));
  xMin = xMax = x;
  y = (int)(mat[1] * (scale * (face->bbox.xMin / div)) +
	    mat[3] * (scale * (face->bbox.yMin / div)));
  yMin = yMax = y;
  x = (int)(mat[0] * (scale * (face->bbox.xMin / div)) +
	    mat[2] * (scale * (face->bbox.yMax / div)));
  if (x < xMin) {
    xMin = x;
  } else if (x > xMax) {
    xMax = x;
  }
  y = (int)(mat[1] * (scale * (face->bbox.xMin / div)) +
	    mat[3] * (scale * (face->bbox.yMax / div)));
  if (y < yMin) {
    yMin = y;
  } else if (y > yMax) {
    yMax = y;
  }
  x = (int)(mat[0] * (scale * (face->bbox.xMax / div)) +
	    mat[2] * (scale * (face->bbox.yMin / div)));
  if (x < xMin) {
    xMin = x;
  } else if (x > xMax) {
    xMax = x;
  }
  y = (int)(mat[1] * (scale * (face->bbox.xMax / div)) +
	    mat[3] * (scale * (face->bbox.yMin / div)));
  if (y < yMin) {
    yMin = y;
  } else if (y > yMax) {
    yMax = y;
  }
  x = (int)(mat[0] * (scale * (face->bbox.xMax / div)) +
	    mat[2] * (scale * (face->bbox.yMax / div)));
  if (x < xMin) {
    xMin = x;
  } else if (x > xMax) {
    xMax = x;
  }
  y = (int)(mat[1] * (scale * (face->bbox.xMax / div)) +
	    mat[3] * (scale * (face->bbox.yMax / div)));
  if (y < yMin) {
    yMin = y;
  } else if (y > yMax) {
    yMax = y;
  }
#else // USE_FIXEDPOINT
  // transform the four corners of the font bounding box -- the min
  // and max values form the bounding box of the transformed font
  x = (int)((mat[0] * (SplashCoord)face->bbox.xMin
	     + mat[2] * (SplashCoord)face->bbox.yMin) /
	    (div * face->units_per_EM));
  xMin = xMax = x;
  y = (int)((mat[1] * (SplashCoord)face->bbox.xMin
	     + mat[3] * (SplashCoord)face->bbox.yMin) /
	    (div * face->units_per_EM));
  yMin = yMax = y;
  x = (int)((mat[0] * (SplashCoord)face->bbox.xMin
	     + mat[2] * (SplashCoord)face->bbox.yMax) /
	    (div * face->units_per_EM));
  if (x < xMin) {
    xMin = x;
  } else if (x > xMax) {
    xMax = x;
  }
  y = (int)((mat[1] * (SplashCoord)face->bbox.xMin
	     + mat[3] * (SplashCoord)face->bbox.yMax) /
	    (div * face->units_per_EM));
  if (y < yMin) {
    yMin = y;
  } else if (y > yMax) {
    yMax = y;
  }
  x = (int)((mat[0] * (SplashCoord)face->bbox.xMax
	     + mat[2] * (SplashCoord)face->bbox.yMin) /
	    (div * face->units_per_EM));
  if (x < xMin) {
    xMin = x;
  } else if (x > xMax) {
    xMax = x;
  }
  y = (int)((mat[1] * (SplashCoord)face->bbox.xMax
	     + mat[3] * (SplashCoord)face->bbox.yMin) /
	    (div * face->units_per_EM));
  if (y < yMin) {
    yMin = y;
  } else if (y > yMax) {
    yMax = y;
  }
  x = (int)((mat[0] * (SplashCoord)face->bbox.xMax
	     + mat[2] * (SplashCoord)face->bbox.yMax) /
	    (div * face->units_per_EM));
  if (x < xMin) {
    xMin = x;
  } else if (x > xMax) {
    xMax = x;
  }
  y = (int)((mat[1] * (SplashCoord)face->bbox.xMax
	     + mat[3] * (SplashCoord)face->bbox.yMax) /
	    (div * face->units_per_EM));
  if (y < yMin) {
    yMin = y;
  } else if (y > yMax) {
    yMax = y;
  }
#endif // USE_FIXEDPOINT
  // This is a kludge: some buggy PDF generators embed fonts with
  // zero bounding boxes.
  if (xMax == xMin) {
    xMin = 0;
    xMax = size;
  }
  if (yMax == yMin) {
    yMin = 0;
    yMax = (int)((SplashCoord)1.2 * size);
  }

  // compute the transform matrix
#if USE_FIXEDPOINT
  matrix.xx = (FT_Fixed)((mat[0] / size).get16Dot16());
  matrix.yx = (FT_Fixed)((mat[1] / size).get16Dot16());
  matrix.xy = (FT_Fixed)((mat[2] / size).get16Dot16());
  matrix.yy = (FT_Fixed)((mat[3] / size).get16Dot16());
  textMatrix.xx = (FT_Fixed)((textMat[0] / (textScale * size)).get16Dot16());
  textMatrix.yx = (FT_Fixed)((textMat[1] / (textScale * size)).get16Dot16());
  textMatrix.xy = (FT_Fixed)((textMat[2] / (textScale * size)).get16Dot16());
  textMatrix.yy = (FT_Fixed)((textMat[3] / (textScale * size)).get16Dot16());
#else
  matrix.xx = (FT_Fixed)((mat[0] / size) * 65536);
  matrix.yx = (FT_Fixed)((mat[1] / size) * 65536);
  matrix.xy = (FT_Fixed)((mat[2] / size) * 65536);
  matrix.yy = (FT_Fixed)((mat[3] / size) * 65536);
  textMatrix.xx = (FT_Fixed)((textMat[0] / (textScale * size)) * 65536);
  textMatrix.yx = (FT_Fixed)((textMat[1] / (textScale * size)) * 65536);
  textMatrix.xy = (FT_Fixed)((textMat[2] / (textScale * size)) * 65536);
  textMatrix.yy = (FT_Fixed)((textMat[3] / (textScale * size)) * 65536);
#endif
}

SplashFTFont::~SplashFTFont() {
}

GBool SplashFTFont::getGlyph(int c, int xFrac, int yFrac,
			     SplashGlyphBitmap *bitmap) {
  return SplashFont::getGlyph(c, xFrac, 0, bitmap);
}

GBool SplashFTFont::makeGlyph(int c, int xFrac, int yFrac,
			      SplashGlyphBitmap *bitmap) {
  SplashFTFontFile *ff;
  FT_Vector offset;
  FT_GlyphSlot slot;
  int gid;
  FT_Int32 flags;
  int rowSize;
  Guchar *p, *q;
  int i;

  ff = (SplashFTFontFile *)fontFile;

  ff->face->size = sizeObj;
  offset.x = (FT_Pos)(int)((SplashCoord)xFrac * splashFontFractionMul * 64);
  offset.y = 0;
  FT_Set_Transform(ff->face, &matrix, &offset);
  slot = ff->face->glyph;

  if (ff->codeToGID && c < ff->codeToGIDLen) {
    gid = ff->codeToGID[c];
  } else {
    gid = c;
  }
  if (ff->fontType == splashFontTrueType && gid < 0) {
    // skip the TrueType notdef glyph
    return gFalse;
  }

  // Set up the load flags:
  // * disable bitmaps because they look ugly when scaled, rotated,
  //   etc.
  // * disable autohinting because it can fail badly with font subsets
  //   that use invalid glyph names (the FreeType autohinter depends
  //   on the glyph name to figure out how to autohint the glyph)
  // * but enable light autohinting for Type 1 fonts because regular
  //   hinting looks pretty bad, and the invalid glyph name issue
  //   seems to be very rare (Type 1 fonts are mostly used for
  //   substitution, in which case the full font is being used, which
  //   means we have the glyph names)
  // This also sets the "pedantic" flag, running the FreeType hinter
  // in paranoid mode.  If that triggers any errors, we disable
  // hinting below.
  flags = FT_LOAD_NO_BITMAP | FT_LOAD_PEDANTIC;
  if (ff->engine->flags & splashFTNoHinting) {
    flags |= FT_LOAD_NO_HINTING;
  } else if (ff->fontType == splashFontType1) {
    flags |= FT_LOAD_TARGET_LIGHT;
  } else {
    flags |= FT_LOAD_NO_AUTOHINT;
  }
  if (FT_Load_Glyph(ff->face, (FT_UInt)gid, flags)) {
    // fonts with broken hinting instructions can cause errors here;
    // try again with no hinting (this is probably only relevant for
    // TrueType fonts)
    flags = FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING;
    if (FT_Load_Glyph(ff->face, (FT_UInt)gid, flags)) {
      return gFalse;
    }
  }
  if (FT_Render_Glyph(slot, aa ? FT_RENDER_MODE_NORMAL
		               : FT_RENDER_MODE_MONO)) {
    return gFalse;
  }
  if (slot->bitmap.width == 0 || slot->bitmap.rows == 0) {
    // this can happen if (a) the glyph is really tiny or (b) the
    // metrics in the TrueType file are broken
    return gFalse;
  }

  bitmap->x = -slot->bitmap_left;
  bitmap->y = slot->bitmap_top;
  bitmap->w = slot->bitmap.width;
  bitmap->h = slot->bitmap.rows;
  bitmap->aa = aa;
  if (aa) {
    rowSize = bitmap->w;
  } else {
    rowSize = (bitmap->w + 7) >> 3;
  }
  bitmap->data = (Guchar *)gmallocn(bitmap->h, rowSize);
  bitmap->freeData = gTrue;
  for (i = 0, p = bitmap->data, q = slot->bitmap.buffer;
       i < bitmap->h;
       ++i, p += rowSize, q += slot->bitmap.pitch) {
    memcpy(p, q, rowSize);
  }

  return gTrue;
}

struct SplashFTFontPath {
  SplashPath *path;
  SplashCoord textScale;
  GBool needClose;
};

SplashPath *SplashFTFont::getGlyphPath(int c) {
  static FT_Outline_Funcs outlineFuncs = {
#if FREETYPE_MINOR <= 1
    (int (*)(FT_Vector *, void *))&glyphPathMoveTo,
    (int (*)(FT_Vector *, void *))&glyphPathLineTo,
    (int (*)(FT_Vector *, FT_Vector *, void *))&glyphPathConicTo,
    (int (*)(FT_Vector *, FT_Vector *, FT_Vector *, void *))&glyphPathCubicTo,
#else
    &glyphPathMoveTo,
    &glyphPathLineTo,
    &glyphPathConicTo,
    &glyphPathCubicTo,
#endif
    0, 0
  };
  SplashFTFontFile *ff;
  SplashFTFontPath path;
  FT_GlyphSlot slot;
  int gid;
  FT_Glyph glyph;

  ff = (SplashFTFontFile *)fontFile;
  ff->face->size = sizeObj;
  FT_Set_Transform(ff->face, &textMatrix, NULL);
  slot = ff->face->glyph;
  if (ff->codeToGID && c < ff->codeToGIDLen) {
    gid = ff->codeToGID[c];
  } else {
    gid = c;
  }
  if (ff->fontType == splashFontTrueType && gid < 0) {
    // skip the TrueType notdef glyph
    return NULL;
  }
  if (FT_Load_Glyph(ff->face, (FT_UInt)gid, FT_LOAD_NO_BITMAP)) {
    // fonts with broken hinting instructions can cause errors here;
    // try again with no hinting (this is probably only relevant for
    // TrueType fonts)
    if (FT_Load_Glyph(ff->face, (FT_UInt)gid,
		      FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING)) {
      return NULL;
    }
  }
  if (FT_Get_Glyph(slot, &glyph)) {
    return NULL;
  }
  path.path = new SplashPath();
  path.textScale = textScale;
  path.needClose = gFalse;
  FT_Outline_Decompose(&((FT_OutlineGlyph)glyph)->outline,
		       &outlineFuncs, &path);
  if (path.needClose) {
    path.path->close();
  }
  FT_Done_Glyph(glyph);
  return path.path;
}

static int glyphPathMoveTo(const FT_Vector *pt, void *path) {
  SplashFTFontPath *p = (SplashFTFontPath *)path;

  if (p->needClose) {
    p->path->close();
    p->needClose = gFalse;
  }
  p->path->moveTo((SplashCoord)pt->x * p->textScale / 64.0,
		  (SplashCoord)pt->y * p->textScale / 64.0);
  return 0;
}

static int glyphPathLineTo(const FT_Vector *pt, void *path) {
  SplashFTFontPath *p = (SplashFTFontPath *)path;

  p->path->lineTo((SplashCoord)pt->x * p->textScale / 64.0,
		  (SplashCoord)pt->y * p->textScale / 64.0);
  p->needClose = gTrue;
  return 0;
}

static int glyphPathConicTo(const FT_Vector *ctrl, const FT_Vector *pt,
			    void *path) {
  SplashFTFontPath *p = (SplashFTFontPath *)path;
  SplashCoord x0, y0, x1, y1, x2, y2, x3, y3, xc, yc;

  if (!p->path->getCurPt(&x0, &y0)) {
    return 0;
  }
  xc = (SplashCoord)ctrl->x * p->textScale / 64.0;
  yc = (SplashCoord)ctrl->y * p->textScale / 64.0;
  x3 = (SplashCoord)pt->x * p->textScale / 64.0;
  y3 = (SplashCoord)pt->y * p->textScale / 64.0;

  // A second-order Bezier curve is defined by two endpoints, p0 and
  // p3, and one control point, pc:
  //
  //     p(t) = (1-t)^2*p0 + t*(1-t)*pc + t^2*p3
  //
  // A third-order Bezier curve is defined by the same two endpoints,
  // p0 and p3, and two control points, p1 and p2:
  //
  //     p(t) = (1-t)^3*p0 + 3t*(1-t)^2*p1 + 3t^2*(1-t)*p2 + t^3*p3
  //
  // Applying some algebra, we can convert a second-order curve to a
  // third-order curve:
  //
  //     p1 = (1/3) * (p0 + 2pc)
  //     p2 = (1/3) * (2pc + p3)

  x1 = (SplashCoord)(1.0 / 3.0) * (x0 + (SplashCoord)2 * xc);
  y1 = (SplashCoord)(1.0 / 3.0) * (y0 + (SplashCoord)2 * yc);
  x2 = (SplashCoord)(1.0 / 3.0) * ((SplashCoord)2 * xc + x3);
  y2 = (SplashCoord)(1.0 / 3.0) * ((SplashCoord)2 * yc + y3);

  p->path->curveTo(x1, y1, x2, y2, x3, y3);
  p->needClose = gTrue;
  return 0;
}

static int glyphPathCubicTo(const FT_Vector *ctrl1, const FT_Vector *ctrl2,
			    const FT_Vector *pt, void *path) {
  SplashFTFontPath *p = (SplashFTFontPath *)path;

  p->path->curveTo((SplashCoord)ctrl1->x * p->textScale / 64.0,
		   (SplashCoord)ctrl1->y * p->textScale / 64.0,
		   (SplashCoord)ctrl2->x * p->textScale / 64.0,
		   (SplashCoord)ctrl2->y * p->textScale / 64.0,
		   (SplashCoord)pt->x * p->textScale / 64.0,
		   (SplashCoord)pt->y * p->textScale / 64.0);
  p->needClose = gTrue;
  return 0;
}

#endif // HAVE_FREETYPE_H
