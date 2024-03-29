// Fill out your copyright notice in the Description page of Project Settings.


#include "GameSerializerManager.h"
#include <PlatformFeatures.h>
#include <SaveGameSystem.h>
#include <GameFramework/PlayerState.h>
#include <GameFramework/GameModeBase.h>
#include <Engine/LevelStreaming.h>
#include <GameFramework/GameStateBase.h>
#include <GameFramework/WorldSettings.h>
#if WITH_EDITOR
#include <Editor.h>
#endif

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
	bIsRemovedLevel = true;
}

UGameSerializerLevelStreamingLambda::UGameSerializerLevelStreamingLambda()
{
	
}

void UGameSerializerLevelStreamingLambda::WhenLevelLoaded()
{
	OnLevelLoaded.Execute(CastChecked<ULevelStreaming>(GetOuter())->GetLoadedLevel());
}

bool UGameSerializerWorldSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UGameSerializerWorldSubsystem::UpdateStreamingState()
{
	Super::UpdateStreamingState();

	if (const UGameInstance* GameInstance = GetWorld()->GetGameInstance())
	{
		if (UGameSerializerManager* GameSerializerManager = GameInstance->GetSubsystem<UGameSerializerManager>())
		{
			GameSerializerManager->UpdateLevelStreamingData();
		}
	}
}

UGameSerializerManager::UGameSerializerManager()
	: bIsEnable(false)
	, bInvokeLoadGame(true)
	, bShouldInitSpawnActor(true)
{
	
}

bool UGameSerializerManager::ShouldCreateSubsystem(UObject* Outer) const
{
	if (UGameSerializerManager::StaticClass() == GetClass())
	{
		TArray<UClass*> DerivedClasses;
		GetDerivedClasses(UGameSerializerManager::StaticClass(), DerivedClasses, false);
		return DerivedClasses.Num() == 0;
	}
	return true;
}

void UGameSerializerManager::Initialize(FSubsystemCollectionBase& Collection)
{
	EnableSystem();
}

void UGameSerializerManager::Deinitialize()
{
	if (UWorld* World = LoadedWorld.Get())
	{
		UE_LOG(GameSerializer_Log, Display, TEXT("游戏实例被销毁，储存整个世界[%s]"), *World->GetName());
		SerializeWorldWhenRemoved(World);
	}
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

#if WITH_EDITOR
		PrePIEEnded_DelegateHandle = FEditorDelegates::PrePIEEnded.AddWeakLambda(this, [this](const bool)
		{
			if (UWorld* World = LoadedWorld.Get())
			{
				UE_LOG(GameSerializer_Log, Display, TEXT("结束PlayInEditor，储存整个世界[%s]"), *World->GetName());
				SerializeWorldWhenRemoved(World);
			}
		});
#endif

		OnLevelAdd_DelegateHandle = FWorldDelegates::LevelAddedToWorld.AddWeakLambda(this, [this](ULevel* Level, UWorld* World)
		{
			if (bIsEnable == false || IsArchiveWorld(World) == false)
			{
				return;
			}

			LoadOrInitLevel(Level);
		});

		OnWorldCleanup_DelegateHandle = FWorldDelegates::OnWorldCleanup.AddWeakLambda(this, [this](UWorld* World, bool /*bSessionEnded*/, bool /*bCleanupResources*/)
		{
			if (World != LoadedWorld || ensure(IsArchiveWorld(World)) == false)
			{
				return;
			}

			UE_LOG(GameSerializer_Log, Display, TEXT("世界[%s]被销毁，储存整个世界"), *World->GetName());
			SerializeWorldWhenRemoved(World);
		});
		
		OnGameModeInitialized_DelegateHandle = FGameModeEvents::GameModeInitializedEvent.AddWeakLambda(this, [this](AGameModeBase* GameMode)
		{
			UWorld* World = GameMode->GetWorld();
			World->GameStateSetEvent.AddWeakLambda(this, [this, World](AGameStateBase* GameState)
			{
				if (bIsEnable == false || IsArchiveWorld(World) == false)
				{
					return;
				}

				if (ensure(GameState))
				{
					GameState->Rename(TEXT("GameState"));
					LoadOrInitWorld(World);
				}
			});
		});
	}
}

void UGameSerializerManager::DisableSystem()
{
	if (ensure(bIsEnable))
	{
		bIsEnable = false;
		FWorldDelegates::LevelAddedToWorld.Remove(OnLevelAdd_DelegateHandle);
		FWorldDelegates::OnWorldCleanup.Remove(OnWorldCleanup_DelegateHandle);
		FGameModeEvents::GameModeInitializedEvent.Remove(OnGameModeInitialized_DelegateHandle);
#if WITH_EDITOR
		FEditorDelegates::PrePIEEnded.Remove(PrePIEEnded_DelegateHandle);
#endif
	}
}

