/*
 * Gazette -- the Aqua toolbar (Mac OS X, osx-legacy only)
 * Copyright (c) 2026 brunocastello
 *
 * A real HIToolbar, nine items and an HISearchField, for the window that
 * platinum_window.c opens. See aqua_toolbar.h, and docs/osx-port.md Stage 2
 * part A for how a window with no standard handler gets its clicks and keys.
 *
 * Everything here is Tiger-safe: nothing newer than Mac OS X 10.4 is called,
 * and no CFSTR is used (the gcc 14 cross-compiler for 10.4 is not Apple's,
 * and Apple's constant-string builtin is not something to bet a build on).
 */
#ifdef GAZETTE_MACOSX

#include "ui/aqua_toolbar.h"

#include <CarbonEvents.h>
#include <CFArray.h>
#include <CFBundle.h>
#include <CFString.h>
#include <CGDataProvider.h>
#include <CGImage.h>
#include <ControlDefinitions.h>
#include <Controls.h>
#include <HIObject.h>
#include <HIToolbar.h>
#include <HIView.h>
#include <Icons.h>
#include <MacMemory.h>
#include <MacWindows.h>

#include <string.h>

enum {
    kSearchIndex  = kGazetteAquaItems,  /* after the nine */
    kSearchWidth  = 170,                /* fixed: Bruno wants it not to grow */
    kSearchHeight = 22,
    kLabelLen     = 48,
    kImageLen     = 24,
    kTextLen      = 128
};

/* Where each group begins: the same three breaks the Mac OS 9 bar has. The
   groups are separated by the toolbar's own space, as Mail's are. */
static const int kGroupStart[3] = { 1, 3, 5 };

static const char *const kIdentName[kGazetteAquaItems] = {
    "new", "sidebar", "refresh", "markall", "hideread", "markread", "star",
    "nextunread", "browser"
};
#define kIdentPrefix "org.brunocastello.gazette.toolbar."

/* What each item last was told, so that an item the toolbar makes afresh (the
   customise sheet makes copies) starts out right, and so that an update that
   changes nothing costs nothing. */
typedef struct {
    Boolean enabled;
    Boolean known;
    char    label[kLabelLen];
    char    image[kImageLen];
    short   iconID;
} ItemState;

static ItemState           gState[kGazetteAquaItems];
static WindowRef           gWindow;
static HIToolbarRef        gToolbar;
static HIViewRef           gField;
static Boolean             gSearchEnabled = true;
static char                gText[kTextLen];     /* the field's text, mirrored */
static Boolean             gInForward;          /* see WindowTextInput */
static GazetteAquaItemProc gOnItem;
static GazetteAquaProc     gOnSearch;
static GazetteAquaProc     gOnToggle;

static EventHandlerUPP gDelegateUPP;
static EventHandlerUPP gItemUPP;
static EventHandlerUPP gViewUPP;
static EventHandlerUPP gFieldUPP;
static EventHandlerUPP gWindowUPP;

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

/* A CFString from a MacRoman C string. The caller releases it. */
static CFStringRef MakeStr(const char *text)
{
    return CFStringCreateWithCString(NULL, text, kCFStringEncodingMacRoman);
}

static CFStringRef MakeIdentifier(int index)
{
    char name[96];

    if (index == kSearchIndex) {
        strcpy(name, kIdentPrefix "search");
    } else {
        strcpy(name, kIdentPrefix);
        strcat(name, kIdentName[index]);
    }
    return MakeStr(name);
}

/* Which of our items an identifier names, or -1 for anyone else's. */
static int IndexForIdentifier(CFStringRef ident)
{
    char buf[96];
    int  i;

    if (ident == NULL ||
        !CFStringGetCString(ident, buf, sizeof buf, kCFStringEncodingMacRoman)) {
        return -1;
    }
    if (strncmp(buf, kIdentPrefix, sizeof kIdentPrefix - 1) != 0) {
        return -1;
    }
    if (strcmp(buf + sizeof kIdentPrefix - 1, "search") == 0) {
        return kSearchIndex;
    }
    for (i = 0; i < kGazetteAquaItems; i++) {
        if (strcmp(buf + sizeof kIdentPrefix - 1, kIdentName[i]) == 0) {
            return i;
        }
    }
    return -1;
}

static void AppendSystem(CFMutableArrayRef array, const char *ident)
{
    CFStringRef s = MakeStr(ident);

    if (s != NULL) {
        CFArrayAppendValue(array, s);
        CFRelease(s);
    }
}

static void AppendOurs(CFMutableArrayRef array, int index)
{
    CFStringRef s = MakeIdentifier(index);

    if (s != NULL) {
        CFArrayAppendValue(array, s);
        CFRelease(s);
    }
}

/* The system's own items. Their identifiers are the kHIToolbar*Identifier
   strings in HIToolbar.h, written out here rather than through CFSTR. */
