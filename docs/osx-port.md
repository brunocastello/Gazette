# Gazette on Mac OS X and macOS

Plan and ownership map for the two Mac OS X builds. CLAUDE.md holds the rules; this file holds the plan. Written 2026-10-31, the start of the `osx-legacy` stage.

## The two builds

| | `osx-legacy` | `macos-universal` |
|---|---|---|
| Systems | Mac OS X 10.4 – 10.14 | macOS 10.13 – current |
| Architectures | ppc + i386 (Universal Binary) | x86_64 + arm64 (Universal 2) |
| Shell | the Mac OS 9 Carbon shell (`src/main.cpp`, `src/ui/`), compiled as Mach-O | AppKit, `src/cocoa/` (not started) |
| Event loop | `WaitNextEvent`, as on Mac OS 9 | `NSApplication` run loop + timer |
| Network | BSD sockets + Certainly/BearSSL (`GAZETTE_USE_EMBEDDED_TLS`) | `URLSession` (`GAZETTE_USE_NATIVE_TLS`) |
| Toolchain | iWordle's: gcc 14 cross (ppc) + clang/ld64 (i386) against the 10.4u SDK, on Ubuntu | Xcode on `macos-latest` |
| Workflow | `.github/workflows/osx-legacy.yml` | `.github/workflows/macos.yml` (later) |

10.13 and 10.14 are covered by both; that overlap is what closes Catalina's gap (no 32-bit).

## `osx-legacy`: what changes, file by file

The Carbon shell is CarbonLib code and Carbon on OS X runs it with Aqua controls, so this stage is mostly a re-link. What differs from the Mac OS 9 build:

| Area | Mac OS 9 | Mac OS X (`osx-legacy`) | Where |
|---|---|---|---|
| TCP | Open Transport (`transport_ot.c`, `gazette_net_ot.c`) | BSD sockets (`transport_posix.c`, `gazette_net_posix.c`) | Certainly, `src/net/` |
| Entropy | Toolbox sources (`entropy.c`) | `/dev/urandom` + timing (`entropy_posix.c`) | Certainly |
| Certainly compat | `CERTAINLY_OPEN_TRANSPORT` | `CERTAINLY_POSIX` (calloc, monotonic TickCount) | `certainly_compat.h`, PATCHES.md |
| Headers | Universal Interfaces 3.4, flat | 10.4u SDK framework `Headers` dirs on the include path, still flat | build script |
| Pascal strings | `-fpascal-strings` (Retro68) | rewritten at build time by `tools/pascal_strings.py`; sources unchanged | build script |
| Resources | resource fork | `Contents/Resources/Gazette.rsrc` (Retro68 Rez, data fork) | workflow |
| Icon | `ICN#`/`icl8`/`ics#`/`ics8` | `Gazette.icns` assembled from the same classic elements (proper large art is an X4 item) | workflow |
| Quit | File > Quit | application menu (Aqua layout), `kAEQuitApplication` | `src/main.cpp`, behind `GAZETTE_MACOSX` |
| Drawing outside updates | immediate | `QDFlushPortBuffer` in tracking loops | `src/ui/` |
| Byte order | big-endian | ppc big, i386 little; the shell reads only system resource types, which the Resource Manager swaps itself, so nothing changed | (checked) |
| Prefs and cache | System Folder:Preferences | `~/Library/Preferences` via `FindFolder(kUserDomain, ...)`, prefs file and `Gazette Cache` folder alike (same names and formats) | `src/store/gazette_store.c` |
| User agent | `(Macintosh; Mac OS 9)` | `(Macintosh; Mac OS X)` via `GAZETTE_MACOSX` | `src/portable/gazette_http.h` |

Engine (`src/app`, `src/core`, `src/feeds`, `src/extract`, `src/prefs`, `src/portable`): unchanged. It already runs little-endian on Windows.

## Proof

- `net-posix.yml`: the POSIX transport, Certainly and the fetch run as a console test on `ubuntu-latest` and `macos-latest` against live TLS 1.3 and TLS 1.2 servers, the same URLs as the Windows test under Wine.
- `osx-legacy.yml`: `lipo -info` shows ppc and i386; artifact `Gazette-mac-os-x` (`.zip` + `.dmg`).
- Mac OS 9 (`build.yml`) and Windows (`windows.yml`) stay green; host tests gate every job.
- On real systems (Bruno, in UTM): 10.4 PowerPC and one Intel 10.6–10.10 show live headlines.

