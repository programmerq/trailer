#include "QuitMenu.h"

#include <QAction>
#include <QPointer>
#include <QString>

#import <AppKit/AppKit.h>

// macOS native "Quit and Keep Windows" affordances. Guarded by the CMake
// wiring (this .mm is compiled only on APPLE; QuitMenu_stub.cpp is used
// elsewhere), mirroring src/platform/Share.mm. All Cocoa is confined here.

// Objective-C trampoline that forwards the native alternate menu item's
// action to the bound cross-platform QAction, so a menu click and the
// ⌥⌘Q accelerator both drive Application::requestQuit. Declared at file
// scope (outside the C++ namespace) so it is a proper Objective-C class.
@interface TrailerKeepTrampoline : NSObject {
@public
    // QPointer (not a raw QAction*) so the ivar auto-nulls if the owning
    // window/QAction is destroyed before the trampoline. A menu item can
    // outlive its bound QAction (secondary windows, teardown ordering), and
    // a raw pointer would then dangle — trailerTriggerKeep: would call into
    // freed memory. QPointer closes that use-after-free. Legal here because
    // this is ObjC++ (.mm): the ivar is a C++ object, default-constructed.
    QPointer<QAction> action;
}
- (void)trailerTriggerKeep:(id)sender;
@end

@implementation TrailerKeepTrampoline
- (void)trailerTriggerKeep:(id)sender {
    Q_UNUSED(sender);
    if (action)
        action->trigger();
}
@end

namespace trailer {

namespace {

// Locate the "Quit Trailer" NSMenuItem in the application (first) menu.
// Qt drives QAction(QuitRole) onto that item, but exposes no handle to it,
// so we walk the native NSApp main menu.
//
// Primary match: AppKit's own terminate: selector. Defensive fallbacks (a
// selector mismatch would otherwise make the whole alternate-item swap a
// silent no-op — dead ⌥⌘Q with no error): also accept a menu item whose
// title contains "Quit", and as a last resort the application (first) menu's
// last enabled item, which is where AppKit conventionally places Quit.
//
// MANUAL-VERIFY (owner, real Mac): confirm Qt's QuitRole item on the
// installed Qt version actually carries @selector(terminate:). If Qt ever
// routes Quit through a different selector/target, the primary match stops
// firing and we rely on the title/position fallback below — verify the swap
// still lands on the real Quit row. See the decision record's manual gate.
NSMenuItem *findQuitItem() {
    NSMenu *mainMenu = [NSApp mainMenu];
    if (!mainMenu)
        return nil;

    // Pass 1: exact selector match (most robust when it holds).
    for (NSMenuItem *top in [mainMenu itemArray]) {
        NSMenu *submenu = [top submenu];
        if (!submenu)
            continue;
        for (NSMenuItem *item in [submenu itemArray]) {
            if ([item action] == @selector(terminate:))
                return item;
        }
    }

    // Pass 2: title-contains-"Quit" fallback (any submenu). Guards against a
    // Qt version whose QuitRole item uses a non-terminate: selector.
    for (NSMenuItem *top in [mainMenu itemArray]) {
        NSMenu *submenu = [top submenu];
        if (!submenu)
            continue;
        for (NSMenuItem *item in [submenu itemArray]) {
            NSString *title = [item title];
            if (title && [title rangeOfString:@"Quit"].location != NSNotFound)
                return item;
        }
    }

    // Pass 3: positional fallback — the application (first) menu's last
    // enabled, non-separator item, AppKit's conventional slot for Quit.
    NSArray<NSMenuItem *> *top = [mainMenu itemArray];
    if ([top count] > 0) {
        NSMenu *appMenu = [[top objectAtIndex:0] submenu];
        for (NSInteger i = (NSInteger)[[appMenu itemArray] count] - 1; i >= 0; --i) {
            NSMenuItem *item = [[appMenu itemArray] objectAtIndex:(NSUInteger)i];
            if (![item isSeparatorItem] && [item isEnabled])
                return item;
        }
    }
    return nil;
}

// Sentinel tag so a second menu build doesn't install a duplicate.
constexpr NSInteger kKeepItemTag = 0x51574B57; // 'QWKW'

} // namespace

void QuitMenu::installAlternateKeepItem(QAction *quitAction, QAction *keepAction) {
    Q_UNUSED(quitAction);
    if (!keepAction)
        return;

    NSMenuItem *quitItem = findQuitItem();
    if (!quitItem)
        return;
    NSMenu *menu = [quitItem menu];
    if (!menu)
        return;

    const NSInteger quitIndex = [menu indexOfItem:quitItem];
    if (quitIndex < 0)
        return;

    for (NSMenuItem *existing in [menu itemArray]) {
        if ([existing tag] == kKeepItemTag)
            return; // already installed
    }

    const QString title = keepAction->text().isEmpty()
                              ? QStringLiteral("Quit and Keep Windows")
                              : keepAction->text();
    NSString *nsTitle = [NSString stringWithUTF8String:title.toUtf8().constData()];

    NSMenuItem *keepItem = [[NSMenuItem alloc] initWithTitle:nsTitle
                                                      action:@selector(trailerTriggerKeep:)
                                               keyEquivalent:@"q"];
    // alternate=YES + the same key-equivalent letter as the Quit item
    // directly above makes AppKit hide this row and swap it into the Quit
    // row's place while Option is held — the standard in-place swap.
    [keepItem setKeyEquivalentModifierMask:(NSEventModifierFlagCommand |
                                            NSEventModifierFlagOption)];
    [keepItem setAlternate:YES];
    [keepItem setTag:kKeepItemTag];

    // Retain the trampoline for the process lifetime (the menu item does
    // not retain its target under ARC). installAlternateKeepItem runs once
    // per NSMenu — MainWindow::buildMenus AND installNoWindowMenuBar each
    // build a DISTINCT menu — so a single `static` slot is WRONG: the second
    // call would overwrite it, ARC would release the first trampoline while
    // its menu item still (unsafely) referenced it, and ⌥⌘Q on the earlier
    // menu would go dead (or crash). Retain ONE trampoline PER menu item by
    // parking every trampoline in a process-lifetime array so none is ever
    // deallocated while its item is alive.
    static NSMutableArray *keepTrampolines = nil;
    if (!keepTrampolines)
        keepTrampolines = [[NSMutableArray alloc] init];
    TrailerKeepTrampoline *trampoline = [[TrailerKeepTrampoline alloc] init];
    trampoline->action = keepAction;
    [keepTrampolines addObject:trampoline];
    [keepItem setTarget:trampoline];

    [menu insertItem:keepItem atIndex:(quitIndex + 1)];
}

bool QuitMenu::osQuitAlwaysKeepsWindows() {
    return [[NSUserDefaults standardUserDefaults] boolForKey:@"NSQuitAlwaysKeepsWindows"];
}

} // namespace trailer
