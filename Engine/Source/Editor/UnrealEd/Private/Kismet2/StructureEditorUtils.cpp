// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "UnrealEd.h"
#include "StructureEditorUtils.h"
#include "ScopedTransaction.h"
#include "Kismet2NameValidators.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraphSchema_K2.h"
#include "ObjectTools.h"
#include "Editor/UnrealEd/Public/Kismet2/CompilerResultsLog.h"
#include "Editor/UnrealEd/Public/EditorModes.h"
#include "Editor/KismetCompiler/Public/KismetCompilerModule.h"
#include "Toolkits/AssetEditorManager.h"
#include "Editor/DataTableEditor/Public/IDataTableEditor.h"

#define LOCTEXT_NAMESPACE "Structure"

//////////////////////////////////////////////////////////////////////////
// FStructEditorManager
FStructureEditorUtils::FStructEditorManager::FStructEditorManager() : FListenerManager<UUserDefinedStruct>()
{}

FStructureEditorUtils::FStructEditorManager& FStructureEditorUtils::FStructEditorManager::Get()
{
	static TSharedRef< FStructEditorManager > EditorManager( new FStructEditorManager() );
	return *EditorManager;
}

//////////////////////////////////////////////////////////////////////////
// FStructureEditorUtils
UUserDefinedStruct* FStructureEditorUtils::CreateUserDefinedStruct(UObject* InParent, FName Name, EObjectFlags Flags)
{
	UUserDefinedStruct* Struct = NULL;
	
	if (UserDefinedStructEnabled())
	{
		Struct = NewNamedObject<UUserDefinedStruct>(InParent, Name, Flags);
		check(Struct);
		Struct->EditorData = NewNamedObject<UUserDefinedStructEditorData>(Struct, NAME_None, RF_Transactional);
		check(Struct->EditorData);

		Struct->SetMetaData(TEXT("BlueprintType"), TEXT("true"));
		Struct->Bind();
		Struct->StaticLink(true);
		Struct->Status = UDSS_Error;

		{
			const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
			AddVariable(Struct, FEdGraphPinType(K2Schema->PC_Boolean, FString(), NULL, false, false));
		}
	}

	return Struct;
}

FStructureEditorUtils::EStructureError FStructureEditorUtils::IsStructureValid(const UScriptStruct* Struct, const UStruct* RecursionParent, FString* OutMsg)
{
	check(Struct);
	if (Struct == RecursionParent)
	{
		if (OutMsg)
		{
			 *OutMsg = FString::Printf(*LOCTEXT("StructureRecursion", "Recursion: Struct cannot have itself as a member variable. Struct '%s', recursive parent '%s'").ToString(), 
				 *Struct->GetFullName(), *RecursionParent->GetFullName());
		}
		return EStructureError::Recursion;
	}

	const UScriptStruct* FallbackStruct = GetFallbackStruct();
	if (Struct == FallbackStruct)
	{
		if (OutMsg)
		{
			*OutMsg = LOCTEXT("StructureUnknown", "Struct unknown (deleted?)").ToString();
		}
		return EStructureError::FallbackStruct;
	}

	if (Struct->GetStructureSize() <= 0)
	{
		if (OutMsg)
		{
			*OutMsg = FString::Printf(*LOCTEXT("StructureSizeIsZero", "Struct '%s' is empty").ToString(), *Struct->GetFullName());
		}
		return EStructureError::EmptyStructure;
	}

	if (const UUserDefinedStruct* UDStruct = Cast<const UUserDefinedStruct>(Struct))
	{
		if (UDStruct->Status != EUserDefinedStructureStatus::UDSS_UpToDate)
		{
			if (OutMsg)
			{
				*OutMsg = FString::Printf(*LOCTEXT("StructureNotCompiled", "Struct '%s' is not compiled").ToString(), *Struct->GetFullName());
			}
			return EStructureError::NotCompiled;
		}

		for (const UProperty * P = Struct->PropertyLink; P; P = P->PropertyLinkNext)
		{
			const UStructProperty* StructProp = Cast<const UStructProperty>(P);
			if (NULL == StructProp)
			{
				if (const UArrayProperty* ArrayProp = Cast<const UArrayProperty>(P))
				{
					StructProp = Cast<const UStructProperty>(ArrayProp->Inner);
				}
			}

			if (StructProp)
			{
				if ((NULL == StructProp->Struct) || (FallbackStruct == StructProp->Struct))
				{
					if (OutMsg)
					{
						*OutMsg = FString::Printf(*LOCTEXT("StructureUnknownProperty", "Struct unknown (deleted?). Parent '%s' Property: '%s'").ToString(),
							*Struct->GetFullName(), *StructProp->GetName());
					}
					return EStructureError::FallbackStruct;
				}

				FString OutMsgInner;
				const EStructureError Result = IsStructureValid(
					StructProp->Struct,
					RecursionParent ? RecursionParent : Struct,
					OutMsg ? &OutMsgInner : NULL);
				if (EStructureError::Ok != Result)
				{
					if (OutMsg)
					{
						*OutMsg = FString::Printf(*LOCTEXT("StructurePropertyErrorTemplate", "Struct '%s' Property '%s' Error ( %s )").ToString(),
							*Struct->GetFullName(), *StructProp->GetName(), *OutMsgInner);
					}
					return Result;
				}
			}
		}
	}

	return EStructureError::Ok;
}

