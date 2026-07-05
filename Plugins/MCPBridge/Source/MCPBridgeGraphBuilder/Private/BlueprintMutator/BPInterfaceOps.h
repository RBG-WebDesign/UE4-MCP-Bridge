// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
class UBlueprint;
class UClass;

class FBPInterfaceOps
{
public:
    static bool AddInterfaceImplementation(UBlueprint* BP, UClass* InterfaceClass);
    static bool RemoveInterfaceImplementation(UBlueprint* BP, UClass* InterfaceClass);
};
