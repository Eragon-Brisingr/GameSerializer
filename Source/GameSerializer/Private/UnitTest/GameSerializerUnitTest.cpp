// Fill out your copyright notice in the Description page of Project Settings.


#include "GameSerializerUnitTest.h"

#include "GameSerializerCore.h"

void UParentObject::WhenGamePostLoad_Implementation(const FGameSerializerExtendDataContainer& ExtendData)
{
	const FGameSerializerExtendData_UnitTest& UnitTest = static_cast<const FGameSerializerExtendData_UnitTest&>(*ExtendData.ExtendData.Get());
	ensure(UnitTest.TestValue1 == 100);
}

FGameSerializerExtendDataContainer UParentObject::WhenGamePreSave_Implementation()
{
	FGameSerializerExtendData_UnitTest UnitTest;
	UnitTest.TestValue1 = 100;
	UnitTest.TestValue2.Add(4, TEXT("Test"));
	return FGameSerializerExtendDataContainer::Make(UnitTest);
}

void UGameSerializerUnitTest::ExecuteTest()
{
	FTestSerializeData SerializeData1;
	SerializeData1.Value0 = 3;
	SerializeData1.ArrayValue1 = { 3, 2, 1 };

	URootObject* RootObject = NewObject<URootObject>(NewObject<URootObject>());
	{
		RootObject->ParentObjects.Add_GetRef(NewObject<UParentObject>(RootObject));
		UParentObject* Object = RootObject->ParentObjects.Add_GetRef(NewObject<UParentObject>(RootObject));
		RootObject->ParentObjects.Add(Object);
		Object->ClassValue = UObject::StaticClass();
	}

	GameSerializerCore::FStructToJson GameSerializer;
	GameSerializer.AddObjects(TEXT("ObjectList"), { RootObject, RootObject, nullptr });
	GameSerializer.AddStruct(TEXT("FTestSerializeData"), SerializeData1);

	const TSharedRef<FJsonObject> JsonObject = GameSerializer.GetResultJson();

	FString JSONPayload;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>> > JsonWriter = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&JSONPayload, 0);
	FJsonSerializer::Serialize(JsonObject, JsonWriter);
	JsonWriter->Close();

	UE_LOG(LogTemp, Display, TEXT("%s"), *JSONPayload);

	GameSerializerCore::FJsonToStruct JsonToStruct(GetTransientPackage(), JsonObject);
	JsonToStruct.LoadAllData();
	TArray<UObject*> LoadedObjects = JsonToStruct.GetObjects(TEXT("ObjectList"));
}

FString UGameSerializerUnitTest::ActorsToJson(UObject* WorldContextObject, TArray<AActor*> Actors)
{
	GameSerializerCore::FStructToJson GameSerializer;
	GameSerializer.CheckFlags = CPF_SaveGame;
	GameSerializer.AddObjects(ActorsFieldName, reinterpret_cast<const TArray<UObject*>&>(Actors));
	return GameSerializerCore::JsonObjectToString(GameSerializer.GetResultJson());
}

TArray<AActor*> UGameSerializerUnitTest::JsonToActors(UObject* WorldContextObject, const FString& JsonString)
{
	ULevel* Level = WorldContextObject->GetWorld()->PersistentLevel;

	GameSerializerCore::FJsonToStruct JsonToStruct(Level, GameSerializerCore::StringToJsonObject(JsonString).ToSharedRef());
	JsonToStruct.CheckFlags = CPF_SaveGame;
	const TArray<UObject*> NewActors = JsonToStruct.GetObjects(ActorsFieldName);

	return reinterpret_cast<const TArray<AActor*>&>(NewActors);
}