bool FStructureEditorUtils::CanHaveAMemberVariableOfType(const UUserDefinedStruct* Struct, const FEdGraphPinType& VarType, FString* OutMsg)
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	if ((VarType.PinCategory == K2Schema->PC_Struct) && Struct)
	{
		if (const UScriptStruct* SubCategoryStruct = Cast<const UScriptStruct>(VarType.PinSubCategoryObject.Get()))
		{
			const EStructureError Result = IsStructureValid(SubCategoryStruct, Struct, OutMsg);
			if (EStructureError::Ok != Result)
			{
				return false;
			}
		}
		else
		{
			if (OutMsg)
			{
				*OutMsg = LOCTEXT("StructureIncorrectStructType", "Icorrect struct type in a structure member variable.").ToString();
			}
			return false;
		}
	}
	else if ((VarType.PinCategory == K2Schema->PC_Exec) 
		|| (VarType.PinCategory == K2Schema->PC_Meta) 
		|| (VarType.PinCategory == K2Schema->PC_Wildcard)
		|| (VarType.PinCategory == K2Schema->PC_MCDelegate)
		|| (VarType.PinCategory == K2Schema->PC_Delegate))
	{
		if (OutMsg)
		{
			*OutMsg = LOCTEXT("StructureIncorrectTypeCategory", "Icorrect type for a structure member variable.").ToString();
		}
		return false;
	}
	else
	{
		const auto PinSubCategoryClass = Cast<const UClass>(VarType.PinSubCategoryObject.Get());
		if (PinSubCategoryClass && PinSubCategoryClass->IsChildOf(UBlueprint::StaticClass()))
		{
			if (OutMsg)
			{
				*OutMsg = LOCTEXT("StructureUseBlueprintReferences", "Struct cannot use any blueprint references").ToString();
			}
			return false;
		}
	}
	return true;
}

struct FMemberVariableNameHelper
{
	static FName Generate(UUserDefinedStruct* Struct, const FString& NameBase, const FGuid Guid, FString* OutFriendlyName = NULL)
	{
		check(Struct);

		FString Result;
		if (!NameBase.IsEmpty())
		{
			const FName NewNameBase(*NameBase);
			if (ensure(NewNameBase.IsValidXName(INVALID_OBJECTNAME_CHARACTERS)))
			{
				Result = NameBase;
			}
		}

		if (Result.IsEmpty())
		{
			Result = TEXT("MemberVar");
		}

		const uint32 UniqueNameId = CastChecked<UUserDefinedStructEditorData>(Struct->EditorData)->GenerateUniqueNameIdForMemberVariable();
		const FString FriendlyName = FString::Printf(TEXT("%s_%u"), *Result, UniqueNameId);
		if (OutFriendlyName)
		{
			*OutFriendlyName = FriendlyName;
		}
		const FName NameResult = *FString::Printf(TEXT("%s_%s"), *FriendlyName, *Guid.ToString(EGuidFormats::Digits));
		check(NameResult.IsValidXName(INVALID_OBJECTNAME_CHARACTERS));
		return NameResult;
	}

