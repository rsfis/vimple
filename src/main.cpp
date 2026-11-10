// vimple - editor de código modal (estilo vim) para terminal, tema gruvbox-dark.
// Toda a interação "de programa" é por UI (menus, diálogos, botões, mouse).
// Dentro do arquivo a edição é modal: hjkl, i, v, dd, yy, p, u ...
// ---- curses: ncurses (Linux/macOS) ou PDCurses/PDCursesMod (Windows)
#ifdef _WIN32
#ifndef _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#endif
#ifndef PDC_NCMOUSE
#define PDC_NCMOUSE  // getmouse/MEVENT no estilo ncurses
#endif
#endif
#if __has_include(<ncurses/ncurses.h>)
#include <ncurses/ncurses.h>
#elif __has_include(<ncursesw/ncurses.h>)
#include <ncursesw/ncurses.h>
#elif __has_include(<ncurses.h>)
#include <ncurses.h>
#else
#include <curses.h>
#endif
#include <locale.h>
#include <csignal>
#include <cstdint>
#ifdef _WIN32
// Windows: sem fork/pipe/waitpid. O Run sempre usa o "modo terminal completo" (system()).
#include <direct.h>
#include <stdio.h>
#ifdef _MSC_VER
typedef int pid_t;
#endif
#define SEP "\\"
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#define SEP "/"
#endif

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;
using std::string;
using std::vector;

// ============================================================ plataforma: caminhos UTF-8 e API do Windows
// Internamente todo texto/caminho é UTF-8. No Windows, std::filesystem/fstream/system() usam a codepage ANSI
// quando recebem std::string, o que quebra acentos; por isso TODO acesso a disco passa por toPath()/pstr().
#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#endif
#ifdef _WIN32
// Declarações mínimas (evita incluir <windows.h>, que conflita com macros do PDCurses como MOUSE_MOVED).
struct W32_SA { unsigned long n; void* sd; int inherit; };
struct W32_SI { unsigned long cb; wchar_t* res; wchar_t* desk; wchar_t* title; unsigned long x, y, xs, ys, xc, yc, fill, flags;
    unsigned short show, cbres2; unsigned char* res2; void* in; void* out; void* err; };
struct W32_PI { void* proc; void* thr; unsigned long pid, tid; };
extern "C" {
__declspec(dllimport) unsigned long __stdcall GetLogicalDrives(void);
__declspec(dllimport) unsigned int __stdcall GetDriveTypeA(const char*);
__declspec(dllimport) int __stdcall MultiByteToWideChar(unsigned int, unsigned long, const char*, int, wchar_t*, int);
__declspec(dllimport) int __stdcall WideCharToMultiByte(unsigned int, unsigned long, const wchar_t*, int, char*, int, const char*, int*);
__declspec(dllimport) unsigned int __stdcall GetConsoleCP(void);
__declspec(dllimport) unsigned int __stdcall GetConsoleOutputCP(void);
__declspec(dllimport) unsigned int __stdcall GetOEMCP(void);
__declspec(dllimport) int __stdcall SetConsoleCP(unsigned int);
__declspec(dllimport) int __stdcall SetConsoleOutputCP(unsigned int);
__declspec(dllimport) int __stdcall CreatePipe(void**, void**, W32_SA*, unsigned long);
__declspec(dllimport) int __stdcall SetHandleInformation(void*, unsigned long, unsigned long);
__declspec(dllimport) void* __stdcall CreateFileW(const wchar_t*, unsigned long, unsigned long, W32_SA*, unsigned long, unsigned long, void*);
__declspec(dllimport) int __stdcall CreateProcessW(const wchar_t*, wchar_t*, W32_SA*, W32_SA*, int, unsigned long, void*, const wchar_t*, W32_SI*, W32_PI*);
__declspec(dllimport) int __stdcall PeekNamedPipe(void*, void*, unsigned long, unsigned long*, unsigned long*, unsigned long*);
__declspec(dllimport) int __stdcall ReadFile(void*, void*, unsigned long, unsigned long*, void*);
__declspec(dllimport) int __stdcall GetExitCodeProcess(void*, unsigned long*);
__declspec(dllimport) int __stdcall CloseHandle(void*);
__declspec(dllimport) void* __stdcall GetStdHandle(unsigned long);
__declspec(dllimport) int __stdcall GetConsoleMode(void*, unsigned long*);
__declspec(dllimport) int __stdcall SetConsoleMode(void*, unsigned long);
__declspec(dllimport) int __stdcall TerminateProcess(void*, unsigned int);
__declspec(dllimport) void* __stdcall GlobalAlloc(unsigned int, size_t);
__declspec(dllimport) void* __stdcall GlobalLock(void*);
__declspec(dllimport) int __stdcall GlobalUnlock(void*);
__declspec(dllimport) int __stdcall OpenClipboard(void*);
__declspec(dllimport) int __stdcall CloseClipboard(void);
__declspec(dllimport) int __stdcall EmptyClipboard(void);
__declspec(dllimport) void* __stdcall GetClipboardData(unsigned int);
__declspec(dllimport) void* __stdcall SetClipboardData(unsigned int, void*);
}
static std::wstring u8w(const string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(65001, 0, s.data(), (int)s.size(), nullptr, 0); if (n <= 0) return std::wstring();
    std::wstring w((size_t)n, L'\0'); MultiByteToWideChar(65001, 0, s.data(), (int)s.size(), &w[0], n); return w;
}
static string wu8(const wchar_t* w, int len = -1) {
    if (!w) return string(); if (len < 0) len = (int)wcslen(w); if (len == 0) return string();
    int n = WideCharToMultiByte(65001, 0, w, len, nullptr, 0, nullptr, nullptr); if (n <= 0) return string();
    string s((size_t)n, '\0'); WideCharToMultiByte(65001, 0, w, len, &s[0], n, nullptr, nullptr); return s;
}
static string cpToU8(const string& s, unsigned cp) {  // bytes em uma codepage (0 = ANSI, 1 = OEM, n) -> UTF-8
    if (s.empty()) return s;
    int n = MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), nullptr, 0); if (n <= 0) return s;
    std::wstring w((size_t)n, L'\0'); MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), &w[0], n); return wu8(w.data(), n);
}
static bool validUtf8(const string& s) {
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i]; int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (n == 0 || i + n > s.size()) return false;
        for (int k = 1; k < n; k++) if ((((unsigned char)s[i + k]) & 0xC0) != 0x80) return false;
        i += n;
    }
    return true;
}
#endif
static fs::path toPath(const string& s) {
#ifdef _WIN32
    return fs::u8path(s);
#else
    return fs::path(s);
#endif
}
static string pstr(const fs::path& p) {
#ifdef _WIN32
    auto u = p.u8string(); return string(u.begin(), u.end());
#else
    return p.string();
#endif
}
// Chave de comparação de caminhos (Windows: sem distinção de maiúsculas e '/' == '\\').
static string pathKey(string s) {
#ifdef _WIN32
    for (auto& c : s) { if (c == '/') c = '\\'; else if ((unsigned char)c < 128) c = (char)tolower((unsigned char)c); }
    while (s.size() > 3 && s.back() == '\\') s.pop_back();
#endif
    return s;
}
static bool samePath(const string& a, const string& b) { return pathKey(a) == pathKey(b); }
static string trimSep(string s) {  // remove separador final (menos em "/" e "C:\")
    while (s.size() > 1 && (s.back() == '/' || s.back() == '\\') && !(s.size() == 3 && s[1] == ':')) s.pop_back();
    return s;
}
static string upperA(string s) { for (auto& c : s) if ((unsigned char)c < 128) c = (char)toupper((unsigned char)c); return s; }
static string lowerA(string s) { for (auto& c : s) if ((unsigned char)c < 128) c = (char)tolower((unsigned char)c); return s; }
// Lista um diretório sem lançar exceção (acesso negado, entradas quebradas etc.).
static vector<fs::directory_entry> listDir(const fs::path& dir) {
    vector<fs::directory_entry> es; std::error_code ec;
    fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
    while (!ec && it != end) { es.push_back(*it); it.increment(ec); }
    return es;
}

// ============================================================ utilidades
static vector<string> splitWords(const char* s) {
    vector<string> v; std::istringstream is(s); string w;
    while (is >> w) v.push_back(w);
    return v;
}
static vector<string> splitLines(const string& s) {
    vector<string> v; string cur;
    for (char c : s) { if (c == '\n') { v.push_back(cur); cur.clear(); } else cur += c; }
    v.push_back(cur);
    return v;
}
static bool startsWith(const string& s, const string& p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }
static int cplen(unsigned char c) {
    if (c < 0x80) return 1; if ((c >> 5) == 6) return 2; if ((c >> 4) == 14) return 3; if ((c >> 3) == 30) return 4; return 1;
}
static int ulen(const string& s) { int n = 0; for (size_t i = 0; i < s.size(); i += cplen((unsigned char)s[i])) n++; return n; }
static string clip(const string& s, int cols) {
    if (cols <= 0) return ""; size_t i = 0; int n = 0;
    while (i < s.size() && n < cols) { i += cplen((unsigned char)s[i]); n++; }
    return s.substr(0, std::min(i, s.size()));
}
static string tailClip(const string& s, int cols) {  // últimas `cols` colunas
    int total = ulen(s); if (total <= cols) return s;
    size_t i = 0; int skip = total - cols;
    while (skip-- > 0 && i < s.size()) i += cplen((unsigned char)s[i]);
    return s.substr(std::min(i, s.size()));
}
static int nextCp(const string& l, int i) { if (i >= (int)l.size()) return (int)l.size(); return std::min<int>(i + cplen((unsigned char)l[i]), (int)l.size()); }
static int prevCp(const string& l, int i) { if (i <= 0) return 0; i--; while (i > 0 && (((unsigned char)l[i]) & 0xC0) == 0x80) i--; return i; }
static int vcolOf(const string& l, int cx) {
    int vc = 0;
    for (int i = 0; i < cx && i < (int)l.size();) { vc += l[i] == '\t' ? 4 - (vc % 4) : 1; i = nextCp(l, i); }
    return vc;
}
static int cxFromV(const string& l, int v) {
    int vc = 0, i = 0;
    while (i < (int)l.size()) { int w = l[i] == '\t' ? 4 - (vc % 4) : 1; if (vc + w > v) break; vc += w; i = nextCp(l, i); }
    return i;
}
static int firstNonBlank(const string& l) { size_t k = l.find_first_not_of(" \t"); return k == string::npos ? (int)(l.empty() ? 0 : l.size() - 1) : (int)k; }
static string homeDir() {
#ifdef _WIN32
    if (const wchar_t* h = _wgetenv(L"USERPROFILE")) if (*h) return wu8(h);
    const wchar_t* d = _wgetenv(L"HOMEDRIVE"); const wchar_t* pth = _wgetenv(L"HOMEPATH");
    if (d && pth) return wu8((std::wstring(d) + pth).c_str());
    return "C:\\";
#else
    const char* h = getenv("HOME");
    return h ? h : SEP;
#endif
}

// ============================================================ tema gruvbox-dark
enum { K_BG0, K_BG1, K_BG2, K_BG3, K_FG, K_RED, K_GRN, K_YEL, K_BLU, K_PUR, K_AQU, K_ORG, K_GRY, K_LGR, K_BTN, K_DRD, K_LOR, K_N };
static const int RGB[K_N][3] = {{40, 40, 40}, {60, 56, 54}, {80, 73, 69}, {102, 92, 84}, {235, 219, 178}, {251, 73, 52}, {184, 187, 38}, {250, 189, 47},
                                {131, 165, 152}, {211, 134, 155}, {142, 192, 124}, {254, 128, 25}, {146, 131, 116}, {168, 153, 134}, {69, 133, 136}, {204, 36, 29}, {255, 170, 90}};
static const int FB256[K_N] = {235, 237, 239, 241, 223, 167, 142, 214, 109, 175, 108, 208, 245, 246, 66, 124, 215};
static int C[K_N];
// Pares de cor: os ids lógicos (P_*) são mapeados para pares compactos (PM). Algumas builds do PDCurses aceitam poucos pares
// (ou codificam o número do par em poucos bits); pares iguais são reutilizados e, se faltar espaço, o que for cosmético cai num par parecido.
static int PM[128];
static int diagPairFail = 0, diagPairLim = 0, diagPairUsed = 0;
#define CP(n) COLOR_PAIR(PM[(n)])
enum { P_TEXT = 1, P_BAR, P_SEL, P_LINENO, P_LINENO_CUR, P_BTN, P_BTN_STOP, P_MENU, P_MENU_ACT, P_TAB, P_TAB_ACT, P_MODE_N, P_MODE_I, P_MODE_V, P_MODE_T,
       P_POP, P_POPSEL, P_DIR, P_ERR, P_WARN, P_NOTE, P_OK, P_GRAY, P_SIDE_SEL, P_SIDE_SELF, P_DLG, P_DLGB, P_DLGT, P_INP, P_INPF, P_BTNX,
       P_HINT, P_TITLE, P_SEP, P_HINTBAR, P_TREEHDR, P_SYN = 40 };
enum { S_TEXT, S_KW, S_TYPE, S_STR, S_NUM, S_COM, S_PRE, S_FN, S_OP };

// Console de 16 cores (PDCurses clássico / conhost): usa os 16 slots da paleta (redefinidos se possível).
static bool pal16 = false, palSaved = false; static short savedPal[16][3];
static int nearest16(int k) {
    static const struct { int c, r, g, b; } STD[16] = {
                  {COLOR_BLACK, 0, 0, 0}, {COLOR_RED, 170, 0, 0}, {COLOR_GREEN, 0, 170, 0}, {COLOR_YELLOW, 170, 85, 0}, {COLOR_BLUE, 0, 0, 170}, {COLOR_MAGENTA, 170, 0, 170}, {COLOR_CYAN, 0, 170, 170}, {COLOR_WHITE, 170, 170, 170},
                  {COLOR_BLACK + 8, 85, 85, 85}, {COLOR_RED + 8, 255, 85, 85}, {COLOR_GREEN + 8, 85, 255, 85}, {COLOR_YELLOW + 8, 255, 255, 85}, {COLOR_BLUE + 8, 85, 85, 255}, {COLOR_MAGENTA + 8, 255, 85, 255}, {COLOR_CYAN + 8, 85, 255, 255}, {COLOR_WHITE + 8, 255, 255, 255}};
    int best = 0, bd = 1 << 30;
    for (int i = 0; i < 16; i++) { int dr = STD[i].r - RGB[k][0], dg = STD[i].g - RGB[k][1], db = STD[i].b - RGB[k][2]; int d = dr * dr + dg * dg + db * db; if (d < bd) { bd = d; best = i; } }
    return STD[best].c;
}
static void restoreTheme() {
    if (pal16 && palSaved) for (int i = 0; i < 16; i++) init_color((short)i, savedPal[i][0], savedPal[i][1], savedPal[i][2]);
    palSaved = false;
}
static void initTheme() {
    start_color();
    bool tc = getenv("RSFVIM_TRUECOLOR") && can_change_color() && COLORS >= 256;
    if (COLORS < 256) {
        pal16 = true; bool can = can_change_color();
        for (int i = 0; i < K_N; i++) {
            if (can && i < 16) {
                color_content((short)i, &savedPal[i][0], &savedPal[i][1], &savedPal[i][2]);
                init_color((short)i, (short)(RGB[i][0] * 1000 / 255), (short)(RGB[i][1] * 1000 / 255), (short)(RGB[i][2] * 1000 / 255)); C[i] = i;
            } else if (can) C[i] = C[K_ORG];
            else C[i] = nearest16(i);
        }
        palSaved = can;
    } else for (int i = 0; i < K_N; i++) {
            if (tc) { init_color(16 + i, RGB[i][0] * 1000 / 255, RGB[i][1] * 1000 / 255, RGB[i][2] * 1000 / 255); C[i] = 16 + i; }
            else C[i] = FB256[i];
        }
    int lim = COLOR_PAIRS > 0 ? COLOR_PAIRS : 64; if (lim > 128) lim = 128;
    for (int n = 1; n < lim; n++) if (PAIR_NUMBER(COLOR_PAIR(n)) != n) { lim = n; break; }  // número do par não cabe no atributo
    diagPairLim = lim;
    int nextId = 1; std::map<std::pair<int, int>, int> seen; diagPairFail = 0;
    auto P = [&](int id, int f, int b) {
        auto k = std::make_pair(C[f], C[b]); auto it = seen.find(k);
        if (it != seen.end()) { PM[id] = it->second; return; }
        if (nextId < lim && init_pair((short)nextId, (short)C[f], (short)C[b]) == OK) { seen[k] = nextId; PM[id] = nextId++; }
        else { PM[id] = 0; diagPairFail++; }
    };
    int syn[9] = {K_FG, K_RED, K_YEL, K_GRN, K_PUR, K_GRY, K_AQU, K_BLU, K_LOR};
    P(P_TEXT, K_FG, K_BG0);
    for (int i = 0; i < 9; i++) P(P_SYN + i, syn[i], K_BG0);  // realce de sintaxe tem prioridade
    P(P_BAR, K_FG, K_BG1); P(P_SEL, K_FG, K_BG3); P(P_LINENO, K_GRY, K_BG0); P(P_LINENO_CUR, K_YEL, K_BG1);
    P(P_BTN, K_FG, K_BTN); P(P_BTN_STOP, K_FG, K_DRD); P(P_MENU, K_FG, K_BG1); P(P_MENU_ACT, K_BG0, K_LGR);
    P(P_TAB, K_LGR, K_BG2); P(P_TAB_ACT, K_YEL, K_BG0);
    P(P_MODE_N, K_BG0, K_LGR); P(P_MODE_I, K_BG0, K_BLU); P(P_MODE_V, K_BG0, K_ORG); P(P_MODE_T, K_BG0, K_GRN);
    P(P_POP, K_FG, K_BG2); P(P_POPSEL, K_BG0, K_BLU); P(P_DIR, K_BLU, K_BG0); P(P_ERR, K_RED, K_BG0); P(P_WARN, K_YEL, K_BG0);
    P(P_NOTE, K_AQU, K_BG0); P(P_OK, K_GRN, K_BG0); P(P_GRAY, K_GRY, K_BG0); P(P_SIDE_SEL, K_FG, K_BG2); P(P_SIDE_SELF, K_BG0, K_BLU);
    P(P_DLG, K_FG, K_BG1); P(P_DLGB, K_LGR, K_BG1); P(P_DLGT, K_YEL, K_BG1); P(P_INP, K_FG, K_BG0); P(P_INPF, K_FG, K_BG2); P(P_BTNX, K_FG, K_BG2);
    P(P_HINT, K_GRY, K_BG0); P(P_TITLE, K_ORG, K_BG0); P(P_SEP, K_BG3, K_BG0); P(P_HINTBAR, K_GRY, K_BG1); P(P_TREEHDR, K_BLU, K_BG1);
    for (int i = 0; i < 9; i++) P(P_SYN + 16 + i, syn[i], K_BG1);  // linha atual (por último: é só estético)
    for (int id = 1; id < P_SYN + 16; id++) if (!PM[id]) PM[id] = PM[P_TEXT];
    for (int id = P_SYN + 16; id < P_SYN + 25; id++) if (!PM[id]) PM[id] = PM[id - 16];
    diagPairUsed = nextId - 1;
    bkgd(CP(P_TEXT));
}

