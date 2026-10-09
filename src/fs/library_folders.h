#pragma once

// The Media Library as Folder Tree uses it: its folders as roots and, later, which folders hold
// tracks. Main thread only. Nothing happens until a panel asks (library_index()); from then on
// library changes rebuild the index on a worker, debounced, and the listener is told.

#include <memory>

#include "../model/library_index.h"

namespace filetree::fs {

//! The last index built, or null while the first one is being built (or the library is off).
//! The first call starts following the library.
std::shared_ptr<const model::LibraryIndex> library_index();

//! Called on the main thread whenever a new index is in. One listener (the views' registry).
void set_library_listener(void (*listener)());

} // namespace filetree::fs