	static FGuid GetGuidFromName(const FName Name)
	{
		const FString NameStr = Name.ToString();
		const int32 GuidStrLen = 32;
		if (NameStr.Len() > (GuidStrLen + 1))
		{
			const int32 UnderscoreIndex = NameStr.Len() - GuidStrLen - 1;
			if (TCHAR('_') == NameStr[UnderscoreIndex])
			{
				const FString GuidStr = NameStr.Right(GuidStrLen);
				FGuid Guid;
				if (FGuid::ParseExact(GuidStr, EGuidFormats::Digits, Guid))
				{
					return Guid;
				}
			}
		}
		return FGuid();
	}
};

bool FStructureEditorUtils::AddVariable(UUserDefinedStruct* Struct, const FEdGraphPinType& VarType)
{
	if (Struct)
	{
		const FScopedTransaction Transaction( LOCTEXT("AddVariable", "Add Variable") );
		ModifyStructData(Struct);

		FString ErrorMessage;
		if (!CanHaveAMemberVariableOfType(Struct, VarType, &ErrorMessage))
		{
			UE_LOG(LogBlueprint, Warning, TEXT("%s"), *ErrorMessage);
			return false;
		}

		const FGuid Guid = FGuid::NewGuid();
		FString DisplayName;
		const FName VarName = FMemberVariableNameHelper::Generate(Struct, FString(), Guid, &DisplayName);
		check(NULL == GetVarDesc(Struct).FindByPredicate(FStructureEditorUtils::FFindByNameHelper<FStructVariableDescription>(VarName)));
		check(IsUniqueVariableDisplayName(Struct, DisplayName));

		FStructVariableDescription NewVar;
		NewVar.VarName = VarName;
		NewVar.FriendlyName = DisplayName;
		NewVar.SetPinType(VarType);
		NewVar.VarGuid = Guid;
		NewVar.bDontEditoOnInstance = false;
		NewVar.bInvalidMember = false;
		GetVarDesc(Struct).Add(NewVar);

		OnStructureChanged(Struct);
		return true;
	}
	return false;
}

bool FStructureEditorUtils::RemoveVariable(UUserDefinedStruct* Struct, FGuid VarGuid)
{
	if(Struct)
	{
		const auto OldNum = GetVarDesc(Struct).Num();
		const bool bAllowToMakeEmpty = false;
		if (bAllowToMakeEmpty || (OldNum > 1))
		{
			const FScopedTransaction Transaction(LOCTEXT("RemoveVariable", "Remove Variable"));
			ModifyStructData(Struct);

			GetVarDesc(Struct).RemoveAll(FFindByGuidHelper<FStructVariableDescription>(VarGuid));
			if (OldNum != GetVarDesc(Struct).Num())
			{
				OnStructureChanged(Struct);
				return true;
			}
		}
		else
		{
			UE_LOG(LogBlueprint, Log, TEXT("Member variable cannot be removed. User Defined Structure cannot be empty"));
		}
	}
	return false;
}

bool FStructureEditorUtils::RenameVariable(UUserDefinedStruct* Struct, FGuid VarGuid, const FString& NewDisplayNameStr)
{
	if (Struct)
	{
		auto VarDesc = GetVarDescByGuid(Struct, VarGuid);
		if (VarDesc && FName(*NewDisplayNameStr).IsValidXName(INVALID_OBJECTNAME_CHARACTERS) &&
			IsUniqueVariableDisplayName(Struct, NewDisplayNameStr))
		{
			const FScopedTransaction Transaction(LOCTEXT("RenameVariable", "Rename Variable"));
			ModifyStructData(Struct);

			VarDesc->FriendlyName = NewDisplayNameStr;
			//>>> TEMPORARY it's more important to prevent changes in structs instances, than to have consistent names
			if (GetGuidFromPropertyName(VarDesc->VarName).IsValid())
			//<<< TEMPORARY
			{
				const FName NewName = FMemberVariableNameHelper::Generate(Struct, NewDisplayNameStr, VarGuid);
				check(NULL == GetVarDesc(Struct).FindByPredicate(FFindByNameHelper<FStructVariableDescription>(NewName)))
				VarDesc->VarName = NewName;
			}
			OnStructureChanged(Struct);
			return true;
		}
	}
	return false;
}


