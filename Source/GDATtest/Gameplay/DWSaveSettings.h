#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DWSaveSettings.generated.h"

/** Packaged Windows save location; independent of the executable's directory. */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="DoughWorld Saves"))
class GDATTEST_API UDWSaveSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    virtual FName GetCategoryName() const override { return TEXT("Project"); }
    UPROPERTY(Config, EditAnywhere, Category="Save Location", meta=(DisplayName="Documents Folder Name", ToolTip="Folder inside the current user's Documents. Use one folder name, not a path. Restart/repackage after changing. Existing folders and saves are preserved; renaming does not move them."))
    FString DocumentsFolderName = TEXT("DoughWorld");
#if WITH_EDITORONLY_DATA
    /** PIE only; packaged and standalone games never enable iteration storage. */
    UPROPERTY(Config, EditAnywhere, Category="Editor Testing", meta=(DisplayName="Editor Map Iteration Saves"))
    bool bUseEditorMapIterationSaves=true;
#endif
};
