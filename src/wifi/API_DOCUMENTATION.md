# ZenoPCB WiFi Provisioning API Documentation

## Tổng quan

Module WiFi Provisioning cung cấp RESTful API để cấu hình kết nối mạng cho thiết bị ESP32 thông qua Access Point mode. Hỗ trợ 3 loại kết nối: **WiFi**, **Ethernet**, và **4G/Cellular**.

## Kích hoạt AP Mode

**Cách 1**: Giữ nút IO-0 (BOOT) trong 3 giây
**Cách 2**: Gọi `provisioning.startAPMode()` trong code

Khi AP mode được kích hoạt:
- ESP32 sẽ phát WiFi với SSID: `ZENO-{ChipID}` (ví dụ: `ZENO-1A2B3C4D`)
- Password mặc định: `zenopcb12345`
- IP mặc định: `192.168.4.1`
- Port: `80`

## Base URL

```
http://192.168.4.1
```

## CORS Support

Tất cả endpoints đều hỗ trợ CORS với headers:
- `Access-Control-Allow-Origin: *`
- `Access-Control-Allow-Methods: GET, POST, OPTIONS`
- `Access-Control-Allow-Headers: Content-Type, Authorization`

---

## API Endpoints Summary

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/` | API documentation |
| GET | `/api/info` | Device information |
| GET | `/api/networks` | Scan WiFi networks |
| GET | `/api/config` | Get current configuration |
| POST | `/api/config` | Save basic configuration |
| POST | `/api/verify` | Verify WiFi credentials |
| POST | `/api/connect/wifi` | ⭐ Connect via WiFi (DHCP/Static) |
| POST | `/api/connect/ethernet` | ⭐ Connect via Ethernet (DHCP/Static) |
| POST | `/api/connect/cellular` | ⭐ Connect via 4G/Cellular |
| POST | `/api/reset` | Factory reset |

---

## API Endpoints Detail

### 1. GET / - API Documentation

Trả về thông tin API và danh sách endpoints.

**Request:**
```http
GET / HTTP/1.1
Host: 192.168.4.1
```

**Response:**
```json
{
  "name": "ZenoPCB WiFi Provisioning API",
  "version": "1.3.0",
  "endpoints": {
    "GET /api/info": "Device information (includes supportedConnections)",
    "GET /api/networks": "Scan WiFi networks (max 20, sorted by signal)",
    "GET /api/config": "Get current configuration",
    "POST /api/config": "Save configuration",
    "POST /api/verify": "Verify WiFi credentials before saving",
    "POST /api/connect/wifi": "Connect via WiFi (supports DHCP/Static IP)",
    "POST /api/connect/ethernet": "Connect via Ethernet (supports DHCP/Static IP)",
    "POST /api/connect/cellular": "Connect via 4G/Cellular (APN config)",
    "POST /api/reset": "Factory reset"
  }
}
```

---

### 2. GET /api/info - Thông tin thiết bị

Lấy thông tin thiết bị bao gồm chip ID, trạng thái WiFi, cấu hình hiện tại và **các loại kết nối được hỗ trợ**.

**Request:**
```http
GET /api/info HTTP/1.1
Host: 192.168.4.1
```

**Response:**
```json
{
  "chipId": "1A2B3C4D",
  "apSSID": "ZENO-1A2B3C4D",
  "deviceType": "Sensor Hub",
  "deviceModel": "ZenoPCB SH-01",
  "firmwareVersion": "1.0.0",
  "manufacturer": "ZenoPCB",
  "provisionedDeviceId": "04B245F9E8D7C6B5A4938271F0E1D2C3",
  "provisionedToken": "A1B2C3D4E5F6789012345678ABCDEF01",
  "supportedConnections": ["wifi", "ethernet", "cellular"],
  "configured": true,
  "connectionType": "wifi",
  "userId": "user123",
  "deviceId": "device_abc123",
  "wifiSSID": "MyWiFi",
  "wifiConnected": false,
  "wifiIP": "",
  "wifiRSSI": 0
}
```

**Response Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `chipId` | string | Mã chip ESP32 (8 ký tự hex) |
| `apSSID` | string | Tên WiFi Access Point |
| `deviceType` | string | Loại thiết bị (set trong firmware) |
| `deviceModel` | string | Tên model/sản phẩm |
| `firmwareVersion` | string | Phiên bản firmware |
| `manufacturer` | string | Nhà sản xuất |
| `provisionedDeviceId` | string | **Device ID 32 ký tự từ server ZenoPCB (lưu trong NVS)** |
| `provisionedToken` | string | **Token 32 ký tự từ server ZenoPCB (lưu trong NVS)** |
| `supportedConnections` | array | **Danh sách loại kết nối được hỗ trợ** (`"wifi"`, `"ethernet"`, `"cellular"`) |
| `configured` | boolean | Thiết bị đã được cấu hình hay chưa |
| `connectionType` | string | Loại kết nối hiện tại |
| `userId` | string | User ID của ZenoPCB Platform |
| `deviceId` | string | Device ID duy nhất trên ZenoPCB Platform |
| `wifiSSID` | string | SSID WiFi đã cấu hình |
| `wifiConnected` | boolean | Trạng thái kết nối WiFi |
| `wifiIP` | string | Địa chỉ IP (nếu đã kết nối) |
| `wifiRSSI` | number | Cường độ tín hiệu WiFi (dBm) |

**Note:** 
- `supportedConnections` được set trong firmware, giúp app biết thiết bị hỗ trợ loại kết nối nào để hiển thị UI phù hợp.
- `provisionedDeviceId` và `provisionedToken` là credentials được cấp từ server ZenoPCB, lưu trong NVS và được dùng để xác thực với cloud.

---

### 3. GET /api/networks - Quét mạng WiFi

Quét và trả về danh sách các mạng WiFi khả dụng (tối đa 20, sắp xếp theo signal strength).

**Request:**
```http
GET /api/networks HTTP/1.1
Host: 192.168.4.1
```

**Response:**
```json
{
  "success": true,
  "count": 3,
  "networks": [
    {
      "ssid": "MyWiFi",
      "rssi": -45,
      "encryption": "WPA2"
    },
    {
      "ssid": "Neighbor_WiFi",
      "rssi": -67,
      "encryption": "WPA/WPA2"
    },
    {
      "ssid": "Open_Network",
      "rssi": -80,
      "encryption": "Open"
    }
  ]
}
```

**Network Object Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `ssid` | string | Tên mạng WiFi |
| `rssi` | number | Cường độ tín hiệu (dBm, -30 là mạnh nhất, -90 là yếu nhất) |
| `encryption` | string | Loại mã hóa: "Open", "WEP", "WPA", "WPA2", "WPA/WPA2", "WPA2-Enterprise" |

---

### 4. GET /api/config - Lấy cấu hình hiện tại

Lấy cấu hình đã lưu của thiết bị.

**Request:**
```http
GET /api/config HTTP/1.1
Host: 192.168.4.1
```

**Response:**
```json
{
  "configured": true,
  "connectionType": "wifi",
  "userId": "user123",
  "deviceId": "device_abc123",
  "wifiSSID": "MyWiFi",
  "wifiDHCP": true,
  "ethernetDHCP": true,
  "cellularAPN": ""
}
```

**Note:** Password không được trả về vì lý do bảo mật.

---

### 5. POST /api/config - Lưu cấu hình cơ bản

Lưu thông tin cơ bản của thiết bị (userId, deviceId). Để cấu hình kết nối, sử dụng `/api/connect/*`.

**Request:**
```http
POST /api/config HTTP/1.1
Host: 192.168.4.1
Content-Type: application/json

{
  "userId": "user123",
  "deviceId": "device_abc123"
}
```

**Request Body Fields:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `userId` | string | Optional | User ID của ZenoPCB Platform |
| `deviceId` | string | Optional | Device ID duy nhất trên ZenoPCB Platform |

**Success Response:**
```json
{
  "success": true,
  "message": "Configuration saved",
  "configured": true
}
```

---

### 6. POST /api/verify - Kiểm tra WiFi credentials

Kiểm tra WiFi SSID và password có đúng không **trước khi kết nối**. 

**Request:**
```http
POST /api/verify HTTP/1.1
Host: 192.168.4.1
Content-Type: application/json

{
  "wifiSSID": "MyWiFi",
  "wifiPassword": "mypassword123"
}
```

**Success Response:**
```json
{
  "success": true,
  "message": "WiFi credentials verified successfully",
  "ssid": "MyWiFi",
  "rssi": -45,
  "ip": "192.168.1.105"
}
```

**Error Response:**
```json
{
  "success": false,
  "error": "WiFi connection failed",
  "reason": "WRONG_PASSWORD",
  "ssid": "MyWiFi"
}
```

**Possible `reason` values:**

| Reason | Description |
|--------|-------------|
| `WRONG_PASSWORD` | Sai password WiFi |
| `NO_AP_FOUND` | Không tìm thấy WiFi với SSID này |
| `CONNECTION_TIMEOUT` | Timeout khi kết nối |
| `AUTH_FAILED` | Xác thực thất bại |

---

## 🌐 Connection APIs

### 7. POST /api/connect/wifi - Kết nối WiFi ⭐

Kết nối qua WiFi với hỗ trợ **DHCP** hoặc **Static IP**.

**Request (DHCP - Mặc định):**
```http
POST /api/connect/wifi HTTP/1.1
Host: 192.168.4.1
Content-Type: application/json

{
  "userId": "user123",
  "deviceId": "device_abc123",
  "wifiSSID": "MyWiFi",
  "wifiPassword": "password123",
  "dhcp": true
}
```

**Request (Static IP):**
```http
POST /api/connect/wifi HTTP/1.1
Host: 192.168.4.1
Content-Type: application/json

{
  "userId": "user123",
  "deviceId": "device_abc123",
  "wifiSSID": "MyWiFi",
  "wifiPassword": "password123",
  "dhcp": false,
  "ip": "192.168.1.100",
  "gateway": "192.168.1.1",
  "subnet": "255.255.255.0",
  "dns": "8.8.8.8"
}
```

**Request Body Fields:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `userId` | string | Optional | User ID của ZenoPCB Platform |
| `deviceId` | string | Optional | Device ID duy nhất trên ZenoPCB Platform |
| `wifiSSID` | string | **Required** | SSID WiFi để kết nối |
| `wifiPassword` | string | Optional | Password WiFi |
| `dhcp` | boolean | Optional | Sử dụng DHCP (default: `true`) |
| `ip` | string | If dhcp=false | Static IP address |
| `gateway` | string | If dhcp=false | Gateway address |
| `subnet` | string | If dhcp=false | Subnet mask |
| `dns` | string | Optional | DNS server |

**Success Response (DHCP):**
```json
{
  "success": true,
  "message": "Connecting to WiFi...",
  "connectionType": "wifi",
  "ssid": "MyWiFi",
  "dhcp": true
}
```

**Success Response (Static IP):**
```json
{
  "success": true,
  "message": "Connecting to WiFi...",
  "connectionType": "wifi",
  "ssid": "MyWiFi",
  "dhcp": false,
  "ip": "192.168.1.100",
  "gateway": "192.168.1.1",
  "subnet": "255.255.255.0",
  "dns": "8.8.8.8"
}
```

**Error Response:**
```json
{
  "success": false,
  "error": "wifiSSID is required"
}
```

**Hành vi sau khi gọi API:**
1. ESP32 lưu cấu hình vào NVS
2. Tắt Access Point mode
3. Kết nối đến WiFi với config đã chỉ định
4. Nếu dùng Static IP, apply IP settings trước khi connect

---

### 8. POST /api/connect/ethernet - Kết nối Ethernet ⭐

Cấu hình kết nối Ethernet với hỗ trợ **DHCP** hoặc **Static IP**.

**Request (DHCP - Mặc định):**
```http
POST /api/connect/ethernet HTTP/1.1
Host: 192.168.4.1
Content-Type: application/json

{
  "userId": "user123",
  "deviceId": "device_abc123",
  "dhcp": true
}
```

**Request (Static IP):**
```http
POST /api/connect/ethernet HTTP/1.1
Host: 192.168.4.1
Content-Type: application/json

{
  "userId": "user123",
  "deviceId": "device_abc123",
  "dhcp": false,
  "ip": "192.168.1.100",
  "gateway": "192.168.1.1",
  "subnet": "255.255.255.0",
  "dns": "8.8.8.8"
}
```

**Request Body Fields:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `userId` | string | Optional | User ID của ZenoPCB Platform |
| `deviceId` | string | Optional | Device ID duy nhất trên ZenoPCB Platform |
| `dhcp` | boolean | Optional | Sử dụng DHCP (default: `true`) |
| `ip` | string | If dhcp=false | Static IP address |
| `gateway` | string | If dhcp=false | Gateway address |
| `subnet` | string | If dhcp=false | Subnet mask |
| `dns` | string | Optional | DNS server |

**Success Response (DHCP):**
```json
{
  "success": true,
  "message": "Ethernet mode configured. Exiting AP mode...",
  "connectionType": "ethernet",
  "dhcp": true
}
```

**Success Response (Static IP):**
```json
{
  "success": true,
  "message": "Ethernet mode configured. Exiting AP mode...",
  "connectionType": "ethernet",
  "dhcp": false,
  "ip": "192.168.1.100",
  "gateway": "192.168.1.1",
  "subnet": "255.255.255.0",
  "dns": "8.8.8.8"
}
```

**Error Response:**
```json
{
  "success": false,
  "error": "Static IP is required when dhcp=false"
}
```

**Hành vi sau khi gọi API:**
1. ESP32 lưu cấu hình Ethernet vào NVS
2. Tắt Access Point mode
3. **KHÔNG** kết nối WiFi
4. Gọi callback `onConfigReceived` để firmware xử lý Ethernet hardware

---

### 9. POST /api/connect/cellular - Kết nối 4G/Cellular ⭐

Cấu hình kết nối 4G/Cellular. **Không hỗ trợ DHCP/Static IP** vì địa chỉ IP được cấp bởi nhà mạng.

**Request:**
```http
POST /api/connect/cellular HTTP/1.1
Host: 192.168.4.1
Content-Type: application/json

{
  "userId": "user123",
  "deviceId": "device_abc123",
  "apn": "internet",
  "user": "",
  "pass": ""
}
```

**Request Body Fields:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `userId` | string | Optional | User ID của ZenoPCB Platform |
| `deviceId` | string | Optional | Device ID duy nhất trên ZenoPCB Platform |
| `apn` | string | **Required** | Access Point Name của nhà mạng |
| `user` | string | Optional | Username (nếu nhà mạng yêu cầu) |
| `pass` | string | Optional | Password (nếu nhà mạng yêu cầu) |

**Các APN phổ biến tại Việt Nam:**

| Nhà mạng | APN | User | Pass |
|----------|-----|------|------|
| Viettel | `v-internet` | - | - |
| Vinaphone | `m3-world` | `mms` | `mms` |
| Mobifone | `m-wap` | `mms` | `mms` |
| Vietnamobile | `internet` | - | - |

**Success Response:**
```json
{
  "success": true,
  "message": "Cellular mode configured. Exiting AP mode...",
  "connectionType": "cellular",
  "apn": "internet"
}
```

**Error Response:**
```json
{
  "success": false,
  "error": "APN is required for cellular connection"
}
```

**Hành vi sau khi gọi API:**
1. ESP32 lưu cấu hình Cellular vào NVS
2. Tắt Access Point mode
3. **KHÔNG** kết nối WiFi
4. Gọi callback `onConfigReceived` để firmware xử lý 4G module

---

### 10. POST /api/reset - Factory Reset

Xóa toàn bộ cấu hình và restart thiết bị.

**Request:**
```http
POST /api/reset HTTP/1.1
Host: 192.168.4.1
```

**Response:**
```json
{
  "success": true,
  "message": "Factory reset in progress..."
}
```

---

## Workflow điển hình

### Workflow 1: Kết nối WiFi (DHCP)

```
1. Giữ nút IO-0 trong 3 giây → ESP phát WiFi "ZENO-XXXXXXXX"
2. Kết nối điện thoại vào WiFi "ZENO-XXXXXXXX" (pass: zenopcb12345)
3. GET /api/info → Kiểm tra supportedConnections có "wifi"
4. GET /api/networks → Lấy danh sách WiFi
5. POST /api/verify → Kiểm tra credentials (optional)
6. POST /api/connect/wifi → Kết nối với DHCP
   {
     "wifiSSID": "MyWiFi",
     "wifiPassword": "password123"
   }
7. Thiết bị kết nối WiFi và tắt AP mode
```

### Workflow 2: Kết nối WiFi (Static IP)

```
1. Vào AP mode
2. GET /api/info → Kiểm tra supportedConnections
3. GET /api/networks → Lấy danh sách WiFi
4. POST /api/connect/wifi
   {
     "wifiSSID": "MyWiFi",
     "wifiPassword": "password123",
     "dhcp": false,
     "ip": "192.168.1.100",
     "gateway": "192.168.1.1",
     "subnet": "255.255.255.0",
     "dns": "8.8.8.8"
   }
```

### Workflow 3: Kết nối Ethernet

```
1. Vào AP mode
2. GET /api/info → Kiểm tra supportedConnections có "ethernet"
3. POST /api/connect/ethernet
   // DHCP:
   { "dhcp": true }
   
   // hoặc Static IP:
   {
     "dhcp": false,
     "ip": "192.168.1.100",
     "gateway": "192.168.1.1",
     "subnet": "255.255.255.0"
   }
4. Thiết bị tắt AP mode, firmware xử lý Ethernet
```

### Workflow 4: Kết nối 4G/Cellular

```
1. Vào AP mode
2. GET /api/info → Kiểm tra supportedConnections có "cellular"
3. POST /api/connect/cellular
   {
     "apn": "v-internet"
   }
4. Thiết bị tắt AP mode, firmware xử lý 4G module
```

---

## Code Examples

### JavaScript (Fetch API)

```javascript
const BASE_URL = 'http://192.168.4.1';

// Lấy thông tin thiết bị
async function getDeviceInfo() {
  const response = await fetch(`${BASE_URL}/api/info`);
  const info = await response.json();
  
  // Kiểm tra loại kết nối được hỗ trợ
  console.log('Supported:', info.supportedConnections);
  return info;
}

// Kết nối WiFi với DHCP
async function connectWiFiDHCP(ssid, password) {
  const response = await fetch(`${BASE_URL}/api/connect/wifi`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      wifiSSID: ssid,
      wifiPassword: password,
      dhcp: true
    }),
  });
  return response.json();
}

// Kết nối WiFi với Static IP
async function connectWiFiStatic(ssid, password, ip, gateway, subnet, dns) {
  const response = await fetch(`${BASE_URL}/api/connect/wifi`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      wifiSSID: ssid,
      wifiPassword: password,
      dhcp: false,
      ip: ip,
      gateway: gateway,
      subnet: subnet,
      dns: dns
    }),
  });
  return response.json();
}

// Kết nối Ethernet
async function connectEthernet(dhcp, ip, gateway, subnet, dns) {
  const body = { dhcp };
  if (!dhcp) {
    body.ip = ip;
    body.gateway = gateway;
    body.subnet = subnet;
    body.dns = dns;
  }
  
  const response = await fetch(`${BASE_URL}/api/connect/ethernet`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
  return response.json();
}

// Kết nối 4G/Cellular
async function connectCellular(apn, user = '', pass = '') {
  const response = await fetch(`${BASE_URL}/api/connect/cellular`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ apn, user, pass }),
  });
  return response.json();
}

// Sử dụng
async function setupDevice() {
  const info = await getDeviceInfo();
  
  // Kiểm tra thiết bị hỗ trợ loại kết nối nào
  if (info.supportedConnections.includes('wifi')) {
    // Kết nối WiFi với DHCP
    await connectWiFiDHCP('MyWiFi', 'password123');
  } else if (info.supportedConnections.includes('ethernet')) {
    // Kết nối Ethernet với DHCP
    await connectEthernet(true);
  } else if (info.supportedConnections.includes('cellular')) {
    // Kết nối 4G
    await connectCellular('v-internet');
  }
}
```

### Dart (Flutter)

```dart
import 'dart:convert';
import 'package:http/http.dart' as http;

class ZenoPCBProvisioning {
  static const String baseUrl = 'http://192.168.4.1';

  Future<Map<String, dynamic>> getDeviceInfo() async {
    final response = await http.get(Uri.parse('$baseUrl/api/info'));
    return json.decode(response.body);
  }

  /// Connect WiFi with DHCP or Static IP
  Future<Map<String, dynamic>> connectWiFi({
    required String ssid,
    String? password,
    bool dhcp = true,
    String? ip,
    String? gateway,
    String? subnet,
    String? dns,
  }) async {
    final body = {
      'wifiSSID': ssid,
      if (password != null) 'wifiPassword': password,
      'dhcp': dhcp,
      if (!dhcp && ip != null) 'ip': ip,
      if (!dhcp && gateway != null) 'gateway': gateway,
      if (!dhcp && subnet != null) 'subnet': subnet,
      if (dns != null) 'dns': dns,
    };

    final response = await http.post(
      Uri.parse('$baseUrl/api/connect/wifi'),
      headers: {'Content-Type': 'application/json'},
      body: json.encode(body),
    );
    return json.decode(response.body);
  }

  /// Connect Ethernet with DHCP or Static IP
  Future<Map<String, dynamic>> connectEthernet({
    bool dhcp = true,
    String? ip,
    String? gateway,
    String? subnet,
    String? dns,
  }) async {
    final body = {
      'dhcp': dhcp,
      if (!dhcp && ip != null) 'ip': ip,
      if (!dhcp && gateway != null) 'gateway': gateway,
      if (!dhcp && subnet != null) 'subnet': subnet,
      if (dns != null) 'dns': dns,
    };

    final response = await http.post(
      Uri.parse('$baseUrl/api/connect/ethernet'),
      headers: {'Content-Type': 'application/json'},
      body: json.encode(body),
    );
    return json.decode(response.body);
  }

  /// Connect 4G/Cellular
  Future<Map<String, dynamic>> connectCellular({
    required String apn,
    String? user,
    String? pass,
  }) async {
    final response = await http.post(
      Uri.parse('$baseUrl/api/connect/cellular'),
      headers: {'Content-Type': 'application/json'},
      body: json.encode({
        'apn': apn,
        if (user != null) 'user': user,
        if (pass != null) 'pass': pass,
      }),
    );
    return json.decode(response.body);
  }
}
```

### cURL Commands

```bash
# Lấy thông tin thiết bị
curl http://192.168.4.1/api/info

# Quét mạng WiFi
curl http://192.168.4.1/api/networks

# ===== KẾT NỐI WIFI =====

# WiFi với DHCP (mặc định)
curl -X POST http://192.168.4.1/api/connect/wifi \
  -H "Content-Type: application/json" \
  -d '{"wifiSSID":"MyWiFi","wifiPassword":"password123"}'

# WiFi với Static IP
curl -X POST http://192.168.4.1/api/connect/wifi \
  -H "Content-Type: application/json" \
  -d '{"wifiSSID":"MyWiFi","wifiPassword":"password123","dhcp":false,"ip":"192.168.1.100","gateway":"192.168.1.1","subnet":"255.255.255.0","dns":"8.8.8.8"}'

# ===== KẾT NỐI ETHERNET =====

# Ethernet với DHCP
curl -X POST http://192.168.4.1/api/connect/ethernet \
  -H "Content-Type: application/json" \
  -d '{"dhcp":true}'

# Ethernet với Static IP
curl -X POST http://192.168.4.1/api/connect/ethernet \
  -H "Content-Type: application/json" \
  -d '{"dhcp":false,"ip":"192.168.1.100","gateway":"192.168.1.1","subnet":"255.255.255.0","dns":"8.8.8.8"}'

# ===== KẾT NỐI 4G/CELLULAR =====

# 4G Viettel
curl -X POST http://192.168.4.1/api/connect/cellular \
  -H "Content-Type: application/json" \
  -d '{"apn":"v-internet"}'

# 4G Vinaphone
curl -X POST http://192.168.4.1/api/connect/cellular \
  -H "Content-Type: application/json" \
  -d '{"apn":"m3-world","user":"mms","pass":"mms"}'

# Factory Reset
curl -X POST http://192.168.4.1/api/reset
```

---

## HTTP Status Codes

| Code | Meaning |
|------|---------|
| 200 | Success |
| 400 | Bad Request - Invalid input |
| 404 | Not Found - Endpoint không tồn tại |
| 500 | Internal Server Error - Lỗi server |

---

## Lưu ý quan trọng

1. **Timeout AP Mode**: AP mode sẽ tự động tắt sau 5 phút nếu không có hoạt động
2. **Password AP**: Password mặc định là `zenopcb12345`, có thể thay đổi trong code
3. **NVS Storage**: Cấu hình được lưu trong NVS và giữ nguyên sau khi restart
4. **supportedConnections**: Kiểm tra trước khi hiển thị UI cho user
5. **DHCP vs Static IP**: 
   - WiFi và Ethernet hỗ trợ cả DHCP và Static IP
   - 4G/Cellular **không hỗ trợ** Static IP (IP do nhà mạng cấp)
6. **Verify trước khi connect**: Luôn gọi `/api/verify` trước `/api/connect/wifi` để đảm bảo credentials đúng

---

## So sánh các loại kết nối

| Tính năng | WiFi | Ethernet | 4G/Cellular |
|-----------|------|----------|-------------|
| DHCP | ✅ | ✅ | ✅ (tự động) |
| Static IP | ✅ | ✅ | ❌ |
| Cần password | ✅ (WiFi password) | ❌ | Optional (APN) |
| Cần hardware thêm | ❌ | ✅ (Ethernet module) | ✅ (4G module) |
| API endpoint | `/api/connect/wifi` | `/api/connect/ethernet` | `/api/connect/cellular` |

---

## Changelog

### v1.3.0
- ⭐ **BREAKING**: Tách `/api/connect` thành 3 endpoints riêng biệt:
  - `POST /api/connect/wifi` - Kết nối WiFi (DHCP/Static IP)
  - `POST /api/connect/ethernet` - Kết nối Ethernet (DHCP/Static IP)
  - `POST /api/connect/cellular` - Kết nối 4G/Cellular (chỉ APN)
- ⭐ **NEW**: WiFi hỗ trợ **Static IP** (ip, gateway, subnet, dns)
- ⭐ **NEW**: Ethernet hỗ trợ **DHCP/Static IP**
- ⭐ **NEW**: Cellular chỉ cần APN config (không có Static IP vì nhà mạng cấp)
- Cập nhật documentation và code examples

### v1.2.0
- Thêm `supportedConnections` array trong `/api/info` response
- Thêm `connectionType` trong `/api/config`
- Hỗ trợ cấu hình Ethernet và Cellular

### v1.1.0
- Thêm endpoint `POST /api/verify` để kiểm tra WiFi credentials
- Filter duplicate SSID, giới hạn 20 networks
- Thêm Device Info (deviceType, deviceModel, firmwareVersion, manufacturer)

### v1.0.0
- Initial release
- RESTful API only (no HTML interface)
- Support for WiFi scanning, configuration, and connection
