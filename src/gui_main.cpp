#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <wrl.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <dwmapi.h>
#include <string>
#include <vector>
#include <sstream>
#include <thread>
#include <mutex>
#include <filesystem>
#include <iostream>
#include <memory>
#include <atomic>

#include "WebView2.h"
#include "converter_engine.hpp"

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "dwmapi.lib")

using namespace Microsoft::WRL;
namespace fs = std::filesystem;

// Global UI Handles
static HWND g_hWnd = nullptr;
static ComPtr<ICoreWebView2Controller> g_controller = nullptr;
static ComPtr<ICoreWebView2> g_webview = nullptr;

static std::string g_last_output_dir = "";
static std::vector<fs::path> g_last_converted_files;
static std::wstring g_last_out_ext = L"jpg";
static std::mutex g_files_mutex;

// Helper to escape strings for JavaScript execution
static std::wstring escape_js_string(const std::wstring& str) {
    std::wstring out;
    for (wchar_t c : str) {
        if (c == L'\\') out += L"\\\\";
        else if (c == L'"') out += L"\\\"";
        else if (c == L'\'') out += L"\\\'";
        else if (c == L'\n') out += L"\\n";
        else if (c == L'\r') out += L"\\r";
        else if (c == L'\t') out += L"\\t";
        else out += c;
    }
    return out;
}

static std::string ws2s(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string str(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &str[0], size, nullptr, nullptr);
    return str;
}

static std::wstring s2ws(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), nullptr, 0);
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), &wstr[0], size);
    return wstr;
}

// Execute JavaScript in webview from any thread safely
static void exec_js_safe(const std::wstring& js) {
    if (!g_hWnd || !g_webview) return;
    std::wstring* script_copy = new std::wstring(js);
    PostMessageW(g_hWnd, WM_USER + 101, (WPARAM)script_copy, 0);
}

// Native Win32 Open Folder Dialog
static std::wstring open_folder_dialog(HWND parent) {
    std::wstring selected_path;
    IFileOpenDialog* pFileOpen = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
    if (SUCCEEDED(hr)) {
        DWORD dwOptions;
        if (SUCCEEDED(pFileOpen->GetOptions(&dwOptions))) {
            pFileOpen->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        }
        pFileOpen->SetTitle(L"选择包含 JP2 文件的文件夹");
        if (SUCCEEDED(pFileOpen->Show(parent))) {
            IShellItem* pItem = nullptr;
            if (SUCCEEDED(pFileOpen->GetResult(&pItem))) {
                PWSTR pszFilePath = nullptr;
                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath))) {
                    selected_path = pszFilePath;
                    CoTaskMemFree(pszFilePath);
                }
                pItem->Release();
            }
        }
        pFileOpen->Release();
    }
    return selected_path;
}

// Native Win32 Open Files Dialog
static std::vector<std::wstring> open_files_dialog(HWND parent) {
    std::vector<std::wstring> file_paths;
    IFileOpenDialog* pFileOpen = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
    if (SUCCEEDED(hr)) {
        DWORD dwOptions;
        if (SUCCEEDED(pFileOpen->GetOptions(&dwOptions))) {
            pFileOpen->SetOptions(dwOptions | FOS_ALLOWMULTISELECT | FOS_FORCEFILESYSTEM);
        }
        COMDLG_FILTERSPEC fileTypes[] = {
            { L"JPEG 2000 图像 (*.jp2;*.j2k)", L"*.jp2;*.j2k;*.jpf;*.jpc" },
            { L"所有文件 (*.*)", L"*.*" }
        };
        pFileOpen->SetFileTypes(2, fileTypes);
        pFileOpen->SetTitle(L"选择 JP2 图像文件");

        if (SUCCEEDED(pFileOpen->Show(parent))) {
            IShellItemArray* pItems = nullptr;
            if (SUCCEEDED(pFileOpen->GetResults(&pItems))) {
                DWORD count = 0;
                pItems->GetCount(&count);
                for (DWORD i = 0; i < count; ++i) {
                    IShellItem* pItem = nullptr;
                    if (SUCCEEDED(pItems->GetItemAt(i, &pItem))) {
                        PWSTR pszFilePath = nullptr;
                        if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath))) {
                            file_paths.push_back(pszFilePath);
                            CoTaskMemFree(pszFilePath);
                        }
                        pItem->Release();
                    }
                }
                pItems->Release();
            }
        }
        pFileOpen->Release();
    }
    return file_paths;
}

