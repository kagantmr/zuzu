#ifndef CTYPE_H
#define CTYPE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Checks if the given character is a digit (0-9).
 */
static inline int isdigit(int c) {
    return (c >= '0' && c <= '9');
}

/**
 * @brief Checks if the given character is a hexadecimal digit (0-9, a-f, A-F).
 */
static inline int isxdigit(int c) {
    return ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'));
}

/**
 * @brief Checks if the given character is an alphanumeric character (a-z, A-Z, 0-9).
 */
static inline int isalnum(int c) {
    return ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
}

/**
 * @brief Checks if the given character is a whitespace character (space, tab, newline, vertical tab, form feed, carriage return).
 */
static inline int isspace(int c) {
    return c == ' ' || (c >= '\t' && c <= '\r');
}

/**
 * @brief Checks if the given character is an uppercase letter (A-Z).
 */
static inline int isupper(int c) {
    return (c >= 'A' && c <= 'Z');
}

/**
 * @brief Checks if the given character is a lowercase letter (a-z).
 */
static inline int islower(int c) {
    return (c >= 'a' && c <= 'z');
}

/**
 * @brief Checks if the given character is a printable character (including space).
 */
static inline int isprint(int c) {
    return (c >= 0x20 && c <= 0x7E);
}

/**
 * @brief Checks if the given character is a graphical character (printable and not a space).
 */
static inline int isgraph(int c) {
    return (c > 0x20 && c <= 0x7E);
}

/**
 * @brief Checks if the given character is a blank character (space or horizontal tab).
 */
static inline int isblank(int c){
    return (c == ' ' || c == '\t');
}

/**
 * @brief Checks if the given character is a control character (non-printable).
 */
static inline int iscntrl(int c){
    return (c >= 0 && c < 0x20) || c == 0x7F;
}

/**
 * @brief Checks if the given character is a punctuation character (printable but not alphanumeric or space).
 */
static inline int ispunct(int c){
    return isprint(c) && !isalnum(c) && c != ' ';
}

/**
 * @brief Converts the given character to uppercase if it is a lowercase letter.
 */
static inline int toupper(int c) {
    return islower(c) ? c - 32 : c;
}

/**
 * @brief Converts the given character to lowercase if it is an uppercase letter.
 */
static inline int tolower(int c) {
    return isupper(c) ? c + 32 : c;
}

/**
 * @brief Checks if the given character is an alphabetic character (a-z, A-Z).
 */
static inline int isalpha(int c){
    return (islower(c) || isupper(c));
}

#ifdef __cplusplus
}
#endif

#endif // CTYPE_H