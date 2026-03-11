// SEH (Structured Exception Handling) wrappers for crash-safe Blueprint and Material operations.
// Windows-only: wraps potentially crashing UE calls so the editor doesn't hard-crash.

#include "CoreMinimal.h"

#if PLATFORM_WINDOWS

#include "Engine/Blueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialExpression.h"
#include "MaterialEditingLibrary.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>

int32 TryCompileBlueprintSEH(UBlueprint* BP, EBlueprintCompileOptions Opts)
{
	__try
	{
		FKismetEditorUtilities::CompileBlueprint(BP, Opts, nullptr);
		return 0; // success
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return 1; // crash caught
	}
}

int32 TryAddMaterialExpressionSEH(
	UObject* Owner, UClass* ExprClass, UMaterial* Material, UMaterialFunction* MatFunc,
	int32 PosX, int32 PosY, UMaterialExpression** OutExpr)
{
	__try
	{
		UMaterialExpression* Expr = nullptr;
		if (Material)
		{
			Expr = UMaterialEditingLibrary::CreateMaterialExpression(Material, ExprClass, PosX, PosY);
		}
		else if (MatFunc)
		{
			Expr = UMaterialEditingLibrary::CreateMaterialExpressionInFunction(MatFunc, ExprClass, PosX, PosY);
		}
		if (OutExpr) *OutExpr = Expr;
		return Expr ? 0 : -1;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		if (OutExpr) *OutExpr = nullptr;
		return 1; // crash caught
	}
}

#include "Windows/HideWindowsPlatformTypes.h"

#endif // PLATFORM_WINDOWS