#define kSpaceIdent         "com.apple.hitoolbox.toolbar.space"
#define kFlexibleSpaceIdent "com.apple.hitoolbox.toolbar.flexiblespace"
#define kSeparatorIdent     "com.apple.hitoolbox.toolbar.separator"
#define kCustomizeIdent     "com.apple.hitoolbox.toolbar.customize"

/* ------------------------------------------------------------------ */
/* Images                                                              */
/* ------------------------------------------------------------------ */

/*
 * Contents/Resources/toolbar/<name>.png, 32x32 with alpha, or NULL. The
 * caller releases the image.
 */
static CGImageRef LoadPNG(const char *name)
{
    CFBundleRef     bundle = CFBundleGetMainBundle();
    CFStringRef     base, ext, dir;
    CFURLRef        url = NULL;
    CGDataProviderRef provider;
    CGImageRef      image = NULL;

    if (bundle == NULL || name == NULL || name[0] == '\0') {
        return NULL;
    }
    base = MakeStr(name);
    ext  = MakeStr("png");
    dir  = MakeStr("toolbar");
    if (base != NULL && ext != NULL && dir != NULL) {
        url = CFBundleCopyResourceURL(bundle, base, ext, dir);
    }
    if (base != NULL) CFRelease(base);
    if (ext != NULL)  CFRelease(ext);
    if (dir != NULL)  CFRelease(dir);
    if (url == NULL) {
        return NULL;
    }
    provider = CGDataProviderCreateWithURL(url);
    CFRelease(url);
    if (provider != NULL) {
        image = CGImageCreateWithPNGDataProvider(provider, NULL, true,
                                                 kCGRenderingIntentDefault);
        CGDataProviderRelease(provider);
    }
    return image;
}

/* The 16x16 suite from the application's resources, as an IconRef the
   toolbar can scale. Until the PNGs exist it is the picture, and blurry. */
static IconRef IconFromSuite(short resID)
{
    IconSuiteRef     suite  = NULL;
    IconFamilyHandle family = NULL;
    IconRef          ref    = NULL;

    if (resID == 0 || GetIconSuite(&suite, resID, svAllAvailableData) != noErr ||
        suite == NULL) {
        return NULL;
    }
    if (IconSuiteToIconFamily(suite, svAllAvailableData, &family) == noErr &&
        family != NULL) {
        HLock((Handle)family);
        if (GetIconRefFromIconFamilyPtr(*family, GetHandleSize((Handle)family),
                                        &ref) != noErr) {
            ref = NULL;
        }
        HUnlock((Handle)family);
        DisposeHandle((Handle)family);
    }
    /* false: the handles belong to the Resource Manager and are shared with
       the suites platinum_window.c loaded for the same IDs. */
    (void)DisposeIconSuite(suite, false);
    return ref;
}

static void ApplyImage(HIToolbarItemRef item, const ItemState *s)
{
    CGImageRef image = LoadPNG(s->image);

    if (image != NULL) {
        (void)HIToolbarItemSetImage(item, image);
        CGImageRelease(image);
    } else {
        IconRef ref = IconFromSuite(s->iconID);

        if (ref != NULL) {
            (void)HIToolbarItemSetIconRef(item, ref);
            (void)ReleaseIconRef(ref);
        }
    }
}

static void ApplyLabel(HIToolbarItemRef item, const ItemState *s)
{
    CFStringRef label = MakeStr(s->label);

    if (label != NULL) {
        (void)HIToolbarItemSetLabel(item, label);
        (void)HIToolbarItemSetHelpText(item, label, NULL);
        CFRelease(label);
    }
}

/* ------------------------------------------------------------------ */
/* Items                                                               */
/* ------------------------------------------------------------------ */

/*
 * A click on one of the nine. The item is clicked through HIViewClick (see
 * GazetteAquaToolbarMouseDown), the standard item view tracks the press, and
 * when it is let go on the item it sends this. The item has the command ID
 * kHIToolbarCommandPressAction, without which Apple's documentation says an
 * item is not clickable at all, and this handler is on the item, so there is
 * no question of where a kEventCommandProcess would have been sent.
 */
static OSStatus ItemAction(EventHandlerCallRef next, EventRef event, void *data)
{
    (void)next; (void)event;
    if (gOnItem != NULL) {
        gOnItem((int)(long)data);
    }
    return noErr;
}

/*
 * The search field is created when the item first needs a view, once per
 * toolbar it is shown in. It is fixed at one size by answering the size
 * query with a minimum equal to its maximum; Mail's grows, and Bruno's rule
 * is that this one does not.
 */