// Native Win32 Save File Dialog (Save single image to custom path)
static std::wstring save_file_dialog(HWND parent, const std::wstring& default_name, const std::wstring& ext) {
    std::wstring selected_path;
    IFileSaveDialog* pFileSave = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, NULL, CLSCTX_ALL, IID_IFileSaveDialog, reinterpret_cast<void**>(&pFileSave));
    if (SUCCEEDED(hr)) {
        pFileSave->SetFileName(default_name.c_str());
        pFileSave->SetDefaultExtension(ext.c_str());
        std::wstring filter_name = (ext == L"jpg" || ext == L"jpeg") ? L"JPEG 图像 (*.jpg)" : L"PNG 图像 (*.png)";
        std::wstring filter_spec = L"*." + ext;
        COMDLG_FILTERSPEC fileTypes[] = {
            { filter_name.c_str(), filter_spec.c_str() },
            { L"所有文件 (*.*)", L"*.*" }
        };
        pFileSave->SetFileTypes(2, fileTypes);
        pFileSave->SetTitle(L"保存图像为...");
        if (SUCCEEDED(pFileSave->Show(parent))) {
            IShellItem* pItem = nullptr;
            if (SUCCEEDED(pFileSave->GetResult(&pItem))) {
                PWSTR pszFilePath = nullptr;
                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath))) {
                    selected_path = pszFilePath;
                    CoTaskMemFree(pszFilePath);
                }
                pItem->Release();
            }
        }
        pFileSave->Release();
    }
    return selected_path;
}

// Native Win32 Custom Save Folder Dialog
static std::wstring save_folder_dialog(HWND parent, const std::wstring& title) {
    std::wstring selected_path;
    IFileOpenDialog* pFileOpen = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
    if (SUCCEEDED(hr)) {
        DWORD dwOptions;
        if (SUCCEEDED(pFileOpen->GetOptions(&dwOptions))) {
            pFileOpen->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        }
        pFileOpen->SetTitle(title.empty() ? L"选择保存图像的目标文件夹" : title.c_str());
        if (SUCCEEDED(pFileOpen->Show(parent))) {
            IShellItem* pItem = nullptr;
            if (SUCCEEDED(pFileOpen->GetResult(&pItem))) {
                PWSTR pszFilePath = nullptr;
                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath))) {
                    selected_path = pszFilePath;
                    CoTaskMemFree(pszFilePath);
                }
                pItem->Release();
            }
        }
        pFileOpen->Release();
    }
    return selected_path;
}

// Process dropped files/folders from Windows Explorer
static void handle_dropped_paths(const std::vector<std::wstring>& paths) {
    if (paths.empty()) return;
    
    std::vector<fs::path> all_jp2_files;
    std::error_code ec;

    for (const auto& wpath : paths) {
        fs::path p(wpath);
        if (fs::is_directory(p)) {
            std::vector<fs::path> scanned = ConverterEngine::scan_files(p, true);
            all_jp2_files.insert(all_jp2_files.end(), scanned.begin(), scanned.end());
        } else if (fs::is_regular_file(p)) {
            all_jp2_files.push_back(p);
        }
    }

    if (!all_jp2_files.empty()) {
        std::wstringstream js;
        js << L"window.onNativeFilesSelected([";
        for (size_t i = 0; i < all_jp2_files.size(); ++i) {
            if (i > 0) js << L", ";
            auto fsize = fs::file_size(all_jp2_files[i], ec);
            js << L"{\"name\":\"" << escape_js_string(all_jp2_files[i].filename().wstring()) 
               << L"\", \"path\":\"" << escape_js_string(all_jp2_files[i].wstring()) 
               << L"\", \"size\":" << fsize << L"}";
        }
        js << L"]);";
        exec_js_safe(js.str());
    }
}

