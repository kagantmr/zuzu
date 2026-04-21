#include <zuzu.h>
#include <stdio.h>

int main(int argc, char *argv[])
{
    float a = 3.14f;
    float b = 2.0f;
    float c = a * b;

    // manually format since printf %f may not work yet
    int whole = (int)c;
    int frac = (int)((c - (float)whole) * 1000.0f);
    if (frac < 0) frac = -frac;

    char buf[64];
    snprintf(buf, sizeof(buf), "3.14 * 2.0 = %d.%03d\n", whole, frac);
    printf("%s", buf);

    // verify it's actually 6.28
    if (whole == 6 && frac == 280)
        printf("PASS\n");
    else
        printf("FAIL\n");

    return 0;
}