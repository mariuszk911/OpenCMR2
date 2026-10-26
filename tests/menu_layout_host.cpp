/*
 * 32-bit host ABI drift guard for the fork-owned Menu layout override.
 *
 * This test includes the real game/Menu.h and checks it against the shared
 * expectations in port/menu_layout_abi.h. src/port/menu_layout.h asserts its
 * own copy against the same header, so an upstream change must be reflected in
 * both places instead of silently diverging.
 */
#include "port/types.h"
#include "port/menu_layout_abi.h"
#include "Menu.h"

#include <cstddef>
#include <type_traits>

static_assert(sizeof(MenuItem) == OPENCMR2_MENU_ITEM_SIZE, "MenuItem size drifted");
static_assert(sizeof(Menu) == OPENCMR2_MENU_SIZE, "Menu size drifted");

static_assert(offsetof(MenuItem, text) == OPENCMR2_MENU_ITEM_TEXT_OFFSET, "MenuItem::text drifted");
static_assert(offsetof(MenuItem, id) == OPENCMR2_MENU_ITEM_ID_OFFSET, "MenuItem::id drifted");
static_assert(offsetof(MenuItem, type) == OPENCMR2_MENU_ITEM_TYPE_OFFSET, "MenuItem::type drifted");
static_assert(offsetof(MenuItem, value) == OPENCMR2_MENU_ITEM_VALUE_OFFSET, "MenuItem::value drifted");
static_assert(offsetof(MenuItem, min) == OPENCMR2_MENU_ITEM_MIN_OFFSET, "MenuItem::min drifted");
static_assert(offsetof(MenuItem, max) == OPENCMR2_MENU_ITEM_MAX_OFFSET, "MenuItem::max drifted");
static_assert(offsetof(MenuItem, pSubMenu) == OPENCMR2_MENU_ITEM_SUBMENU_OFFSET, "MenuItem::pSubMenu drifted");
static_assert(offsetof(MenuItem, action) == OPENCMR2_MENU_ITEM_ACTION_OFFSET, "MenuItem::action drifted");

static_assert(offsetof(Menu, text) == OPENCMR2_MENU_TEXT_OFFSET, "Menu::text drifted");
static_assert(offsetof(Menu, field_0x4) == OPENCMR2_MENU_FIELD_0X4_OFFSET, "Menu::field_0x4 drifted");
static_assert(offsetof(Menu, itemCount) == OPENCMR2_MENU_ITEMCOUNT_OFFSET, "Menu::itemCount drifted");
static_assert(offsetof(Menu, cursor) == OPENCMR2_MENU_CURSOR_OFFSET, "Menu::cursor drifted");
static_assert(offsetof(Menu, pParent) == OPENCMR2_MENU_PARENT_OFFSET, "Menu::pParent drifted");
static_assert(offsetof(Menu, defaultCursor) == OPENCMR2_MENU_DEFAULTCURSOR_OFFSET, "Menu::defaultCursor drifted");
static_assert(offsetof(Menu, layout) == OPENCMR2_MENU_LAYOUT_OFFSET, "Menu::layout drifted");
static_assert(offsetof(Menu, moveFlags) == OPENCMR2_MENU_MOVEFLAGS_OFFSET, "Menu::moveFlags drifted");
static_assert(offsetof(Menu, value) == OPENCMR2_MENU_VALUE_OFFSET, "Menu::value drifted");
static_assert(offsetof(Menu, field_0x11) == OPENCMR2_MENU_FIELD_0X11_OFFSET, "Menu::field_0x11 drifted");
static_assert(offsetof(Menu, items) == OPENCMR2_MENU_ITEMS_OFFSET, "Menu::items drifted");
static_assert(offsetof(Menu, pfnCallback0) == OPENCMR2_MENU_CALLBACK0_OFFSET, "Menu::pfnCallback0 drifted");
static_assert(offsetof(Menu, pfnCallback1) == OPENCMR2_MENU_CALLBACK1_OFFSET, "Menu::pfnCallback1 drifted");
static_assert(offsetof(Menu, pfnCallback2) == OPENCMR2_MENU_CALLBACK2_OFFSET, "Menu::pfnCallback2 drifted");
static_assert(offsetof(Menu, pfnCallback3) == OPENCMR2_MENU_CALLBACK3_OFFSET, "Menu::pfnCallback3 drifted");
static_assert(offsetof(Menu, pItemCallbacks) == OPENCMR2_MENU_ITEMCALLBACKS_OFFSET, "Menu::pItemCallbacks drifted");