// Helper to unescape JSON string values
static std::string unescape_json_string(const std::string& str) {
    std::string out;
    out.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '\\' && i + 1 < str.size()) {
            char next = str[i + 1];
            if (next == '\\') { out += '\\'; i++; }
            else if (next == '"') { out += '"'; i++; }
            else if (next == '/') { out += '/'; i++; }
            else if (next == 'n') { out += '\n'; i++; }
            else if (next == 'r') { out += '\r'; i++; }
            else if (next == 't') { out += '\t'; i++; }
            else if (next == 'u' && i + 5 < str.size()) {
                try {
                    std::string hex_str = str.substr(i + 2, 4);
                    wchar_t wch = static_cast<wchar_t>(std::stoul(hex_str, nullptr, 16));
                    int u8size = WideCharToMultiByte(CP_UTF8, 0, &wch, 1, nullptr, 0, nullptr, nullptr);
                    if (u8size > 0) {
                        std::string u8c(u8size, 0);
                        WideCharToMultiByte(CP_UTF8, 0, &wch, 1, &u8c[0], u8size, nullptr, nullptr);
                        out += u8c;
                    }
                    i += 5;
                } catch (...) {
                    out += str[i];
                }
            } else {
                out += str[i];
            }
        } else {
            out += str[i];
        }
    }
    return out;
}

// JSON parsing helper
static std::string get_json_field(const std::string& json, const std::string& key) {
    std::string search_key = "\"" + key + "\":";
    size_t pos = json.find(search_key);
    if (pos == std::string::npos) {
        search_key = "\"" + key + "\" :";
        pos = json.find(search_key);
    }
    if (pos == std::string::npos) return "";

    pos += search_key.length();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (pos >= json.size()) return "";

    if (json[pos] == '"') {
        size_t end_pos = pos + 1;
        while (end_pos < json.size()) {
            if (json[end_pos] == '"' && json[end_pos - 1] != '\\') break;
            end_pos++;
        }
        if (end_pos < json.size()) {
            std::string raw = json.substr(pos + 1, end_pos - (pos + 1));
            return unescape_json_string(raw);
        }
    } else {
        size_t end_pos = json.find_first_of(",}\n\r", pos);
        if (end_pos != std::string::npos) {
            return json.substr(pos, end_pos - pos);
        }
    }
    return "";
}

// Clean and normalize paths across Windows
static fs::path to_clean_path(const std::string& u8str) {
    if (u8str.empty()) return fs::path();
    std::wstring wstr = s2ws(u8str);
    for (auto& ch : wstr) {
        if (ch == L'/') ch = L'\\';
    }
    std::wstring clean;
    for (size_t i = 0; i < wstr.size(); ++i) {
        if (wstr[i] == L'\\' && i > 0 && i + 1 < wstr.size() && wstr[i + 1] == L'\\') {
            continue;
        }
        clean += wstr[i];
    }
    return fs::path(clean);
}

// Extract string array from JSON with support for escaped backslashes
static std::vector<std::string> get_json_string_array(const std::string& json, const std::string& key) {
    std::vector<std::string> items;
    std::string search_key = "\"" + key + "\":";
    size_t pos = json.find(search_key);
    if (pos == std::string::npos) return items;

    size_t arr_start = json.find('[', pos);
    size_t arr_end = json.find(']', arr_start);
    if (arr_start == std::string::npos || arr_end == std::string::npos) return items;

    std::string arr_str = json.substr(arr_start + 1, arr_end - arr_start - 1);
    size_t cur = 0;
    while (cur < arr_str.size()) {
        size_t quote_start = arr_str.find('"', cur);
        if (quote_start == std::string::npos) break;
        
        size_t quote_end = quote_start + 1;
        while (quote_end < arr_str.size()) {
            if (arr_str[quote_end] == '"' && arr_str[quote_end - 1] != '\\') break;
            quote_end++;
        }
        if (quote_end >= arr_str.size()) break;

        std::string raw_val = arr_str.substr(quote_start + 1, quote_end - quote_start - 1);
        items.push_back(unescape_json_string(raw_val));
        cur = quote_end + 1;
    }
    return items;
}

