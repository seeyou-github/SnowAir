#pragma once
#include <include/Ling.h>
#include <memory>

class WinSettingQuickTranslate;

/**
 * 托盘「快捷翻译」弹出的独立窗口。
 *
 * 页面本体直接复用设置中心里的「快捷翻译」页（WinSettingQuickTranslate），
 * 版式、交互、历史列表完全一致，只是没有左侧分类栏 —— 整窗就是那一个页面。
 *
 * 生命周期与设置窗口一致：用完即关，关闭后放掉空闲的图形设备。
 */
class WinQuickTranslate : public Ling::WinBase
{
public:
	~WinQuickTranslate();
	// 托盘菜单 / 全局热键入口：已在前台就关掉，否则打开（或从最小化还原）并抢前台
	static void toggle();
	static void init();
	static void dispose();
private:
	WinQuickTranslate();
	void onCreated() override;
	void bringToForeground();
private:
	unsigned long long uid{ 0 };   // 实例代号：延迟析构回调据此判断是否已被新实例取代
	WinSettingQuickTranslate* page{ nullptr };
};
