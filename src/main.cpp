/*
 * Gazette — Carbon RSS / Atom Reader for Mac OS 9 & Mac OS X
 * Copyright (c) 2026 brunocastello
 *
 * Phase 0: Skeleton — Carbon shell, event loop, menus, quit.
 * Uses Apple's Universal Interfaces (Carbon API).
 */

#include <Carbon/Carbon.h>
#include "core/gazette_core.h"

/* ------------------------------------------------------------------ */
/* Forward declarations                                              */
/* ------------------------------------------------------------------ */

static Boolean InitGazette(void);
static void   RunGazette(void);
static void   DoExitGazette(void);

static void   HandleMenuChoice(MenuRef menuRef, SInt32 itemIndex);
static void   DoEvent(EventRef event);

static void   HandleAbout(void);
static void   HandleQuit(void);
static void   DrawPlatinumWindow(WindowRef window);

/* ------------------------------------------------------------------ */
/* Application globals                                               */
/* ------------------------------------------------------------------ */

static Boolean gDone = false;
static WindowRef gMainWindow = nil;

/* Menu IDs — Carbon convention */
enum {
    kMenuApple  = 128,
    kMenuFile   = 129,
    kMenuEdit   = 130,
    kMenuWindow = 131
};

/* Menu item IDs */
enum {
    kMenuItemQuit       = 1,
    kMenuItemAbout      = 1,
    kMenuItemUndo       = 1,
    kMenuItemCut        = 2,
    kMenuItemCopy       = 3,
    kMenuItemPaste      = 4,
    kMenuItemClear      = 5,
    kMenuItemCloseWin   = 1
};

/* ------------------------------------------------------------------ */
/* Entry point — Carbon main                                         */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    if (!InitGazette()) {
        DoExitGazette();
    }

    RunGazette();
    DoExitGazette(); /* should not reach here */

    return 0;
}

/* ------------------------------------------------------------------ */
/* Initialization — create menus, window, cursor                       */
/* ------------------------------------------------------------------ */

static Boolean InitGazette(void)
{
    /* Carbon handles memory management; SIZE resource is optional. */

    InitCursor();

    /* Create menus programmatically (no resource fork needed) */
    {
        MenuRef appleMenu, fileMenu, editMenu, windowMenu;

        /* Apple menu (ID 128) — system populates first item */
        appleMenu = CreateMenu(kMenuApple, "\pGazette");
        InsertMenuItemWithAccel(appleMenu, "\pAbout Gazette...", kMenuItemAbout, 'Q', 0);
        InsertMenuSeparator(appleMenu);
        InsertMenuItemWithAccel(appleMenu, "\pServices", 0, 0, 0);
        InsertMenuSeparator(appleMenu);

        /* File menu (ID 129) */
        fileMenu = CreateMenu(kMenuFile, "\pFile");
        InsertMenuItemWithAccel(fileMenu, "\pQuit", kMenuItemQuit, 'Q', 0);
        InsertMenu(fileMenu, nil);

        /* Edit menu (ID 130) */
        editMenu = CreateMenu(kMenuEdit, "\pEdit");
        InsertMenuItemWithAccel(editMenu, "\pUndo", kMenuItemUndo, 'Z', 0);
        InsertMenuSeparator(editMenu);
        InsertMenuItemWithAccel(editMenu, "\pCut", kMenuItemCut, 'X', 0);
        InsertMenuItemWithAccel(editMenu, "\pCopy", kMenuItemCopy, 'C', 0);
        InsertMenuItemWithAccel(editMenu, "\pPaste", kMenuItemPaste, 'V', 0);
        InsertMenuItemWithAccel(editMenu, "\pClear", kMenuItemClear, 0, 0);
        InsertMenu(editMenu, nil);

        /* Window menu (ID 131) */
        windowMenu = CreateMenu(kMenuWindow, "\pWindow");
        InsertMenuItemWithAccel(windowMenu, "\pClose Window", kMenuItemCloseWin, 'W', 0);
        InsertMenu(windowMenu, nil);

        DrawMenuBar();
    }

    /* Create the main window */
    {
        Rect bounds = { 40, 60, 520, 780 };
        Str255 title;

        StringCp(title, "\pGazette — RSS / Atom Reader");
        gMainWindow = CreateWindow(
            kDocumentWindowClass,
            nil,
            &bounds,
            title,
            kWindowStandardHandlerAttribute | kWindowTearOffMenuAttribute |
            kWindowLiveResizeAttribute,
            nil
        );

        if (gMainWindow) {
            ShowWindow(gMainWindow);
            SelectWindow(gMainWindow);

            /* Draw initial Platinum grey fill */
            DrawPlatinumWindow(gMainWindow);
        }
    }

    return gMainWindow != nil;
}

