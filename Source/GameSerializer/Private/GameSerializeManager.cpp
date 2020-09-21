// Fill out your copyright notice in the Description page of Project Settings.


#include "GameSerializeManager.h"

#include "GameSerializer_Log.h"

bool UGameSerializeManager::bInvokeLoadGame = false;

void UGameSerializeManager::Initialize(FSubsystemCollectionBase& Collection)
{
	UWorld* World = GetWorld();

	UE_LOG(GameSerializer_Log, Display, TEXT("---------------------------------------------初始化主世界[%s]-------------------------------------------------------"), *World->GetName());

	//for (ULevel* Level : World->GetLevels())
	//{
	//	LoadLevelOrInitLevel(Level, false);
	//}

	//OnLevelAdd_DelegateHandle = FWorldDelegates::LevelAddedToWorld.AddWeakLambda(this, [this](ULevel* Level, UWorld* World)
	//{
	//	//由于绑的是全局回调，PIE内没法分辨是不是为Server，在这里再做判断
	//	if (World->IsServer() == false)
	//	{
	//		return;
	//	}

	//	LoadLevelOrInitLevel(Level, true);
	//});

	//World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateWeakLambda(this, [this](AActor* Actor)
	//{
	//	if (bShouldInitSpawnActor && Actor->Implements<UXD_SaveGameInterface>())
	//	{
	//		NotifyActorAndComponentInit(Actor);
	//	}
	//}));
}

void UGameSerializeManager::Deinitialize()
{
	FWorldDelegates::LevelAddedToWorld.Remove(OnLevelAdd_DelegateHandle);
}
