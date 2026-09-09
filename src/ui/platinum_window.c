/*
 * Gazette — the Platinum main window
 * Copyright (c) 2026 brunocastello
 *
 * See platinum_window.h. Three panes, three scroll bars, two draggable
 * dividers, and a status line.
 */

#include "ui/platinum_window.h"

#include "core/gazette_core.h"
#include "extract/gazette_extract.h"
#include "feeds/gazette_feeds.h"
#include "portable/gazette_portable.h"

#include <Appearance.h>
#include <Controls.h>
#include <ControlDefinitions.h>
#include <DateTimeUtils.h>
#include <Fonts.h>
#include <Quickdraw.h>
#include <QuickdrawText.h>

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Metrics                                                             */
/* ------------------------------------------------------------------ */

enum {
    kScrollWidth   = 16,        /* Platinum's scroll bar, including its frame */
    kHeaderHeight  = 17,        /* the placard over each list                 */
    kStatusHeight  = 20,
    kRowHeight     = 14,        /* a headline row: Geneva 9 plus leading      */
    kReaderLead    = 13,        /* a wrapped reader line                      */
    kDividerWidth  = 4,         /* the draggable gap between panes            */
    kTextInset     = 4,
    kDateColumn    = 46,        /* headline text starts here, after the time  */

    /* The sidebar is an outline: a column for the disclosure triangle, then
       one indent for a group's feeds. A top-level feed is a group's sibling,
       so it starts where a group's name does. */
    kTriangleColumn = 11,       /* width of the triangle's own column */
    kGroupIndent    = 12,

    kMinSidebar    = 96,
    kMaxSidebarPad = 160,       /* how much room the right side must keep     */
    kMinListHeight = 3 * kRowHeight,
    kMinReader     = 3 * kReaderLead,

    /*
     * The reader pane holds one article. That used to be a feed's summary
     * and is now a whole page of extracted text when the full-text
     * preference is on, so both of these are sized for the larger of the
     * two: kGazetteExtractMax of body, plus the title and the byline.
     *
     * At a narrow window width a line is about fifty characters, which puts
     * 8 KB at some 170 lines before the blank line between each paragraph.
     * A ReaderLine is six bytes, so the headroom here costs 3 KB.
     */
    kMaxReaderLines = 512,

    /* Control reference numbers, so one action proc can serve all three. */
    kRefSidebar = 1,
    kRefList    = 2,
    kRefReader  = 3
};

/* Seconds between the Macintosh epoch (1904) and the Unix one (1970). */
enum { kMacToUnixEpoch = 2082844800L };

typedef struct {
    short start;                /* offset into gReaderText */
    short len;
    short style;                /* 0 title, 1 byline, 2 body */
} ReaderLine;

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

static WindowRef              gWindow;
static GazetteUIFeedChosen    gOnFeedChosen;
static GazetteUIArticleChosen gOnArticleChosen;

static ControlRef gSidebarScroll;
static ControlRef gListScroll;
static ControlRef gReaderScroll;
static ControlActionUPP gScrollUPP;

/* Pane rectangles, recomputed by Layout() and by nothing else. */
static Rect gSidebarRect;       /* the white list area, inside its frame */
static Rect gSidebarHeader;
static Rect gListRect;
static Rect gListHeader;
static Rect gReaderRect;
static Rect gStatusRect;
static Rect gVDivider;          /* between sidebar and the right side */
static Rect gHDivider;          /* between headlines and the article  */

static short gSidebarWidth = 168;
static short gListShare    = 45;    /* percent of the right side given to the
                                       headline list; the article gets the rest */

static int gSelectedFeed    = 0;
static int gSelectedArticle = -1;

/* The group the sidebar has selected, or -1 when the selection is a feed.
   gSelectedFeed keeps its meaning either way: it is the feed the headline
   list and the reader are showing, which a click on a group does not
   change. */
static int gSelectedGroup   = -1;

static char gStatus[192];

/* The reader's text, copied out of the store rather than pointing into it:
   a refresh replaces the articles, and a wrapped line index into freed
   headlines is the kind of bug that shows up as garbage on screen days
   later. */
static char       gReaderText[kGazetteExtractMax + 512];
static ReaderLine gReaderLines[kMaxReaderLines];
static short      gReaderLineCount;

static void Layout(void);
static void RewrapReader(void);
static void DrawSidebar(void);
static void DrawList(void);
static void DrawReader(void);
static void DrawStatus(void);

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static short WidthOfN(const char *text, short len)
{
    return (len <= 0) ? 0 : TextWidth(text, 0, len);
}

/*
 * Draw text clipped to a width, ending in an ellipsis when it does not fit.
 * A headline is nearly always too long for its column, and a hard clip mid
 * word reads as a drawing bug rather than as truncation.
 */
static void DrawTruncated(const char *text, short maxWidth)
{
    short len;
    short ellipsis;

    if (text == NULL || maxWidth <= 0) {
        return;
    }

    len = (short)strlen(text);
    if (WidthOfN(text, len) <= maxWidth) {
        DrawText(text, 0, len);
        return;
    }

    /* "\311" is the MacRoman ellipsis, one character rather than three. */
    ellipsis = CharWidth('\311');
    while (len > 0 && WidthOfN(text, len) + ellipsis > maxWidth) {
        len--;
    }
    if (len > 0) {
        DrawText(text, 0, len);
    }
    DrawChar('\311');
}