bool FStructureEditorUtils::ChangeVariableType(UUserDefinedStruct* Struct, FGuid VarGuid, const FEdGraphPinType& NewType)
{
	if (Struct)
	{
		FString ErrorMessage;
		if(!CanHaveAMemberVariableOfType(Struct, NewType, &ErrorMessage))
		{
			UE_LOG(LogBlueprint, Warning, TEXT("%s"), *ErrorMessage);
			return false;
		}

		auto VarDesc = GetVarDescByGuid(Struct, VarGuid);
		if(VarDesc)
		{
			const bool bChangedType = (VarDesc->ToPinType() != NewType);
			if (bChangedType)
			{
				const FScopedTransaction Transaction(LOCTEXT("ChangeVariableType", "Change Variable Type"));
				ModifyStructData(Struct);

				VarDesc->VarName = FMemberVariableNameHelper::Generate(Struct, VarDesc->FriendlyName, VarDesc->VarGuid);
				VarDesc->DefaultValue = FString();
				VarDesc->SetPinType(NewType);

				OnStructureChanged(Struct);
				return true;
			}
		}
	}
	return false;
}

bool FStructureEditorUtils::ChangeVariableDefaultValue(UUserDefinedStruct* Struct, FGuid VarGuid, const FString& NewDefaultValue)
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	auto VarDesc = GetVarDescByGuid(Struct, VarGuid);
	if (VarDesc 
		&& (NewDefaultValue != VarDesc->DefaultValue)
		&& K2Schema->DefaultValueSimpleValidation(VarDesc->ToPinType(), FString(), NewDefaultValue, NULL, FText::GetEmpty()))
	{
		bool bAdvancedValidation = true;
		if (!NewDefaultValue.IsEmpty())
		{
			const auto Property = FindField<UProperty>(Struct, VarDesc->VarName);
			FStructOnScope StructDefaultMem(Struct);
			bAdvancedValidation = StructDefaultMem.IsValid() && Property &&
				FBlueprintEditorUtils::PropertyValueFromString(Property, NewDefaultValue, StructDefaultMem.GetStructMemory());
		}

		if (bAdvancedValidation)
		{
			const FScopedTransaction Transaction(LOCTEXT("ChangeVariableDefaultValue", "Change Variable Default Value"));
			ModifyStructData(Struct);

			VarDesc->DefaultValue = NewDefaultValue;
			OnStructureChanged(Struct);
			return true;
		}
	}
	return false;
}

bool FStructureEditorUtils::IsUniqueVariableDisplayName(const UUserDefinedStruct* Struct, const FString& DisplayName)
{
	if(Struct)
	{
		for (auto& VarDesc : GetVarDesc(Struct))
		{
			if (VarDesc.FriendlyName == DisplayName)
			{
				return false;
			}
		}
		return true;
	}
	return false;
}

FString FStructureEditorUtils::GetVariableDisplayName(const UUserDefinedStruct* Struct, FGuid VarGuid)
{
	const auto VarDesc = GetVarDescByGuid(Struct, VarGuid);
	return VarDesc ? VarDesc->FriendlyName : FString();
}

bool FStructureEditorUtils::UserDefinedStructEnabled()
{
	static FBoolConfigValueHelper UseUserDefinedStructure(TEXT("UserDefinedStructure"), TEXT("bUseUserDefinedStructure"));
	return UseUserDefinedStructure;
}

bool FStructureEditorUtils::Fill_MakeStructureDefaultValue(const UUserDefinedStruct* Struct, uint8* StructData)
{
	bool bResult = true;
	for (TFieldIterator<UProperty> It(Struct); It; ++It)
	{
		if (const UProperty* Property = *It)
		{
			auto VarDesc = GetVarDesc(Struct).FindByPredicate(FFindByNameHelper<FStructVariableDescription>(Property->GetFName()));
			if (VarDesc && !VarDesc->CurrentDefaultValue.IsEmpty())
			{
				bResult &= FBlueprintEditorUtils::PropertyValueFromString(Property, VarDesc->CurrentDefaultValue, StructData);
			}
			else
			{
				uint8* const InnerData = Property->ContainerPtrToValuePtr<uint8>(StructData);
				bResult &= Fill_MakeStructureDefaultValue(Property, InnerData);
			}
		}
	}

	return bResult;
}

