#include "pch.h"
#include "../App.h"
#include "../Lang.h"
#include "../Setting.h"
#include "../Tool/IconCodes.h"
#include "../Util.h"
#include "WinSetting.h"
#include "WinSettingAbout.h"
#include "SettingTheme.h"
#include "SettingWidgets.h"

namespace {

// 选一个配置文件（导入用）。取消返回空串。
std::wstring pickConfigFile(HWND owner)
{
	std::wstring out;
	Microsoft::WRL::ComPtr<IFileOpenDialog> dlg;
	if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(dlg.GetAddressOf()))) || !dlg)
		return out;
	DWORD flags{ 0 };
	if (SUCCEEDED(dlg->GetOptions(&flags)))
		dlg->SetOptions(flags | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);
	COMDLG_FILTERSPEC spec[]{ { L"JSON (*.json)", L"*.json" } };
	dlg->SetFileTypes(_countof(spec), spec);
	dlg->SetFileTypeIndex(1);
	if (FAILED(dlg->Show(owner))) return out;   // 取消
	Microsoft::WRL::ComPtr<IShellItem> item;
	if (FAILED(dlg->GetResult(item.GetAddressOf())) || !item) return out;
	PWSTR p{ nullptr };
	if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p) {
		out = p;
		CoTaskMemFree(p);
	}
	return out;
}

// 整行居中的容器（水平居中 + 交叉轴居中）
Ling::Node* centerRow(Ling::Node* parent)
{
	auto* row = parent->makeChild<Ling::Node>();
	row->setWidthPercent(100.f);
	row->setFlexDirection(Ling::FlexDirection::Row);
	row->setJustifyContent(Ling::Justify::Center);
	row->setAlignItems(Ling::Align::Center);
	return row;
}

// 只读胶囊（版本号 / Lite beta）。故意不用 Button：不能点，就不该出现手型光标。
Ling::Node* badge(Ling::Node* parent, const std::wstring& text, bool accent)
{
	auto* box = parent->makeChild<Ling::Node>();
	box->setHeight(22.f);
	box->setBorderRadius(SettingTheme::radiusFull);
	box->setPadding(10.f, 0.f, 10.f, 0.f);
	box->setBorder(1.f, accent ? SettingTheme::successBorder : SettingTheme::border);
	box->setBg(accent ? SettingTheme::successBg : SettingTheme::card);
	box->setAlignItems(Ling::Align::Center);
	box->setJustifyContent(Ling::Justify::Center);

	auto* label = box->makeChild<Ling::Label>();
	label->setText(text);
	label->setFontSize(11.5f);
	label->setColor(accent ? SettingTheme::accentGreen : SettingTheme::textSecondary);
	return box;
}

} // namespace

