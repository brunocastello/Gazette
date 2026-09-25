/*
 * gazette_win.h - what the Windows shell's two halves say to each other.
 *
 * The split mirrors the Mac build's: gazette_win_main.c is main.cpp --
 * the application, its menus and its About box -- and
 * gazette_win_window.c is platinum_window.c, the window itself and
 * everything drawn in it.
 */
#ifndef GAZETTE_WIN_H
#define GAZETTE_WIN_H

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>

/* What the application asks of a window, on both systems; this shell's
   window answers it (gazette_win_window.c). */
#include "app/gazette_ui.h"

/*
 * The icons, in the order tools/generate_win_assets.py lays them into
 * Resources/win/ui_icons.bmp -- which is resource-id order in
 * Gazette_ui_icons.r, so index N here is id 256 + N there and the two
 * builds cannot drift apart without the generator saying so.
 */
enum {
    kIconToday = 0,
    kIconAllUnread,
    kIconStarred,
    kIconStarredArticle,
    kIconSidebar,
    kIconRefresh,
    kIconMarkAllRead,
    kIconMarkAllUnread,
    kIconHideRead,
    kIconShowRead,
    kIconMarkRead,
    kIconMarkUnread,
    kIconNextUnread,
    kIconBrowser,
    kIconNew,
    kIconFind,
    kIconCount
};

/* The window. */
BOOL  GazetteWindowRegisterClasses(HINSTANCE instance);
BOOL  GazetteWindowCreate(HWND frame, HINSTANCE instance);
void  GazetteWindowLayout(HWND frame);
void  GazetteWindowDestroy(void);
HFONT GazetteWindowFont(void);
/* The light grey of the date bands and the rules between things. */
COLORREF GazetteWindowLightTone(void);

/* What to call when a row or a headline is chosen, and when Find asks for
   a search -- the Mac's GazetteUIOpen arguments. Before the window opens. */
void GazetteWindowSetCallbacks(GazetteUIFeedChosen onFeedChosen,
                               GazetteUIArticleChosen onArticleChosen,
                               GazetteUIGroupChosen onGroupChosen,
                               GazetteUISmartChosen onSmartChosen,
                               GazetteUICommandChosen onCommand);

/* View menu. Each answers whether the item is showing "Hide" or "Show";
   the preferences hold the answer, and GazetteUIViewChanged reads it. */
BOOL GazetteWindowSidebarHidden(void);
BOOL GazetteWindowToolbarHidden(void);

/* Messages the frame receives on the window's behalf. */
void GazetteWindowMeasureItem(MEASUREITEMSTRUCT *measure);
void GazetteWindowDrawItem(const DRAWITEMSTRUCT *draw);
BOOL GazetteWindowNotify(HWND frame, NMHDR *header, LRESULT *result);
/* A WM_COMMAND the window itself answers -- the toolbar's chevron. Returns
   TRUE when it was one. */
BOOL GazetteWindowCommand(HWND frame, WPARAM wParam, LPARAM lParam);

/* The status bar's two sections: what the view holds, and what the
   network is doing. */
/* The frame's own painting: the edge round the toolbar strip. */
void GazetteWindowPaint(HWND frame, HDC dc);

void GazetteWindowSetCount(const char *text);
void GazetteWindowSetStatus(const char *text);

/* The Find dialog, which is modeless: the loop gives it its keystrokes,
   and the frame passes on the message it reports through. */
HWND GazetteWindowFindDialog(void);
UINT GazetteWindowFindMessage(void);
void GazetteWindowFindEvent(const FINDREPLACEA *find);
void GazetteWindowMinimumSize(POINT *minimum);
void GazetteWindowColumnWidths(int *sidebar, int *list);

/* The article pane, gazette_win_reader.c: the window class's procedure,
   and the text composed for an article (-1 for none) and laid out. */
LRESULT CALLBACK GazetteWinReaderProc(HWND hwnd, UINT message,
                                      WPARAM wParam, LPARAM lParam);
void GazetteWinReaderCompose(int article);
void GazetteWinReaderLayout(void);
int  GazetteWinReaderOffset(void);
void GazetteWinReaderScrollTo(int offset);

#endif /* GAZETTE_WIN_H */
