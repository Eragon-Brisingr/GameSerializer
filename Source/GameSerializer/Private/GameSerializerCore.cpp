// Fill out your copyright notice in the Description page of Project Settings.


#include "GameSerializerCore.h"
#include <JsonObjectWrapper.h>
#include <Internationalization/Culture.h>
#include <Misc/ScopeExit.h>
#include <Engine/SimpleConstructionScript.h>
#include <Engine/SCS_Node.h>
#include <Engine/LevelScriptActor.h>
#include <Dom/JsonObject.h>
#include <Policies/CondensedJsonPrintPolicy.h>
#include <Serialization/JsonSerializer.h>
#include <Serialization/JsonWriter.h>

#include "GameSerializerInterface.h"
#include "GameSerializer_Log.h"

namespace CustomJsonConverter
{
	DECLARE_DELEGATE_RetVal_FourParams(TSharedPtr<FJsonValue>, FCustomExportCallback, FProperty* /* Property */, const void* /* Value */, const void* /*Default*/, bool& /*bSameValue*/);

	struct StructToJson
	{
		static TSharedPtr<FJsonValue> ConvertScalarFPropertyToJsonValue(FProperty* Property, const void* Value, const void* DefaultValue, bool& bSameValue, int64 CheckFlags, int64 SkipFlags, const FCustomExportCallback& ExportCb)
		{
			bSameValue = false;
			
			// See if there's a custom export callback first, so it can override default behavior
			if (ExportCb.IsBound())
			{
				TSharedPtr<FJsonValue> CustomValue = ExportCb.Execute(Property, Value, DefaultValue, bSameValue);
				if (CustomValue.IsValid())
				{
					return CustomValue;
				}
				// fall through to default cases
			}

			if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
			{
				// export enums as strings
				UEnum* EnumDef = EnumProperty->GetEnum();
				const int64 EnumValue = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(Value);
				FString StringValue = EnumDef->GetNameStringByValue(EnumValue);
				if (DefaultValue)
				{
					const int64 DefaultEnumValue = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(DefaultValue);
					bSameValue = EnumValue == DefaultEnumValue;
				}
				return MakeShared<FJsonValueString>(StringValue);
			}
			else if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
			{
				// see if it's an enum
				UEnum* EnumDef = NumericProperty->GetIntPropertyEnum();
				if (EnumDef != nullptr)
				{
					// export enums as strings
					const int64 EnumValue = NumericProperty->GetSignedIntPropertyValue(Value);
					const FString StringValue = EnumDef->GetNameStringByValue(EnumValue);
					if (DefaultValue)
					{
						const int64 DefaultEnumValue = NumericProperty->GetSignedIntPropertyValue(DefaultValue);
						bSameValue = EnumValue == DefaultEnumValue;
					}
					return MakeShared<FJsonValueString>(StringValue);
				}

				// We want to export numbers as numbers
				if (NumericProperty->IsFloatingPoint())
				{
					const float Number = NumericProperty->GetFloatingPointPropertyValue(Value);
					if (DefaultValue)
					{
						const float DefaultNumber = NumericProperty->GetFloatingPointPropertyValue(DefaultValue);
						bSameValue = Number == DefaultNumber;
					}
					return MakeShared<FJsonValueNumber>(Number);
				}
				else if (NumericProperty->IsInteger())
				{
					const int64 Number = NumericProperty->GetSignedIntPropertyValue(Value);
					if (DefaultValue)
					{
						const int64 DefaultNumber = NumericProperty->GetSignedIntPropertyValue(DefaultValue);
						bSameValue = Number == DefaultNumber;
					}
					return MakeShared<FJsonValueNumber>(NumericProperty->GetSignedIntPropertyValue(Value));
				}

				// fall through to default
			}
			else if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
			{
				// Export bools as bools
				const bool BoolValue = BoolProperty->GetPropertyValue(Value);
				if (DefaultValue)
				{
					const bool DefaultBoolValue = BoolProperty->GetPropertyValue(DefaultValue);
					bSameValue = BoolValue == DefaultBoolValue;
				}
				return MakeShared<FJsonValueBoolean>(BoolValue);
			}
			else if (FStrProperty* StringProperty = CastField<FStrProperty>(Property))
			{
				const FString StringValue = StringProperty->GetPropertyValue(Value);
				if (DefaultValue)
				{
					const FString DefaultStringValue = StringProperty->GetPropertyValue(DefaultValue);
					bSameValue = StringValue == DefaultStringValue;
				}
				return MakeShared<FJsonValueString>(StringValue);
			}
			else if (FTextProperty* TextProperty = CastField<FTextProperty>(Property))
			{
				const FText TextValue = TextProperty->GetPropertyValue(Value);
				if (DefaultValue)
				{
					const FText DefaultTextValue = TextProperty->GetPropertyValue(DefaultValue);
					bSameValue = TextValue.CompareTo(DefaultTextValue) == 0;
				}
				return MakeShared<FJsonValueString>(TextProperty->GetPropertyValue(Value).ToString());
			}
			else if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
			{
				TArray< TSharedPtr<FJsonValue> > Out;
				FScriptArrayHelper Helper(ArrayProperty, Value);
				TOptional<FScriptArrayHelper> DefaultValueHelper;
				if (DefaultValue)
				{
					DefaultValueHelper = FScriptArrayHelper(ArrayProperty, DefaultValue);
					bSameValue = Helper.Num() == DefaultValueHelper->Num();
				}
				else
				{
					bSameValue = true;
				}
				for (int32 i = 0, n = Helper.Num(); i < n; ++i)
				{
					const bool IsValidDefaultValueIdx = DefaultValue ? DefaultValueHelper->IsValidIndex(i) : false;
					bool bElementSameValue;
					TSharedPtr<FJsonValue> Elem = UPropertyToJsonValue(ArrayProperty->Inner, Helper.GetRawPtr(i), IsValidDefaultValueIdx ? DefaultValueHelper->GetRawPtr(i) : nullptr, bElementSameValue, CheckFlags & (~CPF_ParmFlags), SkipFlags, ExportCb);
					bSameValue &= bElementSameValue;
					if (Elem.IsValid())
					{
						// add to the array
						Out.Push(Elem);
					}
				}
				return MakeShared<FJsonValueArray>(Out);
			}
			else if (FSetProperty* SetProperty = CastField<FSetProperty>(Property))
			{
				TArray< TSharedPtr<FJsonValue> > Out;
				FScriptSetHelper Helper(SetProperty, Value);
				
				TOptional<FScriptSetHelper> DefaultValueHelper;
				if (DefaultValue)
				{
					DefaultValueHelper = FScriptSetHelper(SetProperty, DefaultValue);
					bSameValue = Helper.Num() == DefaultValueHelper->Num();
				}
				else
				{
					bSameValue = true;
				}
				for (int32 i = 0, n = Helper.Num(); n; ++i)
				{
					if (Helper.IsValidIndex(i))
					{
						const bool IsValidDefaultValueIdx = DefaultValue ? DefaultValueHelper->IsValidIndex(i) : false;
						bool bElementSameValue;
						TSharedPtr<FJsonValue> Elem = UPropertyToJsonValue(SetProperty->ElementProp, Helper.GetElementPtr(i), IsValidDefaultValueIdx ? DefaultValueHelper->GetElementPtr(i) : nullptr, bElementSameValue, CheckFlags & (~CPF_ParmFlags), SkipFlags, ExportCb);
						bSameValue &= bElementSameValue;
						if (Elem.IsValid())
						{
							// add to the array
							Out.Push(Elem);
						}

						--n;
					}
				}
				return MakeShared<FJsonValueArray>(Out);
			}
			else if (FMapProperty* MapProperty = CastField<FMapProperty>(Property))
			{
				TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();

				FScriptMapHelper Helper(MapProperty, Value);

				TOptional<FScriptMapHelper> DefaultValueHelper;
				if (DefaultValue)
				{
					DefaultValueHelper = FScriptMapHelper(MapProperty, DefaultValue);
					bSameValue = Helper.Num() == DefaultValueHelper->Num();
				}
				else
				{
					bSameValue = true;
				}
				for (int32 i = 0, n = Helper.Num(); n; ++i)
				{
					if (Helper.IsValidIndex(i))
					{
						const bool IsValidDefaultValueIdx = DefaultValue ? DefaultValueHelper->IsValidIndex(i) : false;
						bool bKeySameValue;
						TSharedPtr<FJsonValue> KeyElement = UPropertyToJsonValue(MapProperty->KeyProp, Helper.GetKeyPtr(i), IsValidDefaultValueIdx ? DefaultValueHelper->GetKeyPtr(i) : nullptr, bKeySameValue, CheckFlags & (~CPF_ParmFlags), SkipFlags, ExportCb);
						bool bValueSameValue;
						TSharedPtr<FJsonValue> ValueElement = UPropertyToJsonValue(MapProperty->ValueProp, Helper.GetValuePtr(i), IsValidDefaultValueIdx ? DefaultValueHelper->GetValuePtr(i) : nullptr, bValueSameValue, CheckFlags & (~CPF_ParmFlags), SkipFlags, ExportCb);
						bSameValue &= bKeySameValue && bValueSameValue;
						if (KeyElement.IsValid() && ValueElement.IsValid())
						{
							FString KeyString;
							if (!KeyElement->TryGetString(KeyString))
							{
								MapProperty->KeyProp->ExportTextItem(KeyString, Helper.GetKeyPtr(i), nullptr, nullptr, 0);
								if (KeyString.IsEmpty())
								{
									UE_LOG(GameSerializer_Log, Error, TEXT("Unable to convert key to string for property %s."), *MapProperty->GetName())
										KeyString = FString::Printf(TEXT("Unparsed Key %d"), i);
								}
							}

							Out->SetField(KeyString, ValueElement);
						}

						--n;
					}
				}

				return MakeShared<FJsonValueObject>(Out);
			}
			else if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				UScriptStruct::ICppStructOps* TheCppStructOps = StructProperty->Struct->GetCppStructOps();
				// Intentionally exclude the JSON Object wrapper, which specifically needs to export JSON in an object representation instead of a string
				if (StructProperty->Struct != FJsonObjectWrapper::StaticStruct() && TheCppStructOps && TheCppStructOps->HasExportTextItem())
				{
					FString OutValueStr;
					TheCppStructOps->ExportTextItem(OutValueStr, Value, nullptr, nullptr, PPF_None, nullptr);
					return MakeShared<FJsonValueString>(OutValueStr);
				}

				TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
				if (UStructToJsonAttributes(StructProperty->Struct, Value, DefaultValue, bSameValue, Out->Values, CheckFlags & (~CPF_ParmFlags), SkipFlags, ExportCb))
				{
					return MakeShared<FJsonValueObject>(Out);
				}
				// fall through to default
			}
			else
			{
				// Default to export as string for everything else
				FString StringValue;
				Property->ExportTextItem(StringValue, Value, DefaultValue, nullptr, PPF_None);

				if (DefaultValue)
				{
					bSameValue = Property->Identical(Value, DefaultValue);
				}
				
				return MakeShared<FJsonValueString>(StringValue);
			}

