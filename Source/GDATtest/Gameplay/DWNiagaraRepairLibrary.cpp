#include "DWNiagaraRepairLibrary.h"
#include "NiagaraSystem.h"
#include "UObject/Package.h"

bool UDWNiagaraRepairLibrary::ForceCompileSystem(UNiagaraSystem* System)
{
#if WITH_EDITOR
    if(!System||!System->GetOutermost()->GetName().StartsWith(TEXT("/Game/")))return false;
    System->RequestCompile(true);
    return true;
#else
    return false;
#endif
}
