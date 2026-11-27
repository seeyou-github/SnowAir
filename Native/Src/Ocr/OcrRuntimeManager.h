#pragma once
#include "OcrPackVariant.h"
#include <string>

// PP-OCRv6 运行时随程序分发，不再提供下载/安装流程。
class OcrRuntimeManager {
public:
	static OcrRuntimeManager& instance();
	bool isInstalled(OcrPackVariant v) const;
	// 有 DLL 但 runtime.id 与内置版本不一致
	bool hasOutdated(OcrPackVariant v) const;
	/** 卸载：删除 PP-OCRv6 运行时目录（含老版本遗留的 RapidOCR/ONNX 依赖目录） */
	void uninstall(OcrPackVariant v);
private:
	OcrRuntimeManager() = default;
};