bool FStructureEditorUtils::Fill_MakeStructureDefaultValue(const UProperty* Property, uint8* PropertyData)
{
	bool bResult = true;

	if (const UStructProperty* StructProperty = Cast<const UStructProperty>(Property))
	{
		if (const UUserDefinedStruct* InnerStruct = Cast<const UUserDefinedStruct>(StructProperty->Struct))
		{
			bResult &= Fill_MakeStructureDefaultValue(InnerStruct, PropertyData);
		}
	}
	else if (const UArrayProperty* ArrayProp = Cast<const UArrayProperty>(Property))
	{
		StructProperty = Cast<const UStructProperty>(ArrayProp->Inner);
		const UUserDefinedStruct* InnerStruct = StructProperty ? Cast<const UUserDefinedStruct>(StructProperty->Struct) : NULL;
		if(InnerStruct)
		{
			FScriptArrayHelper ArrayHelper(ArrayProp, PropertyData);
			for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
			{
				uint8* const ValuePtr = ArrayHelper.GetRawPtr(Index);
				bResult &= Fill_MakeStructureDefaultValue(InnerStruct, ValuePtr);
			}
		}
	}

	return bResult;
}

void FStructureEditorUtils::CompileStructure(UUserDefinedStruct* Struct)
{
	if (Struct)
	{
		IKismetCompilerInterface& Compiler = FModuleManager::LoadModuleChecked<IKismetCompilerInterface>(KISMET_COMPILER_MODULENAME);
		FCompilerResultsLog Results;
		Compiler.CompileStructure(Struct, Results);
	}
}

void FStructureEditorUtils::OnStructureChanged(UUserDefinedStruct* Struct)
{
	if (Struct)
	{
		Struct->Status = EUserDefinedStructureStatus::UDSS_Dirty;
		CompileStructure(Struct);
		Struct->MarkPackageDirty();
	}
}

//TODO: Move to blueprint utils
void FStructureEditorUtils::RemoveInvalidStructureMemberVariableFromBlueprint(UBlueprint* Blueprint)
{
	if (Blueprint)
	{
		const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
		const UScriptStruct* FallbackStruct = GetFallbackStruct();

		FString DislpayList;
		TArray<FName> ZombieMemberNames;
		for (int32 VarIndex = 0; VarIndex < Blueprint->NewVariables.Num(); ++VarIndex)
		{
			const FBPVariableDescription& Var = Blueprint->NewVariables[VarIndex];
			if (Var.VarType.PinCategory == K2Schema->PC_Struct)
			{
				const UScriptStruct* ScriptStruct = Cast<const UScriptStruct>(Var.VarType.PinSubCategoryObject.Get());
				const bool bInvalidStruct = (NULL == ScriptStruct) || (FallbackStruct == ScriptStruct);
				if (bInvalidStruct)
				{
					DislpayList += Var.FriendlyName.IsEmpty() ? Var.VarName.ToString() : Var.FriendlyName;
					DislpayList += TEXT("\n");
					ZombieMemberNames.Add(Var.VarName);
				}
			}
		}

		if (ZombieMemberNames.Num())
		{
			auto Response = FMessageDialog::Open( 
				EAppMsgType::OkCancel,
				FText::Format(
					LOCTEXT("RemoveInvalidStructureMemberVariable_Msg", "The following member variables in blueprint '{0}' have invalid type. Would you like to remove them? \n\n{1}"), 
					FText::FromString(Blueprint->GetFullName()),
					FText::FromString(DislpayList)
				));
			check((EAppReturnType::Ok == Response) || (EAppReturnType::Cancel == Response));

			if (EAppReturnType::Ok == Response)
			{				
				Blueprint->Modify();

				for (auto NameIter = ZombieMemberNames.CreateConstIterator(); NameIter; ++NameIter)
				{
					const FName Name = *NameIter;
					Blueprint->NewVariables.RemoveAll(FFindByNameHelper<FBPVariableDescription>(Name)); //TODO: Add RemoveFirst to TArray
					FBlueprintEditorUtils::RemoveVariableNodes(Blueprint, Name);
				}
			}
		}
	}
}