// ============================================================ dados de autocomplete
static const char* KW_HL = "alignas alignof and asm break case catch class const consteval constexpr constinit const_cast continue co_await co_return co_yield decltype default delete do dynamic_cast else enum explicit export extern false final for friend goto if inline mutable namespace new noexcept nullptr operator override private protected public register reinterpret_cast requires return sizeof static static_assert static_cast struct switch template this thread_local throw true try typedef typeid typename union using virtual volatile while NULL";
static const char* TY_HL = "auto bool char double float int long short signed unsigned void size_t ssize_t ptrdiff_t wchar_t char8_t char16_t char32_t uint8_t uint16_t uint32_t uint64_t int8_t int16_t int32_t int64_t std string string_view vector map set array FILE";
static const char* PREPROC = "#include #define #ifdef #ifndef #endif #pragma #if #else #elif #undef #error";
static const char* STDNAMES = "std::string std::string_view std::vector std::array std::map std::unordered_map std::set std::unordered_set std::deque std::list std::queue std::stack std::priority_queue std::pair std::tuple std::optional std::variant std::any std::function std::unique_ptr std::shared_ptr std::weak_ptr std::make_unique std::make_shared std::make_pair std::make_tuple std::move std::forward std::swap std::cout std::cin std::cerr std::endl std::getline std::to_string std::stoi std::stol std::stod std::stof std::sort std::find std::find_if std::for_each std::transform std::accumulate std::reverse std::min std::max std::clamp std::abs std::sqrt std::pow std::floor std::ceil std::round std::begin std::end std::size std::ifstream std::ofstream std::fstream std::stringstream std::ostringstream std::istringstream std::runtime_error std::exception std::invalid_argument std::out_of_range std::thread std::mutex std::lock_guard std::unique_lock std::condition_variable std::atomic std::async std::future std::promise std::chrono std::chrono::steady_clock std::chrono::milliseconds std::chrono::seconds std::this_thread::sleep_for std::filesystem std::filesystem::path std::filesystem::exists std::filesystem::directory_iterator std::regex std::regex_match std::regex_search std::span std::bitset std::nullopt std::size_t std::uint8_t std::uint16_t std::uint32_t std::uint64_t std::int32_t std::int64_t std::initializer_list std::numeric_limits std::iota std::copy std::fill std::count std::any_of std::all_of std::none_of std::lower_bound std::upper_bound std::unique std::remove std::remove_if std::erase_if std::format std::print std::println std::ranges std::views";
static const char* HEADERS = "iostream string vector map unordered_map set unordered_set array deque list queue stack algorithm numeric functional memory utility tuple optional variant any string_view sstream fstream filesystem thread mutex condition_variable atomic future chrono random regex cmath cstdlib cstdio cstring cstdint cassert climits cfloat ctime iomanip limits stdexcept exception typeinfo type_traits bitset span format print ranges numbers concepts iterator initializer_list csignal cerrno stdio.h stdlib.h string.h math.h unistd.h fcntl.h signal.h pthread.h sys/types.h sys/stat.h sys/wait.h sys/socket.h netinet/in.h arpa/inet.h SDL2/SDL.h SDL2/SDL_image.h SDL2/SDL_ttf.h SDL2/SDL_mixer.h GLFW/glfw3.h GL/gl.h GL/glu.h glad/glad.h GL/glew.h glm/glm.hpp glm/gtc/matrix_transform.hpp glm/gtc/type_ptr.hpp fmt/core.h fmt/format.h fmt/ranges.h spdlog/spdlog.h nlohmann/json.hpp boost/asio.hpp boost/filesystem.hpp boost/algorithm/string.hpp boost/program_options.hpp ncurses.h curl/curl.h sqlite3.h SFML/Graphics.hpp SFML/Audio.hpp SFML/Window.hpp SFML/System.hpp SFML/Network.hpp raylib.h Eigen/Dense Eigen/Core opencv2/opencv.hpp opencv2/imgproc.hpp opencv2/highgui.hpp gtest/gtest.h catch2/catch_test_macros.hpp doctest/doctest.h vulkan/vulkan.h imgui.h libavcodec/avcodec.h libavformat/avformat.h libavutil/avutil.h libavutil/imgutils.h libswscale/swscale.h libswresample/swresample.h libavfilter/avfilter.h tbb/tbb.h openssl/ssl.h zlib.h png.h jpeglib.h";
static const char* LIBSYMS = "printf fprintf snprintf sprintf malloc calloc realloc free memcpy memset memmove strlen strcmp strncmp strcpy strncpy strcat fopen fclose fread fwrite fgets fputs puts perror exit abort atoi atof strtol NULL EXIT_SUCCESS EXIT_FAILURE assert "
                             "SDL_Init SDL_Quit SDL_CreateWindow SDL_DestroyWindow SDL_CreateRenderer SDL_DestroyRenderer SDL_RenderClear SDL_RenderPresent SDL_SetRenderDrawColor SDL_RenderFillRect SDL_RenderCopy SDL_PollEvent SDL_Event SDL_QUIT SDL_KEYDOWN SDL_Delay SDL_GetTicks SDL_INIT_VIDEO SDL_WINDOWPOS_CENTERED SDL_WINDOW_SHOWN SDL_Rect SDL_Texture SDL_Surface SDL_LoadBMP IMG_Load TTF_Init "
                             "glfwInit glfwTerminate glfwCreateWindow glfwMakeContextCurrent glfwWindowShouldClose glfwSwapBuffers glfwPollEvents glfwSetKeyCallback glfwGetKey glfwWindowHint glfwDestroyWindow glfwGetTime GLFW_KEY_ESCAPE GLFW_PRESS GLFW_CONTEXT_VERSION_MAJOR GLFW_CONTEXT_VERSION_MINOR GLFW_OPENGL_PROFILE GLFW_OPENGL_CORE_PROFILE gladLoadGLLoader glewInit "
                             "glClear glClearColor glViewport glGenBuffers glBindBuffer glBufferData glGenVertexArrays glBindVertexArray glVertexAttribPointer glEnableVertexAttribArray glDrawArrays glDrawElements glCreateShader glShaderSource glCompileShader glCreateProgram glAttachShader glLinkProgram glUseProgram glGetUniformLocation glUniform1f glUniform3f glUniformMatrix4fv glGenTextures glBindTexture glTexImage2D GL_COLOR_BUFFER_BIT GL_DEPTH_BUFFER_BIT GL_TRIANGLES GL_FLOAT GL_ARRAY_BUFFER GL_STATIC_DRAW GL_VERTEX_SHADER GL_FRAGMENT_SHADER "
                             "glm::vec2 glm::vec3 glm::vec4 glm::mat4 glm::translate glm::rotate glm::scale glm::perspective glm::lookAt glm::radians glm::value_ptr glm::normalize glm::dot glm::cross fmt::print fmt::println fmt::format spdlog::info spdlog::warn spdlog::error spdlog::debug nlohmann::json "
                             "curl_easy_init curl_easy_setopt curl_easy_perform curl_easy_cleanup curl_easy_strerror CURLOPT_URL CURLOPT_WRITEFUNCTION CURLOPT_WRITEDATA sqlite3_open sqlite3_exec sqlite3_close sqlite3_prepare_v2 sqlite3_step sqlite3_finalize "
                             "sf::RenderWindow sf::VideoMode sf::Event sf::Color sf::Texture sf::Sprite sf::Clock sf::Vector2f cv::Mat cv::imread cv::imshow cv::waitKey cv::VideoCapture cv::VideoWriter cv::cvtColor cv::resize cv::GaussianBlur "
                             "avformat_open_input avformat_find_stream_info avformat_close_input avcodec_find_decoder avcodec_alloc_context3 avcodec_open2 avcodec_send_packet avcodec_receive_frame av_read_frame av_packet_alloc av_packet_free av_frame_alloc av_frame_free av_dump_format sws_getContext sws_scale "
                             "initscr endwin refresh getch mvprintw newwin wrefresh InitWindow CloseWindow WindowShouldClose BeginDrawing EndDrawing ClearBackground DrawText DrawRectangle";

static vector<string> DICT, HDRS;
static void buildDict() {
    std::set<string> s;
    for (auto* p : {KW_HL, TY_HL, PREPROC, STDNAMES, LIBSYMS}) for (auto& w : splitWords(p)) s.insert(w);
    DICT.assign(s.begin(), s.end());
    HDRS = splitWords(HEADERS);
}

// ============================================================ buffer
struct Snap { vector<string> lines; int cy, cx; bool dirty; };
struct Buffer {
    string path; vector<string> lines{""}; bool dirty = false, crlf = false, bom = false;  // crlf/bom: preservados ao salvar
    int cy = 0, cx = 0, top = 0, left = 0, want = 0;
    vector<Snap> undo, redo;
    string name() const { return pstr(toPath(path).filename()); }
};

enum Mode { NORMAL, INSERT, VISUAL, VLINE };
enum Focus { F_EDITOR, F_TREE };

struct Config { string run = "rsfmake run", release = "rsfmake run -r"; bool terminal = false; };
struct Runner { pid_t pid = -1; int fd = -1; bool running = false, eof = false, killed = false; string part; };
struct TreeNode { string path, name; bool dir; int depth; };
struct Popup { bool on = false; vector<string> items; int sel = 0, start = 0; };
struct Hit { int x0, x1, idx, cx0; };

static vector<Buffer> bufs; static int cur = -1;
static string root; static Config cfg; static bool releaseMode = false;
static Mode mode = NORMAL; static Focus focus = F_TREE;
static bool showTree = true, showOut = false, quitting = false;
static vector<TreeNode> tree; static std::set<string> expanded; static int treeSel = 0, treeTop = 0;
static string msg; static bool msgErr = false;
static string reg; static bool regLine = false;
static int vsy = 0, vsx = 0; static string lastSearch;
static Runner runner; static vector<string> outLines; static string outTitle; static int outScroll = -1, outFirst = 0;
static Popup pop;
static int SH = 24, SW = 80;
static struct { int treeW, edX, edW, edY, edH, outY, outH, statY, msgY; } L;
static int menuOpen = -1, menuSel = 0, btnW = 0, gGut = 4, gTX = 0, curSY = 0, curSX = 0;
static vector<Hit> tabHits, menuHits, treeBtns;
static struct { int x, y, w, h; } dropRect = {0, 0, 0, 0};
static string startErr;
// seleção no modo inserir (âncora = selAy/selAx; a outra ponta é o cursor), arraste do mouse e agrupamento do desfazer
static bool insSel = false, dragging = false; static int selAy = 0, selAx = 0;
static int insKind = 0; static bool lastSp = false;
static string sysSeen;  // último conteúdo visto na área de transferência do sistema
static long diagMouseN = 0; static unsigned long diagLastB = 0; static int diagLastX = -1, diagLastY = -1, diagLastKey = 0;

static void setMsg(const string& m, bool err = false) { msg = m; msgErr = err; }