## Process

Plan (this file, lead) → implementation by `porter` subagents on separate branches (`osx-legacy-net` for the network, `osx-legacy` for the build) → `reviewer` (Opus) on each branch → lead reviews the merged diff → Bruno tests in UTM.

## Stage 2: the Aqua pass (`osx-legacy`)

Decided 2026-10-31 (Bruno): on Mac OS X the window must look like an Aqua application, measured against **Mail 2.0 on a stock Tiger**. It keeps Gazette's own three-column layout, not Mail's. The reference is stock Aqua: Bruno's UTM Tiger wears a third-party theme (Uno), so his screenshots show the wrong chrome. **The Mac OS 9 build must not change by one pixel or one behaviour.** Every change below is either in an OS X-only file or inside `#ifdef GAZETTE_MACOSX`.

**Avoid:**
- Platinum bevels, grooves, grab handles and placards;
- bitmap Geneva, Charcoal and Monaco;
- focus rings round panes (Mail has none; an inactive selection turns grey instead);
- brushed metal (Mail 2.0 is not metal);
- the flat Leopard-and-later look hand-drawn onto Tiger;
- drawing by hand anything the system draws (toolbar, search field, scroll bars, list headers).

### A. Toolbar and search field (new file `src/ui/aqua_toolbar.c`, OS X only)

- **The toolbar is a real `HIToolbar`.** The window is created with `kWindowToolbarAttribute | kWindowUnifiedTitleAndToolbarAttribute`. Unified is 10.4+, and every `osx-legacy` system is 10.4+. On 10.5 and later the same attribute draws that system's own unified look, which is the native look there.
- **Items.** These are the same nine commands as the Mac OS 9 toolbar, in the same order (`kTB*` in `platinum_window.c`).
  - Groups are separated by `kHIToolbarSpaceIdentifier`, as Mail does, not by separators.
  - A flexible space comes next, then the search field at the right-hand end.
  - Each item sends its command through `gOnCommand`, exactly as `ToolbarButtonPressed` does now.
  - The toggling items swap label and image the way their buttons do now (`AdjustToolbarState` drives both).
  - New shows the New Feed… / New Group… menu.
