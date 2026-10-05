#include "plaza_hacks.h"
#include <stdlib.h>
#include <string.h>
extern "C" int sms_force_clean_plaza() {
    const char* value=getenv("SMS_CLEAN_SHINE_GATE");
    return value && *value && strcmp(value,"0") && strcmp(value,"off");
}
