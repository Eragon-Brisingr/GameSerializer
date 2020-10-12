// Fill out your copyright notice in the Description page of Project Settings.


#include "GameSerializerInterface.h"

#include "GameSerializer_Log.h"

void FGameSerializerCallRepNotifyFunc::CallRepNotifyFunc() const
{
#if DO_CHECK
	ensureAlwaysMsgf(bCalled == false, TEXT("[%s] FGameSerializerCallRepNotifyFunc::CallRepNotifyFunc only can call once."), *InstancedObject->GetName());
	bCalled = true;
#endif
	
	UClass* Class = InstancedObject->GetClass();
	const UObject* CDO = Class->GetDefaultObject();

	TGuardValue<bool> IsGameSerializerCallGuard(bIsGameSerializerCall, true);
	
	for (const FGameSerializerNetNotifyData& NetNotifyData : *NetNotifyDatas)
	{
		UFunction* RepNotifyFunc = NetNotifyData.RepNotifyFunc;

		FMemMark Mark(FMemStack::Get());
		uint8* Params = new(FMemStack::Get(), MEM_Zeroed, RepNotifyFunc->ParmsSize)uint8;

		if (RepNotifyFunc->NumParms == 1)
		{
			FField* FirstParam = RepNotifyFunc->ChildProperties;
			check(FirstParam->GetClass() == FirstParam->GetClass());

			TFieldIterator<FProperty> Itr(RepNotifyFunc);
			Itr->CopyCompleteValue(Itr->ContainerPtrToValuePtr<void>(Params), NetNotifyData.Property->ContainerPtrToValuePtr<void>(CDO));
		}

		InstancedObject->ProcessEvent(RepNotifyFunc, Params);

		Mark.Pop();
	}
}

bool FGameSerializerCallRepNotifyFunc::bIsGameSerializerCall = false;
bool FGameSerializerCallRepNotifyFunc::IsGameSerializerCall()
{
	return bIsGameSerializerCall;
}

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
		UGameSerializerExtendDataFunctionLibrary::DefaultPostLoadGame(Obj, ExtendData);
		CallRepNotifyFunc.CallRepNotifyFunc();
	}
}

void IComponentGameSerializerInterface::WhenGameInit_Implementation()
{

}

void IGameSerializerInterface::WhenGamePostLoad_Implementation(const FGameSerializerExtendDataContainer& ExtendData, const FGameSerializerCallRepNotifyFunc& CallRepNotifyFunc)
{
	DefaultWhenGamePostLoad(ExtendData);
	CallRepNotifyFunc.CallRepNotifyFunc();
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