// ---- leitura de tecla. Entrega SEMPRE bytes UTF-8 (um por chamada) para caracteres, como o ncurses no Linux.
static std::deque<int> keyq;
static void pushUtf8(unsigned cp) {
    if (cp < 0x80) keyq.push_back((int)cp);
    else if (cp < 0x800) { keyq.push_back(0xC0 | (cp >> 6)); keyq.push_back(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { keyq.push_back(0xE0 | (cp >> 12)); keyq.push_back(0x80 | ((cp >> 6) & 63)); keyq.push_back(0x80 | (cp & 63)); }
    else { keyq.push_back(0xF0 | (cp >> 18)); keyq.push_back(0x80 | ((cp >> 12) & 63)); keyq.push_back(0x80 | ((cp >> 6) & 63)); keyq.push_back(0x80 | (cp & 63)); }
}
static int readKey(int ms) {  // ms < 0 bloqueia
    if (!keyq.empty()) { int c = keyq.front(); keyq.pop_front(); return c; }
    timeout(ms); int ch;
#if defined(_WIN32) && defined(PDC_WIDE)
    static unsigned hi = 0;  // metade alta de um par substituto UTF-16
    wint_t wc = 0; int r = get_wch(&wc); timeout(-1);
    if (r == ERR) return ERR;
    if (r == KEY_CODE_YES) ch = (int)wc;
    else {
        unsigned cp = (unsigned)wc;
        if (cp >= 0xD800 && cp < 0xDC00) { hi = cp; return ERR; }
        if (cp >= 0xDC00 && cp < 0xE000 && hi) { cp = 0x10000 + ((hi - 0xD800) << 10) + (cp - 0xDC00); }
        hi = 0; pushUtf8(cp); ch = keyq.front(); keyq.pop_front(); return ch;
    }
#else
    ch = getch(); timeout(-1);
#ifdef _WIN32
    if (ch >= 128 && ch < 256) {  // PDCurses sem suporte a wide: byte na codepage do console -> UTF-8
        string u = cpToU8(string(1, (char)ch), GetConsoleCP()); for (unsigned char c : u) keyq.push_back(c);
        if (!keyq.empty()) { ch = keyq.front(); keyq.pop_front(); }
    }
#endif
#endif
    if (ch == KEY_RESIZE) {
#ifdef PDCURSES
        resize_term(0, 0);  // PDCurses exige isto para atualizar o tamanho (inclusive dentro de diálogos)
#endif
    }
    return ch;
}

static void layout() {
    getmaxyx(stdscr, SH, SW);
    L.msgY = SH - 1; L.statY = SH - 2;
    L.outH = showOut ? std::max(4, std::min(12, (SH - 6) / 3)) : 0;
    L.outY = L.statY - L.outH; L.edY = 2; L.edH = std::max(1, L.outY - L.edY);
    L.treeW = showTree ? std::min(32, std::max(22, SW / 4)) : 0;
    L.edX = showTree ? L.treeW + 1 : 0; L.edW = SW - L.edX;
}

// ============================================================ operações de buffer
static void clampCursor(Buffer& b) {
    int n = (int)b.lines.size(); b.cy = std::max(0, std::min(b.cy, n - 1));
    const string& l = b.lines[b.cy]; int len = (int)l.size();
    int maxx = mode == INSERT ? len : (len > 0 ? prevCp(l, len) : 0);
    b.cx = std::max(0, std::min(b.cx, maxx));
    while (b.cx > 0 && b.cx < len && (((unsigned char)l[b.cx]) & 0xC0) == 0x80) b.cx--;
}
static void setWant(Buffer& b) { b.want = vcolOf(b.lines[b.cy], b.cx); }
static void snap(Buffer& b) {
    b.undo.push_back({b.lines, b.cy, b.cx, b.dirty}); if (b.undo.size() > 300) b.undo.erase(b.undo.begin());
    b.redo.clear(); b.dirty = true;
}
static void doUndo() {
    if (cur < 0) return; Buffer& b = bufs[cur];
    if (b.undo.empty()) { setMsg("Nothing to undo"); return; }
    b.redo.push_back({b.lines, b.cy, b.cx, b.dirty}); Snap s = b.undo.back(); b.undo.pop_back();
    b.lines = s.lines; b.cy = s.cy; b.cx = s.cx; b.dirty = s.dirty; clampCursor(b); setWant(b);
}
static void doRedo() {
    if (cur < 0) return; Buffer& b = bufs[cur];
    if (b.redo.empty()) { setMsg("Nothing to redo"); return; }
    b.undo.push_back({b.lines, b.cy, b.cx, b.dirty}); Snap s = b.redo.back(); b.redo.pop_back();
    b.lines = s.lines; b.cy = s.cy; b.cx = s.cx; b.dirty = s.dirty; clampCursor(b); setWant(b);
}
static bool saveBuffer(Buffer& b) {
    std::ofstream f(toPath(b.path), std::ios::binary);
    if (!f) { setMsg("Error saving " + b.path, true); return false; }
    if (b.bom) f << "\xEF\xBB\xBF";
    const char* eol = b.crlf ? "\r\n" : "\n";
    for (auto& l : b.lines) f << l << eol;
    f.flush(); if (!f) { setMsg("Error writing " + b.path, true); return false; }
    b.dirty = false; return true;
}
static string getRange(Buffer& b, int y1, int x1, int y2, int x2) {
    if (y1 == y2) return b.lines[y1].substr(x1, x2 - x1);
    string r = b.lines[y1].substr(x1) + "\n";
    for (int y = y1 + 1; y < y2; y++) r += b.lines[y] + "\n";
    return r + b.lines[y2].substr(0, x2);
}
static void delRange(Buffer& b, int y1, int x1, int y2, int x2) {
    string head = b.lines[y1].substr(0, x1), tail = b.lines[y2].substr(x2);
    b.lines.erase(b.lines.begin() + y1 + 1, b.lines.begin() + y2 + 1); b.lines[y1] = head + tail;
    b.cy = y1; b.cx = x1;
}
static void yankLines(Buffer& b, int y, int n) {
    n = std::min(n, (int)b.lines.size() - y); reg.clear();
    for (int i = 0; i < n; i++) { if (i) reg += "\n"; reg += b.lines[y + i]; }
    regLine = true;
}
static void delLines(Buffer& b, int y, int n) {
    n = std::min(n, (int)b.lines.size() - y); yankLines(b, y, n);
    b.lines.erase(b.lines.begin() + y, b.lines.begin() + y + n);
    if (b.lines.empty()) b.lines.push_back("");
    b.cy = std::min(y, (int)b.lines.size() - 1); b.cx = firstNonBlank(b.lines[b.cy]);
}
static void pasteText(Buffer& b, bool after) {
    if (reg.empty()) return;
    vector<string> parts = splitLines(reg);
    if (regLine) {
        int pos = b.cy + (after ? 1 : 0); b.lines.insert(b.lines.begin() + pos, parts.begin(), parts.end());
        b.cy = pos; b.cx = firstNonBlank(b.lines[pos]); return;
    }
    string& l = b.lines[b.cy]; int x = (after && !l.empty()) ? nextCp(l, b.cx) : b.cx;
    if (parts.size() == 1) { l.insert(x, parts[0]); b.cx = x + std::max<int>(0, (int)parts[0].size() - 1); return; }
    string tail = l.substr(x); l = l.substr(0, x) + parts[0];
    vector<string> mid(parts.begin() + 1, parts.end()); mid.back() += tail;
    b.lines.insert(b.lines.begin() + b.cy + 1, mid.begin(), mid.end()); b.cx = x;
}
// ---- seleção / desfazer / área de transferência no modo inserir
static bool hasSel(const Buffer& b) {
    return insSel && selAy < (int)b.lines.size() && selAx <= (int)b.lines[selAy].size() && (selAy != b.cy || selAx != b.cx);
}
static void selRange(const Buffer& b, int& y1, int& x1, int& y2, int& x2) {
    y1 = selAy; x1 = selAx; y2 = b.cy; x2 = b.cx;
    if (y1 > y2 || (y1 == y2 && x1 > x2)) { std::swap(y1, y2); std::swap(x1, x2); }
}
static string selText(Buffer& b) { int y1, x1, y2, x2; selRange(b, y1, x1, y2, x2); return getRange(b, y1, x1, y2, x2); }
static void deleteSel(Buffer& b) { int y1, x1, y2, x2; selRange(b, y1, x1, y2, x2); delRange(b, y1, x1, y2, x2); insSel = false; }
static void mouseToCursor(Buffer& b, int x, int y) {  // y fora da área rola o texto (usado no arraste)
    int row = y - L.edY, ny = b.top + row;
    if (row < 0) ny = b.top - 1; else if (row >= L.edH) ny = b.top + L.edH;
    b.cy = std::max(0, std::min((int)b.lines.size() - 1, ny));
    b.cx = cxFromV(b.lines[b.cy], std::max(0, b.left + x - (L.edX + gGut)));
    clampCursor(b); setWant(b);
}
// Desfazer no inserir: digitar (1) e apagar (2) agrupam em sequência; qualquer outra edição (3) abre novo ponto.
static void insSnap(Buffer& b, int kind) {
    if (kind != 3 && kind == insKind) return;
    insKind = kind;
    if (!b.undo.empty() && b.undo.back().lines == b.lines) { b.redo.clear(); b.dirty = true; return; }
    snap(b);
}
static void beginEdit(Buffer& b, int kind) {  // prepara uma edição: ponto de desfazer + remove a seleção (substitui)
    bool s = hasSel(b); if (s) insKind = -1;
    insSnap(b, kind); if (s) deleteSel(b); insSel = false;
}
static string b64(const string& s) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; string o; size_t i = 0;
    for (; i + 2 < s.size(); i += 3) {
        unsigned v = ((unsigned char)s[i] << 16) | ((unsigned char)s[i + 1] << 8) | (unsigned char)s[i + 2];
        o += T[v >> 18]; o += T[(v >> 12) & 63]; o += T[(v >> 6) & 63]; o += T[v & 63];
    }
    if (i + 1 == s.size()) { unsigned v = (unsigned char)s[i] << 16; o += T[v >> 18]; o += T[(v >> 12) & 63]; o += "=="; }
    else if (i + 2 == s.size()) { unsigned v = ((unsigned char)s[i] << 16) | ((unsigned char)s[i + 1] << 8); o += T[v >> 18]; o += T[(v >> 12) & 63]; o += T[(v >> 6) & 63]; o += '='; }
    return o;
}
#ifdef _WIN32
// Windows: clipboard nativo (CF_UNICODETEXT) direto pela API; independe da versão do PDCurses.
static string readClip() {
    string s; if (!OpenClipboard(nullptr)) return s;
    if (void* h = GetClipboardData(13)) { if (const wchar_t* w = (const wchar_t*)GlobalLock(h)) { s = wu8(w); GlobalUnlock(h); } }
    CloseClipboard();
    if (s.size() > (1u << 20) || memchr(s.data(), 0, s.size())) return "";
    return s;
}
static void pushClip(const string& t) {
    string crlf; crlf.reserve(t.size() + 16); for (char c : t) { if (c == '\n') crlf += '\r'; crlf += c; }  // apps do Windows esperam CRLF
    std::wstring w = u8w(crlf);
    if (OpenClipboard(nullptr)) {
        EmptyClipboard();
        if (void* h = GlobalAlloc(2, (w.size() + 1) * sizeof(wchar_t))) {
            if (void* p = GlobalLock(h)) { memcpy(p, w.c_str(), (w.size() + 1) * sizeof(wchar_t)); GlobalUnlock(h); SetClipboardData(13, h); }
        }
        CloseClipboard();
    }
    sysSeen = readClip();
}
#else
static const char* CLIP_GET[4] = {"wl-paste -n 2>/dev/null", "xclip -selection clipboard -o 2>/dev/null", "xsel -ob 2>/dev/null", "pbpaste 2>/dev/null"};
static const char* CLIP_SET[4] = {"wl-copy >/dev/null 2>&1", "xclip -selection clipboard -i >/dev/null 2>&1", "xsel -ib >/dev/null 2>&1", "pbcopy >/dev/null 2>&1"};
static int detectClip() {  // ferramenta de clipboard do sistema (-1 = nenhuma)
    static int k = -2; if (k != -2) return k; k = -1;
    const char* bin[4] = {"wl-paste", "xclip", "xsel", "pbpaste"};
    for (int i = 0; i < 4; i++) {
        if (i == 0 && !getenv("WAYLAND_DISPLAY")) continue;
        if ((i == 1 || i == 2) && !getenv("DISPLAY")) continue;
        if (system((string("command -v ") + bin[i] + " >/dev/null 2>&1").c_str()) == 0) { k = i; break; }
    }
    return k;
}
static string readClip() {
    int k = detectClip(); if (k < 0) return ""; FILE* p = popen(CLIP_GET[k], "r"); if (!p) return "";
    string s; char buf[4096]; size_t n; while ((n = fread(buf, 1, sizeof buf, p)) > 0 && s.size() < (1u << 20)) s.append(buf, n);
    pclose(p); if (memchr(s.data(), 0, s.size())) return ""; return s;
}
static void pushClip(const string& t) {  // envia ao clipboard do sistema (ou OSC 52 se não houver ferramenta)
    int k = detectClip();
    if (k < 0) { string o = "\033]52;c;" + b64(t) + "\a"; fputs(o.c_str(), stdout); fflush(stdout); return; }
    if (FILE* p = popen(CLIP_SET[k], "w")) { fwrite(t.data(), 1, t.size(), p); pclose(p); }
    sysSeen = readClip();
}
#endif
static void pasteSource(string& t, bool& line) {  // conteúdo novo do sistema vence; senão usa o registrador interno (yy, dd, Ctrl-C...)
    string s = readClip();
    if (!s.empty() && s != sysSeen) { sysSeen = s; t.clear(); for (char c : s) if (c != '\r') t += c; line = false; return; }
    t = reg; line = regLine;
}
static void insPasteText(Buffer& b, const string& t) {
    vector<string> parts = splitLines(t); string tail = b.lines[b.cy].substr(b.cx); string head = b.lines[b.cy].substr(0, b.cx) + parts[0];
    if (parts.size() == 1) { b.cx = (int)head.size(); b.lines[b.cy] = head + tail; return; }
    b.lines[b.cy] = head; vector<string> mid(parts.begin() + 1, parts.end()); int nx = (int)mid.back().size(); mid.back() += tail;
    b.lines.insert(b.lines.begin() + b.cy + 1, mid.begin(), mid.end()); b.cy += (int)mid.size(); b.cx = nx;
}
static void insCopy(Buffer& b) {
    if (hasSel(b)) { reg = selText(b); regLine = false; pushClip(reg); setMsg("Copied"); }
    else { yankLines(b, b.cy, 1); pushClip(reg + "\n"); setMsg("Line copied"); }
}
static void insCut(Buffer& b) {
    if (hasSel(b)) { string t = selText(b); beginEdit(b, 3); reg = t; regLine = false; pushClip(t); setMsg("Cut"); }
    else { string t = b.lines[b.cy]; beginEdit(b, 3); delLines(b, b.cy, 1); pushClip(t + "\n"); setMsg("Line cut"); }
    pop.on = false; setWant(b);
}
static void insPaste(Buffer& b) {
    string t; bool line = false; pasteSource(t, line);
    if (t.empty()) { setMsg("Clipboard is empty", true); return; }
    beginEdit(b, 3); pop.on = false;
    if (line) { vector<string> parts = splitLines(t); b.lines.insert(b.lines.begin() + b.cy, parts.begin(), parts.end()); b.cy += (int)parts.size(); }  // linha inteira entra acima da atual
    else insPasteText(b, t);
    setWant(b);
}
static void indentLines(Buffer& b, int y, int n, bool in) {
    n = std::min(n, (int)b.lines.size() - y); snap(b);
    for (int i = 0; i < n; i++) {
        string& l = b.lines[y + i];
        if (in) { if (!l.empty()) l = "    " + l; }
        else { if (!l.empty() && l[0] == '\t') l.erase(0, 1); else { int k = 0; while (k < 4 && k < (int)l.size() && l[k] == ' ') k++; l.erase(0, k); } }
    }
    b.cx = firstNonBlank(b.lines[b.cy]);
}
static int clsOf(char c) { if (isspace((unsigned char)c)) return 0; if (isalnum((unsigned char)c) || c == '_' || (unsigned char)c >= 128) return 1; return 2; }
static void moveW(Buffer& b) {
    string& l = b.lines[b.cy]; int i = b.cx, len = (int)l.size();
    if (i < len) { int c = clsOf(l[i]); if (c != 0) while (i < len && clsOf(l[i]) == c) i++; }
    while (i < len && clsOf(l[i]) == 0) i++;
    if (i >= len && b.cy + 1 < (int)b.lines.size()) { b.cy++; b.cx = firstNonBlank(b.lines[b.cy]); if (b.lines[b.cy].empty()) b.cx = 0; }
    else b.cx = std::min(i, std::max(0, len - 1));
}
static void moveB(Buffer& b) {
    if (b.cx == 0) { if (b.cy == 0) return; b.cy--; b.cx = (int)b.lines[b.cy].size(); }
    string& l = b.lines[b.cy]; int i = b.cx;
    while (i > 0 && clsOf(l[i - 1]) == 0) i--;
    if (i > 0) { int c = clsOf(l[i - 1]); while (i > 0 && clsOf(l[i - 1]) == c) i--; }
    b.cx = i;
}
static void moveE(Buffer& b) {
    int y = b.cy, i = b.cx + 1, n = (int)b.lines.size();
    for (;;) {
        const string& l = b.lines[y];
        while (i < (int)l.size() && clsOf(l[i]) == 0) i++;
        if (i < (int)l.size()) break;
        if (y + 1 >= n) { b.cy = y; b.cx = std::max(0, (int)l.size() - 1); return; }
        y++; i = 0;
    }
    const string& l = b.lines[y]; int c = clsOf(l[i]);
    while (i + 1 < (int)l.size() && clsOf(l[i + 1]) == c) i++;
    b.cy = y; b.cx = i;
}
static bool matchBracket(Buffer& b) {
    const string& l0 = b.lines[b.cy]; if (b.cx >= (int)l0.size()) return false;
    char c = l0[b.cx]; const string op = "([{", cl = ")]}"; size_t pi = op.find(c), qi = cl.find(c);
    if (pi == string::npos && qi == string::npos) return false;
    int dir = pi != string::npos ? 1 : -1; char target = pi != string::npos ? cl[pi] : op[qi];
    int y = b.cy, x = b.cx, depth = 0, n = (int)b.lines.size();
    for (;;) {
        const string& s = b.lines[y];
        if (x >= 0 && x < (int)s.size()) { char ch = s[x]; if (ch == c) depth++; else if (ch == target) { depth--; if (depth == 0) { b.cy = y; b.cx = x; return true; } } }
        x += dir;
        if (x < 0) { if (y == 0) return false; y--; x = (int)b.lines[y].size() - 1; }
        else if (x >= (int)b.lines[y].size()) { if (y + 1 >= n) return false; y++; x = 0; }
    }
}
static bool searchDir(Buffer& b, const string& pat, bool fwd) {
    if (pat.empty()) return false; int n = (int)b.lines.size(), y = b.cy;
    if (fwd) {
        size_t from = b.cx + 1;
        for (int k = 0; k <= n; k++) {
            const string& l = b.lines[y]; size_t p = from <= l.size() ? l.find(pat, from) : string::npos;
            if (p != string::npos) { b.cy = y; b.cx = (int)p; return true; }
            y = (y + 1) % n; from = 0;
        }
    } else {
        long from = (long)b.cx - 1;
        for (int k = 0; k <= n; k++) {
            const string& l = b.lines[y]; size_t p = from >= 0 ? l.rfind(pat, (size_t)from) : string::npos;
            if (p != string::npos) { b.cy = y; b.cx = (int)p; return true; }
            y = (y - 1 + n) % n; from = (long)b.lines[y].size();
        }
    }
    return false;
}

// ============================================================ realce de sintaxe
static std::unordered_set<string> HL_KW, HL_TY;
static bool isIdent(char c) { return isalnum((unsigned char)c) || c == '_' || (unsigned char)c >= 128; }
static void scanLine(const string& s, bool& inb, vector<uint8_t>* out) {
    size_t n = s.size(); if (out) out->assign(n, S_TEXT);
    auto setr = [&](size_t a, size_t e, uint8_t c) { if (out) for (size_t k = a; k < e && k < n; k++) (*out)[k] = c; };
    size_t i = 0, fnb = s.find_first_not_of(" \t");
    while (i < n) {
        if (inb) { size_t e = s.find("*/", i); if (e == string::npos) { setr(i, n, S_COM); i = n; } else { setr(i, e + 2, S_COM); i = e + 2; inb = false; } continue; }
        char c = s[i];
        if (c == '/' && i + 1 < n && s[i + 1] == '/') { setr(i, n, S_COM); break; }
        if (c == '/' && i + 1 < n && s[i + 1] == '*') { inb = true; setr(i, i + 2, S_COM); i += 2; continue; }
        if (c == '"' || c == '\'') {
            size_t j = i + 1; while (j < n && s[j] != c) { if (s[j] == '\\') j++; j++; }
            j = std::min(n, j + 1); setr(i, j, S_STR); i = j; continue;
        }
        if (c == '#' && i == fnb) {
            size_t j = i + 1; while (j < n && isspace((unsigned char)s[j])) j++; while (j < n && isalpha((unsigned char)s[j])) j++;
            setr(i, j, S_PRE); i = j;
            if (s.compare(fnb, 8, "#include") == 0) { size_t lt = s.find('<', i); if (lt != string::npos) { size_t gt = s.find('>', lt); size_t e = gt == string::npos ? n : gt + 1; setr(lt, e, S_STR); i = e; } }
            continue;
        }
        if (isdigit((unsigned char)c) || (c == '.' && i + 1 < n && isdigit((unsigned char)s[i + 1]))) {
            if (i > 0 && isIdent(s[i - 1])) { i++; continue; }
            size_t j = i; while (j < n && (isalnum((unsigned char)s[j]) || s[j] == '.' || s[j] == '_' || s[j] == '\'')) j++;
            setr(i, j, S_NUM); i = j; continue;
        }
        if (isalpha((unsigned char)c) || c == '_') {
            size_t j = i; while (j < n && isIdent(s[j])) j++;
            string w = s.substr(i, j - i); uint8_t k = S_TEXT;
            if (HL_KW.count(w)) k = S_KW; else if (HL_TY.count(w)) k = S_TYPE;
            else { size_t q = j; while (q < n && s[q] == ' ') q++; if (q < n && s[q] == '(') k = S_FN; }
            setr(i, j, k); i = j; continue;
        }
        {   // operadores de comparação e lógicos: == != <= >= < > && ||
            char d = i + 1 < n ? s[i + 1] : 0; size_t len = 0;
            if (c == '-' && d == '>') { i += 2; continue; }                                   // seta ->
            if ((c == '<' || c == '>') && d == c) { i += (i + 2 < n && s[i + 2] == '=') ? 3 : 2; continue; }  // << >> <<= >>=
            if (c == '=' && d == '=') len = 2;
            else if ((c == '!' || c == '<' || c == '>') && d == '=') len = 2;
            else if ((c == '&' || c == '|') && d == c) len = 2;
            else if (c == '<' || c == '>') len = 1;
            if (len) { setr(i, i + len, S_OP); i += len; continue; }
        }
        i++;
    }
}

// ============================================================ árvore de arquivos
static bool isExp(const string& p) { return expanded.count(pathKey(p)) > 0; }
static void setExp(const string& p, bool on) { if (on) expanded.insert(pathKey(p)); else expanded.erase(pathKey(p)); }
static fs::path resolvePath(const string& s) {
    string e = s; if (!e.empty() && e[0] == '~') e = homeDir() + e.substr(1);
    std::error_code ec0; fs::path p = toPath(e);
    if (p.is_relative()) p = (root.empty() ? fs::current_path(ec0) : toPath(root)) / p;
    std::error_code ec; fs::path c = fs::weakly_canonical(p, ec); return ec ? p : c;
}
static void buildTree(const fs::path& dir, int depth, vector<TreeNode>& out) {
    vector<fs::directory_entry> es = listDir(dir);
    auto low = [](const fs::path& p) { return lowerA(pstr(p.filename())); };
    std::sort(es.begin(), es.end(), [&](const fs::directory_entry& a, const fs::directory_entry& b) {
        std::error_code e1; bool da = a.is_directory(e1), db = b.is_directory(e1);
        if (da != db) return da; return low(a.path()) < low(b.path());
    });
    for (auto& e : es) {
        string nm = pstr(e.path().filename()); if (nm == ".git") continue;
        std::error_code e2; bool d = e.is_directory(e2); string ps = pstr(e.path());
        out.push_back({ps, nm, d, depth});
        if (d && isExp(ps)) buildTree(e.path(), depth + 1, out);
    }
}
static void refreshTree() {
    string sel = (treeSel >= 0 && treeSel < (int)tree.size()) ? tree[treeSel].path : "";
    tree.clear(); if (!root.empty()) buildTree(toPath(root), 0, tree);
    treeSel = 0; for (int i = 0; i < (int)tree.size(); i++) if (samePath(tree[i].path, sel)) treeSel = i;
}
static void selectPath(const string& p) {
    fs::path q = toPath(p); string rk = pathKey(root);
    for (fs::path d = q.parent_path();; d = d.parent_path()) {
        string dk = pathKey(pstr(d)); if (dk.size() <= rk.size() || !startsWith(dk, rk)) break;
        setExp(pstr(d), true);
    }
    refreshTree(); for (int i = 0; i < (int)tree.size(); i++) if (samePath(tree[i].path, p)) treeSel = i;
}
static string selectedDir() {
    if (treeSel >= 0 && treeSel < (int)tree.size()) { auto& n = tree[treeSel]; return n.dir ? n.path : pstr(toPath(n.path).parent_path()); }
    return root;
}
static string treeLabel() { string n = pstr(toPath(root).filename()); return n.empty() ? root : n; }  // "C:\" não tem filename
static string relToRoot(const string& base) {
    if (samePath(base, root)) return "(project root)";
    string r = base.substr(std::min(base.size(), root.size())); while (!r.empty() && (r[0] == '/' || r[0] == '\\')) r.erase(0, 1);
    return r.empty() ? "(project root)" : r;
}

// ============================================================ arquivos / projeto
static bool openFile(const string& p, int line = 0, int col = 0) {
    std::error_code ec; fs::path fpth = toPath(p);
    if (!fs::is_regular_file(fpth, ec)) { setMsg("Not a file: " + p, true); return false; }
    for (int i = 0; i < (int)bufs.size(); i++) if (samePath(bufs[i].path, p)) { cur = i; goto place; }
    {
        if (fs::file_size(fpth, ec) > 20u * 1024 * 1024) { setMsg("File too large (>20MB)", true); return false; }
        std::ifstream f(fpth, std::ios::binary); if (!f) { setMsg("Could not open: " + p, true); return false; }
        char probe[4096]; f.read(probe, sizeof probe); size_t got = (size_t)f.gcount();
        if (memchr(probe, 0, got)) { setMsg("Binary file — not opened", true); return false; }
        f.clear(); f.seekg(0);
        Buffer b; b.path = p; b.lines.clear(); string l; size_t ncr = 0;
        while (std::getline(f, l)) { if (!l.empty() && l.back() == '\r') { l.pop_back(); ncr++; } b.lines.push_back(l); }
        if (b.lines.empty()) b.lines.push_back("");
        b.crlf = ncr > 0 && ncr * 2 >= b.lines.size();  // preserva CRLF ao salvar
        if (b.lines[0].compare(0, 3, "\xEF\xBB\xBF") == 0) { b.lines[0].erase(0, 3); b.bom = true; }  // BOM UTF-8 (comum no Windows)
        bufs.push_back(std::move(b)); cur = (int)bufs.size() - 1;
    }
place:
    Buffer& b = bufs[cur]; mode = NORMAL; focus = F_EDITOR; pop.on = false;
    if (line > 0) { b.cy = std::min(line - 1, (int)b.lines.size() - 1); b.cx = col > 0 ? col - 1 : firstNonBlank(b.lines[b.cy]); clampCursor(b); b.top = std::max(0, b.cy - L.edH / 2); }
    setWant(b); return true;
}
static void loadConfig() {
    cfg = Config(); std::ifstream f(toPath(root) / ".vimple.conf"); string l;
    while (std::getline(f, l)) {
        size_t e = l.find('='); if (e == string::npos) continue; string k = l.substr(0, e), v = l.substr(e + 1);
        if (k == "run") cfg.run = v; else if (k == "release") cfg.release = v; else if (k == "terminal") cfg.terminal = (v == "1" || v == "on");
    }
}
static void saveConfig() {
    std::ofstream f(toPath(root) / ".vimple.conf"); f << "run=" << cfg.run << "\nrelease=" << cfg.release << "\nterminal=" << (cfg.terminal ? 1 : 0) << "\n";
}
static void openProject(const fs::path& p) {
    { fs::path q = p; q.make_preferred(); root = trimSep(pstr(q)); }
    bufs.clear(); cur = -1; expanded.clear(); tree.clear(); treeSel = treeTop = 0;
    loadConfig(); refreshTree(); showTree = true; focus = F_TREE; mode = NORMAL; outLines.clear(); showOut = false;
}

// ============================================================ execução do projeto
static void outPush(string s) {
    string o; o.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = s[i];
        if (c == 27 && i + 1 < s.size() && s[i + 1] == '[') { i += 2; while (i < s.size() && !isalpha((unsigned char)s[i])) i++; continue; }
        if (c == '\r') continue; if (c == '\t') { o += "    "; continue; } o += (char)c;
    }
    outLines.push_back(o); if (outLines.size() > 5000) outLines.erase(outLines.begin(), outLines.begin() + 1000);
}
#ifdef _WIN32
// Windows: cmd.exe /c com saída por pipe anônimo (painel de saída funciona como no Linux); parar = taskkill /T (árvore inteira).
static void* wProc = nullptr; static void* wPipe = nullptr; static unsigned long wPid = 0;
static bool procAlive() { unsigned long code = 0; return wProc && GetExitCodeProcess(wProc, &code) && code == 259; }
static bool runnerStart(const string& cmd) {
    W32_SA sa = {(unsigned long)sizeof(W32_SA), nullptr, 1}; void* rd = nullptr; void* wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return false;
    SetHandleInformation(rd, 1, 0);  // lado de leitura não é herdado
    void* nul = CreateFileW(L"NUL", 0x80000000u, 3, &sa, 3, 0, nullptr);  // stdin do filho = NUL (não rouba o teclado)
    W32_SI si; memset(&si, 0, sizeof si); si.cb = (unsigned long)sizeof si; si.flags = 0x101; si.show = 0; si.in = nul; si.out = wr; si.err = wr;
    W32_PI pi; memset(&pi, 0, sizeof pi);
    std::wstring line = L"cmd.exe /d /s /c \"" + u8w(cmd) + L"\""; std::wstring wd = u8w(root);
    int ok = CreateProcessW(nullptr, &line[0], nullptr, nullptr, 1, 0x08000000 /*CREATE_NO_WINDOW*/, nullptr, wd.c_str(), &si, &pi);
    CloseHandle(wr); if (nul && nul != (void*)(intptr_t)-1) CloseHandle(nul);
    if (!ok) { CloseHandle(rd); return false; }
    CloseHandle(pi.thr); wProc = pi.proc; wPipe = rd; wPid = pi.pid;
    runner.running = true; runner.eof = false; runner.killed = false; runner.part.clear(); return true;
}
static string fixOut(const string& s) { return validUtf8(s) ? s : cpToU8(s, GetOEMCP()); }  // compiladores antigos saem em OEM
static void runnerPoll() {
    if (!runner.running) return;
    if (!runner.eof && wPipe) {
        for (int guard = 0; guard < 64; guard++) {
            unsigned long avail = 0;
            if (!PeekNamedPipe(wPipe, nullptr, 0, nullptr, &avail, nullptr)) { runner.eof = true; break; }
            if (avail == 0) { if (!procAlive()) runner.eof = true; break; }  // processo acabou (netos podem segurar o pipe)
            char buf[4096]; unsigned long got = 0;
            if (!ReadFile(wPipe, buf, sizeof buf, &got, nullptr) || got == 0) { runner.eof = true; break; }
            runner.part.append(buf, got); size_t pos;
            while ((pos = runner.part.find('\n')) != string::npos) { outPush(fixOut(runner.part.substr(0, pos))); runner.part.erase(0, pos + 1); }
        }
        if (runner.eof) { if (!runner.part.empty()) { outPush(fixOut(runner.part)); runner.part.clear(); } CloseHandle(wPipe); wPipe = nullptr; }
    }
    if (runner.eof) {
        unsigned long code = 259;
        if (!wProc || !GetExitCodeProcess(wProc, &code)) code = 1;
        if (code != 259) { if (wProc) CloseHandle(wProc); wProc = nullptr; runner.running = false; outPush("■ finished (code " + std::to_string((long)code) + ")"); }
    }
}
static void runnerStop() {
    if (!runner.running) return;
    if (!runner.killed) {
        std::wstring line = L"taskkill /PID " + std::to_wstring(wPid) + L" /T /F";
        W32_SI si; memset(&si, 0, sizeof si); si.cb = (unsigned long)sizeof si; si.flags = 1; si.show = 0; W32_PI pi; memset(&pi, 0, sizeof pi);
        if (CreateProcessW(nullptr, &line[0], nullptr, nullptr, 0, 0x08000000, nullptr, nullptr, &si, &pi)) { CloseHandle(pi.thr); CloseHandle(pi.proc); }
        else if (wProc) TerminateProcess(wProc, 1);
    } else if (wProc) TerminateProcess(wProc, 1);
    runner.killed = true;
}
static const bool kForceTerminal = false;
#else
static bool runnerStart(const string& cmd) {
    int p[2]; if (pipe(p) < 0) return false; pid_t id = fork();
    if (id < 0) { close(p[0]); close(p[1]); return false; }
    if (id == 0) {
        setsid(); dup2(p[1], 1); dup2(p[1], 2); int dn = open("/dev/null", O_RDONLY); if (dn >= 0) dup2(dn, 0);
        close(p[0]); close(p[1]); if (chdir(root.c_str()) != 0) _exit(126);
        execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr); _exit(127);
    }
    close(p[1]); runner.fd = p[0]; fcntl(runner.fd, F_SETFL, O_NONBLOCK); fcntl(runner.fd, F_SETFD, FD_CLOEXEC);
    runner.pid = id; runner.running = true; runner.eof = false; runner.killed = false; runner.part.clear(); return true;
}
static void runnerPoll() {
    if (!runner.running) return;
    if (!runner.eof && runner.fd >= 0) {
        char buf[4096];
        for (;;) {
            ssize_t n = read(runner.fd, buf, sizeof buf);
            if (n > 0) { runner.part.append(buf, (size_t)n); size_t pos; while ((pos = runner.part.find('\n')) != string::npos) { outPush(runner.part.substr(0, pos)); runner.part.erase(0, pos + 1); } }
            else if (n == 0) { runner.eof = true; break; }
            else { if (errno == EAGAIN || errno == EINTR) break; runner.eof = true; break; }
        }
        if (runner.eof) { if (!runner.part.empty()) { outPush(runner.part); runner.part.clear(); } close(runner.fd); runner.fd = -1; }
    }
    if (runner.eof) {
        int st = 0; pid_t r = waitpid(runner.pid, &st, WNOHANG);
        if (r == runner.pid) {
            int code = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st); runner.running = false;
            outPush("■ finished (code " + std::to_string(code) + ")");
        }
    }
}
static void runnerStop() {
    if (!runner.running) return;
    kill(-runner.pid, runner.killed ? SIGKILL : SIGTERM); runner.killed = true;
}
static const bool kForceTerminal = false;
#endif
static void saveAll() { for (auto& b : bufs) if (b.dirty) saveBuffer(b); }
static void startRun(bool rel) {
    if (root.empty()) return;
    if (runner.running) { setMsg("Already running — use the Stop button (F7)", true); return; }
    saveAll(); string cmd = rel ? cfg.release : cfg.run;
    if (cmd.empty()) { setMsg("Empty command — configure in Project > Configure run (F9)", true); return; }
    if (kForceTerminal || cfg.terminal) {
        def_prog_mode(); endwin();
        printf("\n$ %s\n\n", cmd.c_str()); fflush(stdout);
#ifdef _WIN32
        int rc = _wsystem(u8w("\"cd /d \"" + root + "\" && " + cmd + "\"").c_str());
#else
        int rc = system(("cd '" + root + "' && " + cmd).c_str());
#endif
        printf("\n[code %d] Press Enter to return to the editor...", rc); fflush(stdout); (void)getchar();
        reset_prog_mode(); refresh(); clearok(stdscr, TRUE); return;
    }
    outLines.clear(); outScroll = -1; showOut = true; outTitle = cmd; outPush("$ " + cmd);
    if (!runnerStart(cmd)) setMsg("Failed to start the process", true);
}
static void gotoFilePos(const fs::path& fp, int line, int col) { selectPath(pstr(fp)); openFile(pstr(fp), line, col); }
static bool jumpFromLine(const string& s) {
    size_t c = s.find(':');
    while (c != string::npos) {
        if (c + 1 < s.size() && isdigit((unsigned char)s[c + 1])) {
            size_t a = s.rfind(' ', c); a = (a == string::npos) ? 0 : a + 1; string path = s.substr(a, c - a);
            int line = atoi(s.c_str() + c + 1); int col = 0; size_t c2 = s.find(':', c + 1);
            if (c2 != string::npos && c2 + 1 < s.size() && isdigit((unsigned char)s[c2 + 1])) col = atoi(s.c_str() + c2 + 1);
            fs::path fp = resolvePath(path); std::error_code ec;
            if (!path.empty() && fs::is_regular_file(fp, ec)) { gotoFilePos(fp, line, col); return true; }
        }
        c = s.find(':', c + 1);
    }
    for (size_t q = s.find('('); q != string::npos; q = s.find('(', q + 1)) {  // estilo MSVC: arquivo.cpp(12,5): error ...
        if (q + 1 >= s.size() || !isdigit((unsigned char)s[q + 1])) continue;
        size_t a = s.rfind(' ', q); a = (a == string::npos) ? 0 : a + 1; string path = s.substr(a, q - a);
        int line = atoi(s.c_str() + q + 1); int col = 0; size_t cm = s.find_first_of(",)", q + 1);
        if (cm != string::npos && s[cm] == ',' && cm + 1 < s.size() && isdigit((unsigned char)s[cm + 1])) col = atoi(s.c_str() + cm + 1);
        fs::path fp = resolvePath(path); std::error_code ec;
        if (!path.empty() && fs::is_regular_file(fp, ec)) { gotoFilePos(fp, line, col); return true; }
    }
    return false;
}

