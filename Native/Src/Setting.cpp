#include "pch.h"
#include <include/Ling.h>
#include <wincodec.h>
#include <ole2.h>
#include <chrono>
#include <fstream>
#include <mutex>
#include <atomic>
#include "Setting.h"
#include "BlacklistMatch.h"
#include "CommandKeys.h"
#include "ConfigMerge.h"
#include "Util.h"
#include "Lang.h"
#include "Win/WinCap.h"
#include "Win/WinPin.h"
#include "Win/HotkeyHook.h"
#include "Win/WinSetting.h"
#include "Win/WinQuickTranslate.h"
#include "Win/WinImageEdit.h"
#include "App.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <cwctype>

namespace {
    std::unique_ptr<Setting> setting;
    // 全局热键表：id（配置键 = 设置页那一行）→ msgId（RegisterHotKey 的 id，也是 onHotKey 收到的值）。
    // 只有**真正绑定了动作**的行才在此处；设置页里其余几行（show_cursor / quick_translate /
    //   open_clipboard）目前没有对应实现，不注册 —— 否则会出现"按键被消费但没有任何效果"。
    struct HotkeyEntry { const wchar_t* id; UINT msgId; };
    // 顺序即注册顺序：同一把组合键被两条热键占用时「先到先得」（见 applyHotkeys 的去重），
    // 所以把「右键菜单」页管的这几条排在最前面 —— 冲突时以右键菜单为准。
    const HotkeyEntry kHotkeys[] = {
        { L"capture",                100 },
        { L"ocr",                    114 },
        { L"ocr_translate",          115 },
        { L"quick_translate",        108 },
        { L"record",                 104 },
        { L"fullscreen_canvas",      103 },
        { L"make_collage",           113 },
        { L"capture_fullscreen",     101 },
        { L"capture_focused_window", 102 },
        { L"toggle_pins",            105 },
        { L"show_main",              106 },
        { L"open_screenshot_dir",    107 },
    };
    bool isGlobalHotkeyId(const std::wstring& id)
    {
        for (const auto& hk : kHotkeys) if (id == hk.id) return true;
        return false;
    }
    // 全局热键的内置默认键位：配置里没有这一项（从没设过）时用它。
    // 托盘右键菜单是这几条键位的主要入口（截图 F1 / OCR F2 / 翻译 F3），其余项出厂不绑 ——
    // 录屏、演示画布以前默认 F2 / F3，与 OCR / 翻译撞车，已按「右键菜单为准」让位。
    const wchar_t* defaultShortcutOf(const std::wstring& type)
    {
        if (type == L"capture") return L"F1";
        if (type == L"ocr") return L"F2";
        if (type == L"ocr_translate") return L"F3";
        return L"";
    }
    constexpr UINT_PTR kHotkeyRetryTimerId{ 0x5A18 };
    // 兜底模式下的自动恢复周期：只要仍有热键落在钩子上，就按该间隔重新执行一遍
    // 「注册 + 重装钩子」流程。该流程不设重试次数上限，原因是：钩子会被系统静默摘除（回调超时），
    // 也可能被其他程序后安装的热键抢占；一旦放弃重试，这些热键将长期失效，
    // 只能通过重启程序或开关一次截图会话来恢复。
    constexpr UINT kHotkeyRetryMs{ 30000 };
    // 配置文件的默认内容。空文件、坏 JSON、缺键都拿它兜底，所以这里列出的每一项
    // 都是代码里会直接按名字取的（见 getLang / getAutoStart）。
    // 全局热键**不在这里**给默认值 —— 键位统一由 getShortcutKey 的内置默认表决定（defaultShortcutOf）
    constexpr std::wstring_view defaultConfig{ LR"""({"common":{"autoStart":false}})""" };

}


// —— config.json 的读取与落盘 ——
namespace {
    // 旧配置的 features 可能不是对象；GetNamedObject 遇到错误类型会崩溃。
    JsonObject getFeaturesObjectOrNull(const JsonObject& root)
    {
        if (!root || !root.HasKey(L"features")) return JsonObject{ nullptr };
        if (root.GetNamedValue(L"features").ValueType() != JsonValueType::Object)
            return JsonObject{ nullptr };
        return root.GetNamedObject(L"features");
    }

    // 读配置文件。返回 false = 读不出来（被占用 / 没权限 / IO 错误），与「文件是空的」区分开。
    // 旧实现直接用 readFileText：读失败也返回空串，于是启动时会把读不到的配置当成空文件、
    // 立刻写回默认值 —— 一次临时的读取失败（杀软扫描、另一个实例正在保存）就会清空用户的
    // 全部设置，连带 OCR 档位与插件目录，表现成「OCR 说运行时未安装」「设置自己变回去了」。
    bool readConfigContent(const std::filesystem::path& path, std::wstring& out)
    {
        out.clear();
        std::ifstream in{ path, std::ios::binary };
        if (!in) return false;
        std::string bytes{ std::istreambuf_iterator<char>{ in }, std::istreambuf_iterator<char>{} };
        if (in.bad()) return false;
        out = Ling::Util::readTextFromBytes(bytes.data(), bytes.size());
        return true;
    }

    // 只有空白 / BOM：老版本写盘中断留下的空文件，按「首次运行」处理
    bool isBlankConfig(const std::wstring& s)
    {
        for (wchar_t c : s)
            if (c != L' ' && c != L'\t' && c != L'\r' && c != L'\n' && c != 0xFEFF) return false;
        return true;
    }

    // 启动读取配置时重试若干次：OneDrive 下载或杀毒软件扫描可能导致首次打开失败。
    // 若立即判定为读取失败，本次会话将进入只读状态，后续修改无法保存。
    bool readConfigContentRetry(const std::filesystem::path& path, std::wstring& out)
    {
        for (int attempt = 0; attempt < 10; ++attempt) {
            if (readConfigContent(path, out)) return true;
            Sleep(80);
        }
        return false;
    }

    bool isOwnProcessForeground()
    {
        HWND fg = GetForegroundWindow();
        if (!fg) return false;
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        return pid == GetCurrentProcessId();
    }

    // 原子写：先写同目录临时文件，成功后再整体替换目标。
    // 旧实现 CreateFile(CREATE_ALWAYS) 直接截断目标文件，写到一半崩溃 / 被杀就留下半截 JSON，
    // 下次启动解析失败 —— 用户看到的「配置文件读取错误」就是这么来的。
    bool writeConfigAtomic(const std::filesystem::path& dst, const std::wstring& text)
    {
        // 数据目录虽在启动时由 Util::initAppDataDirs() 创建，这里仍补建父目录，
        // 以免用户删掉目录后第一次保存失败。
        if (!dst.parent_path().empty()) {
            std::error_code ec;
            std::filesystem::create_directories(dst.parent_path(), ec);
        }
        // 临时文件名包含进程 ID 和序号，避免多实例或多线程写入时共用同一临时文件。
        // 固定名称在同步盘上可能导致创建失败：上一次的临时文件仍被同步引擎占用。
        static std::atomic<unsigned long long> tmpSeq{ 0 };
        const std::wstring tmp = dst.wstring() + L"." + std::to_wstring(GetCurrentProcessId())
            + L"." + std::to_wstring(++tmpSeq) + L".tmp";
        HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        const WORD bom = 0xFEFF;
        const DWORD bytes = static_cast<DWORD>(text.size() * sizeof(wchar_t));
        DWORD written = 0;
        bool ok = WriteFile(h, &bom, sizeof(bom), &written, nullptr) && written == sizeof(bom)
            && WriteFile(h, text.data(), bytes, &written, nullptr) && written == bytes
            && FlushFileBuffers(h);
        CloseHandle(h);
        if (!ok) {
            DeleteFileW(tmp.c_str());
            return false;
        }
        // 目标文件可能被 OneDrive、杀毒软件或另一实例短暂占用，替换失败时重试若干次
        for (int attempt = 0; attempt < 8; ++attempt) {
            if (MoveFileExW(tmp.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING)) return true;
            const DWORD err = GetLastError();
            if (err != ERROR_ACCESS_DENIED && err != ERROR_SHARING_VIOLATION
                && err != ERROR_LOCK_VIOLATION) break;
            Sleep(20);
        }
        DeleteFileW(tmp.c_str());
        return false;
    }

    std::filesystem::path siblingOf(const std::filesystem::path& p, const wchar_t* suffix)
    {
        return std::filesystem::path{ p.wstring() + suffix };
    }

}


Setting::Setting() :dataPath{ initDataPath() }, configPath{ initConfigPath() }, cachePath{ initCachePath() }
{
    if (std::filesystem::exists(configPath)) {
        std::wstring content;
        if (!readConfigContentRetry(configPath, content)) {
            // 读不到 ≠ 空文件：原文件保持原样，本次先用内存里的默认值，绝不回写覆盖
            configReadOnly = true;
            configObj = JsonObject::Parse(defaultConfig);
            MessageBox(nullptr,
                L"config.json 读取失败（文件被占用或没有权限）。\n"
                L"本次使用默认设置，原文件已保留、不会被覆盖。\n\n"
                L"Could not read config.json. The file was left untouched.",
                L"ScreenShut", MB_OK | MB_ICONWARNING);
            return;
        }
        if (isBlankConfig(content)) {
            // 真空文件（首次安装 / 上次写到一半）：补一份默认配置
            configObj = JsonObject::Parse(defaultConfig);
            save();
            return;
        }
        JsonObject obj{ nullptr };
        if (JsonObject::TryParse(content, obj)) {
            configObj = obj;
            // 记录已保存内容，供 save() 跳过无变化的写入（后续启动清理步骤也会调用 save()）
            lastSavedConfig = content;
            // 留一份可用配置：下次真解析失败时用它恢复，而不是把设置清成默认值
            backupConfigFile();
            // 若 OneDrive 冲突副本比主文件新，采用较新版本，避免设置回退
            adoptNewerConflictCopy();
            return;
        }
        // 解析失败：先留存现场，再尝试上一份可用配置，都没有才回落默认值。
        // 旧实现只把内存换成默认值、坏文件原地不动：用户每启动一次丢一次设置。
        preserveBrokenConfig();
        if (restoreFromBackup()) return;
        configObj = JsonObject::Parse(defaultConfig);
        MessageBox(nullptr,
            L"config.json 格式错误，已备份为 config.json.bad，本次使用默认设置。\n\n"
            L"config.json is malformed; a backup was kept as config.json.bad.",
            L"ScreenShut", MB_OK | MB_ICONWARNING);
        return;
    }
    // 没有 config.json：真正的首次运行（或用户主动删掉想恢复默认），按默认值初始化。
    // 不从 .bak 复活：删文件是最直观的「恢复默认」，不能让它把旧设置又拉回来。
    configObj = JsonObject::Parse(defaultConfig);
}

