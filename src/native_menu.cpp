#include "native_menu.h"

#if defined(_WIN32)
#include <SDL_syswm.h>
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <vector>

namespace {
HWND hwnd_of(SDL_Window *w) {
    SDL_SysWMinfo wi; SDL_VERSION(&wi.version);
    return SDL_GetWindowWMInfo(w, &wi) ? wi.info.win.window : nullptr;
}
// Aspect ratio while resizing: SDL's window procedure is wrapped so that WM_SIZING (sent, not
// posted, so SDL's message hook does not see it) can correct the rectangle being dragged.
float g_aspect = 0;                     // client width / height (0 = free)
WNDPROC g_prev_proc = nullptr;
LRESULT CALLBACK aspect_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_SIZING && g_aspect > 0 && lp) {
        RECT *r = (RECT *)lp, wr, cr;
        GetWindowRect(h, &wr); GetClientRect(h, &cr);
        int ncw = (wr.right - wr.left) - (cr.right - cr.left);      // borders, title and menu bar
        int nch = (wr.bottom - wr.top) - (cr.bottom - cr.top);
        int cw = (r->right - r->left) - ncw, ch = (r->bottom - r->top) - nch;
        if (wp == WMSZ_TOP || wp == WMSZ_BOTTOM) {                  // height drives the width
            cw = (int)(ch * g_aspect + 0.5f);
            r->right = r->left + cw + ncw;
        } else {                                                    // width drives the height
            ch = (int)(cw / g_aspect + 0.5f);
            if (wp == WMSZ_TOPLEFT || wp == WMSZ_TOPRIGHT) r->top = r->bottom - ch - nch;
            else r->bottom = r->top + ch + nch;
        }
        CallWindowProcW(g_prev_proc, h, msg, wp, lp);
        return TRUE;
    }
    return CallWindowProcW(g_prev_proc, h, msg, wp, lp);
}
}

void NativeMenu::set_aspect(int w, int h) {
    g_aspect = (w > 0 && h > 0) ? (float)w / h : 0;
    HWND hw = hwnd_of(win);
    if (hw && !g_prev_proc) g_prev_proc = (WNDPROC)SetWindowLongPtrW(hw, GWLP_WNDPROC, (LONG_PTR)aspect_proc);
}

bool NativeMenu::init(SDL_Window *w, bool kb) {
    win = w;
    HWND h = hwnd_of(w);
    if (!h) return false;
    HMENU bar = CreateMenu(), file = CreatePopupMenu(), view = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, OPEN_DISK, L"&Open Disk Image...\tdrag && drop");
    AppendMenuW(file, MF_STRING, EJECT_DISK, L"&Eject Disk\tCtrl+E");
    AppendMenuW(file, MF_STRING, SAVE_DISK, L"&Save Disk Changes\tCtrl+S");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, RESET, L"&Reset\tCtrl+R");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, EXIT, L"E&xit\tAlt+F4");
    AppendMenuW(view, MF_STRING | (kb ? MF_CHECKED : 0), TOGGLE_KEYBOARD, L"Show &Keyboard\tF9");
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_STRING, OPTIONS, L"&Options...\tCtrl+O");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"&File");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)view, L"&View");
    int cw, ch; SDL_GetWindowSize(w, &cw, &ch);
    SetMenu(h, bar);
    SDL_SetWindowSize(w, cw, ch);          // keep the client area (SDL accounts for the menu bar)
    SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
    menu = view; ok = true;
    return true;
}

NativeMenu::Cmd NativeMenu::command(const SDL_Event &e) const {
    if (!ok || e.type != SDL_SYSWMEVENT || !e.syswm.msg) return NONE;
    const auto &m = e.syswm.msg->msg.win;
    if (m.msg != WM_COMMAND || HIWORD(m.wParam) != 0) return NONE;
    int id = LOWORD(m.wParam);
    return id >= OPEN_DISK && id <= OPTIONS ? (Cmd)id : NONE;
}

void NativeMenu::set_keyboard(bool shown) {
    if (ok) CheckMenuItem((HMENU)menu, TOGGLE_KEYBOARD, MF_BYCOMMAND | (shown ? MF_CHECKED : MF_UNCHECKED));
}

