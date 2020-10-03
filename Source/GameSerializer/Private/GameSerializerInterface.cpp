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
