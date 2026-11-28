#include "pch.h"
#include <algorithm>
#include <cctype>
#include "../CommandKeys.h"
#include "../Lang.h"
#include "../Setting.h"
#include "WinSetting.h"
#include "WinSettingTrayMenu.h"
#include "SettingTheme.h"
#include "SettingWidgets.h"
#include "../Tool/IconCodes.h"

namespace {
	// —— 列宽（表头与数据行共用，保证两行之间列对得上）——
	constexpr float kColShortcut{ 170.f };   // 快捷键框
	constexpr float kColSwitch{ 76.f };      // 开关（胶囊 44 居中偏左）
	constexpr float kColDrag{ 64.f };        // 拖动排序
	constexpr float kRowH{ 48.f };           // 数据行高
	constexpr float kBoxH{ 30.f };           // 快捷键框高
	constexpr float kSpinnerPad{ 16.f };

	// 不配修饰键、单独按下也能当快捷键的那一类（与「全局热键」页同口径）
	bool canBeUsedAlone(const std::wstring& key)
	{
		if (key.size() >= 2 && key[0] == L'F') {
			auto num = key.substr(1);
			auto isDigit = [](wchar_t c) { return c >= L'0' && c <= L'9'; };
			if (std::all_of(num.begin(), num.end(), isDigit)) return true;
		}
		return key == L"PrintScreen" || key == L"ScrollLock" || key == L"Pause";
	}

	bool isValidShortcutKeys(const std::vector<std::wstring>& keys)
	{
		bool hasModifier{ false };
		std::wstring normalKey;
		for (const auto& key : keys) {
			if (KeyCapture::isModifierKey(key)) { hasModifier = true; continue; }
			if (!normalKey.empty()) return false;
			normalKey = key;
		}
		if (normalKey.empty()) return false;
		if (!hasModifier && !canBeUsedAlone(normalKey)) return false;
		std::wstring lowerKey = normalKey;
		std::transform(lowerKey.begin(), lowerKey.end(), lowerKey.begin(), ::towlower);
		return Ling::Util::strToKey(lowerKey) != 0;
	}

	// 系统保留键（注册不会成功）/ 通用编辑键（会抢占其他程序）—— 与「全局热键」页同口径
	enum class Reserved { None, System, Common };
	Reserved reservedKindOf(const std::wstring& shortcut)
	{
		static const std::vector<std::wstring> system{
			L"Ctrl+Alt+Delete", L"Ctrl+Shift+Esc", L"Ctrl+Esc",
			L"Alt+Tab", L"Alt+Esc", L"Alt+F4",
			L"Win+L", L"Win+D", L"Win+E", L"Win+R", L"Win+Tab",
		};
		static const std::vector<std::wstring> common{
			L"Ctrl+C", L"Ctrl+V", L"Ctrl+X", L"Ctrl+Z", L"Ctrl+Y", L"Ctrl+A", L"Ctrl+S",
		};
		const std::wstring chord = CommandKeys::canonicalChord(shortcut);
		auto listed = [&chord](const std::vector<std::wstring>& table) {
			for (const auto& s : table) if (_wcsicmp(s.c_str(), chord.c_str()) == 0) return true;
			return false;
		};
		if (listed(system)) return Reserved::System;
		if (listed(common)) return Reserved::Common;
		return Reserved::None;
	}

	// 菜单项 → 托盘菜单 / 本页共用的语言键
	const wchar_t* labelKeyOf(const std::wstring& id)
	{
		if (id == L"capture") return L"tray.capture";
		if (id == L"ocr") return L"tray.ocr";
		if (id == L"ocr_translate") return L"tray.translate";
		if (id == L"quick_translate") return L"tray.quickTranslate";
		if (id == L"record") return L"tray.record";
		if (id == L"fullscreen_canvas") return L"tray.demoCanvas";
		if (id == L"settings") return L"tray.setting";
		return L"tray.exit";
	}

	// 快捷键框配色：常态 = 白底 + 描边；录制中 = 绿色底 + 白字
	constexpr uint32_t kCaptureBg{ 0x0FDC78FF };
	constexpr uint32_t kCaptureFg{ 0xFFFFFFFF };