/* ------------------------------------------------------------------ */
/* Main event loop — single GetNextEvent, cooperative multitasking     */
/* ------------------------------------------------------------------ */

static void RunGazette(void)
{
    EventRef   event;
    MenuRef    menuRef = nil;
    SInt32     itemIndex = 0;

    while (!gDone) {
        /* Wait for any event, yielding to the system */
        if (GetNextEvent(kHighLevelEventMask | keyDownMask | mouseUpMask, &event) == noErr) {
            /* Dispatch the event */
            DoEvent(event);

            /* Check if a menu was selected (from MenuSelect) */
            GetEventParameter(event, kEventParamMenuRef, typeMenuRef, NULL, sizeof(MenuRef), NULL, &menuRef);
            GetEventParameter(event, kEventParamMenuItemIndex, typeSInt32, NULL, sizeof(SInt32), NULL, &itemIndex);

            if (menuRef) {
                HandleMenuChoice(menuRef, itemIndex);
            }

            ReleaseEvent(event);
        } else {
            /* No event — yield to the system (cooperative multitasking) */
            WaitNextEvent(kHighLevelEventMask | keyDownMask, &event, 60);
            DoEvent(event);
            ReleaseEvent(event);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Menu handling — dispatch menu item actions                          */
/* ------------------------------------------------------------------ */

static void HandleMenuChoice(MenuRef menuRef, SInt32 itemIndex)
{
    SInt32 menuID = 0;

    GetMenuItemCommandID(menuRef, itemIndex, (EventCommandID *)&menuID);
    (void)GetMenuID(menuRef, &menuID);

    /* Determine which menu this belongs to */
    if (menuRef) {
        GetMenuID(menuRef, &menuID);
    }

    switch (menuID) {
        case kMenuApple:
            if (itemIndex == kMenuItemAbout) {
                HandleAbout();
            }
            break;

        case kMenuFile:
            if (itemIndex == kMenuItemQuit) {
                HandleQuit();
            }
            break;

        case kMenuWindow:
            if (itemIndex == kMenuItemCloseWin) {
                /* Close current window — for Phase 3 multi-window support */
                if (gMainWindow) {
                    CloseWindow(gMainWindow);
                    gMainWindow = nil;
                }
            }
            break;

        default:
            SysBeep(10);
            break;
    }
}

/* ------------------------------------------------------------------ */
/* Event dispatch — mouse clicks, keyboard                           */
/* ------------------------------------------------------------------ */

static void DoEvent(EventRef event)
{
    SInt32 message;
    GetEventParameter(event, kEventParamMessage, typeSInt32, NULL, sizeof(SInt32), NULL, &message);

    switch (message) {
        case kEventMouseDown: {
            Point where;
            WindowRef window = nil;

            GetEventParameter(event, kEventParamMouseLocation, typeQDPoint, NULL, sizeof(Point), NULL, &where);
            GetEventParameter(event, kEventParamMouseWindow, typeWindowRef, NULL, sizeof(WindowRef), NULL, &window);

            switch (FindWindow(where, &window)) {
                case inMenuBar: {
                    MenuRef menuRef;
                    SInt32 itemIndex;

                    MenuSelect(where, &menuRef, &itemIndex);
                    if (menuRef) {
                        HandleMenuChoice(menuRef, itemIndex);
                        DrawMenuBar();
                    }
                    break;
                }

                case inSysWindow:
                    SystemClick(event, window);
                    break;

                case inContent:
                    if (window && FrontWindow() != window) {
                        SelectWindow(window);
                    }
                    break;

                case inGoAway:
                    if (TrackGoAway(window, where)) {
                        gDone = true;
                    }
                    break;

                default:
                    break;
            }
            break;
        }

        case kEventKeyboardKeyPress: {
            SInt32 keyCode, modifiers;

            GetEventParameter(event, kEventParamKeyCode, typeSInt32, NULL, sizeof(SInt32), NULL, &keyCode);
            GetEventParameter(event, kEventParamKeyModifiers, typeSInt32, NULL, sizeof(SInt32), NULL, &modifiers);

            /* Command-key shortcuts — dispatch directly (Carbon style) */
            if (modifiers & cmdKey) {
                switch (keyCode) {
                    case kVK_q: /* Cmd+Q */
                        HandleQuit();
                        break;

                    case kVK_comma: /* Cmd+, → About */
                        HandleAbout();
                        break;

                    case kVK_w: /* Cmd+W — close window */
                        if (gMainWindow) {
                            CloseWindow(gMainWindow);
                            gMainWindow = nil;
                        }
                        break;

                    default:
                        break;
                }
            }
            break;
        }

        default:
            break;
    }
}

/* ------------------------------------------------------------------ */
/* Menu actions                                                      */
/* ------------------------------------------------------------------ */

static void HandleAbout(void)
{
    /* Simple About box — Carbon alert dialog (stub for Phase 3) */
    CFStringRef title = CFStringCreateWithCString(nil, "About Gazette", kCFStringEncodingASCII);
    CFStringRef message = CFStringCreateWithCString(
        nil,
        "Gazette v0.1\n"
        "RSS / Atom Reader for Mac OS 9 & Mac OS X\n"
        "\n"
        "Built with Carbon. Uses Gateway networking\n"
        "and NewsProxy feed intelligence.\n"
        "\n"
        "Platinum UI. PowerPC.",
        kCFStringEncodingASCII
    );

    CFShow(title); /* Stub: replace with proper alert dialog in Phase 3 */
    CFRelease(message);
    CFRelease(title);
}

static void HandleQuit(void)
{
    gDone = true;
}

/* ------------------------------------------------------------------ */
/* Draw a basic Platinum-style window (Carbon version)                 */
/* ------------------------------------------------------------------ */

static void DrawPlatinumWindow(WindowRef window)
{
    Rect bounds;
    CGrafPtr oldPort;

    GetWindowBounds(window, &bounds);
    GetPort(&oldPort);
    SetPort(window);

    /* Fill with Platinum grey (RGB 254, 254, 254) */
    RGBColor platinum = { 0xFEFE, 0xFEFE, 0xFEFE };
    RGBBackColor(&platinum);
    EraseRect(&bounds);

    /* Draw a subtle border */
    PenNormal();
    RGBColor black = { 0, 0, 0 };
    RGBForeColor(&black);

    FrameRoundRect(&bounds, 12, 12);

    SetPort(oldPort);
}

/* ------------------------------------------------------------------ */
/* Cleanup                                                             */
/* ------------------------------------------------------------------ */

static void DoExitGazette(void)
{
    if (gMainWindow) {
        CloseWindow(gMainWindow);
        gMainWindow = nil;
    }

    /* Dispose all menus */
    {
        MenuRef menu;
        short id;

        for (id = kMenuApple; id <= kMenuWindow; id++) {
            menu = GetMenuRef(id);
            if (menu) {
                DisposeMenu(menu);
            }
        }
    }

    ExitToShell();
}
