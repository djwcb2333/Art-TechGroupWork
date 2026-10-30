#include "DWGatheringPond.h"
bool ADWGatheringPond::CanPersistPondConfiguration() const
{
    return IsConfigurationValid();
}
// Only called after the aggregate has validated the saved amount and entered its barrier.
void ADWGatheringPond::AdoptPondPersistenceFacts(double SavedRemaining)
{
    RemainingWater=SavedRemaining;
    bWaterRestored=true;
    bIsDry=SavedRemaining<=0.;
}
// Publish occurs only after the aggregate barrier has been released.
void ADWGatheringPond::PublishPondPersistenceFacts()
{
    ClearPondVFX();
    RefreshPlaceholder();
    RefreshBankAppearance();
    if(RemainingWater<=0.)ReleaseAllEnvironmentSlow();
    RefreshPromptAvailability();
}