	void styleShortcutBox(Ling::Button* btn, bool capturing)
	{
		if (capturing) {
			btn->setBg(kCaptureBg);
			btn->setHoverBg(kCaptureBg);   // 悬停取同值 → 无停悬效果
			btn->setBorder(0.f, 0);
			btn->setColor(kCaptureFg);
			btn->setHoverColor(kCaptureFg);
		}
		else {
			btn->setBg(SettingTheme::inputBg);
			btn->setHoverBg(SettingTheme::accent);
			btn->setBorder(1.f, SettingTheme::input);
			btn->setColor(SettingTheme::textPrimary);
			btn->setHoverColor(SettingTheme::textPrimary);
		}
	}

	void applyShortcutLabel(Ling::Button* btn, const std::wstring& id)
	{
		auto key = Setting::get()->getShortcutKey(id);
		btn->setText(key.empty() ? Lang::get(L"shortcut.setToBind") : key);
		btn->setColor(key.empty() ? SettingTheme::textMuted : SettingTheme::textPrimary);
	}

}

WinSettingTrayMenu::WinSettingTrayMenu(Ling::WinBase* parent) : Ling::Node(parent)
{
	setWidthPercent(100.f);
	setFlexDirection(Ling::FlexDirection::Column);

	Ling::Node* titleRow = nullptr;
	SettingUi::sectionHeader(this, Lang::get(L"setting.trayMenuTitle"),
		Lang::get(L"setting.trayMenuDesc"), Lang::get(L"setting.trayMenuTip"), &titleRow);

	// 标题行右端：恢复默认（项序 + 开关 + 这几条菜单项的键位）
	if (titleRow) {
		auto* spacer = titleRow->makeChild<Ling::Node>();
		spacer->setFlexGrow(1.f);
		spacer->setFlexShrink(1.f);
		auto* restoreBtn = SettingUi::btnOutline(titleRow, Lang::get(L"setting.restoreDefault"), [this]() {
			this->capture_.end();                    // 正在录制键位时先收尾
			Setting::get()->restoreTrayMenuDefaults();
			this->order = Setting::get()->getTrayMenuOrder();
			this->scheduleRebuild();
			});
		restoreBtn->setMarginLeft(SettingTheme::sp3);
	}

	buildHeader(this);

	listHost = makeChild<Ling::Node>();
	listHost->setWidthPercent(100.f);
	listHost->setFlexDirection(Ling::FlexDirection::Column);

	order = Setting::get()->getTrayMenuOrder();
	rebuildList();
	SettingUi::trimTrailingGap(this);

	// —— 快捷键录制：与「全局热键」页共用 KeyCapture，只负责校验与落值 ——
	capture_.init(win, kSpinnerTimerId,
		[this](const std::wstring& id) { this->refreshShortcutLabel(id); },
		[this](const std::wstring& id, const std::vector<std::wstring>& keys) { this->commitChord(id, keys); },
		[this](const std::wstring& id) { Setting::get()->setShortcutKey(id, {}); },
		[](const std::vector<std::wstring>& keys) { return isValidShortcutKeys(keys); });

	auto weakThis = getWeakThis();
	downTok = win->onMouseDown.add([this, weakThis](POINT pos, bool isRight) {
		if (!weakThis.lock()) return;
		capture_.handleMouseDown(pos);
		if (!isRight) onDown(pos);
		});
	moveTok = win->onMouseMove.add([this, weakThis](POINT pos) {
		if (!weakThis.lock()) return;
		onMove(pos);
		});
	upTok = win->onMouseUp.add([this, weakThis](POINT pos, bool isRight) {
		if (!weakThis.lock()) return;
		onUp(pos, isRight);
		});
	keyDownTok = win->onKeyDown.add([this, weakThis](UINT key) {
		if (!weakThis.lock()) return;
		capture_.handleKey(key);
		});
	keyUpTok = win->onKeyUp.add([this, weakThis](UINT) {
		if (!weakThis.lock()) return;
		capture_.handleKeyUp();
		});
	timerTok = win->onTimer.add([this, weakThis](UINT id) {
		if (!weakThis.lock()) return;
		capture_.handleTimer(id);
		});
	blurTok = win->onBlur.add([this, weakThis]() {
		if (!weakThis.lock()) return;
		capture_.handleBlur();
		// 拖到窗口外松手时收不到 mouseUp：窗口失焦就当作这次拖动结束
		if (dragging) {
			dragging = false;
			dragId.clear();
			Setting::get()->setTrayMenuOrder(order);
			scheduleRebuild();
		}
		});
}

WinSettingTrayMenu::~WinSettingTrayMenu()
{
	capture_.shutdown();
	win->onMouseDown.remove(downTok);
	win->onMouseMove.remove(moveTok);
	win->onMouseUp.remove(upTok);
	win->onKeyDown.remove(keyDownTok);
	win->onKeyUp.remove(keyUpTok);
	win->onTimer.remove(timerTok);
	win->onBlur.remove(blurTok);
}

