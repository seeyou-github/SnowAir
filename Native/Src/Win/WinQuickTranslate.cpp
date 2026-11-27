#include "pch.h"
#include <dwmapi.h>
#include <include/D2D.h>
#include "../App.h"
#include "../Lang.h"
#include "../Setting.h"
#include "WinQuickTranslate.h"
#include "WinSettingQuickTranslate.h"
#include "SettingTheme.h"
#include "SettingWidgets.h"

std::unique_ptr<WinQuickTranslate> winQuickTranslate;

namespace {
	unsigned long long gQuickUid = 0;   // 每建一个快捷翻译窗 +1
}

WinQuickTranslate::WinQuickTranslate() : Ling::WinBase()
{
	uid = ++gQuickUid;
	// 主题必须先套用：子页构建时就把语义色读进控件了
	SettingTheme::apply(Setting::get()->getAppTheme());
	onDestroy.add([uid = uid]() {
		Ling::App::get()->dq.TryEnqueue([uid]() {
			if (!winQuickTranslate || winQuickTranslate->uid != uid) return;
			winQuickTranslate.reset();
			// 与设置页同一套收工处理：没有别的窗口了就把空闲图形设备放掉
			auto* app = Ling::App::get();
			if (app && app->windows.empty()) {
				Ling::D2D::dispose();
				SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
			}
			});
		});
	setTitle(Lang::get(L"tray.quickTranslate"));
	// 与设置页里「快捷翻译」那一页的可视面积相当（少了左侧导航栏，整体更窄）
	setSize(840, 660);
	setCenter();
	// 原生标题栏（可拖动 + 最小/最大化/关闭）
	createNativeWindow(NULL, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN);
}

WinQuickTranslate::~WinQuickTranslate()
{
	// 「?」气泡的宿主是设置窗口，这里只清掉子页内可能残留的弹层（子页析构自己会收）
}

void WinQuickTranslate::onCreated()
{
	enableShadow();

	// 深色主题：连系统标题栏一起切成深色（Win10 1809+）
	if (SettingTheme::isDark() && hwnd) {
		BOOL dark = TRUE;
		DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
	}

	body->setBg(SettingTheme::canvas);
	body->setFlexDirection(Ling::FlexDirection::Row);

	// 页面本体：与设置页 case 1 完全一致的挂法（铺满、无额外内边距）
	page = new WinSettingQuickTranslate(this);
	SettingUi::adoptChild(body.get(), page);
	page->setWidthPercent(100.f);
	page->setFlexGrow(1.f);
	page->setFlexShrink(1.f);
	YGNodeStyleSetMinHeight(page->node, 0.f);
	YGNodeStyleSetMinWidth(page->node, 0.f);
	page->setFlexDirection(Ling::FlexDirection::Column);
	page->setPadding(0.f, 0.f, 0.f, 0.f);

	layout();

	// ★ 首帧底色垫层（同设置页）：窗口显示前先把 GDI 表面刷成画布色，
	//   合成内容上屏前的那几帧就不会露出系统白底。
	SettingUi::primeWindowBackground(hwnd, SettingTheme::canvas);
}

// 打开时切前台并获得焦点（与设置页同一套兜底）
void WinQuickTranslate::bringToForeground()
{
	if (!hwnd) return;
	ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
	SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
	if (!SetForegroundWindow(hwnd)) {
		SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		SetForegroundWindow(hwnd);
	}
	SetFocus(hwnd);
}

void WinQuickTranslate::init()
{
	if (!winQuickTranslate || !winQuickTranslate->hwnd || !IsWindow(winQuickTranslate->hwnd)) {
		winQuickTranslate.reset(new WinQuickTranslate());
	}
	winQuickTranslate->bringToForeground();
}

void WinQuickTranslate::toggle()
{
	// 已打开且在前台 → 关闭；否则打开 / 还原
	if (winQuickTranslate && winQuickTranslate->hwnd && IsWindow(winQuickTranslate->hwnd)
		&& IsWindowVisible(winQuickTranslate->hwnd)
		&& !IsIconic(winQuickTranslate->hwnd)
		&& GetForegroundWindow() == winQuickTranslate->hwnd) {
		winQuickTranslate->close();
		return;
	}
	init();
}

void WinQuickTranslate::dispose()
{
	// 真正 DestroyWindow 再释放：退出时消息循环已结束，只 reset() 会留下空白窗口
	if (winQuickTranslate && winQuickTranslate->hwnd) winQuickTranslate->close();
	winQuickTranslate.reset();
}
