/**
 * @file ubsan.c
 * @brief Undefined-behaviour sanitizer runtime: each handler logs the violation and panics.
 */
#include <stdint.h>

#define LOG_FMT(fmt) "(ubsan) " fmt
#include "core/log.h"
#include "core/panic.h"

/** @brief Source position emitted by the compiler for a checked operation. */
struct source_location {
    const char *filename;  /**< Source file name, or NULL. */
    uint32_t line;         /**< Line number. */
    uint32_t column;       /**< Column number. */
};

/** @brief Compiler-emitted description of a C type. */
struct type_descriptor {
    uint16_t type_kind;    /**< Type class (integer, float, other). */
    uint16_t type_info;    /**< Class-specific information such as width and signedness. */
    char type_name[1];     /**< NUL-terminated type name (variable length). */
};

/** @brief Opaque operand value passed by instrumented code. */
typedef unsigned long value_handle;

/** @brief Static data for a type mismatch (null, misaligned or oversized access) report. */
struct type_mismatch_data_v1 {
    struct source_location loc;          /**< Location of the access. */
    const struct type_descriptor *type;  /**< Type being accessed. */
    uint8_t log_alignment;               /**< Log2 of the required alignment. */
    uint8_t type_check_kind;             /**< Kind of check (load, store, member access, ...). */
};

/** @brief Static data for an arithmetic overflow report. */
struct overflow_data {
    struct source_location loc;          /**< Location of the operation. */
    const struct type_descriptor *type;  /**< Operand type. */
};

/** @brief Static data for an invalid shift report. */
struct shift_out_of_bounds_data {
    struct source_location loc;              /**< Location of the shift. */
    const struct type_descriptor *lhs_type;  /**< Type of the shifted value. */
    const struct type_descriptor *rhs_type;  /**< Type of the shift amount. */
};

/** @brief Static data for an array index out of bounds report. */
struct out_of_bounds_data {
    struct source_location loc;               /**< Location of the access. */
    const struct type_descriptor *array_type; /**< Array type. */
    const struct type_descriptor *index_type; /**< Index type. */
};

/** @brief Static data for a load of an invalid value (for example a bad enum or bool). */
struct invalid_value_data {
    struct source_location loc;          /**< Location of the load. */
    const struct type_descriptor *type;  /**< Type being loaded. */
};

/** @brief Static data for a pointer arithmetic overflow report. */
struct pointer_overflow_data {
    struct source_location loc;  /**< Location of the arithmetic. */
};

/** @brief Static data for a null argument passed to a nonnull parameter. */
struct nonnull_arg_data {
    struct source_location loc;       /**< Location of the call. */
    struct source_location attr_loc;  /**< Location of the nonnull attribute. */
    int32_t arg_index;                /**< Index of the offending argument. */
};

/** @brief Static data for unreachable-code and missing-return reports. */
struct unreachable_data {
    struct source_location loc;  /**< Location of the reported point. */
};

/** @brief Static data for an invalid builtin use report. */
struct invalid_builtin_data {
    struct source_location loc;  /**< Location of the builtin call. */
    uint8_t kind;                /**< Which builtin was misused. */
};

/**
 * @brief Get a printable file name from a source location.
 *
 * @param[in] loc Source location, may be NULL.
 * @return File name, or "<unknown>" if unavailable.
 */
static const char *loc_file(const struct source_location *loc)
{
    return (loc && loc->filename) ? loc->filename : "<unknown>";
}

/**
 * @brief Get a printable type name from a type descriptor.
 *
 * @param[in] td Type descriptor, may be NULL.
 * @return Type name, or "<unknown>" if @p td is NULL.
 */
static const char *td_name(const struct type_descriptor *td)
{
    return td ? td->type_name : "<unknown>";
}

