// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
// #include "GameSerializerCore.generated.h"

/**
 * 
 */

namespace GameSerializer
{
	using FObjectIdx = int32;

	constexpr EPropertyFlags DefaultCheckFlags = CPF_AllFlags;
	constexpr EPropertyFlags DefaultSkipFlags = CPF_None;
	
	struct FStructToJson
	{
		EPropertyFlags CheckFlags = DefaultCheckFlags;
		EPropertyFlags SkipFlags = DefaultSkipFlags;

		using FPersistentInstanceGraph = TMap<UObject*, FObjectIdx>;

		FStructToJson(const FPersistentInstanceGraph* PersistentInstanceGraph);

		void AddObject(const FString& FieldName, UObject* Object);

		void AddObjects(const FString& FieldName, TArray<UObject*> Objects);

		void AddStruct(const FString& FieldName, UScriptStruct* Struct, const void* Value);

		const TSharedRef<FJsonObject>& GetResultJson() const { return RootJsonObject; }

		const FPersistentInstanceGraph& GetInstanceGraph() const { return ObjectIdxMap; }
	private:
		const FPersistentInstanceGraph* PersistentInstanceGraph;

		TSharedRef<FJsonObject> RootJsonObject = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> AssetJsonObject = MakeShared<FJsonObject>();

		struct FOuterData
		{
			FOuterData(UObject* Outer, const TSharedRef<FJsonObject>& OuterJsonObject)
				: Outer(Outer), OuterJsonObject(OuterJsonObject)
			{}

			UObject* Outer;
			const TSharedRef<FJsonObject>& OuterJsonObject;
		};
		TArray<FOuterData> OuterChain = TArray<FOuterData>();

		FObjectIdx ObjectUniqueIdx = 0;
		FObjectIdx AssetUniqueIdx = 0;

		TMap<const UObject*, FObjectIdx> AssetIdxMap;
		TMap<UObject*, FObjectIdx> ObjectIdxMap;

		FObjectIdx GetAssetIndex(const UObject* Asset);

		void ObjectToJsonObject(const TSharedRef<FJsonObject>& JsonObject, UObject* Object);

		TSharedPtr<FJsonValue> ConvertObjectToJson(FProperty* Property, const void* Value);
	};

	struct FJsonToStruct
	{
		EPropertyFlags CheckFlags = DefaultCheckFlags;
		EPropertyFlags SkipFlags = DefaultSkipFlags;
		
		using FPersistentInstanceGraph = TMap<FObjectIdx, UObject*>;

		FJsonToStruct(UObject* Outer, const TSharedRef<FJsonObject>& RootJsonObject, const FPersistentInstanceGraph* PersistentInstanceGraph);

		TArray<UObject*> GetObjects(const FString FieldName) const;

		UObject* JsonObjectToInstanceObject(const TSharedRef<FJsonObject>& JsonObject, FObjectIdx ObjectIdx);

		void SyncAllInstanceJsonData();

	private:
		UObject* GetObjectByIdx(FObjectIdx ObjectIdx) const;
		
		UObject* Outer;
		TSharedRef<FJsonObject> RootJsonObject;
		TArray<UObject*> AssetsArray = { nullptr };
		TArray<UObject*> ObjectsArray = { nullptr };
		const FPersistentInstanceGraph* PersistentInstanceGraph;

		TArray<AActor*> SpawnedActors;

		struct FInstancedObjectData
		{
			UObject* Object;
			TSharedRef<FJsonObject> JsonObject;
		};
		TArray<FInstancedObjectData> InstancedObjectDatas;
	};

	FString JsonObjectToString(const TSharedRef<FJsonObject>& JsonObject);
	TSharedPtr<FJsonObject> StringToJsonObject(const FString& JsonString);
}
