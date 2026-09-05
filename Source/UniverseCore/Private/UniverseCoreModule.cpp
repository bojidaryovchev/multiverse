// Copyright Universe Project. All Rights Reserved.

#include "Modules/ModuleManager.h"

// No custom startup work: the core is stateless by design, so there is nothing
// to initialise and nothing that could differ between two runs.
IMPLEMENT_MODULE(FDefaultModuleImpl, UniverseCore);