			// invalid
			return TSharedPtr<FJsonValue>();
		}

		static TSharedPtr<FJsonValue> UPropertyToJsonValue(FProperty* Property, const void* Value, const void* DefaultValue, bool& bSameValue, int64 CheckFlags, int64 SkipFlags, const FCustomExportCallback& ExportCb)
		{
			if (Property->ArrayDim == 1)
			{
				return ConvertScalarFPropertyToJsonValue(Property, Value, DefaultValue, bSameValue, CheckFlags, SkipFlags, ExportCb);
			}

			bSameValue = true;
			TArray< TSharedPtr<FJsonValue> > Array;
			for (int Index = 0; Index != Property->ArrayDim; ++Index)
			{
				const int32 Offset = Index * Property->ElementSize;
				bool bElementSameValue;
				Array.Add(ConvertScalarFPropertyToJsonValue(Property, (char*)Value + Offset, (char*)DefaultValue + Offset, bElementSameValue, CheckFlags, SkipFlags, ExportCb));
				bSameValue &= bElementSameValue;
			}
			return MakeShared<FJsonValueArray>(Array);
		}

		static bool UStructToJsonAttributes(const UStruct* StructDefinition, const void* Struct, const void* DefaultStruct, bool& bSameValue, TMap< FString, TSharedPtr<FJsonValue> >& OutJsonAttributes, int64 CheckFlags, int64 SkipFlags, const FCustomExportCallback& ExportCb)
		{
			if (SkipFlags == 0)
			{
				// If we have no specified skip flags, skip deprecated, transient and skip serialization by default when writing
				SkipFlags |= CPF_Deprecated | CPF_Transient;
			}

			if (StructDefinition == FJsonObjectWrapper::StaticStruct())
			{
				// Just copy it into the object
				const FJsonObjectWrapper* ProxyObject = static_cast<const FJsonObjectWrapper*>(Struct);

				if (ProxyObject->JsonObject.IsValid())
				{
					OutJsonAttributes = ProxyObject->JsonObject->Values;
				}
				return true;
			}

			bSameValue = true;
			for (TFieldIterator<FProperty> It(StructDefinition); It; ++It)
			{
				FProperty* Property = *It;

				// Check to see if we should ignore this property
				if (CheckFlags != 0 && !Property->HasAnyPropertyFlags(CheckFlags))
				{
					continue;
				}
				if (Property->HasAnyPropertyFlags(SkipFlags))
				{
					continue;
				}

				FString VariableName = Property->GetName();
				const void* Value = Property->ContainerPtrToValuePtr<uint8>(Struct);
				const void* DefaultValue = DefaultStruct ? Property->ContainerPtrToValuePtr<uint8>(DefaultStruct) : nullptr;

				bool bPropertySameValue;
				// convert the property to a FJsonValue
				TSharedPtr<FJsonValue> JsonValue = UPropertyToJsonValue(Property, Value, DefaultValue, bPropertySameValue, CheckFlags, SkipFlags, ExportCb);
				bSameValue &= bPropertySameValue;
				if (!JsonValue.IsValid())
				{
					FFieldClass* PropClass = Property->GetClass();
					UE_LOG(GameSerializer_Log, Error, TEXT("UStructToJsonObject - Unhandled property type '%s': %s"), *PropClass->GetName(), *Property->GetPathName());
					return false;
				}

				if (bPropertySameValue == false)
				{
					// set the value on the output object
					OutJsonAttributes.Add(VariableName, JsonValue);
				}
			}

			return true;
		}
	};

	DECLARE_DELEGATE_RetVal_ThreeParams(bool, FCustomImportCallback, const TSharedPtr<FJsonValue>& /*JsonValue*/, FProperty* /*Property*/, void* /*OutValue*/);

	struct JsonToStruct
	{
		static bool GetTextFromObject(const TSharedRef<FJsonObject>& Obj, FText& TextOut)
		{
			// get the prioritized culture name list
			FCultureRef CurrentCulture = FInternationalization::Get().GetCurrentCulture();
			TArray<FString> CultureList = CurrentCulture->GetPrioritizedParentCultureNames();

			// try to follow the fall back chain that the engine uses
			FString TextString;
			for (const FString& CultureCode : CultureList)
			{
				if (Obj->TryGetStringField(CultureCode, TextString))
				{
					TextOut = FText::FromString(TextString);
					return true;
				}
			}

			// try again but only search on the locale region (in the localized data). This is a common omission (i.e. en-US source text should be used if no en is defined)
			for (const FString& LocaleToMatch : CultureList)
			{
				int32 SeparatorPos;
				// only consider base language entries in culture chain (i.e. "en")
				if (!LocaleToMatch.FindChar('-', SeparatorPos))
				{
					for (const auto& Pair : Obj->Values)
					{
						// only consider coupled entries now (base ones would have been matched on first path) (i.e. "en-US")
						if (Pair.Key.FindChar('-', SeparatorPos))
						{
							if (Pair.Key.StartsWith(LocaleToMatch))
							{
								TextOut = FText::FromString(Pair.Value->AsString());
								return true;
							}
						}
					}
				}
			}

			// no luck, is this possibly an unrelated json object?
			return false;
		}

		static bool ConvertScalarJsonValueToFPropertyWithContainer(const TSharedPtr<FJsonValue>& JsonValue, FProperty* Property, void* OutValue, const UStruct* ContainerStruct, void* Container, int64 CheckFlags, int64 SkipFlags, const FCustomImportCallback& CustomImportCallback)
		{
			if (CustomImportCallback.IsBound())
			{
				if (CustomImportCallback.Execute(JsonValue, Property, OutValue))
				{
					return true;
				}
			}

			if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
			{
				if (JsonValue->Type == EJson::String)
				{
					// see if we were passed a string for the enum
					const UEnum* Enum = EnumProperty->GetEnum();
					check(Enum);
					FString StrValue = JsonValue->AsString();
					int64 IntValue = Enum->GetValueByName(FName(*StrValue));
					if (IntValue == INDEX_NONE)
					{
						UE_LOG(GameSerializer_Log, Error, TEXT("JsonValueToUProperty - Unable import enum %s from string value %s for property %s"), *Enum->CppType, *StrValue, *Property->GetNameCPP());
						return false;
					}
					EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(OutValue, IntValue);
				}
				else
				{
					// AsNumber will log an error for completely inappropriate types (then give us a default)
					EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(OutValue, (int64)JsonValue->AsNumber());
				}
			}
			else if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
			{
				if (NumericProperty->IsEnum() && JsonValue->Type == EJson::String)
				{
					// see if we were passed a string for the enum
					const UEnum* Enum = NumericProperty->GetIntPropertyEnum();
					check(Enum); // should be assured by IsEnum()
					FString StrValue = JsonValue->AsString();
					int64 IntValue = Enum->GetValueByName(FName(*StrValue));
					if (IntValue == INDEX_NONE)
					{
						UE_LOG(GameSerializer_Log, Error, TEXT("JsonValueToUProperty - Unable import enum %s from string value %s for property %s"), *Enum->CppType, *StrValue, *Property->GetNameCPP());
						return false;
					}
					NumericProperty->SetIntPropertyValue(OutValue, IntValue);
				}
				else if (NumericProperty->IsFloatingPoint())
				{
					// AsNumber will log an error for completely inappropriate types (then give us a default)
					NumericProperty->SetFloatingPointPropertyValue(OutValue, JsonValue->AsNumber());
				}
				else if (NumericProperty->IsInteger())
				{
					if (JsonValue->Type == EJson::String)
					{
						// parse string -> int64 ourselves so we don't lose any precision going through AsNumber (aka double)
						NumericProperty->SetIntPropertyValue(OutValue, FCString::Atoi64(*JsonValue->AsString()));
					}
					else
					{
						// AsNumber will log an error for completely inappropriate types (then give us a default)
						NumericProperty->SetIntPropertyValue(OutValue, (int64)JsonValue->AsNumber());
					}
				}
				else
				{
					UE_LOG(GameSerializer_Log, Error, TEXT("JsonValueToUProperty - Unable to set numeric property type %s for property %s"), *Property->GetClass()->GetName(), *Property->GetNameCPP());
					return false;
				}
			}
			else if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
			{
				// AsBool will log an error for completely inappropriate types (then give us a default)
				BoolProperty->SetPropertyValue(OutValue, JsonValue->AsBool());
			}
			else if (FStrProperty* StringProperty = CastField<FStrProperty>(Property))
			{
				// AsString will log an error for completely inappropriate types (then give us a default)
				StringProperty->SetPropertyValue(OutValue, JsonValue->AsString());
			}
			else if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
			{
				if (JsonValue->Type == EJson::Array)
				{
					TArray< TSharedPtr<FJsonValue> > ArrayValue = JsonValue->AsArray();
					int32 ArrLen = ArrayValue.Num();

					// make the output array size match
					FScriptArrayHelper Helper(ArrayProperty, OutValue);
					Helper.Resize(ArrLen);

					// set the property values
					for (int32 i = 0; i < ArrLen; ++i)
					{
						const TSharedPtr<FJsonValue>& ArrayValueItem = ArrayValue[i];
						if (ArrayValueItem.IsValid() && !ArrayValueItem->IsNull())
						{
							if (!JsonValueToFPropertyWithContainer(ArrayValueItem, ArrayProperty->Inner, Helper.GetRawPtr(i), ContainerStruct, Container, CheckFlags & (~CPF_ParmFlags), SkipFlags, CustomImportCallback))
							{
								UE_LOG(GameSerializer_Log, Error, TEXT("JsonValueToUProperty - Unable to deserialize array element [%d] for property %s"), i, *Property->GetNameCPP());
								return false;
							}
						}
					}
				}
				else
				{
					UE_LOG(GameSerializer_Log, Error, TEXT("JsonValueToUProperty - Attempted to import TArray from non-array JSON key for property %s"), *Property->GetNameCPP());
					return false;
				}
			}
			else if (FMapProperty* MapProperty = CastField<FMapProperty>(Property))
			{
				if (JsonValue->Type == EJson::Object)
				{
					TSharedPtr<FJsonObject> ObjectValue = JsonValue->AsObject();

					FScriptMapHelper Helper(MapProperty, OutValue);

					check(ObjectValue);

					int32 MapSize = ObjectValue->Values.Num();
					Helper.EmptyValues(MapSize);

					// set the property values
					for (const auto& Entry : ObjectValue->Values)
					{
						if (Entry.Value.IsValid() && !Entry.Value->IsNull())
						{
							int32 NewIndex = Helper.AddDefaultValue_Invalid_NeedsRehash();

							TSharedPtr<FJsonValueString> TempKeyValue = MakeShared<FJsonValueString>(Entry.Key);

							const bool bKeySuccess = JsonValueToFPropertyWithContainer(TempKeyValue, MapProperty->KeyProp, Helper.GetKeyPtr(NewIndex), ContainerStruct, Container, CheckFlags & (~CPF_ParmFlags), SkipFlags, CustomImportCallback);
							const bool bValueSuccess = JsonValueToFPropertyWithContainer(Entry.Value, MapProperty->ValueProp, Helper.GetValuePtr(NewIndex), ContainerStruct, Container, CheckFlags & (~CPF_ParmFlags), SkipFlags, CustomImportCallback);

							if (!(bKeySuccess && bValueSuccess))
							{
								UE_LOG(GameSerializer_Log, Error, TEXT("JsonValueToUProperty - Unable to deserialize map element [key: %s] for property %s"), *Entry.Key, *Property->GetNameCPP());
								return false;
							}
						}
					}

					Helper.Rehash();
				}
				else
				{
					UE_LOG(GameSerializer_Log, Error, TEXT("JsonValueToUProperty - Attempted to import TMap from non-object JSON key for property %s"), *Property->GetNameCPP());
					return false;
				}
			}
			else if (FSetProperty* SetProperty = CastField<FSetProperty>(Property))
			{
				if (JsonValue->Type == EJson::Array)
				{
					TArray< TSharedPtr<FJsonValue> > ArrayValue = JsonValue->AsArray();
					int32 ArrLen = ArrayValue.Num();

					FScriptSetHelper Helper(SetProperty, OutValue);

					// set the property values
					for (int32 i = 0; i < ArrLen; ++i)
					{
						const TSharedPtr<FJsonValue>& ArrayValueItem = ArrayValue[i];
						if (ArrayValueItem.IsValid() && !ArrayValueItem->IsNull())
						{
							int32 NewIndex = Helper.AddDefaultValue_Invalid_NeedsRehash();
							if (!JsonValueToFPropertyWithContainer(ArrayValueItem, SetProperty->ElementProp, Helper.GetElementPtr(NewIndex), ContainerStruct, Container, CheckFlags & (~CPF_ParmFlags), SkipFlags, CustomImportCallback))
							{
								UE_LOG(GameSerializer_Log, Error, TEXT("JsonValueToUProperty - Unable to deserialize set element [%d] for property %s"), i, *Property->GetNameCPP());
								return false;
							}
						}
					}

					Helper.Rehash();
				}
				else
				{
					UE_LOG(GameSerializer_Log, Error, TEXT("JsonValueToUProperty - Attempted to import TSet from non-array JSON key for property %s"), *Property->GetNameCPP());
					return false;
				}
			}
			else if (FTextProperty* TextProperty = CastField<FTextProperty>(Property))
			{
				if (JsonValue->Type == EJson::String)
				{
					// assume this string is already localized, so import as invariant
					TextProperty->SetPropertyValue(OutValue, FText::FromString(JsonValue->AsString()));
				}
				else if (JsonValue->Type == EJson::Object)
				{
					TSharedPtr<FJsonObject> Obj = JsonValue->AsObject();
					check(Obj.IsValid()); // should not fail if Type == EJson::Object

					// import the subvalue as a culture invariant string
					FText Text;
					if (!GetTextFromObject(Obj.ToSharedRef(), Text))
					{
						UE_LOG(GameSerializer_Log, Error, TEXT("JsonValueToUProperty - Attempted to import FText from JSON object with invalid keys for property %s"), *Property->GetNameCPP());
						return false;
					}
					TextProperty->SetPropertyValue(OutValue, Text);
				}
				else
				{
					UE_LOG(GameSerializer_Log, Error, TEXT("JsonValueToUProperty - Attempted to import FText from JSON that was neither string nor object for property %s"), *Property->GetNameCPP());
					return false;
				}
			}
			else if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				static const FName NAME_DateTime(TEXT("DateTime"));
				static const FName NAME_Color(TEXT("Color"));
				static const FName NAME_LinearColor(TEXT("LinearColor"));
				if (JsonValue->Type == EJson::Object)
				{
					TSharedPtr<FJsonObject> Obj = JsonValue->AsObject();
					check(Obj.IsValid()); // should not fail if Type == EJson::Object
					if (!JsonAttributesToUStructWithContainer(Obj->Values, StructProperty->Struct, OutValue, ContainerStruct, Container, CheckFlags & (~CPF_ParmFlags), SkipFlags, CustomImportCallback))
					{
						UE_LOG(GameSerializer_Log, Error, TEXT("JsonValueToUProperty - FJsonObjectConverter::JsonObjectToUStruct failed for property %s"), *Property->GetNameCPP());
						return false;
					}
				}
				else if (JsonValue->Type == EJson::String && StructProperty->Struct->GetFName() == NAME_LinearColor)
				{
					FLinearColor& ColorOut = *(FLinearColor*)OutValue;
					FString ColorString = JsonValue->AsString();

					FColor IntermediateColor;
					IntermediateColor = FColor::FromHex(ColorString);

					ColorOut = IntermediateColor;
				}
				else if (JsonValue->Type == EJson::String && StructProperty->Struct->GetFName() == NAME_Color)
				{
					FColor& ColorOut = *(FColor*)OutValue;
					FString ColorString = JsonValue->AsString();

					ColorOut = FColor::FromHex(ColorString);
				}
				else if (JsonValue->Type == EJson::String && StructProperty->Struct->GetFName() == NAME_DateTime)
				{
					FString DateString = JsonValue->AsString();
					FDateTime& DateTimeOut = *(FDateTime*)OutValue;
					if (DateString == TEXT("min"))
					{
						// min representable value for our date struct. Actual date may vary by platform (this is used for sorting)
						DateTimeOut = FDateTime::MinValue();
					}
					else if (DateString == TEXT("max"))
					{
						// max representable value for our date struct. Actual date may vary by platform (this is used for sorting)
						DateTimeOut = FDateTime::MaxValue();
					}
					else if (DateString == TEXT("now"))
					{
						// this value's not really meaningful from json serialization (since we don't know timezone) but handle it anyway since we're handling the other keywords
						DateTimeOut = FDateTime::UtcNow();
					}
					else if (FDateTime::ParseIso8601(*DateString, DateTimeOut))
					{
						// ok
					}
					else if (FDateTime::Parse(DateString, DateTimeOut))
					{
						// ok
					}
					else
					{
						UE_LOG(GameSerializer_Log, Error, TEXT("JsonValueToUProperty - Unable to import FDateTime for property %s"), *Property->GetNameCPP());
						return false;
					}
				}
				else if (JsonValue->Type == EJson::String && StructProperty->Struct->GetCppStructOps() && StructProperty->Struct->GetCppStructOps()->HasImportTextItem())
				{
					UScriptStruct::ICppStructOps* TheCppStructOps = StructProperty->Struct->GetCppStructOps();

					FString ImportTextString = JsonValue->AsString();
					const TCHAR* ImportTextPtr = *ImportTextString;
					if (!TheCppStructOps->ImportTextItem(ImportTextPtr, OutValue, PPF_None, nullptr, (FOutputDevice*)GWarn))
					{
						// Fall back to trying the tagged property approach if custom ImportTextItem couldn't get it done
						Property->ImportText(ImportTextPtr, OutValue, PPF_None, nullptr);
					}
				}
				else if (JsonValue->Type == EJson::String)
				{
					FString ImportTextString = JsonValue->AsString();
					const TCHAR* ImportTextPtr = *ImportTextString;
					Property->ImportText(ImportTextPtr, OutValue, PPF_None, nullptr);
				}
				else
				{
					UE_LOG(GameSerializer_Log, Error, TEXT("JsonValueToUProperty - Attempted to import UStruct from non-object JSON key for property %s"), *Property->GetNameCPP());
					return false;
				}
			}
			else if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
			{
				if (JsonValue->Type == EJson::Object)
				{
					UObject* Outer = GetTransientPackage();
					if (ContainerStruct->IsChildOf(UObject::StaticClass()))
					{
						Outer = (UObject*)Container;
					}

					UClass* PropertyClass = ObjectProperty->PropertyClass;
					UObject* createdObj = StaticAllocateObject(PropertyClass, Outer, NAME_None, EObjectFlags::RF_NoFlags, EInternalObjectFlags::None, false);
					(*PropertyClass->ClassConstructor)(FObjectInitializer(createdObj, PropertyClass->ClassDefaultObject, false, false));

					ObjectProperty->SetObjectPropertyValue(OutValue, createdObj);

					TSharedPtr<FJsonObject> Obj = JsonValue->AsObject();
					check(Obj.IsValid()); // should not fail if Type == EJson::Object
					if (!JsonAttributesToUStructWithContainer(Obj->Values, ObjectProperty->PropertyClass, createdObj, ObjectProperty->PropertyClass, createdObj, CheckFlags & (~CPF_ParmFlags), SkipFlags, CustomImportCallback))
					{
						UE_LOG(GameSerializer_Log, Error, TEXT("JsonValueToUProperty - FJsonObjectConverter::JsonObjectToUStruct failed for property %s"), *Property->GetNameCPP());
						return false;
					}
				}
				else if (JsonValue->Type == EJson::String)
				{
					// Default to expect a string for everything else
					if (Property->ImportText(*JsonValue->AsString(), OutValue, 0, NULL) == NULL)
					{
						UE_LOG(GameSerializer_Log, Error, TEXT("JsonValueToUProperty - Unable import property type %s from string value for property %s"), *Property->GetClass()->GetName(), *Property->GetNameCPP());
						return false;
					}
				}
			}
			else
			{
				// Default to expect a string for everything else
				if (Property->ImportText(*JsonValue->AsString(), OutValue, 0, NULL) == NULL)
				{
					UE_LOG(GameSerializer_Log, Error, TEXT("JsonValueToUProperty - Unable import property type %s from string value for property %s"), *Property->GetClass()->GetName(), *Property->GetNameCPP());
					return false;
				}
			}

			return true;
		}

		static bool JsonValueToFPropertyWithContainer(const TSharedPtr<FJsonValue>& JsonValue, FProperty* Property, void* OutValue, const UStruct* ContainerStruct, void* Container, int64 CheckFlags, int64 SkipFlags, const FCustomImportCallback& CustomImportCallback)
		{
			if (!JsonValue.IsValid())
			{
				UE_LOG(GameSerializer_Log, Error, TEXT("JsonValueToUProperty - Invalid value JSON key"));
				return false;
			}

			bool bArrayOrSetProperty = Property->IsA<FArrayProperty>() || Property->IsA<FSetProperty>();
			bool bJsonArray = JsonValue->Type == EJson::Array;

			if (!bJsonArray)
			{
				if (bArrayOrSetProperty)
				{
					UE_LOG(GameSerializer_Log, Error, TEXT("JsonValueToUProperty - Attempted to import TArray from non-array JSON key"));
					return false;
				}

				if (Property->ArrayDim != 1)
				{
					UE_LOG(GameSerializer_Log, Warning, TEXT("Ignoring excess properties when deserializing %s"), *Property->GetName());
				}

				return ConvertScalarJsonValueToFPropertyWithContainer(JsonValue, Property, OutValue, ContainerStruct, Container, CheckFlags, SkipFlags, CustomImportCallback);
			}

			// In practice, the ArrayDim == 1 check ought to be redundant, since nested arrays of FPropertys are not supported
			if (bArrayOrSetProperty && Property->ArrayDim == 1)
			{
				// Read into TArray
				return ConvertScalarJsonValueToFPropertyWithContainer(JsonValue, Property, OutValue, ContainerStruct, Container, CheckFlags, SkipFlags, CustomImportCallback);
			}

			// We're deserializing a JSON array
			const auto& ArrayValue = JsonValue->AsArray();
			if (Property->ArrayDim < ArrayValue.Num())
			{
				UE_LOG(GameSerializer_Log, Warning, TEXT("Ignoring excess properties when deserializing %s"), *Property->GetName());
			}

			// Read into native array
			int ItemsToRead = FMath::Clamp(ArrayValue.Num(), 0, Property->ArrayDim);
			for (int Index = 0; Index != ItemsToRead; ++Index)
			{
				if (!ConvertScalarJsonValueToFPropertyWithContainer(ArrayValue[Index], Property, (char*)OutValue + Index * Property->ElementSize, ContainerStruct, Container, CheckFlags, SkipFlags, CustomImportCallback))
				{
					return false;
				}
			}
			return true;
		}

		static bool JsonAttributesToUStructWithContainer(const TMap< FString, TSharedPtr<FJsonValue> >& JsonAttributes, const UStruct* StructDefinition, void* OutStruct, const UStruct* ContainerStruct, void* Container, int64 CheckFlags, int64 SkipFlags, const FCustomImportCallback& CustomImportCallback)
		{
			if (StructDefinition == FJsonObjectWrapper::StaticStruct())
			{
				// Just copy it into the object
				FJsonObjectWrapper* ProxyObject = (FJsonObjectWrapper*)OutStruct;
				ProxyObject->JsonObject = MakeShared<FJsonObject>();
				ProxyObject->JsonObject->Values = JsonAttributes;
				return true;
			}

			int32 NumUnclaimedProperties = JsonAttributes.Num();
			if (NumUnclaimedProperties <= 0)
			{
				return true;
			}

			// iterate over the struct properties
			for (TFieldIterator<FProperty> PropIt(StructDefinition); PropIt; ++PropIt)
			{
				FProperty* Property = *PropIt;

				// Check to see if we should ignore this property
				if (CheckFlags != 0 && !Property->HasAnyPropertyFlags(CheckFlags))
				{
					continue;
				}
				if (Property->HasAnyPropertyFlags(SkipFlags))
				{
					continue;
				}

				// find a json value matching this property name
				const TSharedPtr<FJsonValue>* JsonValue = JsonAttributes.Find(Property->GetName());
				if (!JsonValue)
				{
					// we allow values to not be found since this mirrors the typical UObject mantra that all the fields are optional when deserializing
					continue;
				}

				if (JsonValue->IsValid() && !(*JsonValue)->IsNull())
				{
					void* Value = Property->ContainerPtrToValuePtr<uint8>(OutStruct);
					if (!JsonValueToFPropertyWithContainer(*JsonValue, Property, Value, ContainerStruct, Container, CheckFlags, SkipFlags, CustomImportCallback))
					{
						UE_LOG(GameSerializer_Log, Error, TEXT("JsonObjectToUStruct - Unable to parse %s.%s from JSON"), *StructDefinition->GetName(), *Property->GetName());
						return false;
					}
				}

				if (--NumUnclaimedProperties <= 0)
				{
					// If we found all properties that were in the JsonAttributes map, there is no reason to keep looking for more.
					break;
				}
			}

			return true;
		}
	};
}

