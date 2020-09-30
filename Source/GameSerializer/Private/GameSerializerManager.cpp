// Fill out your copyright notice in the Description page of Project Settings.


#include "GameSerializerManager.h"
#include <PlatformFeatures.h>
#include <SaveGameSystem.h>
#include <GameFramework/PlayerState.h>
#include <GameFramework/GameModeBase.h>
#include <Engine/LevelStreaming.h>
#include <GameFramework/GameStateBase.h>

#include "GameSerializer_Log.h"
#include "GameSerializerCore.h"
#include "GameSerializerInterface.h"

namespace JsonFieldName
{
	constexpr TCHAR LevelActors[] = TEXT("LevelActors");
	constexpr TCHAR WorldOrigin[] = TEXT("WorldOrigin");
	constexpr TCHAR GameState[] = TEXT("GameState");

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
	bIsRemovedLevel = true;
}

UGameSerializerLevelStreamingLambda::UGameSerializerLevelStreamingLambda()
{
	
}

void UGameSerializerLevelStreamingLambda::WhenLevelLoaded()
{
	OnLevelLoaded.Execute(CastChecked<ULevelStreaming>(GetOuter())->GetLoadedLevel());
}

UGameSerializerManager::UGameSerializerManager()
	: bIsEnable(false)
	, bInvokeLoadGame(true)
	, bShouldInitSpawnActor(true)
{
	
}

void UGameSerializerManager::Initialize(FSubsystemCollectionBase& Collection)
{
	EnableSystem();
}

void UGameSerializerManager::Deinitialize()
{
	DisableSystem();
}

void UGameSerializerManager::EnableSystem()
{
	if (ensure(bIsEnable == false))
	{
		bIsEnable = true;

		if (UWorld* World = GetWorld())
		{
			if (World->IsServer() == false)
			{
				return;
			}
			if (LoadedWorld != World && World->GetGameState() != nullptr)
			{
				LoadOrInitWorld(World);
			}
		}
		
		OnGameModeInitialized_DelegateHandle = FGameModeEvents::GameModeInitializedEvent.AddWeakLambda(this, [this](AGameModeBase* GameMode)
		{
			UWorld* World = GameMode->GetWorld();
			World->GameStateSetEvent.AddWeakLambda(this, [this, World](AGameStateBase*)
			{
				if (bIsEnable == false || IsArchiveWorld(World) == false)
				{
					return;
				}

				LoadOrInitWorld(World);
			});
		});

		OnLevelAdd_DelegateHandle = FWorldDelegates::LevelAddedToWorld.AddWeakLambda(this, [this](ULevel* Level, UWorld* World)
		{
			if (bIsEnable == false || IsArchiveWorld(World) == false)
			{
				return;
			}

			LoadOrInitLevel(Level);
		});
	}
}

void UGameSerializerManager::DisableSystem()
{
	if (ensure(bIsEnable))
	{
		bIsEnable = false;
		FWorldDelegates::LevelAddedToWorld.Remove(OnLevelAdd_DelegateHandle);
		FWorldDelegates::OnPostWorldInitialization.Remove(OnGameModeInitialized_DelegateHandle);
	}
}

bool UGameSerializerManager::IsArchiveWorld(UWorld* World) const
{
	return World->IsServer();
}

TOptional<TSharedRef<FJsonObject>> UGameSerializerManager::TryLoadJsonObject(UWorld* World, const FString& Category, const FString& FileName)
{
	ISaveGameSystem* SaveSystem = IPlatformFeaturesModule::Get().GetSaveGameSystem();
	if (ensure(SaveSystem))
	{
		const FString FilePath = FPaths::Combine(Category, FileName);
		if (SaveSystem->DoesSaveGameExist(*FilePath, UserIndex))
		{
			TArray<uint8> BinaryArray;
			if (SaveSystem->LoadGame(false, *FilePath, UserIndex, BinaryArray))
			{
				uint8* CompressedBuffer = BinaryArray.GetData();
				int32 CompressionHeader = 0;
				const int32 CompressionHeaderSize = sizeof(CompressionHeader);

				FMemory::Memcpy(&CompressionHeader, CompressedBuffer, CompressionHeaderSize);
				CompressedBuffer += CompressionHeaderSize;

				const int32 CompressedSize = BinaryArray.Num() - CompressionHeaderSize;
				const int32 UncompressedSize = CompressionHeader;
				TArray<uint8> UncompressedBuffer;
				UncompressedBuffer.AddUninitialized(UncompressedSize);

				FCompression::UncompressMemory(NAME_Zlib, UncompressedBuffer.GetData(), UncompressedSize, CompressedBuffer, CompressedSize);
				
				UncompressedBuffer.Add(0);
				const FString JsonString = FString(UTF8_TO_TCHAR(UncompressedBuffer.GetData()));
				const TSharedPtr<FJsonObject> JsonObject = GameSerializerCore::StringToJsonObject(JsonString);
				if (ensure(JsonObject.IsValid()))
				{
					return JsonObject.ToSharedRef();
				}
			}
		}
	}
	return {};
}

