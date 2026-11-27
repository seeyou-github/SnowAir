#include "pch.h"
#include "OcrRuntimeManager.h"
#include "OcrPackVariant.h"
#include "PluginPaths.h"
#include "LwPpOcrEngine.h"
#include <fstream>

namespace {
	constexpr const char* kRuntimeVersion = "1.2.2";
	constexpr const wchar_t* kRuntimeMarker = L"runtime.id";
}

OcrRuntimeManager& OcrRuntimeManager::instance()
{
	static OcrRuntimeManager inst;
	return inst;
}

void OcrRuntimeManager::uninstall(OcrPackVariant v)
{
	// 先放掉引擎与 DLL，否则文件被占用删不干净
	LwPpOcrEngine::release();
	std::error_code ec;
	std::filesystem::remove_all(PluginPaths::ocrRuntime(v), ec);
	std::filesystem::remove_all(PluginPaths::ocrBin(), ec);   // 老版本遗留的 RapidOCR.dll 目录
}

bool OcrRuntimeManager::isInstalled(OcrPackVariant v) const
{
	std::error_code ec;
	auto dll = PluginPaths::ocrRuntime(v) / L"lw_ppocr_c.dll";
	if (!std::filesystem::is_regular_file(dll, ec) || std::filesystem::file_size(dll, ec) <= 100 * 1024)
		return false;
	std::ifstream marker(PluginPaths::ocrRuntime(v) / kRuntimeMarker, std::ios::binary);
	std::string value;
	std::getline(marker, value);
	while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' '))
		value.pop_back();
	return value == kRuntimeVersion;
}

bool OcrRuntimeManager::hasOutdated(OcrPackVariant v) const
{
	std::error_code ec;
	auto dll = PluginPaths::ocrRuntime(v) / L"lw_ppocr_c.dll";
	if (!std::filesystem::is_regular_file(dll, ec) || std::filesystem::file_size(dll, ec) <= 100 * 1024)
		return false;
	return !isInstalled(v);
}