static OSStatus FieldSize(EventHandlerCallRef next, EventRef event, void *data)
{
    HISize size;

    (void)next; (void)data;
    size.width  = kSearchWidth;
    size.height = kSearchHeight;
    (void)SetEventParameter(event, kEventParamMinimumSize, typeHISize,
                            sizeof size, &size);
    (void)SetEventParameter(event, kEventParamMaximumSize, typeHISize,
                            sizeof size, &size);
    return noErr;
}

static void SetFieldText(HIViewRef field, const char *text)
{
    CFStringRef s = MakeStr(text);

    if (s != NULL) {
        (void)SetControlData(field, kControlEditTextPart,
                             kControlEditTextCFStringTag, sizeof s, &s);
        CFRelease(s);
        HIViewSetNeedsDisplay(field, true);
    }
}

/* The cancel button empties the field, and then the search is cleared too:
   the same act as pressing Return in an empty field. */
static OSStatus FieldCancel(EventHandlerCallRef next, EventRef event, void *data)
{
    (void)next; (void)event; (void)data;
    gText[0] = '\0';
    if (gField != NULL) {
        SetFieldText(gField, "");
    }
    if (gOnSearch != NULL) {
        gOnSearch();
    }
    return noErr;
}

/* The magnifier, with no menu behind it: it starts the search. */
static OSStatus FieldSearchClicked(EventHandlerCallRef next, EventRef event,
                                   void *data)
{
    (void)next; (void)event; (void)data;
    if (gOnSearch != NULL) {
        gOnSearch();
    }
    return noErr;
}

/*
 * The customise sheet makes its own copies of every item, the search item's
 * included, and each copy has a field. Only the field that is in the real
 * window is ours (gField); the others must never be. So a field is adopted
 * when its owner is gWindow, dropped when it moves elsewhere, and forgotten
 * when it is disposed of, whichever comes last.
 */
static OSStatus FieldOwnership(EventRef event, void *data)
{
    HIViewRef field = (HIViewRef)data;

    if (GetEventKind(event) == kEventControlDispose) {
        if (gField == field) {
            gField = NULL;
        }
    } else {
        WindowRef owner = NULL;

        (void)GetEventParameter(event, kEventParamControlCurrentOwningWindow,
                                typeWindowRef, NULL, sizeof owner, NULL, &owner);
        if (owner != NULL && owner == gWindow) {
            gField = field;
        } else if (gField == field) {
            gField = NULL;
        }
    }
    return eventNotHandledErr;              /* the view's own handling goes on */
}

static OSStatus FieldHandler(EventHandlerCallRef next, EventRef event, void *data)
{
    UInt32 cls  = GetEventClass(event);
    UInt32 kind = GetEventKind(event);

    if (cls == kEventClassControl && (kind == kEventControlDispose ||
                                      kind == kEventControlOwningWindowChanged)) {
        return FieldOwnership(event, data);
    }

    if (cls == kEventClassControl && kind == kEventControlGetSizeConstraints) {
        return FieldSize(next, event, data);
    }
    if (cls == kEventClassSearchField && kind == kEventSearchFieldCancelClicked) {
        return FieldCancel(next, event, data);
    }
    if (cls == kEventClassSearchField && kind == kEventSearchFieldSearchClicked) {
        return FieldSearchClicked(next, event, data);
    }
    return eventNotHandledErr;
}

static OSStatus CreateSearchView(EventRef event)
{
    static const EventTypeSpec kSpecs[5] = {
        { kEventClassControl,     kEventControlDispose },
        { kEventClassControl,     kEventControlOwningWindowChanged },
        { kEventClassControl,     kEventControlGetSizeConstraints },
        { kEventClassSearchField, kEventSearchFieldCancelClicked },
        { kEventClassSearchField, kEventSearchFieldSearchClicked }
    };
    HIRect      bounds;
    HIViewRef   field = NULL;
    CFStringRef hint  = MakeStr("Search");
    OSStatus    err;

    bounds.origin.x    = 0;
    bounds.origin.y    = 0;
    bounds.size.width  = kSearchWidth;
    bounds.size.height = kSearchHeight;
    err = HISearchFieldCreate(&bounds,
                              kHISearchFieldAttributesCancel |
                              kHISearchFieldAttributesSearchIcon,
                              NULL, hint, &field);
    if (hint != NULL) {
        CFRelease(hint);
    }
    if (err != noErr || field == NULL) {
        return err != noErr ? err : eventNotHandledErr;
    }
    (void)InstallEventHandler(GetControlEventTarget(field), gFieldUPP,
                              5, kSpecs, field, NULL);
    SetFieldText(field, gText);
    if (!gSearchEnabled) {
        DisableControl(field);
    }
    HIViewSetVisible(field, true);
    /* Not yet gField: it has no window until the toolbar embeds it, and the
       owning-window event above adopts it if it turns out to be the real one. */
    if (GetControlOwner(field) == gWindow) {
        gField = field;
    }
    return SetEventParameter(event, kEventParamControlRef, typeControlRef,
                             sizeof field, &field);
}