- **Display.** The default is icon and label at normal (32×32) size. The toolbar is configurable and autosaves its configuration, so the user can pick icon only, text only or small size from the toolbar's contextual menu, as in Mail.
- **Images** are 32×32 PNGs in `Contents/Resources/toolbar/` (the C stage below), loaded with `CGImageCreateWithPNGDataProvider`. Until they exist, fall back to the existing 16×16 suites; a blurry placeholder is fine there.
- **Search field.** An `HISearchField` (10.3+) in a custom toolbar item:
  - **fixed width** (Bruno: it must not grow the way Mail's does), with its minimum equal to its maximum;
  - a magnifier and the cancel button, as the field draws them;
  - Return or the search action sends `kGazetteCmdSearch`;
  - the cancel button empties the field and sends `kGazetteCmdSearch` again, so the search clears;
  - `GazetteUISearchText` / `GazetteUISetSearchText` read and write the field (CFString to MacRoman).
  - The old Edit Text control, the drawn buttons, the hover tracking and the magnifier are not created on OS X.
- **Hide Toolbar** (View menu) calls `ShowHideWindowToolbar`. The toolbar button in the title bar (the pill) does the same and keeps `GazetteCoreHideToolbar` and the menu item in step.
- **The risk to settle first.** The window keeps the `WaitNextEvent` loop and has no `kWindowStandardHandlerAttribute`. Clicks in the toolbar arrive in the structure region (`FindWindow` → `inStructure` / `inToolbarButton`), and keystrokes for the search field need routing. Find out from Apple's 10.4 headers and documentation how a non-standard-handler window gets its toolbar items and a focused `HISearchField` to work, and do that: for instance a Carbon event handler on the window (or on the toolbar's views) for exactly the events the toolbar needs. Write the finding down in this file. Do **not** switch the window to the standard handler or to compositing: the panes are QuickDraw.
- **Finding (written before it could be run on a Mac; Bruno's UTM Tiger is the test).** Apple's HIToolbar guide says only that a window with a toolbar "should use the standard window event handler", and gives nothing for a `WaitNextEvent` loop; the 10.4u headers show what the standard handler would have done, and `aqua_toolbar.c` does exactly that much by hand, window-side and nothing more:
  - **Items.** Each item's view is the system's, and a click on it ends in `kEventToolbarItemPerformAction`, sent to the *item*. A handler is installed on every item's own target, and each item has the command ID `kHIToolbarCommandPressAction` (without a command ID Apple's docs call an item "inherently not clickable"). So no `kEventCommandProcess` is relied on, and there is no question of which target it reaches. Items are made by a delegate (the toolbar is its own delegate) so that the customise sheet and autosave work.
  - **Getting the click there.** `FindWindow` reports the toolbar as `inStructure`, `inDrag` or `inToolbarButton`. `HandleMouseDown` offers every mouse-down on the window to `GazetteAquaToolbarMouseDown` first. It takes only clicks in the band between the structure's top and the content's top, asks `HIViewGetViewForMouseEvent` (from the root view and from the topmost ancestor, since 10.4 does not say where a non-composited window keeps its toolbar) which view is under the mouse, and calls `HIViewClick` on it with the current mouse-down event (or one built from the `EventRecord`). If the answer is only the window's frame, or `HIViewClick` declines, it returns false and the shell drags the window as before. The first click on an inactive window only brings it forward.
  - **The pill.** `inToolbarButton` is tracked by hand (`StillDown`, then `FindWindow` at the release) and calls the toggle; Command-click cycles the display mode, as Apple's does. A handler for `kEventWindowToolbarSwitchMode` on the window does the same in case the system's own handler ever sees the click first; the two cannot both fire for one click.
  - **Keys.** A keystroke only reaches an `HIView` through the standard handler, which forwards it to the focused view. Without it the key comes out of `WaitNextEvent` as a `keyDown` and the field never sees it. But Text Services sends `kEventTextInputUnicodeForKeyEvent` to the user focus (the window) *before* that happens, so a handler on the window catches it: if the search field has the focus (`HIViewGetFocusPart`), the event is forwarded to the field's own target (guarded against the loop of an unhandled event returning to the window), which inserts the character. Only `kEventTextInputUnicodeForKeyEvent` is forwarded: the other Text Input events (an input method's marked text, for one) are not, so a composing input method is untested and may not reach the field. Return and Escape are taken there for the shell. Anything the field declines falls out as an ordinary `keyDown`, and `GazetteUIKey` swallows it while the field has the focus.
  - **Copies and gaps.** The customise sheet makes copies of every item, so a search field is only adopted while its owning window is the real one (`kEventControlOwningWindowChanged`, forgotten on `kEventControlDispose`). A click is taken only if something under the mouse has a part to hit (`HIViewGetPartHit`, tried in content and structure coordinates); spaces and gaps fall through to `DragWindow`. The field has the keyboard only when it is clicked itself; clicking a pane, or hiding the toolbar, takes it away. If the toolbar cannot be created, the drawn Platinum one is used and the failure goes to stderr.
  - **Not done:** the window is not given the standard handler or compositing; nothing in the panes changes.
  - **Deviation from the plan above:** `kWindowToolbarAttribute` does not exist. A window gets its toolbar from `SetWindowToolbar`, and the pill is `kWindowToolbarButtonAttribute`. The 10.4u SDK does not declare `kWindowUnifiedTitleAndToolbarAttribute` either, so `(1L << 7)` is used; if the system refuses it the window is created without it. Custom toolbar items have no minimum/maximum size API in 10.4, so the field's fixed width comes from its view answering `kEventControlGetSizeConstraints` with equal minimum and maximum.
- **Layout.** In `platinum_window.c`, the toolbar height is 0 on OS X: the toolbar lives in the window's structure and the content starts below it. After a show/hide, the content size changes and `Layout()` runs as on a resize.

### B. The panes (`platinum_window.c`, behind `GAZETTE_MACOSX`; helpers may go in `src/ui/aqua_draw.c`)

- **Text.**
  - At startup (`main.cpp`, OS X only), `QDSwapTextFlags(kQDUseCGTextRendering | kQDUseCGTextMetrics)`, so QuickDraw and TextEdit draw with Quartz anti-aliasing.
  - Fonts come from the theme: `kThemeSystemFont` (Lucida Grande 13), `kThemeSmallSystemFont` (11) and `kThemeViewsFont` (12), plus Helvetica for the article body, as Mail's default is. `MeasureFonts` keeps measuring rows from whatever these turn out to be.
