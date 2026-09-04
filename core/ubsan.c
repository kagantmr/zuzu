/*
 * core/ubsan.c - minimal freestanding UBSan runtime.
 */

#include <stdint.h>

#define LOG_FMT(fmt) "(ubsan) " fmt
#include "core/log.h"
#include "core/panic.h"

struct source_location {
    const char *filename;
    uint32_t    line;
    uint32_t    column;
};

struct type_descriptor {
    uint16_t type_kind;
    uint16_t type_info;
    char     type_name[1];      /* NUL-terminated, flexible in practice */
};

typedef unsigned long value_handle;

/* ---- per-check data blobs (libsanitizer/ubsan/ubsan_handlers.h) --------- */

struct type_mismatch_data_v1 {
    struct source_location loc;
    const struct type_descriptor *type;
    uint8_t log_alignment;
    uint8_t type_check_kind;
};

struct overflow_data {
    struct source_location loc;
    const struct type_descriptor *type;
};

struct shift_out_of_bounds_data {
    struct source_location loc;
    const struct type_descriptor *lhs_type;
    const struct type_descriptor *rhs_type;
};

struct out_of_bounds_data {
    struct source_location loc;
    const struct type_descriptor *array_type;
    const struct type_descriptor *index_type;
};

struct invalid_value_data {
    struct source_location loc;
    const struct type_descriptor *type;
};

struct pointer_overflow_data {
    struct source_location loc;
};

struct nonnull_arg_data {
    struct source_location loc;
    struct source_location attr_loc;
    int32_t arg_index;
};

struct unreachable_data {
    struct source_location loc;
};

struct invalid_builtin_data {
    struct source_location loc;
    uint8_t kind;
};

/* ---- helpers ----------------------------------------------------------- */

static const char *loc_file(const struct source_location *loc)
{
    return (loc && loc->filename) ? loc->filename : "<unknown>";
}

static const char *td_name(const struct type_descriptor *td)
{
    return td ? td->type_name : "<unknown>";
}

