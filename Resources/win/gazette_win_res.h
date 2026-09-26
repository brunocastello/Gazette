/*
 * gazette_win_res.h - the identifiers the resource script and the Windows
 * shell both have to agree on.
 *
 * Included by Gazette.rc (through windres) and by the sources under
 * src/win, so it must
 * stay free of anything a resource compiler cannot read: #define only, no
 * declarations and no types.
 */
#ifndef GAZETTE_WIN_RES_H
#define GAZETTE_WIN_RES_H

/* Resources. 1 is reserved: it is CREATEPROCESS_MANIFEST_RESOURCE_ID, and
   the manifest has to carry that exact number for the loader to find it. */
#define IDI_GAZETTE            100
#define IDR_MAIN_MENU          101
#define IDR_ACCELERATORS       102
/* The strip of every 16x16 icon Gazette drew for itself, cut into an
   image list at startup. tools/generate_win_assets.py writes it. */
#define IDB_UI_ICONS           103
/* The dialogs, gazette_win_dialogs.c: the Mac's DLOGs 128-130 in
   Windows' layout. */
#define IDD_FEED               110
#define IDD_NAME               111
#define IDD_PREFS              112
#define IDD_SEARCH             113

/* Menu commands. Grouped by menu in blocks of a hundred so a new item
   never has to be squeezed between two existing numbers. */
#define IDM_FILE_NEW_FEED      40100
#define IDM_FILE_NEW_GROUP     40101
#define IDM_FILE_REFRESH       40102
#define IDM_FILE_IMPORT        40103
#define IDM_FILE_EXPORT        40104
#define IDM_FILE_QUIT          40105

#define IDM_EDIT_UNDO          40200
#define IDM_EDIT_CUT           40201
#define IDM_EDIT_COPY          40202
#define IDM_EDIT_PASTE         40203
#define IDM_EDIT_CLEAR         40204
#define IDM_EDIT_FIND          40205
#define IDM_EDIT_PREFS         40206
/* The toolbar's Clear Search, which no menu has. */
#define IDM_EDIT_CLEAR_SEARCH  40207

#define IDM_VIEW_HIDE_READ     40301
#define IDM_VIEW_HIDE_FEEDS    40302
#define IDM_VIEW_SHOW_PHOTOS   40303
#define IDM_VIEW_SIDEBAR       40304
#define IDM_VIEW_TOOLBAR       40305

#define IDM_FEEDS_TODAY        40400
#define IDM_FEEDS_UNREAD       40401
#define IDM_FEEDS_STARRED      40402
#define IDM_FEEDS_OLDEST_FIRST 40403
#define IDM_FEEDS_MARK_ALL     40404
#define IDM_FEEDS_EDIT         40405
#define IDM_FEEDS_TURN_OFF     40406
#define IDM_FEEDS_DELETE       40407

#define IDM_ARTICLE_NEXT       40500
#define IDM_ARTICLE_UNREAD     40501
#define IDM_ARTICLE_ABOVE      40502
#define IDM_ARTICLE_BELOW      40503
#define IDM_ARTICLE_STAR       40504
#define IDM_ARTICLE_BROWSER    40505

#define IDM_HELP_ABOUT         40600

/* The contextual menus' own commands -- the ones no menu in the bar has.
   Everything else on them sends the bar's command. */
#define IDM_CTX_REFRESH        40700  /* what is selected, not every feed */
#define IDM_CTX_OPEN_HOME      40701
#define IDM_CTX_COPY_FEED_URL  40702
#define IDM_CTX_COPY_HOME_URL  40703
#define IDM_CTX_COPY_ARTICLE_URL 40704

/* Child window identifiers. */
#define IDC_TOOLBAR             999
#define IDC_SIDEBAR            1000
#define IDC_HEADLINES          1001
#define IDC_READER             1002
#define IDC_SPLIT_LEFT         1003
#define IDC_SPLIT_RIGHT        1004
#define IDC_STATUSBAR          1005
#define IDC_SIDEBAR_HEADER     1006
#define IDC_LIST_HEADER        1007
#define IDC_SEARCH             1008
#define IDC_FINDICON           1009
#define IDC_TOOL_CHEVRON       1010   /* the toolbar's >> when it is full */

/* Dialog items. IDOK and IDCANCEL are Windows' own 1 and 2. */
#define IDC_FEED_NOTE          1100
#define IDC_FEED_NAME          1101
#define IDC_FEED_URL           1102
#define IDC_FEED_GROUP         1103
#define IDC_NAME_PROMPT        1110
#define IDC_NAME_TEXT          1111
#define IDC_PREFS_MINUTES      1120
#define IDC_PREFS_ARTICLES     1121
#define IDC_PREFS_PHOTOS       1122
#define IDC_SEARCH_TEXT        1130
#define IDC_SEARCH_CLEAR       1131

#endif /* GAZETTE_WIN_RES_H */