static OSStatus ViewHandler(EventHandlerCallRef next, EventRef event, void *data)
{
    (void)next; (void)data;
    if (GetEventClass(event) == kEventClassToolbarItem &&
        GetEventKind(event) == kEventToolbarItemCreateCustomView) {
        return CreateSearchView(event);
    }
    return eventNotHandledErr;
}

static HIToolbarItemRef MakeItem(int index)
{
    static const EventTypeSpec kAction[1] = {
        { kEventClassToolbarItem, kEventToolbarItemPerformAction }
    };
    static const EventTypeSpec kView[1] = {
        { kEventClassToolbarItem, kEventToolbarItemCreateCustomView }
    };
    CFStringRef      ident = MakeIdentifier(index);
    HIToolbarItemRef item  = NULL;

    if (ident == NULL ||
        HIToolbarItemCreate(ident, kHIToolbarItemNoAttributes, &item) != noErr ||
        item == NULL) {
        if (ident != NULL) CFRelease(ident);
        return NULL;
    }
    CFRelease(ident);

    if (index == kSearchIndex) {
        CFStringRef label = MakeStr("Search");

        if (label != NULL) {
            (void)HIToolbarItemSetLabel(item, label);
            CFRelease(label);
        }
        (void)InstallEventHandler(HIObjectGetEventTarget((HIObjectRef)item),
                                  gViewUPP, 1, kView, NULL, NULL);
    } else {
        const ItemState *s = &gState[index];

        (void)HIToolbarItemSetCommandID(item, kHIToolbarCommandPressAction);
        (void)InstallEventHandler(HIObjectGetEventTarget((HIObjectRef)item),
                                  gItemUPP, 1, kAction, (void *)(long)index,
                                  NULL);
        if (s->known) {
            ApplyLabel(item, s);
            ApplyImage(item, s);
            (void)HIToolbarItemSetEnabled(item, s->enabled);
        }
    }
    return item;
}

/* ------------------------------------------------------------------ */
/* The toolbar's delegate: what is on it, and how to make each thing    */
/* ------------------------------------------------------------------ */

static OSStatus DelegateHandler(EventHandlerCallRef next, EventRef event,
                                void *data)
{
    UInt32 kind = GetEventKind(event);
    int    i, g;

    (void)next; (void)data;

    if (kind == kEventToolbarGetDefaultIdentifiers ||
        kind == kEventToolbarGetAllowedIdentifiers) {
        CFMutableArrayRef array = NULL;

        if (GetEventParameter(event, kEventParamMutableArray,
                              typeCFMutableArrayRef, NULL, sizeof array, NULL,
                              &array) != noErr || array == NULL) {
            return eventNotHandledErr;
        }
        if (kind == kEventToolbarGetDefaultIdentifiers) {
            /* Mail's arrangement: groups apart by a space, then a flexible
               space to push the field to the right-hand end. */
            g = 0;
            for (i = 0; i < kGazetteAquaItems; i++) {
                if (g < 3 && i == kGroupStart[g]) {
                    AppendSystem(array, kSpaceIdent);
                    g++;
                }
                AppendOurs(array, i);
            }
            AppendSystem(array, kFlexibleSpaceIdent);
            AppendOurs(array, kSearchIndex);
        } else {
            for (i = 0; i <= kSearchIndex; i++) {
                AppendOurs(array, i);
            }
            AppendSystem(array, kSpaceIdent);
            AppendSystem(array, kFlexibleSpaceIdent);
            AppendSystem(array, kSeparatorIdent);
            AppendSystem(array, kCustomizeIdent);
        }
        return noErr;
    }

    if (kind == kEventToolbarCreateItemWithIdentifier) {
        CFStringRef      ident = NULL;
        HIToolbarItemRef item;
        int              index;

        if (GetEventParameter(event, kEventParamToolbarItemIdentifier,
                              typeCFStringRef, NULL, sizeof ident, NULL,
                              &ident) != noErr) {
            return eventNotHandledErr;
        }
        index = IndexForIdentifier(ident);
        if (index < 0) {
            return eventNotHandledErr;      /* the system's: it makes them */
        }
        item = MakeItem(index);
        if (item == NULL) {
            return eventNotHandledErr;
        }
        /* Not released: whether the toolbar takes its own reference is not
           documented, and a few bytes leaked beat a crash. */
        return SetEventParameter(event, kEventParamToolbarItem,
                                 typeHIToolbarItemRef, sizeof item, &item);
    }
    return eventNotHandledErr;
}

/* ------------------------------------------------------------------ */
/* The window: the keyboard, and the toolbar button                      */
/* ------------------------------------------------------------------ */

