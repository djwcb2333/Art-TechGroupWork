#include "DWGameplayGameMode.h"
#include "DWPlayerCharacter.h"
#include "DWPlayerController.h"
#include "DWGameplayHUD.h"
ADWGameplayGameMode::ADWGameplayGameMode(){DefaultPawnClass=ADWPlayerCharacter::StaticClass();PlayerControllerClass=ADWPlayerController::StaticClass();HUDClass=ADWGameplayHUD::StaticClass();}
