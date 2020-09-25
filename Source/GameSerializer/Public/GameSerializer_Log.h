// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

DECLARE_LOG_CATEGORY_EXTERN(GameSerializer_Log, Log, All);

DECLARE_STATS_GROUP(TEXT("GameSerializer"), STATGROUP_GameSerializer, STATCAT_Advanced);

#define GameSerializerStatLog(Stat) \
	SCOPE_CYCLE_COUNTER(Stat); \
	FGameSerializerStatScope Stat##GameSerializerStatScope(GET_STATID(Stat))

struct FGameSerializerStatScope
{
	double StartTime = 0.0;
	const TStatId StatId;
	FGameSerializerStatScope(const TStatId& TStatId)
		: StatId(TStatId)
	{
		StartTime = FPlatformTime::Seconds();
	}
	~FGameSerializerStatScope()
	{
		const double DiffSeconds = FPlatformTime::Seconds() - StartTime;
		UE_LOG(GameSerializer_Log, Display, TEXT("GameSerializerStat [%.2f ms] %s"), DiffSeconds * 1000.f, StatId.GetStatDescriptionWIDE());
	}
};
