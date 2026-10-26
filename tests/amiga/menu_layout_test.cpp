/*
 * MENU/MENUITEM bitfield order check for 68k.
 *
 * The original MSVC layout puts flag0..flag7 in bits 0..7 of the byte after
 * Menu::layout, and MENU_FLAGS reads that byte directly. GCC on 68k allocates
 * bitfields from the MSB, which silently disables every menu direction while
 * the select/back tests land on flag5/flag4. This test compiles the same
 * port/menu_layout.h override as the game (reversed declarations, which make
 * GCC's MSB-first allocation produce the original LSB-first byte) and checks
 * the raw bytes.
 */
#include "port/types.h"
#include "port/menu_layout.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void Check(int ok, const char *what)
{
    if (!ok) {
        printf("menu_layout: FAIL %s\n", what);
        fails++;
    }
}

int main(void)
{
    Menu menu;
    MenuItem item;
    BYTE flags;
    BYTE itemFlags;

    memset(&menu, 0, sizeof(menu));
    memset(&item, 0, sizeof(item));

    menu.flag0 = 1;
    menu.flag1 = 1;
    menu.flag2 = 1;
    menu.flag3 = 1;
    menu.flag4 = 1;
    menu.flag5 = 1;
    flags = *((BYTE *)&menu.layout + 1);
    Check((flags & 0x01) != 0, "flag0 -> bit0 (prev/next)");
    Check((flags & 0x02) != 0, "flag1 -> bit1 (left/right)");
    Check((flags & 0x04) != 0, "flag2 -> bit2 (select)");
    Check((flags & 0x08) != 0, "flag3 -> bit3 (back)");
    Check((flags & 0x10) != 0, "flag4 -> bit4 (wrap)");
    Check((flags & 0x20) != 0, "flag5 -> bit5");
    Check((flags & 0xC0) == 0, "flag6/flag7 stay clear");

    item.enabled = 1;
    item.visible = 1;
    item.flag2 = 1;
    item.flag3 = 1;
    item.flag4 = 3;
    itemFlags = *((BYTE *)&item + 6);
    Check(itemFlags == 0x3F, "MenuItem flags: enabled/visible/flag2/flag3/flag4 LSB");

    if (fails == 0)
        printf("menu_layout: OK (menu flags=%02x item flags=%02x)\n",
               (unsigned int)flags, (unsigned int)itemFlags);
    return fails != 0;
}
