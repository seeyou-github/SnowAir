#pragma once
#include "OcrPackVariant.h"
#include <string>

// LWM 模型随程序分发，不再提供下载/安装流程。
class OcrModelManager {
public:
	static OcrModelManager& instance();
	bool isInstalled(OcrPackVariant v) const;
	// 模型文件齐全但 pack.id 与内置版本不一致
	bool hasOutdated(OcrPackVariant v) const;
	/** 卸载：删除该方案的模型目录 */
	void uninstall(OcrPackVariant v);
	std::wstring modelsDir(OcrPackVariant v) const;
private:
	OcrModelManager() = default;
};
