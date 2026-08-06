/* speedtest_ipc_child - passive echo server for speedtest's cross-process
 * IPC RTT benchmark.
 *
 * Lives on the SD card (test_apps, like speedtest itself) and is spawned
 * on demand by speedtest via sysd's SYSD_EXEC -- the same
 * pspawn -> grant -> SYSD_EXEC -> kickstart path zztest uses to spawn
 * zztest_child. speedtest creates the echo port itself and grants it in
 * pre-kickstart, passing the child-side slot as argv[1], so there is no
 * rendezvous step (no nametable lookup, no polling): by the time this
 * process's first ZuzuMsgRecv runs, the port handle is already sitting in
 * its handle table.
 *
 * Protocol: echoes every ZuzuMsgCall with (1, 0, 0) -- the same reply
 * shape as speedtest's own in-process echo_server_thread (see
 * user/test_apps/speedtest/main.c) so the two benchmarks measure the same
 * protocol cost, differing only in whether sender and receiver share an
 * address space. Exits on the same MSG_QUIT sentinel (w2 of the received
 * message) that echo_server_thread honors, letting speedtest reap this
 * process with ZuzuWait instead of leaving it running.
 */
#include <zuzu/zuzu.h>
#include <stdlib.h>

/* Must match speedtest's MSG_QUIT (user/test_apps/speedtest/globals.h) --
 * there's no shared header since this sentinel is the only thing the two
 * sides need to agree on. */
#define MSG_QUIT 0xFFFFFFFFu

int main(int argc, char **argv)
{
    if (argc < 2)
        return 1;

    Handle port = (Handle)strtol(argv[1], NULL, 10);

    for (;;) {
        Message cmd = ZuzuMsgRecv(port, TIMEOUT_INFINITE);

        if (cmd.w2 == MSG_QUIT) {
            ZuzuMsgReply(cmd.w0, 1, 0, 0);
            return 0;
        }

        ZuzuMsgReply(cmd.w0, 1, 0, 0);
    }
}
