// Native menu bar of the window: File (open / eject / save disk, reset, exit) and View
// (keyboard strip, options). Windows: a Win32 menu on the SDL window; macOS: items in the
// application menu bar; elsewhere nothing (the keyboard shortcuts still work).
#pragma once
#include <SDL.h>
#include <string>

class NativeMenu {
public:
    enum Cmd { NONE = 0, OPEN_DISK, EJECT_DISK, SAVE_DISK, RESET, EXIT, TOGGLE_KEYBOARD, OPTIONS, RECORD_WAV };
    bool init(SDL_Window *w, bool keyboard_shown);   // false if the platform has no native menu
    Cmd command(const SDL_Event &e) const;            // the menu command an event carries, or NONE
    void set_keyboard(bool shown);                   // check mark of View > Show Keyboard
    // keep the window's client area at w:h while the user resizes it (Windows: WM_SIZING,
    // macOS: NSWindow contentAspectRatio; elsewhere the program corrects the size afterwards)
    void set_aspect(int w, int h);
    // file dialog for a disk image (Windows/macOS); "" if cancelled or not available
    std::string open_disk_dialog();
    // folder picker (Windows/macOS; zenity or kdialog on Linux); "" if cancelled or not available
    std::string choose_folder_dialog(const std::string &title);
    bool active() const { return ok; }
private:
    SDL_Window *win = nullptr;
    bool ok = false;
    void *menu = nullptr;     // HMENU of View
};
