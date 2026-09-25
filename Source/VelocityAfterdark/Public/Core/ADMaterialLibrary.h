#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ADMaterialLibrary.generated.h"

class UMaterial;

/** Editor bootstrap bridge for material inputs hidden from Unreal's Python enum. */
UCLASS()
class VELOCITYAFTERDARK_API UADMaterialLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Afterdark|Content", meta=(DevelopmentOnly))
    static bool ConfigureClearCoat(UMaterial* Material, float Amount, float Roughness);
};