// ============================================================ desenho base
// IMPORTANTE: atributos são chtype (64 bits no PDCursesMod); guardá-los em int descarta o par de cor.
static void put(int y, int x, const string& s, chtype attr, int maxw = 100000) {
    if (y < 0 || y >= SH || x >= SW || x < 0) return;
    string t = clip(s, std::min(maxw, SW - x)); attrset(attr); mvaddnstr(y, x, t.c_str(), (int)t.size());
}
static void fillRow(int y, int x, int w, chtype attr) { if (w <= 0) return; attrset(attr); mvhline(y, x, ' ', w); }
static void fillRect(int y, int x, int h, int w, chtype attr) { for (int r = 0; r < h; r++) fillRow(y + r, x, w, attr); }
static void drawBox(int y0, int x0, int h, int w, const string& title) {
    fillRect(y0, x0, h, w, CP(P_DLG)); attrset(CP(P_DLGB));
    mvaddch(y0, x0, ACS_ULCORNER); mvhline(y0, x0 + 1, ACS_HLINE, w - 2); mvaddch(y0, x0 + w - 1, ACS_URCORNER);
    for (int r = 1; r < h - 1; r++) { mvaddch(y0 + r, x0, ACS_VLINE); mvaddch(y0 + r, x0 + w - 1, ACS_VLINE); }
    mvaddch(y0 + h - 1, x0, ACS_LLCORNER); mvhline(y0 + h - 1, x0 + 1, ACS_HLINE, w - 2); mvaddch(y0 + h - 1, x0 + w - 1, ACS_LRCORNER);
    put(y0, x0 + 2, " " + title + " ", CP(P_DLGT) | A_BOLD);
}

struct MItem { string label, key; int act; };
enum { ACT_OPEN_PROJECT = 1, ACT_NEW_FILE, ACT_NEW_DIR, ACT_SAVE, ACT_SAVE_ALL, ACT_CLOSE, ACT_QUIT, ACT_UNDO, ACT_REDO, ACT_FIND, ACT_FINDNEXT, ACT_GOTO,
       ACT_TREE, ACT_OUT, ACT_RELTOGGLE, ACT_RUN, ACT_RUN_REL, ACT_STOP, ACT_CONFIG, ACT_REFRESH, ACT_RENAME, ACT_DELETE, ACT_DIAG };
static const char* MENU_TITLES[4] = {"File", "Edit", "View", "Project"};
static vector<MItem> menuItems(int m) {
    switch (m) {
    case 0: return {{"Open project...", "Ctrl-O", ACT_OPEN_PROJECT}, {"New file...", "Ctrl-N", ACT_NEW_FILE}, {"New folder...", "", ACT_NEW_DIR},
                {"Save", "Ctrl-S", ACT_SAVE}, {"Save all", "", ACT_SAVE_ALL}, {"Close file", "Ctrl-W", ACT_CLOSE}, {"Quit", "Ctrl-Q", ACT_QUIT}};
    case 1: return {{"Undo", "u", ACT_UNDO}, {"Redo", "Ctrl-R", ACT_REDO}, {"Find...", "Ctrl-F", ACT_FIND}, {"Find next", "n", ACT_FINDNEXT}, {"Go to line...", "Ctrl-G", ACT_GOTO}};
    case 2: return {{"File explorer", "Ctrl-T", ACT_TREE}, {"Output panel", "F8", ACT_OUT}, {string("Mode: ") + (releaseMode ? "Release  (switch to Debug)" : "Debug  (switch to Release)"), "", ACT_RELTOGGLE}, {"Terminal diagnostics", "", ACT_DIAG}};
    default: return {{"Run", "F5", ACT_RUN}, {"Run (release)", "F6", ACT_RUN_REL}, {"Stop", "F7", ACT_STOP}, {"Configure run...", "F9", ACT_CONFIG}, {"Refresh explorer", "", ACT_REFRESH}};
    }
}