static void CycleDisplayMode(void)
{
    HIToolbarDisplayMode mode = kHIToolbarDisplayModeDefault;

    if (gToolbar == NULL ||
        HIToolbarGetDisplayMode(gToolbar, &mode) != noErr) {
        return;
    }
    /* Icon and label, icon only, label only, and round again. Default is the
       system's choice, which is icon and label. */
    if (mode == kHIToolbarDisplayModeDefault ||
        mode == kHIToolbarDisplayModeIconAndLabel) {
        mode = kHIToolbarDisplayModeIconOnly;
    } else if (mode == kHIToolbarDisplayModeIconOnly) {
        mode = kHIToolbarDisplayModeLabelOnly;
    } else {
        mode = kHIToolbarDisplayModeIconAndLabel;
    }
    (void)HIToolbarSetDisplayMode(gToolbar, mode);
}

/* The toolbar button in the title bar: plain, it shows or hides the toolbar;
   with Command it cycles how the toolbar is displayed, as Apple's does. */
static void ToolbarButtonClicked(UInt32 modifiers)
{
    if ((modifiers & cmdKey) != 0) {
        CycleDisplayMode();
    } else if (gOnToggle != NULL) {
        gOnToggle();
    }
}

/*
 * Text input for a window with no standard handler.
 *
 * With the standard handler installed, the handler is what hands a keystroke
 * to the view that has the keyboard focus. This window has none, so the
 * keystroke would reach nobody: it comes back out of the event loop as a
 * plain keyDown, and the field never sees it. The Text Services layer sends a
 * kEventTextInputUnicodeForKeyEvent to the user focus (the front window)
 * *before* that happens, and this is the handler that catches it. If the
 * search field has the focus the event goes to the field, which inserts the
 * character, with the input methods working; otherwise it is declined, and
 * the keystroke comes out as a keyDown for the shell to use as it always has.
 * Return and Escape are the shell's, so they are taken here.
 *
 * gInForward stops a loop: an event the field does not handle is passed up
 * the view hierarchy to the window, which is this handler again.
 */
static OSStatus WindowTextInput(EventHandlerCallRef next, EventRef event)
{
    EventRef raw = NULL;
    char     code = 0;
    UInt32   mods = 0;
    OSStatus err;

    (void)next;
    if (gInForward || !GazetteAquaSearchHasFocus()) {
        return eventNotHandledErr;
    }
    if (GetEventParameter(event, kEventParamTextInputSendKeyboardEvent,
                          typeEventRef, NULL, sizeof raw, NULL,
                          &raw) == noErr && raw != NULL) {
        (void)GetEventParameter(raw, kEventParamKeyMacCharCodes, typeChar,
                                NULL, sizeof code, NULL, &code);
        (void)GetEventParameter(raw, kEventParamKeyModifiers, typeUInt32,
                                NULL, sizeof mods, NULL, &mods);
    }
    if ((mods & cmdKey) != 0) {
        return eventNotHandledErr;          /* a menu key */
    }
    if (code == '\r' || code == 3) {        /* Return, Enter */
        if (gOnSearch != NULL) {
            gOnSearch();
        }
        return noErr;
    }
    if (code == 0x1B) {                     /* Escape */
        GazetteAquaSetSearchText("");
        if (gOnSearch != NULL) {
            gOnSearch();
        }
        return noErr;
    }
    gInForward = true;
    err = SendEventToEventTarget(event, GetControlEventTarget(gField));
    gInForward = false;
    return err;
}

static OSStatus WindowHandler(EventHandlerCallRef next, EventRef event, void *data)
{
    UInt32 cls  = GetEventClass(event);
    UInt32 kind = GetEventKind(event);

    (void)data;
    if (cls == kEventClassTextInput && kind == kEventTextInputUnicodeForKeyEvent) {
        return WindowTextInput(next, event);
    }
    if (cls == kEventClassWindow && kind == kEventWindowToolbarSwitchMode) {
        /* Sent by the system's own handler, if it ever sees the click first;
           the shell's mouse-down path handles the click itself otherwise, so
           the two never both fire for one click. */
        ToolbarButtonClicked(GetCurrentKeyModifiers());
        return noErr;
    }
    return eventNotHandledErr;
}

/* ------------------------------------------------------------------ */
/* Creating and disposing                                              */
/* ------------------------------------------------------------------ */