- **Headers over the two lists** ("Feeds", "Google News – Top Stories (36 unread)"):
  - drawn with `DrawThemeButton(kThemeListHeaderButton)` across the header rect, which is the Aqua column header Mail's table has;
  - text in the small system font, black; the "(36 unread)" part in grey;
  - no `CreateWindowHeaderControl` on OS X.
- **Sidebar (source list).**
  - Background RGB(214,221,229).
  - The selected row, active window and focused pane: a vertical gradient RGB(92,147,213) → RGB(22,91,187), a 1-px top line RGB(69,128,200), white bold text.
  - The selected row, unfocused or inactive: RGB(138,162,190) → RGB(112,137,168), white bold text.
  - Row text in the system font at 12 (views font).
  - The unread count: a rounded pill at the right, fill RGB(152,168,202) with white bold small text (on a selected row, a white pill with text in the selection colour). A zero count shows nothing, as in Mail.
  - The 16×16 icons and disclosure triangles stay. The disclosure triangle is the theme's (`DrawThemeButton(kThemeDisclosureButton)`).
- **Headline list.**
  - White background, with every other article (both of its lines) striped RGB(237,243,254), as Mail's table is.
  - Selection: RGB(56,117,215) with white text when the pane is focused and the window active; RGB(212,212,212) with black text otherwise.
  - Headlines in the views font (Lucida Grande 12), unread in bold, as now.
  - Date headings ("Today"): small system font bold, RGB(110,110,110), on white, with a 1-px RGB(220,220,220) rule under them. They take no stripe.
- **Focus.** No focus border is drawn round any pane on OS X. The focused pane is shown by its selection colour alone. Tab and the arrow keys behave as now.
- **Dividers.**
  - Each divider is a single 1-px line, RGB(165,165,165), with the panes' own backgrounds either side.
  - No groove and no grab handle.
  - The mouse may still grab the divider a few pixels either side of the line, so it is as easy to drag as now.
- **Reader.**
  - Title: Lucida Grande 14 bold. Byline: small system font, RGB(128,128,128). Body: Helvetica 12.
  - The rule under the byline is 1 px, RGB(200,200,200).
  - Selection colours come from the theme (`LMGetHiliteRGB` holds the user's Aqua highlight).
  - Photos are unchanged.
- **Status line.** The strip along the bottom is drawn with `DrawThemeButton(kThemeListHeaderButton, kThemeStateInactive)`, a light Aqua gradient bar as at the foot of Mail's window. Its text is in the small system font, RGB(64,64,64), with no black rule and no grooves.
- **Scroll bars.** Already Aqua, because they are real controls. The grow box is the window's own.

### C. Toolbar icons (new art, needs Bruno's OK before commit)

- There are twelve: New, Hide/Show Sidebar, Refresh, Mark All Read/Unread, Hide/Show Read, Mark Read/Unread, Star, Next Unread, Open in Browser.
- **Style.** Aqua-era toolbar art at 32×32, PNG with alpha:
  - lit from above and front, a 3/4 view where an object has depth, soft drop shadow, saturated but not neon;
  - the family resemblance of Tiger's Mail and Finder toolbars;
  - no flat glyphs and no outlines-only.
- **How they are made.** They are drawn by code, `tools/generate_aqua_toolbar_icons.py` (pure Python, no network), which also writes a proof sheet. They are committed to `Resources/osx/toolbar/` only after Bruno approves the sheet.
- The workflow copies them into `Contents/Resources/toolbar/`.

### Done for stage 2

1. `build.yml` (Mac OS 9), `osx-legacy.yml` and `net-posix.yml` are green. The Mac OS 9 shell is unchanged: every hunk in `src/` is in an OS X-only file or inside `#ifdef GAZETTE_MACOSX`, and the reviewer checks this.
2. A, B and C as above, in the `Gazette-mac-os-x` artifact.
3. Bruno opens it beside Mail on 10.4 (theme discounted) and it reads as the same family.

Branches:
- `osx-aqua` is the integration branch, off `osx-legacy`;
- `osx-aqua-toolbar` (A), `osx-aqua-panes` (B) and `osx-aqua-icons` (C) are the porters' branches.

## Later: `macos-universal`

Waits for the native-TLS seam (TASKS.md N1/N2), which `win-modern` builds first with WinHTTP. Then: an AppKit shell in `src/cocoa/` reusing `src/app/` the way `src/win/` does, and a `URLSession` `GazetteFetch` backend.
