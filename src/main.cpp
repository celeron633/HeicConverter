#include "converter.h"
#include "utf8.h"

#include <d3d11.h>
#include <shobjidl.h>
#include <windows.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <filesystem>
#include <string>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

namespace {

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_deviceContext = nullptr;
IDXGISwapChain* g_swapChain = nullptr;
ID3D11RenderTargetView* g_renderTargetView = nullptr;

void CreateRenderTarget() {
    ID3D11Texture2D* backBuffer = nullptr;
    if (SUCCEEDED(g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
        g_device->CreateRenderTargetView(backBuffer, nullptr, &g_renderTargetView);
        backBuffer->Release();
    }
}

void CleanupRenderTarget() {
    if (g_renderTargetView != nullptr) {
        g_renderTargetView->Release();
        g_renderTargetView = nullptr;
    }
}

bool CreateDeviceD3D(HWND window) {
    DXGI_SWAP_CHAIN_DESC description{};
    description.BufferCount = 2;
    description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.OutputWindow = window;
    description.SampleDesc.Count = 1;
    description.Windowed = TRUE;
    description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    description.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    constexpr D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL createdFeatureLevel{};
    HRESULT result = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &description,
        &g_swapChain,
        &g_device,
        &createdFeatureLevel,
        &g_deviceContext);
    if (FAILED(result)) {
        result = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            0,
            featureLevels,
            ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            &description,
            &g_swapChain,
            &g_device,
            &createdFeatureLevel,
            &g_deviceContext);
    }
    if (FAILED(result)) {
        return false;
    }

    CreateRenderTarget();
    return g_renderTargetView != nullptr;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_swapChain != nullptr) {
        g_swapChain->Release();
        g_swapChain = nullptr;
    }
    if (g_deviceContext != nullptr) {
        g_deviceContext->Release();
        g_deviceContext = nullptr;
    }
    if (g_device != nullptr) {
        g_device->Release();
        g_device = nullptr;
    }
}

std::string PickFolder(HWND owner) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return {};
    }

    DWORD flags = 0;
    dialog->GetOptions(&flags);
    dialog->SetOptions(flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"选择包含 HEIC 文件的文件夹");

    std::string selected;
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                selected = WideToUtf8(path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return selected;
}

void ConfigureStyleAndFont() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0F;
    style.FrameRounding = 5.0F;
    style.GrabRounding = 5.0F;
    style.WindowPadding = ImVec2(22.0F, 18.0F);
    style.ItemSpacing = ImVec2(10.0F, 11.0F);
    style.FramePadding = ImVec2(10.0F, 7.0F);

    ImGuiIO& io = ImGui::GetIO();
    const ImWchar* ranges = io.Fonts->GetGlyphRangesChineseFull();
    const char* candidates[] = {
        "C:\\Windows\\Fonts\\msyh.ttc",
        "C:\\Windows\\Fonts\\msyhbd.ttc",
        "C:\\Windows\\Fonts\\simhei.ttf",
    };
    for (const char* candidate : candidates) {
        if (GetFileAttributesA(candidate) != INVALID_FILE_ATTRIBUTES &&
            io.Fonts->AddFontFromFileTTF(candidate, 18.0F, nullptr, ranges) != nullptr) {
            break;
        }
    }
}

