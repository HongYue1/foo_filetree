#pragma once

// Resource IDs, shared by foo_filetree.rc and the C++ side. Included by the resource compiler:
// #define only. IDC_STATIC (-1) comes from winres.h.

#define IDD_PREFERENCES 101
// The tabs, consecutive and in tab order.
#define IDD_TAB_GENERAL 102
#define IDD_TAB_DISPLAY 103
#define IDD_TAB_FILTER 104
#define IDD_TAB_ACTIONS 105
#define IDD_TAB_MENU 106
#define IDD_TAB_FAVOURITES 107

#define IDC_TABS 1000
#define IDC_PAGE_HOST 1001

// General
#define IDC_TEMP_PLAYLIST 1010
#define IDC_RECURSIVE 1011
#define IDC_DRIVES_LABEL 1012
#define IDC_SHOW_ADDRESS_BAR 1013
#define IDC_FILTER_BOX 1014
#define IDC_STARTUP 1015
#define IDC_STARTUP_FOLDER 1016
#define IDC_STARTUP_BROWSE 1017
#define IDC_WATCH_CHANGES 1018
// Drive check boxes are created at run time: IDC_DRIVE_FIRST + 0 (A:) ... + 25 (Z:).
#define IDC_DRIVE_FIRST 1100

// Display
#define IDC_LINES 1020
#define IDC_LINE_THICKNESS 1021
#define IDC_LINE_FOLLOW 1022
#define IDC_LINE_OPACITY 1023
#define IDC_LINE_CUSTOM 1024
#define IDC_LINE_SWATCH 1025
#define IDC_LINE_HEX 1026
#define IDC_ROW_PADDING 1027
#define IDC_EXTENSIONS 1028
#define IDC_SORT_FIELD 1029
#define IDC_FOLDERS_FIRST 1030
#define IDC_SORT_REVERSE 1031
#define IDC_SHOW_ICONS 1032
#define IDC_MARK_FAVOURITES 1033

// Filter
#define IDC_FILES 1040
#define IDC_SHOW_HIDDEN 1041
#define IDC_SHOW_SYSTEM 1042
#define IDC_ALWAYS_SHOW 1043
#define IDC_NEVER_SHOW 1044
#define IDC_HIDE_PATTERNS 1045

// Actions: one drop-down per gesture and kind, IDC_BIND_FIRST + gesture * 2 + (0 folder, 1 file).
#define IDC_BIND_FIRST 1050

// Menu
#define IDC_MENU_LIST 1060
#define IDC_MENU_UP 1061
#define IDC_MENU_DOWN 1062
#define IDC_MENU_SHOW 1063

// Favourites tab
#define IDC_FAV_LIST 1070
#define IDC_FAV_ADD 1071
#define IDC_FAV_REMOVE 1072
#define IDC_FAV_UP 1073
#define IDC_FAV_DOWN 1074
#define IDC_FAV_PLACE 1075
#define IDC_FAV_SEPARATE 1076
#define IDC_FAV_GAP 1077
#define IDC_MARK_PLAYING 1078
#define IDC_ICON_RAISE 1079
#define IDC_MARK_RAISE 1080
