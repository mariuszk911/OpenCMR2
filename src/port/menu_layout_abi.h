#ifndef OPENCMR2_PORT_MENU_LAYOUT_ABI_H
#define OPENCMR2_PORT_MENU_LAYOUT_ABI_H

#include "port/types.h"

/*
 * Expected 32-bit Menu/MenuItem ABI. Both the fork-owned layout override and
 * the 32-bit host drift test assert against these values, so an upstream
 * game/Menu.h change fails one of the two instead of silently diverging.
 */
#define OPENCMR2_MENU_ITEM_SIZE 0x14
#define OPENCMR2_MENU_SIZE 0x1e0

#define OPENCMR2_MENU_ITEM_TEXT_OFFSET 0x0
#define OPENCMR2_MENU_ITEM_ID_OFFSET 0x4
#define OPENCMR2_MENU_ITEM_TYPE_OFFSET 0x7
#define OPENCMR2_MENU_ITEM_VALUE_OFFSET 0x8
#define OPENCMR2_MENU_ITEM_MIN_OFFSET 0xa
#define OPENCMR2_MENU_ITEM_MAX_OFFSET 0xb
#define OPENCMR2_MENU_ITEM_SUBMENU_OFFSET 0xc
#define OPENCMR2_MENU_ITEM_ACTION_OFFSET 0x10

#define OPENCMR2_MENU_TEXT_OFFSET 0x0
#define OPENCMR2_MENU_FIELD_0X4_OFFSET 0x4
#define OPENCMR2_MENU_ITEMCOUNT_OFFSET 0x6
#define OPENCMR2_MENU_CURSOR_OFFSET 0x7
#define OPENCMR2_MENU_PARENT_OFFSET 0x8
#define OPENCMR2_MENU_DEFAULTCURSOR_OFFSET 0xc
#define OPENCMR2_MENU_LAYOUT_OFFSET 0xd
#define OPENCMR2_MENU_MOVEFLAGS_OFFSET 0xf
#define OPENCMR2_MENU_VALUE_OFFSET 0x10
#define OPENCMR2_MENU_FIELD_0X11_OFFSET 0x11
#define OPENCMR2_MENU_ITEMS_OFFSET 0x14
#define OPENCMR2_MENU_CALLBACK0_OFFSET 0x1cc
#define OPENCMR2_MENU_CALLBACK1_OFFSET 0x1d0
#define OPENCMR2_MENU_CALLBACK2_OFFSET 0x1d4
#define OPENCMR2_MENU_CALLBACK3_OFFSET 0x1d8
#define OPENCMR2_MENU_ITEMCALLBACKS_OFFSET 0x1dc

#if defined(__cplusplus)
struct Menu;
struct MenuItem;
struct MenuItemCallbacks;

using OpenCMR2MenuCallback = void (*)(Menu *, int);
using OpenCMR2MenuItemAction = void (*)(Menu *, MenuItem *);

using OpenCMR2MenuPlaySoundFn = void (*)(int);
using OpenCMR2MenuInitFn = void (*)(Menu *, char *, short, int, Menu *, MenuItemCallbacks *, BYTE, BYTE, BYTE);
using OpenCMR2MenuClearNextItemFn = void (*)(Menu *);
using OpenCMR2MenuAddItemType3Fn = void (*)(Menu *, char *, short, BYTE, BYTE, BYTE, int, OpenCMR2MenuItemAction, short);
using OpenCMR2MenuAddItemType6Fn = void (*)(Menu *, char *, short, BYTE, BYTE, BYTE, int, OpenCMR2MenuItemAction, short);
using OpenCMR2MenuAddItemType4Fn = void (*)(Menu *, char *, short, OpenCMR2MenuItemAction, short);
using OpenCMR2MenuAddItemType1Fn = void (*)(Menu *, char *, short, OpenCMR2MenuItemAction, short);
using OpenCMR2MenuAddItemType2Fn = void (*)(Menu *, char *, short, Menu *, OpenCMR2MenuItemAction, short);
using OpenCMR2MenuSetCursorFn = void (*)(Menu *, BYTE);
using OpenCMR2MenuSetParentFn = void (*)(Menu *, Menu *);
using OpenCMR2MenuFindItemFn = int (*)(Menu *, int);
using OpenCMR2MenuGetItemFn = MenuItem *(*)(Menu *, int);
using OpenCMR2MenuSetItemSubMenuFn = void (*)(Menu *, int, Menu *);
using OpenCMR2MenuSelectItemFn = void (*)(Menu *, int);
using OpenCMR2MenuSetCallbacksFn = void (*)(Menu *, OpenCMR2MenuCallback, OpenCMR2MenuCallback, OpenCMR2MenuCallback, OpenCMR2MenuCallback);
using OpenCMR2MenuSetFlagsFn = void (*)(Menu *, BYTE, BYTE, BYTE, BYTE);
using OpenCMR2MenuValidateCursorFn = void (*)(Menu *, int);
using OpenCMR2MenuCallCallback0Fn = void (*)(Menu *);
using OpenCMR2MenuCallCallback3Fn = void (*)(Menu *);
using OpenCMR2MenuCallCallback2Fn = void (*)(Menu *);
using OpenCMR2MenuUpdateFn = Menu *(*)(Menu *, unsigned int);
using OpenCMR2MenuSetNextActionFn = void (*)(Menu *);
using OpenCMR2MenuGoBackFn = void (*)(Menu *);
using OpenCMR2MenuPlaySoundIdFn = void (*)(int);
using OpenCMR2MenuSetInputStateFlagFn = void (*)(char);
using OpenCMR2MenuSetActionLatchFn = void (*)(char);
#endif

#endif /* OPENCMR2_PORT_MENU_LAYOUT_ABI_H */