void UGameSerializerManager::SaveJsonObject(UWorld* World, const TSharedRef<FJsonObject>& JsonObject, const FString& Category, const FString& FileName)
{
	ISaveGameSystem* SaveSystem = IPlatformFeaturesModule::Get().GetSaveGameSystem();
	if (ensure(SaveSystem))
	{
		const FString JsonString = GameSerializerCore::JsonObjectToString(JsonObject);

		const FTCHARToUTF8 UTF8String(*JsonString);
		
		TArray<uint8> BinaryBuffer;
		const int32 UncompressedSize = UTF8String.Length();
		int32 CompressionHeader = UncompressedSize;
		const int32 CompressionHeaderSize = sizeof(CompressionHeader);

		int32 CompressedSize = FMath::TruncToInt(1.1f * UncompressedSize);
		BinaryBuffer.SetNum(CompressionHeaderSize + CompressedSize);

		uint8* CompressedBuffer = BinaryBuffer.GetData();
		FMemory::Memcpy(CompressedBuffer, &CompressionHeader, CompressionHeaderSize);
		CompressedBuffer += CompressionHeaderSize;

		FCompression::CompressMemory(NAME_Zlib, CompressedBuffer, CompressedSize, UTF8String.Get(), UncompressedSize, COMPRESS_BiasMemory);

		BinaryBuffer.SetNum(CompressionHeaderSize + CompressedSize);
		
		SaveSystem->SaveGame(false, *FPaths::Combine(Category, FileName), UserIndex, BinaryBuffer);
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
	const bool IsUnLoaded = LoadedLevels.Contains(Level) == false;
	ensure(IsUnLoaded);
	if (IsUnLoaded == false)
	{
		return;
	}
	LoadedLevels.Add(Level);
	
	const FString LevelName = Level->GetOuter()->GetName();
	AWorldSettings* WorldSettings = CastChecked<UWorld>(Level->GetOuter())->GetWorldSettings();
	if (ensure(WorldSettings))
	{
		UGameSerializerLevelComponent* LevelComponent = NewObject<UGameSerializerLevelComponent>(WorldSettings);
		WorldSettings->AddOwnedComponent(LevelComponent);
		LevelComponent->RegisterComponent();

		LevelComponent->OnLevelRemoved.BindWeakLambda(this, [this](ULevel* RemovedLevel)
		{
			const bool IsLoadedLevel = LoadedLevels.Contains(RemovedLevel);
			ensure(IsLoadedLevel);
			if (IsLoadedLevel == false)
			{
				return;
			}
			LoadedLevels.Remove(RemovedLevel);
			SerializeLevel(RemovedLevel);
		});
	}

	if (TSharedRef<FLevelDeserializer>* StreamLoadedLevelDeserializerPtr = StreamLoadedLevelDataMap.Find(Level))
	{
		GameSerializerStatLog(STAT_GameSerializerManager_LoadStreamLevelEnd);
		
		UE_LOG(GameSerializer_Log, Display, TEXT("完成流式关卡[%s]加载"), *LevelName);
		
		FLevelDeserializer& FLevelDeserializer = StreamLoadedLevelDeserializerPtr->Get();
		FLevelDeserializer.LoadDynamicObjectExtendData();

		StreamLoadedLevelDataMap.Remove(Level);
		WhenLevelLoaded(Level);
		return;
	}
	
	if (bInvokeLoadGame)
	{
		TOptional<TSharedRef<FJsonObject>> JsonObject = TryLoadJsonObject(Level->GetWorld(), TEXT("Levels"), *LevelName);
		if (JsonObject.IsSet())
		{
			FGuardValue_Bitfield(bShouldInitSpawnActor, false);

			GameSerializerStatLog(STAT_GameSerializerManage_LoadLevel);
			
			UE_LOG(GameSerializer_Log, Display, TEXT("加载关卡[%s]"), *LevelName);
			
			TArray<AActor*> PrepareLoadActors;
			for (AActor* Actor : Level->Actors)
			{
				if (Actor && Actor->GetOwner() == nullptr && Actor->Implements<UActorGameSerializerInterface>())
				{
					if (IActorGameSerializerInterface::CanGameSerializedInLevel(Actor))
					{
						PrepareLoadActors.Add(Actor);
					}
				}
			}

			FLevelDeserializer LevelDeserializer(Level, JsonObject.GetValue());
			const FIntVector OldWorldOrigin = LevelDeserializer.GetStruct<FIntVector>(JsonFieldName::WorldOrigin);
			TGuardValue<FIntVector> WorldOffsetGuard(GameSerializerContext::WorldOffset, OldWorldOrigin - Level->GetWorld()->OriginLocation);

			UWorld* World = Level->GetWorld();
			const bool IsMainLevel = World->PersistentLevel == Level;
			if (IsMainLevel)
			{
				LevelDeserializer.RetargetDynamicObjectName(JsonFieldName::GameState, World->GetGameState()->GetFName());
			}
			
			LevelDeserializer.LoadAllDataImmediately();
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

			WhenLevelLoaded(Level);
			return;
		}
	}

	{
		GameSerializerStatLog(STAT_GameSerializerManage_InitLevel);
		
		UE_LOG(GameSerializer_Log, Display, TEXT("初始化关卡[%s]"), *LevelName);
		for (int32 Idx = 0; Idx < Level->Actors.Num(); ++Idx)
		{
			AActor* Actor = Level->Actors[Idx];
			if (IsValid(Actor))
			{
				InitActorAndComponents(Actor);
			}
		}

		WhenLevelInited(Level);
	}
}

DECLARE_CYCLE_STAT(TEXT("GameSerializerManager_LoadStreamLevelStart"), STAT_GameSerializerManager_LoadStreamLevelStart, STATGROUP_GameSerializer);
DECLARE_CYCLE_STAT(TEXT("GameSerializerManager_LoadOrInitWorld"), STAT_GameSerializerManage_LoadOrInitWorld, STATGROUP_GameSerializer);
void UGameSerializerManager::LoadOrInitWorld(UWorld* World)
{
	if (LoadedWorld == World)
	{
		return;
	}
	LoadedWorld = World;
	
	GameSerializerStatLog(STAT_GameSerializerManage_LoadOrInitWorld);
	
	UE_LOG(GameSerializer_Log, Display, TEXT("世界[%s]启动游戏序列化系统"), *World->GetName());

	ensure(LoadedLevels.Num() == 0);
	LoadedLevels.Reset();
	
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
					TOptional<TSharedRef<FJsonObject>> JsonObject = TryLoadJsonObject(LoadedLevel->GetWorld(), TEXT("Levels"), *LevelName);
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
								if (IActorGameSerializerInterface::CanGameSerializedInLevel(Actor))
								{
									PrepareLoadActors.Add(Actor);
								}
							}
						}

						TSharedRef<FLevelDeserializer> LevelDeserializer = MakeShared<FLevelDeserializer>(LoadedLevel, JsonObject.GetValue());
						const FIntVector OldWorldOrigin = LevelDeserializer->GetStruct<FIntVector>(JsonFieldName::WorldOrigin);
						TGuardValue<FIntVector> WorldOffsetGuard(GameSerializerContext::WorldOffset, OldWorldOrigin - LoadedLevel->GetWorld()->OriginLocation);
						LevelDeserializer->LoadExternalObject();
						LevelDeserializer->InstanceDynamicObject();
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
	AWorldSettings* LevelSettings = CastChecked<UWorld>(Level->GetOuter())->GetWorldSettings();
	const bool bIsLevelConstructed = LevelSettings->IsActorInitialized();
	UGameSerializerLevelComponent* GameSerializerLevelData = LevelSettings->FindComponentByClass<UGameSerializerLevelComponent>();
	if (bIsLevelConstructed == false || GameSerializerLevelData == nullptr || GameSerializerLevelData->bIsRemovedLevel == true)
	{
		return;
	}

	WhenLevelPreSave(Level);
	
	const FString LevelName = Level->GetOuter()->GetName();
	UE_LOG(GameSerializer_Log, Display, TEXT("保存关卡[%s]"), *LevelName);

	TArray<UObject*> SerializeList;
	SerializeList.Reset(Level->Actors.Num());
	for (AActor* Actor : Level->Actors)
	{
		if (IsValid(Actor) && Actor->Implements<UActorGameSerializerInterface>())
		{
			if (IActorGameSerializerInterface::CanGameSerializedInLevel(Actor))
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

	UWorld* World = Level->GetWorld();
	const bool IsMainLevel = World->PersistentLevel == Level;
	if (IsMainLevel)
	{
		LevelSerializer.AddObject(JsonFieldName::GameState, World->GetGameState());
	}

	const TSharedRef<FJsonObject> JsonObject = LevelSerializer.GetResultJson();
	SaveJsonObject(Level->GetWorld(), JsonObject, TEXT("Levels"), *LevelName);
}

void UGameSerializerManager::ArchiveWorldAllState(UWorld* World)
{
	if (ensure(IsArchiveWorld(World)))
	{
		for (auto PlayerControllerIterator = World->GetPlayerControllerIterator(); PlayerControllerIterator; ++PlayerControllerIterator)
		{
			APlayerController* PlayerController = PlayerControllerIterator->Get();
			if (ensure(PlayerController))
			{
				SerializePlayer(PlayerController);
			}
		}

		// 持久性关卡最后保存（可能有全局数据）
		const TArray<ULevel*>& Levels = World->GetLevels();
		for (int32 Idx = Levels.Num() - 1; Idx >= 0; --Idx)
		{
			ULevel* Level = Levels[Idx];
			SerializeLevel(Level);
		}
	}
}

APawn* UGameSerializerManager::LoadOrSpawnDefaultPawn(AGameModeBase* GameMode, AController* NewPlayer, const FTransform& SpawnTransform)
{
	UWorld* World = GameMode->GetWorld();
	check(World);

	APlayerState* PlayerState = NewPlayer->GetPlayerState<APlayerState>();
	check(PlayerState);
	
	FGuardValue_Bitfield(PlayerState->bUseCustomPlayerNames, true);
	const FString PlayerName = PlayerState->GetPlayerName();

	FActorSpawnParameters SpawnInfo;
	SpawnInfo.Name = *PlayerName;
	SpawnInfo.Instigator = GameMode->GetInstigator();
	SpawnInfo.ObjectFlags |= RF_Transient;	// We never want to save default player pawns into a map
	UClass* PawnClass = GameMode->GetDefaultPawnClassForController(NewPlayer);
	APawn* Pawn = World->SpawnActor<APawn>(PawnClass, SpawnTransform, SpawnInfo);

	if (IsArchiveWorld(World) == false)
	{
		return Pawn;
	}

	UE_LOG(GameSerializer_Log, Display, TEXT("玩家[%s]启动游戏序列化系统"), *PlayerName);

	APlayerController* PlayerController = Cast<APlayerController>(NewPlayer);
	if (ensure(PlayerController))
	{
		FPlayerData PlayerData;
		PlayerData.OriginPawn = Pawn;
		PlayerData.PlayerState = PlayerState;
		PlayerDataMap.Add(PlayerController, PlayerData);
		PlayerController->OnEndPlay.AddDynamic(this, &UGameSerializerManager::OnPlayerPawnEndPlay);
	}
	
	if (bInvokeLoadGame)
	{
		TOptional<TSharedRef<FJsonObject>> JsonObject = TryLoadJsonObject(Pawn->GetWorld(), TEXT("Players"), *PlayerName);
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
			TGuardValue<FIntVector> WorldOffsetGuard(GameSerializerContext::WorldOffset, OldWorldOrigin - Pawn->GetWorld()->OriginLocation);
			PlayerDeserializer.LoadAllDataImmediately();
		}
	}
	
	return Pawn;
}