FIntVector GameSerializerContext::WorldOffset = FIntVector::ZeroValue;

namespace GameSerializerCore
{
	constexpr FObjectIdx NullIdx = 0;

	namespace FieldName
	{
		constexpr TCHAR ExternalObjectsFieldName[] = TEXT("__ExternalObjects");
		constexpr TCHAR DynamicObjectsFieldName[] = TEXT("__DynamicObjects");
		constexpr TCHAR SubObjectsFieldName[] = TEXT("__SubObjects");
		constexpr TCHAR ObjectNameFieldName[] = TEXT("__Name");
		constexpr TCHAR ObjectClassFieldName[] = TEXT("__Class");
		constexpr TCHAR ExtendDataFieldName[] = TEXT("__ExtendData");
		constexpr TCHAR ExtendDataTypeFieldName[] = TEXT("__Type");
		constexpr TCHAR ActorTransformFieldName[] = TEXT("__ActorTransform");
		constexpr TCHAR ActorOwnerFieldName[] = TEXT("__ActorOwner");
	}
	
	using namespace CustomJsonConverter;
	using namespace FieldName;

	FStructToJson::FStructToJson()
	{
		RootJsonObject->SetObjectField(ExternalObjectsFieldName, ExternalJsonObject);
		RootJsonObject->SetObjectField(DynamicObjectsFieldName, DynamicJsonObject);
	}

