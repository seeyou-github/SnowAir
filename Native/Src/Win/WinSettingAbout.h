#pragma once
#include <include/Ling.h>

// 设置中心「关于」页：标题、版本、口号与配置导入/导出入口。
class WinSettingAbout : public Ling::Node
{
public:
	WinSettingAbout(Ling::WinBase* parent);
	~WinSettingAbout();
};