static long UnixNow(void)
{
    unsigned long macNow = 0;

    GetDateTime(&macNow);
    return (long)macNow - kMacToUnixEpoch;
}

static short VisibleRowsIn(const Rect *r, short rowHeight)
{
    short h = (short)(r->bottom - r->top);

    if (h < rowHeight) {
        return 1;
    }
    return (short)(h / rowHeight);
}

/* Set a scroll bar's range from a content size, and clamp its value. A range
   of zero disables the bar, which is what the Control Manager expects and
   what makes it draw greyed rather than live. */
static void SyncScroll(ControlRef control, int total, short visible)
{
    int max = total - visible;

    if (control == NULL) {
        return;
    }
    if (max < 0) {
        max = 0;
    }
    if (max > 32767) {
        max = 32767;
    }
    SetControlMaximum(control, (short)max);
    if (GetControlValue(control) > max) {
        SetControlValue(control, (short)max);
    }
    HiliteControl(control, (max > 0) ? 0 : 255);
}

/* ------------------------------------------------------------------ */
/* Layout                                                              */
/* ------------------------------------------------------------------ */

static void Layout(void)
{
    Rect  bounds;
    short rightLeft;
    short listBottom;
    short contentBottom;

    if (gWindow == NULL) {
        return;
    }

    GetWindowPortBounds(gWindow, &bounds);

    contentBottom = (short)(bounds.bottom - kStatusHeight);

    /* The sidebar keeps its width until the window gets too narrow to give
       the right-hand side anything useful. */
    if (gSidebarWidth > bounds.right - bounds.left - kMaxSidebarPad) {
        gSidebarWidth = (short)(bounds.right - bounds.left - kMaxSidebarPad);
    }
    if (gSidebarWidth < kMinSidebar) {
        gSidebarWidth = kMinSidebar;
    }

    SetRect(&gSidebarHeader, bounds.left, bounds.top,
            (short)(bounds.left + gSidebarWidth),
            (short)(bounds.top + kHeaderHeight));
    SetRect(&gSidebarRect, bounds.left,
            (short)(bounds.top + kHeaderHeight),
            (short)(bounds.left + gSidebarWidth - kScrollWidth),
            contentBottom);

    SetRect(&gVDivider, (short)(bounds.left + gSidebarWidth), bounds.top,
            (short)(bounds.left + gSidebarWidth + kDividerWidth),
            contentBottom);

    rightLeft = (short)(gVDivider.right);

    listBottom = (short)(bounds.top + kHeaderHeight +
                         (long)(contentBottom - bounds.top - kHeaderHeight) *
                         gListShare / 100);
    if (listBottom < bounds.top + kHeaderHeight + kMinListHeight) {
        listBottom = (short)(bounds.top + kHeaderHeight + kMinListHeight);
    }
    if (listBottom > contentBottom - kMinReader - kDividerWidth) {
        listBottom = (short)(contentBottom - kMinReader - kDividerWidth);
    }

    SetRect(&gListHeader, rightLeft, bounds.top,
            bounds.right, (short)(bounds.top + kHeaderHeight));
    SetRect(&gListRect, rightLeft, (short)(bounds.top + kHeaderHeight),
            (short)(bounds.right - kScrollWidth), listBottom);

    SetRect(&gHDivider, rightLeft, listBottom,
            bounds.right, (short)(listBottom + kDividerWidth));

    SetRect(&gReaderRect, rightLeft, (short)(listBottom + kDividerWidth),
            (short)(bounds.right - kScrollWidth), contentBottom);

    SetRect(&gStatusRect, bounds.left, contentBottom,
            bounds.right, bounds.bottom);

    /* The scroll bars sit in the gutter each pane leaves for them, overlapping
       the pane frame by a pixel the way Platinum does. */
    if (gSidebarScroll != NULL) {
        MoveControl(gSidebarScroll, (short)(gSidebarRect.right - 1),
                    gSidebarRect.top);
        SizeControl(gSidebarScroll, (short)(kScrollWidth + 1),
                    (short)(gSidebarRect.bottom - gSidebarRect.top));
    }
    if (gListScroll != NULL) {
        MoveControl(gListScroll, (short)(gListRect.right - 1), gListRect.top);
        SizeControl(gListScroll, (short)(kScrollWidth + 1),
                    (short)(gListRect.bottom - gListRect.top));
    }
    if (gReaderScroll != NULL) {
        MoveControl(gReaderScroll, (short)(gReaderRect.right - 1),
                    gReaderRect.top);
        SizeControl(gReaderScroll, (short)(kScrollWidth + 1),
                    (short)(gReaderRect.bottom - gReaderRect.top));
    }

    RewrapReader();

    SyncScroll(gSidebarScroll, GazetteCoreSidebarRowCount(),
               VisibleRowsIn(&gSidebarRect, kRowHeight));
    SyncScroll(gListScroll, GazetteFeedsArticleCount(),
               VisibleRowsIn(&gListRect, kRowHeight));
    SyncScroll(gReaderScroll, gReaderLineCount,
               VisibleRowsIn(&gReaderRect, kReaderLead));
}

/* ------------------------------------------------------------------ */
/* The reader pane                                                     */
/* ------------------------------------------------------------------ */

static void AddReaderLine(short start, short len, short style)
{
    if (gReaderLineCount >= kMaxReaderLines) {
        return;
    }
    gReaderLines[gReaderLineCount].start = start;
    gReaderLines[gReaderLineCount].len   = len;
    gReaderLines[gReaderLineCount].style = style;
    gReaderLineCount++;
}

