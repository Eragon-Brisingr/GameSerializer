// Fill out your copyright notice in the Description page of Project Settings.


#include "GameSerializerCore.h"
#include <JsonObjectWrapper.h>
#include <Internationalization/Culture.h>

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
				Property->ExportTextItem(StringValue, Value, nullptr, nullptr, PPF_None);
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

		static FString StandardizeCase(const FString& StringIn)
		{
			// this probably won't work for all cases, consider downcaseing the string fully
			FString FixedString = StringIn;
			FixedString[0] = FChar::ToLower(FixedString[0]); // our json classes/variable start lower case
			FixedString.ReplaceInline(TEXT("ID"), TEXT("Id"), ESearchCase::CaseSensitive); // Id is standard instead of ID, some of our fnames use ID
			return FixedString;
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

				FString VariableName = StandardizeCase(Property->GetName());
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

namespace GameSerializer
{
	constexpr FObjectIdx NullIdx = 0;
	constexpr FObjectIdx PersistentStartIdx = -TNumericLimits<FObjectIdx>::Max() / 2;

	constexpr EObjectFlags RF_InPackageFlags = RF_ClassDefaultObject | RF_ArchetypeObject | RF_DefaultSubObject | RF_InheritableComponentTemplate | RF_WasLoaded | RF_LoadCompleted;
	constexpr TCHAR ExternalObjectsFieldName[] = TEXT("__ExternalObjects");
	constexpr TCHAR DynamicObjectsFieldName[] = TEXT("__DynamicObjects");
	constexpr TCHAR SubObjectsFieldName[] = TEXT("__SubObjects");
	constexpr TCHAR ObjectIdxFieldName[] = TEXT("__Idx");
	constexpr TCHAR ObjectNameFieldName[] = TEXT("__Name");
	constexpr TCHAR ObjectClassFieldName[] = TEXT("__Class");
	constexpr TCHAR ExtendDataFieldName[] = TEXT("__ExtendData");
	constexpr TCHAR ExtendDataTypeFieldName[] = TEXT("__Type");

	using namespace CustomJsonConverter;

	FStructToJson::FStructToJson()
	{
		RootJsonObject->SetObjectField(ExternalObjectsFieldName, ExternalJsonObject);
		RootJsonObject->SetObjectField(DynamicObjectsFieldName, DynamicJsonObject);
	}

	void FStructToJson::AddObjects(const FString& FieldName, TArray<UObject*> Objects)
	{
		TArray<TSharedPtr<FJsonValue>> ObjectsJsonArray;
		for (UObject* Object : Objects)
		{
			if (Object == nullptr)
			{
				ObjectsJsonArray.Add(MakeShared<FJsonValueNumber>(NullIdx));
			}
			else if (FObjectIdx* ExistObjectIdx = ObjectIdxMap.Find(Object))
			{
				ObjectsJsonArray.Add(MakeShared<FJsonValueNumber>(*ExistObjectIdx));
			}
			else
			{
				const FObjectIdx NewObjectIdx = ObjectUniqueIdx + 1;
				ObjectsJsonArray.Add(MakeShared<FJsonValueNumber>(NewObjectIdx));
				
				const TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
				ObjectToJsonObject(JsonObject, Object);
				DynamicJsonObject->SetObjectField(FString::FromInt(NewObjectIdx), JsonObject);
			}
		}
		RootJsonObject->SetArrayField(FieldName, ObjectsJsonArray);
	}

	void FStructToJson::AddStruct(const FString& FieldName, UScriptStruct* Struct, const void* Value, const void* DefaultValue)
	{
		const TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
		bool bSameValue;
		ensure(StructToJson::UStructToJsonAttributes(Struct, Value, DefaultValue, bSameValue, JsonObject->Values, CheckFlags, SkipFlags, FCustomExportCallback::CreateRaw(this, &FStructToJson::ConvertObjectToJson)));
		RootJsonObject->SetObjectField(FieldName, JsonObject);
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

	void FStructToJson::ObjectToJsonObject(const TSharedRef<FJsonObject>& JsonObject, UObject* Object)
	{
		UClass* Class = Object->GetClass();

		check(ObjectIdxMap.Contains(Object) == false);
		ObjectUniqueIdx += 1;
		ObjectIdxMap.Add(Object, ObjectUniqueIdx);

		JsonObject->SetNumberField(ObjectIdxFieldName, ObjectUniqueIdx);
		JsonObject->SetStringField(ObjectNameFieldName, Object->GetName());
		JsonObject->SetNumberField(ObjectClassFieldName, GetExternalObjectIndex(Class));

		OuterChain.Add(FOuterData(Object, JsonObject));
		bool bSameValue;
		ensure(StructToJson::UStructToJsonAttributes(Class, Object, Class->GetDefaultObject(), bSameValue, JsonObject->Values, CheckFlags, SkipFlags, FCustomExportCallback::CreateRaw(this, &FStructToJson::ConvertObjectToJson)));
		OuterChain.Pop();
	}

	TSharedPtr<FJsonValue> FStructToJson::ConvertObjectToJson(FProperty* Property, const void* Value, const void* Default, bool& bSameValue)
	{
		if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
		{
			UObject* SubObject = ObjectProperty->GetPropertyValue(Value);

			if (Default)
			{
				UObject* DefaultSubObject = ObjectProperty->GetPropertyValue(Default);
				bSameValue = SubObject == DefaultSubObject;
			}
			
			if (SubObject != nullptr)
			{
				// 存在于包内的数据记录路径即可
				// TODO：找到直接判断是否在持久性包内的方法
				if (SubObject->IsAsset() || SubObject->IsA<UStruct>())
				{
					const FObjectIdx ExternalObjectIdx = GetExternalObjectIndex(SubObject);
					return MakeShared<FJsonValueNumber>(ExternalObjectIdx);
				}

				// 能找到Outer的储存所有数据
				for (FObjectIdx Idx = OuterChain.Num() - 1; Idx >= 0; --Idx)
				{
					const FOuterData& TestOuterData = OuterChain[Idx];
					if (SubObject->GetOuter() == TestOuterData.Outer)
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

						if (ObjectIdxMap.Contains(SubObject))
						{
							return MakeShared<FJsonValueNumber>(ObjectIdxMap[SubObject]);
						}
						else
						{
							const FObjectIdx ObjectIdx = ObjectUniqueIdx + 1;
							const TSharedRef<FJsonObject> SubObjectJsonObject = MakeShared<FJsonObject>();
							
							if (SubObject->Implements<UGameSerializerInterface>())
							{
								const TSharedRef<FJsonObject> ExtendDataContainerJsonObject = MakeShared<FJsonObject>();

								const FGameSerializerExtendDataContainer ExtendDataContainer = IGameSerializerInterface::WhenGamePreSave(SubObject);
								const FObjectIdx StructIdx = GetExternalObjectIndex(ExtendDataContainer.Struct);
								ExtendDataContainerJsonObject->SetNumberField(ExtendDataTypeFieldName, StructIdx);

								if (ExtendDataContainer.Struct && ensure(ExtendDataContainer.ExtendData.IsValid()))
								{
									FGameSerializerExtendData* DefaultExtendData = static_cast<FGameSerializerExtendData*>(FMemory::Malloc(ExtendDataContainer.Struct->GetStructureSize()));
									ExtendDataContainer.Struct->InitializeStruct(DefaultExtendData);
									bool bSubObjectSameValue;
									ensure(StructToJson::UStructToJsonAttributes(ExtendDataContainer.Struct, ExtendDataContainer.ExtendData.Get(), DefaultExtendData, bSubObjectSameValue, ExtendDataContainerJsonObject->Values, CheckFlags, SkipFlags, FCustomExportCallback::CreateRaw(this, &FStructToJson::ConvertObjectToJson)));
									ExtendDataContainer.Struct->DestroyStruct(DefaultExtendData);
									FMemory::Free(DefaultExtendData);

									if (bSubObjectSameValue == false)
									{
										SubObjectJsonObject->SetObjectField(ExtendDataFieldName, ExtendDataContainerJsonObject);
									}
								}
							}
							ObjectToJsonObject(SubObjectJsonObject, SubObject);
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

	FJsonToStruct::FJsonToStruct(UObject* Outer, const TSharedRef<FJsonObject>& RootJsonObject)
		: Outer(Outer)
	    , RootJsonObject(RootJsonObject)
	{
		// 加载ExternalObject
		{
#if WITH_EDITOR
			if (Outer)
			{
				const int32 PIEInstanceID = Outer->GetOutermost()->PIEInstanceID;
				TGuardValue<int32> GPlayInEditorIDGuard(GPlayInEditorID, PIEInstanceID);
			}
#endif
			const TSharedPtr<FJsonObject> ExternalObjectJsonObject = RootJsonObject->GetObjectField(ExternalObjectsFieldName);
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : ExternalObjectJsonObject->Values)
			{
				const FObjectIdx Idx = -FCString::Atoi(*Pair.Key);
				ExternalObjectsArray.SetNumUninitialized(Idx + 1);

				const FString SoftObjectPathString = Pair.Value->AsString();
				const TCHAR* Buffer = *SoftObjectPathString;

				FSoftObjectPath SoftObjectPath;
				SoftObjectPath.ImportTextItem(Buffer, PPF_None, nullptr, nullptr);

				UObject* ExternalObject = SoftObjectPath.TryLoad();
				ensure(ExternalObject);
				ExternalObjectsArray[Idx] = ExternalObject;
			}
		}

		// 实例化Object
		{
			const TSharedPtr<FJsonObject> DynamicJsonObject = RootJsonObject->GetObjectField(DynamicObjectsFieldName);
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : DynamicJsonObject->Values)
			{
				const FObjectIdx Idx = FCString::Atoi(*Pair.Key);
				JsonObjectToInstanceObject(Pair.Value->AsObject().ToSharedRef(), Idx);
			}
		}

		// 读取实例化的Object数据
		{
			const FCustomImportCallback JsonObjectIdxToObject = FCustomImportCallback::CreateLambda([this](const TSharedPtr<FJsonValue>& JsonValue, FProperty* Property, void* OutValue)
			{
				if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
				{
					const FObjectIdx ObjectIdx = static_cast<int>(JsonValue->AsNumber());
					ObjectProperty->SetObjectPropertyValue(OutValue, GetObjectByIdx(ObjectIdx));
					return true;
				}
				return false;
			});

			for (const FInstancedObjectData& InstancedObjectData : InstancedObjectDatas)
			{
				UClass* Class = InstancedObjectData.Object->GetClass();
				UObject* LoadedObject = InstancedObjectData.Object;

				ensure(JsonToStruct::JsonAttributesToUStructWithContainer(InstancedObjectData.JsonObject->Values, Class, LoadedObject, Class, LoadedObject, CheckFlags, SkipFlags, JsonObjectIdxToObject));
			}

			for (const FInstancedObjectData& InstancedObjectData : InstancedObjectDatas)
			{
				UObject* LoadedObject = InstancedObjectData.Object;
				if (LoadedObject->Implements<UGameSerializerInterface>())
				{
					const TSharedPtr<FJsonObject>* ExtendDataJsonObject;
					if (InstancedObjectData.JsonObject->TryGetObjectField(ExtendDataFieldName, ExtendDataJsonObject))
					{
						UScriptStruct* Struct = CastChecked<UScriptStruct>(ExternalObjectsArray[-int32(ExtendDataJsonObject->Get()->GetNumberField(ExtendDataTypeFieldName))]);
						FGameSerializerExtendData* ExtendData = static_cast<FGameSerializerExtendData*>(FMemory::Malloc(Struct->GetStructureSize()));
						Struct->InitializeStruct(ExtendData);
						ensure(JsonToStruct::JsonAttributesToUStructWithContainer(ExtendDataJsonObject->Get()->Values, Struct, ExtendData, Struct, ExtendData, CheckFlags, SkipFlags, JsonObjectIdxToObject));
						FGameSerializerExtendDataContainer DataContainer;
						DataContainer.Struct = Struct;
						DataContainer.ExtendData = MakeShareable(ExtendData);
						IGameSerializerInterface::WhenGamePostLoad(LoadedObject, DataContainer);
					}
					else
					{
						IGameSerializerInterface::WhenGamePostLoad(LoadedObject, FGameSerializerExtendDataContainer());
					}
				}
			}

			for (AActor* SpawnedActor : SpawnedActors)
			{
				check(SpawnedActor);
				SpawnedActor->FinishSpawning(SpawnedActor->GetActorTransform());
			}
		}
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

	UObject* FJsonToStruct::JsonObjectToInstanceObject(const TSharedRef<FJsonObject>& JsonObject, FObjectIdx ObjectIdx)
	{
		const FString ObjectName = JsonObject->GetStringField(ObjectNameFieldName);
		UClass* ObjectClass = CastChecked<UClass>(ExternalObjectsArray[-JsonObject->GetIntegerField(ObjectClassFieldName)]);

		UObject* Object = FindObject<UObject>(Outer, *ObjectName);

		if (Object && Object->IsPendingKill())
		{
			Object->Rename(nullptr, GetTransientPackage());
			Object = nullptr;
		}
		
		if (Object)
		{
			ensure(Object->IsA(ObjectClass));
		}
		else
		{
			if (ObjectClass->IsChildOf<AActor>())
			{
				ULevel* Level = CastChecked<ULevel>(Outer);

				FActorSpawnParameters ActorSpawnParameters;
				ActorSpawnParameters.OverrideLevel = Level;
				ActorSpawnParameters.bDeferConstruction = true;
				ActorSpawnParameters.Name = *ObjectName;

				AActor* Actor = Level->GetWorld()->SpawnActor<AActor>(ObjectClass, ActorSpawnParameters);
				SpawnedActors.Add(Actor);
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

		FInstancedObjectData& InstancedObjectData = InstancedObjectDatas.AddZeroed_GetRef();
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
