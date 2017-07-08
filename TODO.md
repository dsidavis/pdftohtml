# Todo List

##

+ Write TIFF  images out.

 
+ The links are wrong in
  0045901437/Heymann-1998-Re-emergence of Monkeypox in Afri.pdf
  i.e the Download link and the text for the link are out of sync.


+ Escape entity characters in fontName in src/HtmlFonts.cc
  Entities - replace & with &amp;    HtmlFont::HtmlFilter seems to be only place that addresses this.
 See /Users/duncan/DSIProjects/Zoonotics/NewData_Feb2017/Zoo_02_02_2017 Copy.Data/PDF/3793185613

 /Users/duncan/DSIProjects/Zoonotics/NewData_Feb2017/Zoo_02_02_2017 Copy.Data/PDF/2841880783 for

+ Generating invalid characters, e.g., ~/DSIProjects/Zoonotics/NewData_Feb2017/Zoo_02_02_2017 Copy.Data/PDF/4068053084/.  These are mathematical symbols.
   See HtmlFonts.cc  HtmlFont::HtmlFilter  

+ < and > in an entity.


+ SegFault   clearGList()
 These seem to be figures that generate lots of paths/coords.
 And I probably am not dealing with the memory management properly.
 HtmlOutputDev.cc:1004 - two routines we instantiate. Called @1037 in HtmlPage::clear()
  eg. ~/DSIProjects/Zoonotics/NewData_Feb2017/Zoo_02_02_2017 Copy.Data/PDF/3704354175/
  Also producing 200 Mb from a small pdf.
  /Users/duncan/DSIProjects/Zoonotics/NewData_Feb2017/Zoo_02_02_2017 Copy.Data/PDF/3419661726
  also huge XML file.

  doRadialShFill is common to both.
  Also - Error: PDF version 1.6 -- xpdf supports version 1.5 (continuing anyway)
 
  Also
   /Users/duncan/DSIProjects/Zoonotics/NewData_Feb2017/Zoo_02_02_2017 Copy.Data/PDF/2717564992


+ Have the coalesce respect changes in fonts, colors, rotation, etc.!

+ Why do we get the rectangle  twice in plot.pdf in  ReadPDF/inst/samples/

+ Fix the coalesce() routine and the fixFnt and <span/>. 
   For now it is disabled.
   We seem to be adding span elements even when the font isn't changing - it is being set, but still the same.

+ [check] Fix all the fontspecs in the XML that are the same.
  There is extra information about italic, bold, oblique,  that we weren't emitting and so they may well have been unique.


   Memory management.


+ In sample.pdf created by R, we are getting text elements with a value of q.
   Can we do better?
   These are the points in the plot.  These are in font F1 which is ZapfDingbats

+ [check] Get colors and line width, styles, etc of rectangles, lines etc.

  Dashes on lines.

  fill and line colors

  out device is notified of a change to colors with updateStrokeColor(), updateFillColor().
  Doesn't say which type of color it is. 
    The GfxColorSpace has a getMode() and this tells us what it is. We can even get the name.
    But with the plot.pdf which has an /sRGB

  Stroke versus fill


## Verify

+ [verify] Recognize spacing between strings - see LatestDocs/PDF/1834853125/394.full.pdf ResearchGate pages.
  See word.pdf.
  With -coalesce toggled off, we get the individual characters along with
   an entry with no character.
  Seems like we have a \t so convert it to a space for now.


  

## Done

+ [done] Info about Images 
  See img.tex/pdf.  We have a 520x700 image of shields library.
  On each of page 2, 3, 4 we change its size on the page. We report the same size in the XML
  So we need  the *transformed dimensions*
  Get the correct x, y, width and height for PDF images.
    width and height seem to be the issue.
  [Done now] Not recognizing the PDF image in map.pdf.  What is the op in the PDF processing.
          This is a Do operation.  And a form (pointing to Im1 which resolves to 17 0 obj)

  are the x, y, width, height correct?  Compare with pdfminer?
  width height position.
  Format
  [yes] can we get the name of the original file if it is available.
     And meta data for the file.


+ [done] Have pdftohtml include the rotation information for text. 
  examples/rotate.pdf and .tex - 45, 90 and horizontal
  See /Users/duncan/DSIProjects/Zoonotics-shared/NewData_Feb2017/Zoo_02_02_2017 Copy.Data/PDF/0201749332/Aviles-1992-Transmission of western equine enc.xml
  And for images.
  
+ [done] Size of images as rendered on the page
   examples/img.pdf and .tex



+ [done] Add flag to turn off output of paths.  -paths

+ [done] Add flag to turn off output of images.  -images

+ [done] Add the rotation to each <page>.

+ [done] insert info. about links
  HmtmlOutputDev::drawLink() while processing a page.
  Calls pages->AddLink(t). So in the page.

+ [done] For the paths, do we need to deal with the current transformation
  Did we do this?  location, scaling.

  In KimShauman/inst/SampleCVs/CVTsoukiasweb_pdftohtml.xml, the <rect>
  has values that are 538, 5794, 4563, 5819 - so in the thousands for a
  page width and height 918 and 1188

