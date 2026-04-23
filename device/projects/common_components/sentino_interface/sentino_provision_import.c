#include <string.h>
#include <stdbool.h>

#include "sentino_provision_import.h"
#include "sentino_mqtt.h"
#include "sentino_dev_info.h"

static bool s_triple_loaded = false;

void sentino_provision_apply_from_ble(const char *user_id,
                                      const char *asset_id,
                                      const char *broker_url,
                                      uint16_t port)
{
    sentino_provision_info_t prov = {0};
    if (user_id    && *user_id)    strncpy(prov.user_id,     user_id,    sizeof(prov.user_id)     - 1);
    if (asset_id   && *asset_id)   strncpy(prov.asset_id,    asset_id,   sizeof(prov.asset_id)    - 1);
    if (broker_url && *broker_url) strncpy(prov.mqtt_broker, broker_url, sizeof(prov.mqtt_broker) - 1);
    prov.mqtt_port = port ? port : 8883;
    sentino_provision_info_write(&prov);
}

void sentino_provision_ensure_loaded(void)
{
    if (s_triple_loaded) return;

#ifdef SENTINO_TRIPLE_TEST
    sentino_triple_t test = {0};
    strncpy(test.Uuid,   SENTINO_TEST_UUID,   sizeof(test.Uuid)   - 1);
    strncpy(test.Secret, SENTINO_TEST_SECRET, sizeof(test.Secret) - 1);
    strncpy(test.Mac,    SENTINO_TEST_MAC,    sizeof(test.Mac)    - 1);
    strncpy(test.Pid,    SENTINO_TEST_PID,    sizeof(test.Pid)    - 1);
    sentino_dev_info_load(SENTINO_DEFAULT_PID, &test);
#else
    sentino_dev_info_load(SENTINO_DEFAULT_PID, NULL);
#endif

    s_triple_loaded = true;
}

const char *sentino_provision_get_uuid(void)
{
    sentino_provision_ensure_loaded();
    const sentino_triple_t *t = sentino_dev_info_get_triple();
    return t ? t->Uuid : "";
}

const char *sentino_provision_get_pid(void)
{
    sentino_provision_ensure_loaded();
    const sentino_triple_t *t = sentino_dev_info_get_triple();
    return (t && t->Pid[0]) ? t->Pid : SENTINO_DEFAULT_PID;
}

void sentino_provision_clear(void)
{
    sentino_provision_info_clear();
}