#define UBSAN_REPORT(loc, fmt, ...)                                            \
    do {                                                                      \
        const struct source_location *ubsan__l = (loc);                       \
        KERROR("undefined behaviour: " fmt " at %s:%u:%u",                     \
               ##__VA_ARGS__, loc_file(ubsan__l),                             \
               ubsan__l ? ubsan__l->line : 0u,                                \
               ubsan__l ? ubsan__l->column : 0u);                             \
        panic("UBSan: " fmt, ##__VA_ARGS__);                                  \
    } while (0)

/* ---- handlers -------------------------------------------------------- */

#define UBSAN_HANDLER __attribute__((used))

UBSAN_HANDLER
void __ubsan_handle_type_mismatch_v1(struct type_mismatch_data_v1 *data,
                                     value_handle ptr);
void __ubsan_handle_type_mismatch_v1(struct type_mismatch_data_v1 *data,
                                     value_handle ptr)
{
    unsigned long align = data ? (1UL << data->log_alignment) : 0;
    if (ptr == 0) {
        UBSAN_REPORT(&data->loc, "null pointer dereference of type %s",
                     td_name(data->type));
    } else if (align && (ptr & (align - 1))) {
        UBSAN_REPORT(&data->loc,
                     "misaligned access: ptr %#lx not aligned to %lu for type %s",
                     (unsigned long)ptr, align, td_name(data->type));
    } else {
        UBSAN_REPORT(&data->loc,
                     "access within address space for a value of type %s at ptr %#lx",
                     td_name(data->type), (unsigned long)ptr);
    }
}

static void ubsan_overflow(struct overflow_data *data, const char *op)
{
    UBSAN_REPORT(&data->loc, "%s integer overflow, type %s", op,
                 td_name(data->type));
}

UBSAN_HANDLER
void __ubsan_handle_add_overflow(struct overflow_data *data,
                                 value_handle lhs, value_handle rhs);
void __ubsan_handle_add_overflow(struct overflow_data *data,
                                 value_handle lhs, value_handle rhs)
{
    (void)lhs; (void)rhs;
    ubsan_overflow(data, "addition");
}

UBSAN_HANDLER
void __ubsan_handle_sub_overflow(struct overflow_data *data,
                                 value_handle lhs, value_handle rhs);
void __ubsan_handle_sub_overflow(struct overflow_data *data,
                                 value_handle lhs, value_handle rhs)
{
    (void)lhs; (void)rhs;
    ubsan_overflow(data, "subtraction");
}

UBSAN_HANDLER
void __ubsan_handle_mul_overflow(struct overflow_data *data,
                                 value_handle lhs, value_handle rhs);
void __ubsan_handle_mul_overflow(struct overflow_data *data,
                                 value_handle lhs, value_handle rhs)
{
    (void)lhs; (void)rhs;
    ubsan_overflow(data, "multiplication");
}

UBSAN_HANDLER
void __ubsan_handle_negate_overflow(struct overflow_data *data,
                                    value_handle old_val);
void __ubsan_handle_negate_overflow(struct overflow_data *data,
                                    value_handle old_val)
{
    (void)old_val;
    ubsan_overflow(data, "negation");
}

UBSAN_HANDLER
void __ubsan_handle_divrem_overflow(struct overflow_data *data,
                                    value_handle lhs, value_handle rhs);
void __ubsan_handle_divrem_overflow(struct overflow_data *data,
                                    value_handle lhs, value_handle rhs)
{
    (void)lhs; (void)rhs;
    UBSAN_REPORT(&data->loc, "division overflow or division by zero, type %s",
                 td_name(data->type));
}

UBSAN_HANDLER
void __ubsan_handle_out_of_bounds(struct out_of_bounds_data *data,
                                  value_handle index);
void __ubsan_handle_out_of_bounds(struct out_of_bounds_data *data,
                                  value_handle index)
{
    UBSAN_REPORT(&data->loc,
                 "array index %#lx out of bounds for type %s (index type %s)",
                 (unsigned long)index, td_name(data->array_type),
                 td_name(data->index_type));
}

UBSAN_HANDLER
void __ubsan_handle_shift_out_of_bounds(struct shift_out_of_bounds_data *data,
                                        value_handle lhs, value_handle rhs);
void __ubsan_handle_shift_out_of_bounds(struct shift_out_of_bounds_data *data,
                                        value_handle lhs, value_handle rhs)
{
    (void)lhs;
    UBSAN_REPORT(&data->loc,
                 "shift exponent %#lx is invalid for %s shifted by %s",
                 (unsigned long)rhs, td_name(data->lhs_type),
                 td_name(data->rhs_type));
}

UBSAN_HANDLER
void __ubsan_handle_load_invalid_value(struct invalid_value_data *data,
                                       value_handle val);
void __ubsan_handle_load_invalid_value(struct invalid_value_data *data,
                                       value_handle val)
{
    UBSAN_REPORT(&data->loc,
                 "load of value %#lx which is not valid for type %s",
                 (unsigned long)val, td_name(data->type));
}

UBSAN_HANDLER
void __ubsan_handle_pointer_overflow(struct pointer_overflow_data *data,
                                     value_handle base, value_handle result);
void __ubsan_handle_pointer_overflow(struct pointer_overflow_data *data,
                                     value_handle base, value_handle result)
{
    UBSAN_REPORT(&data->loc,
                 "pointer arithmetic overflow: base %#lx result %#lx",
                 (unsigned long)base, (unsigned long)result);
}

UBSAN_HANDLER
void __ubsan_handle_nonnull_arg(struct nonnull_arg_data *data);
void __ubsan_handle_nonnull_arg(struct nonnull_arg_data *data)
{
    UBSAN_REPORT(&data->loc,
                 "null passed to argument %d declared nonnull",
                 data ? (int)data->arg_index : -1);
}

UBSAN_HANDLER
_Noreturn void __ubsan_handle_builtin_unreachable(struct unreachable_data *data);
_Noreturn void __ubsan_handle_builtin_unreachable(struct unreachable_data *data)
{
    UBSAN_REPORT(&data->loc, "execution reached __builtin_unreachable()");
}

UBSAN_HANDLER
_Noreturn void __ubsan_handle_missing_return(struct unreachable_data *data);
_Noreturn void __ubsan_handle_missing_return(struct unreachable_data *data)
{
    UBSAN_REPORT(&data->loc,
                 "execution reached end of value-returning function "
                 "without returning a value");
}

UBSAN_HANDLER
void __ubsan_handle_invalid_builtin(struct invalid_builtin_data *data);
void __ubsan_handle_invalid_builtin(struct invalid_builtin_data *data)
{
    UBSAN_REPORT(&data->loc, "invalid use of a builtin (kind %u)",
                 data ? data->kind : 0u);
}
