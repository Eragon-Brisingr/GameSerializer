// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Components/ActorComponent.h"
#include "GameSerializerManager.generated.h"

// Level层级的数据和事件
UCLASS()
class UGameSerializerLevelComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	DECLARE_DELEGATE_OneParam(FOnLevelRemoved, ULevel*);
	FOnLevelRemoved OnLevelRemoved;
	
	void OnUnregister() override;
};

UCLASS()
class UGameSerializerLevelStreamingLambda : public UObject
{
	GENERATED_BODY()
public:
	UGameSerializerLevelStreamingLambda();

	DECLARE_DELEGATE_OneParam(FOnLevelLoaded, ULevel*);
	FOnLevelLoaded OnLevelLoaded;
	
	UFUNCTION()
	void WhenLevelLoaded();
};

/**
 * 
 */
UCLASS()
class GAMESERIALIZER_API UGameSerializerManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	UGameSerializerManager();

	void Initialize(FSubsystemCollectionBase& Collection) override;
	void Deinitialize() override;
protected:
	virtual TOptional<TSharedRef<FJsonObject>> TryLoadJsonObject(const FString& Category, const FString& FileName);
	virtual void SaveJsonObject(const TSharedRef<FJsonObject>& JsonObject, const FString& Category, const FString& FileName);

	int32 UserIndex = 0;

	void InitActorAndComponents(AActor* Actor);
	void LoadOrInitLevel(ULevel* Level);
	void LoadOrInitWorld(UWorld* World);

	void SerializeLevel(ULevel* Level);
public:
	void WhenLevelInited(ULevel* Level) { OnLevelInitedNative.Broadcast(Level); }
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnLevelInitedNative, ULevel*);
	FOnLevelInitedNative OnLevelInitedNative;

	void WhenLevelLoaded(ULevel* Level) { OnLevelLoadedNative.Broadcast(Level); }
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnLevelLoadedNative, ULevel*);
	FOnLevelLoadedNative OnLevelLoadedNative;

	void WhenLevelPreSave(ULevel* Level) { OnLevelPreSaveNative.Broadcast(Level); }
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnLevelPreSaveNative, ULevel*);
	FOnLevelPreSaveNative OnLevelPreSaveNative;
private:
	FDelegateHandle OnGameModeInitialized_DelegateHandle;
	FDelegateHandle OnLevelAdd_DelegateHandle;
	uint8 bInvokeLoadGame : 1;
	uint8 bShouldInitSpawnActor : 1;

#if DO_CHECK
	TArray<TWeakObjectPtr<ULevel>> LoadedLevels;
#endif

	TMap<TWeakObjectPtr<ULevel>, TSharedRef<struct FLevelDeserializer>> StreamLoadedLevelDataMap;
	UPROPERTY(Transient)
	TArray<UGameSerializerLevelStreamingLambda*> CachedLevelStreamingLambdas;
public:
	struct FPlayerData
	{
		TWeakObjectPtr<APlayerController> PlayerController;
		TWeakObjectPtr<APlayerState> PlayerState;
	};
	TMap<TWeakObjectPtr<APawn>, FPlayerData> PlayerDataMap;

	UFUNCTION(BlueprintCallable, Category = "游戏序列化")
	APawn* LoadOrSpawnDefaultPawn(AGameModeBase* GameMode, AController* NewPlayer, const FTransform& SpawnTransform);
private:
	UFUNCTION()
	void OnPlayerPawnEndPlay(AActor* PawnActor, EEndPlayReason::Type EndPlayReason);
};
