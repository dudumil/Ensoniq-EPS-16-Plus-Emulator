// macOS menu bar items for the emulator (see native_menu.h)
#import <Cocoa/Cocoa.h>
#include <SDL.h>
#include <string>

static Uint32 g_event = 0;
static NSMenuItem *g_kb_item = nil;

@interface EmuMenuTarget : NSObject
- (void)openDisk:(id)sender;
- (void)ejectDisk:(id)sender;
- (void)saveDisk:(id)sender;
- (void)resetMachine:(id)sender;
- (void)toggleKeyboard:(id)sender;
- (void)showOptions:(id)sender;
@end

static void push_cmd(int c) {
    SDL_Event e; SDL_zero(e);
    e.type = g_event; e.user.code = c;
    SDL_PushEvent(&e);
}

@implementation EmuMenuTarget
- (void)openDisk:(id)sender { push_cmd(1); }
- (void)ejectDisk:(id)sender { push_cmd(2); }
- (void)saveDisk:(id)sender { push_cmd(3); }
- (void)resetMachine:(id)sender { push_cmd(4); }
- (void)toggleKeyboard:(id)sender { push_cmd(6); }
- (void)showOptions:(id)sender { push_cmd(7); }
@end

static EmuMenuTarget *g_target = nil;

static NSMenuItem *add_item(NSMenu *m, NSString *title, SEL sel, NSString *key) {
    NSMenuItem *it = [[NSMenuItem alloc] initWithTitle:title action:sel keyEquivalent:key];
    [it setTarget:g_target];
    [m addItem:it];
    return it;
}

bool mac_menu_init(Uint32 event_type, bool kb) {
    @autoreleasepool {
        g_event = event_type;
        NSMenu *bar = [NSApp mainMenu];
        if (!bar) return false;
        g_target = [[EmuMenuTarget alloc] init];
        // insert before the "Window" menu SDL creates (or at the end)
        NSInteger at = [bar numberOfItems];
        for (NSInteger i = 0; i < [bar numberOfItems]; i++)
            if ([[[bar itemAtIndex:i] title] isEqualToString:@"Window"]) { at = i; break; }

        NSMenu *file = [[NSMenu alloc] initWithTitle:@"File"];
        add_item(file, @"Open Disk Image…", @selector(openDisk:), @"o");
        add_item(file, @"Eject Disk", @selector(ejectDisk:), @"e");
        add_item(file, @"Save Disk Changes", @selector(saveDisk:), @"s");
        [file addItem:[NSMenuItem separatorItem]];
        add_item(file, @"Reset", @selector(resetMachine:), @"r");
        NSMenuItem *fileItem = [[NSMenuItem alloc] initWithTitle:@"File" action:nil keyEquivalent:@""];
        [fileItem setSubmenu:file];
        [bar insertItem:fileItem atIndex:at++];

        NSMenu *view = [[NSMenu alloc] initWithTitle:@"View"];
        g_kb_item = add_item(view, @"Show Keyboard", @selector(toggleKeyboard:), @"k");
        [g_kb_item setState:kb ? NSControlStateValueOn : NSControlStateValueOff];
        [view addItem:[NSMenuItem separatorItem]];
        add_item(view, @"Options…", @selector(showOptions:), @",");
        NSMenuItem *viewItem = [[NSMenuItem alloc] initWithTitle:@"View" action:nil keyEquivalent:@""];
        [viewItem setSubmenu:view];
        [bar insertItem:viewItem atIndex:at];
        return true;
    }
}

void mac_menu_set_keyboard(bool shown) {
    if (g_kb_item) [g_kb_item setState:shown ? NSControlStateValueOn : NSControlStateValueOff];
}

std::string mac_open_disk_dialog() {
    @autoreleasepool {
        NSOpenPanel *p = [NSOpenPanel openPanel];
        [p setCanChooseFiles:YES];
        [p setCanChooseDirectories:NO];
        [p setAllowsMultipleSelection:NO];
        [p setTitle:@"Open disk image"];
        if ([p runModal] != NSModalResponseOK) return "";
        NSURL *u = [[p URLs] firstObject];
        return u ? std::string([[u path] UTF8String]) : std::string();
    }
}

std::string mac_choose_folder_dialog(const std::string &title) {
    @autoreleasepool {
        NSOpenPanel *p = [NSOpenPanel openPanel];
        [p setCanChooseFiles:NO];
        [p setCanChooseDirectories:YES];
        [p setCanCreateDirectories:YES];
        [p setAllowsMultipleSelection:NO];
        [p setTitle:[NSString stringWithUTF8String:title.c_str()]];
        if ([p runModal] != NSModalResponseOK) return "";
        NSURL *u = [[p URLs] firstObject];
        return u ? std::string([[u path] UTF8String]) : std::string();
    }
}