static void drawMenuBar() {
    fillRow(0, 0, SW, CP(P_MENU));
    string bt = runner.running ? " ■ Stop " : (releaseMode ? " ▶ Run · release " : " ▶ Run ");
    put(0, 0, bt, CP(runner.running ? P_BTN_STOP : P_BTN) | A_BOLD); btnW = ulen(bt);
    menuHits.clear(); int x = btnW + 1;
    for (int i = 0; i < 4; i++) {
        string t = string(" ") + MENU_TITLES[i] + " "; int w = ulen(t);
        put(0, x, t, CP(menuOpen == i ? P_MENU_ACT : P_MENU)); menuHits.push_back({x, x + w, i, 0}); x += w + 1;
    }
    string cs = "$ " + (releaseMode ? cfg.release : cfg.run) + " "; int w = ulen(cs);
    if (x + w < SW) put(0, SW - w, cs, CP(P_HINTBAR));
}
static void drawTabs() {
    fillRow(1, 0, SW, CP(P_BAR)); tabHits.clear(); int x = 0;
    if (bufs.empty()) { put(1, 1, "no file open", CP(P_HINTBAR)); return; }
    for (int i = 0; i < (int)bufs.size(); i++) {
        string t = " " + bufs[i].name() + (bufs[i].dirty ? " ●" : "") + " × "; int w = ulen(t); if (x + w > SW) break;
        put(1, x, t, i == cur ? (CP(P_TAB_ACT) | A_BOLD) : CP(P_TAB)); tabHits.push_back({x, x + w, i, x + w - 3}); x += w + 1;
    }
}
static void drawTree() {
    fillRect(L.edY, 0, L.outY - L.edY, L.treeW, CP(P_TEXT));
    fillRow(L.edY, 0, L.treeW, CP(P_TREEHDR)); put(L.edY, 0, " ▾ " + treeLabel(), CP(P_TREEHDR) | A_BOLD, L.treeW);
    fillRow(L.edY + 1, 0, L.treeW, CP(P_BAR)); treeBtns.clear();
    const char* lb[5] = {" +File ", " +Dir ", " Ren ", " Del ", " ↻ "}; int x = 0;
    for (int i = 0; i < 5; i++) { int w = ulen(lb[i]); if (x + w > L.treeW) break; put(L.edY + 1, x, lb[i], CP(P_BTNX)); treeBtns.push_back({x, x + w, i, 0}); x += w + 1; }
    int listY = L.edY + 2, rows = std::max(0, L.outY - listY);
    if (treeSel < treeTop) treeTop = treeSel; if (treeSel >= treeTop + rows) treeTop = treeSel - rows + 1; if (treeTop < 0) treeTop = 0;
    for (int r = 0; r < rows; r++) {
        int i = treeTop + r; if (i >= (int)tree.size()) break; auto& n = tree[i];
        chtype attr = CP(n.dir ? P_DIR : P_TEXT) | (n.dir ? A_BOLD : 0);
        if (!n.dir && n.name[0] == '.') attr = CP(P_GRAY);
        if (i == treeSel) { attr = CP(focus == F_TREE ? P_SIDE_SELF : P_SIDE_SEL) | A_BOLD; fillRow(listY + r, 0, L.treeW, attr); }
        string icon = n.dir ? (isExp(n.path) ? "▾ " : "▸ ") : "  ";
        put(listY + r, 0, string(n.depth * 2 + 1, ' ') + icon + n.name, attr, L.treeW);
    }
    attrset(CP(P_SEP)); mvvline(L.edY, L.treeW, ACS_VLINE, L.outY - L.edY);
}
static bool isSel(const Buffer& b, int y, int i) {
    if (mode == INSERT) {
        if (!hasSel(b)) return false; int y1, x1, y2, x2; selRange(b, y1, x1, y2, x2);
        if (y < y1 || y > y2) return false; if (y == y1 && i < x1) return false; if (y == y2 && i >= x2) return false; return true;
    }
    if (mode != VISUAL && mode != VLINE) return false;
    int ay = vsy, ax = vsx, by = b.cy, bx = b.cx; if (ay > by || (ay == by && ax > bx)) { std::swap(ay, by); std::swap(ax, bx); }
    if (mode == VLINE) return y >= ay && y <= by;
    if (y < ay || y > by) return false; if (y == ay && i < ax) return false; if (y == by && i > bx) return false; return true;
}
static void scrollTo(Buffer& b, int tw) {
    if (b.cy < b.top) b.top = b.cy; if (b.cy >= b.top + L.edH) b.top = b.cy - L.edH + 1; if (b.top < 0) b.top = 0;
    int vc = vcolOf(b.lines[b.cy], b.cx); if (vc < b.left) b.left = vc; if (vc >= b.left + tw) b.left = vc - tw + 1; if (b.left < 0) b.left = 0;
}
static void drawEditor() {
    fillRect(L.edY, L.edX, L.edH, L.edW, CP(P_TEXT));
    if (cur < 0) {
        int cy = L.edY + L.edH / 2 - 1;
        string a = "No file open", b2 = "Pick a file in the explorer (Ctrl-E) or use  File > New file", c2 = "F10 opens the menus   ·   F5 runs the project";
        put(cy, L.edX + std::max(0, (L.edW - ulen(a)) / 2), a, CP(P_TITLE) | A_BOLD);
        put(cy + 2, L.edX + std::max(0, (L.edW - ulen(b2)) / 2), b2, CP(P_HINT));
        put(cy + 3, L.edX + std::max(0, (L.edW - ulen(c2)) / 2), c2, CP(P_HINT)); return;
    }
    Buffer& b = bufs[cur]; int n = (int)b.lines.size();
    int digits = std::max(3, (int)std::to_string(n).size()); gGut = digits + 2; int tx = L.edX + gGut, tw = L.edW - gGut; gTX = tx;
    if (tw < 5) return; clampCursor(b); scrollTo(b, tw);
    bool inb = false; for (int y = 0; y < b.top; y++) scanLine(b.lines[y], inb, nullptr);
    for (int r = 0; r < L.edH; r++) {
        int y = b.top + r, sy = L.edY + r;
        if (y >= n) { put(sy, L.edX, " ~", CP(P_LINENO)); continue; }
        bool curl = (y == b.cy) && mode != VISUAL && mode != VLINE; int bgo = curl ? 16 : 0; const string& line = b.lines[y];
        fillRow(sy, tx, tw, CP(P_SYN + bgo));
        attrset(CP(y == b.cy ? P_LINENO_CUR : P_LINENO)); mvprintw(sy, L.edX, " %*d ", digits, y + 1);
        vector<uint8_t> col; scanLine(line, inb, &col); int vc = 0, len = (int)line.size();
        for (int i = 0; i < len;) {
            int cl = std::min(cplen((unsigned char)line[i]), len - i); int w = line[i] == '\t' ? 4 - (vc % 4) : 1;
            chtype attr = isSel(b, y, i) ? CP(P_SEL) : CP(P_SYN + bgo + col[i]);
            for (int k = 0; k < w; k++) {
                int sx = vc + k - b.left; if (sx < 0 || sx >= tw) continue; attrset(attr);
                if (line[i] == '\t') mvaddch(sy, tx + sx, ' '); else mvaddnstr(sy, tx + sx, line.c_str() + i, cl);
            }
            vc += w; i += cl;
        }
    }
    curSY = L.edY + b.cy - b.top; curSX = tx + vcolOf(b.lines[b.cy], b.cx) - b.left;
}
static void drawOut() {
    int y0 = L.outY, h = L.outH; fillRow(y0, 0, SW, CP(P_BAR));
    string t = " OUTPUT  $ " + outTitle + (runner.running ? "   ● running" : ""); put(y0, 0, t, CP(P_BAR) | A_BOLD);
    put(y0, SW - 24, " click to jump to error ", CP(P_HINTBAR));
    int rows = h - 1, total = (int)outLines.size();
    int first = outScroll < 0 ? std::max(0, total - rows) : std::min(outScroll, std::max(0, total - rows)); outFirst = first;
    fillRect(y0 + 1, 0, rows, SW, CP(P_TEXT));
    for (int r = 0; r < rows; r++) {
        int i = first + r; if (i >= total) break; const string& s = outLines[i]; chtype attr = CP(P_TEXT);
        string low; for (char c : s) low += (char)tolower((unsigned char)c);
        if (startsWith(s, "■ finished (code 0)")) attr = CP(P_OK) | A_BOLD;
        else if (startsWith(s, "■")) attr = CP(P_ERR) | A_BOLD;
        else if (startsWith(s, "$ ")) attr = CP(P_TITLE);
        else if (low.find("error") != string::npos || low.find("erro") != string::npos) attr = CP(P_ERR);
        else if (low.find("warning") != string::npos) attr = CP(P_WARN);
        else if (low.find("note:") != string::npos) attr = CP(P_NOTE);
        put(y0 + 1 + r, 1, s, attr, SW - 1);
    }
}
static void drawStatus() {
    fillRow(L.statY, 0, SW, CP(P_BAR)); string ml; int mp = P_MODE_N;
    if (focus == F_TREE) { ml = " EXPLORER "; mp = P_MODE_T; }
    else if (mode == INSERT) { ml = " INSERT "; mp = P_MODE_I; } else if (mode == VISUAL) { ml = " VISUAL "; mp = P_MODE_V; }
    else if (mode == VLINE) { ml = " VISUAL LINE "; mp = P_MODE_V; } else ml = " NORMAL ";
    put(L.statY, 0, ml, CP(mp) | A_BOLD); int x = ulen(ml) + 1;
    if (cur >= 0) { put(L.statY, x, bufs[cur].name() + (bufs[cur].dirty ? " [+]" : ""), CP(P_BAR)); }
    if (runner.running) put(L.statY, std::max(x + 24, SW / 2 - 6), "● running", CP(P_BAR) | A_BOLD);
    string right = releaseMode ? "release  " : "debug  ";
    if (cur >= 0) { Buffer& b = bufs[cur]; right += "Ln " + std::to_string(b.cy + 1) + "/" + std::to_string(b.lines.size()) + "  Col " + std::to_string(vcolOf(b.lines[b.cy], b.cx) + 1) + "  C++ "; }
    put(L.statY, SW - ulen(right), right, CP(P_BAR));
}
static void drawMsg() {
    fillRow(L.msgY, 0, SW, CP(P_TEXT));
    if (!msg.empty()) { put(L.msgY, 1, msg, CP(msgErr ? P_ERR : P_OK) | A_BOLD); return; }
    string h;
    if (focus == F_TREE) h = "Enter open · a new file · A new folder · r rename · d delete · Esc back to editor";
    else if (mode == INSERT) h = "Esc normal · Ctrl-Z/Y undo/redo · Ctrl-C/X/V copy/cut/paste · mouse selects · Ctrl-N autocomplete · Ctrl-S save";
    else if (mode == VISUAL || mode == VLINE) h = "y/Ctrl-C copy · d delete · > < indent · Esc cancel";
    else h = "i insert · v visual · dd delete line · u undo · Ctrl-F find · Ctrl-S save · F5 run · F10 menu";
    put(L.msgY, 1, h, CP(P_HINT));
}
static void drawMenuDropdown() {
    if (menuOpen < 0) { dropRect = {0, 0, 0, 0}; return; }
    auto items = menuItems(menuOpen); int w = 0; for (auto& it : items) w = std::max(w, ulen(it.label) + ulen(it.key) + 6);
    int x = menuHits[menuOpen].x0, y = 1; if (x + w > SW) x = std::max(0, SW - w);
    dropRect = {x, y, w, (int)items.size() + 2};
    drawBox(y, x, (int)items.size() + 2, w, "");
    for (int i = 0; i < (int)items.size(); i++) {
        chtype attr = CP(i == menuSel ? P_POPSEL : P_DLG); fillRow(y + 1 + i, x + 1, w - 2, attr);
        put(y + 1 + i, x + 2, items[i].label, attr); put(y + 1 + i, x + w - 2 - ulen(items[i].key), items[i].key, CP(i == menuSel ? P_POPSEL : P_DLGB));
    }
}
static void drawPopup() {
    if (!pop.on || cur < 0 || focus != F_EDITOR || mode != INSERT || menuOpen >= 0) return;
    Buffer& b = bufs[cur]; int total = (int)pop.items.size(), n = std::min(total, 8), w = 0;
    for (auto& s : pop.items) w = std::max(w, std::min(ulen(s) + 2, 42)); w = std::max(w, 12);
    int x = gTX + vcolOf(b.lines[b.cy], pop.start) - b.left; if (x + w > SW) x = std::max(0, SW - w);
    int y = curSY + 1; if (y + n > L.edY + L.edH) y = curSY - n; if (y < L.edY) y = curSY + 1;
    int first = pop.sel >= n ? pop.sel - n + 1 : 0;
    for (int i = 0; i < n; i++) {
        int idx = first + i; chtype attr = CP(idx == pop.sel ? P_POPSEL : P_POP); fillRow(y + i, x, w, attr); put(y + i, x + 1, pop.items[idx], attr, w - 2);
    }
}
static void drawStart() {
    fillRect(0, 0, SH, SW, CP(P_TEXT));
    int cy = std::max(1, SH / 2 - 8); string t1 = "r s f v i m", t2 = "modal code editor · gruvbox-dark theme", t3 = "Pick the project folder to get started";
    put(cy, (SW - ulen(t1)) / 2, t1, CP(P_TITLE) | A_BOLD); put(cy + 1, (SW - ulen(t2)) / 2, t2, CP(P_HINT)); put(cy + 2, (SW - ulen(t3)) / 2, t3, CP(P_HINT));
}
static void render() {
    layout();
    if (root.empty()) { drawStart(); return; }
    erase(); drawMenuBar(); drawTabs(); if (showTree) drawTree(); drawEditor(); if (showOut) drawOut(); drawStatus(); drawMsg(); drawMenuDropdown(); drawPopup();
}
static void drawAll() {
    render();
    if (!root.empty() && focus == F_EDITOR && cur >= 0 && menuOpen < 0) { curs_set(1); move(curSY, curSX); } else curs_set(0);
    refresh();
}

// ============================================================ diálogos (UI)
struct DField { string label, text; bool isCheck = false, check = false; };
// Retorna índice do botão pressionado, ou -1 (Esc).
static int dialog(const string& title, const string& body, vector<DField>& f, const vector<string>& btns, int defBtn = 0) {
    int nf = (int)f.size(), nb = (int)btns.size(), foc = nf > 0 ? 0 : nf + defBtn; vector<string> bl = body.empty() ? vector<string>() : splitLines(body);
    menuOpen = -1;
    for (;;) {
        runnerPoll(); render();
        int bw = 0; for (auto& b : btns) bw += ulen(b) + 5; int bodyW = 0; for (auto& s : bl) bodyW = std::max(bodyW, ulen(s));
        int w = std::min(SW - 2, std::max({54, bw + 6, bodyW + 6})); int rows = 0; for (auto& d : f) rows += d.isCheck ? 2 : 3;
        int h = 2 + (bl.empty() ? 0 : (int)bl.size() + 1) + rows + 2; int y0 = std::max(0, (SH - h) / 2), x0 = std::max(0, (SW - w) / 2);
        drawBox(y0, x0, h, w, title); int y = y0 + 1;
        for (auto& s : bl) put(y++, x0 + 2, s, CP(P_DLG), w - 4); if (!bl.empty()) y++;
        struct R { int y, x, w, idx; }; vector<R> frs, brs; int curX = -1, curY = -1;
        for (int i = 0; i < nf; i++) {
            if (f[i].isCheck) {
                bool fo = foc == i; string t = string(f[i].check ? "[x] " : "[ ] ") + f[i].label; fillRow(y, x0 + 2, w - 4, CP(fo ? P_INPF : P_DLG));
                put(y, x0 + 2, t, CP(fo ? P_INPF : P_DLG)); frs.push_back({y, x0 + 2, w - 4, i}); y += 2;
            } else {
                put(y, x0 + 2, f[i].label, CP(P_DLGT)); y++; int iw = w - 4; fillRow(y, x0 + 2, iw, CP(foc == i ? P_INPF : P_INP));
                string shown = tailClip(f[i].text, iw - 2); put(y, x0 + 3, shown, CP(foc == i ? P_INPF : P_INP), iw - 2);
                if (foc == i) { curY = y; curX = x0 + 3 + ulen(shown); } frs.push_back({y, x0 + 2, iw, i}); y += 2;
            }
        }
        int bx = x0 + std::max(2, (w - bw) / 2), by = y0 + h - 2;
        for (int i = 0; i < nb; i++) {
            string t = "  " + btns[i] + "  "; bool fo = foc == nf + i; put(by, bx, t, CP(fo ? P_POPSEL : P_BTNX) | (fo ? A_BOLD : 0));
            brs.push_back({by, bx, ulen(t), i}); bx += ulen(t) + 2;
        }
        if (curX >= 0) { curs_set(1); move(curY, curX); } else curs_set(0);
        refresh(); int ch = readKey(50);
        if (ch == ERR || ch == KEY_RESIZE) continue;
        int total = nf + nb;
        if (ch == KEY_MOUSE) {
            MEVENT ev; if (getmouse(&ev) != OK) continue; if (!(ev.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED))) continue;
            for (auto& r : frs) if (ev.y == r.y && ev.x >= r.x && ev.x < r.x + r.w) { foc = r.idx; if (f[r.idx].isCheck) f[r.idx].check = !f[r.idx].check; }
            for (auto& r : brs) if (ev.y == r.y && ev.x >= r.x && ev.x < r.x + r.w) return r.idx;
            continue;
        }
        if (ch == 27) return -1;
        if (ch == 9 || ch == KEY_DOWN) { foc = (foc + 1) % total; continue; }
        if (ch == KEY_BTAB || ch == KEY_UP) { foc = (foc + total - 1) % total; continue; }
        bool onBtn = foc >= nf;
        if (onBtn) {
            if (ch == KEY_LEFT) { if (foc > nf) foc--; } else if (ch == KEY_RIGHT) { if (foc < total - 1) foc++; }
            else if (ch == 10 || ch == 13 || ch == KEY_ENTER || ch == ' ') return foc - nf;
            continue;
        }
        DField& d = f[foc];
        if (d.isCheck) { if (ch == ' ' || ch == 10 || ch == 13) d.check = !d.check; continue; }
        if (ch == 10 || ch == 13 || ch == KEY_ENTER) return defBtn;
        if (ch == 127 || ch == 8 || ch == KEY_BACKSPACE) { if (!d.text.empty()) d.text.erase(prevCp(d.text, (int)d.text.size())); }
        else if (ch == 21) d.text.clear();
        else if (ch >= 32 && ch < 256 && ch != 127) d.text += (char)ch;
    }
}
static int msgBox(const string& title, const string& body, const vector<string>& btns) { vector<DField> none; return dialog(title, body, none, btns, 0); }

