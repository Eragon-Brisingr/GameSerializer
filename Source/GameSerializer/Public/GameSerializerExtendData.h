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
	TObjectPtr<const UScriptStruct> Struct = nullptr;

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
		if (Struct && ensure(Struct->IsChildOf(T::StaticStruct())))
		{
			return static_cast<const T&>(*ExtendData.Get());
		}
		static const T DefaultValue;
		return DefaultValue;
	}
};

struct FGameSerializerNetNotifyData
{
	FProperty* Property;
	UFunction* RepNotifyFunc;
};

USTRUCT(BlueprintType, BlueprintInternalUseOnly)
struct GAMESERIALIZER_API FGameSerializerCallRepNotifyFunc
{
	GENERATED_BODY()
	public:
	FGameSerializerCallRepNotifyFunc() = default;
	FGameSerializerCallRepNotifyFunc(UObject* InstancedObject, const TArray<FGameSerializerNetNotifyData>& NetNotifyDatas)
		: InstancedObject(InstancedObject), NetNotifyDatas(&NetNotifyDatas)
	{}

	void CallRepNotifyFunc() const;
	static bool IsGameSerializerCall();
	private:
	UObject* InstancedObject = nullptr;
	const TArray<FGameSerializerNetNotifyData>* NetNotifyDatas = nullptr;
#if DO_CHECK
	mutable bool bCalled = false;
#endif
	static bool bIsGameSerializerCall;
};

UCLASS()
class UGameSerializerCallRepNotifyFuncFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = "游戏序列化")
	static void CallRepNotifyFunc(const FGameSerializerCallRepNotifyFunc& CallRepNotifyFunc) { CallRepNotifyFunc.CallRepNotifyFunc(); }

	UFUNCTION(BlueprintPure, Category = "游戏序列化")
	static bool IsGameSerializerCall() { return FGameSerializerCallRepNotifyFunc::IsGameSerializerCall(); }
};

UCLASS()
class UGameSerializerExtendDataFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintPure, Category = "游戏序列化", meta = (DefaultToSelf = Instance, HidePin = Instance))
	static FGameSerializerExtendDataContainer DefaultPreGameSave(UObject* Instance);
	
	UFUNCTION(BlueprintCallable, Category = "游戏序列化", meta = (DefaultToSelf = Instance, HidePin = Instance))
	static void DefaultPostLoadGame(UObject* Instance, const FGameSerializerExtendDataContainer& ExtendData, const FGameSerializerCallRepNotifyFunc& CallRepNotifyFunc);
};

struct GAMESERIALIZER_API FGameSerializerExtendDataFactory
{
	virtual FGameSerializerExtendDataContainer WhenGamePreSave(UObject* Instance) = 0;
	virtual void WhenGamePostLoad(UObject* Instance, const FGameSerializerExtendDataContainer& ExtendData, const FGameSerializerCallRepNotifyFunc& CallRepNotifyFunc) = 0;
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
	UPROPERTY(SaveGame)
	TObjectPtr<APawn> Instigator = nullptr;

	void SaveData(const AActor* Actor);
	void LoadData(AActor* Actor) const;
};

struct GAMESERIALIZER_API FActorGameSerializerExtendDataFactory : public FGameSerializerExtendDataFactory
{
	FGameSerializerExtendDataContainer WhenGamePreSave(UObject* Instance) override;
	void WhenGamePostLoad(UObject* Instance, const FGameSerializerExtendDataContainer& ExtendData, const FGameSerializerCallRepNotifyFunc& CallRepNotifyFunc) override;
};
