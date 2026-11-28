#include "pch.h"
#include "OcrModelManager.h"
#include "PluginPaths.h"
#include <algorithm>

namespace {
	// 引擎实际要读的四个文件（det/cls/rec 的 LWM 模型 + 识别字典），缺一个或为空都不可用
	const wchar_t* const kFiles[] = { L"det.lwm", L"cls.lwm", L"rec.lwm", L"ppocr_keys.txt" };
}

OcrModelManager& OcrModelManager::instance()
{
	static OcrModelManager inst;
	return inst;
}

std::wstring OcrModelManager::modelsDir(const std::wstring& name) const
{
	return (PluginPaths::ocrModels() / name).wstring();
}

bool OcrModelManager::isReady(const std::wstring& name) const
{
	std::error_code ec;
	const auto dir = PluginPaths::ocrModels() / name;
	for (const auto* file : kFiles) {
		auto p = dir / file;
		if (!std::filesystem::is_regular_file(p, ec)) return false;
		if (std::filesystem::file_size(p, ec) == 0) return false;
	}
	return true;
}

std::vector<std::wstring> OcrModelManager::listModels() const
{
	std::vector<std::wstring> out;
	std::error_code ec;
	for (const auto& entry : std::filesystem::directory_iterator(PluginPaths::ocrModels(), ec)) {
		if (!entry.is_directory(ec)) continue;
		const std::wstring name = entry.path().filename().wstring();
		if (!isReady(name)) continue;
		out.push_back(name);
	}
	// 目录枚举顺序不保证，按名称排一下，让设置里的列表顺序稳定
	std::sort(out.begin(), out.end(), [](const std::wstring& a, const std::wstring& b) {
		return _wcsicmp(a.c_str(), b.c_str()) < 0;
	});
	return out;
}
