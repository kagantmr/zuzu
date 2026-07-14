#ifndef DEN_H
#define DEN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <zuzu/types.h>

/**
 * @brief Creates a new den with the specified name and capability.
 * 
 * @param name The name of the den to create.
 * @param cap The capability associated with the den.
 * 
 * @return den_id_t Returns the ID of the newly created den, or a negative value on error.
 */
den_id_t den_create(const char *name, uint32_t cap);

/**
 * @brief Destroys the den with the specified ID.
 * 
 * @param id The ID of the den to destroy.
 * 
 * @return int Returns 0 on success, or a negative value on error.
 */
int den_destroy(den_id_t id);

/**
 * @brief Retrieves the ID of the den with the specified name.
 * 
 * @param name The name of the den to look up.
 * 
 * @return den_id_t Returns the ID of the den if found, or a negative value if not found.
 */
den_id_t den_myden(const char *name);

#ifdef __cplusplus
}
#endif

#endif // DEN_H