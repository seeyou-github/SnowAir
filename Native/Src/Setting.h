#pragma once
#include <include/Ling.h>
#include <filesystem>
#include <functional>
#include <winrt/Windows.Data.Json.h>
using namespace winrt::Windows::Data::Json;

class Setting
{
public:
	~Setting();
	static void init();
	static void dispose();
	static Setting* get();
	std::filesystem::path getDataPath();
	// 缓存根目录（默认 AppData\temp）：当前不存放文件，保留给用户自定义扩展使用。
	// 用户可在「文件输出 → 保存路径 → 缓存文件路径」改成别的目录。
	std::filesystem::path getCacheRoot();
	std::wstring getCacheDir();
	bool setCacheDir(const std::wstring& dir);
	std::filesystem::path getDefaultCacheRoot();
	// 插件（OCR / 翻译模型等）根目录。留空 = 自动：exe 同目录下已有 plugin 文件夹就用它
	// （绿色包 / 不想占用 C 盘），否则退回 AppData\config\plugin。见 Ocr/PluginPaths.h。
	std::wstring getPluginDir();
	void setPluginDir(const std::wstring& dir);
	const JsonObject getConfigObj();
	void setShortcutKey(const std::wstring& type, const std::vector<std::wstring>& keys);
	std::wstring getShortcutKey(const std::wstring& type);
	// 「全局热键」页的「恢复默认」：清掉用户设过的键位（清空后 getShortcutKey 逐条回落内置默认表），
	// 并按新键位整体重注册
	void restoreShortcutKeyDefaults();
	// 全局热键是否仍是出厂键位（没有任何覆盖项）
	bool shortcutKeyAtDefaults();
	// 键位冲突：chord 已被另一条全局热键占用时返回那条的 id（空 = 没冲突）。
	// 托盘右键菜单页与「全局热键」页共用同一套键位，跨页判定靠它。
	std::wstring findShortcutOwner(const std::wstring& chord, const std::wstring& selfId);

	// —— 托盘右键菜单（设置 → 右键菜单）——
	// 菜单项 id 与全局热键 id 同名：capture / ocr / ocr_translate / quick_translate /
	// record / fullscreen_canvas / settings / exit
	static std::vector<std::wstring> defaultTrayMenuOrder();
	// 当前项序（读盘顺序 + 补齐缺失项 + 丢弃未知项）
	std::vector<std::wstring> getTrayMenuOrder();
	void setTrayMenuOrder(const std::vector<std::wstring>& order);
	// 该菜单项是否出现在托盘菜单里（settings / exit 恒为 true：固定项不能关闭）
	bool getTrayMenuItemEnabled(const std::wstring& id);
	void setTrayMenuItemEnabled(const std::wstring& id, bool on);
	// settings / exit：不能关、不能拖动、不能设快捷键
	static bool trayMenuItemFixed(const std::wstring& id);
	// 「右键菜单」页的「恢复默认」：项序 + 开关 + 这几条菜单项的键位一起回到出厂
	void restoreTrayMenuDefaults();
	bool trayMenuAtDefaults();
	void setAutoStart(bool autoStart);
	bool getAutoStart();
	// 启动时调用：按当前配置同步一次 HKCU Run 项。开启时用当前 exe 路径覆盖，
	// 关闭时删掉值和键（与 TrafficMonitor 的 SyncAutoStart 一致）。
	void syncAutoStart();
	void initShortcutKeys();
	// 录制热键期间挂起/恢复全局热键：挂起后系统热键与低级键盘钩子都不再拦截按键，
	// 否则用户在设置页录入热键时，按键会先被全局热键消费而无法录入。
	void suspendShortcutKeys();
	void resumeShortcutKeys();
	// 全局热键归谁：完整版与 Lite 版可以并存，但热键只归完整版。
	// Lite 版发现完整版在跑时调 setHotkeysYielded(true) 交出热键（注销已注册的），
	// 完整版退出后调 setHotkeysYielded(false) 取回。
	void setHotkeysYielded(bool yielded);
	void purgeRemovedAiSettings();
	// 启动时清理旧程序 ScreenCapture 留下的历史键位（`cap`，现用 id 为 capture）；见 Setting.cpp
	void purgeLegacyShortcutKeys();
	bool getToolFlag(const std::wstring& tool, const std::wstring& key, bool def);
	void setToolFlag(const std::wstring& tool, const std::wstring& key, bool val);
	float getToolNum(const std::wstring& tool, const std::wstring& key, float def);
	void setToolNum(const std::wstring& tool, const std::wstring& key, float val);
	UINT32 getToolUInt(const std::wstring& tool, const std::wstring& key, UINT32 def);
	void setToolUInt(const std::wstring& tool, const std::wstring& key, UINT32 val);
	std::wstring getToolStr(const std::wstring& tool, const std::wstring& key, const std::wstring& def);
	void setToolStr(const std::wstring& tool, const std::wstring& key, const std::wstring& val);
	// 删除 toolPin/<tool>/<key>（键不存在时什么都不做）。用于「恢复默认」时把覆盖项清掉。
	void removeToolKey(const std::wstring& tool, const std::wstring& key);
	int getOcrEngine();
	void setOcrEngine(int v);
	// 离线 OCR 模型 = ocr-models 下的目录名（设置页里扫出来的就是这些目录名）。
	// 旧配置只有档位 int（0=轻量 1=标准）时按此映射成目录名。
	std::wstring getOcrModelDir();
	void setOcrModelDir(const std::wstring& v);
	// OCR 结果版式：0 = 原位覆盖（默认），1 = 文本重排（整段可编辑文本框）
	int getOcrResultMode();
	void setOcrResultMode(int v);

