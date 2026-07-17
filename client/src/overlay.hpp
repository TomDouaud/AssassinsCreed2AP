// AC2AP - in-game overlay (D3D9 + Dear ImGui) for multiworld feedback.
// Hooks IDirect3DDevice9::EndScene + Reset via MinHook (vtable read from a throwaway
// device - the vtable is shared by all D3D9 devices, so this hooks the game's too).
// Renders a queue of toasts ("Received: 500 Florins", "Sent Sword A -> Zelda").
// Opt-in via AC2AP.ini (enable_overlay=1); off by default until proven stable.
// Passive display only for now: no WndProc hook, no input capture -> cannot break the game's
// input. All calls are SEH/flag guarded; a failure disables the overlay rather than crashing.
#pragma once
#include <windows.h>
#include <d3d9.h>
#include <MinHook.h>

#include <deque>
#include <mutex>
#include <string>

#include "imgui.h"
#include "backends/imgui_impl_dx9.h"
#include "backends/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace ac2ap::overlay {

using EndScene_t = HRESULT(WINAPI*)(IDirect3DDevice9*);
using Reset_t    = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

inline EndScene_t o_EndScene = nullptr;
inline Reset_t    o_Reset    = nullptr;
inline bool g_imgui_ready = false;
inline bool g_enabled = false;          // set by init() from the ini flag
inline HWND g_hwnd = nullptr;
inline const char* g_fail = "";         // last install() failure point (for logging)

struct Toast { std::string text; ULONGLONG expire; ImU32 color; };
inline std::mutex g_toast_mtx;
inline std::deque<Toast> g_toasts;

// --- connection menu (toggle with INSERT) ------------------------------------
// The form fills g_conn; the worker thread polls g_conn.requested to (re)connect.
struct ConnRequest {
    char server[128] = "archipelago.gg:38281";
    char slot[64] = "";
    char password[64] = "";
    volatile bool requested = false;   // set by UI, cleared by worker
    volatile bool connected = false;   // set by worker for status display
};
inline ConnRequest g_conn;
inline bool g_menu_open = false;
inline WNDPROC o_WndProc = nullptr;

// Prefill the form fields from the ini (worker calls once at startup).
inline void set_defaults(const std::string& server, const std::string& slot, const std::string& pass) {
    if (!server.empty()) strncpy(g_conn.server, server.c_str(), sizeof(g_conn.server) - 1);
    strncpy(g_conn.slot, slot.c_str(), sizeof(g_conn.slot) - 1);
    strncpy(g_conn.password, pass.c_str(), sizeof(g_conn.password) - 1);
}
inline void set_connected(bool c) { g_conn.connected = c; }

// Thread-safe: called from the worker thread when an item/check event happens.
inline void toast(const std::string& text, ImU32 color = IM_COL32(255, 255, 255, 255),
                  unsigned ms = 5000) {
    std::lock_guard<std::mutex> lk(g_toast_mtx);
    if (g_toasts.size() > 8) g_toasts.pop_front();          // cap
    g_toasts.push_back({text, GetTickCount64() + ms, color});
}

inline void render_toasts() {
    ImGuiIO& io = ImGui::GetIO();
    ULONGLONG now = GetTickCount64();
    std::lock_guard<std::mutex> lk(g_toast_mtx);
    while (!g_toasts.empty() && g_toasts.front().expire <= now) g_toasts.pop_front();
    if (g_toasts.empty()) return;

    float y = 12.0f;
    int i = 0;
    for (const auto& t : g_toasts) {
        ULONGLONG left = t.expire > now ? t.expire - now : 0;
        float alpha = left < 600 ? (float)left / 600.0f : 1.0f;    // fade last 0.6s
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 12.0f, y), ImGuiCond_Always, ImVec2(1, 0));
        ImGui::SetNextWindowBgAlpha(0.72f * alpha);
        char id[32]; sprintf(id, "##ac2ap_toast_%d", i++);
        ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs);
        ImU32 c = (t.color & 0x00FFFFFF) | ((ImU32)(255 * alpha) << 24);
        ImGui::PushStyleColor(ImGuiCol_Text, c);
        ImGui::TextUnformatted(t.text.c_str());
        ImGui::PopStyleColor();
        y += ImGui::GetWindowHeight() + 6.0f;
        ImGui::End();
    }
}

inline void render_menu() {
    if (!g_menu_open) return;
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f,
                                   ImGui::GetIO().DisplaySize.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Appearing);
    ImGui::Begin("AC2AP - Archipelago Connection", &g_menu_open,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextColored(g_conn.connected ? ImVec4(0.5f, 0.9f, 0.5f, 1) : ImVec4(0.9f, 0.6f, 0.4f, 1),
                       g_conn.connected ? "Status: connected" : "Status: not connected");
    ImGui::Separator();
    ImGui::InputText("Server (host:port)", g_conn.server, sizeof(g_conn.server));
    ImGui::InputText("Slot / player name", g_conn.slot, sizeof(g_conn.slot));
    ImGui::InputText("Password", g_conn.password, sizeof(g_conn.password), ImGuiInputTextFlags_Password);
    ImGui::Spacing();
    if (ImGui::Button("Connect", ImVec2(120, 0))) {
        g_conn.requested = true;
        g_menu_open = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(120, 0))) g_menu_open = false;
    ImGui::TextDisabled("Press INSERT or F8 to toggle this menu.");
    ImGui::End();
}