void UGameSerializerManager::SerializePlayer(APlayerController* Player)
{
	const FPlayerData& PlayerData = PlayerDataMap.FindChecked(Player);
	APawn* Pawn = PlayerData.OriginPawn.Get();
	if (ensure(Pawn))
	{
		struct FPlayerSerializer : public GameSerializerCore::FStructToJson
		{
			FPlayerSerializer()
			{
				CheckFlags = CPF_SaveGame;
			}
		};
		FPlayerSerializer PlayerSerializer;
		PlayerSerializer.AddStruct(JsonFieldName::WorldOrigin, Pawn->GetWorld()->OriginLocation);
		PlayerSerializer.AddObject(JsonFieldName::PlayerController, Player);
		PlayerSerializer.AddObject(JsonFieldName::PlayerState, PlayerData.PlayerState.Get(true));
		PlayerSerializer.AddObject(JsonFieldName::PlayerPawn, Pawn);

		Pawn->MarkPendingKill();

		SaveJsonObject(Pawn->GetWorld(), PlayerSerializer.GetResultJson(), TEXT("Players"), *Pawn->GetName());
	}
}

void UGameSerializerManager::OnPlayerPawnEndPlay(AActor* Controller, EEndPlayReason::Type EndPlayReason)
{
	if (bIsEnable == false)
	{
		return;
	}
	
	APlayerController* Player = CastChecked<APlayerController>(Controller);
	SerializePlayer(Player);
	PlayerDataMap.Remove(Player);
}