Boolean GazetteAquaToolbarCreate(WindowRef window, GazetteAquaItemProc onItem,
                                 GazetteAquaProc onSearch,
                                 GazetteAquaProc onToggle)
{
    static const EventTypeSpec kDelegate[3] = {
        { kEventClassToolbar, kEventToolbarGetDefaultIdentifiers },
        { kEventClassToolbar, kEventToolbarGetAllowedIdentifiers },
        { kEventClassToolbar, kEventToolbarCreateItemWithIdentifier }
    };
    static const EventTypeSpec kWindow[2] = {
        { kEventClassTextInput, kEventTextInputUnicodeForKeyEvent },
        { kEventClassWindow,    kEventWindowToolbarSwitchMode }
    };
    HIToolbarRef toolbar = NULL;
    CFStringRef  ident;
    OSStatus     err;

    if (window == NULL || gWindow != NULL) {
        return false;
    }
    gOnItem   = onItem;
    gOnSearch = onSearch;
    gOnToggle = onToggle;

    gDelegateUPP = NewEventHandlerUPP(DelegateHandler);
    gItemUPP     = NewEventHandlerUPP(ItemAction);
    gViewUPP     = NewEventHandlerUPP(ViewHandler);
    gFieldUPP    = NewEventHandlerUPP(FieldHandler);
    gWindowUPP   = NewEventHandlerUPP(WindowHandler);

    ident = MakeStr("org.brunocastello.gazette.toolbar");
    if (ident == NULL) {
        return false;
    }
    err = HIToolbarCreate(ident,
                          kHIToolbarAutoSavesConfig | kHIToolbarIsConfigurable,
                          &toolbar);
    CFRelease(ident);
    if (err != noErr || toolbar == NULL) {
        return false;
    }

    /* The toolbar is its own delegate, so the handler for the delegate's
       events goes on its target. */
    (void)InstallEventHandler(HIObjectGetEventTarget((HIObjectRef)toolbar),
                              gDelegateUPP, 3, kDelegate, NULL, NULL);
    (void)HIToolbarSetDelegate(toolbar, (HIObjectRef)toolbar);

    (void)InstallEventHandler(GetWindowEventTarget(window), gWindowUPP, 2,
                              kWindow, NULL, NULL);

    err = SetWindowToolbar(window, toolbar);
    CFRelease(toolbar);                     /* the window holds it now */
    if (err != noErr) {
        return false;
    }
    gWindow = window;
    (void)GetWindowToolbar(window, &gToolbar);
    return true;
}

void GazetteAquaToolbarDispose(void)
{
    /* The window owns the toolbar and takes it, its items and the field with
       it. The handlers were on those, so they go too; only our pointers need
       forgetting. The UPPs are kept for the life of the process. */
    gWindow  = NULL;
    gToolbar = NULL;
    gField   = NULL;
    gOnItem   = NULL;
    gOnSearch = NULL;
    gOnToggle = NULL;
}

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

void GazetteAquaToolbarSetItem(int index, Boolean enabled, const char *label,
                               const char *imageName, short iconID)
{
    ItemState *s;
    Boolean    changed;
    CFArrayRef items = NULL;

    if (index < 0 || index >= kGazetteAquaItems) {
        return;
    }
    if (label == NULL)     label = "";
    if (imageName == NULL) imageName = "";
    s = &gState[index];

    changed = (Boolean)(!s->known || s->enabled != enabled ||
                        s->iconID != iconID ||
                        strncmp(s->label, label, kLabelLen - 1) != 0 ||
                        strncmp(s->image, imageName, kImageLen - 1) != 0);
    if (!changed) {
        return;
    }
    s->known   = true;
    s->enabled = enabled;
    s->iconID  = iconID;
    strncpy(s->label, label, kLabelLen - 1);
    s->label[kLabelLen - 1] = '\0';
    strncpy(s->image, imageName, kImageLen - 1);
    s->image[kImageLen - 1] = '\0';

    /* The toolbar's real items are asked for each time rather than kept: the
       customise sheet makes and drops copies, and a pointer kept across that
       would dangle. */
    if (gToolbar == NULL ||
        HIToolbarCopyItems(gToolbar, &items) != noErr || items == NULL) {
        return;
    }
    {
        CFIndex n = CFArrayGetCount(items);
        CFIndex k;

        for (k = 0; k < n; k++) {
            HIToolbarItemRef item = (HIToolbarItemRef)CFArrayGetValueAtIndex(items, k);
            CFStringRef      ident = NULL;

            if (HIToolbarItemCopyIdentifier(item, &ident) != noErr) {
                continue;
            }
            if (IndexForIdentifier(ident) == index) {
                ApplyLabel(item, s);
                ApplyImage(item, s);
                (void)HIToolbarItemSetEnabled(item, s->enabled);
            }
            if (ident != NULL) {
                CFRelease(ident);
            }
        }
    }
    CFRelease(items);
}

void GazetteAquaToolbarSetSearchEnabled(Boolean enabled)
{
    if (gSearchEnabled == enabled) {
        return;
    }
    gSearchEnabled = enabled;
    if (gField != NULL) {
        if (enabled) {
            EnableControl(gField);
        } else {
            DisableControl(gField);
        }
    }
}

