#include "waitslot.h"

#include "core/panic.h"
#include <string.h>

#define LOG_FMT(fmt) "(waitslot) " fmt
#include <zuzu/log.h>

#ifdef DEBUG
/**
 * @brief Assert bookkeeping matches actual list linkage.
 * @param t     Thread to check.
 * @param where Caller name, for the panic message.
 */
static void CheckInvariant(const Thread *t, const char *where)
{
	for (uint32_t i = 0; i < WAITANY_MAX_HANDLES; i++) {
		const ListNode *n = &t->waitany_slots[i].node;
		bool should_be_linked = i < t->waitany_slot_count;
		bool is_linked = (n->prev != NULL) && (n->next != NULL);
		bool half_linked = (n->prev != NULL) != (n->next != NULL);

		if (half_linked) {
			panic("waitslot: tid=%u slot %u half-linked after %s "
			      "(prev=%p next=%p kind=%d)",
			      (unsigned)t->tid, i, where, (void *)n->prev, (void *)n->next,
			      (int)t->waitany_slots[i].kind);
		}
		if (should_be_linked != is_linked) {
			panic("waitslot: tid=%u slot %u %s after %s (slot_count=%u kind=%d)",
			      (unsigned)t->tid, i, is_linked ? "unexpectedly linked" : "not linked",
			      where, (unsigned)t->waitany_slot_count, (int)t->waitany_slots[i].kind);
		}
	}
}
#endif

void WaitSlotsUnregisterAll(Thread *t)
{
	if (!t || !t->waitany_registered)
		return;

	for (uint32_t i = 0; i < t->waitany_slot_count && i < WAITANY_MAX_HANDLES; i++) {
		ListNode *n = &t->waitany_slots[i].node;
		if (n->prev && n->next)
			list_remove(n);
		n->prev = NULL;
		n->next = NULL;
		t->waitany_slots[i].kind = WAIT_KIND_NONE;
	}

	t->waitany_slot_count = 0;
	t->waitany_registered = false;
	t->waitany_match_index = WAITANY_NO_MATCH;

#ifdef DEBUG
	CheckInvariant(t, "WaitSlotsUnregisterAll");
#endif
}

void WaitSlotsRegister(Thread *self, const WaitSlot *slots, uint32_t count)
{
	if (!self)
		return;

	/* Idempotent: never leave a mix of old and new slots linked. */
	WaitSlotsUnregisterAll(self);

	if (count > WAITANY_MAX_HANDLES)
		count = WAITANY_MAX_HANDLES;

	for (uint32_t i = 0; i < count; i++) {
		self->waitany_slots[i] = slots[i];
		self->waitany_slots[i].owner = self;
		self->waitany_slots[i].node.prev = NULL;
		self->waitany_slots[i].node.next = NULL;

		ListHead *queue = (slots[i].kind == WAIT_KIND_NTFN)
				       ? &slots[i].ntfn->wait_queue
				       : &slots[i].port->receiver_queue;
		list_add_tail(&self->waitany_slots[i].node, &queue->node);
	}

	self->waitany_slot_count = count;
	self->waitany_registered = (count > 0);
	self->waitany_match_index = WAITANY_NO_MATCH;

#ifdef DEBUG
	CheckInvariant(self, "WaitSlotsRegister");
#endif
}

void WaitSlotsDeliver(Thread *t, uint32_t handle_index, const WaitanyResult *result)
{
	if (!t || !result)
		return;
	t->waitany_match_index = handle_index;
	t->waitany_pending_result = *result;
}

void WaitanyDeliverNtfn(uint32_t matched_index, uint32_t bits, WaitanyResult *result)
{
	memset(result, 0, sizeof(*result));
	result->size = sizeof(*result);
	result->matched_index = (Handle)matched_index;
	result->kind = WAITANY_KIND_NTFN;
	result->source = 0;
	result->w1 = bits;
}