TArray<FStructVariableDescription>& FStructureEditorUtils::GetVarDesc(UUserDefinedStruct* Struct)
{
	check(Struct);
	return CastChecked<UUserDefinedStructEditorData>(Struct->EditorData)->VariablesDescriptions;
}

const TArray<FStructVariableDescription>& FStructureEditorUtils::GetVarDesc(const UUserDefinedStruct* Struct)
{
	check(Struct);
	return CastChecked<const UUserDefinedStructEditorData>(Struct->EditorData)->VariablesDescriptions;
}

FString FStructureEditorUtils::GetTooltip(const UUserDefinedStruct* Struct)
{
	const auto StructEditorData = Struct ? Cast<const UUserDefinedStructEditorData>(Struct->EditorData) : NULL;
	return StructEditorData ? StructEditorData->ToolTip : FString();
}

bool FStructureEditorUtils::ChangeTooltip(UUserDefinedStruct* Struct, const FString& InTooltip)
{
	auto StructEditorData = Struct ? Cast<UUserDefinedStructEditorData>(Struct->EditorData) : NULL;
	if (StructEditorData && (InTooltip != StructEditorData->ToolTip))
	{
		const FScopedTransaction Transaction(LOCTEXT("ChangeTooltip", "Change UDS Tooltip"));
		StructEditorData->Modify();
		StructEditorData->ToolTip = InTooltip;

		Struct->SetMetaData(FBlueprintMetadata::MD_Tooltip, *StructEditorData->ToolTip);

		return true;
	}
	return false;
}

FString FStructureEditorUtils::GetVariableTooltip(const UUserDefinedStruct* Struct, FGuid VarGuid)
{
	const auto VarDesc = GetVarDescByGuid(Struct, VarGuid);
	return VarDesc ? VarDesc->ToolTip : FString();
}

bool FStructureEditorUtils::ChangeVariableTooltip(UUserDefinedStruct* Struct, FGuid VarGuid, const FString& InTooltip)
{
	auto VarDesc = GetVarDescByGuid(Struct, VarGuid);
	if (VarDesc && (InTooltip != VarDesc->ToolTip))
	{
		const FScopedTransaction Transaction(LOCTEXT("ChangeVariableTooltip", "Change UDS Variable Tooltip"));
		ModifyStructData(Struct);
		VarDesc->ToolTip = InTooltip;

		auto Property = FindField<UProperty>(Struct, VarDesc->VarName);
		if (Property)
		{
			Property->SetMetaData(FBlueprintMetadata::MD_Tooltip, *VarDesc->ToolTip);
		}

		return true;
	}
	return false;
}

bool FStructureEditorUtils::ChangeEditableOnBPInstance(UUserDefinedStruct* Struct, FGuid VarGuid, bool bInIsEditable)
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	auto VarDesc = GetVarDescByGuid(Struct, VarGuid);
	const bool bNewDontEditoOnInstance = !bInIsEditable;
	if (VarDesc && (bNewDontEditoOnInstance != VarDesc->bDontEditoOnInstance))
	{
		const FScopedTransaction Transaction(LOCTEXT("ChangeVariableDefaultValue", "Change Variable Default Value"));
		ModifyStructData(Struct);

		VarDesc->bDontEditoOnInstance = bNewDontEditoOnInstance;
		OnStructureChanged(Struct);
		return true;
	}
	return false;
}

void FStructureEditorUtils::ModifyStructData(UUserDefinedStruct* Struct)
{
	UUserDefinedStructEditorData* EditorData = Struct ? Cast<UUserDefinedStructEditorData>(Struct->EditorData) : NULL;
	ensure(EditorData);
	if (EditorData)
	{
		EditorData->Modify();
	}
}

bool FStructureEditorUtils::CanEnable3dWidget(const UUserDefinedStruct* Struct, FGuid VarGuid)
{
	const auto VarDesc = GetVarDescByGuid(Struct, VarGuid);
	const auto PropertyStruct = VarDesc ? Cast<const UStruct>(VarDesc->SubCategoryObject.Get()) : NULL;
	return FEdMode::CanCreateWidgetForStructure(PropertyStruct);
}

