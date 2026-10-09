// View > Folder Tree in foobar2000's main menu. Commands go to the panel focused last, so they
// also work as keyboard shortcuts (Preferences > Keyboard Shortcuts) from anywhere.

#include <helpers/foobar2000+atl.h>

#include <iterator>

#include "../guids.h"
#include "now_playing.h"
#include "tree_view.h"

namespace filetree::view {
namespace {

struct CommandInfo {
    const GUID* guid;
    const char* name;
    const char* description;
    TreeView::Command command;
};

const CommandInfo commands[] = {
    {&guids::menu_show_playing, "Show now playing",
     "Opens the folders down to the playing track in the Folder Tree panel and selects it.",
     TreeView::Command::show_playing},
    {&guids::menu_refresh, "Refresh", "Checks every open folder in the Folder Tree panel again.",
     TreeView::Command::refresh},
    {&guids::menu_collapse_all, "Collapse all", "Closes every folder in the Folder Tree panel.",
     TreeView::Command::collapse_all},
    {&guids::menu_new_folder, "New folder",
     "Creates a folder in the folder selected in the Folder Tree panel.",
     TreeView::Command::new_folder},
};

class menu_commands : public mainmenu_commands {
public:
    t_uint32 get_command_count() override { return static_cast<t_uint32>(std::size(commands)); }
    GUID get_command(t_uint32 index) override {
        return index < std::size(commands) ? *commands[index].guid : pfc::guid_null;
    }
    void get_name(t_uint32 index, pfc::string_base& out) override {
        if (index < std::size(commands)) out = commands[index].name;
    }
    bool get_description(t_uint32 index, pfc::string_base& out) override {
        if (index >= std::size(commands)) return false;
        out = commands[index].description;
        return true;
    }
    GUID get_parent() override { return guids::menu_group; }
    bool get_display(t_uint32 index, pfc::string_base& out, t_uint32& flags) override {
        if (index >= std::size(commands)) return false;
        get_name(index, out);
        flags = 0;
        const bool playing_missing = commands[index].command == TreeView::Command::show_playing &&
                                     now_playing::path().empty();
        if (TreeView::active() == nullptr || playing_missing) flags = flag_disabled;
        return true;
    }
    void execute(t_uint32 index, service_ptr_t<service_base>) override {
        if (index >= std::size(commands)) return;
        if (TreeView* view = TreeView::active()) view->run_command(commands[index].command);
    }
};

FB2K_SERVICE_FACTORY(menu_commands);

mainmenu_group_popup_factory g_menu_group(guids::menu_group, mainmenu_groups::view,
                                          mainmenu_commands::sort_priority_dontcare,
                                          "Folder Tree");

} // namespace
} // namespace filetree::view
