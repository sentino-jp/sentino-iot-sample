// REST API Client for Sentino IoT Platform (ref-rest-api.md)

const BASE = '/api'; // proxied to https://api-iot.sentino.jp

const PUBLIC_HEADERS = {
  timezone: 'Asia/Shanghai',
  language: 'zh_CN',
  data_center_code: 'cn',
  client_id: 'Y2V0dXMtaW90LWFwcDpvbEFESkNtV2xGSVZYWTFxMWx4MHdVclViemU3WHdlUg==',
  encrypt_type: 'AES/ECB/PKCS5Padding',
  channel_identifier: 'kfvb4s5e',
  package_name: 'jp.sentino.general',
  app_id: 'krkfvb4s5e91hq',
};

const BASIC_AUTH = 'Basic Y2V0dXMtaW90LWFwcDpvbEFESkNtV2xGSVZYWTFxMWx4MHdVclViemU3WHdlUg==';

let accessToken = null;
let userId = null;

function authHeaders() {
  return {
    ...PUBLIC_HEADERS,
    Authorization: `Bearer ${accessToken}`,
  };
}

async function postJSON(path, body = {}) {
  const res = await fetch(`${BASE}${path}`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', ...authHeaders() },
    body: JSON.stringify(body),
  });
  return res.json();
}

/** Login with UID — auto-registers if UID doesn't exist */
export async function login(uid, password) {
  const params = new URLSearchParams({
    grant_type: 'uid',
    uid,
    password,
    area_code: '86',
    user_country_key: 'CN',
  });

  const res = await fetch(
    `${BASE}/auth/oauth/token?grant_type=uid&area_code=86&app_id=krkfvb4s5e91hq`,
    {
      method: 'POST',
      headers: {
        'Content-Type': 'application/x-www-form-urlencoded',
        Authorization: BASIC_AUTH,
        ...PUBLIC_HEADERS,
      },
      body: params,
    }
  );
  const json = await res.json();
  if (json.code !== 200) throw new Error(json.message || 'Login failed');

  accessToken = json.data.access_token;
  userId = json.data.userId || uid;
  return json.data;
}

/** Get asset tree — returns root { assetId, assetName } */
export async function getAssetTree() {
  const json = await postJSON('/business-app/v1/asset/assetTree');
  if (json.code !== 200) throw new Error(json.message || 'Failed to get asset tree');
  // API returns array with {id, name, childrens} — normalize to {assetId, assetName}
  const data = json.data;
  if (Array.isArray(data) && data.length > 0) {
    return { assetId: data[0].id, assetName: data[0].name, children: data[0].childrens || [] };
  }
  // Fallback if format matches docs
  return data;
}

/** Get data center list — returns array with mqttUrl, mqttSslPort, etc. */
export async function getDataCenterList() {
  const json = await postJSON('/business-app/v1/common/getDataCenterList');
  if (json.code !== 200) throw new Error(json.message || 'Failed to get data centers');
  return json.data;
}

/** Get product info by product ID */
export async function getProductInfo(productId) {
  const res = await fetch(
    `${BASE}/business-app/v1/product/getByProductId?productId=${productId}`,
    {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded', ...authHeaders() },
    }
  );
  const json = await res.json();
  if (json.code !== 200) throw new Error(json.message || 'Failed to get product info');
  return json.data;
}

/** Encrypt provisioning data */
export async function dataEncrypt(content) {
  const json = await postJSON('/business-app/v1/distributionNet/dataEncrypt', {
    content: typeof content === 'string' ? content : JSON.stringify(content),
    encryptType: 0,
    protocol: '1',
    type: 'thing.network.set',
  });
  if (json.code !== 200) throw new Error(json.message || 'Encryption failed');
  return json.data;
}

/** Check device bind result — returns 0 on success */
export async function checkBindResult(uuid) {
  const json = await postJSON(`/business-app/v1/device/bind/checkBindResult/${uuid}`);
  if (json.code !== 200) throw new Error(json.message || 'Bind check failed');
  return json.data;
}

/** Get device list */
export async function getDeviceList(assetIds) {
  const json = await postJSON('/business-app/v1/device/getHomeDeviceAndGroupList', { assetIds });
  if (json.code !== 200) throw new Error(json.message || 'Failed to get device list');
  return json.data;
}

/** Get simple device info */
export async function getDeviceInfo(productId, uuid) {
  const json = await postJSON('/business-app/v1/device/getSimpleDeviceInfo', { productId, uuid });
  if (json.code !== 200) throw new Error(json.message || 'Failed to get device info');
  return json.data;
}

export function getUserId() {
  return userId;
}

export function getToken() {
  return accessToken;
}
