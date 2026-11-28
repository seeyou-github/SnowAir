#include "pch.h"
#include "OcrRuntimeManager.h"
#include "PluginPaths.h"
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

bool OcrRuntimeManager::isInstalled() const
{
	std::error_code ec;
	const auto dir = PluginPaths::ocrRuntime();
	auto dll = dir / L"lw_ppocr_c.dll";
	if (!std::filesystem::is_regular_file(dll, ec) || std::filesystem::file_size(dll, ec) <= 100 * 1024)
		return false;
	std::ifstream marker(dir / kRuntimeMarker, std::ios::binary);
	std::string value;
	std::getline(marker, value);
	while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' '))
		value.pop_back();
	return value == kRuntimeVersion;
}