// 表头：名称 / 快捷键 / 开关 / 拖动排序（列宽与数据行一致）
void WinSettingTrayMenu::buildHeader(Ling::Node* parent)
{
	auto* head = parent->makeChild<Ling::Node>();
	head->setWidthPercent(100.f);
	head->setHeight(30.f);
	head->setMarginBottom(SettingTheme::sp2);
	head->setPadding(SettingTheme::sp4, 0, SettingTheme::sp4, 0);
	head->setFlexDirection(Ling::FlexDirection::Row);
	head->setAlignItems(Ling::Align::Center);

	auto makeHead = [head](const wchar_t* text, float w, float padLeft) {
		auto* lab = head->makeChild<Ling::Label>();
		lab->setText(Lang::get(text));
		lab->setFontSize(SettingTheme::fontSm);
		lab->setColor(SettingTheme::textTertiary);
		lab->setFlexShrink(0.f);
		if (w > 0.f) lab->setWidth(w);
		lab->setPadding(padLeft, 0, 0, 0);
		return lab;
		};

	auto* nameHead = makeHead(L"setting.trayColName", 0.f, 0.f);
	nameHead->setFlexGrow(1.f);
	nameHead->setFlexShrink(1.f);
	makeHead(L"setting.trayColShortcut", kColShortcut, 0.f);
	makeHead(L"setting.trayColSwitch", kColSwitch, kSpinnerPad);
	makeHead(L"setting.trayColSort", kColDrag, 0.f);
}

void WinSettingTrayMenu::rebuildList()
{
	if (!listHost) return;
	// 正在录制的框马上要被销毁：先断开录制（撤钩子、恢复全局热键），不去碰旧控件
	if (capture_.active()) capture_.shutdown();

	rows.clear();
	listHost->removeAllChildren();
	for (const auto& id : order) buildRow(listHost, id);
	win->layout();
	win->refresh();
}

void WinSettingTrayMenu::scheduleRebuild()
{
	if (rebuildQueued) return;
	rebuildQueued = true;
	auto* app = Ling::App::get();
	if (!app) { rebuildQueued = false; rebuildList(); return; }
	auto weakThis = getWeakThis();
	app->dq.TryEnqueue([this, weakThis]() {
		if (!weakThis.lock()) return;
		this->rebuildQueued = false;
		this->rebuildList();
		});
}