	DECLARE_CYCLE_STAT(TEXT("StructToJson_AddObjects"), STAT_StructToJson_AddObjects, STATGROUP_GameSerializer);
	void FStructToJson::AddObjects(const FString& FieldName, TArray<UObject*> Objects)
	{
		GameSerializerStatLog(STAT_StructToJson_AddObjects);
		
		TArray<TSharedPtr<FJsonValue>> ObjectsJsonArray;
		for (UObject* Object : Objects)
		{
			ObjectsJsonArray.Add(MakeShared<FJsonValueNumber>(ConvertObjectToObjectIdx(Object)));
		}
		RootJsonObject->SetArrayField(FieldName, ObjectsJsonArray);
	}

	void FStructToJson::AddObject(const FString& FieldName, UObject* Object)
	{
		check(Object);

		RootJsonObject->SetNumberField(FieldName, ConvertObjectToObjectIdx(Object));
	}

	FObjectIdx FStructToJson::GetExternalObjectIndex(const UObject* ExternalObject)
	{
		FObjectIdx& ExternalObjectIdx = ExternalObjectIdxMap.FindOrAdd(ExternalObject);
		if (ExternalObjectIdx == NullIdx)
		{
			ExternalObjectUniqueIdx -= 1;

			ExternalObjectIdx = ExternalObjectUniqueIdx;

			FString SoftObjectPathString;
			FSoftObjectPath SoftObjectPath(ExternalObject);
#if WITH_EDITOR
			const FString Path = SoftObjectPath.ToString();
			const FString ShortPackageOuterAndName = FPackageName::GetLongPackageAssetName(Path);
			if (ShortPackageOuterAndName.StartsWith(PLAYWORLD_PACKAGE_PREFIX))
			{
				const int32 Idx = ShortPackageOuterAndName.Find(TEXT("_"), ESearchCase::IgnoreCase, ESearchDir::FromStart, 7);
				FString OriginPath = FString::Printf(TEXT("%s/%s"), *FPackageName::GetLongPackagePath(Path), *ShortPackageOuterAndName.Mid(Idx + 1));
				SoftObjectPath.SetPath(MoveTemp(OriginPath));
			}
#endif
			SoftObjectPath.ExportTextItem(SoftObjectPathString, FSoftObjectPath(), nullptr, PPF_None, nullptr);
			ExternalJsonObject->SetStringField(FString::FromInt(ExternalObjectUniqueIdx), SoftObjectPathString);
		}
		return ExternalObjectIdx;
	}

