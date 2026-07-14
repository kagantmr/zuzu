#ifndef ZUZU_FMT_H
#define ZUZU_FMT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct {
    uint8_t *buf; // pointer to the buffer where formatted data will be written
    uint32_t size; // total size of the buffer in bytes
    uint32_t pos; // current position in the buffer (number of bytes written so far)
    int      overflow; // flag indicating whether an overflow has occurred (1 if overflow, 0 otherwise)
} fmt_writer_t;

typedef struct {    
    const uint8_t *buf; // pointer to the buffer from which formatted data will be read
    uint32_t       size; // total size of the buffer in bytes
    uint32_t       pos; // current position in the buffer (number of bytes read so far)
} fmt_reader_t;

/**
 * @brief Initializes a fmt_writer_t structure for writing formatted data to a buffer.
 * 
 * @param w Pointer to the fmt_writer_t structure.
 * @param buf Pointer to the null-terminated string to write.
 * @param size The size of the string to write, including the null terminator.
 */
void fmt_writer_init(fmt_writer_t *w, void *buf, uint32_t size);

/**
 * @brief Puts a single byte (uint8_t) into the fmt_writer_t buffer.
 * 
 * @param w Pointer to the fmt_writer_t structure.
 * @param v The value to write into the buffer.
 *
 */
void fmt_put_u8(fmt_writer_t *w, uint8_t v);

/**
 * @brief Puts a single word (uint32_t) into the fmt_writer_t buffer.
 * 
 * @param w Pointer to the fmt_writer_t structure.
 * @param v The value to write into the buffer.
 *
 */
void fmt_put_u32(fmt_writer_t *w, uint32_t v);

/**
 * @brief Puts a string into the fmt_writer_t buffer.
 * 
 * @param w Pointer to the fmt_writer_t structure.
 * @param s The value to write into the buffer.
 *
 */
void fmt_put_str(fmt_writer_t *w, const char *s);  

/**
 * @brief Puts a sequence of bytes into the fmt_writer_t buffer.
 * 
 * @param w Pointer to the fmt_writer_t structure.
 * @param p Pointer to the buffer containing the bytes to write.
 * @param len The number of bytes to write into the buffer.
 */
void fmt_put_bytes(fmt_writer_t *w, const void *p, uint32_t len);

/**
 * @brief Finalizes the fmt_writer_t buffer and returns the number of bytes written.
 * 
 * @param w Pointer to the fmt_writer_t structure.
 * 
 * @return uint32_t Returns the number of bytes written to the buffer, or 0 if an overflow occurred.
 */
uint32_t fmt_finish(fmt_writer_t *w); 

/**
 * @brief Initializes a fmt_reader_t structure for reading formatted data from a buffer.
 * 
 * @param r Pointer to the fmt_reader_t structure.
 * @param buf Pointer to the buffer from which to read data.
 * @param size The size of the buffer in bytes.
 */
void     fmt_reader_init(fmt_reader_t *r, const void *buf, uint32_t size);

/**
 * @brief Reads a single byte (uint8_t) from the fmt_reader_t buffer.
 * 
 * @param r Pointer to the fmt_reader_t structure.
 * @return uint8_t Returns the byte read from the buffer.
 */
uint8_t  fmt_get_u8(fmt_reader_t *r);

/**
 * @brief Reads a single word (uint32_t) from the fmt_reader_t buffer.
 * 
 * @param r Pointer to the fmt_reader_t structure.
 * @return uint32_t Returns the word read from the buffer.
 */
uint32_t fmt_get_u32(fmt_reader_t *r);

/**
 * @brief Reads a string from the fmt_reader_t buffer into the provided output buffer.
 * 
 * @param r Pointer to the fmt_reader_t structure.
 * @param out Pointer to the output buffer where the string will be stored.
 * @param max The maximum number of bytes to read into the output buffer, including the null terminator.
 * @return char* Returns a pointer to the output buffer containing the read string, or NULL
 */
char    *fmt_get_str(fmt_reader_t *r, char *out, uint32_t max);

/**
 * @brief Reads a sequence of bytes from the fmt_reader_t buffer into the provided output buffer.
 * 
 * @param r Pointer to the fmt_reader_t structure.
 * @param out Pointer to the output buffer where the bytes will be stored.
 * @param max The maximum number of bytes to read into the output buffer.
 * @return uint32_t Returns the number of bytes read into the output buffer, or 0 if an overflow occurred or if the requested length exceeds the remaining bytes in the buffer.
 */
uint32_t fmt_get_bytes(fmt_reader_t *r, void *out, uint32_t max);

/**
 * @brief Checks if the fmt_reader_t buffer has been read completely without overflow.
 * 
 * @param r Pointer to the fmt_reader_t structure.
 * @return int Returns 1 if the buffer has been read completely without overflow, 0 otherwise.
 */
int      fmt_ok(const fmt_reader_t *r); 

#ifdef __cplusplus
}
#endif

#endif // ZUZU_FMT_H