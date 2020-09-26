// Fill out your copyright notice in the Description page of Project Settings.


#include "GameSerializerExtendData.h"


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

FGameSerializerExtendDataContainer UGameSerializerExtendDataFunctionLibrary::DefaultPreGameSave(UObject* Instance)
{
	if (FGameSerializerExtendDataFactory* Factory = GameSerializerExtendDataFactory::FindFactory(Instance))
	{
		return Factory->WhenGamePreSave(Instance);
	}
	return FGameSerializerExtendDataContainer();
}

void UGameSerializerExtendDataFunctionLibrary::DefaultPostLoadGame(UObject* Instance, const FGameSerializerExtendDataContainer& ExtendData)
{
	if (ExtendData.IsValid())
	{
		if (FGameSerializerExtendDataFactory* Factory = GameSerializerExtendDataFactory::FindFactory(Instance))
		{
			Factory->WhenGamePostLoad(Instance, ExtendData);
		}
	}
}

FGameSerializerExtendDataContainer FActorGameSerializerExtendDataFactory::WhenGamePreSave(UObject* Instance)
{
	AActor* Actor = CastChecked<AActor>(Instance);
	FActorGameSerializerExtendData ExtendData;
	return FGameSerializerExtendDataContainer::Make(ExtendData);
}

void FActorGameSerializerExtendDataFactory::WhenGamePostLoad(UObject* Instance, const FGameSerializerExtendDataContainer& ExtendData)
{
	AActor* Actor = CastChecked<AActor>(Instance);
	const FActorGameSerializerExtendData& ActorExtendData = ExtendData.Get<FActorGameSerializerExtendData>();
}
