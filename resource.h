#pragma once

// Resource IDs, shared by foo_filetree.rc and the C++ side. Included by the resource compiler:
// #define only. IDC_STATIC (-1) comes from winres.h.

#define IDD_PREFERENCES 101
// The tabs, consecutive and in tab order.
#define IDD_TAB_GENERAL 102
#define IDD_TAB_DISPLAY 103
#define IDD_TAB_FILTER 104

#define IDC_TABS 1000
#define IDC_PAGE_HOST 1001

// General
#define IDC_TEMP_PLAYLIST 1010
#define IDC_RECURSIVE 1011
#define IDC_DRIVES_LABEL 1012
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

// Filter
#define IDC_FILES 1040
#define IDC_SHOW_HIDDEN 1041
#define IDC_SHOW_SYSTEM 1042
#define IDC_ALWAYS_SHOW 1043
#define IDC_NEVER_SHOW 1044
#define IDC_HIDE_PATTERNS 1045
