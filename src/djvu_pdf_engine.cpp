#define _CRT_SECURE_NO_WARNINGS
#include "djvu_pdf_engine.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <windows.h>
#include <cstdio>
#include <memory>
#include <functional>

namespace fs = std::filesystem;

// ============================================================================
// 1. 外部工具探测与路径管理
// ============================================================================

static std::string g_cached_tools_dir = "";

static std::string get_executable_dir() {
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(NULL, buffer, MAX_PATH);
    fs::path p(buffer);
    return p.parent_path().u8string();
}

std::string DjVuPdfEngine::get_tools_dir() {
    if (!g_cached_tools_dir.empty() && fs::exists(fs::u8path(g_cached_tools_dir) / "ddjvu.exe")) {
        return g_cached_tools_dir;
    }

    fs::path exe_dir = fs::u8path(get_executable_dir());
    std::vector<fs::path> candidate_dirs = {
        exe_dir / "djvulibre" / "bin",
        exe_dir / "third_party" / "djvulibre" / "bin",
        exe_dir,
        exe_dir.parent_path() / "djvulibre" / "bin",
        exe_dir.parent_path().parent_path() / "djvulibre" / "bin",
        exe_dir.parent_path().parent_path().parent_path() / "third_party" / "djvulibre" / "bin",
        exe_dir.parent_path().parent_path().parent_path().parent_path() / "third_party" / "djvulibre" / "bin",
        fs::current_path() / "third_party" / "djvulibre" / "bin",
        fs::current_path() / "djvulibre" / "bin",
        fs::path("E:/2025/EzConv-main/third_party/djvulibre/bin"),
        fs::path(getenv("LOCALAPPDATA") ? getenv("LOCALAPPDATA") : "") / "EzConv" / "bin"
    };

    for (const auto& dir : candidate_dirs) {
        std::error_code ec;
        if (fs::exists(dir / "ddjvu.exe", ec) && fs::exists(dir / "djvused.exe", ec)) {
            g_cached_tools_dir = dir.u8string();
            return g_cached_tools_dir;
        }
    }

    return "";
}

bool DjVuPdfEngine::ensure_tools_ready() {
    std::string dir = get_tools_dir();
    return !dir.empty();
}

static std::string run_command_capture(const std::wstring& cmd, const std::string& working_dir = "") {
    std::string output;
    HANDLE hReadPipe, hWritePipe;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        return "";
    }
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    // Provide NUL device as standard input so tools never block waiting on stdin
    HANDLE hNullInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdInput = (hNullInput != INVALID_HANDLE_VALUE) ? hNullInput : NULL;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    ZeroMemory(&pi, sizeof(pi));

    std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back(L'\0');

    std::wstring w_working_dir;
    if (!working_dir.empty()) {
        int size = MultiByteToWideChar(CP_UTF8, 0, working_dir.data(), (int)working_dir.size(), nullptr, 0);
        w_working_dir.resize(size);
        MultiByteToWideChar(CP_UTF8, 0, working_dir.data(), (int)working_dir.size(), &w_working_dir[0], size);
    }

    if (CreateProcessW(NULL, cmd_buf.data(), NULL, NULL, TRUE, 
                       CREATE_NO_WINDOW, NULL, 
                       w_working_dir.empty() ? NULL : w_working_dir.c_str(), 
                       &si, &pi)) {
        CloseHandle(hWritePipe);
        if (hNullInput != INVALID_HANDLE_VALUE) {
            CloseHandle(hNullInput);
            hNullInput = INVALID_HANDLE_VALUE;
        }

        char buffer[4096];
        DWORD bytesRead;
        while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
            output.append(buffer, bytesRead);
        }

        // Wait up to 60 seconds to avoid indefinite hanging
        DWORD waitRes = WaitForSingleObject(pi.hProcess, 60000);
        if (waitRes == WAIT_TIMEOUT) {
            TerminateProcess(pi.hProcess, 1);
        }

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else {
        CloseHandle(hWritePipe);
        if (hNullInput != INVALID_HANDLE_VALUE) {
            CloseHandle(hNullInput);
        }
    }

    CloseHandle(hReadPipe);
    return output;
}

