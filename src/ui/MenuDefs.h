#pragma once
#include <string>
#include <vector>

struct MenuItemDef
{
    std::string label;      // empty string = separator line
    std::string shortcut;   // display string shown on right side of item
};

struct MenuDef
{
    std::string              label;   // top-level menu name
    int                      barCol;  // column on the menu bar where the label starts
    std::vector<MenuItemDef> items;
};

// Menu mnemonic characters (Alt+letter to open each menu)
// Indices match GetMenuDefs() order:
// File=0, Edit=1, Format=2, Search=3, Page=4, Tools=5, Options=6, Insert=7, Help=8
inline char GetMenuMnemonic(int menuIdx)
{
    static const char mnemonics[] = { 'f', 'e', 'r', 's', 'p', 't', 'o', 'i', 'h' };
    if (menuIdx < 0 || menuIdx >= static_cast<int>(sizeof(mnemonics)))
        return '\0';
    return mnemonics[menuIdx];
}

inline const std::vector<MenuDef>& GetMenuDefs()
{
    static const std::vector<MenuDef> s_menus = {
        { "File", 1, {
            { "New",          "^N"   },
            { "Open...",      "^O"   },
            { "Save",         "^S"   },
            { "Save As...",   "^S+S" },
            { "Print...",     "^P"   },
            { "",             ""     },
            { "Exit",         "Esc"  },
        }},
        { "Edit", 7, {
            { "Undo",         "^Z"   },
            { "Redo",         "^Y"   },
            { "",             ""     },
            { "Cut",          "^X"   },
            { "Copy",         "^C"   },
            { "Paste",        "^V"   },
            { "",             ""     },
            { "Select All",   "^A"   },
            { "Find...",      "^F"   },
        }},
        { "Format", 13, {
            { "Bold",                "^B"     },   // shortcut column shows On/Off at draw time
            { "Italic",              "^I"     },
            { "Underline",           "^U"     },
            { "Strikethrough",       ""       },
            { "Text Color...",       ""       },
            { "Highlight Color...",  ""       },
            { "",                    ""       },
            { "Align Left",          "^L"     },
            { "Center",              "^E"     },
            { "Align Right",         "^R"     },
            { "Justify",             "^J"     },
            { "",                    ""       },
            { "Insert Page Break",   "^Enter" },
        }},
        { "Search", 21, {
            { "Find...",      "^F"   },
            { "Find Next",    "F6"   },
        }},
        { "Page", 29, {
            { "Margins...",          ""  },
            // Four independent slots; each shows On/Off live at draw time.
            { "Header: File Name",   ""  },
            { "Header: Page Number", ""  },
            { "Footer: File Name",   ""  },
            { "Footer: Page Number", ""  },
            { "Columns...",          ""  },
        }},
        { "Tools", 35, {
            { "Add to Dictionary...",      "" },
            { "Remove from Dictionary...", "" },
            { "",                          "" },
            { "Check Word...",             "" },
        }},
        { "Options", 42, {
            { "Font...",              ""     },
            { "Theme...",             ""     },
            { "Word Wrap",            ""     },   // shortcut column shows On/Off at draw time
            { "Word Count",           ""     },   // shortcut column shows On/Off at draw time
            { "Spell Check",          ""     },   // shortcut column shows On/Off at draw time
            { "Highlight Misspelled", ""     },   // shortcut column shows On/Off at draw time
            { "Show Margins",         ""     },   // shortcut column shows On/Off at draw time
        }},
        { "Insert", 51, {
            { "Image...",     ""     },
            { "Shape...",     ""     },
            { "Caption...",   ""     },
        }},
        { "Help", 59, {
            { "Help",         "F1"   },
            { "",             ""     },
            { "About...",     ""     },
        }},
    };
    return s_menus;
}

// True for menu items that are in-place On/Off toggles (their dropdown
// shortcut shows live "On"/"Off"). Activating one flips its state in place;
// the dropdown stays open so the user can toggle several without reopening.
// Keep in sync with RetroUi's LiveShortcut, which renders the On/Off label
// for these same items.
inline bool IsToggleMenuItem(int menuIdx, int itemIdx)
{
    // Page menu (idx 4): Header/Footer slots 1..4.
    if (menuIdx == 4 && itemIdx >= 1 && itemIdx <= 4) return true;
    // Options menu (idx 6): Word Wrap, Word Count, Spell Check,
    // Highlight Misspelled, Show Margins (items 2..6).
    if (menuIdx == 6 && itemIdx >= 2 && itemIdx <= 6) return true;
    return false;
}