void GazetteAquaToolbarShow(Boolean show)
{
    if (gWindow != NULL && !show) {
        GazetteAquaSearchDefocus();         /* the keyboard must not stay in it */
    }
    if (gWindow != NULL && IsWindowToolbarVisible(gWindow) != show) {
        (void)ShowHideWindowToolbar(gWindow, show, false);
    }
}

/* ------------------------------------------------------------------ */
/* Clicks                                                              */
/* ------------------------------------------------------------------ */

static Boolean IsSelfOrAncestor(HIViewRef view, HIViewRef from)
{
    HIViewRef v;

    for (v = from; v != NULL; v = HIViewGetSuperview(v)) {
        if (v == view) {
            return true;
        }
    }
    return false;
}

/*
 * The view a click in the toolbar band is for. The mouse event is handed to
 * HIViewGetViewForMouseEvent twice, starting once from the window's root view
 * and once from the topmost view above it: whether Tiger keeps a non-
 * composited window's toolbar under the root or in the frame above it is not
 * documented, and asking from both costs nothing. What comes back must not be
 * the root or one of its ancestors, which would only mean that the click fell
 * on the window's frame.
 */
static HIViewRef ToolbarViewFor(WindowRef window, EventRef event)
{
    HIViewRef root = HIViewGetRoot(window);
    HIViewRef top  = root;
    HIViewRef parent;
    HIViewRef start[2];
    int       i;

    if (root == NULL) {
        return NULL;
    }
    while ((parent = HIViewGetSuperview(top)) != NULL) {
        top = parent;
    }
    start[0] = root;
    start[1] = top;
    for (i = 0; i < 2; i++) {
        HIViewRef hit = NULL;

        if (i == 1 && start[1] == start[0]) {
            break;
        }
        if (HIViewGetViewForMouseEvent(start[i], event, &hit) == noErr &&
            hit != NULL && !IsSelfOrAncestor(hit, root)) {
            return hit;
        }
    }
    return NULL;
}

/* A mouse-down event for HIViewGetViewForMouseEvent, built from the
   EventRecord when the current event is not already one. */
static EventRef MouseDownEvent(WindowRef window, const EventRecord *rec,
                               short part, Boolean *ours)
{
    EventRef cur = GetCurrentEvent();
    EventRef made = NULL;
    HIPoint  global, local;
    Rect     structure;
    UInt32   clicks = 1, mods = rec->modifiers;
    EventMouseButton button = kEventMouseButtonPrimary;

    *ours = false;
    if (cur != NULL && GetEventClass(cur) == kEventClassMouse &&
        GetEventKind(cur) == kEventMouseDown) {
        return cur;
    }
    if (CreateEvent(NULL, kEventClassMouse, kEventMouseDown,
                    GetCurrentEventTime(), kEventAttributeNone, &made) != noErr ||
        made == NULL) {
        return NULL;
    }
    *ours = true;
    if (GetWindowBounds(window, kWindowStructureRgn, &structure) != noErr) {
        structure.left = structure.top = 0;
    }
    global.x = rec->where.h;
    global.y = rec->where.v;
    local.x  = global.x - structure.left;       /* from the structure's corner */
    local.y  = global.y - structure.top;

    (void)SetEventParameter(made, kEventParamMouseLocation, typeHIPoint,
                            sizeof global, &global);
    (void)SetEventParameter(made, kEventParamWindowMouseLocation, typeHIPoint,
                            sizeof local, &local);
    (void)SetEventParameter(made, kEventParamWindowRef, typeWindowRef,
                            sizeof window, &window);
    (void)SetEventParameter(made, kEventParamWindowPartCode, typeWindowPartCode,
                            sizeof part, &part);
    (void)SetEventParameter(made, kEventParamMouseButton, typeMouseButton,
                            sizeof button, &button);
    (void)SetEventParameter(made, kEventParamClickCount, typeUInt32,
                            sizeof clicks, &clicks);
    (void)SetEventParameter(made, kEventParamKeyModifiers, typeUInt32,
                            sizeof mods, &mods);
    return made;
}

/*
 * Is there anything under the mouse to click? The toolbar's own view, a
 * space and a flexible space are views too, and HIViewClick on them returns
 * noErr, but they have no part to hit. The point is tried in both window
 * coordinate systems (from the content's corner and from the structure's)
 * because the header does not say which a toolbar view's window-relative
 * point means; a part found in either counts.
 */
static Boolean HasPartUnderMouse(WindowRef window, HIViewRef view,
                                 const EventRecord *event)
{
    Rect content, structure;
    int  pass;

    if (gField != NULL && IsSelfOrAncestor(gField, view)) {
        return true;
    }
    if (GetWindowBounds(window, kWindowContentRgn, &content) != noErr ||
        GetWindowBounds(window, kWindowStructureRgn, &structure) != noErr) {
        return false;
    }
    for (pass = 0; pass < 2; pass++) {
        const Rect    *origin = pass == 0 ? &content : &structure;
        HIPoint        pt;
        HIViewPartCode part = kControlNoPart;

        pt.x = event->where.h - origin->left;
        pt.y = event->where.v - origin->top;
        if (HIViewConvertPoint(&pt, NULL, view) == noErr &&
            HIViewGetPartHit(view, &pt, &part) == noErr &&
            part != kControlNoPart) {
            return true;
        }
    }
    return false;
}