#ifdef _WIN32
// Discos disponíveis: {"C:\\", "Local disk"} ...
static vector<std::pair<string, string>> listDrives() {
    vector<std::pair<string, string>> v; unsigned long m = GetLogicalDrives();
    for (int i = 0; i < 26; i++) if (m & (1ul << i)) {
            string r = string(1, (char)('A' + i)) + ":\\"; unsigned t = GetDriveTypeA(r.c_str());
            const char* n = t == 2 ? "Removable drive" : t == 3 ? "Local disk" : t == 4 ? "Network drive" : t == 5 ? "CD/DVD" : t == 6 ? "RAM disk" : "Disk";
            v.push_back({r, n});
        }
    return v;
}
#endif
struct PItem { string label; fs::path path; int kind = 0; };  // kind 0 = pasta/disco, 1 = ir para a lista de discos (Windows)
static bool skipDirName(const string& n) {
    if (n.empty() || n[0] == '.') return true;
#ifdef _WIN32
    if (n[0] == '$' || n == "System Volume Information" || n == "Config.Msi") return true;
#endif
    return false;
}
// No Windows, startDir vazio abre direto na lista de discos; de qualquer pasta "raiz" (C:\) o ".." também leva a ela.
static bool pickFolder(const string& startDir, string& out) {
    std::error_code ec0; fs::path curp = startDir.empty() ? fs::current_path(ec0) : toPath(startDir);
    int sel = 0, top = 0, foc = 0; menuOpen = -1; bool atDrives = false; string wantDrive;
#ifdef _WIN32
    if (startDir.empty()) { atDrives = true; wantDrive = pstr(curp.root_name()); }
#endif
    auto isRoot = [&]() { return !curp.has_parent_path() || curp == curp.root_path(); };
    for (;;) {
        vector<PItem> items; std::error_code ec;
#ifdef _WIN32
        if (atDrives) {
            for (auto& d : listDrives()) {
                items.push_back({d.first + "   " + d.second, toPath(d.first), 0});
                if (!wantDrive.empty() && startsWith(upperA(d.first), upperA(wantDrive))) { sel = (int)items.size() - 1; wantDrive.clear(); }
            }
            items.push_back({"~  (home folder)", toPath(homeDir()), 0});
            items.push_back({".  (current folder: " + pstr(fs::current_path(ec)) + ")", fs::current_path(ec), 0});
        } else
#endif
        {
            if (!isRoot()) items.push_back({"..", curp.parent_path(), 0});
#ifdef _WIN32
            else items.push_back({"..  (drive list)", fs::path(), 1});
            items.push_back({"Drives...  (switch drive)", fs::path(), 1});
#endif
            items.push_back({"~  (home folder)", toPath(homeDir()), 0});
            vector<std::pair<string, fs::path>> dirs;
            for (auto& e : listDir(curp)) { std::error_code e2; string nm = pstr(e.path().filename()); if (e.is_directory(e2) && !skipDirName(nm)) dirs.push_back({lowerA(nm), e.path()}); }
            std::sort(dirs.begin(), dirs.end(), [](const std::pair<string, fs::path>& a, const std::pair<string, fs::path>& b) { return a.first < b.first; });
            for (auto& d : dirs) items.push_back({pstr(d.second.filename()) + SEP, d.second, 0});
        }
        if (sel >= (int)items.size()) sel = (int)items.size() - 1; if (sel < 0) sel = 0;
        runnerPoll(); render();
        int w = std::min(SW - 4, 72), h = std::min(SH - 2, 24); int y0 = std::max(0, (SH - h) / 2), x0 = std::max(0, (SW - w) / 2);
        drawBox(y0, x0, h, w, atDrives ? "Open project — choose a drive" : "Open project — choose a folder");
        put(y0 + 1, x0 + 2, atDrives ? string("This computer — available drives") : tailClip(pstr(curp), w - 4), CP(P_DLGT) | A_BOLD, w - 4);
#ifdef _WIN32
        put(y0 + 2, x0 + 2, "↑↓ move · Enter opens · ← up · Ctrl-D drives · letter = drive · Tab", CP(P_DLGB), w - 4);
#else
        put(y0 + 2, x0 + 2, "↑↓ move · Enter/click opens · ← up · Tab buttons", CP(P_DLGB), w - 4);
#endif
        int ly = y0 + 3, lrows = h - 6; if (lrows < 1) lrows = 1;
        if (sel < top) top = sel; if (sel >= top + lrows) top = sel - lrows + 1;
        for (int r = 0; r < lrows; r++) {
            int i = top + r; if (i >= (int)items.size()) break; chtype attr = CP((i == sel && foc == 0) ? P_POPSEL : (i == sel ? P_INPF : P_DLG));
            fillRow(ly + r, x0 + 1, w - 2, attr); put(ly + r, x0 + 2, items[i].label, attr, w - 4);
        }
        string b1 = atDrives ? "  Enter drive  " : "  Open this folder  ", b2 = "  Cancel  "; int bx = x0 + 2, by = y0 + h - 2;
        put(by, bx, b1, CP(foc == 1 ? P_POPSEL : P_BTN) | A_BOLD); put(by, bx + ulen(b1) + 2, b2, CP(foc == 2 ? P_POPSEL : P_BTNX) | A_BOLD);
        curs_set(0); refresh(); int ch = readKey(50);
        if (ch == ERR || ch == KEY_RESIZE) continue;
        auto gotoDrives = [&]() {
#ifdef _WIN32
            wantDrive = pstr(curp.root_name()); atDrives = true; sel = 0; top = 0;
#endif
        };
        auto enter = [&]() {
            if (sel >= (int)items.size()) return; PItem it = items[sel];
            if (it.kind == 1) { gotoDrives(); return; }
            curp = it.path; atDrives = false; sel = 0; top = 0;
        };
        auto goUp = [&]() {
            if (atDrives) return;
            if (!isRoot()) { curp = curp.parent_path(); sel = 0; top = 0; }
#ifdef _WIN32
            else gotoDrives();
#endif
        };
        auto accept = [&]() -> bool { if (atDrives) { enter(); return false; } out = pstr(curp); return true; };
        if (ch == KEY_MOUSE) {
            MEVENT ev; if (getmouse(&ev) != OK) continue;
            if (ev.bstate & BUTTON4_PRESSED) { sel = std::max(0, sel - 3); continue; } if (ev.bstate & BUTTON5_PRESSED) { sel = std::min((int)items.size() - 1, sel + 3); continue; }
            if (!(ev.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED))) continue;
            if (ev.y == by && ev.x >= bx && ev.x < bx + ulen(b1)) { if (accept()) return true; continue; }
            if (ev.y == by && ev.x >= bx + ulen(b1) + 2 && ev.x < bx + ulen(b1) + 2 + ulen(b2)) return false;
            if (ev.y >= ly && ev.y < ly + lrows && ev.x > x0 && ev.x < x0 + w - 1) { int i = top + ev.y - ly; if (i < (int)items.size()) { sel = i; foc = 0; enter(); } }
            continue;
        }
        if (ch == 27) return false;
        if (ch == 9) { foc = (foc + 1) % 3; continue; } if (ch == KEY_BTAB) { foc = (foc + 2) % 3; continue; }
        if (foc == 1 && (ch == 10 || ch == 13 || ch == KEY_ENTER || ch == ' ')) { if (accept()) return true; continue; }
        if (foc == 2 && (ch == 10 || ch == 13 || ch == KEY_ENTER || ch == ' ')) return false;
#ifdef _WIN32
        if (ch == 4) { gotoDrives(); continue; }  // Ctrl-D
        if (atDrives && ch < 128 && isalpha(ch)) {  // digitar a letra do disco seleciona-o
            for (int i = 0; i < (int)items.size(); i++) if (items[i].label.size() > 1 && items[i].label[1] == ':' && toupper(items[i].label[0]) == toupper(ch)) { sel = i; break; }
            continue;
        }
#endif
        if (ch == KEY_UP) sel = std::max(0, sel - 1); else if (ch == KEY_DOWN) sel = std::min((int)items.size() - 1, sel + 1);
        else if (ch == KEY_PPAGE) sel = std::max(0, sel - lrows); else if (ch == KEY_NPAGE) sel = std::min((int)items.size() - 1, sel + lrows);
        else if (ch == KEY_HOME) sel = 0; else if (ch == KEY_END) sel = (int)items.size() - 1;
        else if (ch == KEY_LEFT || ch == KEY_BACKSPACE || ch == 127 || ch == 8) goUp();
        else if (ch == 10 || ch == 13 || ch == KEY_ENTER || ch == KEY_RIGHT) { if (foc == 0) enter(); }
    }
}

// ============================================================ autocomplete
static bool isWordCh(char c) { return isalnum((unsigned char)c) || c == '_' || (unsigned char)c >= 128; }
static void updatePopup(Buffer& b, bool force) {
    pop.on = false; pop.items.clear(); pop.sel = 0; const string& l = b.lines[b.cy]; int cx = b.cx;
    size_t s = l.find_first_not_of(" \t");
    if (s != string::npos && l.compare(s, 8, "#include") == 0 && (int)(s + 8) <= cx) {
        int pos = -1; for (int i = cx - 1; i >= (int)s + 8; i--) { if (l[i] == '<' || l[i] == '"') { pos = i; break; } if (l[i] == '>') break; }
        if (pos < 0) return; string pre = l.substr(pos + 1, cx - pos - 1);
        vector<string> a, c; for (auto& h : HDRS) { size_t p = h.find(pre); if (p == 0) a.push_back(h); else if (p != string::npos) c.push_back(h); }
        for (auto& h : a) pop.items.push_back(h); for (auto& h : c) pop.items.push_back(h);
        if (pop.items.size() > 60) pop.items.resize(60); pop.start = pos + 1; pop.on = !pop.items.empty(); return;
    }
    int st = cx; while (st > 0 && (isWordCh(l[st - 1]) || l[st - 1] == ':')) st--;
    while (st < cx && l[st] == ':' && !(st + 1 < cx && l[st + 1] == ':' && st > 0 && isWordCh(l[st - 1]))) { if (st + 1 < cx && l[st + 1] == ':') break; st++; }
    if (st > 0 && l[st - 1] == '#') st--;
    string pre = l.substr(st, cx - st); size_t minLen = force ? 1 : 2;
    if (pre.size() < minLen || isdigit((unsigned char)pre[0])) return;
    std::set<string> seen; vector<string> prim, sec;
    for (auto& w : DICT) if (w != pre && startsWith(w, pre)) { prim.push_back(w); seen.insert(w); }
    string stdpre = "std::" + pre; for (auto& w : DICT) if (!seen.count(w) && startsWith(w, stdpre)) { sec.push_back(w); seen.insert(w); }
    std::set<string> bw;
    for (auto& line : b.lines) {
        size_t i = 0; while (i < line.size()) {
            if (isWordCh(line[i]) && !isdigit((unsigned char)line[i])) { size_t j = i; while (j < line.size() && isWordCh(line[j])) j++; string w = line.substr(i, j - i); if (w.size() > pre.size() && startsWith(w, pre)) bw.insert(w); i = j; }
            else i++;
        }
    }
    for (auto& w : bw) if (!seen.count(w)) prim.push_back(w);
    for (auto& w : sec) prim.push_back(w);
    if (prim.size() > 60) prim.resize(60); pop.items = prim; pop.start = st; pop.on = !pop.items.empty();
}
static void acceptPopup(Buffer& b) {
    if (!pop.on) return; string item = pop.items[pop.sel]; string& l = b.lines[b.cy]; char open = pop.start > 0 ? l[pop.start - 1] : 0;
    l.replace(pop.start, b.cx - pop.start, item); b.cx = pop.start + (int)item.size();
    if (open == '<' && (b.cx >= (int)l.size() || l[b.cx] != '>')) { l.insert(b.cx, ">"); b.cx++; }
    else if (open == '"' && (b.cx >= (int)l.size() || l[b.cx] != '"')) { l.insert(b.cx, "\""); b.cx++; }
    pop.on = false;
}

// ============================================================ edição (teclas)
static void enterInsert(Buffer& b) { snap(b); mode = INSERT; pop.on = false; insSel = false; insKind = 0; }
static void leaveInsert(Buffer& b) {
    if (!b.undo.empty() && b.undo.back().lines == b.lines) { b.dirty = b.undo.back().dirty; b.undo.pop_back(); }
    mode = NORMAL; pop.on = false; insSel = false; insKind = 0; if (b.cx > 0) b.cx = prevCp(b.lines[b.cy], b.cx); clampCursor(b); setWant(b);
}
static void insText(Buffer& b, const string& t) { b.lines[b.cy].insert(b.cx, t); b.cx += (int)t.size(); }
static void insNewline(Buffer& b) {
    string before = b.lines[b.cy].substr(0, b.cx), rest = b.lines[b.cy].substr(b.cx), ind; size_t k = 0;
    while (k < before.size() && (before[k] == ' ' || before[k] == '\t')) ind += before[k++];
    size_t e = before.find_last_not_of(" \t"); char oc = e != string::npos ? before[e] : 0; bool open = oc == '{';
    bool closeNext = !rest.empty() && ((oc == '{' && rest[0] == '}') || (oc == '(' && rest[0] == ')') || (oc == '[' && rest[0] == ']'));
    b.lines[b.cy] = before;
    if (closeNext) { b.lines.insert(b.lines.begin() + b.cy + 1, ind + "    "); b.lines.insert(b.lines.begin() + b.cy + 2, ind + rest); b.cy++; b.cx = (int)ind.size() + 4; }
    else { if (open) ind += "    "; size_t ws = rest.find_first_not_of(" \t"); string r2 = ws == string::npos ? "" : rest.substr(ws); b.lines.insert(b.lines.begin() + b.cy + 1, ind + r2); b.cy++; b.cx = (int)ind.size(); }
}
static void backspace(Buffer& b) {
    if (b.cx > 0) { string& l = b.lines[b.cy]; int p = prevCp(l, b.cx); l.erase(p, b.cx - p); b.cx = p; }
    else if (b.cy > 0) { int px = (int)b.lines[b.cy - 1].size(); b.lines[b.cy - 1] += b.lines[b.cy]; b.lines.erase(b.lines.begin() + b.cy); b.cy--; b.cx = px; }
}
// ---- auto-fechamento de ( [ { " '
static bool autoPair(Buffer& b, int ch) {  // true = tecla tratada aqui
    bool isOpen = ch == '(' || ch == '[' || ch == '{' || ch == '"' || ch == '\'', isClose = ch == ')' || ch == ']' || ch == '}';
    if (!isOpen && !isClose) return false;
    char close = ch == '(' ? ')' : ch == '[' ? ']' : ch == '{' ? '}' : (char)ch;
    if (hasSel(b)) {  // com seleção: envolve o texto selecionado
        if (!isOpen) return false;
        string t = selText(b); beginEdit(b, 3); insPasteText(b, string(1, (char)ch) + t + close); pop.on = false; setWant(b); return true;
    }
    const string& l = b.lines[b.cy]; char nx = b.cx < (int)l.size() ? l[b.cx] : 0, pv = b.cx > 0 ? l[b.cx - 1] : 0;
    if ((isClose || ch == '"' || ch == '\'') && nx == ch) { b.cx++; pop.on = false; setWant(b); return true; }  // pula o fechamento já existente
    if (!isOpen) return false;
    bool nxOk = nx == 0 || isspace((unsigned char)nx) || strchr(")]};,", nx);
    if (!nxOk) return false;
    if ((ch == '"' || ch == '\'') && (isWordCh(pv) || pv == '\\')) return false;  // don't, 1'000, "\"
    beginEdit(b, 1); string& l2 = b.lines[b.cy]; l2.insert(b.cx, string(1, (char)ch) + close); b.cx++; pop.on = false; setWant(b); return true;
}
static bool pairBackspace(Buffer& b) {  // Backspace entre um par vazio apaga os dois
    const string& l = b.lines[b.cy]; if (b.cx <= 0 || b.cx >= (int)l.size()) return false;
    char a = l[b.cx - 1], c = l[b.cx];
    if (!((a == '(' && c == ')') || (a == '[' && c == ']') || (a == '{' && c == '}') || (a == '"' && c == '"') || (a == '\'' && c == '\''))) return false;
    beginEdit(b, 2); string& l2 = b.lines[b.cy]; l2.erase(b.cx - 1, 2); b.cx--; setWant(b); return true;
}
static void insertKey(int ch) {
    Buffer& b = bufs[cur]; int n = (int)pop.items.size();
    if (!hasSel(b)) insSel = false;
    switch (ch) {
    case 27: leaveInsert(b); return;
    case 26: pop.on = false; insSel = false; insKind = 0; doUndo(); return;   // Ctrl-Z
    case 25: pop.on = false; insSel = false; insKind = 0; doRedo(); return;   // Ctrl-Y
    case 3: insCopy(b); return;                                               // Ctrl-C
    case 24: insCut(b); return;                                               // Ctrl-X
    case 22: insPaste(b); return;                                             // Ctrl-V
    case 14: if (pop.on) pop.sel = (pop.sel + 1) % n; else updatePopup(b, true); return;
    case 16: if (pop.on) pop.sel = (pop.sel + n - 1) % n; else updatePopup(b, true); return;
    case 9: if (pop.on) { beginEdit(b, 3); acceptPopup(b); } else { beginEdit(b, 3); insText(b, "    "); } return;
    case 10: case 13: case KEY_ENTER: pop.on = false; beginEdit(b, 3); insNewline(b); return;
    case 127: case 8: case KEY_BACKSPACE:
        if (hasSel(b)) { pop.on = false; beginEdit(b, 3); return; }
        if (pairBackspace(b)) { pop.on = false; return; }
        beginEdit(b, 2); backspace(b); updatePopup(b, false); return;
    case KEY_DC: {
        pop.on = false; if (hasSel(b)) { beginEdit(b, 3); return; }
        beginEdit(b, 2); string& l = b.lines[b.cy];
        if (b.cx < (int)l.size()) l.erase(b.cx, nextCp(l, b.cx) - b.cx); else if (b.cy + 1 < (int)b.lines.size()) { l += b.lines[b.cy + 1]; b.lines.erase(b.lines.begin() + b.cy + 1); } return; }
    case 23: {
        pop.on = false; if (hasSel(b)) { beginEdit(b, 3); return; }
        beginEdit(b, 2); string& l = b.lines[b.cy]; int i = b.cx; while (i > 0 && clsOf(l[i - 1]) == 0) i--; if (i > 0) { int c = clsOf(l[i - 1]); while (i > 0 && clsOf(l[i - 1]) == c) i--; } l.erase(i, b.cx - i); b.cx = i; return; }
    case KEY_LEFT: pop.on = false; insSel = false; insKind = 0; b.cx = prevCp(b.lines[b.cy], b.cx); setWant(b); return;
    case KEY_RIGHT: pop.on = false; insSel = false; insKind = 0; b.cx = nextCp(b.lines[b.cy], b.cx); setWant(b); return;
    case KEY_UP: if (pop.on) { pop.sel = (pop.sel + n - 1) % n; return; } insSel = false; insKind = 0; b.cy = std::max(0, b.cy - 1); b.cx = cxFromV(b.lines[b.cy], b.want); return;
    case KEY_DOWN: if (pop.on) { pop.sel = (pop.sel + 1) % n; return; } insSel = false; insKind = 0; b.cy = std::min((int)b.lines.size() - 1, b.cy + 1); b.cx = cxFromV(b.lines[b.cy], b.want); return;
    case KEY_HOME: pop.on = false; insSel = false; insKind = 0; b.cx = 0; setWant(b); return;
    case KEY_END: pop.on = false; insSel = false; insKind = 0; b.cx = (int)b.lines[b.cy].size(); setWant(b); return;
    default:
        if (ch >= 32 && ch < 256 && ch != 127) {
            if (insKind == 1 && lastSp && ch != ' ') insKind = 0;  // nova palavra = novo ponto de desfazer
            lastSp = (ch == ' ');
            if (autoPair(b, ch)) return;
            beginEdit(b, 1);
            if (ch == '}') { string& l = b.lines[b.cy]; if (b.cx > 0 && l.find_first_not_of(" \t") == string::npos) { if (l.size() >= 4 && l.compare(l.size() - 4, 4, "    ") == 0) { l.erase(l.size() - 4); b.cx -= 4; } else if (l.back() == '\t') { l.pop_back(); b.cx--; } } }
            insText(b, string(1, (char)ch)); setWant(b); updatePopup(b, false);
        }
    }
}