	std::wstring getTranslateProvider();
	void setTranslateProvider(const std::wstring& v);
	std::wstring getTranslateSourceLang();
	void setTranslateSourceLang(const std::wstring& v);
	std::wstring getTranslateTargetLang();
	void setTranslateTargetLang(const std::wstring& v);
	bool getTranslateDictMode();
	void setTranslateDictMode(bool on);
	// 每次打开快捷翻译页都从空白会话开始（默认关）
	bool getQuickTranslateNewSession();
	void setQuickTranslateNewSession(bool on);
	std::wstring getTranslateMsTone();           // 微软翻译语气：standard / informal / formal
	void setTranslateMsTone(const std::wstring& v);
	// 「功能提示」总开关：开则在设置项旁显示说明问号（默认开）
	bool getFeatureTips();
	void setFeatureTips(bool on);
	// 「快捷键提示」：工具栏停悬文字后追加当前键位（"矩形" → "矩形（1）"）。默认关闭
	bool getHotkeyTip();
	void setHotkeyTip(bool on);

	bool getFindWindowElements();
	void setFindWindowElements(bool on);
	// 贴图上滚动中键（滚轮）执行的动作：0 光标中心缩放 / 1 对向边缘缩放 / 2 调整透明度（默认 0）
	int getPinWheelAction();
	void setPinWheelAction(int v);
	// 截图工具栏相对选区的横向对齐：0 左 / 1 右 / 2 居中（默认左）
	int getCaptureToolbarAlign();
	void setCaptureToolbarAlign(int v);
	int getToolbarSize();
	void setToolbarSize(int v);
	// 静默截图：开启时全屏/重复/焦点窗口截图直接完成；关闭后跳过框选、显示工具栏，需手动完成
	bool getSilentCapture();
	void setSilentCapture(bool on);
	// 全屏窗口时关闭全局热键：检测到其他程序全屏（游戏/视频/演示）时暂停热键，退出后恢复
	bool getPauseHotkeysFullscreen();
	void setPauseHotkeysFullscreen(bool on);
	// 由全屏检测驱动：暂时摘掉 / 重新装回全部全局热键（幂等）
	void setHotkeysSuppressed(bool on);
	bool getPinAutoOcr();
	void setPinAutoOcr(bool on);
	bool getPinAutoFit();
	void setPinAutoFit(bool on);
	int getPinDoubleClickAction();
	void setPinDoubleClickAction(int v);
	int getPinOpacity();
	void setPinOpacity(int v);
	int getDemoDefaultTool();
	void setDemoDefaultTool(int v);
	int getDemoEdgeBehavior();
	void setDemoEdgeBehavior(int v);
	// 线迹标注类工具（矩形/箭头/画笔/高亮）的默认宽度，1–10
	int getToolDefaultWidth();
	void setToolDefaultWidth(int v);
	// —— 初始属性（外观 > 工具栏外观）：每个工具的默认形态 / 数值 ——
	// 存在 features.toolDesign/{工具 id}/{键}；没存过就返回默认值，运行时可据此回落
	bool hasToolDesign(const std::wstring& toolId);
	// 是否有任意工具存过初始属性（「恢复默认」的可用态用）
	bool hasAnyToolDesign();
	int getToolDesignInt(const std::wstring& toolId, const std::wstring& key, int def);
	void setToolDesignInt(const std::wstring& toolId, const std::wstring& key, int value);
	// 初始属性里的字符串项（目前用于停悬气泡的子工具清单，逗号分隔）
	std::wstring getToolDesignStr(const std::wstring& toolId, const std::wstring& key, const std::wstring& def);
	void setToolDesignStr(const std::wstring& toolId, const std::wstring& key, const std::wstring& value);
	void clearToolDesign();
	// 记忆工具样式：记住上次选中的标注工具样式（如箭头样式），不记忆滑动条
	bool getRememberToolStyle();
	void setRememberToolStyle(bool on);
	// 记忆工具样式：上次实际用过的「形态 + 粗细」存在 features.toolLast/{工具 id}/{键}
	// 只在 getRememberToolStyle() 开启时读写；没存过返回 def
	int getToolLastInt(const std::wstring& toolId, const std::wstring& key, int def);
	void setToolLastInt(const std::wstring& toolId, const std::wstring& key, int value);
	int getLastArrowHead();
	void setLastArrowHead(int v);
	bool getAutoSaveAfterCapture();
	void setAutoSaveAfterCapture(bool on);
	std::wstring getSaveFormat();
	void setSaveFormat(const std::wstring& v);
	// 截图完成时做什么：0 保存并复制图片 / 1 保存并复制文件 / 2 仅复制图片（不落盘）
	int getCaptureFinishAction();
	void setCaptureFinishAction(int v);
	bool getCancelConfirmDialog();
	void setCancelConfirmDialog(bool on);
	void restoreFunctionScreenshotDefaults();
	// 「功能设置」各子页的「恢复默认」：逐项写回该子页的出厂值（清掉用户覆盖项）
	void restoreFunctionRecordDefaults();     // 视频录制
	void restoreFunctionOutputDefaults();     // 文件输出
	void restoreFunctionOcrDefaults();        // 文本识别
	void restoreFunctionTranslateDefaults(); // 翻译
	void restoreFunctionOtherDefaults();      // 其他功能（原「其他外观」的开关）
	// 与上面 6 个 restore 成对：该子页是否仍是出厂值（供「恢复默认」按钮置灰）
	bool functionScreenshotAtDefaults();
	bool functionRecordAtDefaults();
	bool functionOutputAtDefaults();
	bool functionOcrAtDefaults();
	bool functionTranslateAtDefaults();
	bool functionOtherAtDefaults();

