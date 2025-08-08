#include <Windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx9.h>
#include <detours.h>
#include <fstream>
#include <string>
#include <iostream>
#include "json.hpp"

// This external function is part of ImGui's Win32 implementation. 
// It helps handle Windows messages properly when the menu is active.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

using json = nlohmann::json;  // Using the json library, because JSON config files rock!

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")

// Typedef for the EndScene function from Direct3D9.
// We're gonna hook this bad boy to draw our menu on top of the game.
typedef HRESULT(__stdcall* EndScene_t)(LPDIRECT3DDEVICE9);
EndScene_t oEndScene = nullptr; // Original EndScene pointer - we keep this so we can call the real function later.

HWND window = nullptr;         // Handle to the game window
WNDPROC oWndProc = nullptr;    // Original Window Procedure, so we can chain calls after hooking
bool initialized = false;      // Flag to know if we've initialized ImGui stuff yet
bool showMenu = false;         // Whether our menu is currently visible (starts off hidden)

// This struct holds all our config options. Defaults are set here.
struct Config
{
    int max_framerate = 144;
    int field_of_view = 90;
    int center_field_of_view = 100;
    int sprint_field_of_view = 110;

    int display_mode = 0;           // 0 = Windowed, 1 = Fullscreen, 2 = Borderless
    int resolution_width = 1920;
    int resolution_height = 1080;
    int aspect_ratio = 1;           // Index for aspect ratio presets
    int graphic_quality = 2;        // 0 = Low, 1 = Medium, 2 = High
} config;

// File path where config will be saved and loaded from
const char* configFilePath = "config.json";

// Some presets for UI combos - keeps things clean and easy to select
const char* displayModes[] = { "Windowed", "Fullscreen", "Borderless" };
const char* qualityLevels[] = { "Low", "Medium", "High" };
const char* resolutionPresets[] = { "800x600","1280x720", "1600x900", "1920x1080", "2560x1440", "3440x1440", "3840x2160" };
const char* aspectRatios[] = { "4:3", "16:9", "16:10", "21:9" };

// Our custom Window Procedure to handle input when the menu is active
LRESULT __stdcall WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    // If the menu is open, let ImGui process the messages first
    if (showMenu && ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
        return true;

    // Otherwise, call the original WindowProc to not break normal behavior
    return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

// Loads config from JSON file, if it exists. Returns false if it fails (first run or missing file)
bool LoadConfig()
{
    std::ifstream i(configFilePath);
    if (!i.is_open())
        return false; // No config file yet, no biggie

    try
    {
        json j;
        i >> j;  // Read entire file into JSON

        // Use .value with default to be safe if some fields are missing
        config.max_framerate = j.value("max_framerate", config.max_framerate);
        config.field_of_view = j.value("field_of_view", config.field_of_view);
        config.center_field_of_view = j.value("center_field_of_view", config.center_field_of_view);
        config.sprint_field_of_view = j.value("sprint_field_of_view", config.sprint_field_of_view);
        config.display_mode = j.value("display_mode", config.display_mode);
        config.resolution_width = j.value("resolution_width", config.resolution_width);
        config.resolution_height = j.value("resolution_height", config.resolution_height);
        config.aspect_ratio = j.value("aspect_ratio", config.aspect_ratio);
        config.graphic_quality = j.value("graphic_quality", config.graphic_quality);
    }
    catch (...)
    {
        return false;  // Something went wrong parsing the file, ignore it silently
    }
    return true;
}

// Save the current config to the JSON file, nicely formatted
bool SaveConfig()
{
    try
    {
        json j;
        j["max_framerate"] = config.max_framerate;
        j["field_of_view"] = config.field_of_view;
        j["center_field_of_view"] = config.center_field_of_view;
        j["sprint_field_of_view"] = config.sprint_field_of_view;
        j["display_mode"] = config.display_mode;
        j["resolution_width"] = config.resolution_width;
        j["resolution_height"] = config.resolution_height;
        j["aspect_ratio"] = config.aspect_ratio;
        j["graphic_quality"] = config.graphic_quality;

        std::ofstream o(configFilePath);
        o << j.dump(4);  // dump with 4 spaces indentation for readability
        return true;
    }
    catch (...)
    {
        return false; // If saving fails, just silently fail - maybe disk error or something
    }
}

// This function applies a nice gamer-themed style to ImGui, because default looks meh
void ApplyGamerStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();

    // Padding and spacing - comfy for clicking and reading
    style.WindowPadding = ImVec2(10, 10);
    style.FramePadding = ImVec2(8, 4);
    style.ItemSpacing = ImVec2(8, 6);
    style.ItemInnerSpacing = ImVec2(6, 4);
    style.IndentSpacing = 20.0f;
    style.ScrollbarSize = 10.0f;
    style.GrabMinSize = 12.0f;

    // Rounded corners, because sharp edges are boring
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 4.0f;
    style.GrabRounding = 3.0f;
    style.ScrollbarRounding = 4.0f;
    style.ChildRounding = 4.0f;
    style.TabRounding = 3.0f;

    // Now let's jazz it up with some colors — blues and darks for that sleek gamer vibe
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.95f, 1.0f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.12f, 0.15f, 0.95f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.18f, 0.22f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.28f, 0.28f, 0.35f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.24f, 0.30f, 1.0f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.15f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.14f, 0.14f, 0.22f, 1.0f);

    // Buttons get a vibrant blue highlight when hovered or active, looks super clean
    colors[ImGuiCol_Button] = ImVec4(0.16f, 0.52f, 0.96f, 0.85f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.68f, 1.00f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.20f, 0.56f, 0.90f, 1.0f);

    // Headers (like tabs or group titles) get similar blues
    colors[ImGuiCol_Header] = ImVec4(0.10f, 0.52f, 0.92f, 0.80f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.16f, 0.68f, 1.00f, 1.0f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.56f, 0.90f, 1.0f);

    // Tabs get a dark background with a nice blue highlight on hover and active
    colors[ImGuiCol_Tab] = ImVec4(0.14f, 0.14f, 0.17f, 0.97f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.20f, 0.50f, 0.90f, 1.0f);
    colors[ImGuiCol_TabActive] = ImVec4(0.18f, 0.60f, 0.98f, 1.0f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.12f, 0.12f, 0.15f, 0.75f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.15f, 0.25f, 0.45f, 0.97f);
}

