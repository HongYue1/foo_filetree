#pragma once

// Global action settings, stored in cfg vars. Main thread only. Preferences (M5) edit them.

#include <pfc/pfc-lite.h>

#include "action.h"

namespace filetree::actions {

//! Current bindings, decoded once and cached.
const Bindings& bindings();
void set_bindings(const Bindings& bindings);

//! Name of the temporary playlist ("Folder Tree" by default), UTF-8.
pfc::string8 temp_playlist_name();
void set_temp_playlist_name(const char* name);

//! Whether folders are sent with their subfolders when an action says "by default".
bool recursive_by_default();
void set_recursive_by_default(bool recursive);

} // namespace filetree::actions
