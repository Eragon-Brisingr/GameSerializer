// Fill out your copyright notice in the Description page of Project Settings.


#include "GameSerializerInterface.h"

#include "GameSerializer_Log.h"

#if !UE_BUILD_SHIPPING
namespace GameSerializer
{
	bool bIsCustomGameInitFunction = false;
}
#endif

FGameSerializerExtendDataContainer IGameSerializerInterface::WhenGamePreSave_Implementation()
{
	return FGameSerializerExtendDataContainer();
}

FGameSerializerExtendDataContainer IGameSerializerInterface::WhenGamePreSave(UObject* Obj)
{
	if (Obj->Implements<UGameSerializerInterface>())
	{
		FEditorScriptExecutionGuard EditorScriptExecutionGuard;
		FGameSerializerExtendDataContainer ExtendDataContainer = Execute_WhenGamePreSave(Obj);
		if (ExtendDataContainer.IsValid())
		{
			return ExtendDataContainer;
		}
	}
	return UGameSerializerExtendDataFunctionLibrary::DefaultPreGameSave(Obj);
}

void IGameSerializerInterface::WhenGamePostLoad(UObject* Obj, const FGameSerializerExtendDataContainer& ExtendData)
{
	if (Obj->Implements<UGameSerializerInterface>())
	{
		FEditorScriptExecutionGuard EditorScriptExecutionGuard;
		Execute_WhenGamePostLoad(Obj, ExtendData);
	}
	else
	{
		UGameSerializerExtendDataFunctionLibrary::DefaultPostLoadGame(Obj, ExtendData);
	}
}

void IComponentGameSerializerInterface::WhenGameInit_Implementation()
{
#if !UE_BUILD_SHIPPING
	GameSerializer::bIsCustomGameInitFunction = false;
#endif
}

void IGameSerializerInterface::WhenGamePostLoad_Implementation(const FGameSerializerExtendDataContainer& ExtendData)
{
	DefaultWhenGamePostLoad(ExtendData);
}

void IGameSerializerInterface::DefaultWhenGamePostLoad(const FGameSerializerExtendDataContainer& ExtendData)
{
	UObject* Self = CastChecked<UObject>(this);
	UGameSerializerExtendDataFunctionLibrary::DefaultPostLoadGame(Self, ExtendData);
}

void IComponentGameSerializerInterface::WhenGameInit(UObject* Obj)
{
#if !UE_BUILD_SHIPPING
	using namespace GameSerializer;
	TGuardValue<bool> IsCustomGameInitFunctionGuardValue(bIsCustomGameInitFunction, true);
#endif
	FEditorScriptExecutionGuard EditorScriptExecutionGuard;
	Execute_WhenGameInit(Obj);
#if !UE_BUILD_SHIPPING
	if (bIsCustomGameInitFunction)
	{
		UE_LOG(GameSerializer_Log, Display, TEXT("  %s 执行初始化"), *Obj->GetName());
	}
#endif
}

void IActorGameSerializerInterface::WhenGameInit_Implementation()
{
#if !UE_BUILD_SHIPPING
	GameSerializer::bIsCustomGameInitFunction = false;
#endif
}

void IActorGameSerializerInterface::WhenGameInit(UObject* Obj)
{
#if !UE_BUILD_SHIPPING
	using namespace GameSerializer;
	TGuardValue<bool> IsCustomGameInitFunctionGuardValue(bIsCustomGameInitFunction, true);
#endif
	FEditorScriptExecutionGuard EditorScriptExecutionGuard;
	Execute_WhenGameInit(Obj);
#if !UE_BUILD_SHIPPING
	if (bIsCustomGameInitFunction)
	{
		UE_LOG(GameSerializer_Log, Display, TEXT("%s 执行初始化"), *Obj->GetName());
	}
#endif
}