struct TaskItem {
    std::string id;
    std::string path;
    std::string name;
};

// Extract structured tasks array from JSON
static std::vector<TaskItem> get_json_tasks(const std::string& json) {
    std::vector<TaskItem> tasks;
    std::string search_key = "\"tasks\":";
    size_t pos = json.find(search_key);
    if (pos == std::string::npos) {
        search_key = "\"tasks\" :";
        pos = json.find(search_key);
    }
    if (pos != std::string::npos) {
        size_t arr_start = json.find('[', pos);
        size_t arr_end = json.rfind(']');
        if (arr_start != std::string::npos && arr_end != std::string::npos && arr_end > arr_start) {
            size_t obj_start = arr_start;
            while ((obj_start = json.find('{', obj_start)) != std::string::npos && obj_start < arr_end) {
                size_t obj_end = json.find('}', obj_start);
                if (obj_end == std::string::npos || obj_end > arr_end) break;
                std::string obj_str = json.substr(obj_start, obj_end - obj_start + 1);
                
                TaskItem item;
                item.id = get_json_field(obj_str, "id");
                item.path = get_json_field(obj_str, "path");
                item.name = get_json_field(obj_str, "name");

                if (!item.path.empty() || !item.id.empty()) {
                    tasks.push_back(item);
                }
                obj_start = obj_end + 1;
            }
        }
    }

    if (tasks.empty()) {
        std::vector<std::string> raw_paths = get_json_string_array(json, "paths");
        for (size_t i = 0; i < raw_paths.size(); ++i) {
            TaskItem item;
            item.id = "task_" + std::to_string(i);
            item.path = raw_paths[i];
            item.name = fs::u8path(raw_paths[i]).filename().u8string();
            tasks.push_back(item);
        }
    }
    return tasks;
}

// Batch state manager for safe background worker
struct BatchContext {
    std::vector<TaskItem> tasks;
    ImageFormat format{ImageFormat::JPG};
    int quality{90};
    size_t threads{4};
    fs::path out_dir;
    std::string out_ext;
    std::atomic<size_t> done_count{0};
    std::atomic<size_t> success_count{0};
    std::atomic<size_t> fail_count{0};
    std::vector<fs::path> converted_files;
    std::mutex files_mtx;
};