	int getVideoQuality();
	void setVideoQuality(int v);
	int getVideoFps();
	void setVideoFps(int v);
	int getGifQuality();
	void setGifQuality(int v);
	int getGifFps();
	void setGifFps(int v);
	int getGifFormat();
	void setGifFormat(int v);
	int getRecordMic();
	void setRecordMic(int v);
	int getVideoEncoder();
	void setVideoEncoder(int v);
	int getEncodeSpeed();
	void setEncodeSpeed(int v);
	bool getHwAccel();
	void setHwAccel(bool on);
	bool getHideToolbarInRecord();
	void setHideToolbarInRecord(bool on);
	// 「记录操作按键」：录制中把按键/鼠标操作显示在录制画面里。
	// 0 = 视窗左侧，1 = 视窗右侧，2 = 不显示（关闭，默认）
	// 老配置只有布尔 showKeystrokes：开过的按「视窗左侧」迁移，没开过就是关闭
	static constexpr int kKeyOverlayLeft{ 0 };
	static constexpr int kKeyOverlayRight{ 1 };
	static constexpr int kKeyOverlayNone{ 2 };
	int getKeyOverlayPos();
	void setKeyOverlayPos(int pos);
	// 「选框鼠标跟随」（视频录制）：录制中处于穿透态时，光标贴近选区边缘就让选区自动平移跟随
	bool getSelFollowMouse();
	void setSelFollowMouse(bool on);
	// 录屏工具栏「贴边隐藏」：手动拖到屏幕上/下边缘后自动缩进边缘（见 ToolVideo）
	bool getRecordToolbarPeek();
	void setRecordToolbarPeek(bool on);
	// 录屏默认开启鼠标穿透：开录即穿透，录制中可用快捷键/工具条按钮临时开关
	bool getRecordDefaultPierce();
	void setRecordDefaultPierce(bool on);
	// 「鼠标轨迹效果」（视频录制）：0 = 高亮提醒，1 = 线条跟随，2 = 无（默认）。
	// 2 保持为「无」是为了兼容旧配置；旧值 3（局部放大）读取时按「无」处理。
	static constexpr int kMouseTrailHighlight{ 0 };
	static constexpr int kMouseTrailLine{ 1 };
	static constexpr int kMouseTrailNone{ 2 };
	int getMouseTrailEffect();
	void setMouseTrailEffect(int effect);
	// 兼容旧配置：旧版只有布尔 mouseHighlight（开 = 高亮提醒）
	bool getMouseHighlight();
	void setMouseHighlight(bool on);
	// 「鼠标单击效果」（视频录制）：0 = 单色波纹，1 = 光标放大，2 = 无（默认），
	// 3 = 光标波纹组合。2 保持为「无」是为了兼容旧配置；组合效果追加在最后。
	static constexpr int kMouseClickRipple{ 0 };
	static constexpr int kMouseClickZoom{ 1 };
	static constexpr int kMouseClickNone{ 2 };
	static constexpr int kMouseClickCombo{ 3 };
	int getMouseClickEffect();
	void setMouseClickEffect(int effect);
	// 轨迹颜色（0xRRGGBBAA，取色存不透明值）：高亮色块 / 跟随线条 / 单击波纹共用；
	// 绘制时再按各自的固定透明度做半透明效果
	UINT32 getMouseHighlightColor();
	void setMouseHighlightColor(UINT32 rgba);

