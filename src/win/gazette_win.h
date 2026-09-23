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

/* View menu. Each answers whether the item is showing "Hide" or "Show". */
void GazetteWindowToggleSidebar(HWND frame);
void GazetteWindowToggleToolbar(HWND frame);
BOOL GazetteWindowSidebarHidden(void);
BOOL GazetteWindowToolbarHidden(void);

/* Messages the frame receives on the window's behalf. */
void GazetteWindowMeasureItem(MEASUREITEMSTRUCT *measure);
void GazetteWindowDrawItem(const DRAWITEMSTRUCT *draw);
BOOL GazetteWindowNotify(HWND frame, NMHDR *header, LRESULT *result);
/* A WM_COMMAND the window itself answers -- the toolbar's chevron. Returns
   TRUE when it was one. */
BOOL GazetteWindowCommand(HWND frame, int id);

/* The status bar's two sections: what the view holds, and what the
   network is doing. */
void GazetteWindowSetCount(const char *text);
void GazetteWindowSetStatus(const char *text);

/* The Find dialog, which is modeless: the loop gives it its keystrokes,
   and the frame passes on the message it reports through. */
HWND GazetteWindowFindDialog(void);
UINT GazetteWindowFindMessage(void);
void GazetteWindowFindEvent(const FINDREPLACEA *find);
void GazetteWindowMinimumSize(POINT *minimum);

#endif /* GAZETTE_WIN_H */
