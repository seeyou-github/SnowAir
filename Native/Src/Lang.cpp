#include "pch.h"
#include "Lang.h"
#include "Util.h"

std::unique_ptr<Lang> lang;

Lang::Lang()
{
}

Lang::~Lang()
{
}

void Lang::init()
{
	lang.reset(new Lang());
	lang->load(L"zh-CN");
}

void Lang::dispose()
{
	lang.reset();
}

Lang* Lang::get()
{
	return lang.get();
}

std::wstring Lang::get(const std::wstring& keyPath)
{
	// 支持任意层级的点路径（如 imageEdit.tool.split）：中间段必须是对象，叶子必须是字符串，
	// 类型不符按缺键处理——否则 GetNamedString 拿到对象会抛 hresult_error，异常穿透
	// DispatcherQueue 回调直接杀进程。缺键时直接显示键名，便于定位。
	auto arr = Ling::Util::splitStr(keyPath, L'.');
	if (arr.size() < 2) return keyPath;

	auto readPath = [](JsonObject const& root, const std::vector<std::wstring>& path) -> std::wstring {
		try {
			JsonObject obj = root;
			if (!obj) return {};
			for (size_t i = 0; i + 1 < path.size(); ++i) {
				obj = obj.GetNamedObject(path[i], nullptr);
				if (!obj) return {};
			}
			auto value = obj.GetNamedValue(path.back(), nullptr);
			if (!value || value.ValueType() != JsonValueType::String) return {};
			return std::wstring{ value.GetString() };
		}
		catch (...) {
			return {};
		}
	};

	if (!lang) return keyPath;
	auto text = readPath(lang->langObj, arr);
	return text.empty() ? keyPath : text;
}

void Lang::load(const std::wstring& langCode)
{
	auto [pData, size] = Ling::Util::getRes(langCode);
	JsonObject obj{ nullptr };
	if (pData && size > 0) {
		const auto json = Ling::Util::readTextFromBytes(pData, size);
		JsonObject::TryParse(json, obj);
	}
	langObj = obj;
}