static Boolean TrackToolbarButton(WindowRef window, const EventRecord *event)
{
    Point    where;
    WindowRef under = NULL;

    while (StillDown()) {
        /* nothing: the click is decided on release, as a button's is */
    }
    GetGlobalMouse(&where);
    if (FindWindow(where, &under) == inToolbarButton && under == window) {
        ToolbarButtonClicked(event->modifiers);
    }
    return true;
}

Boolean GazetteAquaToolbarMouseDown(WindowRef window, const EventRecord *event,
                                    short part)
{
    Rect      structure, content;
    EventRef  me;
    Boolean   ours;
    HIViewRef view;
    OSStatus  err;

    if (window == NULL || window != gWindow || event == NULL) {
        return false;
    }

    /* The toolbar button in the title bar. */
    if (part == inToolbarButton) {
        if (FrontWindow() != window) {
            SelectWindow(window);
            return true;
        }
        return TrackToolbarButton(window, event);
    }

    /* Only the band between the top of the window and the top of its content
       can be the toolbar; everything else is the shell's. */
    if (!IsWindowToolbarVisible(window) ||
        GetWindowBounds(window, kWindowStructureRgn, &structure) != noErr ||
        GetWindowBounds(window, kWindowContentRgn, &content) != noErr ||
        event->where.v < structure.top || event->where.v >= content.top ||
        event->where.h < content.left || event->where.h >= content.right) {
        return false;
    }

    me = MouseDownEvent(window, event, part, &ours);
    if (me == NULL) {
        return false;
    }
    view = ToolbarViewFor(window, me);
    if (view == NULL) {
        if (ours) ReleaseEvent(me);
        return false;                       /* the title bar: drag it */
    }
    if (!HasPartUnderMouse(window, view, event)) {
        if (ours) ReleaseEvent(me);
        return false;                       /* a gap or a space: drag it */
    }
    if (FrontWindow() != window) {
        /* The first click only brings the window forward. */
        SelectWindow(window);
        if (ours) ReleaseEvent(me);
        return true;
    }
    err = HIViewClick(view, me);
    if (ours) ReleaseEvent(me);
    if (err != noErr) {
        return false;                       /* a gap in the bar: drag it */
    }
    /* A click in the field leaves the keyboard in it. */
    if (gField != NULL && IsSelfOrAncestor(gField, view)) {
        (void)SetKeyboardFocus(window, gField, kControlEditTextPart);
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* The search field's text                                             */
/* ------------------------------------------------------------------ */

void GazetteAquaSearchText(char *out, size_t cap)
{
    CFStringRef s = NULL;

    if (out == NULL || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (gField != NULL) {
        Size size = 0;

        if (GetControlData(gField, kControlEditTextPart,
                           kControlEditTextCFStringTag, sizeof s, &s,
                           &size) == noErr && s != NULL) {
            CFIndex used = 0;

            /* MacRoman, with '?' for what it cannot hold; the engine's text
               is ASCII-flattened anyway. */
            (void)CFStringGetBytes(s, CFRangeMake(0, CFStringGetLength(s)),
                                   kCFStringEncodingMacRoman, '?', false,
                                   (UInt8 *)out, (CFIndex)(cap - 1), &used);
            out[used] = '\0';
            CFRelease(s);
            strncpy(gText, out, kTextLen - 1);
            gText[kTextLen - 1] = '\0';
            return;
        }
    }
    /* No field yet (the toolbar is hidden or not built): what was last set. */
    strncpy(out, gText, cap - 1);
    out[cap - 1] = '\0';
}

void GazetteAquaSetSearchText(const char *text)
{
    if (text == NULL) {
        text = "";
    }
    strncpy(gText, text, kTextLen - 1);
    gText[kTextLen - 1] = '\0';
    if (gField != NULL) {
        SetFieldText(gField, gText);
    }
}

void GazetteAquaSearchDefocus(void)
{
    if (gWindow != NULL && gField != NULL && GazetteAquaSearchHasFocus()) {
        (void)SetKeyboardFocus(gWindow, gField, kControlFocusNoPart);
    }
}

Boolean GazetteAquaSearchHasFocus(void)
{
    HIViewPartCode part = kControlFocusNoPart;

    if (gField == NULL ||
        HIViewGetFocusPart(gField, &part) != noErr) {
        return false;
    }
    return (Boolean)(part != kControlFocusNoPart);
}

#endif /* GAZETTE_MACOSX */