// Is this a keyboard/mouse message we should swallow while the menu is open?
inline bool is_input_msg(UINT m) {
    return (m >= WM_KEYFIRST && m <= WM_KEYLAST) || (m >= WM_MOUSEFIRST && m <= WM_MOUSELAST) ||
           m == WM_CHAR || m == WM_SETCURSOR;
}

inline LRESULT CALLBACK hk_WndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    // INSERT toggles the menu.
    if (msg == WM_KEYDOWN && w == VK_INSERT) {
        g_menu_open = !g_menu_open;
        return 0;
    }
    if (g_imgui_ready) {
        ImGui_ImplWin32_WndProcHandler(h, msg, w, l);
        // While the menu is open, keep input from reaching the game (no Ezio moving as you type).
        if (g_menu_open && is_input_msg(msg)) return 0;
    }
    return CallWindowProcA(o_WndProc, h, msg, w, l);
}

inline void init_imgui(IDirect3DDevice9* dev) {
    D3DDEVICE_CREATION_PARAMETERS cp{};
    if (dev->GetCreationParameters(&cp) == D3D_OK) g_hwnd = cp.hFocusWindow;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;               // no imgui.ini on disk
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX9_Init(dev);
    // Hook the game's WndProc so ImGui gets keyboard/mouse (needed for the connection form).
    if (g_hwnd)
        o_WndProc = (WNDPROC)SetWindowLongPtrA(g_hwnd, GWLP_WNDPROC, (LONG_PTR)&hk_WndProc);
    g_imgui_ready = true;
}

// Poll the toggle keys (INSERT / F8) outside any __try so string temporaries are allowed.
// The game reads the keyboard via DirectInput, so WM_KEYDOWN never reaches our WndProc -
// polling the async key state here is the reliable path.
inline void poll_toggle() {
    static bool prev_down = false;
    bool down = (GetAsyncKeyState(VK_INSERT) & 0x8000) || (GetAsyncKeyState(VK_F8) & 0x8000);
    if (down && !prev_down) {
        g_menu_open = !g_menu_open;
        toast(g_menu_open ? "menu: OPEN" : "menu: closed", IM_COL32(255, 180, 80, 255), 2000);
    }
    prev_down = down;
}

inline void render_frame(IDirect3DDevice9* dev) {
    __try {
        if (!g_imgui_ready) init_imgui(dev);
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGuiIO& io = ImGui::GetIO();
        io.MouseDrawCursor = g_menu_open;               // show a cursor only while the menu is up
        // DirectInput game: mouse/keyboard don't arrive as window messages, so feed ImGui
        // manually while the menu is open. Also release the game's cursor clip so the pointer
        // can move over the menu (the game re-locks it for the camera each frame otherwise).
        if (g_menu_open && g_hwnd) {
            ClipCursor(nullptr);
            POINT p; GetCursorPos(&p); ScreenToClient(g_hwnd, &p);
            io.AddMousePosEvent((float)p.x, (float)p.y);
            io.AddMouseButtonEvent(0, (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
            io.AddMouseButtonEvent(1, (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
        }
        ImGui::NewFrame();
        render_toasts();
        render_menu();
        ImGui::EndFrame();
        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

inline HRESULT WINAPI hk_EndScene(IDirect3DDevice9* dev) {
    poll_toggle();       // key polling (may create string temporaries) - outside the __try
    render_frame(dev);   // ImGui render, SEH-guarded (no local objects to unwind)
    return o_EndScene(dev);
}

inline HRESULT WINAPI hk_Reset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp) {
    if (g_imgui_ready) ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = o_Reset(dev, pp);
    if (g_imgui_ready) ImGui_ImplDX9_CreateDeviceObjects();
    return hr;
}

// Reads the D3D9 device vtable via a throwaway device and hooks EndScene(42)/Reset(16).
// Returns false (overlay stays off) on any failure - never fatal.
inline bool install(bool enabled_from_ini) {
    g_enabled = enabled_from_ini;
    if (!g_enabled) return false;

    // Throwaway window for the dummy device. Use the game's foreground window as focus
    // (a fresh hidden HWND can make CreateDevice fail while the game holds the display).
    HWND focus = GetForegroundWindow();
    if (!focus) focus = GetDesktopWindow();

    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) { g_fail = "Direct3DCreate9"; return false; }
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = focus;
    pp.BackBufferWidth = 8;
    pp.BackBufferHeight = 8;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    IDirect3DDevice9* dev = nullptr;
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, focus,
                                   D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED,
                                   &pp, &dev);
    if (FAILED(hr) || !dev) {   // retry with NULLREF device (vtable is the same)
        hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_NULLREF, focus,
                               D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED,
                               &pp, &dev);
    }
    if (FAILED(hr) || !dev) { g_fail = "CreateDevice"; d3d->Release(); return false; }

    void** vtbl = *reinterpret_cast<void***>(dev);
    void* endscene = vtbl[42];
    void* reset    = vtbl[16];
    dev->Release();
    d3d->Release();

    if (MH_Initialize() != MH_OK && MH_Initialize() != MH_ERROR_ALREADY_INITIALIZED) {
        g_fail = "MH_Initialize"; return false;
    }
    if (MH_CreateHook(endscene, (void*)&hk_EndScene, (void**)&o_EndScene) != MH_OK) {
        g_fail = "MH_CreateHook(EndScene)"; return false;
    }
    MH_CreateHook(reset, (void*)&hk_Reset, (void**)&o_Reset);   // best-effort
    if (MH_EnableHook(endscene) != MH_OK) { g_fail = "MH_EnableHook"; o_EndScene = nullptr; return false; }
    MH_EnableHook(reset);
    return true;
}

} // namespace ac2ap::overlay
