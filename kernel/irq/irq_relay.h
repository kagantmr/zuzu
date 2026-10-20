/**
 * @file irq_relay.h
 * @brief Relaying hardware interrupts to user-space drivers as event bits.
 */

#ifndef KERNEL_IRQ_IRQ_RELAY_H
#define KERNEL_IRQ_IRQ_RELAY_H

#include "kernel/ipc/event.h"
#include "stdbool.h"
#include <arch/regs.h>

/** @brief Forward declaration of a space. */
typedef struct SpaceObjectStruct SpaceObject;

/**
 * @brief Who owns an interrupt line and where its interrupts are delivered.
 */
typedef struct {
    SpaceObject *owner; /**< The space that owns the line, or NULL if it is free. */
    bool pending; /**< An interrupt fired while no live event was bound; delivered on the next bind. */
    EventObject *bound_ev; /**< The event the line signals, or NULL. The relay holds a reference. */
    uint32_t bit; /**< The bit of @c bound_ev raised by each interrupt. */
} IrqOwner;

/**
 * @brief Check that a line exists and is not reserved for the kernel itself.
 *
 * @param[in] irq_num The interrupt line.
 * @return true if a driver may bind it.
 */
bool IrqIsValid(Irq irq_num);

/**
 * @brief Release every line a space owns, masking each one.
 *
 * @param[in] owner The space whose lines to release.
 */
void IrqReleaseAll(SpaceObject *owner);

/**
 * @brief Release one line if @p owner owns it, masking it and dropping its event binding.
 *
 * Does nothing if the line is out of range or owned by someone else.
 *
 * @param[in] owner The space that must own the line.
 * @param[in] irq_num The interrupt line.
 */
void IrqRelease(SpaceObject *owner, Irq irq_num);

/**
 * @brief Claim a line for a space and make its interrupts raise a bit on an event.
 *
 * Binding an already owned line to a new event replaces the old binding. The line is unmasked.
 * An interrupt that was pending is delivered at once. After each interrupt the line stays
 * masked until IrqRelayRearm() is called.
 *
 * @param[in] owner The space claiming the line.
 * @param[in] irq_num The interrupt line.
 * @param[in] ev The event to signal.
 * @param[in] bit The bit to raise on @p ev.
 * @return ZUZU_OK, ERR_BUSY if another space owns the line, or ERR_DEAD if @p ev is dead.
 */
Err IrqBindToEvent(SpaceObject *owner, Irq irq_num, EventObject *ev, uint32_t bit);

/**
 * @brief Unmask a line again after its interrupt has been handled.
 *
 * @param[in] owner The space that owns the line.
 * @param[in] irq_num The interrupt line.
 * @return ZUZU_OK, ERR_BADARG if the line is out of range, or ERR_NOPERM if @p owner does not
 * own it.
 */
Err IrqRelayRearm(SpaceObject *owner, Irq irq_num);

/**
 * @brief Get the table of line owners.
 *
 * @return An array of MAX_IRQS entries, indexed by interrupt line.
 */
const IrqOwner *GetIrqOwnersList(void);

#endif /* KERNEL_IRQ_IRQ_RELAY_H */
