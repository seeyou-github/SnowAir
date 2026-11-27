#pragma once
#include <include/Ling.h>
#include <string>
#include <vector>
#include "KeyCapture.h"

/**
 * 设置 · 右键菜单页（托盘右键菜单管理）。
 *
 * 一张表：名称 / 快捷键 / 开关 / 拖动排序。
 *   · 顺序、显隐、快捷键都存在 Setting 里（features.trayMenuOrder / trayMenuOn / trayMenuOff + shortcutKey）；
 *   · 快捷键与「全局热键」页共用同一套键位（同一批 id），跨页冲突由 Setting::findShortcutOwner 判定；
 *   · 「设置」「退出」是固定项：不能关闭、不能拖、不能设快捷键，恒在最下方（设置上方由托盘菜单画分割线）。
 */
class WinSettingTrayMenu : public Ling::Node
{
public:
	WinSettingTrayMenu(Ling::WinBase* parent);
	~WinSettingTrayMenu();
private:
	struct Row {
		Ling::Node* node{ nullptr };         // 整行卡片
		Ling::Node* handle{ nullptr };       // 拖动排序列（命中区；固定项为 nullptr）
		Ling::Button* shortcutBtn{ nullptr };// 快捷键框（固定项 / 关闭的项为只读框，这里是 nullptr）
		std::wstring id;                     // 菜单项 id = 全局热键 id
		bool draggable{ false };
	};
	void buildHeader(Ling::Node* parent);
	void rebuildList();
	// 行内容变了（开关 / 恢复默认）：投递到调度队列再重建，
	// 避免在按钮自己的 onClick 派发途中销毁这个按钮
	void scheduleRebuild();
	void buildRow(Ling::Node* list, const std::wstring& id);
	int indexOf(const std::wstring& id) const;
	// 光标落在第几行（-1 = 不在列表内）
	int indexAt(POINT pos) const;
	void moveItem(int from, int to);
	// —— 拖动排序 ——
	void onDown(POINT pos);
	void onMove(POINT pos);
	void onUp(POINT pos, bool isRight);
	// —— 快捷键录制 ——
	void commitChord(const std::wstring& id, const std::vector<std::wstring>& keys);
	void applyShortcut(const std::wstring& id, const std::vector<std::wstring>& keys);
	void refreshShortcutLabel(const std::wstring& id);
	std::wstring labelOf(const std::wstring& id) const;
private:
	Ling::Node* listHost{ nullptr };
	std::vector<Row> rows;
	std::vector<std::wstring> order;          // 当前工作顺序（拖动中先改它，抬手才落盘）
	KeyCapture capture_;
	bool dragging{ false };
	std::wstring dragId;
	bool rebuildQueued{ false };
	winrt::event_token downTok{}, moveTok{}, upTok{};
	winrt::event_token keyDownTok{}, keyUpTok{}, timerTok{}, blurTok{};
	// 与全局热键页(0x5000) / TextBox(0x4200+) 错开
	static constexpr UINT kSpinnerTimerId{ 0x5100 };
};
