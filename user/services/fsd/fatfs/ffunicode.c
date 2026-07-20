#include "ff.h"

WCHAR ff_oem2uni(WCHAR oem, WORD cp)
{
    (void)cp;
    return oem;
}

WCHAR ff_uni2oem(DWORD uni, WORD cp)
{
    (void)cp;
    if (uni <= 0xFF)
        return (WCHAR)uni;
    return (WCHAR)'?';
}

DWORD ff_wtoupper(DWORD uni)
{
    if (uni >= 'a' && uni <= 'z')
        return uni - ('a' - 'A');
    return uni;
}