bool FStructureEditorUtils::Change3dWidgetEnabled(UUserDefinedStruct* Struct, FGuid VarGuid, bool bIsEnabled)
{
	auto VarDesc = GetVarDescByGuid(Struct, VarGuid);
	const auto PropertyStruct = VarDesc ? Cast<const UStruct>(VarDesc->SubCategoryObject.Get()) : NULL;
	if (FEdMode::CanCreateWidgetForStructure(PropertyStruct) && (VarDesc->bEnable3dWidget != bIsEnabled))
	{
		const FScopedTransaction Transaction(LOCTEXT("ChangeVariableDefaultValue", "Change Variable Default Value"));
		ModifyStructData(Struct);

		VarDesc->bEnable3dWidget = bIsEnabled;
		auto Property = FindField<UProperty>(Struct, VarDesc->VarName);
		if (Property)
		{
			if (VarDesc->bEnable3dWidget)
			{
				Property->SetMetaData(FEdMode::MD_MakeEditWidget, TEXT("true"));
			}
			else
			{
				Property->RemoveMetaData(FEdMode::MD_MakeEditWidget);
			}
		}
		return true;
	}
	return false;
}

bool FStructureEditorUtils::Is3dWidgetEnabled(const UUserDefinedStruct* Struct, FGuid VarGuid)
{
	const auto VarDesc = GetVarDescByGuid(Struct, VarGuid);
	const auto PropertyStruct = VarDesc ? Cast<const UStruct>(VarDesc->SubCategoryObject.Get()) : NULL;
	return VarDesc && VarDesc->bEnable3dWidget && FEdMode::CanCreateWidgetForStructure(PropertyStruct);
}

FGuid FStructureEditorUtils::GetGuidForProperty(const UProperty* Property)
{
	auto UDStruct = Property ? Cast<const UUserDefinedStruct>(Property->GetOwnerStruct()) : NULL;
	auto VarDesc = UDStruct ? GetVarDesc(UDStruct).FindByPredicate(FFindByNameHelper<FStructVariableDescription>(Property->GetFName())) : NULL;
	return VarDesc ? VarDesc->VarGuid : FGuid();
}

UProperty* FStructureEditorUtils::GetPropertyByGuid(const UUserDefinedStruct* Struct, const FGuid VarGuid)
{
	const auto VarDesc = GetVarDescByGuid(Struct, VarGuid);
	return VarDesc ? FindField<UProperty>(Struct, VarDesc->VarName) : NULL;
}

FGuid FStructureEditorUtils::GetGuidFromPropertyName(const FName Name)
{
	return FMemberVariableNameHelper::GetGuidFromName(Name);
}

struct FReinstanceDataTableHelper
{
	// TODO: shell we cache the dependency?
	static TArray<UDataTable*> GetTablesDependentOnStruct(UUserDefinedStruct* Struct)
	{
		TArray<UDataTable*> Result;
		if (Struct)
		{
			TArray<UObject*> DataTables;
			GetObjectsOfClass(UDataTable::StaticClass(), DataTables);
			for (auto DataTableObj : DataTables)
			{
				auto DataTable = Cast<UDataTable>(DataTableObj);
				if (DataTable && (Struct == DataTable->RowStruct))
				{
					Result.Add(DataTable);
				}
			}
		}
		return Result;
	}
};

void FStructureEditorUtils::BroadcastPreChange(UUserDefinedStruct* Struct)
{
	FStructureEditorUtils::FStructEditorManager::Get().PreChange(Struct);
	auto DataTables = FReinstanceDataTableHelper::GetTablesDependentOnStruct(Struct);
	for (auto DataTable : DataTables)
	{
		DataTable->CleanBeforeStructChange();
	}
}

void FStructureEditorUtils::BroadcastPostChange(UUserDefinedStruct* Struct)
{
	FStructureEditorUtils::FStructEditorManager::Get().PostChange(Struct);
	auto DataTables = FReinstanceDataTableHelper::GetTablesDependentOnStruct(Struct);
	for (auto DataTable : DataTables)
	{
		DataTable->RestoreAfterStructChange();
		auto DataTableEditor = static_cast<IDataTableEditor*>(FAssetEditorManager::Get().FindEditorForAsset(DataTable, false));
		if (DataTableEditor)
		{
			DataTableEditor->OnDataTableReloaded();
		}
	}
}

#undef LOCTEXT_NAMESPACE
