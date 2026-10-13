#include <string.h>
#include <util/imgcache.h>
#include <zuzu/zuzu.h>

static void Init(ImgCache *c)
{
    if (c->ready)
        return;
    memset(c, 0, sizeof(*c));
    for (int i = 0; i < IMGCACHE_SLOTS; i++)
        c->slots[i].obj = -1;
    c->ready = true;
}

static ImgCacheEntry *Find(ImgCache *c, const char *path)
{
    for (int i = 0; i < IMGCACHE_SLOTS; i++) {
        if (c->slots[i].obj >= 0 && strcmp(c->slots[i].path, path) == 0)
            return &c->slots[i];
    }
    return NULL;
}

static void Evict(ImgCacheEntry *e)
{
    HandleClose(e->obj);
    e->obj = -1;
}

Handle ImgCacheGet(ImgCache *c, const char *path, uint32_t size, uint32_t mtime)
{
    Init(c);
    ImgCacheEntry *e = Find(c, path);
    if (!e)
        return -1;
    if (e->size != size || e->mtime != mtime) {
        Evict(e);
        return -1;
    }
    e->last_used = ++c->clock;
    return e->obj;
}

bool ImgCachePut(ImgCache *c, const char *path, uint32_t size, uint32_t mtime, Handle obj)
{
    Init(c);
    if (strlen(path) >= IMGCACHE_PATH_MAX)
        return false;

    ImgCacheEntry *e = Find(c, path);
    if (e)
        Evict(e);
    else {
        for (int i = 0; i < IMGCACHE_SLOTS && !e; i++) {
            if (c->slots[i].obj < 0)
                e = &c->slots[i];
        }
    }
    if (!e) {
        e = &c->slots[0];
        for (int i = 1; i < IMGCACHE_SLOTS; i++) {
            if (c->slots[i].last_used < e->last_used)
                e = &c->slots[i];
        }
        Evict(e);
    }

    strcpy(e->path, path);
    e->size = size;
    e->mtime = mtime;
    e->last_used = ++c->clock;
    e->obj = obj;
    return true;
}

void ImgCacheDrop(ImgCache *c, const char *path)
{
    Init(c);
    ImgCacheEntry *e = Find(c, path);
    if (e)
        Evict(e);
}
