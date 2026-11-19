/**
 * @file lbx.h
 * 
 * @brief The litterbox task `lbx` is a kernel task in charge of cleaning up large objects.
 */

#ifndef KERNEL_LBX_LBX_H
#define KERNEL_LBX_LBX_H

/**
 * @brief Unblocks lbx for cleanup.
 */
void LbxKick(void);

/**
 * @brief Creates the litterbox task.
 */
void LbxInit(void);

#endif /* KERNEL_LBX_LBX_H */