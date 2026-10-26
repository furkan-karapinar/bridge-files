# File Explorer (Dolphin fork)

Upstream: Dolphin 26.08.1, `https://download.kde.org/stable/release-service/26.08.1/src/dolphin-26.08.1.tar.xz`
(sha256 `f5635feff84e7070d35342d6defb46e9b42ff1c44e3896635f4bd9f43da947c1`). Licence GPL-2.0-or-later, as upstream.

Goal: the Windows 11 File Explorer, built on Dolphin's KIO, views and file operations.
Build and install: `bash tools/build-files.sh` on the VM (own prefix `~/.local/opt/shellkit-files`).

To merge a newer Dolphin: unpack the new tarball next to this folder, diff it against the previous
release and apply that diff here; the fork's own changes are listed below.

## Changes against upstream

### E1: own identity beside the system Dolphin

- Removed `po/`, `doc/`, `appiumtests/` and their CMake lines. Translations come from the system
  Dolphin's `dolphin` catalogue (`TRANSLATION_DOMAIN` is unchanged); strings new in the fork use
  their own catalogue (E2).
- Executable `shellkit-files` (`OUTPUT_NAME` in `src/CMakeLists.txt`); target names unchanged.
- `KAboutData` component `shellkit-files`, display name "File Explorer", organisation domain
  `shellkit.org`, desktop file `org.shellkit.files` (`src/main.cpp`).
- Config `shellkit-filesrc` instead of `dolphinrc` (all `*.kcfg`, and the cpp files that open it by name).
- KXmlGui component and resource prefix `shellkit-files` (`dolphinmainwindow.cpp`, `dolphin.qrc`),
  session config `shellkit-files`.
- D-Bus instance names `org.shellkit.shellkit-files-<pid>` (`dbusinterface.cpp`, `main.cpp`, `global.cpp`).
- Desktop name of jobs `org.shellkit.files` (`dolphintabwidget.cpp`).

### E2: Windows 11 window layout

- Widget style: `main.cpp` sets the `shellkit-win11` style (`plasma/qtstyle`) when it is installed.
- Own catalogue `shellkit-files` for the fork's strings: `src/shellkit.h` (`shellkitDomain`, used with
  `i18ndc`/`i18ndcp`), `po/tr/shellkit-files.po`, `ki18n_install(po)` in `CMakeLists.txt`, locale dir
  added in `main.cpp`. Upstream strings keep their exact context so the `dolphin` catalogue translates them.
- `dolphinui.rc` (version 50): `mainToolBar` is the address row (back, forward, up, refresh,
  breadcrumb, search box); new `commandBar` (New | cut, copy, paste, rename, delete | Sort, View |
  See more, spacer, Details). `setupGUI` names `dolphinui.rc` explicitly: the default file would be
  `shellkit-filesui.rc`.
- `dolphinmainwindow.*`: `setupShellkitActions()` (button texts and icons, spacer, search box with a
  500 ms delay into the search bar, toolbars locked), `updateSearchBox()`; navigation pane dock (F9)
  in place of Places, which is hidden on first run and lost its shortcut; `find()` focuses the search box.
- `panels/navigation/navigationpanel.*` (new): the navigation tree (Home, Gallery, pinned folders,
  This PC with drives, Network), lazy subfolders, rounded hover/selection rows.
- `search/bar.*`, `dolphinviewcontainer.*`: `search()` / `startSearch()` for the toolbar search box.
- `dolphinnavigatorswidgetaction.cpp`: no spacing that lines the breadcrumb up with the view; the
  container paints the text box behind the breadcrumb (KUrlNavigator gets no paint events there).
- `statusbar/dolphinstatusbar.*`: Details/Large icons buttons on the right, space info hidden;
  `dolphin_generalsettings.kcfg` shows the status bar full width by default.
- `views/dolphinview.cpp`: status text "N items", "N items selected", selection size; no
  recursive size job when nothing is selected.

### E3: views

- Defaults in the kcfg files: folders open in the details view; details rows without expandable
  folders, 12 px side padding, 16 px icons and previews; icons view 96 px ("Large icons"), narrow
  tiles, two text lines; no folder sizes, plain dates, names elided at the end.
