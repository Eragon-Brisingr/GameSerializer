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
	virtual TOptional<TSharedRef<FJsonObject>> TryLoadJsonObject(const FName& FileName);
	virtual void SaveJsonObject(const FName& FileName, const TSharedRef<FJsonObject>& JsonObject);

	int32 UserIndex = 0;

	void InitActorAndComponents(AActor* Actor);
	void LoadOrInitLevel(ULevel* Level);
	void LoadOrInitWorld(UWorld* World);
private:
	FDelegateHandle OnPostWorldInitialization_DelegateHandle;
	FDelegateHandle OnLevelAdd_DelegateHandle;
	uint8 bInvokeLoadGame : 1;
	uint8 bShouldInitSpawnActor : 1;
};