static int cnt = 0; static char pend = 0; static int pendN = 1;
static void moveVert(Buffer& b, int d) { b.cy = std::max(0, std::min((int)b.lines.size() - 1, b.cy + d)); b.cx = cxFromV(b.lines[b.cy], b.want); clampCursor(b); }
static void doFind() {
    if (cur < 0) return; vector<DField> f = {{"Search for:", lastSearch}};
    if (dialog("Find", "", f, {"Find", "Cancel"}) != 0 || f[0].text.empty()) return;
    lastSearch = f[0].text; Buffer& b = bufs[cur];
    if (searchDir(b, lastSearch, true)) { b.top = std::max(0, b.cy - L.edH / 2); setWant(b); focus = F_EDITOR; } else setMsg("Not found: " + lastSearch, true);
}
static void doGoto() {
    if (cur < 0) return; vector<DField> f = {{"Line number:", ""}};
    if (dialog("Go to line", "Lines: 1 to " + std::to_string(bufs[cur].lines.size()), f, {"Go", "Cancel"}) != 0) return;
    int n = atoi(f[0].text.c_str()); if (n < 1) return; Buffer& b = bufs[cur]; b.cy = std::min(n - 1, (int)b.lines.size() - 1); b.cx = firstNonBlank(b.lines[b.cy]);
    clampCursor(b); b.top = std::max(0, b.cy - L.edH / 2); setWant(b); focus = F_EDITOR;
}
static bool motionKey(Buffer& b, int ch, int n, bool hadCount) {
    string& l = b.lines[b.cy];
    switch (ch) {
    case 'h': case KEY_LEFT: for (int i = 0; i < n; i++) b.cx = prevCp(l, b.cx); setWant(b); return true;
    case 'l': case KEY_RIGHT: case ' ': for (int i = 0; i < n; i++) if (nextCp(l, b.cx) < (int)l.size()) b.cx = nextCp(l, b.cx); setWant(b); return true;
    case 'j': case KEY_DOWN: moveVert(b, n); return true;
    case 'k': case KEY_UP: moveVert(b, -n); return true;
    case '0': case KEY_HOME: b.cx = 0; setWant(b); return true;
    case '$': case KEY_END: b.cx = l.empty() ? 0 : prevCp(l, (int)l.size()); b.want = 1 << 28; return true;
    case '^': b.cx = firstNonBlank(l); setWant(b); return true;
    case 'w': for (int i = 0; i < n; i++) moveW(b); setWant(b); return true;
    case 'b': for (int i = 0; i < n; i++) moveB(b); setWant(b); return true;
    case 'e': for (int i = 0; i < n; i++) moveE(b); setWant(b); return true;
    case 'G': b.cy = hadCount ? std::min(n - 1, (int)b.lines.size() - 1) : (int)b.lines.size() - 1; b.cx = firstNonBlank(b.lines[b.cy]); setWant(b); return true;
    case '}': { int y = b.cy; while (y + 1 < (int)b.lines.size() && b.lines[y].empty()) y++; while (y + 1 < (int)b.lines.size() && !b.lines[y].empty()) y++; b.cy = y; b.cx = 0; setWant(b); return true; }
    case '{': { int y = b.cy; while (y > 0 && b.lines[y].empty()) y--; while (y > 0 && !b.lines[y].empty()) y--; b.cy = y; b.cx = 0; setWant(b); return true; }
    case '%': matchBracket(b); setWant(b); return true;
    case 'n': case 'N': if (lastSearch.empty()) { setMsg("Use Ctrl-F to search", true); return true; } if (!searchDir(b, lastSearch, ch == 'n')) setMsg("Not found: " + lastSearch, true); setWant(b); return true;
    case 4: moveVert(b, L.edH / 2); return true;
    case 21: moveVert(b, -L.edH / 2); return true;
    case KEY_NPAGE: moveVert(b, L.edH); return true;
    case KEY_PPAGE: moveVert(b, -L.edH); return true;
    }
    return false;
}
static int endW(const string& l, int i, int n, bool keepSpaces) {
    int len = (int)l.size();
    for (int k = 0; k < n && i < len; k++) {
        int c = clsOf(l[i]); if (c != 0) while (i < len && clsOf(l[i]) == c) i++; else while (i < len && clsOf(l[i]) == 0) i++;
        if (!keepSpaces) while (i < len && clsOf(l[i]) == 0) i++;
    }
    return i;
}
static void opRange(Buffer& b, int x1, int x2, int op) {  // 0 apagar, 1 copiar, 2 mudar
    string& l = b.lines[b.cy]; x2 = std::min(x2, (int)l.size()); if (x2 <= x1) { if (op == 2) mode = INSERT; return; }
    reg = l.substr(x1, x2 - x1); regLine = false;
    if (op != 1) { l.erase(x1, x2 - x1); b.cx = x1; }
    if (op == 2) mode = INSERT; else clampCursor(b);
}
static void pendCmd(Buffer& b, char p, int ch, int n) {
    string& l = b.lines[b.cy];
    switch (p) {
    case 'd':
        if (ch == 'd') { snap(b); delLines(b, b.cy, n); }
        else if (ch == 'w') { snap(b); opRange(b, b.cx, endW(l, b.cx, n, false), 0); }
        else if (ch == '$') { snap(b); opRange(b, b.cx, (int)l.size(), 0); } else if (ch == '0') { snap(b); opRange(b, 0, b.cx, 0); }
        break;
    case 'y':
        if (ch == 'y') { yankLines(b, b.cy, n); setMsg(std::to_string(std::min(n, (int)b.lines.size() - b.cy)) + " line(s) copied"); }
        else if (ch == 'w') opRange(b, b.cx, endW(l, b.cx, n, false), 1); else if (ch == '$') opRange(b, b.cx, (int)l.size(), 1);
        break;
    case 'c':
        if (ch == 'c') { snap(b); yankLines(b, b.cy, 1); int k = 0; while (k < (int)l.size() && (l[k] == ' ' || l[k] == '\t')) k++; l = l.substr(0, k); b.cx = k; mode = INSERT; }
        else if (ch == 'w') { snap(b); opRange(b, b.cx, endW(l, b.cx, n, true), 2); } else if (ch == '$') { snap(b); opRange(b, b.cx, (int)l.size(), 2); }
        break;
    case 'g':
        if (ch == 'g') { b.cy = n > 1 ? std::min(n - 1, (int)b.lines.size() - 1) : 0; b.cx = firstNonBlank(b.lines[b.cy]); setWant(b); }
        else if (ch == 't' && bufs.size() > 1) { cur = (cur + 1) % (int)bufs.size(); }
        else if (ch == 'T' && bufs.size() > 1) { cur = (cur + (int)bufs.size() - 1) % (int)bufs.size(); }
        break;
    case 'r':
        if (ch >= 32 && ch < 127 && b.cx < (int)l.size()) { snap(b); for (int i = 0; i < n && b.cx + i < (int)l.size(); i++) l[b.cx + i] = (char)ch; }
        break;
    case '>': if (ch == '>') indentLines(b, b.cy, n, true); break;
    case '<': if (ch == '<') indentLines(b, b.cy, n, false); break;
    case 'z': if (ch == 'z') b.top = std::max(0, b.cy - L.edH / 2); break;
    }
    if (cur >= 0 && cur < (int)bufs.size()) { Buffer& bb = bufs[cur]; clampCursor(bb); if (p != 'd' || ch != 'd') setWant(bb); }
}
static void normalKey(int ch) {
    Buffer& b = bufs[cur];
    if (pend) { char p = pend; pend = 0; pendCmd(b, p, ch, pendN); return; }
    if ((ch >= '1' && ch <= '9') || (ch == '0' && cnt > 0)) { cnt = cnt * 10 + (ch - '0'); return; }
    int n = cnt ? cnt : 1; bool had = cnt > 0; cnt = 0;
    if (motionKey(b, ch, n, had)) return;
    string& l = b.lines[b.cy];
    switch (ch) {
    case 'i': enterInsert(b); break;
    case 'a': if (!l.empty()) b.cx = nextCp(l, b.cx); enterInsert(b); break;
    case 'I': b.cx = firstNonBlank(l); if (l.find_first_not_of(" \t") == string::npos) b.cx = (int)l.size(); enterInsert(b); break;
    case 'A': b.cx = (int)l.size(); enterInsert(b); break;
    case 'o': case 'O': {
        snap(b); string ind; for (char c : l) { if (c == ' ' || c == '\t') ind += c; else break; }
        size_t e = l.find_last_not_of(" \t"); if (ch == 'o' && e != string::npos && l[e] == '{') ind += "    ";
        int pos = b.cy + (ch == 'o' ? 1 : 0); b.lines.insert(b.lines.begin() + pos, ind); b.cy = pos; b.cx = (int)ind.size(); mode = INSERT; break;
    }
    case 'x': if (!l.empty()) { snap(b); int e = b.cx; for (int i = 0; i < n; i++) e = nextCp(l, e); reg = l.substr(b.cx, e - b.cx); regLine = false; l.erase(b.cx, e - b.cx); clampCursor(b); } break;
    case 'D': snap(b); opRange(b, b.cx, (int)l.size(), 0); break;
    case 'C': snap(b); opRange(b, b.cx, (int)l.size(), 2); break;
    case 'J': if (b.cy + 1 < (int)b.lines.size()) { snap(b); string nx = b.lines[b.cy + 1]; size_t k = nx.find_first_not_of(" \t"); nx = k == string::npos ? "" : nx.substr(k); b.cx = (int)l.size(); if (!l.empty() && !nx.empty()) l += " "; l += nx; b.lines.erase(b.lines.begin() + b.cy + 1); } break;
    case 'p': snap(b); pasteText(b, true); break;
    case 'P': snap(b); pasteText(b, false); break;
    case 'u': case 26: doUndo(); break;
    case 18: case 25: doRedo(); break;
    case 'v': mode = VISUAL; vsy = b.cy; vsx = b.cx; break;
    case 'V': mode = VLINE; vsy = b.cy; vsx = b.cx; break;
    case '/': doFind(); break;
    case 'd': case 'y': case 'c': case 'g': case 'r': case '>': case '<': case 'z': pend = (char)ch; pendN = n; break;
    }
    if (cur >= 0) { Buffer& bb = bufs[cur]; clampCursor(bb); }
}
static void visualKey(int ch) {
    Buffer& b = bufs[cur];
    if (ch == 3) ch = 'y';  // Ctrl-C copia
    if (pend) { char p = pend; pend = 0; pendCmd(b, p, ch, pendN); return; }
    if ((ch >= '1' && ch <= '9') || (ch == '0' && cnt > 0)) { cnt = cnt * 10 + (ch - '0'); return; }
    int n = cnt ? cnt : 1; bool had = cnt > 0; cnt = 0;
    if (motionKey(b, ch, n, had)) return;
    int y1 = vsy, x1 = vsx, y2 = b.cy, x2 = b.cx; if (y1 > y2 || (y1 == y2 && x1 > x2)) { std::swap(y1, y2); std::swap(x1, x2); }
    int x2e = std::min((int)b.lines[y2].size(), nextCp(b.lines[y2], x2));
    switch (ch) {
    case 27: mode = NORMAL; break;
    case 'v': mode = mode == VISUAL ? NORMAL : VISUAL; break;
    case 'V': mode = mode == VLINE ? NORMAL : VLINE; break;
    case 'g': pend = 'g'; pendN = n; break;
    case 'y':
        if (mode == VLINE) yankLines(b, y1, y2 - y1 + 1); else { reg = getRange(b, y1, x1, y2, x2e); regLine = false; }
        pushClip(regLine ? reg + "\n" : reg);
        b.cy = y1; b.cx = mode == VLINE ? b.cx : x1; mode = NORMAL; clampCursor(b); setMsg("Copied"); break;
    case 'd': case 'x': case 'c': {
        snap(b);
        if (mode == VLINE) { delLines(b, y1, y2 - y1 + 1); if (ch == 'c') { b.lines.insert(b.lines.begin() + b.cy, ""); b.cx = 0; mode = INSERT; break; } }
        else { reg = getRange(b, y1, x1, y2, x2e); regLine = false; delRange(b, y1, x1, y2, x2e); }
        mode = ch == 'c' ? INSERT : NORMAL; clampCursor(b); break;
    }
    case '>': mode = NORMAL; indentLines(b, y1, y2 - y1 + 1, true); break;
    case '<': mode = NORMAL; indentLines(b, y1, y2 - y1 + 1, false); break;
    }
}