// 启动读成功时留一份好的副本，供 restoreFromBackup 回退（只在启动时写，运行期不多写盘）
void Setting::backupConfigFile()
{
    if (configPath.empty()) return;
    std::wstring text;
    if (!readConfigContent(configPath, text)) return;
    // 若 .bak 内容与当前配置相同则跳过写入，减少同步盘上的无效上传与冲突
    std::wstring old;
    if (readConfigContent(siblingOf(configPath, L".bak"), old) && old == text) return;
    writeConfigAtomic(siblingOf(configPath, L".bak"), text);
}

// 同步盘在检测到同一文件被多台电脑修改时，会保留两份并将其中一份命名为
// config-电脑名.json 或 config (1).json。启动时若该副本比 config.json 新，则采用该副本
// 原配置已由 backupConfigFile() 保存到 .bak。
void Setting::adoptNewerConflictCopy()
{
    if (configPath.empty()) return;
    std::error_code ec;
    const auto mainTime = std::filesystem::last_write_time(configPath, ec);
    if (ec) return;

    std::filesystem::path best;
    std::filesystem::file_time_type bestTime{};
    for (const auto& entry : std::filesystem::directory_iterator(configPath.parent_path(), ec)) {
        if (ec) break;
        std::error_code fec;
        if (!entry.is_regular_file(fec)) continue;
        const auto path = entry.path();
        if (_wcsicmp(path.extension().c_str(), L".json") != 0) continue;
        const std::wstring name = path.filename().wstring();
        // 仅匹配同步盘或资源管理器生成的冲突副本命名，避免误用用户自建的 config-xxx.json
        const bool conflictName = name.rfind(L"config-", 0) == 0
            || name.rfind(L"config (", 0) == 0
            || name.rfind(L"config - ", 0) == 0;
        if (!conflictName) continue;
        const auto t = entry.last_write_time(fec);
        if (fec || t <= mainTime) continue;
        if (best.empty() || t > bestTime) { best = path; bestTime = t; }
    }
    if (best.empty()) return;

    std::wstring text;
    if (!readConfigContentRetry(best, text) || isBlankConfig(text)) return;
    JsonObject obj{ nullptr };
    if (!JsonObject::TryParse(text, obj)) return;
    // 仅接受包含 common 节的文件，避免将同目录其他 JSON 或不完整文件当作配置，导致设置被清空
    if (!obj.HasKey(L"common")) return;

    OutputDebugStringW((L"[ScreenShut] adopted newer config conflict copy: " + best.wstring() + L"\n").c_str());
    configObj = obj;
    lastSavedConfig.clear();   // 清除已保存记录，确保采用的配置写回 config.json
    save();
}

// 解析失败后的兜底：用上一份可用配置重建主文件
bool Setting::restoreFromBackup()
{
    std::wstring text;
    if (!readConfigContent(siblingOf(configPath, L".bak"), text) || isBlankConfig(text)) return false;
    JsonObject obj{ nullptr };
    if (!JsonObject::TryParse(text, obj)) return false;
    configObj = obj;
    save();
    return true;
}

// 坏档留一份现场（覆盖上一份 .bad），方便用户/排查找回原内容
void Setting::preserveBrokenConfig()
{
    if (configPath.empty()) return;
    std::error_code ec;
    std::filesystem::copy_file(configPath, siblingOf(configPath, L".bad"),
        std::filesystem::copy_options::overwrite_existing, ec);
}



Setting::~Setting()
{

}

void Setting::init()
{
    auto ptr = new Setting();
    setting.reset(ptr);
    // 旧配置的顶层节可能不是对象；进入业务代码前统一归一化。
    setting->normalizeConfigSections();
    // 缓存目录允许用户自定义（文件输出 → 保存路径 → 缓存文件路径），没设置就用默认。
    setting->cachePath = setting->resolveCachePath();
    // 启动时清理旧版本遗留的 AI 配置（AI 对话 / AI 视觉 OCR / AI 翻译）
    setting->purgeRemovedAiSettings();
    // 启动时清理旧程序（ScreenCapture）留下的历史键位
    setting->purgeLegacyShortcutKeys();
}

void Setting::dispose()
{
    setting.reset();
}

Setting* Setting::get()
{
    return setting.get();
}

std::filesystem::path Setting::getDataPath()
{
    return dataPath; //复制一份路径对象，不允许就地修改
}

std::wstring Setting::getPluginDir()
{
    auto common = configObj.GetNamedObject(L"common", nullptr);
    if (!common) return L"";
    return std::wstring{ common.GetNamedString(L"pluginDir", L"") };
}

void Setting::setPluginDir(const std::wstring& dir)
{
    auto common = configObj.GetNamedObject(L"common", nullptr);
    if (!common) {
        common = JsonObject();
        configObj.SetNamedValue(L"common", common);
    }
    common.SetNamedValue(L"pluginDir", JsonValue::CreateStringValue(dir));
    save();
}

const JsonObject Setting::getConfigObj()
{
    return configObj;
}

void Setting::setShortcutKey(const std::wstring& type, const std::vector<std::wstring>& keys)
{
    std::wstring str;
    for (size_t i = 0; i < keys.size(); i++)
    {
        str += L"+" + keys[i];
    }
    str.erase(0,1);
    // 配置里还没有 shortcutKey 对象时（首次设置热键）要先建出来：GetNamedObject 的单参
    // 重载在缺键时会抛 hresult_error，异常会穿透 DispatcherQueue 回调导致进程被杀。
    auto shortcutKey = configObj.GetNamedObject(L"shortcutKey", nullptr);
    if (!shortcutKey) {
        shortcutKey = JsonObject();
        configObj.SetNamedValue(L"shortcutKey", shortcutKey);
    }
    shortcutKey.SetNamedValue(type, JsonValue::CreateStringValue(str));
    save();
    // 改的是哪一行就整体重注册 —— 现在设置页里已实现的行全都真注册了
    if (isGlobalHotkeyId(type)) applyHotkeys();
}

std::wstring Setting::getShortcutKey(const std::wstring& type)
{
    // 一律使用带默认值的重载：启动时 ensureDefaults 已补齐，此处只是避免运行期
    // 意外（配置被外部改动、问了个没配过的 type）变成一次崩溃
    auto obj = configObj.GetNamedObject(L"shortcutKey", nullptr);
    // 配置里"有这一行"就以它为准 —— 包括用户按 Backspace/Delete 清空后写进去的空串，
    // 空串 = 不绑定。只有从来没写过的行才回落到内置默认键位，
    // 否则"清空 capture"会被误当成"没配过"，又变回 F1，清空就永远清不掉。
    if (obj && obj.HasKey(type)
        && obj.GetNamedValue(type).ValueType() == JsonValueType::String) {
        return std::wstring{ obj.GetNamedString(type, L"") };
    }
    return defaultShortcutOf(type);
}

// 「全局热键」页的恢复默认：清空整个 shortcutKey 对象。
// 清空后 getShortcutKey 会逐条回落到内置默认表（capture=F1 / record=F2 / fullscreen_canvas=F3，
// 其余默认不绑）。保留空对象而不是删掉这个键 —— setShortcutKey 取它时不做"缺键"兜底。
void Setting::restoreShortcutKeyDefaults()
{
    configObj.SetNamedValue(L"shortcutKey", JsonObject{});
    save();
    applyHotkeys();
}

// 没有任何键位覆盖项 = 仍是出厂键位。单行清空（Backspace/Delete）也会写进配置对象，
// 所以只要对象非空就说明用户改过，此时「恢复默认」才可点。
bool Setting::shortcutKeyAtDefaults()
{
    auto obj = configObj.GetNamedObject(L"shortcutKey", nullptr);
    return !obj || obj.Size() == 0;
}

// —— 键位冲突（跨页）——
// 「右键菜单」页与「全局热键」页共用同一套键位，两页各自只看到自己那几行，
// 所以冲突要在全局热键表上判定：同一把组合键只能给一条热键，否则注册时必然有一条失效。
std::wstring Setting::findShortcutOwner(const std::wstring& chord, const std::wstring& selfId)
{
    if (chord.empty()) return {};
    const std::wstring target = CommandKeys::canonicalChord(chord);
    for (const auto& hk : kHotkeys) {
        const std::wstring id{ hk.id };
        if (id == selfId) continue;
        const std::wstring other = getShortcutKey(id);
        if (other.empty()) continue;
        if (_wcsicmp(CommandKeys::canonicalChord(other).c_str(), target.c_str()) == 0) return id;
    }
    return {};
}

// —— 托盘右键菜单：项序 / 开关 ——
// 存 features.trayMenuOrder（逗号分隔的 id，顺序即菜单顺序），以及
// features.trayMenuOff / trayMenuOn（分别记录与出厂状态不同的关 / 开项）。
// 只存差异，出厂状态在代码里定义，新增菜单项自动出现在末尾。
namespace {
    // 出厂菜单顺序：设置、退出固定在最下方
    const wchar_t* kTrayMenuFixedTail[] = { L"settings", L"exit" };
    // 出厂默认开启的项（其余默认关闭）
    bool trayMenuDefaultOn(const std::wstring& id)
    {
        return id == L"capture" || id == L"ocr" || id == L"ocr_translate";
    }
    std::vector<std::wstring> splitIds(const std::wstring& s)
    {
        std::vector<std::wstring> out;
        for (auto& part : Ling::Util::splitStr(s, L',')) {
            if (!part.empty()) out.push_back(part);
        }
        return out;
    }
}

std::vector<std::wstring> Setting::defaultTrayMenuOrder()
{
    return {
        L"capture", L"ocr", L"ocr_translate", L"quick_translate",
        L"record", L"fullscreen_canvas", L"make_collage",
        kTrayMenuFixedTail[0], kTrayMenuFixedTail[1],
    };
}

bool Setting::trayMenuItemFixed(const std::wstring& id)
{
    return id == kTrayMenuFixedTail[0] || id == kTrayMenuFixedTail[1];
}

