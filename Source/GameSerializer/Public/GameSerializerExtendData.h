// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameSerializerExtendData.generated.h"

/**
 * 
 */
USTRUCT(BlueprintType, BlueprintInternalUseOnly)
struct GAMESERIALIZER_API FGameSerializerExtendData
{
	GENERATED_BODY()
public:
	virtual ~FGameSerializerExtendData() {}
};

USTRUCT(BlueprintType, BlueprintInternalUseOnly)
struct GAMESERIALIZER_API FGameSerializerExtendDataContainer
{
	GENERATED_BODY()
public:
	FGameSerializerExtendDataContainer() = default;
	
	UPROPERTY()
	const UScriptStruct* Struct = nullptr;

	TSharedPtr<FGameSerializerExtendData> ExtendData;

	bool IsValid() const { return Struct != nullptr; }
	
	template<typename T>
	static FGameSerializerExtendDataContainer Make(const T& ExtendData)
	{
		FGameSerializerExtendDataContainer Container;
		Container.Struct = T::StaticStruct();
		Container.ExtendData = MakeShared<T>(ExtendData);
		return Container;
	}

	template<typename T>
	const T& Get() const
	{
		check(Struct->IsChildOf(T::StaticStruct()));
		return static_cast<const T&>(*ExtendData.Get());
	}
};

UCLASS()
class UGameSerializerExtendDataFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintPure, Category = "游戏序列化", meta = (DefaultToSelf = Instance, HidePin = Instance))
	static FGameSerializerExtendDataContainer DefaultPreGameSave(UObject* Instance);
	
	UFUNCTION(BlueprintCallable, Category = "游戏序列化", meta = (DefaultToSelf = Instance, HidePin = Instance))
	static void DefaultPostLoadGame(UObject* Instance, const FGameSerializerExtendDataContainer& ExtendData);
};

struct GAMESERIALIZER_API FGameSerializerExtendDataFactory
{
	virtual FGameSerializerExtendDataContainer WhenGamePreSave(UObject* Instance) = 0;
	virtual void WhenGamePostLoad(UObject* Instance, const FGameSerializerExtendDataContainer& ExtendData) = 0;
	virtual ~FGameSerializerExtendDataFactory() {}
};

namespace GameSerializerExtendDataFactory
{
	extern GAMESERIALIZER_API TMap<UClass*, TSharedRef<FGameSerializerExtendDataFactory>> Factory;
	
	GAMESERIALIZER_API FGameSerializerExtendDataFactory* FindFactory(UObject* Instance);
	template<typename T>
	void RegisterFactory(UClass* Type) { Factory.Add(Type, MakeShared<T>()); }
	inline void UnregisterFactory(UClass* Type) { Factory.Remove(Type); }
}

USTRUCT()
struct GAMESERIALIZER_API FActorGameSerializerExtendData : public FGameSerializerExtendData
{
	GENERATED_BODY()
public:
};

struct GAMESERIALIZER_API FActorGameSerializerExtendDataFactory : public FGameSerializerExtendDataFactory
{
	FGameSerializerExtendDataContainer WhenGamePreSave(UObject* Instance) override;
	void WhenGamePostLoad(UObject* Instance, const FGameSerializerExtendDataContainer& ExtendData) override;
};