	FObjectIdx FStructToJson::ObjectToJsonObject(const TSharedRef<FJsonObject>& JsonObject, UObject* Object)
	{
		OuterChain.Add(FOuterData(Object, JsonObject));
		ON_SCOPE_EXIT
		{
			OuterChain.Pop();
		};
		UClass* Class = Object->GetClass();

		check(ObjectIdxMap.Contains(Object) == false);
		ObjectUniqueIdx += 1;
		const FObjectIdx ObjectIdx = ObjectUniqueIdx;
		ObjectIdxMap.Add(Object, ObjectIdx);

		JsonObject->SetStringField(ObjectNameFieldName, Object->GetName());
		JsonObject->SetNumberField(ObjectClassFieldName, GetExternalObjectIndex(Class));

		if (AActor* Actor = Cast<AActor>(Object))
		{
			AddStruct(JsonObject, ActorTransformFieldName, Actor->GetActorTransform());
		}
		
		const FGameSerializerExtendDataContainer ExtendDataContainer = IGameSerializerInterface::WhenGamePreSave(Object);
		if (ExtendDataContainer.Struct && ensure(ExtendDataContainer.ExtendData.IsValid()))
		{
			const TSharedRef<FJsonObject> ExtendDataContainerJsonObject = MakeShared<FJsonObject>();
			const FObjectIdx StructIdx = GetExternalObjectIndex(ExtendDataContainer.Struct);
			ExtendDataContainerJsonObject->SetNumberField(ExtendDataTypeFieldName, StructIdx);

			const UScriptStruct* Struct = ExtendDataContainer.Struct;
			FGameSerializerExtendData* DefaultExtendData = static_cast<FGameSerializerExtendData*>(FMemory::Malloc(Struct->GetStructureSize(), Struct->GetMinAlignment()));
			ExtendDataContainer.Struct->InitializeStruct(DefaultExtendData);
			bool bSubObjectSameValue;
			const bool IsSaveSucceed = StructToJson::UStructToJsonAttributes(ExtendDataContainer.Struct, ExtendDataContainer.ExtendData.Get(), DefaultExtendData, bSubObjectSameValue, ExtendDataContainerJsonObject->Values, CheckFlags, SkipFlags, FCustomExportCallback::CreateRaw(this, &FStructToJson::ConvertObjectToJson));
			ensure(IsSaveSucceed);
			ExtendDataContainer.Struct->DestroyStruct(DefaultExtendData);
			FMemory::Free(DefaultExtendData);

			if (bSubObjectSameValue == false)
			{
				JsonObject->SetObjectField(ExtendDataFieldName, ExtendDataContainerJsonObject);
			}
		}

		bool bSameValue;
		const bool IsSaveSucceed = StructToJson::UStructToJsonAttributes(Class, Object, Class->GetDefaultObject(), bSameValue, JsonObject->Values, CheckFlags, SkipFlags, FCustomExportCallback::CreateRaw(this, &FStructToJson::ConvertObjectToJson));
		ensure(IsSaveSucceed);
		return ObjectIdx;
	}

	FObjectIdx FStructToJson::ConvertObjectToObjectIdx(UObject* Object)
	{
		if (Object == nullptr)
		{
			return NullIdx;
		}
		else if (FObjectIdx* ExistObjectIdx = ObjectIdxMap.Find(Object))
		{
			return *ExistObjectIdx;
		}
		else if (Object->IsAsset() || Object->IsA<UStruct>())
		{
			const FObjectIdx ExternalObjectIdx = GetExternalObjectIndex(Object);
			return ExternalObjectIdx;
		}
		else
		{
			const TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
			const FObjectIdx NewObjectIdx = ObjectToJsonObject(JsonObject, Object);
			DynamicJsonObject->SetObjectField(FString::FromInt(NewObjectIdx), JsonObject);

			return NewObjectIdx;
		}
	}