	// 文件名格式固定使用内置默认模板
	std::wstring getManualSaveFormat();
	std::wstring getAutoSaveFormat();
	std::wstring getVideoRecordFormat();
	// 文件输出目录：空串表示用默认目录（桌面）
	std::wstring getScreenshotDir();
	void setScreenshotDir(const std::wstring& v);
	std::wstring getRecordDir();
	void setRecordDir(const std::wstring& v);

	static std::wstring defaultManualSaveFormat();
	static std::wstring defaultAutoSaveFormat();
	static std::wstring defaultVideoRecordFormat();
	int getAppTheme();
	void setAppTheme(int v);

	// —— 新增 (贴图边框 / 浮动按钮 / 窗口黑名单) ——
	bool getPinBorderDefaultEnabled();
	void setPinBorderDefaultEnabled(bool on);
	int getPinBorderWidth();
	void setPinBorderWidth(int v);
	int getPinBorderRadius();
	void setPinBorderRadius(int v);
	UINT32 getPinBorderColor();
	void setPinBorderColor(UINT32 argb);

	// —— 托盘图标（开关 + 样式 + 自定义路径）——
	bool getTrayEnabled();
	void setTrayEnabled(bool on);
	int getTrayIconStyle();               // 0 跟随系统 / 1 主题 / 2 自定义颜色 / 3 自定义图标
	void setTrayIconStyle(int v);
	UINT32 getTrayCustomColor();          // 「自定义颜色」的字形颜色（RGBA）
	void setTrayCustomColor(UINT32 rgba);
	std::wstring getTrayCustomIconPath();
	void setTrayCustomIconPath(const std::wstring& path);

