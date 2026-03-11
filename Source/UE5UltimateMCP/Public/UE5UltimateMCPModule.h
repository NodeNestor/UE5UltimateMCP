#pragma once

#include "Modules/ModuleManager.h"

class FUE5UltimateMCPModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
