// Native menu bar of the window: File (open / eject / save disk, reset, exit) and View
// (keyboard strip, options). Windows: a Win32 menu on the SDL window; macOS: items in the
// application menu bar; elsewhere nothing (the keyboard shortcuts still work).
#pragma once
#include <SDL.h>
#include <string>

class NativeMenu {
public:
    enum Cmd { NONE = 0, OPEN_DISK, EJECT_DISK, SAVE_DISK, RESET, EXIT, TOGGLE_KEYBOARD, OPTIONS };
    bool init(SDL_Window *w, bool keyboard_shown);   // false if the platform has no native menu
    Cmd command(const SDL_Event &e) const;            // the menu command an event carries, or NONE
    void set_keyboard(bool shown);                   // check mark of View > Show Keyboard
    // file dialog for a disk image (Windows/macOS); "" if cancelled or not available
    std::string open_disk_dialog();
    bool active() const { return ok; }
private:
    SDL_Window *win = nullptr;
    bool ok = false;
    void *menu = nullptr;     // HMENU of View
};