std::vector<std::wstring> Setting::getTrayMenuOrder()
{
    const auto def = defaultTrayMenuOrder();
    auto known = [&def](const std::wstring& id) {
        for (const auto& d : def) if (d == id) return true;
        return false;
    };
    // 固定项（设置 / 退出）永远排在最后，且顺序固定
    std::vector<std::wstring> out;
    for (const auto& id : splitIds(featStr(L"trayMenuOrder", L""))) {
        if (!known(id) || trayMenuItemFixed(id)) continue;
        if (std::find(out.begin(), out.end(), id) != out.end()) continue;
        out.push_back(id);
    }
    // 配置里没有的项（新增功能 / 第一次运行）按出厂顺序补在固定项之前
    for (const auto& id : def) {
        if (trayMenuItemFixed(id)) continue;
        if (std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
    }
    for (const auto& id : def) {
        if (trayMenuItemFixed(id)) out.push_back(id);
    }
    return out;
}

void Setting::setTrayMenuOrder(const std::vector<std::wstring>& order)
{
    std::wstring s;
    for (const auto& id : order) {
        if (trayMenuItemFixed(id)) continue;   // 固定项不参与自定义顺序，永远在最后
        if (!s.empty()) s += L',';
        s += id;
    }
    setFeatStr(L"trayMenuOrder", s);
}

bool Setting::getTrayMenuItemEnabled(const std::wstring& id)
{
    if (trayMenuItemFixed(id)) return true;   // 设置 / 退出：无法关闭
    for (const auto& on : splitIds(featStr(L"trayMenuOn", L""))) {
        if (on == id) return true;
    }
    for (const auto& off : splitIds(featStr(L"trayMenuOff", L""))) {
        if (off == id) return false;
    }
    return trayMenuDefaultOn(id);
}

void Setting::setTrayMenuItemEnabled(const std::wstring& id, bool on)
{
    if (trayMenuItemFixed(id)) return;   // 固定项不接受开关
    // 两个差异表都先摘掉本项，再按「是否偏离出厂值」写入其中一边
    auto collect = [this, &id](const wchar_t* key) {
        std::vector<std::wstring> out;
        for (const auto& v : splitIds(featStr(key, L""))) {
            if (v != id) out.push_back(v);
        }
        return out;
    };
    std::vector<std::wstring> onIds = collect(L"trayMenuOn");
    std::vector<std::wstring> offIds = collect(L"trayMenuOff");
    const bool defaultOn = trayMenuDefaultOn(id);
    if (on && !defaultOn) onIds.push_back(id);
    if (!on && defaultOn) offIds.push_back(id);

    auto f = featuresObj();
    auto write = [&f](const wchar_t* key, const std::vector<std::wstring>& ids) {
        std::wstring s;
        for (const auto& v : ids) {
            if (!s.empty()) s += L',';
            s += v;
        }
        if (s.empty()) {
            if (f.HasKey(key)) f.Remove(key);
        }
        else {
            f.SetNamedValue(key, JsonValue::CreateStringValue(s));
        }
    };
    write(L"trayMenuOn", onIds);
    write(L"trayMenuOff", offIds);
    save();
}

void Setting::restoreTrayMenuDefaults()
{
    // 项序 / 开关只是几个 features 字段，删掉即回到出厂
    auto f = featuresObj();
    f.Remove(L"trayMenuOrder");
    f.Remove(L"trayMenuOn");
    f.Remove(L"trayMenuOff");
    save();
    // 这几条菜单项的键位也一起回到内置默认表（删掉覆盖项，getShortcutKey 就会回落默认值）
    auto obj = configObj.GetNamedObject(L"shortcutKey", nullptr);
    if (obj) {
        for (const auto& id : defaultTrayMenuOrder()) {
            if (obj.HasKey(id.c_str())) obj.Remove(id.c_str());
        }
        save();
    }
    applyHotkeys();
}

bool Setting::trayMenuAtDefaults()
{
    const auto f = featuresObj();
    if (f.HasKey(L"trayMenuOrder") || f.HasKey(L"trayMenuOn") || f.HasKey(L"trayMenuOff")) return false;
    // 键位：这几条菜单项若存过覆盖值，也已经不是出厂状态
    auto obj = configObj.GetNamedObject(L"shortcutKey", nullptr);
    if (obj) {
        for (const auto& id : defaultTrayMenuOrder()) {
            if (obj.HasKey(id.c_str())) return false;
        }
    }
    return true;
}

void Setting::purgeRemovedAiSettings()
{
    static const wchar_t* const kRemovedFeatKeys[] = {
        // AI 对话
        L"chatModel", L"chatNewSessionOnOpen", L"chatNewSessionOnClose", L"chatApiConfigs",
        // AI 视觉 OCR
        L"htmlVisionModel", L"ocrHtmlPrompt", L"ocrMarkdownPrompt",
        // AI 翻译
        L"translatePrompt", L"translateEnabledModels",
        L"translateApiProvider", L"translateApiUrl", L"translateApiKey", L"translateApiModel",
        L"translateApiAppId", L"translateApiSecret", L"translateApiName",
        L"translateDomain", L"translateDomainCustom",
    };
    bool changed = false;
    if (configObj.HasKey(L"features")) {
        auto features = getFeaturesObjectOrNull(configObj);
        if (features) {
            for (auto* key : kRemovedFeatKeys) {
                if (features.HasKey(key)) {
                    features.Remove(key);
                    changed = true;
                }
            }
        }
    }
    // 旧「AI 对话」历史（toolPin/chat）：快捷翻译改用独立键 quickTranslate/history，
    // 旧会话不再展示，直接清掉。
    auto toolRoot = configObj.GetNamedObject(L"toolPin", nullptr);
    if (toolRoot && toolRoot.HasKey(L"chat")) {
        toolRoot.Remove(L"chat");
        changed = true;
    }
    if (changed) save();
}

// 旧程序 ScreenCapture（本工程前身）写入 shortcutKey 的历史键位只有一条 `cap`（区域截图，
// 默认 Ctrl+Alt+A）。现用 id 为 capture（见 kHotkeys），`cap` 不在表内、永不会被注册，
// 启动时删除，避免残留随 config.json 长期留存。
void Setting::purgeLegacyShortcutKeys()
{
    static const wchar_t* const kLegacyIds[] = { L"cap" };
    auto obj = configObj.GetNamedObject(L"shortcutKey", nullptr);
    if (!obj) return;
    bool changed = false;
    for (auto* id : kLegacyIds) {
        if (obj.HasKey(id)) {
            obj.Remove(id);
            changed = true;
        }
    }
    if (changed) save();
}

void Setting::setAutoStart(bool autoStart)
{
    // 和 TrafficMonitor 一致：只登记当前用户的 Run 键，值是带引号的 exe 全路径，
    // 不带任何命令行参数；关闭时删掉值，并删掉 Run 键本身。
    constexpr const wchar_t* runKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    constexpr const wchar_t* runValue = L"ScreenShut";
    HKEY hKey{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, runKey, 0, nullptr, 0, KEY_SET_VALUE,
        nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        if (autoStart) {
            wchar_t buffer[MAX_PATH]{};
            GetModuleFileNameW(nullptr, buffer, MAX_PATH);
            const std::wstring command = wfmt(L"\"%s\"", buffer);
            RegSetValueExW(hKey, runValue, 0, REG_SZ,
                reinterpret_cast<const BYTE*>(command.c_str()),
                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
        }
        else {
            RegDeleteValueW(hKey, runValue);
            RegCloseKey(hKey);
            // 键里若还有别的程序的值，RegDeleteKeyW 会失败（不会误删别人的自启项）。
            RegDeleteKeyW(HKEY_CURRENT_USER, runKey);
        }
    }
    auto common = configObj.GetNamedObject(L"common", nullptr);
    if (!common) {
        common = JsonObject();
        configObj.SetNamedValue(L"common", common);
    }
    common.SetNamedValue(L"autoStart", JsonValue::CreateBooleanValue(autoStart));
    save();
}

bool Setting::getAutoStart()
{
    auto common = configObj.GetNamedObject(L"common", nullptr);
    // 默认不开启开机自启
    return common && common.GetNamedBoolean(L"autoStart", false);
}

// 每次启动都按当前配置同步一次 Run 项：开启时用当前 exe 路径覆盖（换目录 / 升级后自动接管），
// 关闭时清掉残留的值和键。与 TrafficMonitor 的 SyncAutoStart() 行为一致。
void Setting::syncAutoStart()
{
    setAutoStart(getAutoStart());
}

std::filesystem::path Setting::initDataPath()
{
    // 全部数据固定在 exe 同目录的 AppData\config 下（不再使用系统 %APPDATA%，
    // 也不再支持 exe 旁的便携 config.json）。目录由 Util::initAppDataDirs() 在启动时创建。
    return Util::configDir();
}

std::filesystem::path Setting::initConfigPath()
{
    return this->dataPath / L"config.json";
}

std::filesystem::path Setting::initCachePath()
{
    // 缓存默认放在 exe 同目录的 AppData\temp（只放图标缓存等临时数据），
    // 不再写到 AppData\config\Cache，也不使用系统 LocalAppData / Roaming。
    return Util::tempDir();
}

std::filesystem::path Setting::getCacheRoot()
{
    std::error_code ec;
    if (!std::filesystem::exists(cachePath, ec)) std::filesystem::create_directories(cachePath, ec);
    return cachePath;
}

std::filesystem::path Setting::resolveCachePath()
{
    auto custom = getCacheDir();
    if (custom.empty()) return initCachePath();
    std::filesystem::path p{ custom };
    if (p.is_relative()) p = dataPath / p;
    return p.lexically_normal();
}

std::wstring Setting::getCacheDir() { return featStr(L"cacheDir", L""); }

std::filesystem::path Setting::getDefaultCacheRoot() { return initCachePath(); }

bool Setting::setCacheDir(const std::wstring& dir)
{
    std::wstring v = dir;
    const auto first = v.find_first_not_of(L" \t\r\n");
    const auto last = v.find_last_not_of(L" \t\r\n");
    v = (first == std::wstring::npos) ? L"" : v.substr(first, last - first + 1);

    const auto def = initCachePath();
    std::filesystem::path target;
    std::wstring stored;
    if (v.empty() || _wcsicmp(v.c_str(), def.c_str()) == 0) {
        target = def;
        stored.clear();
    }
    else {
        target = std::filesystem::path{ v };
        if (target.is_relative()) target = dataPath / target;
        target = target.lexically_normal();
        // 自定义时选的是"放在哪"，实际缓存根统一收在 <所选目录>\ScreenShut\Cache，
        // 避免目标位置直接出现一个泛泛的 Cache 目录，分不清是哪个软件的。
        if (!(_wcsicmp(target.filename().c_str(), L"Cache") == 0 &&
              _wcsicmp(target.parent_path().filename().c_str(), L"ScreenShut") == 0)) {
            if (_wcsicmp(target.filename().c_str(), L"ScreenShut") == 0)
                target /= L"Cache";
            else {
                target /= L"ScreenShut";
                target /= L"Cache";
            }
        }
        stored = target.wstring();
    }
    if (_wcsicmp(target.wstring().c_str(), cachePath.wstring().c_str()) == 0) return true;

    const auto oldRoot = cachePath;
    // 不允许把新目录放到旧缓存目录里面
    {
        const std::wstring oldPrefix = oldRoot.wstring() + L"\\";
        const std::wstring newPath = target.wstring();
        if (newPath.size() > oldPrefix.size() &&
            _wcsnicmp(newPath.c_str(), oldPrefix.c_str(), oldPrefix.size()) == 0)
            return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(target, ec);
    if (ec && !std::filesystem::is_directory(target, ec)) return false;

    setFeatStr(L"cacheDir", stored);
    cachePath = target;
    return true;
}

void Setting::save()
{
    // 这次启动没读出配置：只读运行，避免默认值把用户的原文件冲掉
    if (configReadOnly) return;
    // 设置项会从多个线程改（下载回调、剪贴板监听、热键线程），串行化落盘，
    // 免得两处同时写临时文件/替换目标。原子性本身由 writeConfigAtomic 保证。
    static std::mutex saveMutex;
    std::wstring str{ configObj.Stringify() };
    bool failed = false;
    {
        std::lock_guard<std::mutex> lock(saveMutex);
        // 内容未变化时不重复写入。设置页与启动清理中存在大量值未变的保存调用，
        // 在同步盘上会造成不必要的上传。
        if (str == lastSavedConfig) return;

        // config.json 可能已被其他电脑修改。写入前先读取磁盘内容，将本进程相对
        // lastSavedConfig 的改动合并到最新配置，避免直接覆盖对方修改并触发 OneDrive 冲突副本。
        std::error_code ec;
        const bool exists = std::filesystem::exists(configPath, ec);
        if (exists) {
            std::wstring disk;
            if (!readConfigContent(configPath, disk)) {
                // 读取失败时暂不写入，避免覆盖未知内容；下次保存时重试
                failed = true;
            }
            else if (disk != lastSavedConfig) {
                JsonObject diskObj{ nullptr };
                if (JsonObject::TryParse(disk, diskObj)) {
                    JsonObject baseObj{ nullptr };
                    if (!lastSavedConfig.empty()) JsonObject::TryParse(lastSavedConfig, baseObj);
                    if (!baseObj) baseObj = JsonObject{};
                    ConfigMerge::MergeObjects(diskObj, baseObj, configObj);
                    configObj = diskObj;   // 合并结果同时更新内存中的配置
                    str = std::wstring{ configObj.Stringify() };
                    if (str == disk) {     // 合并结果与磁盘一致，无需重复写入
                        lastSavedConfig = disk;
                        return;
                    }
                }
                else {
                    // 磁盘文件损坏（例如同步中断导致内容不完整）：先备份到 .bad，再写入当前配置
                    std::error_code cec;
                    std::filesystem::copy_file(configPath, siblingOf(configPath, L".bad"),
                        std::filesystem::copy_options::overwrite_existing, cec);
                }
            }
        }

        if (!failed) {
            if (writeConfigAtomic(configPath, str)) {
                lastSavedConfig = str;
                return;
            }
            failed = true;
        }
    }
    // 写入失败时需提示用户，避免误认为已保存。后台运行时不弹窗，
    // 仅在前台操作时提示一次。
    // 提示框必须在 saveMutex 之外：MessageBox 会处理消息，可能重入 save()。
    if (failed && !configSaveWarned && isOwnProcessForeground()) {
        configSaveWarned = true;
        MessageBox(nullptr,
            L"配置未能写入 config.json（可能被 OneDrive / 杀毒软件临时占用）。\n"
            L"本次修改可能没有保存，稍后再改任意设置会重新尝试。\n\n"
            L"Could not write config.json (it may be temporarily locked by OneDrive or antivirus).",
            L"ScreenShut", MB_OK | MB_ICONWARNING);
    }
}

JsonObject Setting::getToolObj(const std::wstring& tool)
{
    // 用带默认值的重载：这两层在旧配置文件里都不存在，直接 GetNamedObject 会抛异常，
    // 值被手工改成非对象时它也一样返回默认值，不会炸
    auto root = configObj.GetNamedObject(L"toolPin", nullptr);
    if (!root) {
        root = JsonObject();
        configObj.SetNamedValue(L"toolPin", root);
    }
    if (root.HasKey(tool) && root.GetNamedValue(tool).ValueType() != JsonValueType::Object)
        root.SetNamedValue(tool, JsonObject{});
    auto obj = root.GetNamedObject(tool, nullptr);
    if (!obj) {
        obj = JsonObject();
        root.SetNamedValue(tool, obj);
    }
    return obj;
}

bool Setting::getToolFlag(const std::wstring& tool, const std::wstring& key, bool def)
{
    auto obj = getToolObj(tool);
    if (!obj.HasKey(key) || obj.GetNamedValue(key).ValueType() != JsonValueType::Boolean) return def;
    return obj.GetNamedValue(key).GetBoolean();
}

void Setting::setToolFlag(const std::wstring& tool, const std::wstring& key, bool val)
{
    getToolObj(tool).SetNamedValue(key, JsonValue::CreateBooleanValue(val));
    save();
}

float Setting::getToolNum(const std::wstring& tool, const std::wstring& key, float def)
{
    auto obj = getToolObj(tool);
    if (!obj.HasKey(key) || obj.GetNamedValue(key).ValueType() != JsonValueType::Number) return def;
    return static_cast<float>(obj.GetNamedValue(key).GetNumber());
}

void Setting::setToolNum(const std::wstring& tool, const std::wstring& key, float val)
{
    getToolObj(tool).SetNamedValue(key, JsonValue::CreateNumberValue(val));
    save();
}

UINT32 Setting::getToolUInt(const std::wstring& tool, const std::wstring& key, UINT32 def)
{
    auto obj = getToolObj(tool);
    if (!obj.HasKey(key) || obj.GetNamedValue(key).ValueType() != JsonValueType::Number) return def;
    return static_cast<UINT32>(obj.GetNamedValue(key).GetNumber());
}

void Setting::setToolUInt(const std::wstring& tool, const std::wstring& key, UINT32 val)
{
    getToolObj(tool).SetNamedValue(key, JsonValue::CreateNumberValue(static_cast<double>(val)));
    save();
}

std::wstring Setting::getToolStr(const std::wstring& tool, const std::wstring& key, const std::wstring& def)
{
    auto obj = getToolObj(tool);
    if (!obj.HasKey(key) || obj.GetNamedValue(key).ValueType() != JsonValueType::String) return def;
    return std::wstring{ obj.GetNamedValue(key).GetString().c_str() };
}

void Setting::setToolStr(const std::wstring& tool, const std::wstring& key, const std::wstring& val)
{
    getToolObj(tool).SetNamedValue(key, JsonValue::CreateStringValue(val));
    save();
}

void Setting::removeToolKey(const std::wstring& tool, const std::wstring& key)
{
    // 只删已存在的键，不额外创建 toolPin/<tool> 这类空对象
    auto root = configObj.GetNamedObject(L"toolPin", nullptr);
    if (!root) return;
    auto obj = root.GetNamedObject(tool, nullptr);
    if (!obj || !obj.HasKey(key)) return;
    obj.Remove(key);
    save();
}

void Setting::normalizeConfigSections()
{
    if (!configObj) configObj = JsonObject{};
    // 顶层节按对象读取；类型异常时统一修正，避免 GetNamedObject 崩溃。
    for (const wchar_t* key : { L"common", L"features", L"toolPin", L"shortcutKey" }) {
        if (configObj.HasKey(key)
            && configObj.GetNamedValue(key).ValueType() != JsonValueType::Object) {
            configObj.SetNamedValue(key, JsonObject{});
        }
    }
    auto ensureType = [](JsonObject& obj, const wchar_t* key, JsonValueType type) {
        if (obj.HasKey(key) && obj.GetNamedValue(key).ValueType() != type) obj.Remove(key);
    };
    if (auto common = configObj.GetNamedObject(L"common", nullptr)) {
        ensureType(common, L"autoStart", JsonValueType::Boolean);
        ensureType(common, L"pluginDir", JsonValueType::String);
    }
    if (auto features = configObj.GetNamedObject(L"features", nullptr)) {
        ensureType(features, L"trayIconStyle", JsonValueType::Number);
        ensureType(features, L"trayIconStyle2", JsonValueType::Number);
        ensureType(features, L"trayCustomColor", JsonValueType::Number);
        ensureType(features, L"pinBorderColor", JsonValueType::Number);
        ensureType(features, L"mouseHighlightColor", JsonValueType::Number);
    }
}

JsonObject Setting::featuresObj()
{
	if (!configObj) configObj = JsonObject{};
	auto features = getFeaturesObjectOrNull(configObj);
	if (!features) {
		configObj.SetNamedValue(L"features", JsonObject{});
		return configObj.GetNamedObject(L"features");
	}
	return features;
}

bool Setting::featBool(const wchar_t* key, bool def)
{
	auto f = featuresObj();
	if (!f.HasKey(key) || f.GetNamedValue(key).ValueType() != JsonValueType::Boolean) return def;
	return f.GetNamedValue(key).GetBoolean();
}
void Setting::setFeatBool(const wchar_t* key, bool v)
{
	featuresObj().SetNamedValue(key, JsonValue::CreateBooleanValue(v));
	save();
}
int Setting::featInt(const wchar_t* key, int def)
{
	auto f = featuresObj();
	if (!f.HasKey(key) || f.GetNamedValue(key).ValueType() != JsonValueType::Number) return def;
	return static_cast<int>(f.GetNamedValue(key).GetNumber());
}
void Setting::setFeatInt(const wchar_t* key, int v)
{
	featuresObj().SetNamedValue(key, JsonValue::CreateNumberValue(v));
	save();
}
std::wstring Setting::featStr(const wchar_t* key, const wchar_t* def)
{
	auto f = featuresObj();
	if (!f.HasKey(key) || f.GetNamedValue(key).ValueType() != JsonValueType::String) return def;
	return std::wstring{ f.GetNamedValue(key).GetString().c_str() };
}
void Setting::setFeatStr(const wchar_t* key, const std::wstring& v)
{
	featuresObj().SetNamedValue(key, JsonValue::CreateStringValue(v));
	save();
}
JsonArray Setting::featArray(const wchar_t* key)
{
	auto f = featuresObj();
	if (!f.HasKey(key) || f.GetNamedValue(key).ValueType() != JsonValueType::Array)
		return JsonArray{};
	return f.GetNamedArray(key);
}
void Setting::setFeatArray(const wchar_t* key, const JsonArray& arr)
{
	featuresObj().SetNamedValue(key, arr);
	save();
}

int Setting::getOcrEngine() { return featInt(L"ocrEngine", 0); }
void Setting::setOcrEngine(int v) { setFeatInt(L"ocrEngine", v); }
// 没显式选过模型时按旧档位 int（0=轻量 1=标准）映射成目录名；
// 映射出的目录若已不在，引擎侧会回落到第一个可用模型（见 LwPpOcrEngine::resolveModelDir）
std::wstring Setting::getOcrModelDir()
{
	const std::wstring v = featStr(L"ocrModelDir", L"");
	if (!v.empty()) return v;
	return featInt(L"ocrPackVariant", 0) == 1 ? L"ppocr-v6" : L"ppocr-v6-tiny";
}
void Setting::setOcrModelDir(const std::wstring& v) { setFeatStr(L"ocrModelDir", v); }
int Setting::getOcrResultMode() { return featInt(L"ocrResultMode", 0); }
void Setting::setOcrResultMode(int v) { setFeatInt(L"ocrResultMode", std::clamp(v, 0, 1)); }
std::wstring Setting::getTranslateProvider() { return featStr(L"translateProvider", L"youdao"); }
void Setting::setTranslateProvider(const std::wstring& v) { setFeatStr(L"translateProvider", v); }
std::wstring Setting::getTranslateSourceLang() { return featStr(L"translateSourceLang", L"auto"); }
void Setting::setTranslateSourceLang(const std::wstring& v) { setFeatStr(L"translateSourceLang", v); }
// 默认目标语言为英语（即默认中译英）
std::wstring Setting::getTranslateTargetLang() { return featStr(L"translateTargetLang", L"en"); }
void Setting::setTranslateTargetLang(const std::wstring& v) { setFeatStr(L"translateTargetLang", v); }
bool Setting::getTranslateDictMode() { return featBool(L"translateDictMode", false); }
bool Setting::getQuickTranslateNewSession() { return featBool(L"quickTranslateNewSession", false); }
void Setting::setQuickTranslateNewSession(bool on) { setFeatBool(L"quickTranslateNewSession", on); }
// 微软翻译语气（仅微软在线翻译用）：standard 标准 / informal 非正式 / formal 正式
std::wstring Setting::getTranslateMsTone() { return featStr(L"translateMsTone", L"standard"); }
void Setting::setTranslateMsTone(const std::wstring& v) { setFeatStr(L"translateMsTone", v); }
void Setting::setTranslateDictMode(bool on) { setFeatBool(L"translateDictMode", on); }
bool Setting::getFeatureTips() { return featBool(L"featureTips", true); }
void Setting::setFeatureTips(bool on) { setFeatBool(L"featureTips", on); }
bool Setting::getHotkeyTip() { return featBool(L"hotkeyTip", false); }
void Setting::setHotkeyTip(bool on) { setFeatBool(L"hotkeyTip", on); }

bool Setting::getFindWindowElements() { return featBool(L"findWindowElements", true); }
void Setting::setFindWindowElements(bool on) { setFeatBool(L"findWindowElements", on); }
int Setting::getPinWheelAction()
{
	// 老配置只有布尔 pinZoomAtCursor（true = 光标中心缩放，false = 对向边缘缩放）。
	// 改成三档后以 pinWheelAction 为准；读不到时才按老布尔映射，保留老用户原来的手感。
	const int v = featInt(L"pinWheelAction", -1);
	if (v >= 0 && v <= 2) return v;
	return featBool(L"pinZoomAtCursor", true) ? 0 : 1;
}
void Setting::setPinWheelAction(int v)
{
	v = std::clamp(v, 0, 2);
	auto f = featuresObj();
	f.SetNamedValue(L"pinWheelAction", JsonValue::CreateNumberValue(v));
	// 老版本只认这个布尔：顺手同步一份，降级回去时至少不会整档丢失
	//（透明度档在老版本里没有对应行为，按「光标中心缩放」处理）。
	f.SetNamedValue(L"pinZoomAtCursor", JsonValue::CreateBooleanValue(v != 1));
	save();
}
int Setting::getCaptureToolbarAlign() { return featInt(L"captureToolbarAlign", 0); }
void Setting::setCaptureToolbarAlign(int v) { setFeatInt(L"captureToolbarAlign", v < 0 ? 0 : (v > 2 ? 2 : v)); }
int Setting::getToolbarSize() { return featInt(L"toolbarSize", 1); }
void Setting::setToolbarSize(int v)
{
    setFeatInt(L"toolbarSize", v < 0 ? 0 : (v > 2 ? 2 : v));
    for (auto& listener : toolbarSizeListeners) {
        if (listener) listener();
    }
}

void Setting::addToolbarSizeListener(std::function<void()> listener)
{
    if (listener) toolbarSizeListeners.push_back(std::move(listener));
}
bool Setting::getSilentCapture() { return featBool(L"silentCapture", true); }
void Setting::setSilentCapture(bool on) { setFeatBool(L"silentCapture", on); }
bool Setting::getPauseHotkeysFullscreen() { return featBool(L"pauseHotkeysFullscreen", false); }
void Setting::setPauseHotkeysFullscreen(bool on) { setFeatBool(L"pauseHotkeysFullscreen", on); }
void Setting::setHotkeysSuppressed(bool on)
{
    if (hotkeysSuppressed_ == on) return;
    hotkeysSuppressed_ = on;
    if (on) suspendShortcutKeys();    // 摘掉 RegisterHotKey 与兜底钩子
    else resumeShortcutKeys();        // 按当前配置重新注册
}
bool Setting::getPinAutoOcr() { return featBool(L"pinAutoOcr", false); }
void Setting::setPinAutoOcr(bool on) { setFeatBool(L"pinAutoOcr", on); }
bool Setting::getPinAutoFit() { return featBool(L"pinAutoFit", true); }
void Setting::setPinAutoFit(bool on) { setFeatBool(L"pinAutoFit", on); }
int Setting::getPinDoubleClickAction() { return featInt(L"pinDoubleClickAction", 0); }
void Setting::setPinDoubleClickAction(int v) { setFeatInt(L"pinDoubleClickAction", v); }
int Setting::getPinOpacity() { return featInt(L"pinOpacity", 100); }
void Setting::setPinOpacity(int v) { setFeatInt(L"pinOpacity", v); }
int Setting::getDemoDefaultTool() { return featInt(L"demoDefaultTool", 0); }
void Setting::setDemoDefaultTool(int v) { setFeatInt(L"demoDefaultTool", v); }
int Setting::getDemoEdgeBehavior() { return featInt(L"demoEdgeBehavior", 0); }
void Setting::setDemoEdgeBehavior(int v) { setFeatInt(L"demoEdgeBehavior", v); }
int Setting::getToolDefaultWidth()
{
	return std::clamp(featInt(L"toolDefaultWidth", 4), 1, 10);
}
void Setting::setToolDefaultWidth(int v)
{
	setFeatInt(L"toolDefaultWidth", std::clamp(v, 1, 10));
}
// —— 初始属性 ——
namespace {
	JsonObject toolDesignRoot(JsonObject& features)
	{
		if (!features.HasKey(L"toolDesign")
			|| features.GetNamedValue(L"toolDesign").ValueType() != JsonValueType::Object)
			features.SetNamedValue(L"toolDesign", JsonObject{});
		return features.GetNamedObject(L"toolDesign");
	}
}
bool Setting::hasToolDesign(const std::wstring& toolId)
{
	if (toolId.empty()) return false;
	auto features = featuresObj();
	if (!features.HasKey(L"toolDesign")
		|| features.GetNamedValue(L"toolDesign").ValueType() != JsonValueType::Object)
		return false;
	auto design = features.GetNamedObject(L"toolDesign");
	if (!design.HasKey(toolId)) return false;
	return design.GetNamedValue(toolId).ValueType() == JsonValueType::Object;
}
int Setting::getToolDesignInt(const std::wstring& toolId, const std::wstring& key, int def)
{
	if (toolId.empty()) return def;
	auto features = featuresObj();
	if (!features.HasKey(L"toolDesign")
		|| features.GetNamedValue(L"toolDesign").ValueType() != JsonValueType::Object)
		return def;
	auto design = features.GetNamedObject(L"toolDesign");
	if (!design.HasKey(toolId)) return def;
	auto one = design.GetNamedObject(toolId, nullptr);
	if (!one || !one.HasKey(key)) return def;
	return (int)one.GetNamedNumber(key, (double)def);
}
void Setting::setToolDesignInt(const std::wstring& toolId, const std::wstring& key, int value)
{
	if (toolId.empty() || key.empty()) return;
	auto features = featuresObj();
	auto design = toolDesignRoot(features);
	JsonObject one = design.HasKey(toolId) ? design.GetNamedObject(toolId, JsonObject{}) : JsonObject{};
	one.SetNamedValue(key, JsonValue::CreateNumberValue((double)value));
	design.SetNamedValue(toolId, one);
	features.SetNamedValue(L"toolDesign", design);
	features.SetNamedValue(L"toolDesignUsed", JsonValue::CreateBooleanValue(true));
	save();
}
std::wstring Setting::getToolDesignStr(const std::wstring& toolId, const std::wstring& key, const std::wstring& def)
{
	if (toolId.empty() || key.empty()) return def;
	auto features = featuresObj();
	if (!features.HasKey(L"toolDesign")
		|| features.GetNamedValue(L"toolDesign").ValueType() != JsonValueType::Object)
		return def;
	auto design = features.GetNamedObject(L"toolDesign");
	if (!design.HasKey(toolId)) return def;
	auto one = design.GetNamedObject(toolId, nullptr);
	if (!one || !one.HasKey(key)) return def;
	auto v = one.GetNamedValue(key);
	if (v.ValueType() != JsonValueType::String) return def;
	return std::wstring{ one.GetNamedString(key, def.c_str()) };
}
void Setting::setToolDesignStr(const std::wstring& toolId, const std::wstring& key, const std::wstring& value)
{
	if (toolId.empty() || key.empty()) return;
	auto features = featuresObj();
	auto design = toolDesignRoot(features);
	JsonObject one = design.HasKey(toolId) ? design.GetNamedObject(toolId, JsonObject{}) : JsonObject{};
	one.SetNamedValue(key, JsonValue::CreateStringValue(value));
	design.SetNamedValue(toolId, one);
	features.SetNamedValue(L"toolDesign", design);
	features.SetNamedValue(L"toolDesignUsed", JsonValue::CreateBooleanValue(true));
	save();
}
bool Setting::hasAnyToolDesign()
{
	return featBool(L"toolDesignUsed", false);
}
void Setting::clearToolDesign()
{
	auto features = featuresObj();
	if (features.HasKey(L"toolDesign")) features.Remove(L"toolDesign");
	features.SetNamedValue(L"toolDesignUsed", JsonValue::CreateBooleanValue(false));
	save();
}
bool Setting::getRememberToolStyle() { return featBool(L"rememberToolStyle", false); }
void Setting::setRememberToolStyle(bool on) { setFeatBool(L"rememberToolStyle", on); }
namespace {
	JsonObject toolLastRoot(JsonObject& features)
	{
		if (!features.HasKey(L"toolLast")
			|| features.GetNamedValue(L"toolLast").ValueType() != JsonValueType::Object)
			features.SetNamedValue(L"toolLast", JsonObject{});
		return features.GetNamedObject(L"toolLast");
	}
}
int Setting::getToolLastInt(const std::wstring& toolId, const std::wstring& key, int def)
{
	if (toolId.empty() || key.empty()) return def;
	auto features = featuresObj();
	if (!features.HasKey(L"toolLast")
		|| features.GetNamedValue(L"toolLast").ValueType() != JsonValueType::Object)
		return def;
	auto last = features.GetNamedObject(L"toolLast");
	if (!last.HasKey(toolId)) return def;
	auto one = last.GetNamedObject(toolId, nullptr);
	if (!one || !one.HasKey(key)) return def;
	return (int)one.GetNamedNumber(key, (double)def);
}
void Setting::setToolLastInt(const std::wstring& toolId, const std::wstring& key, int value)
{
	if (toolId.empty() || key.empty()) return;
	auto features = featuresObj();
	auto last = toolLastRoot(features);
	JsonObject one = last.HasKey(toolId) ? last.GetNamedObject(toolId, JsonObject{}) : JsonObject{};
	one.SetNamedValue(key, JsonValue::CreateNumberValue((double)value));
	last.SetNamedValue(toolId, one);
	features.SetNamedValue(L"toolLast", last);
	save();
}
int Setting::getLastArrowHead() { return featInt(L"lastArrowHead", 0); }
void Setting::setLastArrowHead(int v) { setFeatInt(L"lastArrowHead", v); }
bool Setting::getAutoSaveAfterCapture() { return featBool(L"autoSaveAfterCapture", false); }
void Setting::setAutoSaveAfterCapture(bool on) { setFeatBool(L"autoSaveAfterCapture", on); }
std::wstring Setting::getSaveFormat() { return featStr(L"saveFormat", L"PNG (*.png)"); }
void Setting::setSaveFormat(const std::wstring& v) { setFeatStr(L"saveFormat", v); }
int Setting::getCaptureFinishAction()
{
	// 老配置只有布尔 copyAsFile：true = 以文件复制，false = 复制图片。
	// 迁移成多档时保持它们原来的行为，之后就以 captureFinishAction 为准。
	const int v = featInt(L"captureFinishAction", -1);
	if (v >= 0 && v <= 3) return v;
	return featBool(L"copyAsFile", false) ? 1 : 0;
}
void Setting::setCaptureFinishAction(int v) { setFeatInt(L"captureFinishAction", v < 0 ? 0 : (v > 3 ? 3 : v)); }
bool Setting::getCancelConfirmDialog() { return featBool(L"cancelConfirmDialog", false); }
void Setting::setCancelConfirmDialog(bool on) { setFeatBool(L"cancelConfirmDialog", on); }

void Setting::restoreFunctionScreenshotDefaults()
{
	setAutoStart(false);
	setFindWindowElements(true);
	setCancelConfirmDialog(false);
	setAutoSaveAfterCapture(false);
	setCaptureFinishAction(0);
	setSaveFormat(L"PNG (*.png)");
	setDemoDefaultTool(0);
	setDemoEdgeBehavior(0);
	setPinWheelAction(0);
	setPinAutoOcr(false);
	setPinAutoFit(true);
	setPinDoubleClickAction(0);
	setPinOpacity(100);
}

// 「功能设置」各子页的「恢复默认」：把该子页的每一项写回出厂值。
// 视频录制：编码/画质/帧率/GIF/声音/录制附加项，外加高亮圆颜色（删覆盖项 → 回落默认品牌绿）
void Setting::restoreFunctionRecordDefaults()
{
	setVideoQuality(4);
	setVideoFps(0);
	setGifQuality(1);
	setGifFps(1);
	setGifFormat(0);
	setRecordMic(0);
	setVideoEncoder(0);
	setEncodeSpeed(1);
	setHwAccel(true);
	setHideToolbarInRecord(true);
	setKeyOverlayPos(kKeyOverlayNone);
	setSelFollowMouse(false);
	setRecordToolbarPeek(false);
	setRecordDefaultPierce(false);
	setMouseTrailEffect(kMouseTrailNone);
	setMouseClickEffect(kMouseClickNone);
	featuresObj().Remove(L"mouseHighlightColor");
	featuresObj().Remove(L"mouseHighlight");   // 旧版布尔开关一并清掉
	save();
}

// 文件输出：目录清空即"没改过"，界面回到灰占位符（默认目录）
void Setting::restoreFunctionOutputDefaults()
{
	setScreenshotDir(L"");
	setRecordDir(L"");
}

// 文本识别：回到默认的离线 PP-OCRv6（未安装时运行时会自动回落系统 OCR），
// 模型选择清空 = 不再认目录，由引擎从 ocr-models 里自动挑
void Setting::restoreFunctionOcrDefaults()
{
	setOcrEngine(0);
	setOcrModelDir(L"");
	setOcrResultMode(0);
}

// 翻译：回到默认在线服务与「非」字典模式；语种不在本子页里，不动
void Setting::restoreFunctionTranslateDefaults()
{
	setTranslateProvider(L"youdao");
	setTranslateDictMode(false);
	setTranslateMsTone(L"standard");
	setQuickTranslateNewSession(false);
}

// 其他功能：功能提示开、快捷键提示关、记忆工具样式关、静默截图开
void Setting::restoreFunctionOtherDefaults()
{
	setFeatureTips(true);
	setHotkeyTip(false);
	setRememberToolStyle(false);
	setSilentCapture(true);
	setCaptureToolbarAlign(0);
	setPauseHotkeysFullscreen(false);
}

// —— 与上面的 restore* 成对：判断该子页是否**仍是出厂值**（供「恢复默认」按钮置灰）——
// 判据统一取"restore* 会不会真的改动什么"：每项都与 restore 写回的值比一遍。改默认值时两处一起改。
bool Setting::functionScreenshotAtDefaults()
{
	return !getAutoStart()
		&& getFindWindowElements() && !getCancelConfirmDialog() && !getAutoSaveAfterCapture()
		&& getCaptureFinishAction() == 0 && getSaveFormat() == L"PNG (*.png)"
		&& getDemoDefaultTool() == 0 && getDemoEdgeBehavior() == 0
		&& getPinWheelAction() == 0 && !getPinAutoOcr() && getPinAutoFit()
		&& getPinDoubleClickAction() == 0 && getPinOpacity() == 100;
}

bool Setting::functionRecordAtDefaults()
{
	return getVideoQuality() == 4 && getVideoFps() == 0 && getGifQuality() == 1
		&& getGifFps() == 1 && getGifFormat() == 0 && getRecordMic() == 0
		&& getVideoEncoder() == 0 && getEncodeSpeed() == 1 && getHwAccel()
		&& getHideToolbarInRecord() && getKeyOverlayPos() == kKeyOverlayNone && !getSelFollowMouse()
		&& !getRecordToolbarPeek() && !getRecordDefaultPierce()
		&& getMouseTrailEffect() == kMouseTrailNone
		&& getMouseClickEffect() == kMouseClickNone
		&& getMouseHighlightColor() == 0x0FDC78FF;
}

bool Setting::functionOutputAtDefaults()
{
	return getScreenshotDir().empty() && getRecordDir().empty();
}

bool Setting::functionOcrAtDefaults()
{
	// 模型目录按"没显式选过"（键为空）算默认；直接查原始键，不走旧档位映射
	return getOcrEngine() == 0 && getOcrResultMode() == 0
		&& featStr(L"ocrModelDir", L"").empty();
}

bool Setting::functionTranslateAtDefaults()
{
	return getTranslateProvider() == L"youdao" && !getTranslateDictMode()
		&& getTranslateMsTone() == L"standard" && !getQuickTranslateNewSession();
}

bool Setting::functionOtherAtDefaults()
{
	return getFeatureTips() && !getHotkeyTip()
		&& !getRememberToolStyle()
		&& getSilentCapture() && getCaptureToolbarAlign() == 0
		&& !getPauseHotkeysFullscreen();
}

int Setting::getVideoQuality() { return featInt(L"videoQuality", 4); }
void Setting::setVideoQuality(int v) { setFeatInt(L"videoQuality", v); }
int Setting::getVideoFps() { return featInt(L"videoFps", 0); }
void Setting::setVideoFps(int v) { setFeatInt(L"videoFps", v); }
int Setting::getGifQuality() { return featInt(L"gifQuality", 1); }
void Setting::setGifQuality(int v) { setFeatInt(L"gifQuality", v); }
int Setting::getGifFps() { return featInt(L"gifFps", 1); }
void Setting::setGifFps(int v) { setFeatInt(L"gifFps", v); }
int Setting::getGifFormat() { return featInt(L"gifFormat", 0); }
void Setting::setGifFormat(int v) { setFeatInt(L"gifFormat", v); }
int Setting::getRecordMic() { return featInt(L"recordMic", 0); }
void Setting::setRecordMic(int v) { setFeatInt(L"recordMic", v); }
int Setting::getVideoEncoder() { return featInt(L"videoEncoder", 0); }
void Setting::setVideoEncoder(int v) { setFeatInt(L"videoEncoder", v); }
int Setting::getEncodeSpeed() { return featInt(L"encodeSpeed", 1); }
void Setting::setEncodeSpeed(int v) { setFeatInt(L"encodeSpeed", v); }
bool Setting::getHwAccel() { return featBool(L"hwAccel", true); }
void Setting::setHwAccel(bool on) { setFeatBool(L"hwAccel", on); }
bool Setting::getHideToolbarInRecord() { return featBool(L"hideToolbarInRecord", true); }
void Setting::setHideToolbarInRecord(bool on) { setFeatBool(L"hideToolbarInRecord", on); }
int Setting::getKeyOverlayPos()
{
	const int v = featInt(L"keyOverlayPos", -1);
	if (v >= kKeyOverlayLeft && v <= kKeyOverlayNone) return v;
	// 老配置只有布尔 showKeystrokes（开/关）：开过就按「视窗左侧」显示，没开过就是关闭
	return featBool(L"showKeystrokes", false) ? kKeyOverlayLeft : kKeyOverlayNone;
}
void Setting::setKeyOverlayPos(int pos)
{
	setFeatInt(L"keyOverlayPos", std::clamp(pos, kKeyOverlayLeft, kKeyOverlayNone));
}
bool Setting::getSelFollowMouse() { return featBool(L"selFollowMouse", false); }
void Setting::setSelFollowMouse(bool on) { setFeatBool(L"selFollowMouse", on); }
bool Setting::getRecordToolbarPeek() { return featBool(L"recordToolbarPeek", false); }
void Setting::setRecordToolbarPeek(bool on) { setFeatBool(L"recordToolbarPeek", on); }
bool Setting::getRecordDefaultPierce() { return featBool(L"recordDefaultPierce", false); }
void Setting::setRecordDefaultPierce(bool on) { setFeatBool(L"recordDefaultPierce", on); }
int Setting::getMouseTrailEffect()
{
	auto f = featuresObj();
	if (f.HasKey(L"mouseTrailEffect")
		&& f.GetNamedValue(L"mouseTrailEffect").ValueType() == JsonValueType::Number) {
		const int v = static_cast<int>(f.GetNamedValue(L"mouseTrailEffect").GetNumber());
		// 旧配置里的 3 = 已删除的「局部放大」，统一回落到「无」
		if (v < kMouseTrailHighlight || v > kMouseTrailNone) return kMouseTrailNone;
		return v;
	}
	// 旧配置只有布尔 mouseHighlight（开 = 高亮提醒，关 = 无）
	return featBool(L"mouseHighlight", false) ? kMouseTrailHighlight : kMouseTrailNone;
}
void Setting::setMouseTrailEffect(int effect)
{
	const int v = (effect < kMouseTrailHighlight || effect > kMouseTrailNone)
		? kMouseTrailNone : effect;
	setFeatInt(L"mouseTrailEffect", v);
	// 新键已经落盘，旧布尔开关不再需要（避免以后读不到新键时又按旧值回退）
	if (featuresObj().HasKey(L"mouseHighlight")) {
		featuresObj().Remove(L"mouseHighlight");
		save();
	}
}
bool Setting::getMouseHighlight() { return getMouseTrailEffect() == kMouseTrailHighlight; }
void Setting::setMouseHighlight(bool on)
{
	setMouseTrailEffect(on ? kMouseTrailHighlight : kMouseTrailNone);
}
int Setting::getMouseClickEffect()
{
	const int v = featInt(L"mouseClickEffect", kMouseClickNone);
	// 2 是旧版的「无」，3 是新增的「光标波纹组合」；未知值一律按「无」
	if (v < kMouseClickRipple || v > kMouseClickCombo) return kMouseClickNone;
	return v;
}
void Setting::setMouseClickEffect(int effect)
{
	const int v = (effect < kMouseClickRipple || effect > kMouseClickCombo)
		? kMouseClickNone : effect;
	setFeatInt(L"mouseClickEffect", v);
}
UINT32 Setting::getMouseHighlightColor()
{
	auto f = featuresObj();
	if (!f.HasKey(L"mouseHighlightColor")
		|| f.GetNamedValue(L"mouseHighlightColor").ValueType() != JsonValueType::Number)
		return 0x0FDC78FF;   // 默认品牌绿
	return static_cast<UINT32>(f.GetNamedValue(L"mouseHighlightColor").GetNumber());
}
void Setting::setMouseHighlightColor(UINT32 rgba)
{
	featuresObj().SetNamedValue(L"mouseHighlightColor", JsonValue::CreateNumberValue(static_cast<double>(rgba)));
	save();
}

std::wstring Setting::defaultManualSaveFormat() { return L"ScreenShut_{{YYYY-MM-DD_HH-mm-ss}}"; }
std::wstring Setting::defaultAutoSaveFormat() { return L"ScreenShut_auto_{{YYYY-MM-DD_HH-mm-ss}}"; }
std::wstring Setting::defaultVideoRecordFormat() { return L"ScreenShut_Video_{{YYYY-MM-DD_HH-mm-ss}}"; }

// 文件名格式固定使用内置默认模板，不再提供自定义入口。
std::wstring Setting::getManualSaveFormat() { return defaultManualSaveFormat(); }
std::wstring Setting::getAutoSaveFormat() { return defaultAutoSaveFormat(); }
std::wstring Setting::getVideoRecordFormat() { return defaultVideoRecordFormat(); }

std::wstring Setting::getScreenshotDir() { return featStr(L"screenshotDir", L""); }
void Setting::setScreenshotDir(const std::wstring& v) { setFeatStr(L"screenshotDir", v); }
std::wstring Setting::getRecordDir() { return featStr(L"recordDir", L""); }
void Setting::setRecordDir(const std::wstring& v) { setFeatStr(L"recordDir", v); }

int Setting::getAppTheme() { return featInt(L"appTheme", 0); }
void Setting::setAppTheme(int v) { setFeatInt(L"appTheme", v); }

// 全局热键。RegisterHotKey 是"先注册先独占"的语义，没有可调优先级：组合键已被其他进程注册
// 时，本次注册会直接失败。所以这里的策略是：
//   1) 每条都先正常 RegisterHotKey；
//   2) 失败的那些攒起来，整体装一个低级键盘钩子兜底 —— 钩子先于常规热键与加速键取得按键，
//      命中后消费该事件，效果上等同于该组合由本进程持有；
//   3) 同时起一个低频重试，等其他进程释放该组合后再切回 RegisterHotKey（并摘掉钩子）。
void Setting::applyHotkeys()
{
    auto* lingApp = Ling::App::get();
    if (!lingApp) return;
    // 已让给同机的完整版：一个全局热键都不注册（否则同一按键会触发两遍动作）
    if (hotkeysYielded_) return;
    // 先全部注销，再按当前配置重注册（配置改过、或从钩子兜底切回都要走这一遍）
    for (const auto& hk : kHotkeys) lingApp->unRegHotKey(hk.msgId);

    std::vector<HotkeyHook::Binding> fallback;
    // 同一把组合键只注册一次（先到先得）：两处配了同一个键时，RegisterHotKey 只有一条能成功，
    // 另一条会退到钩子上 —— 结果是一下按键触发两个动作。这里直接跳过重复项。
    std::vector<std::wstring> usedChords;
    for (const auto& hk : kHotkeys) {
        const std::wstring chord = getShortcutKey(hk.id);
        if (chord.empty()) continue;   // 用户清空过这一行 = 不绑
        const std::wstring canonical = CommandKeys::canonicalChord(chord);
        bool dup = false;
        for (const auto& c : usedChords) {
            if (_wcsicmp(c.c_str(), canonical.c_str()) == 0) { dup = true; break; }
        }
        if (dup) continue;
        usedChords.push_back(canonical);
        if (lingApp->regHotKey(chord, hk.msgId)) continue;
        const std::wstring id{ hk.id };
        fallback.push_back({ chord, [id]() {
            if (auto* self = get()) self->runHotkeyAction(id);
        } });
    }
    HotkeyHook::setBindings(std::move(fallback));
    if (HotkeyHook::installed()) startHotkeyRetry();
    else stopHotkeyRetry();
}

// 一条全局热键要执行的动作。id 取自 kHotkeys。
void Setting::runHotkeyAction(const std::wstring& id)
{
    // 会话开着时，这条热键的键位若是会话里的「系统按键」，先交给会话执行。
    // 例：截图热键 F1 + 系统按键「贴图」F1 —— 第二次 F1 应该贴图；否则热键被 RegisterHotKey
    // 吃掉，窗口收不到按键，init() 又因为会话已开直接返回，表现就是"设了没用"。
    if (auto* cap = WinCap::get()) {
        const std::wstring chord = getShortcutKey(id);
        if (cap->commandFromChord(chord)) return;
        // 「屏幕录制」这条全局热键只负责**打开录屏功能**（进入录屏会话），不兼任
        // 「开始/暂停录制」。后者是「系统按键」里的 cmd_record_toggle，默认也是 F2，
        // 但用户改过键位后必须按新的生效 —— 两者语义不同，不能在这里按 id 兜底转派。
    }
    if (id == L"capture") { WinCap::init(); return; }
    // 托盘「OCR / 翻译」：先截图，框选完成后自动识别（+ 翻译）
    if (id == L"ocr") { WinCap::beginOcrPick(false); return; }
    if (id == L"ocr_translate") { WinCap::beginOcrPick(true); return; }
    if (id == L"quick_translate") { WinQuickTranslate::toggle(); return; }
    if (id == L"capture_fullscreen") { if (auto* app = App::get()) app->captureFullScreenSilent(); return; }
    if (id == L"capture_focused_window") { if (auto* app = App::get()) app->captureFocusedWindowSilent(); return; }
    if (id == L"fullscreen_canvas") { WinCap::beginDemoCanvas(); return; }
    if (id == L"record") { WinCap::beginRecordPick(); return; }
    if (id == L"show_main") { WinSetting::toggle(); return; }
    if (id == L"make_collage") { WinImageEdit::init(); return; }
    if (id == L"toggle_pins") { WinPin::toggleAllHidden(); return; }
    if (id == L"open_screenshot_dir") {
        const auto dir = Util::outputDir();
        if (!dir.empty()) {
            // outputDir 只解析路径不建目录：热键打开前补建一次
            std::error_code ec;
            if (!std::filesystem::exists(dir, ec)) std::filesystem::create_directories(dir, ec);
            ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        return;
    }
}

// 设置页开始录制热键前调用：临时注销全部系统热键、摘掉低级钩子，让按键能正常落到设置窗口。
void Setting::suspendShortcutKeys()
{
    if (auto* lingApp = Ling::App::get())
        for (const auto& hk : kHotkeys) lingApp->unRegHotKey(hk.msgId);
    HotkeyHook::uninstall();
    stopHotkeyRetry();
}

// 录制结束（保存或取消）后调用：按配置重新注册全部全局热键
void Setting::resumeShortcutKeys()
{
    applyHotkeys();
}

// 全局热键归谁：完整版与 Lite 版可以并存，但同一时刻只许一套热键生效。
// 交出时把已注册的全部注销（含兜底钩子），取回时按配置重新注册。
void Setting::setHotkeysYielded(bool yielded)
{
    if (hotkeysYielded_ == yielded) return;
    hotkeysYielded_ = yielded;
    if (yielded) suspendShortcutKeys();
    else applyHotkeys();
}

void Setting::startHotkeyRetry()
{
    if (hotkeyRetryTimer) return;
    hotkeyRetryTick = 0;
    // hwnd 传 NULL 的定时器：回调在装它的线程（UI 线程）的消息泵里跑，不需要窗口
    hotkeyRetryTimer = SetTimer(nullptr, kHotkeyRetryTimerId, kHotkeyRetryMs, &Setting::onHotkeyRetry);
}

void Setting::stopHotkeyRetry()
{
    if (!hotkeyRetryTimer) return;
    KillTimer(nullptr, hotkeyRetryTimer);
    hotkeyRetryTimer = 0;
    hotkeyRetryTick = 0;
}

void CALLBACK Setting::onHotkeyRetry(HWND, UINT, UINT_PTR id, DWORD)
{
    auto* self = get();
    if (!self) return;
    // hWnd 传 NULL 的线程定时器会**忽略 nIDEvent 并另发一个 id**（见 startHotkeyRetry），
    // 所以这里只能和 SetTimer 的返回值比；拿 kHotkeyRetryTimerId 比永远不相等 → 重试从不执行。
    if (id != self->hotkeyRetryTimer) return;
    ++self->hotkeyRetryTick;
    // **不设重试次数上限**。此前重试满 1 分钟即停止，导致：开机时某条键被其他程序短暂占用 →
    // 落到钩子上 → 对方释放后无线程重新抢占 → 钩子再被系统摘除或被其他程序抢占 →
    // 该热键即永久失效（实测表现为"按下数次无响应，托盘点击一次截图后恢复"）。
    // 现在只要仍有键落在钩子上，就每 30 秒重试一次，每次同时完成三件事：
    //   ① 再抢一次 RegisterHotKey —— 对方放开了就立刻回到干净路径，并把钩子摘掉；
    //   ② 重建钩子 —— 被系统摘除或上次安装失败的钩子在此重新安装（自动恢复）；
    //   ③ 清掉过期钩子线程 —— 换代后没退干净的旧线程会自己卸钩退出（见 HotkeyHook）。
    // 全都回到 RegisterHotKey 时，applyHotkeys 中会同时调用 stopHotkeyRetry()，定时器随之停止。
    self->applyHotkeys();
}

void Setting::initShortcutKeys()
{
    auto* lingApp = Ling::App::get();
    // 先挂派发，再注册：热键一注册就能命中回调，不留"注册好了但还没人接"的缝
    lingApp->onHotKey.add([this](UINT msg) {
        for (const auto& hk : kHotkeys) {
            if (msg == hk.msgId) { runHotkeyAction(hk.id); return; }
        }
    });
    // 已经有一个实例在托盘里时，再运行一次 exe：打开设置中心，而不是直接进截图模式。
    // （"打开就只挂托盘待命"这条规则的延伸：重新打开 = 把软件的窗口调出来）
    lingApp->onSecondInstance.add([this]() {
        WinSetting::init();
    });
    applyHotkeys();
}

// ================ 贴图边框外观 ================
bool Setting::getPinBorderDefaultEnabled() { return featBool(L"pinBorderDefaultEnabled", true); }
void Setting::setPinBorderDefaultEnabled(bool on) { setFeatBool(L"pinBorderDefaultEnabled", on); }
int Setting::getPinBorderWidth() { return featInt(L"pinBorderWidth", 2); }
void Setting::setPinBorderWidth(int v) { setFeatInt(L"pinBorderWidth", v); }
int Setting::getPinBorderRadius() { return featInt(L"pinBorderRadius", 6); }
void Setting::setPinBorderRadius(int v) { setFeatInt(L"pinBorderRadius", v); }
UINT32 Setting::getPinBorderColor()
{
    auto f = featuresObj();
    if (!f.HasKey(L"pinBorderColor")
        || f.GetNamedValue(L"pinBorderColor").ValueType() != JsonValueType::Number)
        return 0x34C759FF; // 默认主题绿
    return static_cast<UINT32>(f.GetNamedValue(L"pinBorderColor").GetNumber());
}
void Setting::setPinBorderColor(UINT32 argb)
{
    featuresObj().SetNamedValue(L"pinBorderColor", JsonValue::CreateNumberValue(static_cast<double>(argb)));
    save();
}

// ================ 托盘图标 ================
bool Setting::getTrayEnabled() { return featBool(L"trayEnabled", true); }
void Setting::setTrayEnabled(bool on) { setFeatBool(L"trayEnabled", on); }
int Setting::getTrayIconStyle()
{
    // 用新键 trayIconStyle2 存 4 档（0 跟随系统 / 1 主题 / 2 自定义颜色 / 3 自定义图标）。
    // 不能就地复用旧键：旧的 5 档里 暗色=2、亮色=3 与新方案的 2/3 撞号，会互相污染。
    auto f = featuresObj();
    if (f.HasKey(L"trayIconStyle2")
        && f.GetNamedValue(L"trayIconStyle2").ValueType() == JsonValueType::Number) {
        const int v = static_cast<int>(f.GetNamedValue(L"trayIconStyle2").GetNumber());
        return (v < 0 || v > 3) ? 1 : v;
    }
    // 首次读取：从旧键一次性迁移（旧：0 跟随系统 / 1 主题 / 2 暗色 / 3 亮色 / 4 自定义）
    int old = 1;
    if (f.HasKey(L"trayIconStyle")
        && f.GetNamedValue(L"trayIconStyle").ValueType() == JsonValueType::Number)
        old = static_cast<int>(f.GetNamedValue(L"trayIconStyle").GetNumber());
    int nv = 1;
    if (old == 4) nv = 3;                   // 旧「自定义」→ 自定义图标
    else if (old == 2 || old == 3) nv = 1;  // 旧 暗色/亮色 → 主题
    else nv = (old == 0) ? 0 : 1;           // 0 跟随系统 / 其余默认主题
    setFeatInt(L"trayIconStyle2", nv);      // 迁移结果落到新键，之后只读新键
    return nv;
}
void Setting::setTrayIconStyle(int v)
{
    setFeatInt(L"trayIconStyle2", std::max(0, std::min(3, v)));
}
UINT32 Setting::getTrayCustomColor()
{
    auto f = featuresObj();
    if (!f.HasKey(L"trayCustomColor")
        || f.GetNamedValue(L"trayCustomColor").ValueType() != JsonValueType::Number)
        return 0x34C759FF;   // 默认主题绿
    return static_cast<UINT32>(f.GetNamedValue(L"trayCustomColor").GetNumber());
}
void Setting::setTrayCustomColor(UINT32 rgba)
{
    featuresObj().SetNamedValue(L"trayCustomColor", JsonValue::CreateNumberValue(static_cast<double>(rgba)));
    save();
}
std::wstring Setting::getTrayCustomIconPath() { return featStr(L"trayCustomIconPath", L""); }
void Setting::setTrayCustomIconPath(const std::wstring& path) { setFeatStr(L"trayCustomIconPath", path); }

// ================ 新增：浮动截图按钮 ================
bool Setting::getFloatingCaptureButtonShow() { return featBool(L"floatingCaptureButtonShow", true); }
void Setting::setFloatingCaptureButtonShow(bool on) { setFeatBool(L"floatingCaptureButtonShow", on); }
int Setting::getFloatingCaptureButtonClick() { return featInt(L"floatingCaptureButtonClick", 0); }
void Setting::setFloatingCaptureButtonClick(int v) { setFeatInt(L"floatingCaptureButtonClick", v); }
int Setting::getFloatingLongPressMs() { return featInt(L"floatingLongPressMs", 500); }
void Setting::setFloatingLongPressMs(int ms) { setFeatInt(L"floatingLongPressMs", ms); }

// ================ 新增：窗口黑名单 ================
std::vector<std::wstring> Setting::getWindowBlacklist()
{
    std::vector<std::wstring> list;
    for (auto const& v : featArray(L"windowBlacklist")) {
        if (v.ValueType() == JsonValueType::String)
            list.push_back(v.GetString().c_str());
    }
    return list;
}
void Setting::setWindowBlacklist(const std::vector<std::wstring>& titles)
{
    JsonArray arr;
    for (auto& t : titles) arr.Append(JsonValue::CreateStringValue(t));
    setFeatArray(L"windowBlacklist", arr);
}

// 标题、进程名（任务管理器里的名字）、程序文件说明任一命中都算。
// 有些软件（AI 对话、编辑器）的窗口标题会随当前内容/模式实时变，
// 只按标题匹配会漏；进程名是稳定的，文件说明则兜住「开始菜单显示名 ≠ 进程名」的情况。
bool Setting::isWindowBlacklisted(const std::wstring& windowTitle, const std::wstring& processName,
    const std::wstring& fileDescription)
{
    if (windowTitle.empty() && processName.empty() && fileDescription.empty()) return false;
    for (auto& entry : getWindowBlacklist()) {
        if (BlacklistMatch::matches(entry, windowTitle, processName, fileDescription)) return true;
    }
    return false;
}

// ================ 插件本地数据（存 config.json 的 pluginData 节） ================
namespace {
    std::vector<std::wstring> splitLocalKey(const std::wstring& key)
    {
        std::vector<std::wstring> parts;
        size_t start = 0;
        for (;;) {
            const size_t dot = key.find(L'.', start);
            const std::wstring part = key.substr(start, dot == std::wstring::npos ? std::wstring::npos : dot - start);
            if (!part.empty()) parts.push_back(part);
            if (dot == std::wstring::npos) break;
            start = dot + 1;
        }
        return parts;
    }
}

std::wstring Setting::getLocalDataText(const std::wstring& key)
{
    const auto parts = splitLocalKey(key);
    if (parts.empty()) return {};
    if (!configObj.HasKey(L"pluginData")
        || configObj.GetNamedValue(L"pluginData").ValueType() != JsonValueType::Object)
        return {};
    JsonObject cur = configObj.GetNamedObject(L"pluginData");
    for (size_t i = 0; i < parts.size(); ++i) {
        if (!cur || !cur.HasKey(parts[i])) return {};
        const JsonValue value = cur.GetNamedValue(parts[i]);
        if (i + 1 == parts.size())
            return value.ValueType() == JsonValueType::Object ? std::wstring{ value.Stringify() } : std::wstring{};
        if (value.ValueType() != JsonValueType::Object) return {};
        cur = value.GetObjectW();
    }
    return {};
}

bool Setting::setLocalDataText(const std::wstring& key, const std::wstring& json)
{
    const auto parts = splitLocalKey(key);
    if (parts.empty()) return false;
    JsonObject value{ nullptr };
    if (!JsonObject::TryParse(json, value) || !value) return false;
    if (getLocalDataText(key) == std::wstring{ value.Stringify() }) return true;

    JsonObject root{ nullptr };
    if (configObj.HasKey(L"pluginData")
        && configObj.GetNamedValue(L"pluginData").ValueType() == JsonValueType::Object)
        root = configObj.GetNamedObject(L"pluginData");
    if (!root) root = JsonObject{};

    JsonObject cur = root;
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        if (cur.HasKey(parts[i]) && cur.GetNamedValue(parts[i]).ValueType() == JsonValueType::Object) {
            cur = cur.GetNamedValue(parts[i]).GetObjectW();
            continue;
        }
        JsonObject next;
        cur.SetNamedValue(parts[i], next);
        cur = next;
    }
    cur.SetNamedValue(parts.back(), value);
    configObj.SetNamedValue(L"pluginData", root);
    save();
    return true;
}

