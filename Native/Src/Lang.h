#pragma once
#include <include/Ling.h>
#include <winrt/Windows.Data.Json.h>
using namespace winrt::Windows::Data::Json;

// 界面文案固定使用内置简体中文资源。
class Lang
{
public:
	~Lang();
	static void init();
	// 同 Setting::dispose：langObj 是 WinRT 对象，得趁 COM 还活着放掉
	static void dispose();
	static Lang* get();
	static std::wstring get(const std::wstring& keyPath);
private:
	Lang();
	void load(const std::wstring& langCode);
private:
	JsonObject langObj;
};