std::string NativeMenu::open_disk_dialog() {
    wchar_t buf[1024] = L"";
    OPENFILENAMEW of{}; of.lStructSize = sizeof of; of.hwndOwner = hwnd_of(win);
    of.lpstrFilter = L"Disk images and instruments (*.img;*.efe;*.efa;*.iso;*.hda)\0*.img;*.efe;*.efa;*.iso;*.hda\0All files (*.*)\0*.*\0";
    of.lpstrFile = buf; of.nMaxFile = 1024; of.lpstrTitle = L"Open disk image";
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&of)) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, buf, -1, &s[0], n, nullptr, nullptr);
    return s;
}

std::string NativeMenu::choose_folder_dialog(const std::string &title) {
    HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    std::wstring wt(title.begin(), title.end());
    BROWSEINFOW bi{}; bi.hwndOwner = hwnd_of(win); bi.lpszTitle = wt.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    std::string s;
    if (PIDLIST_ABSOLUTE id = SHBrowseForFolderW(&bi)) {
        wchar_t buf[MAX_PATH * 4] = L"";
        if (SHGetPathFromIDListW(id, buf)) {
            int n = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
            s.assign(n > 0 ? n - 1 : 0, '\0');
            if (n > 1) WideCharToMultiByte(CP_UTF8, 0, buf, -1, &s[0], n, nullptr, nullptr);
        }
        CoTaskMemFree(id);
    }
    if (SUCCEEDED(co)) CoUninitialize();
    return s;
}

#elif defined(__APPLE__)
// implemented in mac/menu_mac.mm (pushes SDL user events)
extern bool mac_menu_init(Uint32 event_type, bool kb);
extern void mac_menu_set_keyboard(bool shown);
extern std::string mac_open_disk_dialog();
extern std::string mac_choose_folder_dialog(const std::string &title);
extern void mac_set_aspect(SDL_Window *w, int aw, int ah);
static Uint32 g_menu_event = 0;

bool NativeMenu::init(SDL_Window *w, bool kb) {
    win = w;
    g_menu_event = SDL_RegisterEvents(1);
    if (g_menu_event == (Uint32)-1) return false;
    ok = mac_menu_init(g_menu_event, kb);
    return ok;
}
NativeMenu::Cmd NativeMenu::command(const SDL_Event &e) const {
    if (!ok || e.type != g_menu_event) return NONE;
    return (Cmd)e.user.code;
}
void NativeMenu::set_keyboard(bool shown) { if (ok) mac_menu_set_keyboard(shown); }
void NativeMenu::set_aspect(int w, int h) { if (win) mac_set_aspect(win, w, h); }
std::string NativeMenu::open_disk_dialog() { return mac_open_disk_dialog(); }
std::string NativeMenu::choose_folder_dialog(const std::string &t) { return mac_choose_folder_dialog(t); }

#else
#include <cstdio>
#include <sys/wait.h>
bool NativeMenu::init(SDL_Window *w, bool) { win = w; return false; }
NativeMenu::Cmd NativeMenu::command(const SDL_Event &) const { return NONE; }
void NativeMenu::set_keyboard(bool) {}
void NativeMenu::set_aspect(int, int) {}   // the program corrects the size after a resize
std::string NativeMenu::open_disk_dialog() { return ""; }
std::string NativeMenu::choose_folder_dialog(const std::string &title) {
    // Linux: zenity (GNOME) or kdialog (KDE) when installed
    std::string t;
    for (char c : title) if (c != '"' && c != '\\' && c != '$' && c != '`') t += c;
    const std::string cmds[2] = { "zenity --file-selection --directory --title=\"" + t + "\" 2>/dev/null",
                                  "kdialog --getexistingdirectory . --title \"" + t + "\" 2>/dev/null" };
    for (const auto &c : cmds) {
        FILE *f = popen(c.c_str(), "r");
        if (!f) continue;
        char buf[4096]; std::string out;
        while (fgets(buf, sizeof buf, f)) out += buf;
        int rc = pclose(f);
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
        if (rc == 0 && !out.empty()) return out;
        if (rc == 0 || (WIFEXITED(rc) && WEXITSTATUS(rc) == 1)) return "";   // cancelled
    }
    return "";
}
#endif
