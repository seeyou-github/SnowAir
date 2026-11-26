#pragma once
#include <filesystem>
#include "OcrPackVariant.h"
#include "../Setting.h"

namespace PluginPaths {
	// 解析插件根目录的优先级：
	//   1) config.json 的 common.pluginDir 显式指定（想放哪就放哪）
	//   2) 退回 AppData\config\plugin（默认位置）
	inline std::filesystem::path root()
	{
		auto* s = Setting::get();
		if (!s) return std::filesystem::path{ L"plugin" };
		std::error_code ec;
		auto custom = s->getPluginDir();
		if (!custom.empty()) {
			const std::filesystem::path customPath{ custom };
			// 配置里记的目录还在（用户明确选过）就听它的；已经不在了（换盘符 / 被删 /
			// 配置被外部改坏）就不能死认 —— 否则组件明明装在标准位置，也会报
			// 「PP-OCRv6 运行时未安装」、已安装组件计数变成 0。
			if (std::filesystem::is_directory(customPath, ec)) return customPath;
		}
		return s->getDataPath() / L"plugin";
	}
	inline std::filesystem::path ocrModels() { return root() / L"ocr-models"; }
	// 运行时（lw_ppocr_c.dll + VC 运行库）与模型档位无关，各档共用一份
	inline std::filesystem::path ocrRuntime(OcrPackVariant) { return root() / L"ocr-runtime" / L"ppocr-v6"; }
	inline std::filesystem::path ocrBin() { return root() / L"ocr-bin"; }
	inline std::filesystem::path translateRoot() { return root() / L"translate"; }
	inline std::filesystem::path bergamotDll() { return translateRoot() / L"bergamot.dll"; }
	inline std::filesystem::path translateModels() { return translateRoot() / L"models"; }
}