/*
 * Greedy word wrap over one paragraph of gReaderText, in whatever font is
 * current. Words longer than the column are broken rather than allowed to
 * overhang, which a URL in a summary will otherwise do.
 */
static void WrapParagraph(short start, short len, short style, short width)
{
    short pos = start;
    short end = (short)(start + len);

    if (len <= 0) {
        AddReaderLine(start, 0, style);
        return;
    }

    while (pos < end) {
        short take  = 0;
        short lastSpace = -1;
        short i;

        for (i = pos; i < end; i++) {
            short trial = (short)(i - pos + 1);

            if (WidthOfN(gReaderText + pos, trial) > width) {
                break;
            }
            take = trial;
            if (gReaderText[i] == ' ') {
                lastSpace = trial;
            }
        }

        if (take == 0) {
            take = 1;                       /* a single character too wide */
        } else if (pos + take < end && lastSpace > 0 &&
                   gReaderText[pos + take] != ' ') {
            take = lastSpace;               /* step back to the word break */
        }

        AddReaderLine(pos, take, style);
        pos = (short)(pos + take);
        while (pos < end && gReaderText[pos] == ' ') {
            pos++;                          /* the break's own space */
        }
    }
}

static void RewrapReader(void)
{
    const GazetteArticle *a;
    GrafPtr savePort;
    short   width;
    size_t  used = 0;
    short   titleStart, titleLen;
    short   bylineStart, bylineLen;
    short   bodyStart, bodyLen;
    char    when[16];

    gReaderLineCount = 0;
    gReaderText[0]   = '\0';

    if (gWindow == NULL) {
        return;
    }
    a = GazetteFeedsArticleAt(gSelectedArticle);
    if (a == NULL) {
        return;
    }

    width = (short)(gReaderRect.right - gReaderRect.left - 2 * kTextInset - 2);
    if (width < 32) {
        return;
    }

    /* Compose the whole pane's text once, then index into it. */
    titleStart = 0;
    used  = gz_copy_n(gReaderText, sizeof gReaderText, a->title,
                      strlen(a->title));
    titleLen = (short)used;

    GazetteFormatDate(a->date, UnixNow(), when, sizeof when);

    bylineStart = (short)(used + 1);
    gReaderText[used++] = '\0';
    {
        char byline[192];

        if (a->source[0] != '\0' && when[0] != '\0') {
            snprintf(byline, sizeof byline, "%s - %s", a->source, when);
        } else if (a->source[0] != '\0') {
            snprintf(byline, sizeof byline, "%s", a->source);
        } else {
            snprintf(byline, sizeof byline, "%s", when);
        }
        used += gz_copy_n(gReaderText + used, sizeof gReaderText - used,
                          byline, strlen(byline));
        bylineLen = (short)(used - bylineStart);
    }

    /*
     * The article's own page when it has been fetched and extracted, and the
     * feed's summary otherwise. The store answers which article the held
     * text belongs to, so switching articles cannot show the last one's body
     * under this one's headline.
     */
    {
        const char *body = a->body;

        if (GazetteFeedsFullTextArticle() == gSelectedArticle) {
            const char *full = GazetteFeedsFullText();

            if (full[0] != '\0') {
                body = full;
            }
        }

        bodyStart = (short)(used + 1);
        gReaderText[used++] = '\0';
        used += gz_copy_n(gReaderText + used, sizeof gReaderText - used,
                          body, strlen(body));
        bodyLen = (short)(used - bodyStart);
    }

    GetPort(&savePort);
    SetPortWindowPort(gWindow);

    TextFont(kFontIDGeneva);
    TextSize(9);
    TextFace(bold);
    WrapParagraph(titleStart, titleLen, 0, width);

    TextFace(normal);
    if (bylineLen > 0) {
        WrapParagraph(bylineStart, bylineLen, 1, width);
    }

    TextSize(10);
    AddReaderLine(0, 0, 2);                 /* a blank line before the body */
    if (bodyLen > 0) {
        /*
         * The body arrives with its paragraphs marked by newlines, so each
         * one is wrapped on its own with a blank line between. Handing the
         * whole thing to WrapParagraph would lay a four-paragraph article
         * out as one unbroken block, which is what this used to do.
         */
        short at    = bodyStart;
        short end   = (short)(bodyStart + bodyLen);
        int   first = 1;

        while (at < end) {
            short stop = at;

            while (stop < end && gReaderText[stop] != '\n') {
                stop++;
            }
            if (stop > at) {
                if (!first) {
                    AddReaderLine(0, 0, 2);
                }
                WrapParagraph(at, (short)(stop - at), 2, width);
                first = 0;
            }
            at = (short)(stop + 1);
        }
    } else {
        static const char kNone[] = "(This feed carries no summary for "
                                    "this article.)";
        short at = (short)used;

        used += gz_copy_n(gReaderText + used, sizeof gReaderText - used,
                          kNone, sizeof kNone - 1);
        WrapParagraph(at, (short)(used - at), 2, width);
    }

    TextFace(normal);
    SetPort(savePort);
}

/* ------------------------------------------------------------------ */
/* Scrolling                                                           */
/* ------------------------------------------------------------------ */