	TSharedPtr<FJsonValue> FStructToJson::ConvertObjectToJson(FProperty* Property, const void* Value, const void* Default, bool& bSameValue)
	{
		if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
		{
			UObject* SubObject = ObjectProperty->GetPropertyValue(Value);

			if (Default)
			{
				UObject* DefaultSubObject = ObjectProperty->GetPropertyValue(Default);
				bSameValue = SubObject == DefaultSubObject;
			}
			
			if (SubObject != nullptr)
			{
				if (FObjectIdx* ObjectIdx = ObjectIdxMap.Find(SubObject))
				{
					return MakeShared<FJsonValueNumber>(*ObjectIdx);
				}

				// 存在于包内的数据记录路径即可
				// TODO：找到直接判断是否在持久性包内的方法
				if (SubObject->IsAsset() || SubObject->IsA<UStruct>())
				{
					const FObjectIdx ExternalObjectIdx = GetExternalObjectIndex(SubObject);
					return MakeShared<FJsonValueNumber>(ExternalObjectIdx);
				}

				// Actor用Owner进行归属的判断
				if (const AActor* SubActor = Cast<AActor>(SubObject))
				{
					for (FObjectIdx Idx = OuterChain.Num() - 1; Idx >= 0; --Idx)
					{
						AActor* SubActorOwner = IActorGameSerializerInterface::GetGameSerializedOwner(SubActor);
						if (SubActorOwner == OuterChain[Idx].Outer)
						{
							// SubActor的命名约定要存在Owner的名称，避免读档时已经存在重名的Actor（不由Owner生成的）
							ensure(SubActor->GetName().Contains(SubActorOwner->GetName()));
							
							const TSharedRef<FJsonObject> SubActorJsonObject = MakeShared<FJsonObject>();
							SubActorJsonObject->SetNumberField(ActorOwnerFieldName, ObjectIdxMap[SubActorOwner]);
							const FObjectIdx ObjectIdx = ObjectToJsonObject(SubActorJsonObject, SubObject);
							
							DynamicJsonObject->SetObjectField(FString::FromInt(ObjectIdx), SubActorJsonObject);
							return MakeShared<FJsonValueNumber>(ObjectIdx);
						}
					}
				}
				else
				{
					// 能找到Outer的储存所有数据
					for (FObjectIdx Idx = OuterChain.Num() - 1; Idx >= 0; --Idx)
					{
						const FOuterData& TestOuterData = OuterChain[Idx];
						const UObject* GameSerializedOuter = IGameSerializerInterface::GetGameSerializedOuter(SubObject);
						if (GameSerializedOuter == TestOuterData.Outer)
						{
							TSharedPtr<FJsonObject> SubObjectsJsonObject;
							if (TestOuterData.OuterJsonObject->HasField(SubObjectsFieldName))
							{
								SubObjectsJsonObject = TestOuterData.OuterJsonObject->GetObjectField(SubObjectsFieldName);
							}
							else
							{
								SubObjectsJsonObject = MakeShared<FJsonObject>();
								TestOuterData.OuterJsonObject->SetObjectField(SubObjectsFieldName, SubObjectsJsonObject);
							}

							const TSharedRef<FJsonObject> SubObjectJsonObject = MakeShared<FJsonObject>();

							const FObjectIdx ObjectIdx = ObjectToJsonObject(SubObjectJsonObject, SubObject);
							SubObjectsJsonObject->SetObjectField(FString::FromInt(ObjectIdx), SubObjectJsonObject);
							return MakeShared<FJsonValueNumber>(ObjectIdx);
						}
					}
				}

				// 不存在Outer，存软引用
				const FObjectIdx ExternalObjectIdx = GetExternalObjectIndex(SubObject);
				return MakeShared<FJsonValueNumber>(ExternalObjectIdx);
			}
			else
			{
				return MakeShared<FJsonValueNumber>(NullIdx);
			}
		}
		return nullptr;
	}

	void FStructToJson::AddStruct(const TSharedRef<FJsonObject>& JsonObject, const FString& FieldName, UScriptStruct* Struct, const void* Value, const void* DefaultValue)
	{
		const TSharedRef<FJsonObject> StructJsonObject = MakeShared<FJsonObject>();
		bool bSameValue;
		const bool IsSaveSucceed = StructToJson::UStructToJsonAttributes(Struct, Value, DefaultValue, bSameValue, StructJsonObject->Values, CheckFlags, SkipFlags, FCustomExportCallback::CreateRaw(this, &FStructToJson::ConvertObjectToJson));
		ensure(IsSaveSucceed);
		if (bSameValue == false)
		{
			JsonObject->SetObjectField(FieldName, StructJsonObject);
		}
	}

	FJsonToStruct::FJsonToStruct(UObject* Outer, const TSharedRef<FJsonObject>& RootJsonObject)
		: Outer(Outer)
	    , RootJsonObject(RootJsonObject)
	{

	}