void DrawInterface(HWND window, ConversionController& controller) {
    static std::string folder;
    static bool recursive = true;
    static bool deleteOriginals = false;
    static bool overwriteExisting = false;
    static int outputFormat = 0;
    static int jpegQuality = 92;
    static bool scrollToBottom = false;
    static size_t previousLogSize = 0;

    const ConversionSnapshot snapshot = controller.Snapshot();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("HeicConverter", nullptr, flags);

    ImGui::TextUnformatted("HEIC → PNG / JPG 批量转换");
    ImGui::TextDisabled("libheif 解码 · libpng / libjpeg-turbo 导出");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::BeginDisabled(snapshot.running);
    ImGui::SetNextItemWidth(-112.0F);
    ImGui::InputTextWithHint("##folder", "请选择文件夹", &folder);
    ImGui::SameLine();
    if (ImGui::Button("浏览…", ImVec2(100.0F, 0.0F))) {
        const std::string picked = PickFolder(window);
        if (!picked.empty()) {
            folder = picked;
        }
    }

    ImGui::Checkbox("搜索子目录", &recursive);
    ImGui::SameLine(190.0F);
    ImGui::Checkbox("转换成功后删除原 HEIC", &deleteOriginals);
    ImGui::SameLine(470.0F);
    ImGui::Checkbox("覆盖已有目标文件", &overwriteExisting);

    ImGui::TextUnformatted("输出格式:");
    ImGui::SameLine(105.0F);
    ImGui::RadioButton("PNG（无损、支持透明）", &outputFormat, 0);
    ImGui::SameLine(330.0F);
    ImGui::RadioButton("JPG（体积较小）", &outputFormat, 1);
    if (outputFormat == 1) {
        ImGui::SameLine(530.0F);
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::SliderInt("##jpeg-quality", &jpegQuality, 1, 100, "质量 %d");
    }

    bool folderValid = false;
    try {
        if (!folder.empty()) {
            std::error_code error;
            folderValid = std::filesystem::is_directory(Utf8ToWide(folder), error) && !error;
        }
    } catch (...) {
        folderValid = false;
    }

    ImGui::BeginDisabled(!folderValid);
    if (ImGui::Button("开始转换", ImVec2(130.0F, 38.0F))) {
        ConversionOptions options;
        options.folder = Utf8ToWide(folder);
        options.recursive = recursive;
        options.deleteOriginals = deleteOriginals;
        options.overwriteExisting = overwriteExisting;
        options.outputFormat = outputFormat == 1 ? OutputFormat::Jpeg : OutputFormat::Png;
        options.jpegQuality = jpegQuality;
        controller.Start(std::move(options));
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();

    if (snapshot.running) {
        ImGui::SameLine();
        if (ImGui::Button(snapshot.cancellationRequested ? "正在停止…" : "取消", ImVec2(130.0F, 38.0F)) &&
            !snapshot.cancellationRequested) {
            controller.Cancel();
        }
    }

    ImGui::Spacing();
    const float progress = snapshot.total == 0
                               ? 0.0F
                               : static_cast<float>(snapshot.processed) / static_cast<float>(snapshot.total);
    std::string overlay = std::to_string(snapshot.processed) + " / " + std::to_string(snapshot.total);
    ImGui::ProgressBar(std::clamp(progress, 0.0F, 1.0F), ImVec2(-1.0F, 26.0F), overlay.c_str());
    ImGui::Text("状态: %s", snapshot.status.c_str());
    ImGui::Text(
        "成功 %zu    失败 %zu    跳过 %zu",
        snapshot.succeeded,
        snapshot.failed,
        snapshot.skipped);
    if (!snapshot.currentFile.empty()) {
        ImGui::TextWrapped("当前: %s", snapshot.currentFile.c_str());
    }

    ImGui::Spacing();
    ImGui::SeparatorText("处理日志");
    const float footerHeight = ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("log", ImVec2(0.0F, -footerHeight), ImGuiChildFlags_Borders);
    for (const std::string& line : snapshot.log) {
        if (line.rfind("失败:", 0) == 0 || line.rfind("任务失败:", 0) == 0) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.42F, 0.42F, 1.0F));
            ImGui::TextWrapped("%s", line.c_str());
            ImGui::PopStyleColor();
        } else if (line.rfind("跳过:", 0) == 0 || line.rfind("扫描警告:", 0) == 0) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.78F, 0.35F, 1.0F));
            ImGui::TextWrapped("%s", line.c_str());
            ImGui::PopStyleColor();
        } else {
            ImGui::TextWrapped("%s", line.c_str());
        }
    }
    if (snapshot.log.size() != previousLogSize) {
        scrollToBottom = true;
        previousLogSize = snapshot.log.size();
    }
    if (scrollToBottom) {
        ImGui::SetScrollHereY(1.0F);
        scrollToBottom = false;
    }
    ImGui::EndChild();
    ImGui::TextDisabled("提示：只有目标图片成功写入后才会删除原文件；JPG 不支持透明通道。");

    ImGui::End();
}

LRESULT WINAPI WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam)) {
        return TRUE;
    }

    switch (message) {
    case WM_SIZE:
        if (g_device != nullptr && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_swapChain->ResizeBuffers(0, static_cast<UINT>(LOWORD(lParam)), static_cast<UINT>(HIWORD(lParam)),
                                       DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0U) == SC_KEYMENU) {
            return 0;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    WNDCLASSEXW windowClass{
        sizeof(WNDCLASSEXW),
        CS_CLASSDC,
        WindowProcedure,
        0,
        0,
        instance,
        LoadIconW(nullptr, IDI_APPLICATION),
        LoadCursorW(nullptr, IDC_ARROW),
        nullptr,
        nullptr,
        L"HeicConverterWindow",
        LoadIconW(nullptr, IDI_APPLICATION),
    };
    RegisterClassExW(&windowClass);

    HWND window = CreateWindowW(
        windowClass.lpszClassName,
        L"HeicConverter",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        920,
        680,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (window == nullptr || !CreateDeviceD3D(window)) {
        if (window != nullptr) {
            DestroyWindow(window);
        }
        UnregisterClassW(windowClass.lpszClassName, instance);
        if (SUCCEEDED(comResult)) {
            CoUninitialize();
        }
        MessageBoxW(nullptr, L"Direct3D 11 初始化失败。", L"HeicConverter", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(window, showCommand);
    UpdateWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ConfigureStyleAndFont();
    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init(g_device, g_deviceContext);

    ConversionController controller;
    bool done = false;
    while (!done) {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            if (message.message == WM_QUIT) {
                done = true;
            }
        }
        if (done) {
            break;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        DrawInterface(window, controller);
        ImGui::Render();

        constexpr float clearColor[4] = {0.08F, 0.09F, 0.11F, 1.0F};
        g_deviceContext->OMSetRenderTargets(1, &g_renderTargetView, nullptr);
        g_deviceContext->ClearRenderTargetView(g_renderTargetView, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swapChain->Present(1, 0);
    }

    controller.Cancel();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(window);
    UnregisterClassW(windowClass.lpszClassName, instance);
    if (SUCCEEDED(comResult)) {
        CoUninitialize();
    }
    return 0;
}
