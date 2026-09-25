#include "Core/ADMaterialLibrary.h"
#include "Materials/Material.h"

bool UADMaterialLibrary::ConfigureClearCoat(UMaterial* Material, float Amount, float Roughness)
{
#if WITH_EDITOR
    if (!IsValid(Material) || !FMath::IsFinite(Amount) || !FMath::IsFinite(Roughness)) return false;
    UMaterialEditorOnlyData* Data=Material->GetEditorOnlyData();
    if (!Data) return false;
    Material->PreEditChange(nullptr);
    Material->SetShadingModel(MSM_ClearCoat);
    Data->ClearCoat.UseConstant=true;
    Data->ClearCoat.Constant=FMath::Clamp(Amount,0.f,1.f);
    Data->ClearCoatRoughness.UseConstant=true;
    Data->ClearCoatRoughness.Constant=FMath::Clamp(Roughness,0.f,1.f);
    Material->PostEditChange();
    return true;
#else
    return false;
#endif
}
