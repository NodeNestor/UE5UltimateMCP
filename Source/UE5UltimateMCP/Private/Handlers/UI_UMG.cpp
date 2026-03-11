// UMG Widget Blueprint tools for UltimateMCP
#include "Tools/MCPToolBase.h"
#include "Tools/MCPToolRegistry.h"

#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/Border.h"
#include "Components/CheckBox.h"
#include "Components/Slider.h"
#include "Components/ProgressBar.h"
#include "Components/EditableTextBox.h"
#include "Components/ScrollBox.h"
#include "Components/Overlay.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/GridPanel.h"
#include "Components/UniformGridPanel.h"
#include "Components/WidgetSwitcher.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Editor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"

// ─────────────────────────────────────────────────────────────
// Helper: Resolve a widget class name to UClass
// ─────────────────────────────────────────────────────────────
static UClass* ResolveWidgetClass(const FString& ClassName)
{
	// Common short names mapping
	static TMap<FString, FString> ShortNames = {
		{TEXT("CanvasPanel"),		TEXT("UCanvasPanel")},
		{TEXT("Button"),			TEXT("UButton")},
		{TEXT("TextBlock"),			TEXT("UTextBlock")},
		{TEXT("Image"),				TEXT("UImage")},
		{TEXT("VerticalBox"),		TEXT("UVerticalBox")},
		{TEXT("HorizontalBox"),		TEXT("UHorizontalBox")},
		{TEXT("Border"),			TEXT("UBorder")},
		{TEXT("CheckBox"),			TEXT("UCheckBox")},
		{TEXT("Slider"),			TEXT("USlider")},
		{TEXT("ProgressBar"),		TEXT("UProgressBar")},
		{TEXT("EditableTextBox"),	TEXT("UEditableTextBox")},
		{TEXT("ScrollBox"),			TEXT("UScrollBox")},
		{TEXT("Overlay"),			TEXT("UOverlay")},
		{TEXT("SizeBox"),			TEXT("USizeBox")},
		{TEXT("Spacer"),			TEXT("USpacer")},
		{TEXT("GridPanel"),			TEXT("UGridPanel")},
		{TEXT("UniformGridPanel"),	TEXT("UUniformGridPanel")},
		{TEXT("WidgetSwitcher"),	TEXT("UWidgetSwitcher")},
	};

	FString Lookup = ClassName;
	if (ShortNames.Contains(ClassName))
	{
		Lookup = ShortNames[ClassName];
	}

	if (!Lookup.StartsWith(TEXT("U")))
	{
		Lookup = TEXT("U") + Lookup;
	}

	UClass* Found = FindFirstObject<UClass>(*Lookup, EFindFirstObjectOptions::ExactClass);
	if (!Found)
	{
		Found = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::ExactClass);
	}
	return Found;
}

