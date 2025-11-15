#include "lib/convert.h"
#include "stddef.h"

int is_digit(char c) {
    return (c == '0') || (c == '1') || (c == '2') || (c == '3') || (c == '4') ||
    (c == '5') || (c == '6') || (c == '7') || (c == '8') || (c == '9');
}


int atoi(const char *str) {
    int value = 0; 
    char is_negative = 0;
    if (*str == '-') {
        is_negative = 1; // 2's complement will be applied if is_negative is 1.
        str++;
    } else if (*str == '+') {
        is_negative = 0; // 2's complement will be applied if is_negative is 1.
        str++;
    }
    
    // no need for extra check, the while loop will rule out invalid characters anyway
    
    while (is_digit(*str)) { // omitted null termination because this also covers that case
        value = value * 10 + (*str++ - '0');
    }

    return is_negative ? -value : value;
}


char *itoa(int value, char *str, unsigned int base) {
    char *result = str;
    
    if (value == 0) {
        *str++ = '0';
        *str = '\0';
        return result;
    }

    if (value < 0) {
        *str = '-';
        str++;
        value = -value;
    } 
 

    char bfr[32];
    char* buffer = bfr;
    
    while (value) {
        int digit = value % base;
        if (digit < 10) {
            *buffer++ = '0' + digit;        // extract digits for systems lesser than 10 as a base
        } else {
            *buffer++ = 'a' + (digit- 10);  // extract digits for systems more than 10 as a base
        }
        value /= base;
    }

    // invert that section of buffer into the string because this was a mess
    buffer--; // now points to last digit
    while (buffer >= bfr) {
        *str++ = *buffer;
        buffer--;
    }

    *str = '\0';
    return result;
}


char *utoa(unsigned int value, char *str, unsigned int base) {
    char *result = str;
    
    if (value == 0) {
        *str++ = '0';
        *str = '\0';
        return result;
    }
 
    char bfr[32];
    char* buffer = bfr;
    
    while (value) {
        int digit = value % base;
        if (digit < 10) {
            *buffer++ = '0' + digit;        // extract digits for systems lesser than 10 as a base
        } else {
            *buffer++ = 'a' + (digit - 10);  // extract digits for systems more than 10 as a base
        }
        value /= base;
    }

    // invert that section of buffer into the string because this was a mess
    buffer--; // now points to last digit
    while (buffer >= bfr) {
        *str++ = *buffer;
        buffer--;
    }

    *str = '\0';
    return result;
}