	bool getFloatingCaptureButtonShow();
	void setFloatingCaptureButtonShow(bool on);
	int getFloatingCaptureButtonClick();   // 0 截图/1 录屏/2 快捷翻译/3 无操作
	void setFloatingCaptureButtonClick(int v);
	int getFloatingLongPressMs();
	void setFloatingLongPressMs(int ms);

	std::vector<std::wstring> getWindowBlacklist();
	void setWindowBlacklist(const std::vector<std::wstring>& titles);
	// 黑名单匹配：窗口标题、进程名（任务管理器里的名字）、程序文件说明任一命中都算。
	// 有些软件标题会随当前对话/模式实时改写，只比标题会漏。
	bool isWindowBlacklisted(const std::wstring& windowTitle,
		const std::wstring& processName = L"", const std::wstring& fileDescription = L"");

	// 插件本地数据：key 用点分路径，如 "rack.items"，值为 JSON 对象；存 config.json。
	std::wstring getLocalDataText(const std::wstring& key);
	bool setLocalDataText(const std::wstring& key, const std::wstring& json);
	std::vector<std::function<void()>> toolbarSizeListeners;
	void addToolbarSizeListener(std::function<void()> listener);
private:
	Setting();
	void normalizeConfigSections();
	// —— 全局热键 ——
	// 全局热键是否已让给同机的完整版（Lite 版专用）：为 true 时不注册任何全局热键
	bool hotkeysYielded_{ false };
	// 注册/重注册全部全局热键：先走系统 RegisterHotKey；注册失败则退到低级键盘钩子兜底
	void applyHotkeys();
	// 执行一条全局热键的动作（id 取自 Setting.cpp 的 kHotkeys）
	void runHotkeyAction(const std::wstring& id);
	// 注册失败时的低频重试（一旦 RegisterHotKey 成功就摘掉钩子），由消息泵上的定时器驱动
	static void CALLBACK onHotkeyRetry(HWND, UINT, UINT_PTR id, DWORD);
	void startHotkeyRetry();
	void stopHotkeyRetry();
	JsonObject featuresObj();
	JsonObject getToolObj(const std::wstring& tool);
	std::filesystem::path initDataPath();
	std::filesystem::path initConfigPath();
	std::filesystem::path initCachePath();
	std::filesystem::path resolveCachePath();
	void save();
	// config.json 的兜底：读失败不覆盖、解析失败留现场并回退到上一份可用配置
	void backupConfigFile();
	bool restoreFromBackup();
	void preserveBrokenConfig();
	// 启动时若发现 OneDrive 等生成的冲突副本（config-电脑名.json / config (1).json）比主配置新，
	// 采用较新版本（原 config.json 已由 backupConfigFile 保存为 .bak），避免设置回退。
	void adoptNewerConflictCopy();
	bool featBool(const wchar_t* key, bool def);
	void setFeatBool(const wchar_t* key, bool v);
	int featInt(const wchar_t* key, int def);
	void setFeatInt(const wchar_t* key, int v);
	std::wstring featStr(const wchar_t* key, const wchar_t* def);
	void setFeatStr(const wchar_t* key, const std::wstring& v);
	JsonArray featArray(const wchar_t* key);
	void setFeatArray(const wchar_t* key, const JsonArray& arr);
private:
	const std::filesystem::path dataPath;
	const std::filesystem::path configPath;
	std::filesystem::path cachePath;
	JsonObject configObj;
	// 上次成功写入的配置文本；内容未变化时跳过写入
	std::wstring lastSavedConfig;
	// 写入失败仅提示一次，避免重复弹窗
	bool configSaveWarned{ false };
	// config.json 这一次没读出来：本次会话只读不写，别拿默认值把用户的文件覆盖掉
	bool configReadOnly{ false };
	UINT_PTR hotkeyRetryTimer{ 0 };
	int hotkeyRetryTick{ 0 };
	// 全屏窗口检测把全局热键临时摘掉了（退出全屏后自动恢复）
	bool hotkeysSuppressed_{ false };
};