static pascal void ScrollAction(ControlRef control, ControlPartCode part)
{
    short value;
    short max;
    short page;
    short delta = 0;

    if (control == NULL || part == 0) {
        return;
    }

    switch (GetControlReference(control)) {
        case kRefSidebar: page = VisibleRowsIn(&gSidebarRect, kRowHeight); break;
        case kRefList:    page = VisibleRowsIn(&gListRect, kRowHeight);    break;
        default:          page = VisibleRowsIn(&gReaderRect, kReaderLead); break;
    }
    if (page > 1) {
        page--;             /* a page scroll keeps one line of context */
    }

    switch (part) {
        case kControlUpButtonPart:   delta = -1;    break;
        case kControlDownButtonPart: delta = 1;     break;
        case kControlPageUpPart:     delta = (short)-page; break;
        case kControlPageDownPart:   delta = page;  break;
        default: return;
    }

    value = (short)(GetControlValue(control) + delta);
    max   = GetControlMaximum(control);
    if (value < 0)   value = 0;
    if (value > max) value = max;

    if (value == GetControlValue(control)) {
        return;
    }
    SetControlValue(control, value);

    /* Redraw here rather than invalidating: this runs inside TrackControl's
       own loop, and an update event would not be seen until it returned. */
    switch (GetControlReference(control)) {
        case kRefSidebar: DrawSidebar(); break;
        case kRefList:    DrawList();    break;
        default:          DrawReader();  break;
    }
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

static void DrawHeader(const Rect *r, const char *text)
{
    Rect inner = *r;

    DrawThemePlacard(r, kThemeStateActive);

    TextFont(kFontIDGeneva);
    TextSize(9);
    TextFace(bold);

    inner.left  = (short)(inner.left + kTextInset + 2);
    inner.right = (short)(inner.right - kTextInset);
    MoveTo(inner.left, (short)(r->top + 12));
    DrawTruncated(text, (short)(inner.right - inner.left));

    TextFace(normal);
}

/* A white list area with the Platinum list-box frame around it. */
static void BeginListArea(const Rect *r, RgnHandle *saveClip)
{
    Rect frame = *r;

    *saveClip = NewRgn();
    if (*saveClip != NULL) {
        GetClip(*saveClip);
    }

    frame.right = (short)(frame.right + 1);     /* the scroll bar overlaps */
    DrawThemeListBoxFrame(&frame, kThemeStateActive);

    SetThemeBackground(kThemeBrushWhite, 8, true);
    EraseRect(r);
    ClipRect(r);
}

static void EndListArea(RgnHandle saveClip)
{
    SetThemeBackground(kThemeBrushDialogBackgroundActive, 8, true);
    if (saveClip != NULL) {
        SetClip(saveClip);
        DisposeRgn(saveClip);
    }
}

/*
 * Platinum's disclosure triangle, drawn rather than embedded as a CDEF 4
 * control: there is one per group and they scroll with the list, so a real
 * control would have to be created, moved and hidden on every scroll and
 * every collapse. At this size it is nine pixels of solid black either
 * pointing right (shut) or down (open), which is exactly what the CDEF
 * draws in the Platinum theme.
 */
static void DrawTriangle(short left, short baseline, Boolean open)
{
    short top = (short)(baseline - 8);
    short i;

    if (open) {
        /* Pointing down: a row of nine narrowing by two from each end. */
        for (i = 0; i < 5; i++) {
            MoveTo((short)(left + i), (short)(top + 2 + i));
            LineTo((short)(left + 8 - i), (short)(top + 2 + i));
        }
    } else {
        /* Pointing right: a column of nine narrowing the same way. */
        for (i = 0; i < 5; i++) {
            MoveTo((short)(left + 2 + i), (short)(top + i));
            LineTo((short)(left + 2 + i), (short)(top + 8 - i));
        }
    }
}

static void DrawSidebar(void)
{
    RgnHandle clip = NULL;
    short     rows;
    short     top;
    short     line;
    int       count;
    int       i;

    if (gWindow == NULL) {
        return;
    }
    SetPortWindowPort(gWindow);

    BeginListArea(&gSidebarRect, &clip);

    TextFont(kFontIDGeneva);
    TextSize(9);

    count = GazetteCoreSidebarRowCount();
    rows  = VisibleRowsIn(&gSidebarRect, kRowHeight);
    top   = (gSidebarScroll != NULL) ? GetControlValue(gSidebarScroll) : 0;
    line  = (short)(gSidebarRect.top + 11);

    for (i = top; i < count && i < top + rows; i++) {
        GazetteSidebarRow row;
        short             textLeft;
        Boolean           selected = false;

        if (!GazetteCoreSidebarRowAt(i, &row)) {
            break;
        }

        if (row.kind == kGazetteRowGroup) {
            /* The group's own line: triangle, then the name in bold, the way
               a folder reads in a Finder list view. */
            SetThemeTextColor(kThemeTextColorListView, 8, true);
            DrawTriangle((short)(gSidebarRect.left + kTextInset), line,
                         !GazetteCoreGroupCollapsed(row.index));

            textLeft = (short)(gSidebarRect.left + kTextInset +
                               kTriangleColumn);
            TextFace(bold);
            MoveTo(textLeft, line);
            DrawTruncated(GazetteCoreGroupName(row.index),
                          (short)(gSidebarRect.right - textLeft - kTextInset));
            TextFace(normal);

            selected = (row.index == gSelectedGroup);
        } else {
            textLeft = (short)(gSidebarRect.left + kTextInset +
                               kTriangleColumn);
            if (GazetteCoreFeedGroup(row.index) >= 0) {
                textLeft = (short)(textLeft + kGroupIndent);
            }

            /* A switched-off feed keeps its place and is drawn the way an
               unavailable item is drawn anywhere else in Platinum, so it
               reads as off rather than as missing. */
            SetThemeTextColor(GazetteCoreFeedEnabled(row.index)
                                  ? kThemeTextColorListView
                                  : kThemeTextColorDialogInactive,
                              8, true);
            MoveTo(textLeft, line);
            DrawTruncated(GazetteCoreFeedTitle(row.index),
                          (short)(gSidebarRect.right - textLeft - kTextInset));

            selected = (gSelectedGroup < 0 && row.index == gSelectedFeed);
        }

        if (selected) {
            Rect box;

            SetRect(&box, gSidebarRect.left, (short)(line - 10),
                    gSidebarRect.right, (short)(line + 3));
            InvertRect(&box);
        }
        line = (short)(line + kRowHeight);
    }

    ForeColor(blackColor);
    EndListArea(clip);
}

static void DrawList(void)
{
    RgnHandle clip = NULL;
    short     rows;
    short     top;
    short     line;
    long      now;
    int       count;
    int       i;

    if (gWindow == NULL) {
        return;
    }
    SetPortWindowPort(gWindow);

    BeginListArea(&gListRect, &clip);

    TextFont(kFontIDGeneva);
    TextSize(9);
    TextFace(normal);           /* the rows set their own; start from plain */

    count = GazetteFeedsArticleCount();
    rows  = VisibleRowsIn(&gListRect, kRowHeight);
    top   = (gListScroll != NULL) ? GetControlValue(gListScroll) : 0;
    line  = (short)(gListRect.top + 11);
    now   = UnixNow();

    if (count == 0) {
        MoveTo((short)(gListRect.left + kTextInset), line);
        DrawString("\pNo headlines yet - press Command-R.");
    }

    for (i = top; i < count && i < top + rows; i++) {
        const GazetteArticle *a = GazetteFeedsArticleAt(i);
        char                  when[16];

        if (a == NULL) {
            break;
        }

        /* The date sits in a fixed column so the headlines line up; an
           article with no date simply leaves it blank rather than shifting. */
        GazetteFormatDate(a->date, now, when, sizeof when);
        if (when[0] != '\0') {
            MoveTo((short)(gListRect.left + kTextInset), line);
            DrawTruncated(when, kDateColumn - kTextInset);
        }

        /* Unread in bold, the way every mail and news reader of the era
           marked one. The date column stays plain either way, so the weight
           reads as being about the headline rather than the row. */
        TextFace(a->read ? normal : bold);
        MoveTo((short)(gListRect.left + kTextInset + kDateColumn), line);
        DrawTruncated(a->title,
                      (short)(gListRect.right - gListRect.left -
                              kDateColumn - 2 * kTextInset));
        TextFace(normal);

        if (i == gSelectedArticle) {
            Rect row;

            SetRect(&row, gListRect.left, (short)(line - 10),
                    gListRect.right, (short)(line + 3));
            InvertRect(&row);
        }
        line = (short)(line + kRowHeight);
    }

    EndListArea(clip);
}

static void DrawReader(void)
{
    RgnHandle clip = NULL;
    short     rows;
    short     top;
    short     line;
    short     i;

    if (gWindow == NULL) {
        return;
    }
    SetPortWindowPort(gWindow);

    BeginListArea(&gReaderRect, &clip);

    rows = VisibleRowsIn(&gReaderRect, kReaderLead);
    top  = (gReaderScroll != NULL) ? GetControlValue(gReaderScroll) : 0;
    line = (short)(gReaderRect.top + 11);

    if (gReaderLineCount == 0) {
        TextFont(kFontIDGeneva);
        TextSize(10);
        MoveTo((short)(gReaderRect.left + kTextInset), line);
        DrawString("\pSelect a headline to read it.");
        EndListArea(clip);
        return;
    }

    for (i = top; i < gReaderLineCount && i < top + rows; i++) {
        const ReaderLine *l = &gReaderLines[i];

        TextFont(kFontIDGeneva);
        switch (l->style) {
            case 0: TextSize(9);  TextFace(bold);   break;
            case 1: TextSize(9);  TextFace(normal); break;
            default: TextSize(10); TextFace(normal); break;
        }

        if (l->len > 0) {
            MoveTo((short)(gReaderRect.left + kTextInset), line);
            DrawText(gReaderText + l->start, 0, l->len);
        }
        line = (short)(line + kReaderLead);
    }

    TextFace(normal);
    EndListArea(clip);
}

static void DrawStatus(void)
{
    if (gWindow == NULL) {
        return;
    }
    SetPortWindowPort(gWindow);

    SetThemeBackground(kThemeBrushDialogBackgroundActive, 8, true);
    EraseRect(&gStatusRect);

    TextFont(kFontIDGeneva);
    TextSize(9);
    TextFace(normal);

    MoveTo((short)(gStatusRect.left + kTextInset + 4),
           (short)(gStatusRect.top + 13));
    DrawTruncated(gStatus,
                  (short)(gStatusRect.right - gStatusRect.left -
                          2 * kTextInset - 8));
}

void GazetteUIUpdate(void)
{
    Rect  bounds;
    char  header[kGazetteFeedTitleLen + 32];

    if (gWindow == NULL) {
        return;
    }
    SetPortWindowPort(gWindow);
    GetWindowPortBounds(gWindow, &bounds);

    SetThemeBackground(kThemeBrushDialogBackgroundActive, 8, true);
    EraseRect(&bounds);

    DrawHeader(&gSidebarHeader, "Feeds");

    if (GazetteFeedsArticleCount() > 0) {
        const char *title  = GazetteFeedsTitle();
        int         unread = GazetteFeedsUnreadCount();

        if (title[0] == '\0') {
            title = GazetteCoreFeedTitle(gSelectedFeed);
        }
        /* How many are left to read is the number worth reading; the total
           is only interesting when there is nothing left. */
        if (unread > 0) {
            snprintf(header, sizeof header, "%s (%d unread of %d)", title,
                     unread, GazetteFeedsArticleCount());
        } else {
            snprintf(header, sizeof header, "%s (%d)", title,
                     GazetteFeedsArticleCount());
        }
    } else {
        snprintf(header, sizeof header, "%s",
                 GazetteCoreFeedTitle(gSelectedFeed));
    }
    DrawHeader(&gListHeader, header);

    /* The dividers, drawn as the Appearance Manager's own separators so they
       track the theme rather than being two hard-coded greys. */
    DrawThemeSeparator(&gVDivider, kThemeStateActive);
    DrawThemeSeparator(&gHDivider, kThemeStateActive);

    DrawSidebar();
    DrawList();
    DrawReader();
    DrawStatus();

    DrawControls(gWindow);
}

/* ------------------------------------------------------------------ */
/* Selection                                                           */
/* ------------------------------------------------------------------ */

/* Bring a row into view, scrolling the least that will do it. */
static void RevealRow(ControlRef scroll, int index, short rows)
{
    short top;

    if (scroll == NULL || index < 0) {
        return;
    }
    top = GetControlValue(scroll);

    if (index < top) {
        top = (short)index;
    } else if (index >= top + rows) {
        top = (short)(index - rows + 1);
    } else {
        return;
    }
    if (top < 0) {
        top = 0;
    }
    if (top > GetControlMaximum(scroll)) {
        top = GetControlMaximum(scroll);
    }
    SetControlValue(scroll, top);
}

static void SelectArticle(int index)
{
    int count = GazetteFeedsArticleCount();

    if (count == 0) {
        gSelectedArticle = -1;
    } else {
        if (index < 0)      index = 0;
        if (index >= count) index = count - 1;
        gSelectedArticle = index;
    }

    /* Opening it is what makes it read — before the draw, so the headline
       loses its bold in the same repaint that highlights it. */
    if (gSelectedArticle >= 0) {
        GazetteFeedsMarkRead(gSelectedArticle, 1);
    }

    RewrapReader();
    if (gReaderScroll != NULL) {
        SetControlValue(gReaderScroll, 0);
        SyncScroll(gReaderScroll, gReaderLineCount,
                   VisibleRowsIn(&gReaderRect, kReaderLead));
    }
    RevealRow(gListScroll, gSelectedArticle,
              VisibleRowsIn(&gListRect, kRowHeight));

    DrawList();
    DrawReader();

    /* Last, so the pane is already showing the summary when the shell decides
       whether to go and fetch anything better. */
    if (gSelectedArticle >= 0 && gOnArticleChosen != NULL) {
        gOnArticleChosen(gSelectedArticle);
    }
}

/* ------------------------------------------------------------------ */
/* Dividers                                                            */
/* ------------------------------------------------------------------ */

/*
 * Drag a divider. Tracking the mouse in a loop here is the same shape as
 * DragWindow or GrowWindow: the Toolbox does it this way, it ends when the
 * button comes up, and no network work is pending while a mouse button is
 * held anyway.
 */
static void TrackDivider(Point where, Boolean vertical)
{
    Rect  bounds;
    Point pt;

    GetWindowPortBounds(gWindow, &bounds);
    (void)where;

    while (StillDown()) {
        GetMouse(&pt);

        if (vertical) {
            short want = pt.h;

            if (want < kMinSidebar) {
                want = kMinSidebar;
            }
            if (want > bounds.right - kMaxSidebarPad) {
                want = (short)(bounds.right - kMaxSidebarPad);
            }
            if (want != gSidebarWidth) {
                gSidebarWidth = want;
                Layout();
                GazetteUIUpdate();
            }
        } else {
            short usable = (short)(bounds.bottom - kStatusHeight -
                                   bounds.top - kHeaderHeight);
            short share;

            if (usable <= 0) {
                break;
            }
            share = (short)((long)(pt.v - bounds.top - kHeaderHeight) * 100 /
                            usable);
            if (share < 10)  share = 10;
            if (share > 90)  share = 90;
            if (share != gListShare) {
                gListShare = share;
                Layout();
                GazetteUIUpdate();
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Events                                                              */
/* ------------------------------------------------------------------ */

void GazetteUIClick(Point where, EventModifiers modifiers)
{
    ControlRef       control = NULL;
    ControlPartCode  part;

    (void)modifiers;

    if (gWindow == NULL) {
        return;
    }
    SetPortWindowPort(gWindow);

    part = FindControl(where, gWindow, &control);
    if (control != NULL && part != 0) {
        if (part == kControlIndicatorPart) {
            /* The thumb tracks itself; the pane is redrawn once it lands. */
            if (TrackControl(control, where, NULL) == kControlIndicatorPart) {
                switch (GetControlReference(control)) {
                    case kRefSidebar: DrawSidebar(); break;
                    case kRefList:    DrawList();    break;
                    default:          DrawReader();  break;
                }
            }
        } else {
            TrackControl(control, where, gScrollUPP);
        }
        return;
    }

    if (PtInRect(where, &gVDivider)) {
        TrackDivider(where, true);
        return;
    }
    if (PtInRect(where, &gHDivider)) {
        TrackDivider(where, false);
        return;
    }

    if (PtInRect(where, &gSidebarRect)) {
        short             top = (gSidebarScroll != NULL)
                                    ? GetControlValue(gSidebarScroll) : 0;
        int               index = top + (where.v - gSidebarRect.top) / kRowHeight;
        GazetteSidebarRow row;

        if (!GazetteCoreSidebarRowAt(index, &row)) {
            return;
        }

        if (row.kind == kGazetteRowGroup) {
            /* The triangle's own column opens and shuts the group; the rest
               of the line selects it, the way a folder behaves in a list
               view. Rename and Remove need something to act on, and that
               something is the selection. */
            if (where.h < gSidebarRect.left + kTextInset + kTriangleColumn) {
                GazetteCoreSetGroupCollapsed(
                    row.index, !GazetteCoreGroupCollapsed(row.index));

                /* The rows below it have moved, so the bar's range and the
                   whole pane both have to come back into agreement. */
                SyncScroll(gSidebarScroll, GazetteCoreSidebarRowCount(),
                           VisibleRowsIn(&gSidebarRect, kRowHeight));
            } else if (row.index == gSelectedGroup) {
                return;
            } else {
                gSelectedGroup = row.index;
            }
            DrawSidebar();
            return;
        }

        if (row.index != gSelectedFeed || gSelectedGroup >= 0) {
            Boolean changed = (row.index != gSelectedFeed);

            gSelectedFeed  = row.index;
            gSelectedGroup = -1;
            DrawSidebar();
            if (changed && gOnFeedChosen != NULL) {
                gOnFeedChosen(row.index);
            }
        }
        return;
    }

    if (PtInRect(where, &gListRect)) {
        short top   = (gListScroll != NULL) ? GetControlValue(gListScroll) : 0;
        int   index = top + (where.v - gListRect.top) / kRowHeight;

        if (index >= 0 && index < GazetteFeedsArticleCount()) {
            SelectArticle(index);
        }
        return;
    }
}

Boolean GazetteUIKey(short key)
{
    int count = GazetteFeedsArticleCount();

    if (gWindow == NULL) {
        return false;
    }

    switch (key) {
        case 0x1E:                          /* up arrow */
            if (count > 0) {
                SelectArticle((gSelectedArticle <= 0) ? 0
                                                      : gSelectedArticle - 1);
            }
            return true;

        case 0x1F:                          /* down arrow */
            if (count > 0) {
                SelectArticle((gSelectedArticle < 0) ? 0
                                                     : gSelectedArticle + 1);
            }
            return true;

        case 0x0B:                          /* page up */
            if (count > 0) {
                SelectArticle(gSelectedArticle -
                              VisibleRowsIn(&gListRect, kRowHeight));
            }
            return true;

        case 0x0C:                          /* page down */
            if (count > 0) {
                SelectArticle(gSelectedArticle +
                              VisibleRowsIn(&gListRect, kRowHeight));
            }
            return true;

        case 0x01:                          /* home */
            if (count > 0) {
                SelectArticle(0);
            }
            return true;

        case 0x04:                          /* end */
            if (count > 0) {
                SelectArticle(count - 1);
            }
            return true;

        default:
            return false;
    }
}

void GazetteUIActivate(Boolean active)
{
    if (gWindow == NULL) {
        return;
    }
    /* The Control Manager greys the scroll bars for us; 255 is the inactive
       hilite state and 0 the active one. A disabled bar stays disabled. */
    if (gSidebarScroll != NULL) {
        HiliteControl(gSidebarScroll,
                      (active && GetControlMaximum(gSidebarScroll) > 0) ? 0 : 255);
    }
    if (gListScroll != NULL) {
        HiliteControl(gListScroll,
                      (active && GetControlMaximum(gListScroll) > 0) ? 0 : 255);
    }
    if (gReaderScroll != NULL) {
        HiliteControl(gReaderScroll,
                      (active && GetControlMaximum(gReaderScroll) > 0) ? 0 : 255);
    }
}

void GazetteUIResized(void)
{
    Rect bounds;

    if (gWindow == NULL) {
        return;
    }
    Layout();
    SetPortWindowPort(gWindow);
    GetWindowPortBounds(gWindow, &bounds);
    InvalWindowRect(gWindow, &bounds);
}

/* ------------------------------------------------------------------ */
/* Notifications from the shell                                        */
/* ------------------------------------------------------------------ */

void GazetteUISetStatus(const char *text)
{
    if (text == NULL) {
        text = "";
    }
    if (strcmp(gStatus, text) == 0) {
        return;                             /* nothing to repaint */
    }
    strncpy(gStatus, text, sizeof gStatus - 1);
    gStatus[sizeof gStatus - 1] = '\0';

    if (gWindow != NULL) {
        DrawStatus();
    }
}

void GazetteUIArticlesChanged(void)
{
    if (gWindow == NULL) {
        return;
    }
    if (gListScroll != NULL) {
        SetControlValue(gListScroll, 0);
    }
    gSelectedArticle = (GazetteFeedsArticleCount() > 0) ? 0 : -1;

    Layout();
    RewrapReader();
    if (gReaderScroll != NULL) {
        SetControlValue(gReaderScroll, 0);
        SyncScroll(gReaderScroll, gReaderLineCount,
                   VisibleRowsIn(&gReaderRect, kReaderLead));
    }
    if (gSelectedArticle >= 0) {
        GazetteFeedsMarkRead(gSelectedArticle, 1);
    }
    GazetteUIUpdate();

    /* The first article is open now, exactly as if it had been clicked. */
    if (gSelectedArticle >= 0 && gOnArticleChosen != NULL) {
        gOnArticleChosen(gSelectedArticle);
    }
}

void GazetteUIArticleTextChanged(void)
{
    if (gWindow == NULL) {
        return;
    }
    RewrapReader();
    if (gReaderScroll != NULL) {
        SetControlValue(gReaderScroll, 0);
        SyncScroll(gReaderScroll, gReaderLineCount,
                   VisibleRowsIn(&gReaderRect, kReaderLead));
    }
    DrawReader();

    /* The scroll bar's own frame is outside the pane DrawReader repaints. */
    if (gReaderScroll != NULL) {
        Draw1Control(gReaderScroll);
    }
}

int GazetteUISelectedArticle(void)
{
    return gSelectedArticle;
}

void GazetteUIFeedsChanged(void)
{
    if (gWindow == NULL) {
        return;
    }
    if (gSelectedFeed >= GazetteCoreFeedCount()) {
        gSelectedFeed = 0;
    }
    if (gSelectedGroup >= GazetteCoreGroupCount()) {
        gSelectedGroup = -1;
    }
    Layout();
    GazetteUIUpdate();
}

int GazetteUISelectedFeed(void)
{
    return gSelectedFeed;
}

Boolean GazetteUISelection(int *kind, int *index)
{
    if (kind == NULL || index == NULL) {
        return false;
    }
    if (gSelectedGroup >= 0 && gSelectedGroup < GazetteCoreGroupCount()) {
        *kind  = kGazetteRowGroup;
        *index = gSelectedGroup;
        return true;
    }
    if (gSelectedFeed >= 0 && gSelectedFeed < GazetteCoreFeedCount()) {
        *kind  = kGazetteRowFeed;
        *index = gSelectedFeed;
        return true;
    }
    return false;
}

void GazetteUISelectGroup(int index)
{
    if (index < 0 || index >= GazetteCoreGroupCount()) {
        return;
    }
    gSelectedGroup = index;
    RevealRow(gSidebarScroll, GazetteCoreSidebarRowForGroup(index),
              VisibleRowsIn(&gSidebarRect, kRowHeight));
    if (gWindow != NULL) {
        DrawSidebar();
    }
}

void GazetteUISelectFeed(int index)
{
    if (index < 0 || index >= GazetteCoreFeedCount()) {
        return;
    }
    gSelectedFeed  = index;
    gSelectedGroup = -1;
    /* -1 when the feed's group is shut: it is still the selection, there is
       just no row to scroll to. */
    RevealRow(gSidebarScroll, GazetteCoreSidebarRowForFeed(index),
              VisibleRowsIn(&gSidebarRect, kRowHeight));
    if (gWindow != NULL) {
        DrawSidebar();
    }
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static ControlRef MakeScroll(long reference)
{
    Rect r;

    /* Placed properly by Layout(); this only has to be a legal rectangle. */
    SetRect(&r, 0, 0, kScrollWidth, 64);
    return NewControl(gWindow, &r, "\p", true, 0, 0, 0,
                      kControlScrollBarProc, reference);
}

Boolean GazetteUIOpen(GazetteUIFeedChosen onFeedChosen,
                      GazetteUIArticleChosen onArticleChosen)
{
    OSStatus         err;
    Rect             bounds;
    WindowAttributes attrs;

    if (gWindow != NULL) {
        return true;
    }

    gOnFeedChosen    = onFeedChosen;
    gOnArticleChosen = onArticleChosen;

    SetRect(&bounds, 40, 48, 40 + 620, 48 + 420);

    /* No kWindowStandardHandlerAttribute: that installs the Carbon Event
       Manager's standard handler, which would compete with the
       WaitNextEvent loop in main.cpp. */
    attrs = kWindowStandardDocumentAttributes;

    err = CreateNewWindow(kDocumentWindowClass, attrs, &bounds, &gWindow);
    if (err != noErr || gWindow == NULL) {
        gWindow = NULL;
        return false;
    }

    SetWTitle(gWindow, "\pGazette");
    SetThemeWindowBackground(gWindow, kThemeBrushDialogBackgroundActive, false);
    SetPortWindowPort(gWindow);

    gScrollUPP = NewControlActionUPP(ScrollAction);

    gSidebarScroll = MakeScroll(kRefSidebar);
    gListScroll    = MakeScroll(kRefList);
    gReaderScroll  = MakeScroll(kRefReader);

    gSelectedFeed    = 0;
    gSelectedArticle = -1;
    gSelectedGroup   = -1;

    Layout();

    ShowWindow(gWindow);
    SelectWindow(gWindow);
    return true;
}

void GazetteUIClose(void)
{
    if (gWindow == NULL) {
        return;
    }

    /* DisposeWindow takes the controls with it; the UPP is ours. */
    DisposeWindow(gWindow);
    gWindow        = NULL;
    gSidebarScroll = NULL;
    gListScroll    = NULL;
    gReaderScroll  = NULL;

    if (gScrollUPP != NULL) {
        DisposeControlActionUPP(gScrollUPP);
        gScrollUPP = NULL;
    }
}

WindowRef GazetteUIWindow(void)
{
    return gWindow;
}