- `views/viewproperties.cpp`: default details columns Name, Date modified, Type, Size.
- `views/dolphinitemlistview.cpp`: details rows 30 px high at 16 px icons.
- `kitemviews/kstandarditemlistwidget.cpp`: text keeps its colour when pressed (the selection is a
  light fill); icons under 32 px are scaled down from a 64 px pixmap (Fluent's small sizes are monochrome).
- `kitemviews/kfileitemmodel.cpp`, `kfileitemmodelrolesupdater.cpp`: Type of folders is "File folder"
  (`shellkitTypeName()` in `src/shellkit.h`).
- The selection, hover and column header look comes from `plasma/qtstyle` (`PE_PanelItemViewItem`, `CE_Header`).

### E4: Home page

- `src/shellkit.h`: `shellkitHomeUrl()` (`shellkit-home:/?title=Home`; the title query is what
  KUrlNavigator and `caption()` show) and `isShellkitHomeUrl()`.
- `homepage/homepage.*` (new): Quick access tiles (pinned folders), Favorites (config group
  "Home", key "Favorites"; filled by E5), Recent (`recently-used.xbel`, 20 newest existing files),
  collapsible sections, rows with full-width hover and selection.
- `dolphinviewcontainer.*`: `setHomePageShown()`; the Home URL shows the page in place of the view
  (the view waits in the home folder), `url()` returns the Home URL, the hidden view's status text
  is ignored, toolbar navigators start from `url()`; opened local files go to `KRecentDocument`.
- `dolphinmainwindow.cpp` `changeUrl()` accepts the Home URL; `main.cpp`, new tabs and
  `goHomeInNewTab()` open the Home page; the navigation tree's Home item points to it.

### E5: tabs, context menu, copy dialog

- `dolphintabwidget.*`: `createTabRow()` moves the tab bar into a row widget (`TabRow`) with a "+"
  button after the last tab; event filters undo QTabWidget's own bar placement and keep the page
  stack over the whole widget. Tabs are always shown and closable; tab icons are the folder's
  colour icon (`tabIcon()`, Home → "user-home").
- `dolphintabbar.*`: document-mode tabs 180-240 px wide (96 px minimum), no expanding, no scroll
  buttons.
- `dolphinmainwindow.*`: `setupShellkitTabRow()` puts the row in its own toolbar above the address
  row; `paintEvent()` paints the Base layer from the tab row down to the command bar so the
  selected tab joins it; the tab bar is never hidden.
- `qtstyle`: Win11 document-mode tabs (rounded top with curved feet for the selected one, hover
  fill, 16 px dividers, thin close ×, 40 px row); dialog tabs keep Fusion.
- `shellkitcontextmenu.*` (new): short menu. Items: icon row (cut, copy, rename, delete), Open, new
  tab/window or Open with, Pin to/Unpin from Quick access (places model), Add to/Remove from
  Favorites, Compress to ZIP (Ark), Copy as path, Properties. Viewport: View, Sort, Paste, Undo,
  Terminal, New, Properties. "Show more options" or Shift+right click opens `DolphinContextMenu`;
  trash, search and recent URLs always get the full menu.
- `jobs/copyprogressdialog.*` (new, in dolphinprivate): "%N complete" window with pause, cancel and
  details (name, speed, time and items left), shown after 600 ms. Paste (`DolphinView::pasteToUrl`)
  and drop (`DragAndDropHelper`) run with `KIO::HideProgressInfo` and feed it.
- `homepage/homepage.*`: context menu (Open, new tab/window or Open file location, Unpin, Add to/
  Remove from Favorites, Remove from Recent, Copy as path, Properties); Favorites read through
  `shellkitFavorites()` (`shellkit.h`). `dolphinviewcontainer.cpp` opens a file location with the
  file selected.
- Copy as path (`copy_location`): Ctrl+Shift+C, every selected path in quotes, one per line.

### Polish after E5

- `dolphinmainwindow.*`: `placeShellkitTabRow()` puts the tab row back on its own first line after
  a saved window state is restored (`readProperties()`); `setupShellkitToolTips()` gives the
  address row and command bar "Copy (Ctrl+C)" tooltips (no What's This, so no "press Shift" line).
- `settings/dolphin_generalsettings.kcfg`: `RememberOpenedTabs` defaults to false (every window
  starts fresh, no tabs of deleted folders).
- `main.cpp`: `ShortcutKeyNames` translator shows Ctrl/Alt/Shift/Enter instead of Qt's Turkish
  key names; writes `[KDE] DndBehavior=MoveIfSameDevice` to the fork's config when no value is set
  (cascades from kdeglobals, so a System Settings choice wins).
- `views/draganddrophelper.cpp`: a plain drop onto another device copies without the drop menu.
- `shellkit.h`: `shellkitTypeName()` "Text Document", "Compressed (zipped) Folder", "<EXT> File" for
  pictures, sound, video and unknown files; `shellkitFileSize()` whole KB in the Size column;
  `shellkitStatusSize()` "8 bytes" / "2.38 MB" in the status bar.
- `kitemviews/kstandarditemlistview.cpp`: no striped rows. `dolphinviewcontainer.cpp`: hovering
  keeps the item count in the status bar.
- `jobs/copyprogressdialog.cpp`: cancel button is a thin cross in the text colour.
- `qtstyle`: tab bars get `WA_Hover`, so tab hover is painted.

### Remaining E2-E5 gaps

- `dolphinviewcontainer.*`: the Search::Bar lives in a hidden holder widget; the search box in the
  address row drives it, so no extra row appears. On the Home page the search path is the home
  folder (`searchableUrl()`), and clearing the search returns to Home (`m_searchFromHome`).
  `takeStatusBar()` hands the container's status bar to the main window.
- `dolphinmainwindow.*`: `showShellkitStatusBar()` puts the active container's bar into
  QMainWindow's status bar, which spans the navigation pane too; it follows the "Show Status Bar"
  setting. "See more" is drawn as three horizontal dots (`moreIcon()`), redrawn on palette change.
- `jobs/copyprogressdialog.*`: `trackDeletion()` takes the trash or delete subjob of a
  `KIO::DeleteOrTrashJob` off the Plasma job tracker and shows it in the same dialog ("Deleting 8,000
  items from sktest2"); used by `DolphinView` and the folders panel. The class is exported from
  dolphinprivate.

### Closer to Windows 11 (layers, command bar, details pane)

- `dolphinmainwindow.*`: `applyShellkitLayers()` gives the body (navigation pane, files, details
  pane) Windows' body colour (#191919 dark, white light) while the tab row and the address row keep
  the lighter layer; `paintEvent` fills that layer only down to the command bar and draws hairlines
  above and below the command bar. Re-applied on application palette change.
- `views/dolphinview.cpp`: "This folder is empty." in normal size at the top centre, below the
  column header; `updatePalette()` takes the palette Base instead of the KColorScheme View colour.
- `panels/navigation/navigationpanel.cpp`: 32 px rows, colour icons scaled down from 64 px, orange
  Home glyph, a pin on pinned folders, Gallery never wins the current-folder highlight.
- `panels/information/informationpanelcontent.*`: a full-width preview band (220 px, 96 px icons),
  left-aligned larger name, and for the folder itself (`showFolder()`, called by the panel when
  nothing is selected) "Name (N items)" with a hint card instead of metadata; also shown for a
  multi-selection. The name label lost its style sheet, which froze its text colour on theme change.
- Qt style (`plasma/qtstyle`): command bar buttons 40 px wide with 8 px gaps and 8 px padding,
  32 px separators, no "on" box for checked buttons in toolbars, vertical hairline dock handles.
- `tools/install-icons.sh`: Fluent's plain `folder`/`folder-open` point at its yellow folders.

### Tabs in the title bar

- With the shellkit window frame (`kwinrc` decoration library `org.shellkit.win11`,
  `shellkitIntegratedCaption()`), the window title ends in U+2060 (`shellkitCaptionMarker`); the
  frame then drops its title bar for that window only and keeps corners, edge, shadow and resize
  margins. Dialogs of the app keep their normal title bar.
- `dolphintabwidget.cpp`: `TabRow` draws Windows caption buttons (46 px, same glyphs as the frame,
  red close) at the right; dragging free space calls `QWindow::startSystemMove()` once the pointer
  travels, a double click maximizes or restores.
- `dolphinmainwindow.cpp`: the tab row toolbar has no layout margins, so the row is 40 px like
  Windows' tab strip and the buttons reach the corner.
- A rebuilt frame plugin only loads in a new KWin: sign out and in, or (tested) kill `kwin_wayland`,
  which the wrapper restarts while Qt apps reconnect (`QT_WAYLAND_RECONNECT=1`).

### Copy dialog: Windows 11 and classic

- `jobs/copyprogressdialog.*`: the Windows 11 layout (default): header with the folders as accent
  links, "45% complete" with pause and cancel glyphs on the same row, a speed graph (`SpeedGraph`:
  speed over progress, copied part tinted, a line with "Speed: 470 MB/s") or a thin bar when the
  details are hidden, "Name:", "Time remaining:", "Items remaining:" as plain lines, and a footer
  band with a chevron "More/Fewer details". Sizes in Windows units (`shellkitStatusSize`).
- `GeneralSettings::classicCopyDialog` (Settings > Interface > Folders & Tabs > Window, "Show copy
  progress in the classic layout"): the Windows 7 layout, a banner "Copying 3 items (2.04 GB)", a
  table (Name, From, To, Time remaining, Items remaining, Speed), a progress bar, and the details
  toggle with a Cancel button in the footer; the title names the time left.
- Whether the details are open is remembered (`[CopyDialog] Details`, default open).
- The job deletes itself when it finishes, so everything the header needs is read in the constructor.

### Windows 7 look (optional)

- Setting `Win7Look` (`dolphin_generalsettings.kcfg`, default false), a "Look:" combo on Interface >
  Folders & Tabs; it takes effect on the next start. `shellkitWin7()` (`shellkit.h`) reads
  `shellkit-filesrc` by name because the style is chosen before `KAboutData`; `shellkitStartUrl()`
  opens the home folder in Windows 7.
- `main.cpp` sets the `shellkit-win7` style (`plasma/qtstyle/src/win7style.*`, derived from the
  Windows 11 style): gradient tool buttons, grey push buttons, Windows 7 selection boxes, white header.
- `dolphinui7.rc`: address row (back, forward, address, refresh, search) and a command bar
  (Organize menu, New folder, view mode, preview pane). `dolphinmainwindow.cpp`:
  `setupShellkitWin7Actions()`, `setupShellkitWin7Bars()`, drawn round back/forward icons
  (`win7NavIcon`), the glass and command band in `paintEvent`.
- `win7detailspane.*`: the details pane below the files, in the window status bar above the
  container status bar (`showShellkitStatusBar()`).
- `dolphintabwidget.cpp`: Windows 7 caption buttons; the tab row hides its tabs with one tab.
- `navigationpanel.cpp`: `rebuildWin7()` groups Favorites, Libraries, Computer and Network;
  `changeEvent()` rebuilds the tree on a palette change (both looks), so tinted icons follow the scheme.
- Win7 menus come from the style (square grey panel, icon column line, blue hover box).
- `dolphinitemlistview.cpp`: 22 px details rows; `copyprogressdialog.cpp`: always the classic dialog.

### This PC and Network pages

- `shellkit.h`: `shellkitThisPcUrl()` (`shellkit-thispc:/`, titled "Computer" in the Windows 7 look),
  `shellkitNetworkUrl()` (`shellkit-network:/`) and `isShellkitPageUrl()`, which replaces
  `isShellkitHomeUrl()` wherever any page is meant (`changeUrl`, search, the view's status text).
- `pages/shellkitpage.h`: the base of `HomePage`, `ThisPcPage` and `NetworkPage` (`refresh()`,
  `itemCount()`, `folderActivated`, `folderInNewTab`, `itemCountChanged`).
  `DolphinViewContainer::setPageShown(url)` replaces `setHomePageShown(bool)` and makes each page
  the first time it is shown.
- `pages/tilesection.*`: a folding section of tiles (icon, name, optional capacity bar, grey lines).
- `pages/thispcpage.*`: "Devices and drives": the root file system first, then the places
  model's drives with free space (`QStorageInfo`); opening a drive that is not mounted mounts it
  first (`requestSetup`); the menu offers Unmount/Eject from the places model.
  `ThisPcPage::driveName()` ("Label (mount point)", "Local Disk (mount point)") also names the
  drives in the navigation pane, which always shows "Local Disk (/)" first and opens This PC on
  the first build.
- `pages/networkpage.*`: adapters (`QNetworkInterface`), the default gateway (`/proc/net/route`),
  computers, media devices, printers and other devices from `avahi-browse --all --resolve
  --parsable --terminate` and the neighbour table (`/proc/net/arp`). A computer opens `smb:/` or
  `sftp:/`, a device its web page in the browser, an adapter Plasma's network settings. Needs
  `Qt6::Network` (top `CMakeLists.txt`, `dolphinstatic`).
- `dolphintabwidget.cpp`: tab icons of the pages.
