#include "genet.h"

#define PHY_ADDR 1
#define MDIO_TIMEOUT 100000

Err MdioRead(uint32_t reg, uint16_t *out)
{
    GenetMdioCmd c = {.raw = 0};
    c.op = 2;
    c.phy = PHY_ADDR;
    c.reg = reg;
    c.busy = 1;
    g_genet.umac->mdio_cmd = c.raw;

    uint32_t timeout = MDIO_TIMEOUT;
    do {
        c.raw = g_genet.umac->mdio_cmd;
    } while (c.busy && --timeout);
    if (!timeout)
        return ERR_TIMEOUT;
    if (c.fail)
        return ERR_IO;
    *out = c.data;
    return ZUZU_OK;
}

Err MdioReadLatched(uint32_t reg, uint16_t *out)
{
    Err rc = MdioRead(reg, out);
    if (rc != ZUZU_OK)
        return rc;
    rc = MdioRead(reg, out);
    return rc;
}

Err MdioWrite(uint32_t reg, uint16_t value)
{
    GenetMdioCmd c = {.raw = 0};
    c.op = 1;
    c.phy = PHY_ADDR;
    c.reg = reg;
    c.data = value;
    c.busy = 1;
    g_genet.umac->mdio_cmd = c.raw;

    uint32_t timeout = MDIO_TIMEOUT;
    do {
        c.raw = g_genet.umac->mdio_cmd;
    } while (c.busy && --timeout);
    if (!timeout)
        return ERR_TIMEOUT;
    return ZUZU_OK;
}