void UGameSerializerManager::DisableAutoSave()
{
	LoadedWorld = nullptr;
	LoadedLevels.Empty();
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
	check(Actor->Implements<UActorGameSerializerInterface>());
	IActorGameSerializerInterface::WhenGameInit(Actor);
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

	auto LoadLevelExtendData = [](const FLevelDeserializer& LevelDeserializer, const TSet<UObject*>& LoadedActors)
	{
		using FInstancedObjectData = FLevelDeserializer::FInstancedObjectData;
		
		struct FActorSortUnit
		{
			FActorSortUnit(const FInstancedObjectData& InstancedObjectData, int32 Priority)
				: InstancedObjectData(InstancedObjectData), Priority(Priority)
			{}
			const FInstancedObjectData& InstancedObjectData;
			int32 Priority;
		};
		TArray<FActorSortUnit> ToLoadActors;
		for (const FInstancedObjectData& InstancedObjectData : LevelDeserializer.GetAllInstancedObjectData())
		{
			if (UObject* LoadedObject = InstancedObjectData.Object.Get())
			{
				if (LoadedActors.Contains(LoadedObject))
				{
					ToLoadActors.Add({ InstancedObjectData, IActorGameSerializerInterface::GetGameSerializePriority(CastChecked<AActor>(LoadedObject)) });
				}
				else
				{
					LevelDeserializer.ExecutePostLoad(LoadedObject, InstancedObjectData);
				}
			}
		}
		ToLoadActors.Sort([](const FActorSortUnit& LHS, const FActorSortUnit& RHS){ return LHS.Priority < RHS.Priority; });
		for (const FActorSortUnit& ActorSortUnit : ToLoadActors)
		{
			if (UObject* LoadedObject = ActorSortUnit.InstancedObjectData.Object.Get())
			{
				LevelDeserializer.ExecutePostLoad(LoadedObject, ActorSortUnit.InstancedObjectData);
			}
		}
	};
	
	const FString LevelName = GetLevelPath(Level);
	AWorldSettings* WorldSettings = CastChecked<UWorld>(Level->GetOuter())->GetWorldSettings();
	if (ensure(WorldSettings))
	{
		UGameSerializerLevelComponent* LevelComponent = NewObject<UGameSerializerLevelComponent>(WorldSettings);
		WorldSettings->AddOwnedComponent(LevelComponent);
		LevelComponent->RegisterComponent();

		LevelComponent->OnLevelRemoved.BindWeakLambda(this, [this](ULevel* RemovedLevel)
		{
			UWorld* World = RemovedLevel->GetWorld();
			ensure(World);
			if (World != LoadedWorld)
			{
				return;
			}
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

	if (bInvokeLoadGame)
	{
		if (TSharedRef<FLevelDeserializer>* StreamLoadedLevelDeserializerPtr = StreamLoadedLevelDataMap.Find(Level))
		{
			GameSerializerStatLog(STAT_GameSerializerManager_LoadStreamLevelEnd);
		
			UE_LOG(GameSerializer_Log, Display, TEXT("完成流式关卡[%s]加载"), *LevelName);

			FLevelDeserializer& LevelDeserializer = StreamLoadedLevelDeserializerPtr->Get();
			LevelDeserializer.RestoreDynamicActorSpawnedData();

			const TSet<UObject*> LoadedActors{ LevelDeserializer.GetObjects(JsonFieldName::LevelActors) };
			LoadLevelExtendData(LevelDeserializer, LoadedActors);

			StreamLoadedLevelDataMap.Remove(Level);
			WhenLevelLoaded(Level);
			return;
		}

		TOptional<TSharedRef<FJsonObject>> JsonObject = TryLoadJsonObject(Level->GetWorld(), TEXT("Levels"), *LevelName);
		if (JsonObject.IsSet())
		{
			FGuardValue_Bitfield(bShouldInitSpawnActor, false);

			GameSerializerStatLog(STAT_GameSerializerManage_LoadLevel);
			
			UE_LOG(GameSerializer_Log, Display, TEXT("加载关卡[%s]"), *LevelName);
			
			TArray<AActor*> PrepareLoadActors;
			for (AActor* Actor : Level->Actors)
			{
				if (IsValid(Actor) && Actor->Implements<UActorGameSerializerInterface>() && IActorGameSerializerInterface::CanGameSerializedInLevel(Actor))
				{
					PrepareLoadActors.Add(Actor);
				}
			}

			FLevelDeserializer LevelDeserializer(Level, JsonObject.GetValue());
			const FIntVector OldWorldOrigin = LevelDeserializer.GetStruct<FIntVector>(JsonFieldName::WorldOrigin);
			TGuardValue<FIntVector> WorldOffsetGuard(GameSerializerContext::WorldOffset, OldWorldOrigin - Level->GetWorld()->OriginLocation);

			LevelDeserializer.LoadExternalObject();
			LevelDeserializer.InstanceDynamicObject();
			LevelDeserializer.LoadDynamicObjectJsonData();
			LevelDeserializer.DynamicActorFinishSpawning();
			LevelDeserializer.RestoreDynamicActorSpawnedData();

			const TSet<UObject*> LoadedActors{ LevelDeserializer.GetObjects(JsonFieldName::LevelActors) };
			LoadLevelExtendData(LevelDeserializer, LoadedActors);

			for (AActor* Actor : PrepareLoadActors)
			{
				if (IsValid(Actor) && LoadedActors.Contains(Actor) == false)
				{
					Actor->Destroy();
				}
			}

			WhenLevelLoaded(Level);
			return;
		}
	}

	{
		GameSerializerStatLog(STAT_GameSerializerManage_InitLevel);
		
		UE_LOG(GameSerializer_Log, Display, TEXT("初始化关卡[%s]"), *LevelName);

		struct FActorSortUnit
		{
			AActor* Actor;
			int32 Priority;
		};
		TArray<FActorSortUnit> ActorsToLoad;
		for (int32 Idx = 0; Idx < Level->Actors.Num(); ++Idx)
		{
			AActor* Actor = Level->Actors[Idx];
			if (IsValid(Actor) && Actor->Implements<UActorGameSerializerInterface>() && IActorGameSerializerInterface::CanGameSerializedInLevel(Actor))
			{
				ActorsToLoad.Add({ Actor, IActorGameSerializerInterface::GetGameSerializePriority(Actor) });
			}
		}
		ActorsToLoad.Sort([](const FActorSortUnit& LHS, const FActorSortUnit& RHS){ return LHS.Priority < RHS.Priority; });
		for (const FActorSortUnit& Actor : ActorsToLoad)
		{
			InitActorAndComponents(Actor.Actor);
		}

		WhenLevelInitialized(Level);
	}
}

DECLARE_CYCLE_STAT(TEXT("GameSerializerManager_LoadStreamLevelStart"), STAT_GameSerializerManager_LoadStreamLevelStart, STATGROUP_GameSerializer);
DECLARE_CYCLE_STAT(TEXT("GameSerializerManager_LoadOrInitWorld"), STAT_GameSerializerManage_LoadOrInitWorld, STATGROUP_GameSerializer);
void UGameSerializerManager::LoadOrInitWorld(UWorld* World)
{
	if (ensure(LoadedWorld == nullptr) == false)
	{
		return;
	}
	LoadedWorld = World;
	
	GameSerializerStatLog(STAT_GameSerializerManage_LoadOrInitWorld);
	
	UE_LOG(GameSerializer_Log, Display, TEXT("世界[%s]启动游戏序列化系统"), *World->GetName());

	ensure(LoadedLevels.Num() == 0);
	LoadedLevels.Reset();
	
	StreamLoadedLevelDataMap.Reset();
	CachedLevelStreamingLambdas.Reset();
	UpdateLevelStreamingData();

	for (ULevel* Level : World->GetLevels())
	{
		LoadOrInitLevel(Level);
	}

	World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateWeakLambda(this, [this](AActor* Actor)
	{
		if (bShouldInitSpawnActor && Actor->Implements<UActorGameSerializerInterface>() && IActorGameSerializerInterface::CanGameSerializedInLevel(Actor))
		{
			InitActorAndComponents(Actor);
		}
	}));
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
	
	const FString LevelName = GetLevelPath(Level);
	UE_LOG(GameSerializer_Log, Display, TEXT("保存关卡[%s]"), *LevelName);

	struct FActorSortUnit
	{
		AActor* Actor;
		int32 Priority;
	};
	TArray<FActorSortUnit> ToSaveActors;
	for (AActor* Actor : Level->Actors)
	{
		if (IsValid(Actor) && Actor->Implements<UActorGameSerializerInterface>() && IActorGameSerializerInterface::CanGameSerializedInLevel(Actor))
		{
			if (ensure(IActorGameSerializerInterface::GetGameSerializedOwner(Actor) == nullptr))
			{
				ToSaveActors.Add({ Actor, IActorGameSerializerInterface::GetGameSerializePriority(Actor) });
			}
		}
	}
	ToSaveActors.Sort([](const FActorSortUnit& LHS, const FActorSortUnit& RHS){ return LHS.Priority > RHS.Priority; });

	TArray<UObject*> SerializeList;
	SerializeList.SetNum(ToSaveActors.Num());
	for (int32 Idx = 0; Idx < ToSaveActors.Num(); ++Idx)
	{
		SerializeList[Idx] = ToSaveActors[Idx].Actor;
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
	SaveJsonObject(Level->GetWorld(), JsonObject, TEXT("Levels"), *LevelName);
}

void UGameSerializerManager::SerializeWorldWhenRemoved(UWorld* World)
{
	check(LoadedWorld == World);
	ArchiveWorldAllState(World);
	LoadedLevels.Empty();
	LoadedWorld = nullptr;
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

void UGameSerializerManager::OpenWorld(TSoftObjectPtr<UWorld> ToWorld)
{
	if (ToWorld.IsNull() == false)
	{
		const FString ToWorldName = ToWorld.ToString();
		if (UWorld* World = LoadedWorld.Get())
		{
			UE_LOG(GameSerializer_Log, Display, TEXT("打开世界[%s]，储存当前世界[%s]"), *ToWorldName, *World->GetName());
			SerializeWorldWhenRemoved(World);
		}
		GEngine->Exec(GetWorld(), *FString::Printf(TEXT("open %s"), *ToWorldName));
	}
}

void UGameSerializerManager::UpdateLevelStreamingData()
{
	if (bInvokeLoadGame == false)
	{
		return;
	}

	UWorld* World = LoadedWorld.Get();
	if (World == nullptr)
	{
		return;
	}
	
	const TArray<ULevelStreaming*>& StreamingLevels = World->GetStreamingLevels();
	for (auto It = CachedLevelStreamingLambdas.CreateIterator(); It; ++It)
	{
		const UGameSerializerLevelStreamingLambda* StreamingLambda = *It;
		if (StreamingLevels.Contains(StreamingLambda->GetOuter()) == false)
		{
			It.RemoveCurrent();
		}
	}
	for (ULevelStreaming* LevelStreaming : StreamingLevels)
	{
		if (CachedLevelStreamingLambdas.ContainsByPredicate([&](const UGameSerializerLevelStreamingLambda* E) { return E->GetOuter() == LevelStreaming; }) == false)
		{
			UGameSerializerLevelStreamingLambda* LevelStreamingLambda = NewObject<UGameSerializerLevelStreamingLambda>(LevelStreaming);
			CachedLevelStreamingLambdas.Add(LevelStreamingLambda);
			LevelStreaming->OnLevelLoaded.AddDynamic(LevelStreamingLambda, &UGameSerializerLevelStreamingLambda::WhenLevelLoaded);
			LevelStreamingLambda->OnLevelLoaded.BindWeakLambda(this, [this](ULevel* LoadedLevel)
			{
				const FString LevelName = GetLevelPath(LoadedLevel);
				TOptional<TSharedRef<FJsonObject>> JsonObject = TryLoadJsonObject(LoadedLevel->GetWorld(), TEXT("Levels"), *LevelName);
				if (JsonObject.IsSet())
				{
					FGuardValue_Bitfield(bShouldInitSpawnActor, false);

					GameSerializerStatLog(STAT_GameSerializerManager_LoadStreamLevelStart);

					UE_LOG(GameSerializer_Log, Display, TEXT("加载流式关卡[%s]"), *LevelName);

					TArray<AActor*> PrepareLoadActors;
					for (AActor* Actor : LoadedLevel->Actors)
					{
						if (IsValid(Actor) && Actor->Implements<UActorGameSerializerInterface>() && IActorGameSerializerInterface::CanGameSerializedInLevel(Actor))
						{
							PrepareLoadActors.Add(Actor);
						}
					}

					const TSharedRef<FLevelDeserializer> LevelDeserializer = MakeShared<FLevelDeserializer>(LoadedLevel, JsonObject.GetValue());
					const FIntVector OldWorldOrigin = LevelDeserializer->GetStruct<FIntVector>(JsonFieldName::WorldOrigin);
					TGuardValue<FIntVector> WorldOffsetGuard(GameSerializerContext::WorldOffset, OldWorldOrigin - LoadedLevel->GetWorld()->OriginLocation);
					LevelDeserializer->LoadExternalObject();
					LevelDeserializer->InstanceDynamicObject();
					LevelDeserializer->LoadDynamicObjectJsonData();

					const TArray<UObject*> LoadedActors = LevelDeserializer->GetObjects(JsonFieldName::LevelActors);
					for (AActor* Actor : PrepareLoadActors)
					{
						if (IsValid(Actor) && LoadedActors.Contains(Actor) == false)
						{
							Actor->Destroy();
						}
					}
					StreamLoadedLevelDataMap.Add(LoadedLevel, LevelDeserializer);
				}
			});
		}
	}
}

FString UGameSerializerManager::GetLevelPath(const ULevel* Level)
{
	const FString PackageName = UWorld::RemovePIEPrefix(Level->GetPackage()->GetName());
	int32 LevelNameStartIndex;
	check(PackageName.FindLastChar(TEXT('/'), LevelNameStartIndex));
	return PackageName.Right(PackageName.Len() - LevelNameStartIndex - 1);
}

APawn* UGameSerializerManager::LoadOrSpawnDefaultPawn(AGameModeBase* GameMode, AController* NewPlayer, const FTransform& SpawnTransform)
{
	UWorld* World = GameMode->GetWorld();
	check(World);

	APlayerState* PlayerState = NewPlayer->GetPlayerState<APlayerState>();
	check(PlayerState);

	FString PlayerName = PlayerState->GetPlayerNameCustom();
	ULevel* SpawnLevel = World->PersistentLevel;
	if (APawn* ExistedPawn = FindObject<APawn>(SpawnLevel, *PlayerName))
	{
		if (ExistedPawn->IsPendingKill())
		{
			ExistedPawn->Rename(nullptr, GetTransientPackage());
		}
		else
		{
			ensure(false);

			// 非Shipping提供个稳定的命名，用于调试
#if !UE_BUILD_SHIPPING
			int32 UniqueIdx = 0;
			FString TestName;
			while (true)
			{
				TestName = FName(*PlayerName, UniqueIdx).ToString();
				ExistedPawn = FindObject<APawn>(SpawnLevel, *TestName);
				if (ExistedPawn == nullptr)
				{
					break;
				}
				if (ExistedPawn->IsPendingKill())
				{
					ExistedPawn->Rename(nullptr, GetTransientPackage());
					break;
				}
				++UniqueIdx;
			}
			PlayerName = TestName;
#else
			PlayerName = MakeUniqueObjectName(SpawnLevel, APawn::StaticClass(), *PlayerName).ToString();
#endif
		}
	}

	UE_LOG(GameSerializer_Log, Display, TEXT("玩家[%s]启动游戏序列化系统"), *PlayerName);

	TOptional<TSharedRef<FJsonObject>> JsonObject = bInvokeLoadGame ? TryLoadJsonObject(World, TEXT("Players"), *PlayerName) : TOptional<TSharedRef<FJsonObject>>();
	FGuardValue_Bitfield(bShouldInitSpawnActor, bInvokeLoadGame ? JsonObject.IsSet() == false : true);
	FActorSpawnParameters SpawnInfo;
	SpawnInfo.bNoFail = true;
	SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	SpawnInfo.Name = *PlayerName;
	SpawnInfo.Instigator = GameMode->GetInstigator();
	SpawnInfo.OverrideLevel = SpawnLevel;
	SpawnInfo.ObjectFlags |= RF_Transient;	// We never want to save default player pawns into a map
	UClass* PawnClass = GameMode->GetDefaultPawnClassForController(NewPlayer);
	APawn* Pawn = World->SpawnActor<APawn>(PawnClass, SpawnTransform, SpawnInfo);

	if (IsArchiveWorld(World) == false)
	{
		return Pawn;
	}

	APlayerController* PlayerController = Cast<APlayerController>(NewPlayer);
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
	else
	{
		if (PlayerController->Implements<UActorGameSerializerInterface>())
		{
			IActorGameSerializerInterface::WhenGameInit(PlayerController);
		}
		if (PlayerState->Implements<UActorGameSerializerInterface>())
		{
			IActorGameSerializerInterface::WhenGameInit(PlayerState);
		}
		if (Pawn->Implements<UActorGameSerializerInterface>())
		{
			IActorGameSerializerInterface::WhenGameInit(Pawn);
		}
	}
	
	return Pawn;
}

void UGameSerializerManager::SerializePlayer(APlayerController* Player)
{
	APawn* Pawn = Player->GetPawn();
	if (ensure(Pawn && Player->PlayerState))
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
		PlayerSerializer.AddObject(JsonFieldName::PlayerState, Player->PlayerState);
		PlayerSerializer.AddObject(JsonFieldName::PlayerPawn, Pawn);

		SaveJsonObject(Pawn->GetWorld(), PlayerSerializer.GetResultJson(), TEXT("Players"), *Pawn->GetName());
	}
}