/** @brief Log the violation with its source location, then panic. Does not return. */
#define UBSAN_REPORT(loc, fmt, ...)                                                                \
    do {                                                                                           \
        const struct source_location *ubsan__l = (loc);                                            \
        KERROR("undefined behaviour: " fmt " at %s:%u:%u", ##__VA_ARGS__, loc_file(ubsan__l),      \
               ubsan__l ? ubsan__l->line : 0u, ubsan__l ? ubsan__l->column : 0u);                  \
        panic("UBSan: " fmt, ##__VA_ARGS__);                                                       \
    } while (0)

/** @brief Marks a handler as used so the linker keeps it for compiler-generated calls. */
#define UBSAN_HANDLER __attribute__((used))

/**
 * @brief Handler for a null, misaligned or otherwise invalid pointer access.
 *
 * @param[in] data Static description of the access.
 * @param[in] ptr Pointer value that was accessed.
 */
UBSAN_HANDLER
void __ubsan_handle_type_mismatch_v1(struct type_mismatch_data_v1 *data, value_handle ptr);
void __ubsan_handle_type_mismatch_v1(struct type_mismatch_data_v1 *data, value_handle ptr)
{
    unsigned long align = data ? (1UL << data->log_alignment) : 0;
    if (ptr == 0) {
        UBSAN_REPORT(&data->loc, "null pointer dereference of type %s", td_name(data->type));
    } else if (align && (ptr & (align - 1))) {
        UBSAN_REPORT(&data->loc, "misaligned access: ptr %#lx not aligned to %lu for type %s",
                     (unsigned long)ptr, align, td_name(data->type));
    } else {
        UBSAN_REPORT(&data->loc, "access within address space for a value of type %s at ptr %#lx",
                     td_name(data->type), (unsigned long)ptr);
    }
}

/**
 * @brief Report an integer overflow and panic.
 *
 * @param[in] data Static overflow description from the compiler.
 * @param[in] op Name of the overflowing operation.
 */
static void ubsan_overflow(struct overflow_data *data, const char *op)
{
    UBSAN_REPORT(&data->loc, "%s integer overflow, type %s", op, td_name(data->type));
}

/**
 * @brief Handler for signed addition overflow.
 *
 * @param[in] data Static description of the operation.
 * @param[in] lhs Left operand (unused).
 * @param[in] rhs Right operand (unused).
 */
UBSAN_HANDLER
void __ubsan_handle_add_overflow(struct overflow_data *data, value_handle lhs, value_handle rhs);
void __ubsan_handle_add_overflow(struct overflow_data *data, value_handle lhs, value_handle rhs)
{
    (void)lhs;
    (void)rhs;
    ubsan_overflow(data, "addition");
}

/**
 * @brief Handler for signed subtraction overflow.
 *
 * @param[in] data Static description of the operation.
 * @param[in] lhs Left operand (unused).
 * @param[in] rhs Right operand (unused).
 */
UBSAN_HANDLER
void __ubsan_handle_sub_overflow(struct overflow_data *data, value_handle lhs, value_handle rhs);
void __ubsan_handle_sub_overflow(struct overflow_data *data, value_handle lhs, value_handle rhs)
{
    (void)lhs;
    (void)rhs;
    ubsan_overflow(data, "subtraction");
}

/**
 * @brief Handler for signed multiplication overflow.
 *
 * @param[in] data Static description of the operation.
 * @param[in] lhs Left operand (unused).
 * @param[in] rhs Right operand (unused).
 */
UBSAN_HANDLER
void __ubsan_handle_mul_overflow(struct overflow_data *data, value_handle lhs, value_handle rhs);
void __ubsan_handle_mul_overflow(struct overflow_data *data, value_handle lhs, value_handle rhs)
{
    (void)lhs;
    (void)rhs;
    ubsan_overflow(data, "multiplication");
}

/**
 * @brief Handler for signed negation overflow.
 *
 * @param[in] data Static description of the operation.
 * @param[in] old_val Value being negated (unused).
 */
UBSAN_HANDLER
void __ubsan_handle_negate_overflow(struct overflow_data *data, value_handle old_val);
void __ubsan_handle_negate_overflow(struct overflow_data *data, value_handle old_val)
{
    (void)old_val;
    ubsan_overflow(data, "negation");
}

/**
 * @brief Handler for division overflow or division by zero.
 *
 * @param[in] data Static description of the operation.
 * @param[in] lhs Dividend (unused).
 * @param[in] rhs Divisor (unused).
 */
UBSAN_HANDLER
void __ubsan_handle_divrem_overflow(struct overflow_data *data, value_handle lhs, value_handle rhs);
void __ubsan_handle_divrem_overflow(struct overflow_data *data, value_handle lhs, value_handle rhs)
{
    (void)lhs;
    (void)rhs;
    UBSAN_REPORT(&data->loc, "division overflow or division by zero, type %s", td_name(data->type));
}

/**
 * @brief Handler for an array index outside the array bounds.
 *
 * @param[in] data Static description of the access.
 * @param[in] index Offending index value.
 */
UBSAN_HANDLER
void __ubsan_handle_out_of_bounds(struct out_of_bounds_data *data, value_handle index);
void __ubsan_handle_out_of_bounds(struct out_of_bounds_data *data, value_handle index)
{
    UBSAN_REPORT(&data->loc, "array index %#lx out of bounds for type %s (index type %s)",
                 (unsigned long)index, td_name(data->array_type), td_name(data->index_type));
}

/**
 * @brief Handler for a shift by an invalid amount.
 *
 * @param[in] data Static description of the shift.
 * @param[in] lhs Shifted value (unused).
 * @param[in] rhs Shift amount.
 */
UBSAN_HANDLER
void __ubsan_handle_shift_out_of_bounds(struct shift_out_of_bounds_data *data, value_handle lhs,
                                        value_handle rhs);
void __ubsan_handle_shift_out_of_bounds(struct shift_out_of_bounds_data *data, value_handle lhs,
                                        value_handle rhs)
{
    (void)lhs;
    UBSAN_REPORT(&data->loc, "shift exponent %#lx is invalid for %s shifted by %s",
                 (unsigned long)rhs, td_name(data->lhs_type), td_name(data->rhs_type));
}

/**
 * @brief Handler for loading a value not valid for its type.
 *
 * @param[in] data Static description of the load.
 * @param[in] val Offending value.
 */
UBSAN_HANDLER
void __ubsan_handle_load_invalid_value(struct invalid_value_data *data, value_handle val);
void __ubsan_handle_load_invalid_value(struct invalid_value_data *data, value_handle val)
{
    UBSAN_REPORT(&data->loc, "load of value %#lx which is not valid for type %s",
                 (unsigned long)val, td_name(data->type));
}

/**
 * @brief Handler for pointer arithmetic overflow.
 *
 * @param[in] data Static description of the operation.
 * @param[in] base Pointer before the arithmetic.
 * @param[in] result Pointer after the arithmetic.
 */
UBSAN_HANDLER
void __ubsan_handle_pointer_overflow(struct pointer_overflow_data *data, value_handle base,
                                     value_handle result);
void __ubsan_handle_pointer_overflow(struct pointer_overflow_data *data, value_handle base,
                                     value_handle result)
{
    UBSAN_REPORT(&data->loc, "pointer arithmetic overflow: base %#lx result %#lx",
                 (unsigned long)base, (unsigned long)result);
}

/**
 * @brief Handler for a null argument passed to a nonnull parameter.
 *
 * @param[in] data Static description of the call.
 */
UBSAN_HANDLER
void __ubsan_handle_nonnull_arg(struct nonnull_arg_data *data);
void __ubsan_handle_nonnull_arg(struct nonnull_arg_data *data)
{
    UBSAN_REPORT(&data->loc, "null passed to argument %d declared nonnull",
                 data ? (int)data->arg_index : -1);
}

/**
 * @brief Handler for execution reaching __builtin_unreachable(). Does not return.
 *
 * @param[in] data Static description of the location.
 */
UBSAN_HANDLER
_Noreturn void __ubsan_handle_builtin_unreachable(struct unreachable_data *data);
_Noreturn void __ubsan_handle_builtin_unreachable(struct unreachable_data *data)
{
    UBSAN_REPORT(&data->loc, "execution reached __builtin_unreachable()");
}

/**
 * @brief Handler for control reaching the end of a value-returning function. Does not return.
 *
 * @param[in] data Static description of the location.
 */
UBSAN_HANDLER
_Noreturn void __ubsan_handle_missing_return(struct unreachable_data *data);
_Noreturn void __ubsan_handle_missing_return(struct unreachable_data *data)
{
    UBSAN_REPORT(&data->loc, "execution reached end of value-returning function "
                             "without returning a value");
}

/**
 * @brief Handler for an invalid builtin use.
 *
 * @param[in] data Static description of the call.
 */
UBSAN_HANDLER
void __ubsan_handle_invalid_builtin(struct invalid_builtin_data *data);
void __ubsan_handle_invalid_builtin(struct invalid_builtin_data *data)
{
    UBSAN_REPORT(&data->loc, "invalid use of a builtin (kind %u)", data ? data->kind : 0u);
}
