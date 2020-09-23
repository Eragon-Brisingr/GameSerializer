// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameSerializerExtendData.h"
#include "GameSerializerInterface.generated.h"

UINTERFACE(MinimalAPI)
class UGameSerializerInterface : public UInterface
{
	GENERATED_BODY()
};

class GAMESERIALIZER_API IGameSerializerInterface
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintNativeEvent, Category = "游戏序列化")
	FGameSerializerExtendDataContainer WhenGamePreSave();
	virtual FGameSerializerExtendDataContainer WhenGamePreSave_Implementation();
	static FGameSerializerExtendDataContainer WhenGamePreSave(UObject* Obj);

	UFUNCTION(BlueprintNativeEvent, Category = "游戏序列化")
	void WhenGamePostLoad(const FGameSerializerExtendDataContainer& ExtendData);
	virtual void WhenGamePostLoad_Implementation(const FGameSerializerExtendDataContainer& ExtendData);
	static void WhenGamePostLoad(UObject* Obj, const FGameSerializerExtendDataContainer& ExtendData);
};

UINTERFACE(MinimalAPI)
class UComponentGameSerializerInterface : public UGameSerializerInterface
{
	GENERATED_BODY()
};

class GAMESERIALIZER_API IComponentGameSerializerInterface : public IGameSerializerInterface
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintNativeEvent, Category = "游戏序列化")
	void WhenGameInit();
	virtual void WhenGameInit_Implementation();
	static void WhenGameInit(UObject* Obj);
};

UINTERFACE(MinimalAPI)
class UActorGameSerializerInterface : public UGameSerializerInterface
{
	GENERATED_BODY()
};

class GAMESERIALIZER_API IActorGameSerializerInterface : public IGameSerializerInterface
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintNativeEvent, Category = "游戏序列化")
	void WhenGameInit();
	virtual void WhenGameInit_Implementation();
	static void WhenGameInit(UObject* Obj);

	UFUNCTION(BlueprintNativeEvent, Category = "游戏序列化")
	bool CanGameSerialized() const;
	virtual bool CanGameSerialized_Implementation() const { return true; }
	static bool CanGameSerialized(UObject* Obj) { return Execute_CanGameSerialized(Obj); }
};
