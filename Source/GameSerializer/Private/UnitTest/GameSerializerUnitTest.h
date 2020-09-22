// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameSerializerInterface.h"
#include "UObject/NoExportTypes.h"
#include "GameSerializerUnitTest.generated.h"

USTRUCT()
struct FGameSerializerExtendData_UnitTest : public FGameSerializerExtendData
{
	GENERATED_BODY()
public:
	UPROPERTY(SaveGame)
	int32 TestValue1 = 0;

	UPROPERTY(SaveGame)
	TMap<int32, FString> TestValue2 = { {4, "Test"} };
};

USTRUCT()
struct FTestSerializeData
{
	GENERATED_BODY()
public:
	UPROPERTY(SaveGame)
	int32 Value0 = 3;
	
	UPROPERTY(SaveGame)
	float Value1 = 5.f;

	UPROPERTY(SaveGame)
	UObject* Value2 = nullptr;

	UPROPERTY(SaveGame)
	TArray<int32> ArrayValue1 = { 1, 2 };
};

UCLASS()
class UChildObject : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(SaveGame)
	int32 TestValue1 = 3;
};

UCLASS()
class UParentObject : public UObject, public IGameSerializerInterface
{
	GENERATED_BODY()
public:
	UParentObject()
	{
		ChildObject = CreateDefaultSubobject<UChildObject>(TEXT("Child"));
	}

	void WhenGamePostLoad_Implementation(const FGameSerializerExtendDataContainer& ExtendData) override;
	FGameSerializerExtendDataContainer WhenGamePreSave_Implementation() override;

	UPROPERTY(SaveGame)
	UChildObject* ChildObject;

	UPROPERTY(SaveGame)
	UClass* ClassValue = nullptr; 
};

UCLASS()
class URootObject : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(SaveGame)
	TArray<UParentObject*> ParentObjects;

	UPROPERTY(SaveGame)
	FTestSerializeData TestSerializeData;
};

/**
 * 
 */
UCLASS()
class UGameSerializerUnitTest : public UObject
{
	GENERATED_BODY()
public:
	static void ExecuteTest();

	static constexpr TCHAR ActorsFieldName[] = TEXT("Actors");
	
	UFUNCTION(BlueprintCallable, meta = (WorldContext = WorldContextObject))
	static FString ActorsToJson(UObject* WorldContextObject, TArray<AActor*> Actors);

	UFUNCTION(BlueprintCallable, meta = (WorldContext = WorldContextObject))
	static TArray<AActor*> JsonToActors(UObject* WorldContextObject, const FString& JsonString);
};