// Handle message received from Web frontend
static void handle_web_message(const std::wstring& msg) {
    try {
        std::string json = ws2s(msg);
        std::string type = get_json_field(json, "type");

        if (type == "select_folder") {
            std::wstring folder = open_folder_dialog(g_hWnd);
            if (!folder.empty()) {
                std::error_code ec;
                fs::path fpath(folder);
                std::vector<fs::path> scanned = ConverterEngine::scan_files(fpath, false);
                
                std::wstringstream js;
                js << L"window.onNativeFolderSelected(\"" << escape_js_string(folder) << L"\", [";
                for (size_t i = 0; i < scanned.size(); ++i) {
                    if (i > 0) js << L", ";
                    auto fsize = fs::file_size(scanned[i], ec);
                    js << L"{\"name\":\"" << escape_js_string(scanned[i].filename().wstring()) 
                       << L"\", \"path\":\"" << escape_js_string(scanned[i].wstring()) 
                       << L"\", \"size\":" << fsize << L"}";
                }
                js << L"]);";
                exec_js_safe(js.str());
            }
        }
        else if (type == "select_files") {
            std::vector<std::wstring> files = open_files_dialog(g_hWnd);
            if (!files.empty()) {
                std::wstringstream js;
                js << L"window.onNativeFilesSelected([";
                std::error_code ec;
                for (size_t i = 0; i < files.size(); ++i) {
                    if (i > 0) js << L", ";
                    fs::path p(files[i]);
                    auto fsize = fs::file_size(p, ec);
                    js << L"{\"name\":\"" << escape_js_string(p.filename().wstring()) 
                       << L"\", \"path\":\"" << escape_js_string(p.wstring()) 
                       << L"\", \"size\":" << fsize << L"}";
                }
                js << L"]);";
                exec_js_safe(js.str());
            }
        }
        else if (type == "select_path_input") {
            std::string input_path_str = get_json_field(json, "path");
            if (!input_path_str.empty()) {
                fs::path p = to_clean_path(input_path_str);
                if (!fs::exists(p)) {
                    try { p = fs::u8path(input_path_str); } catch (...) {}
                }
                if (fs::exists(p)) {
                    std::error_code ec;
                    if (fs::is_directory(p)) {
                        std::vector<fs::path> scanned = ConverterEngine::scan_files(p, false);
                        std::wstringstream js;
                        js << L"window.onNativeFolderSelected(\"" << escape_js_string(p.wstring()) << L"\", [";
                        for (size_t i = 0; i < scanned.size(); ++i) {
                            if (i > 0) js << L", ";
                            auto fsize = fs::file_size(scanned[i], ec);
                            js << L"{\"name\":\"" << escape_js_string(scanned[i].filename().wstring()) 
                               << L"\", \"path\":\"" << escape_js_string(scanned[i].wstring()) 
                               << L"\", \"size\":" << fsize << L"}";
                        }
                        js << L"]);";
                        exec_js_safe(js.str());
                    } else if (fs::is_regular_file(p)) {
                        auto fsize = fs::file_size(p, ec);
                        std::wstringstream js;
                        js << L"window.onNativeFilesSelected([";
                        js << L"{\"name\":\"" << escape_js_string(p.filename().wstring()) 
                           << L"\", \"path\":\"" << escape_js_string(p.wstring()) 
                           << L"\", \"size\":" << fsize << L"}";
                        js << L"]);";
                        exec_js_safe(js.str());
                    }
                }
            }
        }
        else if (type == "save_converted_files" || type == "open_output_folder") {
            std::vector<fs::path> files_to_save;
            std::wstring ext_to_use;
            {
                std::lock_guard<std::mutex> lock(g_files_mutex);
                files_to_save = g_last_converted_files;
                ext_to_use = g_last_out_ext.empty() ? L"jpg" : g_last_out_ext;
            }

            if (files_to_save.size() == 1) {
                fs::path single_src = files_to_save[0];
                std::wstring def_name = single_src.filename().wstring();
                std::wstring save_path = save_file_dialog(g_hWnd, def_name, ext_to_use);
                if (!save_path.empty()) {
                    std::error_code ec;
                    fs::copy_file(single_src, fs::path(save_path), fs::copy_options::overwrite_existing, ec);
                    if (!ec) {
                        std::wstringstream js;
                        js << L"if (app) app.showToast(\"已成功保存至: " << escape_js_string(save_path) << L"\");";
                        exec_js_safe(js.str());
                        ShellExecuteW(NULL, L"open", L"explorer.exe", (L"/select,\"" + save_path + L"\"").c_str(), NULL, SW_SHOWNORMAL);
                    } else {
                        exec_js_safe(L"if (app) app.showToast(\"保存失败，请检查写入权限\");");
                    }
                } else {
                    exec_js_safe(L"if (app) app.showToast(\"已取消保存\");");
                }
            }
            else if (!files_to_save.empty()) {
                std::wstring target_folder = save_folder_dialog(g_hWnd, L"选择保存图像的目标文件夹");
                if (!target_folder.empty()) {
                    std::error_code ec;
                    size_t copied = 0;
                    fs::path dst_dir(target_folder);
                    fs::create_directories(dst_dir, ec);

                    for (const auto& f : files_to_save) {
                        fs::path dst_file = dst_dir / f.filename();
                        fs::copy_file(f, dst_file, fs::copy_options::overwrite_existing, ec);
                        if (!ec) copied++;
                    }

                    std::wstringstream js;
                    js << L"if (app) app.showToast(\"已成功保存 " << copied << L" 个文件至目标文件夹\");";
                    exec_js_safe(js.str());
                    ShellExecuteW(NULL, L"open", target_folder.c_str(), NULL, NULL, SW_SHOWNORMAL);
                } else {
                    exec_js_safe(L"if (app) app.showToast(\"已取消保存\");");
                }
            }
            else if (!g_last_output_dir.empty()) {
                ShellExecuteW(NULL, L"open", s2ws(g_last_output_dir).c_str(), NULL, NULL, SW_SHOWNORMAL);
            }
        }
        else if (type == "window_drag") {
            ReleaseCapture();
            SendMessageW(g_hWnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        }
        else if (type == "window_minimize") {
            ShowWindow(g_hWnd, SW_MINIMIZE);
        }
        else if (type == "window_maximize") {
            if (IsZoomed(g_hWnd)) {
                ShowWindow(g_hWnd, SW_RESTORE);
            } else {
                ShowWindow(g_hWnd, SW_MAXIMIZE);
            }
        }
        else if (type == "window_close") {
            PostMessageW(g_hWnd, WM_CLOSE, 0, 0);
        }
        else if (type == "select_custom_output_folder") {
            std::wstring folder = open_folder_dialog(g_hWnd);
            if (!folder.empty()) {
                std::wstringstream js;
                js << L"if (app) app.setCustomOutputDir(\"" << escape_js_string(folder) << L"\");";
                exec_js_safe(js.str());
            }
        }
        else if (type == "start_convert") {
            auto ctx = std::make_shared<BatchContext>();
            ctx->tasks = get_json_tasks(json);
            
            std::string format_str = get_json_field(json, "format");
            std::string quality_str = get_json_field(json, "quality");
            std::string threads_str = get_json_field(json, "threads");
            std::string custom_out_dir = get_json_field(json, "output_dir");

            ctx->quality = quality_str.empty() ? 90 : std::stoi(quality_str);
            ctx->threads = threads_str.empty() ? 0 : std::stoul(threads_str);
            ctx->format = (format_str == "png") ? ImageFormat::PNG : ImageFormat::JPG;
            ctx->out_ext = (ctx->format == ImageFormat::JPG) ? ".jpg" : ".png";

            if (ctx->tasks.empty()) {
                exec_js_safe(L"window.onNativeBatchDone(0, 0, \"\");");
                return;
            }

            // Determine output directory
            if (!custom_out_dir.empty()) {
                ctx->out_dir = to_clean_path(custom_out_dir);
                if (!fs::exists(ctx->out_dir)) {
                    try { ctx->out_dir = fs::u8path(custom_out_dir); } catch (...) {}
                }
            } else {
                fs::path first_file = to_clean_path(ctx->tasks[0].path);
                if (!fs::exists(first_file)) {
                    try { first_file = fs::u8path(ctx->tasks[0].path); } catch (...) {}
                }
                if (first_file.has_parent_path() && !first_file.parent_path().empty()) {
                    ctx->out_dir = first_file.parent_path() / "converted_images";
                } else {
                    ctx->out_dir = fs::current_path() / "converted_images";
                }
            }
            g_last_output_dir = ws2s(ctx->out_dir.wstring());

            std::error_code ec;
            fs::create_directories(ctx->out_dir, ec);

            // Execute safely in dedicated background worker
            std::thread([ctx]() {
                try {
                    size_t num_threads = (ctx->threads > 0) ? ctx->threads : std::max(1u, std::thread::hardware_concurrency());
                    ThreadPool pool(num_threads);
                    size_t total = ctx->tasks.size();

                    for (const auto& item : ctx->tasks) {
                        pool.enqueue([ctx, item, total]() {
                            try {
                                fs::path src_p = to_clean_path(item.path);
                                if (!fs::exists(src_p)) {
                                    try { src_p = fs::u8path(item.path); } catch (...) {}
                                }
                                fs::path dst_p = ctx->out_dir / src_p.filename();
                                dst_p.replace_extension(ctx->out_ext);

                                std::string src_u8 = ws2s(src_p.wstring());
                                std::string dst_u8 = ws2s(dst_p.wstring());

                                bool ok = ConverterEngine::convert_single(src_u8, dst_u8, ctx->format, ctx->quality);
                                if (ok) {
                                    ctx->success_count++;
                                    std::lock_guard<std::mutex> lock(ctx->files_mtx);
                                    ctx->converted_files.push_back(dst_p);
                                } else {
                                    ctx->fail_count++;
                                }

                                size_t curr = ++ctx->done_count;

                                std::wstringstream js;
                                js << L"window.onNativeProgress(\""
                                   << escape_js_string(s2ws(item.id)) << L"\", "
                                   << (ok ? L"true" : L"false") << L", \"\", "
                                   << curr << L", "
                                   << total << L", \""
                                   << escape_js_string(src_p.filename().wstring()) << L"\");";
                                exec_js_safe(js.str());
                            } catch (...) {
                                ctx->fail_count++;
                                size_t curr = ++ctx->done_count;
                                std::wstringstream js;
                                js << L"window.onNativeProgress(\""
                                   << escape_js_string(s2ws(item.id)) << L"\", false, \"Exception\", "
                                   << curr << L", "
                                   << total << L", \""
                                   << escape_js_string(s2ws(item.name)) << L"\");";
                                exec_js_safe(js.str());
                            }
                        });
                    }

                    pool.wait_all();

                    {
                        std::lock_guard<std::mutex> lock(g_files_mutex);
                        g_last_converted_files = ctx->converted_files;
                        std::string clean_ext = ctx->out_ext;
                        if (!clean_ext.empty() && clean_ext[0] == '.') clean_ext = clean_ext.substr(1);
                        g_last_out_ext = s2ws(clean_ext);
                    }

                    std::wstringstream js_done;
                    js_done << L"window.onNativeBatchDone(" << ctx->success_count.load() << L", " 
                            << ctx->fail_count.load() << L", \"" << escape_js_string(ctx->out_dir.wstring()) << L"\");";
                    exec_js_safe(js_done.str());
                } catch (...) {
                    exec_js_safe(L"window.onNativeBatchDone(0, 0, \"\");");
                }
            }).detach();
        }
    } catch (...) {
        // Prevent any top-level unhandled exception
    }
}

// Window Procedure
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_DROPFILES: {
        HDROP hDrop = reinterpret_cast<HDROP>(wParam);
        UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
        std::vector<std::wstring> dropped_paths;
        for (UINT i = 0; i < fileCount; i++) {
            wchar_t filePath[MAX_PATH * 2] = {0};
            if (DragQueryFileW(hDrop, i, filePath, MAX_PATH * 2)) {
                dropped_paths.push_back(filePath);
            }
        }
        DragFinish(hDrop);
        handle_dropped_paths(dropped_paths);
        return 0;
    }

    case WM_SIZE:
        if (g_controller) {
            RECT bounds;
            GetClientRect(hWnd, &bounds);
            g_controller->put_Bounds(bounds);
        }
        if (g_webview) {
            bool maximized = (wParam == SIZE_MAXIMIZED) || IsZoomed(hWnd);
            std::wstringstream js;
            js << L"if (window.onWindowMaximizedChanged) window.onWindowMaximizedChanged(" << (maximized ? L"true" : L"false") << L");";
            exec_js_safe(js.str());
        }
        return 0;

    case WM_USER + 101: {
        std::wstring* script = reinterpret_cast<std::wstring*>(wParam);
        if (script) {
            if (g_webview) {
                g_webview->ExecuteScript(script->c_str(), nullptr);
            }
            delete script;
        }
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// WinMain Entry Point
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)pCmdLine;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    // Register Window Class
    const wchar_t CLASS_NAME[] = L"JP2ConverterDjVuWindowClass";
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClassExW(&wc);

    // Centered compact frameless window coordinates
    int width = 670;
    int height = 540;
    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);
    int x = (screen_w - width) / 2;
    int y = (screen_h - height) / 2;

    HWND hWnd = CreateWindowExW(
        WS_EX_ACCEPTFILES, // Enable native Win32 Drag & Drop
        CLASS_NAME,
        L"EzConv v.1.0.0",
        WS_POPUP | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_VISIBLE,
        x, y, width, height,
        NULL, NULL, hInstance, NULL
    );

    if (!hWnd) return 0;
    g_hWnd = hWnd;
    DragAcceptFiles(hWnd, TRUE);

    // Enable DWM Drop Shadow for Frameless Window
    MARGINS margins = { 1, 1, 1, 1 };
    DwmExtendFrameIntoClientArea(hWnd, &margins);

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    // Locate app/index.html
    wchar_t exePath[MAX_PATH * 2] = {0};
    GetModuleFileNameW(NULL, exePath, MAX_PATH * 2);
    fs::path current_dir = fs::path(exePath).parent_path();
    fs::path html_path = current_dir / "app" / "index.html";

    if (!fs::exists(html_path)) {
        html_path = current_dir / ".." / ".." / "app" / "index.html";
    }
    if (!fs::exists(html_path)) {
        html_path = current_dir / ".." / "app" / "index.html";
    }

    // Compute user data folder in %LOCALAPPDATA% to prevent polluting application directory
    wchar_t localAppData[MAX_PATH] = {0};
    fs::path user_data_path;
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) > 0) {
        user_data_path = fs::path(localAppData) / L"EzConv" / L"WebView2";
    } else {
        wchar_t tempPath[MAX_PATH] = {0};
        GetTempPathW(MAX_PATH, tempPath);
        user_data_path = fs::path(tempPath) / L"EzConv_WebView2";
    }

    // Initialize WebView2
    CreateCoreWebView2EnvironmentWithOptions(
        nullptr, user_data_path.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [hWnd, html_path](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(hr) || !env) {
                    MessageBoxW(hWnd, L"未检测到 WebView2 运行时。请确保 Windows 10/11 已启用 Edge WebView2。", L"启动错误", MB_ICONERROR);
                    return S_OK;
                }

                env->CreateCoreWebView2Controller(hWnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [hWnd, html_path](HRESULT hr, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(hr) || !controller) return S_OK;

                            g_controller = controller;
                            g_controller->get_CoreWebView2(&g_webview);

                            RECT bounds;
                            GetClientRect(hWnd, &bounds);
                            g_controller->put_Bounds(bounds);

                            ComPtr<ICoreWebView2Settings> settings;
                            g_webview->get_Settings(&settings);
                            if (settings) {
                                settings->put_IsScriptEnabled(TRUE);
                                settings->put_AreDefaultScriptDialogsEnabled(TRUE);
                                settings->put_IsWebMessageEnabled(TRUE);
                                settings->put_AreDevToolsEnabled(FALSE);
                                settings->put_IsStatusBarEnabled(FALSE);
                            }

                            // Register Message Handler from JavaScript
                            EventRegistrationToken token;
                            g_webview->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                        (void)sender;
                                        PWSTR message = nullptr;
                                        if (SUCCEEDED(args->TryGetWebMessageAsString(&message)) && message) {
                                            handle_web_message(message);
                                            CoTaskMemFree(message);
                                        }
                                        return S_OK;
                                    }).Get(), &token);

                            std::wstring url = L"file:///" + fs::canonical(html_path).wstring();
                            for (auto& ch : url) {
                                if (ch == L'\\') ch = L'/';
                            }
                            g_webview->Navigate(url.c_str());

                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());

    // Main Message Loop
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return (int)msg.wParam;
}
