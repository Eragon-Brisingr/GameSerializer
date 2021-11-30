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
	void WhenGamePostLoad(const FGameSerializerExtendDataContainer& ExtendData, const FGameSerializerCallRepNotifyFunc& CallRepNotifyFunc);
	virtual void WhenGamePostLoad_Implementation(const FGameSerializerExtendDataContainer& ExtendData, const FGameSerializerCallRepNotifyFunc& CallRepNotifyFunc);
	static void WhenGamePostLoad(UObject* Obj, const FGameSerializerExtendDataContainer& ExtendData, const FGameSerializerCallRepNotifyFunc& CallRepNotifyFunc);

	UFUNCTION(BlueprintNativeEvent, Category = "游戏序列化")
	TArray<FName> GetCallRepNotifyIgnorePropertyNames();
	virtual TArray<FName> GetCallRepNotifyIgnorePropertyNames_Implementation() { return TArray<FName>(); }
	static TArray<FName> GetCallRepNotifyIgnorePropertyNames(UObject* Obj) { return Execute_GetCallRepNotifyIgnorePropertyNames(Obj); }
protected:
	void DefaultWhenGamePostLoad(const FGameSerializerExtendDataContainer& ExtendData, const FGameSerializerCallRepNotifyFunc& CallRepNotifyFunc);
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
	static void WhenGameInit(UActorComponent* ActorComponent);
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
	static void WhenGameInit(AActor* Actor);

	UFUNCTION(BlueprintNativeEvent, Category = "游戏序列化")
	bool CanGameSerializedInLevel() const;
	virtual bool CanGameSerializedInLevel_Implementation() const { return true; }
	static bool CanGameSerializedInLevel(AActor* Actor) { return Execute_CanGameSerializedInLevel(Actor); }

	UFUNCTION(BlueprintNativeEvent, Category = "游戏序列化")
	AActor* GetGameSerializedOwner() const;
	virtual AActor* GetGameSerializedOwner_Implementation() const;
	static AActor* GetGameSerializedOwner(const AActor* Actor);

	UFUNCTION(BlueprintNativeEvent, Category = "游戏序列化")
	void SetGameSerializedOwner(AActor* GameSerializedOwner);
	virtual void SetGameSerializedOwner_Implementation(AActor* GameSerializedOwner);
	static void SetGameSerializedOwner(AActor* Actor, AActor* GameSerializedOwner);
};
