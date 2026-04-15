#ifndef __BK_SMART_CONFIG_SENTINO_ADAPTER_H__
#define __BK_SMART_CONFIG_SENTINO_ADAPTER_H__

void bk_sconf_trans_stop(void);
int bk_sconf_post_nfc_id(uint8_t *nfc_id);
void agora_ir_mode_config(bool enable);

#if CONFIG_ENABLE_AGORA_DATASTREAM
int bk_sconf_init_datastream_resource(void);
#endif

#endif
