// Fill out your copyright notice in the Description page of Project Settings.


#include "GameSerializerExtendData.h"

#include "GameSerializerInterface.h"


namespace GameSerializerExtendDataFactory
{
	TMap<UClass*, TSharedRef<FGameSerializerExtendDataFactory>> Factory;

	FGameSerializerExtendDataFactory* FindFactory(UObject* Instance)
	{
		for (UClass* Class = Instance->GetClass(); Class; Class = Class->GetSuperClass())
		{
			if (TSharedRef<FGameSerializerExtendDataFactory>* FindFactory = Factory.Find(Class))
			{
				return &FindFactory->Get();
			}
		}
		return nullptr;
	}
}

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

FGameSerializerExtendDataContainer UGameSerializerExtendDataFunctionLibrary::DefaultPreGameSave(UObject* Instance)
{
	if (FGameSerializerExtendDataFactory* Factory = GameSerializerExtendDataFactory::FindFactory(Instance))
	{
		return Factory->WhenGamePreSave(Instance);
	}
	return FGameSerializerExtendDataContainer();
}

void UGameSerializerExtendDataFunctionLibrary::DefaultPostLoadGame(UObject* Instance, const FGameSerializerExtendDataContainer& ExtendData, const FGameSerializerCallRepNotifyFunc& CallRepNotifyFunc)
{
	if (ExtendData.IsValid())
	{
		if (FGameSerializerExtendDataFactory* Factory = GameSerializerExtendDataFactory::FindFactory(Instance))
		{
			Factory->WhenGamePostLoad(Instance, ExtendData, CallRepNotifyFunc);
		}
	}
}

void FActorGameSerializerExtendData::SaveData(const AActor* Actor)
{
	Instigator = Actor->GetInstigator();
}

void FActorGameSerializerExtendData::LoadData(AActor* Actor) const
{
	Actor->SetInstigator(Instigator);
}

FGameSerializerExtendDataContainer FActorGameSerializerExtendDataFactory::WhenGamePreSave(UObject* Instance)
{
	AActor* Actor = CastChecked<AActor>(Instance);
	auto ExtendData = IGameSerializerInterface::MakeSerializerExtendData<FActorGameSerializerExtendData>();
	ExtendData->SaveData(Actor);
	return ExtendData;
}

void FActorGameSerializerExtendDataFactory::WhenGamePostLoad(UObject* Instance, const FGameSerializerExtendDataContainer& ExtendData, const FGameSerializerCallRepNotifyFunc& CallRepNotifyFunc)
{
	AActor* Actor = CastChecked<AActor>(Instance);
	const FActorGameSerializerExtendData& ActorExtendData = ExtendData.Get<FActorGameSerializerExtendData>();
	ActorExtendData.LoadData(Actor);
	CallRepNotifyFunc.CallRepNotifyFunc();
}
