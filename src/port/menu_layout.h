#ifndef OPENCMR2_PORT_MENU_LAYOUT_H
#define OPENCMR2_PORT_MENU_LAYOUT_H

#include "port/types.h"
#include "port/menu_layout_abi.h"

#if defined(__cplusplus)
#include <type_traits>
#endif

/*
 * PORT: AMIGA: Menu/MenuItem layout override.
 *
 * The original MSVC build allocates the flag bitfields from the least
 * significant bit. Menu_Update reads Menu's flag byte directly through
 * MENU_FLAGS, so the in-memory byte must stay identical:
 *
 *   bit0 flag0 (prev/next)   bit4 flag4 (wrap)
 *   bit1 flag1 (left/right)  bit5 flag5
 *   bit2 flag2 (select)      bit6 flag6
 *   bit3 flag3 (back)        bit7 flag7
 *
 * 68k GCC allocates bitfields from the most significant bit and
 * #pragma scalar_storage_order does not change bitfield order (verified by
 * disassembly). Declaring the bitfields in reverse order makes GCC's MSB-first
 * allocation produce the original LSB-first byte. Field names and the rest of
 * the structure are unchanged.
 *
 * The same applies to MenuItem's flags byte:
 *   bit0 enabled, bit1 visible, bit2 flag2, bit3 flag3, bits4-7 flag4.
 *
 * This header defines the game header's include guard on purpose: every
 * Domain A C++ translation unit force-includes it before game/Menu.h, and the
 * real header must not be included afterwards (that would redefine the types
 * with the wrong bitfield order). Keep the declarations below in sync with
 * game/Menu.h; the static asserts catch size/offset drift.
 */
#ifdef _MENU_H
#error "port/menu_layout.h must be included before game/Menu.h"
#endif
#define _MENU_H

struct Menu;
struct MenuItem;

typedef void (*MenuCallback)(Menu *pMenu, int param);
typedef void (*MenuItemCallback)(Menu *pMenu, MenuItem *pItem, int param);
typedef void (*MenuItemAction)(Menu *pMenu, MenuItem *pItem);

struct MenuItemCallbacks {
    MenuItemCallback pfnMove;       // 0x0
    MenuItemCallback pfnChange;     // 0x4
    MenuItemCallback pfnSelect;     // 0x8
    MenuItemCallback pfnBack;       // 0xc
};

struct MenuItem {
    char *text;             // 0x0
    short id;               // 0x4
    BYTE flag4 : 4;         // 0x6 memory bits 4-7
    BYTE flag3 : 1;         // 0x6 memory bit3
    BYTE flag2 : 1;         // 0x6 memory bit2
    BYTE visible : 1;       // 0x6 memory bit1
    BYTE enabled : 1;       // 0x6 memory bit0
    BYTE type;              // 0x7
    short value;            // 0x8
    BYTE min;               // 0xa
    BYTE max;               // 0xb
    Menu *pSubMenu;         // 0xc
    MenuItemAction action;  // 0x10
};

struct Menu {
    char *text;             // 0x0
    short field_0x4;
    char itemCount;         // 0x6
    char cursor;            // 0x7
    Menu *pParent;          // 0x8
    char defaultCursor;     // 0xc
    BYTE layout;            // 0xd
    BYTE flag7 : 1;         // 0xe memory bit7 (userData == -1)
    BYTE flag6 : 1;         // 0xe memory bit6
    BYTE flag5 : 1;         // 0xe memory bit5
    BYTE flag4 : 1;         // 0xe memory bit4 (wrap)
    BYTE flag3 : 1;         // 0xe memory bit3 (back)
    BYTE flag2 : 1;         // 0xe memory bit2 (select)
    BYTE flag1 : 1;         // 0xe memory bit1 (left/right)
    BYTE flag0 : 1;         // 0xe memory bit0 (prev/next)
    BYTE moveFlags;         // 0xf
    BYTE value;             // 0x10
    BYTE field_0x11[3];
    MenuItem items[22];     // 0x14
    MenuCallback pfnCallback0;      // 0x1cc
    MenuCallback pfnCallback1;      // 0x1d0
    MenuCallback pfnCallback2;      // 0x1d4
    MenuCallback pfnCallback3;      // 0x1d8
    MenuItemCallbacks *pItemCallbacks;  // 0x1dc
};

