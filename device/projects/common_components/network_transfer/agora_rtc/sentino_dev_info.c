#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include <string.h>
#include <stdio.h>

#include <components/log.h>
#include "bk_ef.h"
#include "sentino_dev_info.h"

#define TAG "sentino_dev"

#define LOGI(format, ...) BK_LOGI(TAG, format "\n", ##__VA_ARGS__)
#define LOGE(format, ...) BK_LOGE(TAG, format "\n", ##__VA_ARGS__)
#define LOGW(format, ...) BK_LOGW(TAG, format "\n", ##__VA_ARGS__)

#define NVS_KEY_TRIPLE          "d_stn_triple"

static sentino_dev_triple_record_t s_record;
static sentino_dev_state_t         s_state = SENTINO_DEV_UNAUTHORIZED;
static bool                        s_loaded = false;

static bool record_is_valid(const sentino_dev_triple_record_t *r)
{
    return r->magic == SENTINO_TRIPLE_MAGIC
        && r->flag_valid == SENTINO_TRIPLE_FLAG_VALID
        && r->triple.Uuid[0] != '\0'
        && r->triple.Secret[0] != '\0';
}

static bool triples_equal(const sentino_triple_t *a, const sentino_triple_t *b)
{
    return 0 == strncmp(a->Uuid,   b->Uuid,   SENTINO_UUID_SIZE)
        && 0 == strncmp(a->Secret, b->Secret, SENTINO_KEY_SIZE)
        && 0 == strncmp(a->Mac,    b->Mac,    SENTINO_MAC_STR_SIZE)
        && 0 == strncmp(a->Pid,    b->Pid,    SENTINO_PID_SIZE);
}

static int read_from_flash(sentino_dev_triple_record_t *out)
{
    memset(out, 0, sizeof(*out));
    int ret = bk_get_env_enhance(NVS_KEY_TRIPLE, out, sizeof(*out));
    return ret;
}

static int write_to_flash(const sentino_dev_triple_record_t *in)
{
    return bk_set_env_enhance(NVS_KEY_TRIPLE, (void *)in, sizeof(*in));
}

int sentino_dev_info_load(const char *pid_default,
                          const sentino_triple_t *test_triple)
{
    sentino_dev_triple_record_t flash_rec;
    bool flash_valid = false;

    read_from_flash(&flash_rec);
    flash_valid = record_is_valid(&flash_rec);

    if (flash_valid) {
        s_record = flash_rec;
        LOGW("triple loaded from flash: uuid=%s pid=%s",
             s_record.triple.Uuid, s_record.triple.Pid);
    } else {
        memset(&s_record, 0, sizeof(s_record));
        LOGW("no valid triple in flash");
    }

    /* If a test triple is supplied and differs from flash, overwrite. */
    if (test_triple) {
        if (!flash_valid || !triples_equal(&s_record.triple, test_triple)) {
            LOGW("seeding flash from test_triple: uuid=%s", test_triple->Uuid);
            s_record.magic       = SENTINO_TRIPLE_MAGIC;
            s_record.flag_valid  = SENTINO_TRIPLE_FLAG_VALID;
            s_record.triple      = *test_triple;
            memset(s_record.reserve, 0, sizeof(s_record.reserve));
            if (0 != write_to_flash(&s_record)) {
                LOGE("write test_triple to flash failed");
            }
            flash_valid = true;
        }
    }

    /* Backfill PID from default if older record didn't carry one. */
    if (flash_valid && s_record.triple.Pid[0] == '\0' && pid_default) {
        strncpy(s_record.triple.Pid, pid_default, SENTINO_PID_SIZE - 1);
        s_record.triple.Pid[SENTINO_PID_SIZE - 1] = '\0';
        write_to_flash(&s_record);
    }

    s_loaded = true;

    if (flash_valid && s_record.triple.Uuid[0] && s_record.triple.Secret[0]) {
        s_state = SENTINO_DEV_AUTHORIZED;
        return 0;
    }

    s_state = SENTINO_DEV_UNAUTHORIZED;
    LOGE("device UNAUTHORIZED — triple not present, dynamic register or factory burn required");
    return -1;
}

const sentino_triple_t *sentino_dev_info_get_triple(void)
{
    if (!s_loaded || s_state != SENTINO_DEV_AUTHORIZED) {
        return NULL;
    }
    return &s_record.triple;
}

sentino_dev_state_t sentino_dev_info_get_state(void)
{
    return s_state;
}

int sentino_dev_info_save(void)
{
    if (!s_loaded) return -1;
    s_record.magic      = SENTINO_TRIPLE_MAGIC;
    s_record.flag_valid = SENTINO_TRIPLE_FLAG_VALID;
    return write_to_flash(&s_record);
}

int sentino_dev_info_reset(void)
{
    memset(&s_record, 0, sizeof(s_record));
    int ret = write_to_flash(&s_record);
    s_state = SENTINO_DEV_UNAUTHORIZED;
    LOGW("triple wiped");
    return ret;
}
