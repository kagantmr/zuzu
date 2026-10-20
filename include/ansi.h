/**
 * @file
 * @brief ANSI terminal escape sequences.
 */

#ifndef ANSI_H
#define ANSI_H

#ifdef __cplusplus
extern "C" {
#endif

#define ANSI_RESET "\033[0m" /**< Reset all attributes. */
#define ANSI_BOLD "\033[1m" /**< Bold. */
#define ANSI_DIM "\033[2m" /**< Dim. */

#define ANSI_BLACK "\033[30m" /**< Black text. */
#define ANSI_RED "\033[31m" /**< Red text. */
#define ANSI_GREEN "\033[32m" /**< Green text. */
#define ANSI_YELLOW "\033[33m" /**< Yellow text. */
#define ANSI_BLUE "\033[34m" /**< Blue text. */
#define ANSI_MAGENTA "\033[35m" /**< Magenta text. */
#define ANSI_CYAN "\033[36m" /**< Cyan text. */
#define ANSI_WHITE "\033[37m" /**< White text. */
#define ANSI_GRAY "\033[90m" /**< Gray text. */

#define ANSI_BG_BLACK "\033[40m" /**< Black background. */
#define ANSI_BG_RED "\033[41m" /**< Red background. */
#define ANSI_BG_GREEN "\033[42m" /**< Green background. */
#define ANSI_BG_YELLOW "\033[43m" /**< Yellow background. */
#define ANSI_BG_BLUE "\033[44m" /**< Blue background. */
#define ANSI_BG_MAGENTA "\033[45m" /**< Magenta background. */
#define ANSI_BG_CYAN "\033[46m" /**< Cyan background. */
#define ANSI_BG_WHITE "\033[47m" /**< White background. */

#define ANSI_CLEAR "\033[2J\033[H" /**< Clear the screen and move the cursor home. */

#ifdef __cplusplus
}
#endif

#endif