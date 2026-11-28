#pragma once
// PP-OCRv6 运行时随程序分发，不再提供下载/安装流程。
// 运行时 DLL 一份、与哪个模型目录无关，所以检查不需要带模型参数。
class OcrRuntimeManager {
public:
	static OcrRuntimeManager& instance();
	bool isInstalled() const;
private:
	OcrRuntimeManager() = default;
};
