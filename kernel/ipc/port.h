/**
 * @file port.h
 * @brief Ports are the endpoints of message-passing. Messages are received on ports, and sent to
 * ports. Ports can keep callers on hold, and so on.
 */

#ifndef KERNEL_IPC_PORT_H
#define KERNEL_IPC_PORT_H

#include "event.h"
#include "observer.h"
#include <list.h>
#include <stdbool.h>
#include <stddef.h>
#include <types.h>

/**
 * @brief Forward decl. of TaskObject
 */
typedef struct TaskObjectStruct TaskObject;

/**
 * @brief Forward decl. of SpaceObject
 */
typedef struct SpaceObjectStruct SpaceObject;

/**
 * @brief How many callers can a port have on hold at once
 */
#define PORT_HELD_SLOTS 64

/**
 * @brief Represents a call put on hold.
 */
typedef struct {
    TaskObject *caller; /**< The caller */
    Tid caller_tid;     /**< TID of the caller. */
    uint16_t gen;       /**< The held call's generation, used to keep track */
} HeldCall;

/**
 * @brief Represents a port object.
 */
typedef struct {
    ListHead sender_queue;   /**< Blocked callers waiting to send a message. */
    ListHead receiver_queue; /**< Blocked receivers waiting for a message. */
    Spid owner_spid;         /**< Owner space ID  */
    size_t ref_count;        /**< How many references does this object have? */
    bool alive;              /**< Is the port alive? */
    ObserverSet observers;   /**< List of observers that could be signalled by this port. */
    ListHead active_servers; /**< Active server tasks of this port. */
    HeldCall *held;          /**< List of held callers. */
} PortObject;

/**
 * @brief Represents a reply object, a one-shot object that indicates a pending reply.
 */
typedef struct {
    TaskObject *caller_task; /**< The caller task. */
    Tid caller_tid;          /**< Task ID of the caller. */
} ReplyObject;

/**
 * @brief Create a new port.
 * 
 * @param[in] owner The owning space object.
 * 
 * @return PortObject*
 */
PortObject *PortCreate(SpaceObject *owner);

/**
 * @brief Drop one reference from a port, and if nothing is referring it, destroy it.
 * 
 * @param[in] port The port to destroy.
 */
void PortUnref(PortObject *port);

/**
 * @brief Wake the waiter queues of the port, and free the underlying object.
 * 
 * @param[in] port The port to kill.
 */
void PortKill(PortObject *port);

/**
 * @brief Checks if the port has pending calls.
 * 
 * @param[in] port The port to check.
 * 
 * @return bool
 * @retval true The port has pending calls.
 */
bool PortHasPending(const PortObject *port);

#endif /* KERNEL_IPC_PORT_H */
