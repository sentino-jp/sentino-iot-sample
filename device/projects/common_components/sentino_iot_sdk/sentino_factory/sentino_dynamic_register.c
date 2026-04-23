#include <components/log.h>
#include "sentino_dynamic_register.h"

#define TAG "sentino_factory"

int sentino_dynamic_register(sentino_triple_t *out)
{
    (void)out;
    BK_LOGW(TAG, "dynamic_register not implemented — Sentino hasn't published a "
                 "register protocol yet. Use SENTINO_TRIPLE_TEST or factory "
                 "burn-in for now.\n");
    return -1;
}
