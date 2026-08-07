#include "zuzu/err.h"
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <zuzu/lmsg.h>
#include <zuzu/msg.h>
#include <zuzu/syspage.h>
#include <zuzu/task.h>
#include <zuzu/tls.h>
#include <zuzu/types.h>
#include <zuzu/umem.h>

#define STACK_SIZE 4096
#define REQ "hello from worker thread"
#define RESP "reply from main thread"

static int fails = 0;
#define CHECK(c, m)                                                                                \
	do {                                                                                       \
		if (!(c)) {                                                                        \
			printf("FAIL: %s\n", m);                                                   \
			fails++;                                                                   \
		} else {                                                                           \
			printf("ok:   %s\n", m);                                                   \
		}                                                                                  \
	} while (0)

static void worker(void *arg)
{

	(void)arg;
	printf("Hey, I'm thread %d!\n", ZuzuTLS()->tid);
	ZuzuSleep(1000);
	ZuzuTQuit(0);
}

int main(void)
{

	Tid tids[300];
	void *stacks[300];

	for (int i = 0; i < 300; i++) {
		stacks[i] = ZuzuMemMap(HANDLE_ANON, STACK_SIZE, PROT_READ | PROT_WRITE, 0);
		// CHECK(!ZuzuPtrIsErr(stacks[i]), "worker stack");
		Tid tid = ZuzuTMake(worker, (char *)stacks[i] + STACK_SIZE, NULL);
		// CHECK(tid != 0, "thread spawn"); // or whatever the error sentinel is
		tids[i] = tid;
	}

	for (int i = 0; i < 300; i++) {
		ZuzuTJoin(tids[i]);
	}

	return ZUZU_OK;
}
