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

struct Toast { std::string text; ULONGLONG expire; ImU32 color; };
inline std::mutex g_toast_mtx;
inline std::deque<Toast> g_toasts;

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

inline void init_imgui(IDirect3DDevice9* dev) {
    D3DDEVICE_CREATION_PARAMETERS cp{};
    if (dev->GetCreationParameters(&cp) == D3D_OK) g_hwnd = cp.hFocusWindow;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;               // no imgui.ini on disk
    io.MouseDrawCursor = false;
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX9_Init(dev);
    g_imgui_ready = true;
}

inline HRESULT WINAPI hk_EndScene(IDirect3DDevice9* dev) {
    __try {
        if (!g_imgui_ready) init_imgui(dev);
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        render_toasts();
        ImGui::EndFrame();
        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
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

    // Throwaway message-only window for the dummy device.
    WNDCLASSEXA wc{sizeof(wc)}; wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr); wc.lpszClassName = "AC2AP_dummy";
    RegisterClassExA(&wc);
    HWND wnd = CreateWindowA(wc.lpszClassName, "", WS_OVERLAPPEDWINDOW, 0, 0, 8, 8,
                             nullptr, nullptr, wc.hInstance, nullptr);
    if (!wnd) return false;

    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) { DestroyWindow(wnd); return false; }
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = wnd;
    IDirect3DDevice9* dev = nullptr;
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, wnd,
                                   D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_DISABLE_DRIVER_MANAGEMENT,
                                   &pp, &dev);
    if (FAILED(hr) || !dev) { d3d->Release(); DestroyWindow(wnd); return false; }

    void** vtbl = *reinterpret_cast<void***>(dev);
    void* endscene = vtbl[42];
    void* reset    = vtbl[16];
    dev->Release();
    d3d->Release();
    DestroyWindow(wnd);

    if (MH_Initialize() != MH_OK && MH_Initialize() != MH_ERROR_ALREADY_INITIALIZED) return false;
    if (MH_CreateHook(endscene, (void*)&hk_EndScene, (void**)&o_EndScene) != MH_OK) return false;
    MH_CreateHook(reset, (void*)&hk_Reset, (void**)&o_Reset);   // best-effort
    if (MH_EnableHook(endscene) != MH_OK) { o_EndScene = nullptr; return false; }
    MH_EnableHook(reset);
    return true;
}

} // namespace ac2ap::overlay