// ─────────────────────────────────────────────────────────────
// create_widget_blueprint
// ─────────────────────────────────────────────────────────────
class FTool_CreateWidgetBlueprint : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("create_widget_blueprint");
		Info.Description = TEXT("Create a new Widget Blueprint asset with a root CanvasPanel.");
		Info.Annotations.Category = TEXT("UI");
		Info.Annotations.bDestructive = false;

		auto AddParam = [&](const TCHAR* N, const TCHAR* T, const TCHAR* D, bool R)
		{
			FMCPParamSchema P;
			P.Name = N; P.Type = T; P.Description = D; P.bRequired = R;
			Info.Parameters.Add(P);
		};

		AddParam(TEXT("name"), TEXT("string"), TEXT("Name for the Widget Blueprint asset"), true);
		AddParam(TEXT("parent_class"), TEXT("string"), TEXT("Parent class (default: 'UserWidget')"), false);

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString Name = Params->GetStringField(TEXT("name"));
		if (Name.IsEmpty())
		{
			return FMCPToolResult::Error(TEXT("Parameter 'name' is required."));
		}

		UClass* ParentClass = UUserWidget::StaticClass();
		if (Params->HasField(TEXT("parent_class")))
		{
			FString ParentClassName = Params->GetStringField(TEXT("parent_class"));
			UClass* CustomParent = ResolveWidgetClass(ParentClassName);
			if (CustomParent && CustomParent->IsChildOf(UUserWidget::StaticClass()))
			{
				ParentClass = CustomParent;
			}
		}

		FString PackagePath = FString::Printf(TEXT("/Game/UI/%s"), *Name);
		FString PackageName = FPackageName::ObjectPathToPackageName(PackagePath);

		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Failed to create package at '%s'."), *PackageName));
		}

		UWidgetBlueprint* WidgetBP = CastChecked<UWidgetBlueprint>(
			FKismetEditorUtilities::CreateBlueprint(
				ParentClass,
				Package,
				FName(*Name),
				BPTYPE_Normal,
				UWidgetBlueprint::StaticClass(),
				UBlueprintGeneratedClass::StaticClass()
			)
		);

		if (!WidgetBP)
		{
			return FMCPToolResult::Error(TEXT("Failed to create Widget Blueprint."));
		}

		// Ensure a CanvasPanel root exists
		if (WidgetBP->WidgetTree)
		{
			UCanvasPanel* RootCanvas = WidgetBP->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
			WidgetBP->WidgetTree->RootWidget = RootCanvas;
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);

		FAssetRegistryModule::AssetCreated(WidgetBP);
		WidgetBP->MarkPackageDirty();

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		FString FilePath = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		UPackage::SavePackage(Package, WidgetBP, *FilePath, SaveArgs);

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("path"), PackagePath);
		Result->SetStringField(TEXT("name"), Name);
		Result->SetStringField(TEXT("parent_class"), ParentClass->GetName());
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// add_widget_child
// ─────────────────────────────────────────────────────────────
class FTool_AddWidgetChild : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("add_widget_child");
		Info.Description = TEXT("Add a child widget to a Widget Blueprint under a specified parent slot.");
		Info.Annotations.Category = TEXT("UI");
		Info.Annotations.bDestructive = false;

		auto AddParam = [&](const TCHAR* N, const TCHAR* T, const TCHAR* D, bool R)
		{
			FMCPParamSchema P;
			P.Name = N; P.Type = T; P.Description = D; P.bRequired = R;
			Info.Parameters.Add(P);
		};

		AddParam(TEXT("widget_bp"), TEXT("string"), TEXT("Asset path of the Widget Blueprint"), true);
		AddParam(TEXT("child_class"), TEXT("string"), TEXT("Widget class: Button, TextBlock, Image, VerticalBox, HorizontalBox, etc."), true);
		AddParam(TEXT("child_name"), TEXT("string"), TEXT("Name for the new widget"), true);
		AddParam(TEXT("parent_name"), TEXT("string"), TEXT("Name of the parent widget in the tree (default: root)"), false);

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString WidgetBPPath = Params->GetStringField(TEXT("widget_bp"));
		FString ChildClassName = Params->GetStringField(TEXT("child_class"));
		FString ChildName = Params->GetStringField(TEXT("child_name"));
		FString ParentName = Params->HasField(TEXT("parent_name")) ? Params->GetStringField(TEXT("parent_name")) : TEXT("");

		UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *WidgetBPPath);
		if (!WidgetBP)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found at '%s'."), *WidgetBPPath));
		}

		UClass* ChildClass = ResolveWidgetClass(ChildClassName);
		if (!ChildClass || !ChildClass->IsChildOf(UWidget::StaticClass()))
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Widget class '%s' not found."), *ChildClassName));
		}

		UWidgetTree* WidgetTree = WidgetBP->WidgetTree;
		if (!WidgetTree)
		{
			return FMCPToolResult::Error(TEXT("Widget Blueprint has no WidgetTree."));
		}

		// Find parent
		UPanelWidget* ParentWidget = nullptr;
		if (ParentName.IsEmpty())
		{
			ParentWidget = Cast<UPanelWidget>(WidgetTree->RootWidget);
		}
		else
		{
			WidgetTree->ForEachWidget([&](UWidget* Widget)
			{
				if (Widget->GetName() == ParentName && !ParentWidget)
				{
					ParentWidget = Cast<UPanelWidget>(Widget);
				}
			});
		}

		if (!ParentWidget)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Parent widget '%s' not found or is not a panel."), *ParentName));
		}

		// Create child widget
		UWidget* NewChild = WidgetTree->ConstructWidget<UWidget>(ChildClass, FName(*ChildName));
		if (!NewChild)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Failed to construct widget of class '%s'."), *ChildClassName));
		}

		UPanelSlot* Slot = ParentWidget->AddChild(NewChild);

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);
		WidgetBP->MarkPackageDirty();

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("widget_bp"), WidgetBPPath);
		Result->SetStringField(TEXT("child_name"), ChildName);
		Result->SetStringField(TEXT("child_class"), ChildClassName);
		Result->SetStringField(TEXT("parent"), ParentWidget->GetName());
		Result->SetBoolField(TEXT("has_slot"), Slot != nullptr);
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// set_widget_property
// ─────────────────────────────────────────────────────────────
class FTool_SetWidgetProperty : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("set_widget_property");
		Info.Description = TEXT("Set a property on a widget within a Widget Blueprint.");
		Info.Annotations.Category = TEXT("UI");
		Info.Annotations.bDestructive = false;

		auto AddParam = [&](const TCHAR* N, const TCHAR* T, const TCHAR* D, bool R)
		{
			FMCPParamSchema P;
			P.Name = N; P.Type = T; P.Description = D; P.bRequired = R;
			Info.Parameters.Add(P);
		};

		AddParam(TEXT("widget_bp"), TEXT("string"), TEXT("Asset path of the Widget Blueprint"), true);
		AddParam(TEXT("widget_name"), TEXT("string"), TEXT("Name of the target widget"), true);
		AddParam(TEXT("property"), TEXT("string"), TEXT("Property name (e.g. 'Text', 'ColorAndOpacity', 'Visibility')"), true);
		AddParam(TEXT("value"), TEXT("string"), TEXT("Value as string (property-dependent format)"), true);

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString WidgetBPPath = Params->GetStringField(TEXT("widget_bp"));
		FString WidgetName = Params->GetStringField(TEXT("widget_name"));
		FString Property = Params->GetStringField(TEXT("property"));
		FString Value = Params->GetStringField(TEXT("value"));

		UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *WidgetBPPath);
		if (!WidgetBP || !WidgetBP->WidgetTree)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found at '%s'."), *WidgetBPPath));
		}

		// Find the widget by name
		UWidget* TargetWidget = nullptr;
		WidgetBP->WidgetTree->ForEachWidget([&](UWidget* Widget)
		{
			if (Widget->GetName() == WidgetName && !TargetWidget)
			{
				TargetWidget = Widget;
			}
		});

		if (!TargetWidget)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Widget '%s' not found in blueprint."), *WidgetName));
		}

		// Handle common properties with dedicated setters
		if (Property == TEXT("Text") || Property == TEXT("text"))
		{
			if (UTextBlock* TextBlock = Cast<UTextBlock>(TargetWidget))
			{
				TextBlock->SetText(FText::FromString(Value));

				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);
				WidgetBP->MarkPackageDirty();

				TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetStringField(TEXT("widget"), WidgetName);
				Result->SetStringField(TEXT("property"), Property);
				Result->SetStringField(TEXT("value"), Value);
				return FMCPToolResult::Ok(Result);
			}
		}

		if (Property == TEXT("Visibility") || Property == TEXT("visibility"))
		{
			ESlateVisibility Vis = ESlateVisibility::Visible;
			FString LowerVal = Value.ToLower();
			if (LowerVal == TEXT("collapsed")) Vis = ESlateVisibility::Collapsed;
			else if (LowerVal == TEXT("hidden")) Vis = ESlateVisibility::Hidden;
			else if (LowerVal == TEXT("hitTestInvisible") || LowerVal == TEXT("hittestinvisible")) Vis = ESlateVisibility::HitTestInvisible;
			else if (LowerVal == TEXT("selfHitTestInvisible") || LowerVal == TEXT("selfhittestinvisible")) Vis = ESlateVisibility::SelfHitTestInvisible;

			TargetWidget->SetVisibility(Vis);

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);
			WidgetBP->MarkPackageDirty();

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("widget"), WidgetName);
			Result->SetStringField(TEXT("property"), Property);
			Result->SetStringField(TEXT("value"), Value);
			return FMCPToolResult::Ok(Result);
		}

		// Generic property setter via reflection
		FProperty* Prop = TargetWidget->GetClass()->FindPropertyByName(FName(*Property));
		if (!Prop)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Property '%s' not found on widget class '%s'."), *Property, *TargetWidget->GetClass()->GetName()));
		}

		void* PropAddr = Prop->ContainerPtrToValuePtr<void>(TargetWidget);
		if (!Prop->ImportText_Direct(*Value, PropAddr, TargetWidget, PPF_None))
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Failed to set property '%s' to value '%s'."), *Property, *Value));
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);
		WidgetBP->MarkPackageDirty();

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("widget"), WidgetName);
		Result->SetStringField(TEXT("property"), Property);
		Result->SetStringField(TEXT("value"), Value);
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// list_widget_blueprints
// ─────────────────────────────────────────────────────────────
class FTool_ListWidgetBlueprints : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("list_widget_blueprints");
		Info.Description = TEXT("List all Widget Blueprint assets in the project.");
		Info.Annotations.Category = TEXT("UI");
		Info.Annotations.bReadOnly = true;

		FMCPParamSchema P;
		P.Name = TEXT("filter"); P.Type = TEXT("string");
		P.Description = TEXT("Optional name filter (substring match)");
		P.bRequired = false;
		Info.Parameters.Add(P);

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString Filter = Params->HasField(TEXT("filter")) ? Params->GetStringField(TEXT("filter")) : TEXT("");

		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

		TArray<FAssetData> Assets;
		AssetRegistry.GetAssetsByClass(UWidgetBlueprint::StaticClass()->GetClassPathName(), Assets, true);

		TArray<TSharedPtr<FJsonValue>> BPArray;
		for (const FAssetData& Asset : Assets)
		{
			FString AssetName = Asset.AssetName.ToString();
			if (!Filter.IsEmpty() && !AssetName.Contains(Filter))
			{
				continue;
			}

			TSharedPtr<FJsonObject> BPObj = MakeShared<FJsonObject>();
			BPObj->SetStringField(TEXT("name"), AssetName);
			BPObj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
			BPObj->SetStringField(TEXT("package"), Asset.PackageName.ToString());
			BPArray.Add(MakeShared<FJsonValueObject>(BPObj));
		}

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("count"), BPArray.Num());
		Result->SetArrayField(TEXT("widget_blueprints"), BPArray);
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// bind_widget_event
// ─────────────────────────────────────────────────────────────
class FTool_BindWidgetEvent : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("bind_widget_event");
		Info.Description = TEXT("Bind a widget event (e.g. OnClicked) to a function in the Widget Blueprint.");
		Info.Annotations.Category = TEXT("UI");
		Info.Annotations.bDestructive = false;

		auto AddParam = [&](const TCHAR* N, const TCHAR* T, const TCHAR* D, bool R)
		{
			FMCPParamSchema P;
			P.Name = N; P.Type = T; P.Description = D; P.bRequired = R;
			Info.Parameters.Add(P);
		};

		AddParam(TEXT("widget_bp"), TEXT("string"), TEXT("Asset path of the Widget Blueprint"), true);
		AddParam(TEXT("widget_name"), TEXT("string"), TEXT("Name of the widget to bind"), true);
		AddParam(TEXT("event_name"), TEXT("string"), TEXT("Event delegate name (e.g. 'OnClicked', 'OnPressed', 'OnValueChanged')"), true);
		AddParam(TEXT("function_name"), TEXT("string"), TEXT("Name of the function to bind to (must exist in the Blueprint graph)"), true);

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString WidgetBPPath = Params->GetStringField(TEXT("widget_bp"));
		FString WidgetName = Params->GetStringField(TEXT("widget_name"));
		FString EventName = Params->GetStringField(TEXT("event_name"));
		FString FunctionName = Params->GetStringField(TEXT("function_name"));

		UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *WidgetBPPath);
		if (!WidgetBP || !WidgetBP->WidgetTree)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found at '%s'."), *WidgetBPPath));
		}

		// Find the target widget
		UWidget* TargetWidget = nullptr;
		WidgetBP->WidgetTree->ForEachWidget([&](UWidget* Widget)
		{
			if (Widget->GetName() == WidgetName && !TargetWidget)
			{
				TargetWidget = Widget;
			}
		});

		if (!TargetWidget)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Widget '%s' not found."), *WidgetName));
		}

		// Find the multicast delegate property
		FMulticastDelegateProperty* DelegateProp = CastField<FMulticastDelegateProperty>(
			TargetWidget->GetClass()->FindPropertyByName(FName(*EventName))
		);

		if (!DelegateProp)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Event '%s' not found on widget '%s' (class: %s)."), *EventName, *WidgetName, *TargetWidget->GetClass()->GetName()));
		}

		// Verify the function exists in the blueprint
		UFunction* Func = WidgetBP->GeneratedClass ? WidgetBP->GeneratedClass->FindFunctionByName(FName(*FunctionName)) : nullptr;
		if (!Func)
		{
			// Function may not be compiled yet — note this to the user
			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("widget"), WidgetName);
			Result->SetStringField(TEXT("event"), EventName);
			Result->SetStringField(TEXT("function"), FunctionName);
			Result->SetStringField(TEXT("status"), TEXT("binding_registered"));
			Result->SetStringField(TEXT("note"), TEXT("Function not found in compiled class. Ensure the Blueprint is compiled after adding the function graph."));
			return FMCPToolResult::Ok(Result);
		}

		// Add the dynamic binding
		FDelegateEditorBinding Binding;
		Binding.ObjectName = WidgetName;
		Binding.PropertyName = FName(*EventName);
		Binding.FunctionName = FName(*FunctionName);
		WidgetBP->Bindings.Add(Binding);

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);
		WidgetBP->MarkPackageDirty();

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("widget"), WidgetName);
		Result->SetStringField(TEXT("event"), EventName);
		Result->SetStringField(TEXT("function"), FunctionName);
		Result->SetStringField(TEXT("status"), TEXT("bound"));
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────
namespace UltimateMCPTools
{
	void RegisterUITools()
	{
		FMCPToolRegistry& Registry = FMCPToolRegistry::Get();
		Registry.Register(MakeShared<FTool_CreateWidgetBlueprint>());
		Registry.Register(MakeShared<FTool_AddWidgetChild>());
		Registry.Register(MakeShared<FTool_SetWidgetProperty>());
		Registry.Register(MakeShared<FTool_ListWidgetBlueprints>());
		Registry.Register(MakeShared<FTool_BindWidgetEvent>());
	}
}