#if defined(__cplusplus)
static_assert(sizeof(MenuItem) == OPENCMR2_MENU_ITEM_SIZE, "MenuItem layout drifted from game/Menu.h");
static_assert(sizeof(Menu) == OPENCMR2_MENU_SIZE, "Menu layout drifted from game/Menu.h");
static_assert(offsetof(MenuItem, text) == OPENCMR2_MENU_ITEM_TEXT_OFFSET, "MenuItem::text offset drifted");
static_assert(offsetof(MenuItem, id) == OPENCMR2_MENU_ITEM_ID_OFFSET, "MenuItem::id offset drifted");
static_assert(offsetof(MenuItem, type) == OPENCMR2_MENU_ITEM_TYPE_OFFSET, "MenuItem::type offset drifted");
static_assert(offsetof(MenuItem, value) == OPENCMR2_MENU_ITEM_VALUE_OFFSET, "MenuItem::value offset drifted");
static_assert(offsetof(MenuItem, min) == OPENCMR2_MENU_ITEM_MIN_OFFSET, "MenuItem::min offset drifted");
static_assert(offsetof(MenuItem, max) == OPENCMR2_MENU_ITEM_MAX_OFFSET, "MenuItem::max offset drifted");
static_assert(offsetof(MenuItem, pSubMenu) == OPENCMR2_MENU_ITEM_SUBMENU_OFFSET, "MenuItem::pSubMenu offset drifted");
static_assert(offsetof(MenuItem, action) == OPENCMR2_MENU_ITEM_ACTION_OFFSET, "MenuItem::action offset drifted");
static_assert(offsetof(Menu, text) == OPENCMR2_MENU_TEXT_OFFSET, "Menu::text offset drifted");
static_assert(offsetof(Menu, field_0x4) == OPENCMR2_MENU_FIELD_0X4_OFFSET, "Menu::field_0x4 offset drifted");
static_assert(offsetof(Menu, itemCount) == OPENCMR2_MENU_ITEMCOUNT_OFFSET, "Menu::itemCount offset drifted");
static_assert(offsetof(Menu, cursor) == OPENCMR2_MENU_CURSOR_OFFSET, "Menu::cursor offset drifted");
static_assert(offsetof(Menu, pParent) == OPENCMR2_MENU_PARENT_OFFSET, "Menu::pParent offset drifted");
static_assert(offsetof(Menu, defaultCursor) == OPENCMR2_MENU_DEFAULTCURSOR_OFFSET, "Menu::defaultCursor offset drifted");
static_assert(offsetof(Menu, layout) == OPENCMR2_MENU_LAYOUT_OFFSET, "Menu::layout offset drifted");
static_assert(offsetof(Menu, moveFlags) == OPENCMR2_MENU_MOVEFLAGS_OFFSET, "Menu::moveFlags offset drifted");
static_assert(offsetof(Menu, value) == OPENCMR2_MENU_VALUE_OFFSET, "Menu::value offset drifted");
static_assert(offsetof(Menu, field_0x11) == OPENCMR2_MENU_FIELD_0X11_OFFSET, "Menu::field_0x11 offset drifted");
static_assert(offsetof(Menu, items) == OPENCMR2_MENU_ITEMS_OFFSET, "Menu::items offset drifted");
static_assert(offsetof(Menu, pfnCallback0) == OPENCMR2_MENU_CALLBACK0_OFFSET, "Menu::pfnCallback0 offset drifted");
static_assert(offsetof(Menu, pfnCallback1) == OPENCMR2_MENU_CALLBACK1_OFFSET, "Menu::pfnCallback1 offset drifted");
static_assert(offsetof(Menu, pfnCallback2) == OPENCMR2_MENU_CALLBACK2_OFFSET, "Menu::pfnCallback2 offset drifted");
static_assert(offsetof(Menu, pfnCallback3) == OPENCMR2_MENU_CALLBACK3_OFFSET, "Menu::pfnCallback3 offset drifted");
static_assert(offsetof(Menu, pItemCallbacks) == OPENCMR2_MENU_ITEMCALLBACKS_OFFSET, "Menu::pItemCallbacks offset drifted");
#endif

void Menu_PlaySound(int id);
void Menu_Init(Menu *pMenu, char *text, short param3, int param4, Menu *pParent, MenuItemCallbacks *pItemCallbacks, BYTE flag4, BYTE defaultCursor, BYTE layout);
void Menu_ClearNextItem(Menu *pMenu);
void Menu_AddItemType3(Menu *pMenu, char *text, short id, BYTE min, BYTE max, BYTE flag2, int unused, MenuItemAction action, short value);
void Menu_AddItemType6(Menu *pMenu, char *text, short id, BYTE min, BYTE max, BYTE flag2, int unused, MenuItemAction action, short value);
void Menu_AddItemType4(Menu *pMenu, char *text, short id, MenuItemAction action, short value);
void Menu_AddItemType1(Menu *pMenu, char *text, short id, MenuItemAction action, short value);
void Menu_AddItemType2(Menu *pMenu, char *text, short id, Menu *pSubMenu, MenuItemAction action, short value);
void Menu_SetCursor(Menu *pMenu, BYTE cursor);
void Menu_SetParent(Menu *pMenu, Menu *pParent);
int Menu_FindItem(Menu *pMenu, int id);
MenuItem *Menu_GetItem(Menu *pMenu, int id);
void Menu_SetItemSubMenu(Menu *pMenu, int id, Menu *pSubMenu);
void Menu_SelectItem(Menu *pMenu, int id);
void Menu_SetCallbacks(Menu *pMenu, MenuCallback pfn0, MenuCallback pfn1, MenuCallback pfn2, MenuCallback pfn3);
void Menu_SetFlags(Menu *pMenu, BYTE bit0, BYTE bit1, BYTE bit2, BYTE bit3);
void Menu_ValidateCursor(Menu *pMenu, int unused);
void Menu_CallCallback0(Menu *pMenu);
void Menu_CallCallback3(Menu *pMenu);
void Menu_CallCallback2(Menu *pMenu);
Menu *Menu_Update(Menu *pMenu, unsigned int input);
void Menu_SetNextAction(Menu *action);
void Menu_GoBack(Menu *pMenu);
void Menu_PlaySoundId(int id);
void Menu_SetInputStateFlag(char param1);
void Menu_SetActionLatch(char param1);