static std::wstring s2ws(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str.data(), (int)str.size(), nullptr, 0);
    if (size > 0) {
        std::wstring wstr(size, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), &wstr[0], size);
        return wstr;
    }
    // 降级尝试当前系统默认代码页 CP_ACP (解决 Windows 本地控制台/GBK 编码问题)
    size = MultiByteToWideChar(CP_ACP, 0, str.data(), (int)str.size(), nullptr, 0);
    if (size > 0) {
        std::wstring wstr(size, 0);
        MultiByteToWideChar(CP_ACP, 0, str.data(), (int)str.size(), &wstr[0], size);
        return wstr;
    }
    return L"";
}

// 安全路径代理工具：彻底解决 DjVuLibre 工具链在处理 Windows 中文/Unicode/空格路径时的 ByteStream.open_fail 致命缺陷
class SafeDjVuPathProxy {
public:
    std::wstring safe_path;
    std::wstring temp_to_delete;

    SafeDjVuPathProxy(const std::wstring& orig_path_in) {
        if (orig_path_in.empty()) return;

        // 关键：首先将输入路径转换为绝对规范路径，避免跨工作目录执行外部命令时无法定位文件
        std::error_code ec_abs;
        fs::path abs_p = fs::absolute(fs::path(orig_path_in), ec_abs);
        std::wstring orig_path = ec_abs ? orig_path_in : abs_p.wstring();

        // 1. 优先尝试获取 8.3 短路径 (Short Path)
        wchar_t short_buf[MAX_PATH * 2] = {0};
        DWORD res = GetShortPathNameW(orig_path.c_str(), short_buf, MAX_PATH * 2);

        bool is_pure_ascii = false;
        if (res > 0 && res < MAX_PATH * 2) {
            is_pure_ascii = true;
            for (int i = 0; short_buf[i] != 0; ++i) {
                if (static_cast<unsigned short>(short_buf[i]) > 127) {
                    is_pure_ascii = false;
                    break;
                }
            }
        }

        if (is_pure_ascii) {
            safe_path = short_buf;
            return;
        }

        // 2. 如果短路径依然含有中文或系统禁用了短路径，创建纯英文硬链接或临时拷贝
        wchar_t temp_dir[MAX_PATH] = {0};
        GetTempPathW(MAX_PATH, temp_dir);
        wchar_t short_temp[MAX_PATH] = {0};
        GetShortPathNameW(temp_dir, short_temp, MAX_PATH);

        std::wstring target_dir = (short_temp[0] != 0) ? short_temp : temp_dir;
        bool temp_is_ascii = true;
        for (int i = 0; target_dir[i] != 0; ++i) {
            if (static_cast<unsigned short>(target_dir[i]) > 127) {
                temp_is_ascii = false;
                break;
            }
        }
        if (!temp_is_ascii) {
            fs::path op(orig_path);
            if (op.has_root_name()) {
                target_dir = (op.root_name() / L"_ezconv_tmp").wstring();
                std::error_code ec;
                fs::create_directories(target_dir, ec);
            }
        }

        std::wstring proxy_file = target_dir;
        if (!proxy_file.empty() && proxy_file.back() != L'\\') proxy_file += L'\\';
        proxy_file += L"ez_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64()) + L".djvu";

        // 优先使用硬链接 (零空间占用，微秒级完成)
        if (CreateHardLinkW(proxy_file.c_str(), orig_path.c_str(), NULL)) {
            safe_path = proxy_file;
            temp_to_delete = proxy_file;
        } else {
            std::error_code ec;
            fs::copy_file(fs::path(orig_path), fs::path(proxy_file), fs::copy_options::overwrite_existing, ec);
            if (!ec) {
                safe_path = proxy_file;
                temp_to_delete = proxy_file;
            } else {
                safe_path = orig_path;
            }
        }
    }

    ~SafeDjVuPathProxy() {
        if (!temp_to_delete.empty()) {
            DeleteFileW(temp_to_delete.c_str());
        }
    }
};

