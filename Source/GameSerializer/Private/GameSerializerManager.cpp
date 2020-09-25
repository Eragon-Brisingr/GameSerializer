// Fill out your copyright notice in the Description page of Project Settings.


#include "GameSerializerManager.h"
#include <PlatformFeatures.h>
#include <SaveGameSystem.h>
#include <GameFramework/PlayerState.h>
#include <GameFramework/GameModeBase.h>
#include <Engine/LevelStreaming.h>

#include "GameSerializer_Log.h"
#include "GameSerializerCore.h"
#include "GameSerializerInterface.h"

namespace JsonFieldName
{
	constexpr TCHAR LevelActors[] = TEXT("LevelActors");
	constexpr TCHAR WorldOrigin[] = TEXT("WorldOrigin");

	constexpr TCHAR PlayerPawn[] = TEXT("Pawn");
	constexpr TCHAR PlayerState[] = TEXT("PlayerState");
	constexpr TCHAR PlayerController[] = TEXT("PlayerController");
}

static UScriptStruct* StaticGetBaseStructureInternal(FName Name)
{
	static UPackage* CoreUObjectPkg = FindObjectChecked<UPackage>(nullptr, TEXT("/Script/CoreUObject"));

	UScriptStruct* Result = (UScriptStruct*)StaticFindObjectFast(UScriptStruct::StaticClass(), CoreUObjectPkg, Name, false, false, RF_NoFlags, EInternalObjectFlags::None);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (!Result)
	{
		UE_LOG(LogClass, Fatal, TEXT("Failed to find native struct '%s.%s'"), *CoreUObjectPkg->GetName(), *Name.ToString());
	}
#endif
	return Result;
}

template<>
struct TBaseStructure<FIntVector>
{
	static UScriptStruct* Get()
	{
		static UScriptStruct* Struct = StaticGetBaseStructureInternal(TEXT("IntVector"));
		return Struct;
	}
};

void UGameSerializerLevelComponent::OnUnregister()
{
	Super::OnUnregister();

	OnLevelRemoved.Execute(GetOwner()->GetLevel());
}

UGameSerializerLevelStreamingLambda::UGameSerializerLevelStreamingLambda()
{
	
}