WinSettingAbout::WinSettingAbout(Ling::WinBase* parent) : Ling::Node(parent)
{
	setFlexDirection(Ling::FlexDirection::Column);
	setWidthPercent(100.f);
	YGNodeStyleSetMinWidth(node, 0.f);

	// ① ScreenShut 大字
	auto* titleRow = centerRow(this);
	titleRow->setMarginTop(SettingTheme::contentPadY);
	auto* title = titleRow->makeChild<Ling::Label>();
	title->setText(L"ScreenShut");
	title->setFontFamily(L"Segoe UI");
	title->setFontSize(56.f);
	title->setFontWeight(600);
	title->setColor(SettingTheme::textPrimary);

	// ② 版本号 + 版本标记
	auto* metaRow = centerRow(this);
	metaRow->setMarginTop(18.f);
	const auto ver = Util::getAppVersion();
	badge(metaRow, wfmt(L"v%d.%d.%d", ver[0], ver[1], ver[2]), false);
	auto* edition = badge(metaRow, L"Lite beta", true);
	edition->setMarginLeft(9.f);

	// ③ 口号
	auto* sloganRow = centerRow(this);
	sloganRow->setMarginTop(16.f);
	auto* slogan = sloganRow->makeChild<Ling::Label>();
	slogan->setText(Lang::get(L"about.slogan"));
	slogan->setFontSize(13.5f);
	slogan->setColor(SettingTheme::textPrimary);

	// ④ 分隔线 + 配置导入 / 导出 / 打开配置目录
	SettingUi::separator(this, 18.f, 16.f);

	auto* dirRow = makeChild<Ling::Node>();
	dirRow->setWidthPercent(100.f);
	dirRow->setFlexDirection(Ling::FlexDirection::Row);
	dirRow->setAlignItems(Ling::Align::Center);

	// 无边框的轻按钮（文字，可选右侧图标）
	auto lightLink = [dirRow](const std::wstring& text, const wchar_t* icon, bool first,
		std::function<void()> onClick) {
		auto* btn = dirRow->makeChild<Ling::Button>();
		btn->setText(L"");
		btn->setHeight(22.f);
		btn->setBorderRadius(SettingTheme::radiusInner);
		btn->setPadding(6.f, 0.f, 6.f, 0.f);
		btn->setMarginLeft(first ? -6.f : 2.f);
		btn->setFlexDirection(Ling::FlexDirection::Row);
		btn->setJustifyContent(Ling::Justify::Center);
		btn->setAlignItems(Ling::Align::Center);
		btn->setBorder(0.f, 0);
		btn->setBg(0);
		btn->setHoverBg(SettingTheme::accent);

		auto* label = btn->makeChild<Ling::Label>();
		label->setText(text);
		label->setFontSize(11.5f);
		label->setColor(SettingTheme::textTertiary);

		Ling::Label* iconLabel = nullptr;
		if (icon) {
			iconLabel = btn->makeChild<Ling::Label>();
			iconLabel->setText(icon);
			iconLabel->setFontFamily(Icon::Family);
			iconLabel->setFontSize(14.f);
			iconLabel->setColor(SettingTheme::textTertiary);
			iconLabel->setMarginLeft(5.f);
		}

		btn->onEnter.add([label, iconLabel](Ling::Button*) {
			label->setColor(SettingTheme::textPrimary);
			if (iconLabel) iconLabel->setColor(SettingTheme::textPrimary);
		});
		btn->onLeave.add([label, iconLabel](Ling::Button*) {
			label->setColor(SettingTheme::textTertiary);
			if (iconLabel) iconLabel->setColor(SettingTheme::textTertiary);
		});
		btn->onClick.add([onClick](Ling::Button*) { onClick(); });
	};

	const HWND owner = win ? win->hwnd : nullptr;

	lightLink(Lang::get(L"about.importConfig"), nullptr, true, [owner]() {
		const auto path = pickConfigFile(owner);
		if (path.empty()) return;
		const auto tip = Lang::get(L"about.sysTip");
		if (!Setting::get()->importConfigFrom(path)) {
			MessageBoxW(owner, Lang::get(L"about.importFailed").c_str(), tip.c_str(),
				MB_OK | MB_ICONWARNING);
			return;
		}
		MessageBoxW(owner, Lang::get(L"about.importOk").c_str(), tip.c_str(), MB_OK | MB_ICONINFORMATION);
		// 提示关掉后自动重启，让新实例重新读一遍配置。
		auto relaunch = [owner]() {
			if (::App::restart()) return;
			MessageBoxW(owner, Lang::get(L"about.importDone").c_str(),
				Lang::get(L"about.sysTip").c_str(), MB_OK | MB_ICONWARNING);
		};
		if (auto* a = Ling::App::get()) a->dq.TryEnqueue(relaunch);
		else relaunch();
	});

	lightLink(Lang::get(L"about.exportConfig"), nullptr, false, [owner]() {
		const auto path = Util::getSaveFilePath(owner, L"json", L"ScreenShut-config.json", L"");
		if (path.empty()) return;
		if (!Setting::get()->exportConfigTo(path)) {
			MessageBoxW(owner, Lang::get(L"about.exportFailed").c_str(),
				Lang::get(L"about.sysTip").c_str(), MB_OK | MB_ICONWARNING);
		}
	});

	lightLink(Lang::get(L"about.openDataDir"), Icon::Folder, false, [owner]() {
		const auto dir = Setting::get()->getDataPath();
		ShellExecuteW(owner, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	});
}

WinSettingAbout::~WinSettingAbout() = default;