#if defined(__cplusplus)
static_assert(std::is_same<decltype(&Menu_PlaySound), OpenCMR2MenuPlaySoundFn>::value, "Menu_PlaySound prototype drifted");
static_assert(std::is_same<decltype(&Menu_Init), OpenCMR2MenuInitFn>::value, "Menu_Init prototype drifted");
static_assert(std::is_same<decltype(&Menu_ClearNextItem), OpenCMR2MenuClearNextItemFn>::value, "Menu_ClearNextItem prototype drifted");
static_assert(std::is_same<decltype(&Menu_AddItemType3), OpenCMR2MenuAddItemType3Fn>::value, "Menu_AddItemType3 prototype drifted");
static_assert(std::is_same<decltype(&Menu_AddItemType6), OpenCMR2MenuAddItemType6Fn>::value, "Menu_AddItemType6 prototype drifted");
static_assert(std::is_same<decltype(&Menu_AddItemType4), OpenCMR2MenuAddItemType4Fn>::value, "Menu_AddItemType4 prototype drifted");
static_assert(std::is_same<decltype(&Menu_AddItemType1), OpenCMR2MenuAddItemType1Fn>::value, "Menu_AddItemType1 prototype drifted");
static_assert(std::is_same<decltype(&Menu_AddItemType2), OpenCMR2MenuAddItemType2Fn>::value, "Menu_AddItemType2 prototype drifted");
static_assert(std::is_same<decltype(&Menu_SetCursor), OpenCMR2MenuSetCursorFn>::value, "Menu_SetCursor prototype drifted");
static_assert(std::is_same<decltype(&Menu_SetParent), OpenCMR2MenuSetParentFn>::value, "Menu_SetParent prototype drifted");
static_assert(std::is_same<decltype(&Menu_FindItem), OpenCMR2MenuFindItemFn>::value, "Menu_FindItem prototype drifted");
static_assert(std::is_same<decltype(&Menu_GetItem), OpenCMR2MenuGetItemFn>::value, "Menu_GetItem prototype drifted");
static_assert(std::is_same<decltype(&Menu_SetItemSubMenu), OpenCMR2MenuSetItemSubMenuFn>::value, "Menu_SetItemSubMenu prototype drifted");
static_assert(std::is_same<decltype(&Menu_SelectItem), OpenCMR2MenuSelectItemFn>::value, "Menu_SelectItem prototype drifted");
static_assert(std::is_same<decltype(&Menu_SetCallbacks), OpenCMR2MenuSetCallbacksFn>::value, "Menu_SetCallbacks prototype drifted");
static_assert(std::is_same<decltype(&Menu_SetFlags), OpenCMR2MenuSetFlagsFn>::value, "Menu_SetFlags prototype drifted");
static_assert(std::is_same<decltype(&Menu_ValidateCursor), OpenCMR2MenuValidateCursorFn>::value, "Menu_ValidateCursor prototype drifted");
static_assert(std::is_same<decltype(&Menu_CallCallback0), OpenCMR2MenuCallCallback0Fn>::value, "Menu_CallCallback0 prototype drifted");
static_assert(std::is_same<decltype(&Menu_CallCallback3), OpenCMR2MenuCallCallback3Fn>::value, "Menu_CallCallback3 prototype drifted");
static_assert(std::is_same<decltype(&Menu_CallCallback2), OpenCMR2MenuCallCallback2Fn>::value, "Menu_CallCallback2 prototype drifted");
static_assert(std::is_same<decltype(&Menu_Update), OpenCMR2MenuUpdateFn>::value, "Menu_Update prototype drifted");
static_assert(std::is_same<decltype(&Menu_SetNextAction), OpenCMR2MenuSetNextActionFn>::value, "Menu_SetNextAction prototype drifted");
static_assert(std::is_same<decltype(&Menu_GoBack), OpenCMR2MenuGoBackFn>::value, "Menu_GoBack prototype drifted");
static_assert(std::is_same<decltype(&Menu_PlaySoundId), OpenCMR2MenuPlaySoundIdFn>::value, "Menu_PlaySoundId prototype drifted");
static_assert(std::is_same<decltype(&Menu_SetInputStateFlag), OpenCMR2MenuSetInputStateFlagFn>::value, "Menu_SetInputStateFlag prototype drifted");
static_assert(std::is_same<decltype(&Menu_SetActionLatch), OpenCMR2MenuSetActionLatchFn>::value, "Menu_SetActionLatch prototype drifted");
#endif

#endif /* OPENCMR2_PORT_MENU_LAYOUT_H */
