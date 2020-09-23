// Fill out your copyright notice in the Description page of Project Settings.


#include "GameSerializerManager.h"
#include <PlatformFeatures.h>
#include <SaveGameSystem.h>

#include "GameSerializer_Log.h"
#include "GameSerializerCore.h"
#include "GameSerializerInterface.h"


void UGameSerializerLevelComponent::OnUnregister()
{
	Super::OnUnregister();

	OnLevelRemoved.Execute(GetOwner()->GetLevel());
}

UGameSerializerManager::UGameSerializerManager()
	: bInvokeLoadGame(true)
	, bShouldInitSpawnActor(true)
{
	
}

void UGameSerializerManager::Initialize(FSubsystemCollectionBase& Collection)
{
	UWorld* World = GetWorld();
	if (World->IsServer() == false)
	{
		return;
	}

	LoadOrInitWorld(World);
	OnPostWorldInitialization_DelegateHandle = FWorldDelegates::OnPostWorldInitialization.AddWeakLambda(this, [this](UWorld* World, const UWorld::InitializationValues IVS)
	{
		LoadOrInitWorld(World);
	});

	OnLevelAdd_DelegateHandle = FWorldDelegates::LevelAddedToWorld.AddWeakLambda(this, [this](ULevel* Level, UWorld* World)
	{
		//由于绑的是全局回调，PIE内没法分辨是不是为Server，在这里再做判断
		if (World->IsServer() == false)
		{
			return;
		}

		LoadOrInitLevel(Level);
	});
}

void UGameSerializerManager::Deinitialize()
{
	FWorldDelegates::LevelAddedToWorld.Remove(OnLevelAdd_DelegateHandle);
	FWorldDelegates::OnPostWorldInitialization.Remove(OnPostWorldInitialization_DelegateHandle);
}

TOptional<TSharedRef<FJsonObject>> UGameSerializerManager::TryLoadJsonObject(const FName& FileName)
{
	ISaveGameSystem* SaveSystem = IPlatformFeaturesModule::Get().GetSaveGameSystem();
	if (ensure(SaveSystem))
	{
		TArray<uint8> BinaryArray;
		if (SaveSystem->LoadGame(false, *FileName.ToString(), UserIndex, BinaryArray))
		{
			BinaryArray.Add(0);
			const FString JsonString = FString(UTF8_TO_TCHAR(BinaryArray.GetData()));
			const TSharedPtr<FJsonObject> JsonObject = GameSerializerCore::StringToJsonObject(JsonString);
			if (ensure(JsonObject.IsValid()))
			{
				return JsonObject.ToSharedRef();
			}
		}
	}
	return {};
}

void UGameSerializerManager::SaveJsonObject(const FName& FileName, const TSharedRef<FJsonObject>& JsonObject)
{
	ISaveGameSystem* SaveSystem = IPlatformFeaturesModule::Get().GetSaveGameSystem();
	if (ensure(SaveSystem))
	{
		const FString JsonString = GameSerializerCore::JsonObjectToString(JsonObject);

		TArray<uint8> BinaryArray;
		const FTCHARToUTF8 UTF8String(*JsonString);
		const int32 DataSize = UTF8String.Length();
		BinaryArray.SetNumUninitialized(DataSize);
		FMemory::Memcpy(BinaryArray.GetData(), UTF8String.Get(), DataSize);
		
		SaveSystem->SaveGame(false, *FileName.ToString(), UserIndex, BinaryArray);
	}
}

void UGameSerializerManager::InitActorAndComponents(AActor* Actor)
{
	if (bShouldInitSpawnActor && Actor->Implements<UActorGameSerializerInterface>())
	{
		IActorGameSerializerInterface::WhenGameInit(Actor);
		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (Component->Implements<UComponentGameSerializerInterface>())
			{
				IComponentGameSerializerInterface::WhenGameInit(Component);
			}
		}
	}
}

void UGameSerializerManager::LoadOrInitLevel(ULevel* Level)
{
	AWorldSettings* WorldSettings = CastChecked<UWorld>(Level->GetOuter())->GetWorldSettings();
	if (ensure(WorldSettings))
	{
		UGameSerializerLevelComponent* LevelComponent = NewObject<UGameSerializerLevelComponent>(WorldSettings);
		WorldSettings->AddOwnedComponent(LevelComponent);
		LevelComponent->RegisterComponent();

		LevelComponent->OnLevelRemoved.BindWeakLambda(this, [this](ULevel* RemovedLevel)
		{
			const FString LevelName = RemovedLevel->GetOuter()->GetName();
			UE_LOG(GameSerializer_Log, Display, TEXT("保存关卡[%s]"), *LevelName);
			
			TArray<UObject*> SerializeList;
			SerializeList.Reset(RemovedLevel->Actors.Num());
			for (AActor* Actor : RemovedLevel->Actors)
			{
				if (IsValid(Actor) && Actor->Implements<UActorGameSerializerInterface>())
				{
					if (IActorGameSerializerInterface::CanGameSerialized(Actor))
					{
						SerializeList.Add(Actor);
					}
				}
			}

			GameSerializerCore::FStructToJson StructToJson;
			StructToJson.AddObjects(TEXT("LevelActors"), SerializeList);

			const TSharedRef<FJsonObject> JsonObject = StructToJson.GetResultJson();
			SaveJsonObject(*LevelName, JsonObject);
		});
	}

	const FString LevelName = Level->GetOuter()->GetName();
	if (bInvokeLoadGame)
	{
		TOptional<TSharedRef<FJsonObject>> JsonObject = TryLoadJsonObject(*LevelName);
		if (JsonObject.IsSet())
		{
			UE_LOG(GameSerializer_Log, Display, TEXT("加载关卡[%s]"), *LevelName);
			
			TArray<AActor*> PrepareLoadActors;
			for (AActor* Actor : Level->Actors)
			{
				if (ensure(Actor) && Actor->Implements<UActorGameSerializerInterface>())
				{
					if (IActorGameSerializerInterface::CanGameSerialized(Actor))
					{
						PrepareLoadActors.Add(Actor);
					}
				}
			}

			GameSerializerCore::FJsonToStruct JsonToStruct(Level, JsonObject.GetValue());

			const TArray<UObject*> LoadedActors = JsonToStruct.GetObjects(TEXT("LevelActors"));
			for (AActor* Actor : PrepareLoadActors)
			{
				if (IsValid(Actor))
				{
					if (LoadedActors.Contains(Actor) == false)
					{
						Actor->Destroy();
					}
				}
			}
			return;
		}
	}

	UE_LOG(GameSerializer_Log, Display, TEXT("初始化关卡[%s]"), *LevelName);
	for (int32 Idx = 0; Idx < Level->Actors.Num(); ++Idx)
	{
		AActor* Actor = Level->Actors[Idx];
		if (ensure(IsValid(Actor)))
		{
			InitActorAndComponents(Actor);
		}
	}
}

void UGameSerializerManager::LoadOrInitWorld(UWorld* World)
{
	UE_LOG(GameSerializer_Log, Display, TEXT("世界[%s]启动游戏序列化系统"), *World->GetName());

	for (ULevel* Level : World->GetLevels())
	{
		LoadOrInitLevel(Level);
	}

	World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &UGameSerializerManager::InitActorAndComponents));
}
