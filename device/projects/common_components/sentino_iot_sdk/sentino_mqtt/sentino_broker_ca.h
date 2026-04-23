/*
 * Sentino IoT Platform self-signed CA root certificate.
 *
 * Used by mqtts/ to verify the broker's server certificate when use_tls=true.
 * This is a public key — safe to commit to source control.
 *
 * Source:    client/ca.pem (provided by broker ops 2026-04-23)
 * NotBefore: Apr 17 08:51:20 2026 GMT
 * NotAfter:  Mar 24 08:51:20 2126 GMT
 * Subject:   CN=mqtt.sentino.co.jp, O=Sentino, OU=IoT Platform
 *
 * Rotation: edit this file + OTA push. Given 100y validity, rotation is
 * expected to be rare; emergency rotation (CA private key compromise)
 * requires forced fleet-wide OTA before broker cert can be re-issued.
 */

#ifndef __SENTINO_BROKER_CA_H__
#define __SENTINO_BROKER_CA_H__

#ifdef __cplusplus
extern "C" {
#endif

extern const char SENTINO_BROKER_CA_PEM[];

#ifdef __cplusplus
}
#endif

#endif /* __SENTINO_BROKER_CA_H__ */
