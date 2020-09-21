// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameSerializerInterface.generated.h"

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
	
	template<typename T>
	static FGameSerializerExtendDataContainer Make(const T& ExtendData)
	{
		FGameSerializerExtendDataContainer Container;
		Container.Struct = T::StaticStruct();
		Container.ExtendData = MakeShared<T>(ExtendData);
		return Container;
	}
};

// This class does not need to be modified.
UINTERFACE(MinimalAPI)
class UGameSerializerInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class GAMESERIALIZER_API IGameSerializerInterface
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:
	UFUNCTION(BlueprintNativeEvent, Category = "游戏序列化")
	void WhenGamePostLoad(const FGameSerializerExtendDataContainer& ExtendData);
	virtual void WhenGamePostLoad_Implementation(const FGameSerializerExtendDataContainer& ExtendData) {}
	static void WhenGamePostLoad(UObject* Obj, const FGameSerializerExtendDataContainer& ExtendData) { Execute_WhenGamePostLoad(Obj, ExtendData); }

	UFUNCTION(BlueprintNativeEvent, Category = "游戏序列化")
	FGameSerializerExtendDataContainer WhenGamePreSave();
	virtual FGameSerializerExtendDataContainer WhenGamePreSave_Implementation() { return FGameSerializerExtendDataContainer(); }
	static FGameSerializerExtendDataContainer WhenGamePreSave(UObject* Obj) { return Execute_WhenGamePreSave(Obj); }
};
