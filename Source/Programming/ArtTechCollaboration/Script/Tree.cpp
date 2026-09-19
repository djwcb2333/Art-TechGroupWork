#include "Tree.h"

ATree::ATree()
{
    PrimaryActorTick.bCanEverTick = false;
}

void ATree::Chop()
{
    Destroy();
}