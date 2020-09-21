// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameSerializeManager.generated.h"

/**
 * 
 */
UCLASS()
class GAMESERIALIZER_API UGameSerializeManager : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	void Initialize(FSubsystemCollectionBase& Collection) override;
	void Deinitialize() override;

private:
	FDelegateHandle OnLevelAdd_DelegateHandle;
	static bool bInvokeLoadGame;
};