void WinSettingTrayMenu::buildRow(Ling::Node* list, const std::wstring& id)
{
	auto* s = Setting::get();
	const bool fixed = Setting::trayMenuItemFixed(id);
	const bool on = s->getTrayMenuItemEnabled(id);

	auto* row = list->makeChild<Ling::Node>();
	row->setWidthPercent(100.f);
	row->setHeight(kRowH);
	row->setMarginBottom(SettingTheme::sp2);
	row->setBg(SettingTheme::cardBg);
	row->setBorder(1.f, SettingTheme::border);
	row->setBorderRadius(SettingTheme::radiusSm);
	row->setFlexDirection(Ling::FlexDirection::Row);
	row->setAlignItems(Ling::Align::Center);
	row->setPadding(SettingTheme::sp4, 0, SettingTheme::sp4, 0);
	// 正在被拖动的那一行：换个底色 + 主色描边，提示"它正跟着鼠标走"
	if (dragging && id == dragId) {
		row->setBg(SettingTheme::accent);
		row->setBorder(1.f, SettingTheme::ring);
	}

	// ① 名称
	auto* name = row->makeChild<Ling::Label>();
	name->setText(Lang::get(labelKeyOf(id)));
	name->setFontSize(SettingTheme::fontBase);
	name->setColor(on ? SettingTheme::textPrimary : SettingTheme::textMuted);
	name->setFlexGrow(1.f);
	name->setFlexShrink(1.f);

	// ② 快捷键（固定项：不显示；关闭的项：只读展示，点不了）
	Ling::Button* shortcutBtn = nullptr;
	{
		auto* box = row->makeChild<Ling::Node>();
		box->setWidth(kColShortcut);
		box->setFlexShrink(0.f);
		box->setFlexDirection(Ling::FlexDirection::Row);
		box->setAlignItems(Ling::Align::Center);

		if (!fixed) {
			const std::wstring chord = s->getShortcutKey(id);
			if (!on) {
				SettingUi::staticBox(box, chord.empty() ? Lang::get(L"setting.trayNoShortcut") : chord,
					kColShortcut, kBoxH);
			}
			else {
				auto* btn = box->makeChild<Ling::Button>();
				btn->setId(id);
				btn->setWidth(kColShortcut);
				btn->setHeight(kBoxH);
				btn->setBorderRadius(SettingTheme::radiusCtl);
				styleShortcutBox(btn, false);
				applyShortcutLabel(btn, id);
				btn->onClick.add([this](Ling::Button* b) { capture_.begin(b, b->id, styleShortcutBox); });
				shortcutBtn = btn;
			}
		}
	}

	// ③ 开关（固定项：不显示）
	{
		auto* box = row->makeChild<Ling::Node>();
		box->setWidth(kColSwitch);
		box->setFlexShrink(0.f);
		box->setFlexDirection(Ling::FlexDirection::Row);
		box->setAlignItems(Ling::Align::Center);
		box->setPadding(kSpinnerPad, 0, 0, 0);

		if (!fixed) {
			auto* sw = box->makeChild<Ling::Button>();
			SettingUi::styleToggle(sw, on);
			// on 是 const 局部量，MSVC 会把按值捕获的成员也视为 const（C2166），
			// 所以不改捕获值，直接按「取反」的结果刷新。
			sw->onClick.add([this, id, on](Ling::Button* b) {
				SettingUi::styleToggle(b, !on);
				Setting::get()->setTrayMenuItemEnabled(id, !on);
				// 该行的快捷键框（只读/可录）与文字色随开关变化 —— 交给下一轮重建
				this->scheduleRebuild();
				});
		}
	}

	// ④ 拖动排序（固定项：不显示手柄，不参与拖动）
	{
		auto* box = row->makeChild<Ling::Node>();
		box->setWidth(kColDrag);
		box->setHeight(kRowH);
		box->setFlexShrink(0.f);
		box->setFlexDirection(Ling::FlexDirection::Row);
		box->setAlignItems(Ling::Align::Center);
		box->setJustifyContent(Ling::Justify::Center);

		if (!fixed) {
			auto* handle = box->makeChild<Ling::Label>();
			handle->setText(Icon::DragHandle);
			handle->setFontFamily(Icon::Family);
			handle->setFontSize(18.f);
			handle->setColor(SettingTheme::textTertiary);
		}

		rows.push_back({ row, fixed ? nullptr : box, shortcutBtn, id, !fixed });
	}
}

int WinSettingTrayMenu::indexOf(const std::wstring& id) const
{
	for (int i = 0; i < (int)order.size(); ++i) {
		if (order[i] == id) return i;
	}
	return -1;
}

// 光标落在第几行（-1 = 不在列表内）。
// 行高固定，所以直接按"相对列表顶部的可视偏移 / 行距"算；列表在设置页的滚动容器里，
// Node::y 是未滚动的客户区坐标，要先把祖先滚动偏移加回来（同 ToolbarAppearance::tbToContent）。
int WinSettingTrayMenu::indexAt(POINT pos) const
{
	if (!listHost || rows.empty()) return -1;
	const float d = (win && win->dpi > 0.f) ? win->dpi : 1.f;
	float sy = 0.f;
	for (auto* p = listHost->parent; p; p = p->parent) {
		if (auto* sb = dynamic_cast<Ling::ScrollerBox*>(p)) sy += sb->getScrollY();
	}
	const float pitch = (kRowH + SettingTheme::sp2) * d;
	const float y = (float)pos.y - listHost->y + sy;
	if (y < 0.f) return -1;
	const int idx = (int)(y / pitch);
	if (idx < 0 || idx >= (int)rows.size()) return -1;
	return idx;
}

// 把 from 处的项挪到 to（下标按"移除前"的顺序给）
void WinSettingTrayMenu::moveItem(int from, int to)
{
	if (from < 0 || to < 0 || from >= (int)order.size() || to >= (int)order.size()) return;
	if (from == to) return;
	const std::wstring moved = order[from];
	order.erase(order.begin() + from);
	order.insert(order.begin() + to, moved);
}

void WinSettingTrayMenu::onDown(POINT pos)
{
	const int idx = indexAt(pos);
	if (idx < 0) return;
	auto& r = rows[idx];
	if (!r.draggable || !r.handle) return;   // 固定项（设置 / 退出）拖不动
	// 只有落在「拖动排序」这一列才起拖，避免在行上随便一按就把顺序拨乱
	if (pos.x < r.handle->x || pos.x > r.handle->x + r.handle->w) return;
	dragging = true;
	dragId = r.id;
}

