// Component identity. Exactly one DECLARE_COMPONENT_VERSION per DLL.
//
// SDK headers are included with angle brackets on purpose: the project marks angle includes as
// external and silences warnings in them, so /W4-as-errors applies to our code only.
//
// There is deliberately no initquit yet. on_init has a 2 ms budget and the tree is built lazily
// on first show, so nothing needs to happen at startup.

#include <helpers/foobar2000+atl.h>

#include "version.h"

DECLARE_COMPONENT_VERSION(FILETREE_NAME, FILETREE_VERSION,
                          "A folder tree panel for foobar2000 v2.\n"
                          "Hosted in Default UI and Columns UI.\n\n"
                          "Inspired by foo_uie_explorer. No third-party component dependencies.");

// Stops users from renaming the DLL, which would confuse the troubleshooter.
VALIDATE_COMPONENT_FILENAME("foo_filetree.dll");
