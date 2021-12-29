// Fill out your copyright notice in the Description page of Project Settings.


#include "GameSerializerInterface.h"

#include "GameSerializer_Log.h"

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

void IGameSerializerInterface::WhenGamePostLoad(UObject* Obj, const FGameSerializerExtendDataContainer& ExtendData, const FGameSerializerCallRepNotifyFunc& CallRepNotifyFunc)
{
	if (Obj->Implements<UGameSerializerInterface>())
	{
		FEditorScriptExecutionGuard EditorScriptExecutionGuard;
		Execute_WhenGamePostLoad(Obj, ExtendData, CallRepNotifyFunc);
	}
	else
	{
		UGameSerializerExtendDataFunctionLibrary::DefaultPostLoadGame(Obj, ExtendData, CallRepNotifyFunc);
	}
}

void IComponentGameSerializerInterface::WhenGameInit_Implementation()
{

}

void IGameSerializerInterface::WhenGamePostLoad_Implementation(const FGameSerializerExtendDataContainer& ExtendData, const FGameSerializerCallRepNotifyFunc& CallRepNotifyFunc)
{
	DefaultWhenGamePostLoad(ExtendData, CallRepNotifyFunc);
}

void IGameSerializerInterface::DefaultWhenGamePostLoad(const FGameSerializerExtendDataContainer& ExtendData, const FGameSerializerCallRepNotifyFunc& CallRepNotifyFunc)
{
	UObject* Self = CastChecked<UObject>(this);
	UGameSerializerExtendDataFunctionLibrary::DefaultPostLoadGame(Self, ExtendData, CallRepNotifyFunc);
}

void IComponentGameSerializerInterface::WhenGameInit(UActorComponent* ActorComponent)
{
	FEditorScriptExecutionGuard EditorScriptExecutionGuard;
	Execute_WhenGameInit(ActorComponent);
}

void IActorGameSerializerInterface::WhenGameInit_Implementation()
{

}

void IActorGameSerializerInterface::WhenGameInit(AActor* Actor)
{
	FEditorScriptExecutionGuard EditorScriptExecutionGuard;
	Execute_WhenGameInit(Actor);
	for (UActorComponent* Component : Actor->GetComponents())
	{
		if (Component->Implements<UComponentGameSerializerInterface>())
		{
			IComponentGameSerializerInterface::WhenGameInit(Component);
		}
	}
}

bool IActorGameSerializerInterface::CanGameSerializedInLevel_Implementation() const
{
	const UObject* Self = CastChecked<UObject>(this);
	return Self->HasAnyFlags(RF_Transient) == false;
}

AActor* IActorGameSerializerInterface::GetGameSerializedOwner_Implementation() const
{
	return CastChecked<AActor>(this)->GetOwner();
}

AActor* IActorGameSerializerInterface::GetGameSerializedOwner(const AActor* Actor)
{
	if (Actor->Implements<UActorGameSerializerInterface>())
	{
		FEditorScriptExecutionGuard EditorScriptExecutionGuard;
		return Execute_GetGameSerializedOwner(Actor);
	}
	return Actor->GetOwner();
}

void IActorGameSerializerInterface::SetGameSerializedOwner_Implementation(AActor* GameSerializedOwner)
{
	CastChecked<AActor>(this)->SetOwner(GameSerializedOwner);
}

void IActorGameSerializerInterface::SetGameSerializedOwner(AActor* Actor, AActor* GameSerializedOwner)
{
	if (Actor->Implements<UActorGameSerializerInterface>())
	{
		FEditorScriptExecutionGuard EditorScriptExecutionGuard;
		Execute_SetGameSerializedOwner(Actor, GameSerializedOwner);
	}
	Actor->SetOwner(GameSerializedOwner);
}
