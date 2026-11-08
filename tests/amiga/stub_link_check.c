/*
 * Link-time check for the Amiga stubs.
 *
 * Supplies a main, so the target linker must resolve the stubs' runtime
 * dependencies without the game. It is not staged.
 */

int main(void)
{
    return 0;
}
