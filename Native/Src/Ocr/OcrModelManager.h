#pragma once
#include <string>
#include <vector>

// 模型不绑定固定档位：ocr-models 下的每个子目录就是一个可选模型，
// 设置页把目录名原样列出来，用户也可以手动丢一份自己的模型进去。
class OcrModelManager {
public:
	static OcrModelManager& instance();
	// 扫描 ocr-models 下的可用模型目录名（引擎要的四个文件齐全且非 0 字节），按名称排序
	std::vector<std::wstring> listModels() const;
	// 单个目录名是否具备引擎所需的全部文件
	bool isReady(const std::wstring& name) const;
	std::wstring modelsDir(const std::wstring& name) const;
private:
	OcrModelManager() = default;
};