static_assert(std::is_same<decltype(&Menu_PlaySound), OpenCMR2MenuPlaySoundFn>::value, "Menu_PlaySound drifted");
static_assert(std::is_same<decltype(&Menu_Init), OpenCMR2MenuInitFn>::value, "Menu_Init drifted");
static_assert(std::is_same<decltype(&Menu_ClearNextItem), OpenCMR2MenuClearNextItemFn>::value, "Menu_ClearNextItem drifted");
static_assert(std::is_same<decltype(&Menu_AddItemType3), OpenCMR2MenuAddItemType3Fn>::value, "Menu_AddItemType3 drifted");
static_assert(std::is_same<decltype(&Menu_AddItemType6), OpenCMR2MenuAddItemType6Fn>::value, "Menu_AddItemType6 drifted");
static_assert(std::is_same<decltype(&Menu_AddItemType4), OpenCMR2MenuAddItemType4Fn>::value, "Menu_AddItemType4 drifted");
static_assert(std::is_same<decltype(&Menu_AddItemType1), OpenCMR2MenuAddItemType1Fn>::value, "Menu_AddItemType1 drifted");
static_assert(std::is_same<decltype(&Menu_AddItemType2), OpenCMR2MenuAddItemType2Fn>::value, "Menu_AddItemType2 drifted");
static_assert(std::is_same<decltype(&Menu_SetCursor), OpenCMR2MenuSetCursorFn>::value, "Menu_SetCursor drifted");
static_assert(std::is_same<decltype(&Menu_SetParent), OpenCMR2MenuSetParentFn>::value, "Menu_SetParent drifted");
static_assert(std::is_same<decltype(&Menu_FindItem), OpenCMR2MenuFindItemFn>::value, "Menu_FindItem drifted");
static_assert(std::is_same<decltype(&Menu_GetItem), OpenCMR2MenuGetItemFn>::value, "Menu_GetItem drifted");
static_assert(std::is_same<decltype(&Menu_SetItemSubMenu), OpenCMR2MenuSetItemSubMenuFn>::value, "Menu_SetItemSubMenu drifted");
static_assert(std::is_same<decltype(&Menu_SelectItem), OpenCMR2MenuSelectItemFn>::value, "Menu_SelectItem drifted");
static_assert(std::is_same<decltype(&Menu_SetCallbacks), OpenCMR2MenuSetCallbacksFn>::value, "Menu_SetCallbacks drifted");
static_assert(std::is_same<decltype(&Menu_SetFlags), OpenCMR2MenuSetFlagsFn>::value, "Menu_SetFlags drifted");
static_assert(std::is_same<decltype(&Menu_ValidateCursor), OpenCMR2MenuValidateCursorFn>::value, "Menu_ValidateCursor drifted");
static_assert(std::is_same<decltype(&Menu_CallCallback0), OpenCMR2MenuCallCallback0Fn>::value, "Menu_CallCallback0 drifted");
static_assert(std::is_same<decltype(&Menu_CallCallback3), OpenCMR2MenuCallCallback3Fn>::value, "Menu_CallCallback3 drifted");
static_assert(std::is_same<decltype(&Menu_CallCallback2), OpenCMR2MenuCallCallback2Fn>::value, "Menu_CallCallback2 drifted");
static_assert(std::is_same<decltype(&Menu_Update), OpenCMR2MenuUpdateFn>::value, "Menu_Update drifted");
static_assert(std::is_same<decltype(&Menu_SetNextAction), OpenCMR2MenuSetNextActionFn>::value, "Menu_SetNextAction drifted");
static_assert(std::is_same<decltype(&Menu_GoBack), OpenCMR2MenuGoBackFn>::value, "Menu_GoBack drifted");
static_assert(std::is_same<decltype(&Menu_PlaySoundId), OpenCMR2MenuPlaySoundIdFn>::value, "Menu_PlaySoundId drifted");
static_assert(std::is_same<decltype(&Menu_SetInputStateFlag), OpenCMR2MenuSetInputStateFlagFn>::value, "Menu_SetInputStateFlag drifted");
static_assert(std::is_same<decltype(&Menu_SetActionLatch), OpenCMR2MenuSetActionLatchFn>::value, "Menu_SetActionLatch drifted");

int main(void)
{
    return 0;
}