	DECLARE_CYCLE_STAT(TEXT("JsonToStruct_LoadExternalObject"), STAT_JsonToStruct_LoadExternalObject, STATGROUP_GameSerializer);
	void FJsonToStruct::LoadExternalObject()
	{
		GameSerializerStatLog(STAT_JsonToStruct_LoadExternalObject);

		const TSharedPtr<FJsonObject> ExternalObjectJsonObject = RootJsonObject->GetObjectField(ExternalObjectsFieldName);
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : ExternalObjectJsonObject->Values)
		{
			const FObjectIdx Idx = -FCString::Atoi(*Pair.Key);
			ExternalObjectsArray.SetNumUninitialized(Idx + 1);

			const FString SoftObjectPathString = Pair.Value->AsString();
			const TCHAR* Buffer = *SoftObjectPathString;

			FSoftObjectPath SoftObjectPath;
			SoftObjectPath.ImportTextItem(Buffer, PPF_None, nullptr, nullptr);

#if WITH_EDITOR
			TGuardValue<int32> NoneGPlayInEditorIDGuard(GPlayInEditorID, INDEX_NONE);
#endif
			UObject* ExternalObject = SoftObjectPath.TryLoad();
#if WITH_EDITOR
			if (ExternalObject == nullptr && Outer)
			{
				const int32 PIEInstanceID = Outer->GetOutermost()->PIEInstanceID;
				TGuardValue<int32> GPlayInEditorIDGuard(GPlayInEditorID, PIEInstanceID);
				ExternalObject = SoftObjectPath.TryLoad();
			}
#endif

			if (ensure(ExternalObject) == false)
			{
				UE_LOG(GameSerializer_Log, Warning, TEXT("未能加载对象 [%s]"), *SoftObjectPath.ToString());
			}
			ExternalObjectsArray[Idx] = ExternalObject;
		}
	}

	DECLARE_CYCLE_STAT(TEXT("JsonToStruct_InstanceDynamicObject"), STAT_JsonToStruct_InstanceDynamicObject, STATGROUP_GameSerializer);
	void FJsonToStruct::InstanceDynamicObject()
	{
		GameSerializerStatLog(STAT_JsonToStruct_InstanceDynamicObject);

		const TSharedPtr<FJsonObject> DynamicJsonObject = RootJsonObject->GetObjectField(DynamicObjectsFieldName);
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : DynamicJsonObject->Values)
		{
			const FObjectIdx Idx = FCString::Atoi(*Pair.Key);
			JsonObjectToInstanceObject(Pair.Value->AsObject().ToSharedRef(), Idx);
		}
	}

	DECLARE_CYCLE_STAT(TEXT("JsonToStruct_LoadDynamicObjectJsonData"), STAT_JsonToStruct_LoadDynamicObjectJsonData, STATGROUP_GameSerializer);
	void FJsonToStruct::LoadDynamicObjectJsonData()
	{
		GameSerializerStatLog(STAT_JsonToStruct_LoadDynamicObjectJsonData);

		for (FInstancedObjectData& InstancedObjectData : AllInstancedObjectData)
		{
			UObject* InstancedObject = InstancedObjectData.Object.Get();
			if (InstancedObject)
			{
				UClass* Class = InstancedObject->GetClass();
				if (AActor* Actor = Cast<AActor>(InstancedObject))
				{
					FObjectIdx OwnerIdx;
					if (InstancedObjectData.JsonObject->TryGetNumberField(ActorOwnerFieldName, OwnerIdx))
					{
						AActor* Owner = Cast<AActor>(GetObjectByIdx(OwnerIdx));
						if (ensure(Owner))
						{
							IActorGameSerializerInterface::SetGameSerializedOwner(Actor, Owner);
						}
					}
				}

				TArray<FName> CallRepNotifyIgnorePropertyNames;
				if (InstancedObject->Implements<UGameSerializerInterface>())
				{
					CallRepNotifyIgnorePropertyNames = IGameSerializerInterface::GetCallRepNotifyIgnorePropertyNames(InstancedObject);
				}
				
				TArray<FGameSerializerNetNotifyData>& AllNetNotifyData = InstancedObjectData.AllNetNotifyData;
				const bool IsLoadSucceed = JsonToStruct::JsonAttributesToUStructWithContainer(InstancedObjectData.JsonObject->Values, Class, InstancedObject, Class, InstancedObject, CheckFlags, SkipFlags, 
					FCustomImportCallback::CreateLambda([&](const TSharedPtr<FJsonValue>& JsonValue, FProperty* Property, void* OutValue) mutable
					{
						if (Property->HasAnyPropertyFlags(CPF_RepNotify))
						{
							if (CallRepNotifyIgnorePropertyNames.Contains(Property->GetFName()) == false)
							{
								UFunction* RepNotifyFunc = Class->FindFunctionByName(Property->RepNotifyFunc);
								check(RepNotifyFunc);
								FGameSerializerNetNotifyData& PropertyAndPreData = AllNetNotifyData.AddDefaulted_GetRef();
								PropertyAndPreData.Property = Property;
								PropertyAndPreData.RepNotifyFunc = RepNotifyFunc;
							}
						}
						return JsonObjectIdxToObject(JsonValue, Property, OutValue);
					}));
				ensure(IsLoadSucceed);
			}
		}
	}

	DECLARE_CYCLE_STAT(TEXT("JsonToStruct_ActorFinishSpawning"), STAT_JsonToStruct_ActorFinishSpawning, STATGROUP_GameSerializer);
	void FJsonToStruct::DynamicActorFinishSpawning()
	{
		GameSerializerStatLog(STAT_JsonToStruct_ActorFinishSpawning);
		for (FSpawnedActorData& SpawnedActorData : SpawnedActors)
		{
			AActor* SpawnedActor = SpawnedActorData.SpawnedActor.Get();
			if (ensure(SpawnedActor))
			{
				if (UBlueprintGeneratedClass* BPGC = Cast<UBlueprintGeneratedClass>(SpawnedActor->GetClass()))
				{
					// SimpleConstructionScript已经执行过了，跳过
					TGuardValue<TObjectPtr<USimpleConstructionScript>> SimpleConstructionScriptGuard(BPGC->SimpleConstructionScript, nullptr);
					SpawnedActor->FinishSpawning(SpawnedActor->GetActorTransform());
				}
				else
				{
					SpawnedActor->FinishSpawning(SpawnedActor->GetActorTransform());
				}
			}
		}
	}

	void FJsonToStruct::RestoreDynamicActorSpawnedData()
	{
		for (const FSpawnedActorData& ActorData : SpawnedActors)
		{
			if (AActor* Actor = ActorData.SpawnedActor.Get())
			{
				Actor->bNetLoadOnClient = ActorData.bNetLoadOnClient;
			}
		}
	}

	DECLARE_CYCLE_STAT(TEXT("JsonToStruct_LoadDynamicObjectExtendData"), STAT_JsonToStruct_LoadDynamicObjectExtendData, STATGROUP_GameSerializer);
	void FJsonToStruct::LoadDynamicObjectExtendData()
	{
		GameSerializerStatLog(STAT_JsonToStruct_LoadDynamicObjectExtendData);

		for (const FInstancedObjectData& InstancedObjectData : AllInstancedObjectData)
		{
			if (UObject* LoadedObject = InstancedObjectData.Object.Get())
			{
				ExecutePostLoad(LoadedObject, InstancedObjectData);
			}
		}
	}

	void FJsonToStruct::ExecutePostLoad(UObject* LoadedObject, const FInstancedObjectData& InstancedObjectData) const
	{
		const FGameSerializerCallRepNotifyFunc CallRepNotifyFunc(LoadedObject, InstancedObjectData.AllNetNotifyData);
		const TSharedPtr<FJsonObject>* ExtendDataJsonObject;
		if (InstancedObjectData.JsonObject->TryGetObjectField(ExtendDataFieldName, ExtendDataJsonObject))
		{
			UScriptStruct* Struct = CastChecked<UScriptStruct>(ExternalObjectsArray[-int32(ExtendDataJsonObject->Get()->GetNumberField(ExtendDataTypeFieldName))]);
			FGameSerializerExtendData* ExtendData = static_cast<FGameSerializerExtendData*>(FMemory::Malloc(Struct->GetStructureSize()));
			Struct->InitializeStruct(ExtendData);
			const bool IsLoadSucceed = JsonToStruct::JsonAttributesToUStructWithContainer(ExtendDataJsonObject->Get()->Values, Struct, ExtendData, Struct, ExtendData, CheckFlags, SkipFlags, FCustomImportCallback::CreateRaw(this, &FJsonToStruct::JsonObjectIdxToObject));
			ensure(IsLoadSucceed);
			FGameSerializerExtendDataContainer DataContainer;
			DataContainer.Struct = Struct;
			DataContainer.ExtendData = MakeShareable(ExtendData);
			IGameSerializerInterface::WhenGamePostLoad(LoadedObject, DataContainer, CallRepNotifyFunc);
		}
		else
		{
			IGameSerializerInterface::WhenGamePostLoad(LoadedObject, FGameSerializerExtendDataContainer(), CallRepNotifyFunc);
		}
	}

	void FJsonToStruct::RetargetDynamicObjectName(const FString& FieldName, const FName& NewName)
	{
		const TSharedPtr<FJsonObject>& JsonDynamicObjects = RootJsonObject->GetObjectField(DynamicObjectsFieldName);
		const TSharedPtr<FJsonObject>& DynamicObject = JsonDynamicObjects->GetObjectField(FString::FromInt(int32(RootJsonObject->GetNumberField(FieldName))));
		DynamicObject->SetStringField(ObjectNameFieldName, NewName.ToString());
	}

	TArray<UObject*> FJsonToStruct::GetObjects(const FString& FieldName) const
	{
		TArray<UObject*> Res;

		const TArray<TSharedPtr<FJsonValue>>& IdxArrayJson = RootJsonObject->GetArrayField(FieldName);
		for (const TSharedPtr<FJsonValue>& IdxJson : IdxArrayJson)
		{
			Res.Add(GetObjectByIdx(IdxJson->AsNumber()));
		}

		return Res;
	}

	UObject* FJsonToStruct::GetObject(const FString& FieldName) const
	{
		return GetObjectByIdx(int32(RootJsonObject->GetNumberField(FieldName)));
	}

	UObject* FJsonToStruct::JsonObjectToInstanceObject(const TSharedRef<FJsonObject>& JsonObject, FObjectIdx ObjectIdx)
	{
		const FString ObjectName = JsonObject->GetStringField(ObjectNameFieldName);
		UClass* ObjectClass = Cast<UClass>(ExternalObjectsArray[-JsonObject->GetIntegerField(ObjectClassFieldName)]);
		if (ObjectClass == nullptr)
		{
			return nullptr;
		}

		UObject* Object = FindObject<UObject>(Outer, *ObjectName);

		if (Object && Object->IsPendingKill())
		{
			Object->Rename(nullptr, GetTransientPackage());
			Object = nullptr;
		}
		
		if (Object)
		{
			// LevelScriptActor的子类在PIE模式下无法做合法性检测，跳过
			ensure(Object->IsA<ALevelScriptActor>() || Object->IsA(ObjectClass));
			if (AActor* Actor = Cast<AActor>(Object))
			{
				FTransform ActorTransform = GetStruct<FTransform>(JsonObject, ActorTransformFieldName);
				ActorTransform.AddToTranslation(FVector(GameSerializerContext::WorldOffset));
				Actor->SetActorTransform(ActorTransform);
			}
		}
		else
		{
			if (ObjectClass->IsChildOf<AActor>())
			{
				ULevel* Level = CastChecked<ULevel>(Outer);

				FTransform ActorTransform = GetStruct<FTransform>(JsonObject, ActorTransformFieldName);
				ActorTransform.AddToTranslation(FVector(GameSerializerContext::WorldOffset));
				
				FActorSpawnParameters ActorSpawnParameters;
				ActorSpawnParameters.OverrideLevel = Level;
				ActorSpawnParameters.bDeferConstruction = true;
				ActorSpawnParameters.Name = *ObjectName;

				UWorld* World = Level->GetWorld();
				AActor* Actor = World->SpawnActor<AActor>(ObjectClass, ActorTransform, ActorSpawnParameters);
				FSpawnedActorData& SpawnedActorData = SpawnedActors.AddDefaulted_GetRef();
				SpawnedActorData.bNetLoadOnClient = Actor->bNetLoadOnClient;
				Actor->bNetLoadOnClient = false;
				SpawnedActorData.SpawnedActor = Actor;

				// Spawn的蓝图Actor需要优先构造Component
				if (UBlueprintGeneratedClass* ActualBPGC = Cast<UBlueprintGeneratedClass>(ObjectClass))
				{
					TArray<const UBlueprintGeneratedClass*> ParentBPClassStack;
					const bool bErrorFree = UBlueprintGeneratedClass::GetGeneratedClassesHierarchy(ActualBPGC, ParentBPClassStack);
					if (bErrorFree)
					{
						FGuardValue_Bitfield(World->bIsRunningConstructionScript, true);
						for (int32 i = ParentBPClassStack.Num() - 1; i >= 0; i--)
						{
							const UBlueprintGeneratedClass* CurrentBPGClass = ParentBPClassStack[i];
							check(CurrentBPGClass);
							USimpleConstructionScript* SCS = CurrentBPGClass->SimpleConstructionScript;
							if (SCS)
							{
								// 参见
								// USimpleConstructionScript::ExecuteScriptOnActor
								// USCS_Node::ExecuteNodeOnActor
								// 取消了组件注册的流程
								struct FSCS_NodeInstanceComponent
								{
									static UActorComponent* ExecuteNodeOnActor(USCS_Node* RootNode, AActor* Actor, USceneComponent* ParentComponent, const FTransform* RootTransform, const FRotationConversionCache* RootRelativeRotationCache, bool bIsDefaultTransform)
									{
										const FName InternalVariableName = RootNode->GetVariableName();
										
										check(Actor != nullptr);
										check((ParentComponent != nullptr && !ParentComponent->IsPendingKill()) || (RootTransform != nullptr)); // must specify either a parent component or a world transform

										// Create a new component instance based on the template
										UActorComponent* NewActorComp = nullptr;
										UBlueprintGeneratedClass* ActualBPGC = CastChecked<UBlueprintGeneratedClass>(Actor->GetClass());
										const FBlueprintCookedComponentInstancingData* ActualComponentTemplateData = ActualBPGC->UseFastPathComponentInstancing() ? RootNode->GetActualComponentTemplateData(ActualBPGC) : nullptr;
										if (ActualComponentTemplateData && ActualComponentTemplateData->bHasValidCookedData
											&& ensureMsgf(ActualComponentTemplateData->ComponentTemplateClass != nullptr, TEXT("SCS fast path (%s.%s): Cooked data is valid, but runtime support data is not initialized. Using the slow path instead."), *ActualBPGC->GetName(), *InternalVariableName.ToString()))
										{
											// Use cooked instancing data if valid (fast path).
											NewActorComp = Actor->CreateComponentFromTemplateData(ActualComponentTemplateData, InternalVariableName);
										}
										else if (UActorComponent* ActualComponentTemplate = RootNode->GetActualComponentTemplate(ActualBPGC))
										{
											NewActorComp = Actor->CreateComponentFromTemplate(ActualComponentTemplate, InternalVariableName);
										}

										if (NewActorComp != nullptr)
										{
											NewActorComp->CreationMethod = EComponentCreationMethod::SimpleConstructionScript;

											// SCS created components are net addressable
											NewActorComp->SetNetAddressable();

											if (!NewActorComp->HasBeenCreated())
											{
												// Call function to notify component it has been created
												NewActorComp->OnComponentCreated();
											}

											// Special handling for scene components
											USceneComponent* NewSceneComp = Cast<USceneComponent>(NewActorComp);
											if (NewSceneComp != nullptr)
											{
												// If NULL is passed in, we are the root, so set transform and assign as RootComponent on Actor, similarly if the 
												// NewSceneComp is the ParentComponent then we are the root component. This happens when the root component is recycled
												// by StaticAllocateObject.
												if (ParentComponent == nullptr || (ParentComponent && ParentComponent->IsPendingKill()) || ParentComponent == NewSceneComp)
												{
													FTransform WorldTransform = *RootTransform;
													if (bIsDefaultTransform)
													{
														// Note: We use the scale vector from the component template when spawning (to match what happens with a native root). This
														// does NOT occur when this component is instanced as part of dynamically spawning a Blueprint class in a cooked build (i.e.
														// 'bIsDefaultTransform' will be 'false' in that situation). In order to maintain the same behavior between a nativized and
														// non-nativized cooked build, if this ever changes, we would also need to update the code in AActor::PostSpawnInitialize().
														WorldTransform.SetScale3D(NewSceneComp->GetRelativeScale3D());
													}

													if (RootRelativeRotationCache)
													{	// Enforces using the same rotator as much as possible.
														NewSceneComp->SetRelativeRotationCache(*RootRelativeRotationCache);
													}

													NewSceneComp->SetWorldTransform(WorldTransform);
													Actor->SetRootComponent(NewSceneComp);
												}
												// Otherwise, attach to parent component passed in
												else
												{
													NewSceneComp->SetupAttachment(ParentComponent, RootNode->AttachToName);
												}
											}

											// If we want to save this to a property, do it here
											FName VarName = InternalVariableName;
											if (VarName != NAME_None)
											{
												UClass* ActorClass = Actor->GetClass();
												if (FObjectPropertyBase* Prop = FindFProperty<FObjectPropertyBase>(ActorClass, VarName))
												{
													// If it is null we don't really know what's going on, but make it behave as it did before the bug fix
													if (Prop->PropertyClass == nullptr || NewActorComp->IsA(Prop->PropertyClass))
													{
														Prop->SetObjectPropertyValue_InContainer(Actor, NewActorComp);
													}
													else
													{
														UE_LOG(LogBlueprint, Log, TEXT("ExecuteNodeOnActor: Property '%s' on '%s' is of type '%s'. Could not assign '%s' to it."), *VarName.ToString(), *Actor->GetName(), *Prop->PropertyClass->GetName(), *NewActorComp->GetName());
													}
												}
												else
												{
													UE_LOG(LogBlueprint, Log, TEXT("ExecuteNodeOnActor: Couldn't find property '%s' on '%s'"), *VarName.ToString(), *Actor->GetName());
#if WITH_EDITOR
													// If we're constructing editable components in the SCS editor, set the component instance corresponding to this node for editing purposes
													USimpleConstructionScript* SCS = RootNode->GetSCS();
													if (SCS != nullptr && (SCS->IsConstructingEditorComponents() || SCS->GetComponentEditorActorInstance() == Actor))
													{
														RootNode->EditorComponentInstance = NewSceneComp;
													}
#endif
												}
											}

											// Determine the parent component for our children (it's still our parent if we're a non-scene component)
											USceneComponent* ParentSceneComponentOfChildren = (NewSceneComp != nullptr) ? NewSceneComp : ParentComponent;

											// If we made a component, go ahead and process our children
											for (int32 NodeIdx = 0; NodeIdx < RootNode->ChildNodes.Num(); NodeIdx++)
											{
												USCS_Node* Node = RootNode->ChildNodes[NodeIdx];
												check(Node != nullptr);
												ExecuteNodeOnActor(Node, Actor, ParentSceneComponentOfChildren, nullptr, nullptr, false);
											}
										}

										return NewActorComp;
									}
								};

								const TArray<USCS_Node*>& Nodes = SCS->GetRootNodes();
								for (USCS_Node* Node : Nodes)
								{
									FSCS_NodeInstanceComponent::ExecuteNodeOnActor(Node, Actor, nullptr, &ActorTransform, nullptr, false);
								}
							}
						}
					}
				}
				
				Object = Actor;
			}
			else if (ObjectClass->IsChildOf<UActorComponent>())
			{
				AActor* Actor = CastChecked<AActor>(Outer);

				UActorComponent* Component = NewObject<UActorComponent>(Actor, ObjectClass, *ObjectName);
				Actor->AddOwnedComponent(Component);
				Component->RegisterComponent();

				Object = Component;
			}
			else
			{
				Object = NewObject<UObject>(Outer, ObjectClass, *ObjectName);
			}
		}

		ObjectsArray.SetNumUninitialized(ObjectIdx + 1);
		ObjectsArray[ObjectIdx] = Object;

		FInstancedObjectData& InstancedObjectData = AllInstancedObjectData.AddZeroed_GetRef();
		InstancedObjectData.Object = Object;
		InstancedObjectData.JsonObject = JsonObject;

		TGuardValue<UObject*> OuterGuard(Outer, Object);
		const TSharedPtr<FJsonObject>* SubObjectsJsonObject;
		if (JsonObject->TryGetObjectField(SubObjectsFieldName, SubObjectsJsonObject))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : SubObjectsJsonObject->Get()->Values)
			{
				const FObjectIdx SubObjectIdx = FCString::Atoi(*Pair.Key);
				JsonObjectToInstanceObject(Pair.Value->AsObject().ToSharedRef(), SubObjectIdx);
			}
		}

		return Object;
	}

	UObject* FJsonToStruct::GetObjectByIdx(FObjectIdx ObjectIdx) const
	{
		if (ObjectIdx >= 0)
		{
			UObject* DynamicObject = ObjectsArray[ObjectIdx];
			return DynamicObject;
		}
		else
		{
			UObject* ExternalObject = ExternalObjectsArray[-ObjectIdx];
			return ExternalObject;
		}
	}

	bool FJsonToStruct::JsonObjectIdxToObject(const TSharedPtr<FJsonValue>& JsonValue, FProperty* Property, void* OutValue) const
	{
		if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
		{
			const FObjectIdx ObjectIdx = static_cast<int>(JsonValue->AsNumber());
			ObjectProperty->SetObjectPropertyValue(OutValue, GetObjectByIdx(ObjectIdx));
			return true;
		}
		return false;
	}

	void FJsonToStruct::GetStruct(const TSharedRef<FJsonObject>& JsonObject, const FString& FieldName, UScriptStruct* Struct, void* Value) const
	{
		const TSharedPtr<FJsonObject>* StructJsonObjectPtr;
		if (JsonObject->TryGetObjectField(FieldName, StructJsonObjectPtr))
		{
			const TSharedPtr<FJsonObject>& StructJsonObject = *StructJsonObjectPtr;
			const bool IsLoadSucceed = JsonToStruct::JsonAttributesToUStructWithContainer(StructJsonObject->Values, Struct, Value, Struct, Value, CheckFlags, SkipFlags, FCustomImportCallback::CreateRaw(this, &FJsonToStruct::JsonObjectIdxToObject));
			ensure(IsLoadSucceed);
		}
	}

	FString JsonObjectToString(const TSharedRef<FJsonObject>& JsonObject)
	{
		FString JSONPayload;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> JsonWriter = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&JSONPayload, 0);
		FJsonSerializer::Serialize(JsonObject, JsonWriter);
		JsonWriter->Close();
		return JSONPayload;
	}

	TSharedPtr<FJsonObject> StringToJsonObject(const FString& JsonString)
	{
		TSharedPtr<FJsonObject> JsonObject;
		const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(JsonString);
		FJsonSerializer::Deserialize(JsonReader, JsonObject);
		return JsonObject;
	}
}