static fs::path get_safe_temp_dir(const fs::path& base_hint) {
    wchar_t temp_dir[MAX_PATH] = {0};
    GetTempPathW(MAX_PATH, temp_dir);
    wchar_t short_temp[MAX_PATH] = {0};
    GetShortPathNameW(temp_dir, short_temp, MAX_PATH);

    std::wstring tdir = (short_temp[0] != 0) ? short_temp : temp_dir;
    bool is_ascii = true;
    for (int i = 0; tdir[i] != 0; ++i) {
        if (static_cast<unsigned short>(tdir[i]) > 127) {
            is_ascii = false;
            break;
        }
    }
    if (!is_ascii && base_hint.has_root_name()) {
        tdir = (base_hint.root_name() / L"_ezconv_tmp").wstring();
    }
    fs::path p(tdir);
    p /= ("ez_work_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(GetTickCount64()));
    std::error_code ec;
    fs::create_directories(p, ec);
    return p;
}

int DjVuPdfEngine::get_page_count(const std::string& djvu_path) {
    // 1. 优先采用纯原生 C++ DjVu IFF 结构解析 (零进程、零依赖、0毫秒瞬时独立识别)
    // 支持单页文档 (DJVU, PM44, BM44) 以及多页文档 (DJVM + DIRM)
    try {
        std::ifstream file(fs::u8path(djvu_path), std::ios::binary);
        if (file) {
            char magic[4] = {0};
            file.read(magic, 4);
            if (std::memcmp(magic, "AT&T", 4) == 0) {
                char form[4] = {0};
                file.read(form, 4);
                if (std::memcmp(form, "FORM", 4) == 0) {
                    uint32_t form_len = 0;
                    file.read(reinterpret_cast<char*>(&form_len), 4);
                    char doctype[4] = {0};
                    file.read(doctype, 4);
                    if (std::memcmp(doctype, "DJVU", 4) == 0 ||
                        std::memcmp(doctype, "PM44", 4) == 0 ||
                        std::memcmp(doctype, "BM44", 4) == 0) {
                        return 1;
                    }
                    if (std::memcmp(doctype, "DJVM", 4) == 0) {
                        char chunk_id[4] = {0};
                        file.read(chunk_id, 4);
                        if (std::memcmp(chunk_id, "DIRM", 4) == 0) {
                            uint32_t chunk_len = 0;
                            file.read(reinterpret_cast<char*>(&chunk_len), 4);
                            uint8_t flags = 0;
                            file.read(reinterpret_cast<char*>(&flags), 1);
                            uint8_t nfiles_b[2] = {0};
                            file.read(reinterpret_cast<char*>(nfiles_b), 2);
                            int nfiles = (static_cast<int>(nfiles_b[0]) << 8) | static_cast<int>(nfiles_b[1]);
                            if (nfiles > 0) {
                                return nfiles;
                            }
                        }
                    }
                }
            } else if (std::memcmp(magic, "FORM", 4) == 0) {
                // 部分单页 DjVu 直接以 FORM 开头
                uint32_t form_len = 0;
                file.read(reinterpret_cast<char*>(&form_len), 4);
                char doctype[4] = {0};
                file.read(doctype, 4);
                if (std::memcmp(doctype, "DJVU", 4) == 0 ||
                    std::memcmp(doctype, "PM44", 4) == 0 ||
                    std::memcmp(doctype, "BM44", 4) == 0) {
                    return 1;
                }
            }
        }
    } catch (...) {}

    // 2. 备用兜底：若有特殊打包格式，调用 djvused 工具探测
    std::string tools_dir = get_tools_dir();
    if (tools_dir.empty()) return 0;

    std::wstring djvused_exe = s2ws((fs::u8path(tools_dir) / "djvused.exe").u8string());
    std::wstring w_path = s2ws(djvu_path);

    SafeDjVuPathProxy proxy(w_path);
    std::wstring cmd = L"\"" + djvused_exe + L"\" \"" + proxy.safe_path + L"\" -e \"n\"";
    std::string res = run_command_capture(cmd, tools_dir);
    
    int pages = 0;
    std::stringstream ss(res);
    if (ss >> pages && pages > 0) {
        return pages;
    }
    return 0;
}

// ============================================================================
// 2. 书签大纲解析器 (Lisp S-expression)
// ============================================================================

struct BookmarkItem {
    std::string title;
    int page_index{0}; // 0-based
    std::vector<BookmarkItem> children;
};

static std::vector<BookmarkItem> parse_djvu_outline(const std::string& content) {
    std::vector<BookmarkItem> result;
    size_t pos = 0;
    size_t len = content.length();

    auto skip_space = [&]() {
        while (pos < len && std::isspace(static_cast<unsigned char>(content[pos]))) pos++;
    };

    auto read_string = [&]() -> std::string {
        skip_space();
        if (pos >= len || content[pos] != '"') return "";
        pos++;
        std::string s;
        while (pos < len && content[pos] != '"') {
            if (content[pos] == '\\' && pos + 1 < len) {
                pos++;
                s.push_back(content[pos]);
            } else {
                s.push_back(content[pos]);
            }
            pos++;
        }
        if (pos < len && content[pos] == '"') pos++;
        return s;
    };

    std::function<std::vector<BookmarkItem>()> parse_list = [&]() -> std::vector<BookmarkItem> {
        std::vector<BookmarkItem> items;
        while (pos < len) {
            skip_space();
            if (pos >= len || content[pos] == ')') break;
            if (content[pos] == '(') {
                pos++;
                skip_space();
                // 检查是否是 bookmarks 开头
                if (pos + 9 <= len && content.compare(pos, 9, "bookmarks") == 0) {
                    pos += 9;
                    auto sub = parse_list();
                    items.insert(items.end(), sub.begin(), sub.end());
                    skip_space();
                    if (pos < len && content[pos] == ')') pos++;
                    continue;
                }

                std::string title = read_string();
                std::string page_dest = read_string();
                int page_num = 1;
                if (!page_dest.empty()) {
                    if (page_dest[0] == '#') page_dest = page_dest.substr(1);
                    try { page_num = std::stoi(page_dest); } catch (...) { page_num = 1; }
                }

                BookmarkItem item;
                item.title = title;
                item.page_index = page_num - 1;

                // 子书签
                auto children = parse_list();
                item.children = children;

                skip_space();
                if (pos < len && content[pos] == ')') pos++;
                items.push_back(item);
            } else {
                pos++;
            }
        }
        return items;
    };

    result = parse_list();
    return result;
}

// ============================================================================
// 3. 转换流水线与执行入口 (直通 DjVuLibre 原生高压缩引擎 + 实时流式进度解析)
// ============================================================================

static bool run_command_stream(const std::wstring& cmd, 
                               const std::string& working_dir,
                               std::function<void(const std::string& line)> on_line) {
    HANDLE hReadPipe, hWritePipe;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        return false;
    }
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    HANDLE hNullInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdInput = (hNullInput != INVALID_HANDLE_VALUE) ? hNullInput : NULL;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    ZeroMemory(&pi, sizeof(pi));

    std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back(L'\0');

    std::wstring w_working_dir;
    if (!working_dir.empty()) {
        int size = MultiByteToWideChar(CP_UTF8, 0, working_dir.data(), (int)working_dir.size(), nullptr, 0);
        w_working_dir.resize(size);
        MultiByteToWideChar(CP_UTF8, 0, working_dir.data(), (int)working_dir.size(), &w_working_dir[0], size);
    }

    bool success = false;
    if (CreateProcessW(NULL, cmd_buf.data(), NULL, NULL, TRUE, 
                       CREATE_NO_WINDOW, NULL, 
                       w_working_dir.empty() ? NULL : w_working_dir.c_str(), 
                       &si, &pi)) {
        CloseHandle(hWritePipe);
        if (hNullInput != INVALID_HANDLE_VALUE) {
            CloseHandle(hNullInput);
            hNullInput = INVALID_HANDLE_VALUE;
        }

        char buffer[1024];
        DWORD bytesRead;
        std::string current_line;

        while (ReadFile(hReadPipe, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0) {
            for (DWORD i = 0; i < bytesRead; ++i) {
                char ch = buffer[i];
                if (ch == '\n' || ch == '\r') {
                    if (!current_line.empty()) {
                        if (on_line) on_line(current_line);
                        current_line.clear();
                    }
                } else {
                    current_line.push_back(ch);
                }
            }
        }
        if (!current_line.empty() && on_line) {
            on_line(current_line);
        }

        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exit_code = 0;
        if (GetExitCodeProcess(pi.hProcess, &exit_code)) {
            success = (exit_code == 0);
        }

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else {
        CloseHandle(hWritePipe);
        if (hNullInput != INVALID_HANDLE_VALUE) {
            CloseHandle(hNullInput);
        }
    }

    CloseHandle(hReadPipe);
    return success;
}

static bool inject_pdf_outlines(const fs::path& pdf_file, const std::vector<BookmarkItem>& bookmarks) {
    if (bookmarks.empty()) return true;

    std::ifstream in(pdf_file, std::ios::binary);
    if (!in) return false;

    std::string pdf_data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    // 提取 /Kids [ ... ]
    size_t kids_pos = pdf_data.find("/Kids");
    if (kids_pos == std::string::npos) return false;
    size_t bracket_open = pdf_data.find('[', kids_pos);
    size_t bracket_close = pdf_data.find(']', bracket_open);
    if (bracket_open == std::string::npos || bracket_close == std::string::npos) return false;

    std::string kids_str = pdf_data.substr(bracket_open + 1, bracket_close - bracket_open - 1);
    std::vector<int> page_obj_ids;
    std::stringstream ss(kids_str);
    int p_id = 0;
    std::string r_str;
    while (ss >> p_id >> r_str) {
        if (r_str == "0" || r_str == "R") {
            if (ss.peek() == 'R') {
                std::string dummy; ss >> dummy;
            }
            page_obj_ids.push_back(p_id);
        }
    }

    if (page_obj_ids.empty()) return false;

    // 提取当前最大对象 ID
    size_t trailer_pos = pdf_data.rfind("trailer");
    if (trailer_pos == std::string::npos) return false;
    size_t size_pos = pdf_data.find("/Size", trailer_pos);
    if (size_pos == std::string::npos) return false;
    int max_id = 0;
    std::stringstream ss_sz(pdf_data.substr(size_pos + 5));
    ss_sz >> max_id;
    if (max_id <= 0) return false;

    struct FlatNode {
        int obj_id;
        int parent_id;
        int prev_id;
        int next_id;
        std::string title;
        int page_idx;
        std::vector<BookmarkItem> children;
    };

    std::vector<FlatNode> flat;
    int outlines_root_id = max_id++;

    std::function<void(const std::vector<BookmarkItem>&, int)> flatten = 
        [&](const std::vector<BookmarkItem>& list, int parent_id) {
        int prev = 0;
        for (const auto& item : list) {
            int my_id = max_id++;
            FlatNode fn;
            fn.obj_id = my_id;
            fn.parent_id = parent_id;
            fn.prev_id = prev;
            fn.next_id = 0;
            fn.title = item.title;
            fn.page_idx = item.page_index;
            fn.children = item.children;

            if (!flat.empty() && prev != 0) {
                for (auto& n : flat) {
                    if (n.obj_id == prev) {
                        n.next_id = my_id;
                        break;
                    }
                }
            }
            prev = my_id;
            flat.push_back(fn);

            if (!item.children.empty()) {
                flatten(item.children, my_id);
            }
        }
    };

    flatten(bookmarks, outlines_root_id);

    std::vector<std::pair<int, size_t>> new_offsets;
    std::stringstream out;
    out << pdf_data;

    auto append_obj = [&](int id, const std::string& body) {
        size_t off = static_cast<size_t>(out.tellp());
        new_offsets.push_back({id, off});
        out << id << " 0 obj\n" << body << "endobj\n";
    };

    // 写入各 Outline 项
    for (const auto& node : flat) {
        int target_page_obj = (node.page_idx >= 0 && node.page_idx < (int)page_obj_ids.size()) 
                              ? page_obj_ids[node.page_idx] : page_obj_ids[0];
        std::stringstream b;
        b << "<< /Title (";
        for (char c : node.title) {
            if (c == '(' || c == ')' || c == '\\') b << '\\';
            b << c;
        }
        b << ")\n"
          << "   /Parent " << node.parent_id << " 0 R\n";
        if (node.prev_id > 0) b << "   /Prev " << node.prev_id << " 0 R\n";
        if (node.next_id > 0) b << "   /Next " << node.next_id << " 0 R\n";
        b << "   /Dest [" << target_page_obj << " 0 R /XYZ 0 null null]\n"
          << ">>\n";
        append_obj(node.obj_id, b.str());
    }

    // 写入 Outlines 根对象
    int first_id = flat.empty() ? 0 : flat[0].obj_id;
    int last_id = 0;
    for (const auto& n : flat) {
        if (n.parent_id == outlines_root_id) last_id = n.obj_id;
    }
    std::stringstream ob;
    ob << "<< /Type /Outlines\n";
    if (first_id > 0) ob << "   /First " << first_id << " 0 R\n";
    if (last_id > 0) ob << "   /Last " << last_id << " 0 R\n";
    ob << "   /Count " << flat.size() << "\n>>\n";
    append_obj(outlines_root_id, ob.str());

    // 写入新的 Catalog 对象
    int new_catalog_id = max_id++;
    std::stringstream cb;
    cb << "<< /Type /Catalog /Pages 3 0 R /Outlines " << outlines_root_id << " 0 R /PageMode /UseOutlines >>\n";
    append_obj(new_catalog_id, cb.str());

    // 写入增量交叉引用表
    size_t xref_offset = static_cast<size_t>(out.tellp());
    out << "xref\n"
        << outlines_root_id << " " << (max_id - outlines_root_id) << "\n";
    for (const auto& pair : new_offsets) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%010zu 00000 n \n", pair.second);
        out << buf;
    }

    out << "trailer\n"
        << "<< /Size " << max_id << "\n"
        << "   /Root " << new_catalog_id << " 0 R\n"
        << ">>\n"
        << "startxref\n"
        << xref_offset << "\n"
        << "%%EOF\n";

    std::ofstream fout(pdf_file, std::ios::binary);
    if (!fout) return false;
    std::string final_data = out.str();
    fout.write(final_data.data(), final_data.size());
    return true;
}

bool DjVuPdfEngine::convert_djvu_to_pdf(const std::string& djvu_path,
                                        const std::string& pdf_path,
                                        const DjVuConvertOptions& options,
                                        DjVuProgressCallback progress_cb) {
    std::string tools_dir = get_tools_dir();
    if (tools_dir.empty()) {
        std::cerr << "[DjVuEngine] Error: djvulibre tools not found!" << std::endl;
        if (progress_cb) {
            DjVuConvertProgress p;
            p.phase = "错误：未找到 DjVu 核心解码组件 (ddjvu.exe)";
            progress_cb(p);
        }
        return false;
    }

    // 建立安全路径代理，防止中文/空格导致的解析失败
    SafeDjVuPathProxy proxy(s2ws(djvu_path));
    const std::wstring& djvu_cmd_path = proxy.safe_path;

    int total_pages = get_page_count(djvu_path);
    if (total_pages <= 0) {
        std::cerr << "[DjVuEngine] Error: invalid djvu page count!" << std::endl;
        if (progress_cb) {
            DjVuConvertProgress p;
            p.phase = "错误：无法读取 DjVu 文件，可能已损坏或被加密";
            progress_cb(p);
        }
        return false;
    }

    std::error_code ec_sz;
    int64_t src_size = fs::file_size(fs::u8path(djvu_path), ec_sz);

    auto report = [&](int page, const std::string& phase, double percent) {
        if (progress_cb) {
            DjVuConvertProgress p;
            p.current_page = page;
            p.total_pages = total_pages;
            p.phase = phase;
            p.percent = percent;
            p.source_size_bytes = src_size;
            p.output_size_bytes = 0;
            progress_cb(p);
        }
    };

    report(0, "正在启动原生高性能 DjVu->PDF 转换引擎...", 0.0);

    // 1. 提取书签大纲 (若存在)
    std::vector<BookmarkItem> bookmarks;
    if (options.keep_bookmarks) {
        std::wstring djvused_exe = s2ws((fs::u8path(tools_dir) / "djvused.exe").u8string());
        std::wstring cmd = L"\"" + djvused_exe + L"\" \"" + djvu_cmd_path + L"\" -u -e \"print-outline\"";
        std::string out = run_command_capture(cmd, tools_dir);
        if (!out.empty()) {
            bookmarks = parse_djvu_outline(out);
        }
    }

    // 准备纯英文安全临时工作缓存路径
    fs::path temp_dir = get_safe_temp_dir(fs::u8path(pdf_path));
    fs::path temp_pdf = temp_dir / "converted_out.pdf";

    std::wstring ddjvu_exe = s2ws((fs::u8path(tools_dir) / "ddjvu.exe").u8string());

    // 组装直通原生 ddjvu 转换指令 (采用与 DJView 完全一致的 CCITT Group 4 Fax + JPEG 85 组合)
    int quality = (options.bg_quality > 0) ? options.bg_quality : 85;
    std::wstring cmd = L"\"" + ddjvu_exe + L"\" -format=pdf -quality=" + std::to_wstring(quality) + L" -verbose";
    if (options.mode == DjVuConvertMode::Bitonal) {
        cmd += L" -mode=black";
    }
    cmd += L" \"" + djvu_cmd_path + L"\" \"" + temp_pdf.wstring() + L"\"";

    bool success = run_command_stream(cmd, tools_dir, [&](const std::string& line) {
        // 解析行如: -------- page 12 -------
        size_t p_pos = line.find("-------- page ");
        if (p_pos != std::string::npos) {
            int p_num = 0;
            try {
                p_num = std::stoi(line.substr(p_pos + 14));
            } catch (...) {}
            if (p_num > 0) {
                double pct = (p_num * 95.0) / total_pages;
                report(p_num, "正在转换第 " + std::to_string(p_num) + "/" + std::to_string(total_pages) + " 页", pct);
            }
        } else if (line.find("Converting temporary TIFF to PDF") != std::string::npos) {
            report(total_pages, "正在生成高保真紧凑 PDF 文档...", 98.0);
        }
    });

    if (success && fs::exists(temp_pdf) && fs::file_size(temp_pdf) > 0) {
        // 如果有书签，注入大纲目录
        if (!bookmarks.empty()) {
            inject_pdf_outlines(temp_pdf, bookmarks);
        }

        // 安全拷贝至目标文件路径
        fs::path target_pdf = fs::u8path(pdf_path);
        if (target_pdf.has_parent_path()) {
            std::error_code ec;
            fs::create_directories(target_pdf.parent_path(), ec);
        }

        std::error_code ec_copy;
        fs::copy_file(temp_pdf, target_pdf, fs::copy_options::overwrite_existing, ec_copy);
        success = !ec_copy;
    } else {
        success = false;
    }

    // 清理临时工作目录
    std::error_code ec_clean;
    fs::remove_all(temp_dir, ec_clean);

    if (success) {
        std::error_code ec_dst;
        int64_t dst_size = fs::file_size(fs::u8path(pdf_path), ec_dst);
        if (progress_cb) {
            DjVuConvertProgress p;
            p.current_page = total_pages;
            p.total_pages = total_pages;
            p.phase = "转换完成";
            p.percent = 100.0;
            p.source_size_bytes = src_size;
            p.output_size_bytes = dst_size;
            progress_cb(p);
        }
    } else {
        if (progress_cb) {
            DjVuConvertProgress p;
            p.phase = "转换失败";
            progress_cb(p);
        }
    }

    return success;
}
