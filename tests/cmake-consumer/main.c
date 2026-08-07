#include "chronopipe/chronopipe.h"

int main(void) {
    return cp_dtype_size(CP_DTYPE_F32) == sizeof(float) ? 0 : 1;
}
