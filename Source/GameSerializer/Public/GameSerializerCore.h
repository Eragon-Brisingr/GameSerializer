// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include <Dom/JsonValue.h>
// #include "GameSerializerCore.generated.h"

/**
 * 
 */
namespace GameSerializerContext
{
	extern FIntVector WorldOffset;
}

struct FGameSerializerNetNotifyData;

namespace GameSerializerCore
{
	using FObjectIdx = int32;

	constexpr EPropertyFlags DefaultCheckFlags = CPF_AllFlags;
	constexpr EPropertyFlags DefaultSkipFlags = CPF_None;
	
	struct FStructToJson
	{
		EPropertyFlags CheckFlags = DefaultCheckFlags;
		EPropertyFlags SkipFlags = DefaultSkipFlags;

		FStructToJson();

		void AddObjects(const FString& FieldName, TArray<UObject*> Objects);
		void AddObject(const FString& FieldName, UObject* Object);

		void AddStruct(const FString& FieldName, UScriptStruct* Struct, const void* Value, const void* DefaultValue) { AddStruct(RootJsonObject, FieldName, Struct, Value, DefaultValue); }
		template<typename T>
		void AddStruct(const FString& FieldName, const T& Value)
		{
			AddStruct<T>(RootJsonObject, FieldName, Value);
		}

		const TSharedRef<FJsonObject>& GetResultJson() const { return RootJsonObject; }
	private:
		TSharedRef<FJsonObject> RootJsonObject = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> ExternalJsonObject = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> DynamicJsonObject = MakeShared<FJsonObject>();

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
		FObjectIdx ExternalObjectUniqueIdx = 0;

		TMap<const UObject*, FObjectIdx> ExternalObjectIdxMap;
		TMap<UObject*, FObjectIdx> ObjectIdxMap;

		FObjectIdx GetExternalObjectIndex(const UObject* ExternalObject);

		FObjectIdx ObjectToJsonObject(const TSharedRef<FJsonObject>& JsonObject, UObject* Object);

		FObjectIdx ConvertObjectToObjectIdx(UObject* Object);
		TSharedPtr<FJsonValue> ConvertObjectToJson(FProperty* Property, const void* Value, const void* Default, bool& bSameValue);

		void AddStruct(const TSharedRef<FJsonObject>& JsonObject, const FString& FieldName, UScriptStruct* Struct, const void* Value, const void* DefaultValue);
		template<typename T>
		void AddStruct(const TSharedRef<FJsonObject>& JsonObject, const FString& FieldName, const T& Value)
		{
			const static T DefaultValue{};
			AddStruct(JsonObject, FieldName, TBaseStructure<T>::Get(), &Value, &DefaultValue);
		}
	};

	struct FJsonToStruct
	{
		struct FSpawnedActorData
		{
			TWeakObjectPtr<AActor> SpawnedActor;
			// 在关卡进行流式加载完毕前动态Spawn的Actor会在ULevel::InitializeNetworkActors被判断为稳定命名对象（bNetStartup）
			// 通过设置bNetLoadOnClient跳过bNetStartup设置
			uint8 bNetLoadOnClient : 1;
		};

		struct FInstancedObjectData
		{
			TWeakObjectPtr<UObject> Object;
			TSharedRef<FJsonObject> JsonObject;
			TArray<struct FGameSerializerNetNotifyData> AllNetNotifyData;
		};
	public:
		EPropertyFlags CheckFlags = DefaultCheckFlags;
		EPropertyFlags SkipFlags = DefaultSkipFlags;
		
		FJsonToStruct(UObject* Outer, const TSharedRef<FJsonObject>& RootJsonObject);

		void LoadAllDataImmediately()
		{
			LoadExternalObject();
			InstanceDynamicObject();
			LoadDynamicObjectJsonData();
			DynamicActorFinishSpawning();
			RestoreDynamicActorSpawnedData();
			LoadDynamicObjectExtendData();
		}
		
		void LoadExternalObject();
		void InstanceDynamicObject();
		void LoadDynamicObjectJsonData();
		void DynamicActorFinishSpawning();
		void RestoreDynamicActorSpawnedData();
		void LoadDynamicObjectExtendData();

		void RetargetDynamicObjectName(const FString& FieldName, const FName& NewName);

		const TArray<FSpawnedActorData>& GetSpawnedActors() const { return SpawnedActors; }
		const TArray<FInstancedObjectData>& GetAllInstancedObjectData() const { return AllInstancedObjectData; }
		void ExecutePostLoad(UObject* LoadedObject, const FInstancedObjectData& InstancedObjectData) const;

		TArray<UObject*> GetObjects(const FString& FieldName) const;
		UObject* GetObject(const FString& FieldName) const;

		void GetStruct(const FString& FieldName, UScriptStruct* Struct, void* Value) { GetStruct(RootJsonObject, FieldName, Struct, Value); }
		template<typename T>
		T GetStruct(const FString& FieldName)
		{
			return GetStruct<T>(RootJsonObject, FieldName);
		}
	private:
		UObject* JsonObjectToInstanceObject(const TSharedRef<FJsonObject>& JsonObject, FObjectIdx ObjectIdx);
		UObject* GetObjectByIdx(FObjectIdx ObjectIdx) const;
		bool JsonObjectIdxToObject(const TSharedPtr<FJsonValue>& JsonValue, FProperty* Property, void* OutValue) const;
		
		UObject* Outer;
		TSharedRef<FJsonObject> RootJsonObject;
		TArray<UObject*> ExternalObjectsArray = { nullptr };
		TArray<UObject*> ObjectsArray = { nullptr };

		TArray<FSpawnedActorData> SpawnedActors;
		TArray<FInstancedObjectData> AllInstancedObjectData;

		void GetStruct(const TSharedRef<FJsonObject>& JsonObject, const FString& FieldName, UScriptStruct* Struct, void* Value) const;
		template<typename T>
		T GetStruct(const TSharedRef<FJsonObject>& JsonObject, const FString& FieldName) const
		{
			static const T DefaultValue{};
			T Value{ DefaultValue };
			GetStruct(JsonObject, FieldName, TBaseStructure<T>::Get(), &Value);
			return Value;
		}
	};

	FString JsonObjectToString(const TSharedRef<FJsonObject>& JsonObject);
	TSharedPtr<FJsonObject> StringToJsonObject(const FString& JsonString);
}