void UGameSerializerLevelStreamingLambda::WhenLevelLoaded()
{
	OnLevelLoaded.Execute(CastChecked<ULevelStreaming>(GetOuter())->GetLoadedLevel());
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

TOptional<TSharedRef<FJsonObject>> UGameSerializerManager::TryLoadJsonObject(const FString& Category, const FString& FileName)
{
	ISaveGameSystem* SaveSystem = IPlatformFeaturesModule::Get().GetSaveGameSystem();
	if (ensure(SaveSystem))
	{
		TArray<uint8> BinaryArray;
		if (SaveSystem->LoadGame(false, *FPaths::Combine(Category, FileName), UserIndex, BinaryArray))
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

void UGameSerializerManager::SaveJsonObject(const TSharedRef<FJsonObject>& JsonObject, const FString& Category, const FString& FileName)
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
		
		SaveSystem->SaveGame(false, *FPaths::Combine(Category, FileName), UserIndex, BinaryArray);
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

struct FLevelDeserializer : public GameSerializerCore::FJsonToStruct
{
	using Super = FJsonToStruct;

	FLevelDeserializer(ULevel* Level, const TSharedRef<FJsonObject>& RootJsonObject)
		: Super(Level, RootJsonObject)
	{
		CheckFlags = CPF_SaveGame;
		OldWorldOffset = GetStruct<FIntVector>(JsonFieldName::WorldOrigin);
	}

	FIntVector OldWorldOffset;
};

DECLARE_CYCLE_STAT(TEXT("GameSerializerManager_LoadLevel"), STAT_GameSerializerManage_LoadLevel, STATGROUP_GameSerializer);
DECLARE_CYCLE_STAT(TEXT("GameSerializerManager_InitLevel"), STAT_GameSerializerManage_InitLevel, STATGROUP_GameSerializer);
DECLARE_CYCLE_STAT(TEXT("GameSerializerManager_LoadStreamLevelEnd"), STAT_GameSerializerManager_LoadStreamLevelEnd, STATGROUP_GameSerializer);
void UGameSerializerManager::LoadOrInitLevel(ULevel* Level)
{
	const FString LevelName = Level->GetOuter()->GetName();
	AWorldSettings* WorldSettings = CastChecked<UWorld>(Level->GetOuter())->GetWorldSettings();
	if (ensure(WorldSettings))
	{
		UGameSerializerLevelComponent* LevelComponent = NewObject<UGameSerializerLevelComponent>(WorldSettings);
		WorldSettings->AddOwnedComponent(LevelComponent);
		LevelComponent->RegisterComponent();

		LevelComponent->OnLevelRemoved.BindUObject(this, &UGameSerializerManager::SerializeLevel);
	}

	if (TSharedRef<FLevelDeserializer>* StreamLoadedLevelDeserializerPtr = StreamLoadedLevelDataMap.Find(Level))
	{
		GameSerializerStatLog(STAT_GameSerializerManage_LoadLevel);
		
		UE_LOG(GameSerializer_Log, Display, TEXT("完成流式关卡[%s]加载"), *LevelName);
		
		FLevelDeserializer& FLevelDeserializer = StreamLoadedLevelDeserializerPtr->Get();
		FLevelDeserializer.LoadDynamicObjectExtendData();

		StreamLoadedLevelDataMap.Remove(Level);
		return;
	}
	
	if (bInvokeLoadGame)
	{
		TOptional<TSharedRef<FJsonObject>> JsonObject = TryLoadJsonObject(TEXT("Levels"), *LevelName);
		if (JsonObject.IsSet())
		{
			FGuardValue_Bitfield(bShouldInitSpawnActor, false);

			GameSerializerStatLog(STAT_GameSerializerManager_LoadStreamLevelEnd);
			
			UE_LOG(GameSerializer_Log, Display, TEXT("加载关卡[%s]"), *LevelName);
			
			TArray<AActor*> PrepareLoadActors;
			for (AActor* Actor : Level->Actors)
			{
				if (Actor && Actor->Implements<UActorGameSerializerInterface>())
				{
					if (IActorGameSerializerInterface::CanGameSerialized(Actor))
					{
						PrepareLoadActors.Add(Actor);
					}
				}
			}

			FLevelDeserializer LevelDeserializer(Level, JsonObject.GetValue());
			const FIntVector OldWorldOrigin = LevelDeserializer.GetStruct<FIntVector>(JsonFieldName::WorldOrigin);
			TGuardValue<FIntVector> WorldOffsetGuard(FActorGameSerializerExtendData::WorldOffset, OldWorldOrigin - Level->GetWorld()->OriginLocation);
			LevelDeserializer.LoadAllData();
			const TArray<UObject*> LoadedActors = LevelDeserializer.GetObjects(JsonFieldName::LevelActors);
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

	{
		GameSerializerStatLog(STAT_GameSerializerManage_InitLevel);
		
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
}

DECLARE_CYCLE_STAT(TEXT("GameSerializerManager_LoadStreamLevelStart"), STAT_GameSerializerManager_LoadStreamLevelStart, STATGROUP_GameSerializer);
DECLARE_CYCLE_STAT(TEXT("GameSerializerManager_LoadOrInitWorld"), STAT_GameSerializerManage_LoadOrInitWorld, STATGROUP_GameSerializer);
void UGameSerializerManager::LoadOrInitWorld(UWorld* World)
{
	GameSerializerStatLog(STAT_GameSerializerManage_LoadOrInitWorld);
	
	UE_LOG(GameSerializer_Log, Display, TEXT("世界[%s]启动游戏序列化系统"), *World->GetName());

	const TArray<ULevelStreaming*>& StreamingLevels = World->GetStreamingLevels();
	CachedLevelStreamingLambdas.Reset(StreamingLevels.Num());
	for (ULevelStreaming* LevelStreaming : StreamingLevels)
	{
		if (ensure(LevelStreaming))
		{
			UGameSerializerLevelStreamingLambda* LevelStreamingLambda = NewObject<UGameSerializerLevelStreamingLambda>(LevelStreaming);
			CachedLevelStreamingLambdas.Add(LevelStreamingLambda);
			LevelStreaming->OnLevelLoaded.AddDynamic(LevelStreamingLambda, &UGameSerializerLevelStreamingLambda::WhenLevelLoaded);
			LevelStreamingLambda->OnLevelLoaded.BindWeakLambda(this, [this](ULevel* LoadedLevel)
			{
				const FString LevelName = LoadedLevel->GetOuter()->GetName();
				if (bInvokeLoadGame)
				{
					TOptional<TSharedRef<FJsonObject>> JsonObject = TryLoadJsonObject(TEXT("Levels"), *LevelName);
					if (JsonObject.IsSet())
					{
						FGuardValue_Bitfield(bShouldInitSpawnActor, false);

						GameSerializerStatLog(STAT_GameSerializerManager_LoadStreamLevelStart);

						UE_LOG(GameSerializer_Log, Display, TEXT("加载流式关卡[%s]"), *LoadedLevel->GetOuter()->GetName());

						TArray<AActor*> PrepareLoadActors;
						for (AActor* Actor : LoadedLevel->Actors)
						{
							if (Actor && Actor->Implements<UActorGameSerializerInterface>())
							{
								if (IActorGameSerializerInterface::CanGameSerialized(Actor))
								{
									PrepareLoadActors.Add(Actor);
								}
							}
						}

						TSharedRef<FLevelDeserializer> LevelDeserializer = MakeShared<FLevelDeserializer>(LoadedLevel, JsonObject.GetValue());
						LevelDeserializer->LoadExternalObject();
						LevelDeserializer->InstanceDynamicObject();
						LevelDeserializer->DynamicActorExecuteConstruction();
						LevelDeserializer->LoadDynamicObjectJsonData();
						
						const TArray<UObject*> LoadedActors = LevelDeserializer->GetObjects(JsonFieldName::LevelActors);
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
						StreamLoadedLevelDataMap.Add(LoadedLevel, LevelDeserializer);
					}
				}
			});
		}
	}

	for (ULevel* Level : World->GetLevels())
	{
		LoadOrInitLevel(Level);
	}

	World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &UGameSerializerManager::InitActorAndComponents));
}

void UGameSerializerManager::SerializeLevel(ULevel* Level)
{
	const FString LevelName = Level->GetOuter()->GetName();
	UE_LOG(GameSerializer_Log, Display, TEXT("保存关卡[%s]"), *LevelName);

	TArray<UObject*> SerializeList;
	SerializeList.Reset(Level->Actors.Num());
	for (AActor* Actor : Level->Actors)
	{
		if (IsValid(Actor) && Actor->Implements<UActorGameSerializerInterface>())
		{
			if (IActorGameSerializerInterface::CanGameSerialized(Actor))
			{
				SerializeList.Add(Actor);
			}
		}
	}

	struct FLevelSerializer : public GameSerializerCore::FStructToJson
	{
		FLevelSerializer()
		{
			CheckFlags = CPF_SaveGame;
		}
	};

	FLevelSerializer LevelSerializer;
	LevelSerializer.AddStruct(JsonFieldName::WorldOrigin, Level->GetWorld()->OriginLocation);
	LevelSerializer.AddObjects(JsonFieldName::LevelActors, SerializeList);

	const TSharedRef<FJsonObject> JsonObject = LevelSerializer.GetResultJson();
	SaveJsonObject(JsonObject, TEXT("Levels"), *LevelName);
}

APawn* UGameSerializerManager::LoadOrSpawnDefaultPawn(AGameModeBase* GameMode, AController* NewPlayer, const FTransform& SpawnTransform)
{
	UWorld* World = GameMode->GetWorld();
	check(World);

	APlayerState* PlayerState = NewPlayer->GetPlayerState<APlayerState>();
	check(PlayerState);

	const FString PlayerName = PlayerState->GetPlayerName();

	UE_LOG(GameSerializer_Log, Display, TEXT("玩家[%s]启动游戏序列化系统"), *PlayerName);
	
	FActorSpawnParameters SpawnInfo;
	SpawnInfo.Name = *PlayerName;
	SpawnInfo.Instigator = GameMode->GetInstigator();
	SpawnInfo.ObjectFlags |= RF_Transient;	// We never want to save default player pawns into a map
	UClass* PawnClass = GameMode->GetDefaultPawnClassForController(NewPlayer);
	APawn* Pawn = World->SpawnActor<APawn>(PawnClass, SpawnTransform, SpawnInfo);

	APlayerController* PlayerController = Cast<APlayerController>(NewPlayer);
	if (ensure(PlayerController))
	{
		FPlayerData PlayerData;
		PlayerData.PlayerController = PlayerController;
		PlayerData.PlayerState = PlayerState;
		PlayerDataMap.Add(Pawn, PlayerData);
		Pawn->OnEndPlay.AddDynamic(this, &UGameSerializerManager::OnPlayerPawnEndPlay);
	}
	
	if (bInvokeLoadGame)
	{
		TOptional<TSharedRef<FJsonObject>> JsonObject = TryLoadJsonObject(TEXT("Players"), *PlayerName);
		if (JsonObject.IsSet())
		{
			const TSharedRef<FJsonObject> RootJsonObject = JsonObject.GetValue();

			struct FPlayerDeserializer : public GameSerializerCore::FJsonToStruct
			{
				using Super = FJsonToStruct;

				FPlayerDeserializer(ULevel* Level, const TSharedRef<FJsonObject>& RootJsonObject)
					: Super(Level, RootJsonObject)
				{
					CheckFlags = CPF_SaveGame;
				}
			};
			FPlayerDeserializer PlayerDeserializer(World->PersistentLevel, RootJsonObject);

			PlayerDeserializer.RetargetDynamicObjectName(JsonFieldName::PlayerController, PlayerController->GetFName());
			PlayerDeserializer.RetargetDynamicObjectName(JsonFieldName::PlayerState, PlayerState->GetFName());
			PlayerDeserializer.RetargetDynamicObjectName(JsonFieldName::PlayerPawn, Pawn->GetFName());
			
			const FIntVector OldWorldOrigin = PlayerDeserializer.GetStruct<FIntVector>(JsonFieldName::WorldOrigin);
			TGuardValue<FIntVector> WorldOffsetGuard(FActorGameSerializerExtendData::WorldOffset, OldWorldOrigin - Pawn->GetWorld()->OriginLocation);
			PlayerDeserializer.LoadAllData();
		}
	}
	
	return Pawn;
}

void UGameSerializerManager::OnPlayerPawnEndPlay(AActor* PawnActor, EEndPlayReason::Type EndPlayReason)
{
	APawn* Pawn = CastChecked<APawn>(PawnActor);
	const FPlayerData PlayerData = PlayerDataMap.FindAndRemoveChecked(Pawn);

	struct FPlayerSerializer : public GameSerializerCore::FStructToJson
	{
		FPlayerSerializer()
		{
			CheckFlags = CPF_SaveGame;
		}
	};
	FPlayerSerializer PlayerSerializer;
	PlayerSerializer.AddStruct(JsonFieldName::WorldOrigin, Pawn->GetWorld()->OriginLocation);
	PlayerSerializer.AddObject(JsonFieldName::PlayerController, PlayerData.PlayerController.Get(true));
	PlayerSerializer.AddObject(JsonFieldName::PlayerState, PlayerData.PlayerState.Get(true));
	PlayerSerializer.AddObject(JsonFieldName::PlayerPawn, Pawn);

	SaveJsonObject(PlayerSerializer.GetResultJson(), TEXT("Players"), *Pawn->GetName());
}