void WinSettingTrayMenu::onMove(POINT pos)
{
	if (!dragging) return;
	if (rebuildQueued) return;   // 等这一轮重建落地再算下一格，避免用旧行坐标反复互换
	const int from = indexOf(dragId);
	if (from < 0) return;
	const int to = indexAt(pos);
	if (to < 0 || to == from) return;
	if (!rows[to].draggable) return;   // 落到固定项上：不动（设置 / 退出永远在最后）
	moveItem(from, to);
	scheduleRebuild();
}

void WinSettingTrayMenu::onUp(POINT, bool)
{
	if (!dragging) return;
	dragging = false;
	dragId.clear();
	// 顺序落盘（拖动过程中只改内存，抬手才写配置）；再重建一次把拖动高亮清掉
	Setting::get()->setTrayMenuOrder(order);
	scheduleRebuild();
}

// —— 快捷键 ——

void WinSettingTrayMenu::refreshShortcutLabel(const std::wstring& id)
{
	for (auto& r : rows) {
		if (r.id != id || !r.shortcutBtn) continue;
		applyShortcutLabel(r.shortcutBtn, id);
		return;
	}
}

// 录制提交：先处理 Windows 保留键 / 通用编辑键，再进入撞键判定（跨页）
void WinSettingTrayMenu::commitChord(const std::wstring& id, const std::vector<std::wstring>& keys)
{
	const std::wstring chord = KeyCapture::joinKeys(keys);
	if (reservedKindOf(chord) == Reserved::System) {
		SettingUi::ConfirmDialog::ask(win,
			KeyCapture::fillMessage(L"shortcut.reservedSystem", chord, L""),
			[](bool) {}, Lang::get(L"setting.confirm"), L"", true);
		return;
	}
	if (reservedKindOf(chord) == Reserved::Common) {
		auto weakThis = getWeakThis();
		SettingUi::ConfirmDialog::ask(win,
			KeyCapture::fillMessage(L"shortcut.reservedCommon", chord, L""),
			[this, weakThis, id, keys](bool yes) {
				if (!weakThis.lock() || !yes) return;
				this->applyShortcut(id, keys);
			},
			Lang::get(L"setting.confirm"), Lang::get(L"setting.cancel"));
		return;
	}
	applyShortcut(id, keys);
}

void WinSettingTrayMenu::applyShortcut(const std::wstring& id, const std::vector<std::wstring>& keys)
{
	const std::wstring chord = KeyCapture::joinKeys(keys);
	const std::wstring otherId = Setting::get()->findShortcutOwner(chord, id);
	if (otherId.empty()) {
		Setting::get()->setShortcutKey(id, keys);
		refreshShortcutLabel(id);
		return;
	}
	auto weakThis = getWeakThis();
	const std::wstring otherLabel = labelOf(otherId);
	SettingUi::ConfirmDialog::ask(win,
		KeyCapture::fillMessage(L"cmdkey.conflict", chord, otherLabel),
		[this, weakThis, id, otherId, keys](bool yes) {
			if (!weakThis.lock() || !yes) return;
			Setting::get()->setShortcutKey(otherId, {});   // 先解绑旧主
			Setting::get()->setShortcutKey(id, keys);
			this->refreshShortcutLabel(id);
			this->scheduleRebuild();                       // 旧主所在行也要跟着变
		},
		Lang::get(L"setting.confirm"), Lang::get(L"setting.cancel"));
}

// id → 显示名（冲突提示用；跨页的 id 也要认）
std::wstring WinSettingTrayMenu::labelOf(const std::wstring& id) const
{
	for (const auto& r : rows) {
		if (r.id == id) return Lang::get(labelKeyOf(id));
	}
	// 不在本页的全局热键：用「全局热键」页的语言键
	if (id == L"capture_fullscreen") return Lang::get(L"shortcut.capture_fullscreen");
	if (id == L"capture_focused_window") return Lang::get(L"shortcut.capture_focused_window");
	if (id == L"open_clipboard") return Lang::get(L"shortcut.open_clipboard");
	if (id == L"show_main") return Lang::get(L"shortcut.show_main");
	if (id == L"open_screenshot_dir") return Lang::get(L"shortcut.open_screenshot_dir");
	if (id == L"toggle_pins") return Lang::get(L"shortcut.toggle_pins");
	return id;
}
