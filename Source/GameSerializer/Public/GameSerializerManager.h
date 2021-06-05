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
UCLASS()
class GAMESERIALIZER_API UGameSerializerManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	UGameSerializerManager();
	
	bool ShouldCreateSubsystem(UObject* Outer) const override;
	void Initialize(FSubsystemCollectionBase& Collection) override;
	void Deinitialize() override;

	bool IsEnable() const { return bIsEnable; }
	void EnableSystem();
	void DisableSystem();
	void DisableAutoSave();
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
	void SerializeWorldWhenRemoved(UWorld* World);

	virtual void WhenLevelInitialized(ULevel* Level) { OnLevelInitializedNative.Broadcast(Level); }
	virtual void WhenLevelLoaded(ULevel* Level) { OnLevelLoadedNative.Broadcast(Level); }
	virtual void WhenLevelPreSave(ULevel* Level) { OnLevelPreSaveNative.Broadcast(Level); }
public:
	UFUNCTION(BlueprintCallable, Category = "游戏序列化")
	void ArchiveWorldAllState(UWorld* World);

	UFUNCTION(BlueprintCallable, Category = "游戏序列化")
	void OpenWorld(TSoftObjectPtr<UWorld> ToWorld);
	
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnLevelInitializedNative, ULevel*);
	FOnLevelInitializedNative OnLevelInitializedNative;

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnLevelLoadedNative, ULevel*);
	FOnLevelLoadedNative OnLevelLoadedNative;

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnLevelPreSaveNative, ULevel*);
	FOnLevelPreSaveNative OnLevelPreSaveNative;
private:
	FDelegateHandle OnLevelAdd_DelegateHandle;
	FDelegateHandle OnWorldCleanup_DelegateHandle;
	FDelegateHandle OnGameModeInitialized_DelegateHandle;
#if WITH_EDITORONLY_DATA
	FDelegateHandle PrePIEEnded_DelegateHandle;
#endif
	uint8 bIsEnable : 1;
	uint8 bInvokeLoadGame : 1;
	uint8 bShouldInitSpawnActor : 1;

	TWeakObjectPtr<UWorld> LoadedWorld;
	TArray<TWeakObjectPtr<ULevel>> LoadedLevels;

	TMap<TWeakObjectPtr<ULevel>, TSharedRef<struct FLevelDeserializer>> StreamLoadedLevelDataMap;

	static FString GetLevelPath(const ULevel* Level);
public:
	UFUNCTION(BlueprintCallable, Category = "游戏序列化")
	APawn* LoadOrSpawnDefaultPawn(AGameModeBase* GameMode, AController* NewPlayer, const FTransform& SpawnTransform);
	virtual void SerializePlayer(APlayerController* Player);
};