// ============================================================ ações (menus / botões / atalhos)
static bool confirmDiscard(const string& what) {
    vector<string> d; for (auto& b : bufs) if (b.dirty) d.push_back(b.name());
    if (d.empty()) return true;
    string body = "Files with unsaved changes:\n"; for (auto& n : d) body += "  • " + n + "\n";
    int r = msgBox(what, body, {"Save all", "Discard", "Cancel"});
    if (r == 0) { saveAll(); return true; } return r == 1;
}
static void closeBuffer(int i) {
    if (i < 0 || i >= (int)bufs.size()) return;
    if (bufs[i].dirty) {
        int r = msgBox("Close file", "Save changes to " + bufs[i].name() + "?", {"Save", "Discard", "Cancel"});
        if (r == 0) saveBuffer(bufs[i]); else if (r != 1) return;
    }
    bufs.erase(bufs.begin() + i); if (bufs.empty()) { cur = -1; focus = F_TREE; } else cur = std::min(cur >= i && cur > 0 ? cur - 1 : cur, (int)bufs.size() - 1);
    mode = NORMAL; pop.on = false;
}
static void doNewEntry(bool dir) {
    string base = selectedDir(); vector<DField> f = {{dir ? "New folder name:" : "New file name:", ""}};
    string rel = relToRoot(base);
    if (dialog(dir ? "New folder" : "New file", "In: " + rel, f, {"Create", "Cancel"}) != 0 || f[0].text.empty()) return;
    fs::path np = toPath(base) / toPath(f[0].text); std::error_code ec;
    if (fs::exists(np, ec)) { setMsg("Already exists: " + f[0].text, true); return; }
    if (dir) fs::create_directories(np, ec);
    else { fs::create_directories(np.parent_path(), ec); if (!ec) { std::ofstream o(np, std::ios::binary); if (!o) { setMsg("Error creating " + f[0].text, true); return; } } }
    if (ec) { setMsg("Error: " + ec.message(), true); return; }
    selectPath(pstr(np)); if (!dir) openFile(pstr(np)); setMsg((dir ? "Folder created: " : "File created: ") + f[0].text);
}
static void doRename() {
    if (treeSel < 0 || treeSel >= (int)tree.size()) return; TreeNode n = tree[treeSel];
    vector<DField> f = {{"New name:", n.name}}; if (dialog("Rename", "", f, {"Rename", "Cancel"}) != 0 || f[0].text.empty() || f[0].text == n.name) return;
    fs::path np = toPath(n.path).parent_path() / toPath(f[0].text); std::error_code ec; fs::rename(toPath(n.path), np, ec);
    if (ec) { setMsg("Error: " + ec.message(), true); return; }
    string nps = pstr(np), nk = pathKey(n.path) + SEP;
    for (auto& b : bufs) if (samePath(b.path, n.path)) b.path = nps; else if (n.dir && startsWith(pathKey(b.path), nk)) b.path = nps + b.path.substr(n.path.size());
    if (n.dir && isExp(n.path)) { setExp(n.path, false); setExp(nps, true); }
    selectPath(nps);
}
static void doDelete() {
    if (treeSel < 0 || treeSel >= (int)tree.size()) return; TreeNode n = tree[treeSel];
    if (msgBox("Delete", "Delete " + string(n.dir ? "the folder '" : "the file '") + n.name + "'?\nThis action cannot be undone.", {"Delete", "Cancel"}) != 0) return;
    std::error_code ec; fs::remove_all(toPath(n.path), ec); if (ec) { setMsg("Error: " + ec.message(), true); return; }
    for (int i = (int)bufs.size() - 1; i >= 0; i--) if (samePath(bufs[i].path, n.path) || (n.dir && startsWith(pathKey(bufs[i].path), pathKey(n.path) + SEP))) { bufs[i].dirty = false; closeBuffer(i); }
    refreshTree(); setMsg("Deleted: " + n.name);
}
static void doConfig() {
    vector<DField> f = {{"Run command (blue button / F5):", cfg.run}, {"Release command (F6 / release mode):", cfg.release}, {"Run in full terminal (interactive programs)", "", true, cfg.terminal}};
    for (;;) {
        int r = dialog("Configure run", "Runs in the project folder. Default: rsfmake run  /  rsfmake run -r", f, {"Save", "Default", "Cancel"});
        if (r == 1) { f[0].text = "rsfmake run"; f[1].text = "rsfmake run -r"; f[2].check = false; continue; }
        if (r != 0) return; cfg.run = f[0].text; cfg.release = f[1].text; cfg.terminal = f[2].check; saveConfig(); setMsg("Settings saved to .vimple.conf"); return;
    }
}
static void doOpenProject() {
    if (!root.empty() && !confirmDiscard("Open another project")) return;
    string path; std::error_code ecc; string start = root.empty() ? pstr(fs::current_path(ecc)) : pstr(toPath(root).parent_path());
    if (pickFolder(start, path)) { if (runner.running) { runnerStop(); } openProject(path); }
}
static vector<string> diagLog;
static void diagAdd(const string& t) { diagLog.push_back(t); if (diagLog.size() > 14) diagLog.erase(diagLog.begin()); }
static void doDiag() {
    diagLog.clear(); menuOpen = -1; curs_set(0);
    long nPress = 0, nRel = 0, nClick = 0, nMove = 0, nWheel = 0, nOther = 0; unsigned long seen = 0;
    for (;;) {
        layout(); erase(); fillRect(0, 0, SH, SW, CP(P_TEXT)); int y = 0; char t[200];
        put(y++, 1, "DIAGNOSTICS — q/Esc exits. Move the mouse, click, DRAG and scroll the wheel in here.", CP(P_TITLE) | A_BOLD);
        const char* ver = curses_version();
        put(y++, 1, string("Library: ") + (ver ? ver : "?"), CP(P_TEXT));
        snprintf(t, sizeof t, "COLORS=%d  COLOR_PAIRS=%d  can redefine colors=%s  mode16=%s", COLORS, COLOR_PAIRS, can_change_color() ? "yes" : "no", pal16 ? "yes" : "no");
        put(y++, 1, t, CP(P_TEXT));
        snprintf(t, sizeof t, "Pairs: limit=%d  used=%d  failures=%d", diagPairLim, diagPairUsed, diagPairFail);
        put(y++, 1, t, CP(P_TEXT)); y++;
        put(y++, 1, "Samples (each word should have a different color):", CP(P_HINT));
        const char* nm[9] = {"text", "kw", "type", "str", "num", "comment", "pre", "func", "op"};
        put(y, 1, "A", CP(P_HINT)); int x = 4;
        for (int k = 0; k < 9; k++) { attrset(CP(P_SYN + k)); mvaddnstr(y, x, nm[k], (int)strlen(nm[k])); x += (int)strlen(nm[k]) + 2; }
        y++; put(y, 1, "B", CP(P_HINT)); x = 4;  // mesma coisa, mas escolhendo o par com color_set() em vez de COLOR_PAIR()
        for (int k = 0; k < 9; k++) { attrset(A_NORMAL); color_set((short)PM[P_SYN + k], nullptr); mvaddnstr(y, x, nm[k], (int)strlen(nm[k])); x += (int)strlen(nm[k]) + 2; }
        attrset(A_NORMAL); color_set((short)PM[P_TEXT], nullptr); y++;
        put(y, 1, "C", CP(P_HINT)); put(y, 4, " INSERT ", CP(P_MODE_I) | A_BOLD); put(y, 14, " ▶ Run ", CP(P_BTN) | A_BOLD); put(y, 22, "folder/", CP(P_DIR) | A_BOLD);
        put(y, 30, "error", CP(P_ERR)); put(y, 36, "warn", CP(P_WARN)); put(y, 43, "ok", CP(P_OK)); y += 2;
        string pc = "Stored pairs (id:fg/bg): ";
        for (int k = 0; k < 9; k++) { short f = -1, b = -1; pair_content((short)PM[P_SYN + k], &f, &b); snprintf(t, sizeof t, "%d:%d/%d ", PM[P_SYN + k], f, b); pc += t; }
        put(y++, 1, pc, CP(P_TEXT)); y++;
        snprintf(t, sizeof t, "Here: press=%ld release=%ld click=%ld motion=%ld wheel=%ld other=%ld  bits seen=0x%lx", nPress, nRel, nClick, nMove, nWheel, nOther, seen);
        put(y++, 1, t, CP(P_TEXT));
        put(y++, 1, "(test: 1 click; then hold the button and drag; then scroll the wheel)", CP(P_HINT));
        put(y++, 1, "Latest events here (bstate in hexadecimal):", CP(P_HINT));
        for (auto& l : diagLog) put(y++, 3, l, CP(P_TEXT));
        refresh();
        int ch = readKey(-1); if (ch == ERR || ch == KEY_RESIZE) continue;
        if (ch == 27 || ch == 'q' || ch == 'Q') break;
        if (ch == KEY_MOUSE) {
            MEVENT ev; if (getmouse(&ev) == OK) {
                unsigned long b = (unsigned long)ev.bstate; seen |= b;
                if (b & BUTTON1_PRESSED) nPress++; if (b & BUTTON1_RELEASED) nRel++; if (b & BUTTON1_CLICKED) nClick++;
                if (b & (BUTTON4_PRESSED | BUTTON5_PRESSED)) nWheel++;
                if ((b & REPORT_MOUSE_POSITION) && !(b & (BUTTON1_PRESSED | BUTTON1_RELEASED | BUTTON1_CLICKED | BUTTON4_PRESSED | BUTTON5_PRESSED))) nMove++;
                if (b == 0) nOther++;
                snprintf(t, sizeof t, "MOUSE bstate=0x%lx  x=%d y=%d", b, ev.x, ev.y); diagAdd(t);
            }
            else diagAdd("MOUSE (getmouse failed)");
        } else { snprintf(t, sizeof t, "KEY %d", ch); diagAdd(t); }
    }
    clearok(stdscr, TRUE);
}
static void doAction(int a) {
    menuOpen = -1;
    switch (a) {
    case ACT_OPEN_PROJECT: doOpenProject(); break;
    case ACT_NEW_FILE: doNewEntry(false); break;
    case ACT_NEW_DIR: doNewEntry(true); break;
    case ACT_SAVE: if (cur >= 0 && saveBuffer(bufs[cur])) setMsg("Saved: " + bufs[cur].name()); break;
    case ACT_SAVE_ALL: saveAll(); setMsg("All files saved"); break;
    case ACT_CLOSE: closeBuffer(cur); break;
    case ACT_QUIT: if (confirmDiscard("Quit")) quitting = true; break;
    case ACT_UNDO: doUndo(); break;
    case ACT_REDO: doRedo(); break;
    case ACT_FIND: doFind(); break;
    case ACT_FINDNEXT: if (cur >= 0) { if (lastSearch.empty()) doFind(); else if (!searchDir(bufs[cur], lastSearch, true)) setMsg("Not found", true); } break;
    case ACT_GOTO: doGoto(); break;
    case ACT_TREE: showTree = !showTree; if (!showTree) focus = F_EDITOR; else if (cur < 0) focus = F_TREE; break;
    case ACT_OUT: showOut = !showOut; break;
    case ACT_RELTOGGLE: releaseMode = !releaseMode; setMsg(releaseMode ? "Release mode: the Run button uses the release command" : "Debug mode: the Run button uses the default command"); break;
    case ACT_RUN: startRun(false); break;
    case ACT_RUN_REL: startRun(true); break;
    case ACT_STOP: if (runner.running) runnerStop(); break;
    case ACT_CONFIG: doConfig(); break;
    case ACT_REFRESH: refreshTree(); break;
    case ACT_RENAME: doRename(); break;
    case ACT_DELETE: doDelete(); break;
    case ACT_DIAG: doDiag(); break;
    }
}

// ============================================================ explorer (teclas / mouse)
static void treeActivate() {
    if (treeSel < 0 || treeSel >= (int)tree.size()) return; TreeNode n = tree[treeSel];
    if (n.dir) { setExp(n.path, !isExp(n.path)); refreshTree(); }
    else openFile(n.path);
}
static void treeKey(int ch) {
    int n = (int)tree.size();
    switch (ch) {
    case 'j': case KEY_DOWN: if (treeSel + 1 < n) treeSel++; break;
    case 'k': case KEY_UP: if (treeSel > 0) treeSel--; break;
    case 'g': case KEY_HOME: treeSel = 0; break; case 'G': case KEY_END: treeSel = std::max(0, n - 1); break;
    case 10: case 13: case KEY_ENTER: case 'l': case KEY_RIGHT: treeActivate(); break;
    case 'h': case KEY_LEFT:
        if (treeSel < n) { auto& nd = tree[treeSel]; if (nd.dir && isExp(nd.path)) { setExp(nd.path, false); refreshTree(); }
            else { for (int i = treeSel - 1; i >= 0; i--) if (tree[i].dir && tree[i].depth < nd.depth) { treeSel = i; break; } } }
        break;
    case 'a': doNewEntry(false); break; case 'A': doNewEntry(true); break;
    case 'r': doRename(); break; case 'd': case KEY_DC: doDelete(); break; case 'R': refreshTree(); break;
    case 27: if (cur >= 0) focus = F_EDITOR; break;
    }
}
static void handleMouse() {
    MEVENT ev; if (getmouse(&ev) != OK) return; int x = ev.x, y = ev.y; diagLastB = (unsigned long)ev.bstate; diagLastX = x; diagLastY = y;
    bool btnBits = ev.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED | BUTTON1_RELEASED | BUTTON4_PRESSED | BUTTON5_PRESSED);
    bool motion = (ev.bstate & REPORT_MOUSE_POSITION) && !btnBits, rel = ev.bstate & BUTTON1_RELEASED;  // movimento puro (sem botão no evento)
    bool press = ev.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED), up = ev.bstate & BUTTON4_PRESSED, dn = ev.bstate & BUTTON5_PRESSED;
    if (root.empty()) return;
    if (dragging) {  // arrastando a seleção no modo inserir
        if (cur < 0 || mode != INSERT || focus != F_EDITOR) dragging = false;
        else if (up || dn) return;
        else { Buffer& b = bufs[cur]; mouseToCursor(b, x, y); if (rel) { dragging = false; if (!hasSel(b)) insSel = false; } return; }
    }
    if (motion) return;
    if (menuOpen >= 0) {
        if (!press) return; auto items = menuItems(menuOpen);
        if (x >= dropRect.x && x < dropRect.x + dropRect.w && y > dropRect.y && y < dropRect.y + dropRect.h - 1) { int i = y - dropRect.y - 1; if (i >= 0 && i < (int)items.size()) doAction(items[i].act); return; }
        if (y != 0) { menuOpen = -1; return; }
    }
    if (up || dn) {
        int d = up ? -1 : 1;
        if (showOut && y >= L.outY && y < L.statY) { int rows = L.outH - 1, total = (int)outLines.size(); int f = outScroll < 0 ? outFirst : outScroll; f = std::max(0, f + d * 3); if (f >= total - rows) outScroll = -1; else outScroll = f; }
        else if (showTree && x < L.treeW && y >= L.edY && y < L.outY) treeSel = std::max(0, std::min((int)tree.size() - 1, treeSel + d * 3));
        else if (cur >= 0) { Buffer& b = bufs[cur]; b.top = std::max(0, std::min((int)b.lines.size() - 1, b.top + d * 3)); b.cy = std::max(b.top, std::min(b.cy, b.top + L.edH - 1)); b.cy = std::min(b.cy, (int)b.lines.size() - 1); b.cx = cxFromV(b.lines[b.cy], b.want); clampCursor(b); }
        return;
    }
    if (!press) return;
    if (y == 0) {
        if (x < btnW) { doAction(runner.running ? ACT_STOP : (releaseMode ? ACT_RUN_REL : ACT_RUN)); return; }
        for (auto& h : menuHits) if (x >= h.x0 && x < h.x1) { if (menuOpen == h.idx) menuOpen = -1; else { menuOpen = h.idx; menuSel = 0; } return; }
        menuOpen = -1; return;
    }
    if (y == 1) {
        for (auto& h : tabHits) if (x >= h.x0 && x < h.x1) { if (x >= h.cx0) closeBuffer(h.idx); else { cur = h.idx; focus = F_EDITOR; mode = NORMAL; pop.on = false; } return; }
        return;
    }
    if (showTree && x < L.treeW && y >= L.edY && y < L.outY) {
        if (y == L.edY + 1) { for (auto& h : treeBtns) if (x >= h.x0 && x < h.x1) { focus = F_TREE; switch (h.idx) { case 0: doNewEntry(false); break; case 1: doNewEntry(true); break; case 2: doRename(); break; case 3: doDelete(); break; default: refreshTree(); } return; } return; }
        int i = treeTop + (y - (L.edY + 2)); if (y >= L.edY + 2 && i >= 0 && i < (int)tree.size()) { treeSel = i; focus = F_TREE; treeActivate(); }
        return;
    }
    if (showOut && y > L.outY && y < L.statY) { int i = outFirst + (y - L.outY - 1); if (i >= 0 && i < (int)outLines.size()) { if (!jumpFromLine(outLines[i])) setMsg("Line has no file:line reference", true); } return; }
    if (y >= L.edY && y < L.outY && x >= L.edX && cur >= 0) {
        Buffer& b = bufs[cur]; focus = F_EDITOR; pop.on = false;
        mouseToCursor(b, x, y); insKind = 0;
        if (mode == INSERT && (ev.bstate & BUTTON1_PRESSED)) { insSel = true; selAy = b.cy; selAx = b.cx; dragging = true; }  // começa a seleção
        else insSel = false;
    }
}

// ============================================================ teclado global
static void menuKey(int ch) {
    auto items = menuItems(menuOpen); int n = (int)items.size();
    switch (ch) {
    case 27: case KEY_F(10): menuOpen = -1; break;
    case KEY_LEFT: menuOpen = (menuOpen + 3) % 4; menuSel = 0; break; case KEY_RIGHT: menuOpen = (menuOpen + 1) % 4; menuSel = 0; break;
    case KEY_UP: menuSel = (menuSel + n - 1) % n; break; case KEY_DOWN: menuSel = (menuSel + 1) % n; break;
    case 10: case 13: case KEY_ENTER: { int a = items[menuSel].act; menuOpen = -1; doAction(a); break; }
    }
}
static void handleKey(int ch) {
    msg.clear(); if (ch == KEY_MOUSE) diagMouseN++; else diagLastKey = ch;
    if (ch == KEY_RESIZE) {
#ifdef PDCURSES
        resize_term(0, 0);
#endif
        return;
    }
    if (ch != KEY_MOUSE) dragging = false;
    if (ch == KEY_MOUSE) { handleMouse(); return; }
    if (menuOpen >= 0) { menuKey(ch); return; }
    bool inIns = focus == F_EDITOR && cur >= 0 && mode == INSERT;
    switch (ch) {
    case KEY_F(10): menuOpen = 0; menuSel = 0; return;
    case KEY_F(1): doAction(ACT_DIAG); return;
    case KEY_F(5): doAction(ACT_RUN); return; case KEY_F(6): doAction(ACT_RUN_REL); return; case KEY_F(7): doAction(ACT_STOP); return;
    case KEY_F(8): doAction(ACT_OUT); return; case KEY_F(9): doAction(ACT_CONFIG); return;
    case 19: if (inIns && !pop.on) {} doAction(ACT_SAVE); return;
    case 17: doAction(ACT_QUIT); return; case 15: doAction(ACT_OPEN_PROJECT); return; case 20: doAction(ACT_TREE); return;
    case 6: doAction(ACT_FIND); return; case 7: doAction(ACT_GOTO); return;
    case 5: if (cur >= 0 && mode == INSERT) leaveInsert(bufs[cur]); if (focus == F_TREE && cur >= 0) focus = F_EDITOR; else { showTree = true; focus = F_TREE; } return;
    case 14: if (!inIns) { doAction(ACT_NEW_FILE); return; } break;
    case 23: if (!inIns) { doAction(ACT_CLOSE); return; } break;
    }
    if (focus == F_TREE) treeKey(ch);
    else if (cur < 0) return;
    else if (mode == INSERT) insertKey(ch);
    else if (mode == VISUAL || mode == VLINE) visualKey(ch);
    else normalKey(ch);
}

// ============================================================ main
#ifdef _WIN32
#define MOUSE_OFF
#else
#define MOUSE_OFF fputs("\033[?1002l", stdout);
#endif
#ifdef _WIN32
static unsigned oldOutCP = 0, oldInCP = 0;
#endif
static void shutdownCurses() {
    restoreTheme(); MOUSE_OFF endwin();
#ifdef _WIN32
    if (oldOutCP) SetConsoleOutputCP(oldOutCP);
    if (oldInCP) SetConsoleCP(oldInCP);
#endif
}
int main(int argc, char** argv) {
#ifdef _WIN32
    if (!setlocale(LC_ALL, ".UTF-8")) setlocale(LC_ALL, "");  // UTF-8: acentos, ▾ ● ■ etc. (com a locale ANSI padrão viram lixo)
    oldOutCP = GetConsoleOutputCP(); oldInCP = GetConsoleCP();
#ifdef PDC_WIDE
    SetConsoleOutputCP(65001); SetConsoleCP(65001);
#endif
#else
    setlocale(LC_ALL, "");
    signal(SIGPIPE, SIG_IGN); setenv("ESCDELAY", "25", 0);
#endif
    for (auto& w : splitWords(KW_HL)) HL_KW.insert(w); for (auto& w : splitWords(TY_HL)) HL_TY.insert(w); buildDict();
    initscr(); raw(); noecho(); keypad(stdscr, TRUE);
#ifndef _WIN32
    set_escdelay(25);
#endif
    if (has_colors()) start_color();
#ifdef _WIN32
    const int minColors = 8;  // 16 cores bastam: a paleta gruvbox é redefinida/aproximada
#else
    const int minColors = 256;
#endif
    if (!has_colors() || COLORS < minColors) {
        endwin();
#ifdef _WIN32
        fprintf(stderr, "vimple needs a console with colors (Windows Terminal or conhost).\n");
#else
        fprintf(stderr, "vimple needs a 256-color terminal (use TERM=xterm-256color).\n");
#endif
        return 1;
    }
    initTheme();
#ifdef _WIN32
    mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, nullptr); mouseinterval(0);
    {   // O "QuickEdit" do console captura o mouse (seleção/rolagem do próprio console) e o programa não recebe nenhum evento.
        void* hin = GetStdHandle((unsigned long)-10); unsigned long m = 0;
        if (hin && hin != (void*)(intptr_t)-1 && GetConsoleMode(hin, &m)) { m |= 0x80 | 0x10 | 0x8; m &= ~0x40ul; SetConsoleMode(hin, m); }  // EXTENDED_FLAGS|MOUSE|WINDOW, sem QUICK_EDIT
    }
#else
    mousemask(BUTTON1_PRESSED | BUTTON1_CLICKED | BUTTON1_RELEASED | BUTTON4_PRESSED | BUTTON5_PRESSED | REPORT_MOUSE_POSITION, nullptr); mouseinterval(0);
#endif
#ifndef _WIN32
    fputs("\033[?1002h", stdout); fflush(stdout);  // movimento com botão apertado (arrastar para selecionar)
#endif
    sysSeen = readClip();
    if (argc > 1) {
        std::error_code ec; string a = argv[1];
#ifdef _WIN32
        a = cpToU8(a, 0);  // argv vem na codepage ANSI
#endif
        fs::path pth = fs::weakly_canonical(toPath(a), ec); if (fs::is_directory(pth, ec)) openProject(pth);
    }
    while (root.empty()) {
        string path; std::error_code ec;
#ifdef _WIN32
        string start;  // vazio = começa na lista de discos
#else
        string start = pstr(fs::current_path(ec));
#endif
        if (!pickFolder(start, path)) { shutdownCurses(); return 0; } openProject(toPath(path));
    }
    while (!quitting) {
        runnerPoll(); drawAll(); int ch = readKey(runner.running ? 50 : -1);
        if (ch != ERR) handleKey(ch);
        if (mode != INSERT) { insSel = false; insKind = 0; dragging = false; }
    }
#ifdef _WIN32
    if (runner.running) runnerStop();
#else
    if (runner.running) kill(-runner.pid, SIGKILL);
#endif
    shutdownCurses(); return 0;
}