// Renders the first tab - game settings like FOV and max FPS
void RenderSettingsTab()
{
    ImGui::Text("GAME SETTINGS");
    ImGui::Spacing();

    // One slider to control all FOVs simultaneously, max capped at 100° because anything more is nuts
    int newFov = config.field_of_view;
    if (ImGui::SliderInt("Field of View", &newFov, 60, 100, "%d°"))
    {
        // Keep all three FOV values in sync, for simplicity
        config.field_of_view = newFov;
        config.center_field_of_view = newFov;
        config.sprint_field_of_view = newFov;
    }

    // Just showing the synced FOVs so user knows they're all the same right now
    ImGui::Text("Center Field of View: %d°", config.center_field_of_view);
    ImGui::Text("Sprint Field of View: %d°", config.sprint_field_of_view);

    ImGui::SliderInt("Max Framerate", &config.max_framerate, 30, 360, "%d FPS");
}

// Second tab for graphic and display settings
void RenderGraphicSettingsTab()
{
    ImGui::Text("DISPLAY SETTINGS");
    ImGui::Spacing();

    // Simple combo box for screen modes
    ImGui::Combo("Screen Type", &config.display_mode, displayModes, IM_ARRAYSIZE(displayModes));

    // Current resolution displayed as a string like "1920x1080"
    std::string currentRes = std::to_string(config.resolution_width) + "x" + std::to_string(config.resolution_height);

    // Dropdown to pick resolution from presets
    if (ImGui::BeginCombo("Resolution", currentRes.c_str()))
    {
        for (int i = 0; i < IM_ARRAYSIZE(resolutionPresets); i++)
        {
            bool selected = (currentRes == resolutionPresets[i]);
            if (ImGui::Selectable(resolutionPresets[i], selected))
            {
                // When user picks one, parse it into width and height integers
                size_t x = std::string(resolutionPresets[i]).find('x');
                config.resolution_width = std::stoi(std::string(resolutionPresets[i]).substr(0, x));
                config.resolution_height = std::stoi(std::string(resolutionPresets[i]).substr(x + 1));
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    // Aspect ratio picker, because some people like weird ultrawide or classic 4:3
    ImGui::Combo("Aspect Ratio", &config.aspect_ratio, aspectRatios, IM_ARRAYSIZE(aspectRatios));

    ImGui::Separator();
    ImGui::Text("GRAPHICS QUALITY");
    ImGui::Spacing();

    // Graphics quality presets
    ImGui::Combo("Quality Preset", &config.graphic_quality, qualityLevels, IM_ARRAYSIZE(qualityLevels));
}

// Our hooked EndScene function, where the magic happens and we draw our menu
HRESULT __stdcall hkEndScene(LPDIRECT3DDEVICE9 pDevice)
{
    if (!initialized)
    {
        // Find the game window, if we can't find it just bail for now
        window = FindWindowA("S4_Client", nullptr);
        if (!window)
            return oEndScene(pDevice);

        // Create ImGui context and setup styles
        ImGui::CreateContext();
        ApplyGamerStyle();

        // Initialize ImGui for Win32 and DirectX9
        ImGui_ImplWin32_Init(window);
        ImGui_ImplDX9_Init(pDevice);

        // Hook the window proc so we can intercept input events (like keyboard presses)
        oWndProc = (WNDPROC)SetWindowLongPtr(window, GWLP_WNDPROC, (LONG_PTR)WndProc);

        // Load config from file if present
        LoadConfig();

        initialized = true;  // Done with initialization, woo!
    }

    // Toggle the menu visibility with the DELETE key — gotta love hotkeys
    static bool lastDeleteState = false;
    bool currentDeleteState = (GetAsyncKeyState(VK_DELETE) & 0x8000) != 0;
    if (currentDeleteState && !lastDeleteState)  // Key pressed event (not held)
        showMenu = !showMenu;
    lastDeleteState = currentDeleteState;

    // Start a new ImGui frame for drawing our stuff
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if (showMenu)
    {
        ImGui::SetNextWindowSize(ImVec2(600, 350), ImGuiCond_Once);
        ImGui::Begin("Gamer Config", &showMenu, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

        // Tabs for switching between settings categories
        if (ImGui::BeginTabBar("MainTabs"))
        {
            if (ImGui::BeginTabItem("Settings"))
            {
                RenderSettingsTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Graphics"))
            {
                RenderGraphicSettingsTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::Spacing();
        ImGui::Separator();

        float btnWidth = 140.0f;

        // Center the buttons horizontally by moving the cursor X
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - btnWidth * 2 - ImGui::GetStyle().ItemSpacing.x) / 2);

        if (ImGui::Button("Save Config", ImVec2(btnWidth, 35)))
            SaveConfig();

        ImGui::SameLine();

        if (ImGui::Button("Close Menu", ImVec2(btnWidth, 35)))
            showMenu = false;

        ImGui::End();
    }

    // Render everything ImGui has drawn so far
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());

    // Call original EndScene so the game keeps working as usual
    return oEndScene(pDevice);
}

// Thread function that hooks EndScene once the game window is found and Direct3D device is created
DWORD WINAPI InitHook(LPVOID)
{
    // Wait for the game window to show up — gotta be patient
    while (!(window = FindWindowA("S4_Client", nullptr)))
        Sleep(100);

    // Create a temporary Direct3D interface and device just to get the vtable address for hooking
    IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) return 0;

    D3DPRESENT_PARAMETERS d3dpp{};
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.hDeviceWindow = window;

    IDirect3DDevice9* pDevice = nullptr;
    if (FAILED(pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDevice)))
    {
        pD3D->Release();
        return 0;
    }

    // Grab the virtual method table from the device, EndScene is at index 42 (yeah, magic number)
    void** vTable = *reinterpret_cast<void***>(pDevice);
    oEndScene = (EndScene_t)vTable[42];

    // Use Detours library to hook EndScene to our custom function
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)oEndScene, hkEndScene);
    DetourTransactionCommit();

    // Cleanup temp device and interface
    pDevice->Release();
    pD3D->Release();

    return 0;
}

// DLL main entry point, sets up the hook when the DLL is loaded, and cleans up on unload
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule); // Optimization, we don't need DLL_THREAD_ATTACH etc.
        CreateThread(nullptr, 0, InitHook, nullptr, 0, nullptr); // Start the hooking thread
    }
    else if (reason == DLL_PROCESS_DETACH && initialized)
    {
        // Restore original WindowProc when DLL is unloaded (cleanup!)
        SetWindowLongPtr(window, GWLP_WNDPROC, (LONG_PTR)oWndProc);
    }
    return TRUE;
}
