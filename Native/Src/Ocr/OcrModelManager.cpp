#include "pch.h"
#include "OcrModelManager.h"
#include "OcrPackVariant.h"
#include "PluginPaths.h"
#include "LwPpOcrEngine.h"
#include <fstream>

namespace {
	// 随程序分发的 LWM 模型（CI 由官方 ONNX 转换而来）。
	const wchar_t* const kFiles[] = { L"det.lwm", L"cls.lwm", L"rec.lwm", L"ppocr_keys.txt" };
	const wchar_t* const kPackMarkerName = L"pack.id";
}

OcrModelManager& OcrModelManager::instance()
{
	static OcrModelManager inst;
	return inst;
}

std::wstring OcrModelManager::modelsDir(OcrPackVariant v) const
{
	return (PluginPaths::ocrModels() / OcrPack::folderName(v)).wstring();
}

void OcrModelManager::uninstall(OcrPackVariant v)
{
	// 引擎读着模型文件，先把它（连同 DLL）放掉再删，否则删除会失败/半截
	LwPpOcrEngine::release();
	std::error_code ec;
	std::filesystem::remove_all(PluginPaths::ocrModels() / OcrPack::folderName(v), ec);
}

bool OcrModelManager::isInstalled(OcrPackVariant v) const
{
	auto dir = PluginPaths::ocrModels() / OcrPack::folderName(v);
	std::error_code ec;
	if (!std::filesystem::is_directory(dir, ec)) return false;
	for (const auto* name : kFiles) {
		auto p = dir / name;
		if (!std::filesystem::is_regular_file(p, ec)) return false;
		if (std::filesystem::file_size(p, ec) == 0) return false;
	}
	std::ifstream marker(dir / kPackMarkerName, std::ios::binary);
	std::string value;
	std::getline(marker, value);
	while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' '))
		value.pop_back();
	return value == OcrPack::markerValue(v);
}

bool OcrModelManager::hasOutdated(OcrPackVariant v) const
{
	auto dir = PluginPaths::ocrModels() / OcrPack::folderName(v);
	std::error_code ec;
	if (!std::filesystem::is_directory(dir, ec)) return false;
	for (const auto* name : kFiles) {
		auto p = dir / name;
		if (!std::filesystem::is_regular_file(p, ec) || std::filesystem::file_size(p, ec) == 0)
			return false;
	}
	return !isInstalled(v);
}
