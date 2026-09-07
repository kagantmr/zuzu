/* cv.h - zuzuOS notification-based condvar library. */

#ifndef ZUZU_SYNC_CV
#define ZUZU_SYNC_CV

#include "zuzu/err.h"
#include <zuzu/types.h>

typedef struct cv CondVariable;

Err CondVarInit(CondVariable* cv);

Err CondVarDestroy(CondVariable *cv);

Err CondVarPost(CondVariable *cv);

Err CondVarWait(CondVariable *cv);

#endif /* ZUZU_SYNC_CV */