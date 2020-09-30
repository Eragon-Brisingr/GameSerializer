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

	bool bIsRemovedLevel = false;
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
UCLASS(abstract)
class GAMESERIALIZER_API UGameSerializerManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	UGameSerializerManager();

	void Initialize(FSubsystemCollectionBase& Collection) override;
	void Deinitialize() override;

	bool IsEnable() const { return bIsEnable; }
	void EnableSystem();
	void DisableSystem();
protected:
	// 判定是否可以启动游戏序列化系统
	virtual bool IsArchiveWorld(UWorld* World) const;
	virtual TOptional<TSharedRef<FJsonObject>> TryLoadJsonObject(UWorld* World, const FString& Category, const FString& FileName);
	virtual void SaveJsonObject(UWorld* World, const TSharedRef<FJsonObject>& JsonObject, const FString& Category, const FString& FileName);

	int32 UserIndex = 0;

	void InitActorAndComponents(AActor* Actor);
	void LoadOrInitLevel(ULevel* Level);
	void LoadOrInitWorld(UWorld* World);

	void SerializeLevel(ULevel* Level);

	void WhenLevelInited(ULevel* Level) { OnLevelInitedNative.Broadcast(Level); }
	void WhenLevelLoaded(ULevel* Level) { OnLevelLoadedNative.Broadcast(Level); }
	void WhenLevelPreSave(ULevel* Level) { OnLevelPreSaveNative.Broadcast(Level); }
public:
	UFUNCTION(BlueprintCallable, Category = "游戏序列化")
	void ArchiveWorldAllState(UWorld* World);
	
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnLevelInitedNative, ULevel*);
	FOnLevelInitedNative OnLevelInitedNative;

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnLevelLoadedNative, ULevel*);
	FOnLevelLoadedNative OnLevelLoadedNative;

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnLevelPreSaveNative, ULevel*);
	FOnLevelPreSaveNative OnLevelPreSaveNative;
private:
	FDelegateHandle OnGameModeInitialized_DelegateHandle;
	FDelegateHandle OnLevelAdd_DelegateHandle;
	uint8 bIsEnable : 1;
	uint8 bInvokeLoadGame : 1;
	uint8 bShouldInitSpawnActor : 1;

	TWeakObjectPtr<UWorld> LoadedWorld;
	TArray<TWeakObjectPtr<ULevel>> LoadedLevels;

	TMap<TWeakObjectPtr<ULevel>, TSharedRef<struct FLevelDeserializer>> StreamLoadedLevelDataMap;
	UPROPERTY(Transient)
	TArray<UGameSerializerLevelStreamingLambda*> CachedLevelStreamingLambdas;
public:
	struct FPlayerData
	{
		TWeakObjectPtr<APawn> OriginPawn;
		TWeakObjectPtr<APlayerState> PlayerState;
	};
	TMap<TWeakObjectPtr<APlayerController>, FPlayerData> PlayerDataMap;

	UFUNCTION(BlueprintCallable, Category = "游戏序列化")
	APawn* LoadOrSpawnDefaultPawn(AGameModeBase* GameMode, AController* NewPlayer, const FTransform& SpawnTransform);
	void SerializePlayer(APlayerController* Player);
private:
	UFUNCTION()
	void OnPlayerPawnEndPlay(AActor* Controller, EEndPlayReason::Type EndPlayReason);
};
