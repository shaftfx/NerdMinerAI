#include "ota_manager.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <mbedtls/sha256.h>
#include <ArduinoJson.h>
#include "semver.h"
#include "version.h"

// esp_crt_bundle covers GitHub's DigiCert CA chain (available in espressif32 >= 5.x)
#include "esp_crt_bundle.h"

volatile bool g_ota_in_progress = false;

#define OTA_API_URL \
    "https://api.github.com/repos/" OTA_GITHUB_OWNER "/" OTA_GITHUB_REPO "/releases/latest"

// Expected asset names in a release:
//   NerdMinerAI-{BOARD_NAME}-v2.0.1.bin
//   NerdMinerAI-{BOARD_NAME}-v2.0.1.bin.sha256
#define OTA_ASSET_PREFIX  "NerdMinerAI-" BOARD_NAME "-"
#define OTA_ASSET_BIN_SFX ".bin"
#define OTA_ASSET_SHA_SFX ".bin.sha256"

static bool hex_to_bytes(const char* hex, uint8_t* out, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char hi = hex[i * 2], lo = hex[i * 2 + 1];
        if (!isxdigit(hi) || !isxdigit(lo)) return false;
        auto nibble = [](char c) -> uint8_t {
            return (c >= '0' && c <= '9') ? c - '0' :
                   (c >= 'a' && c <= 'f') ? c - 'a' + 10 : c - 'A' + 10;
        };
        out[i] = (nibble(hi) << 4) | nibble(lo);
    }
    return true;
}

// Fetch the SHA256 sidecar file and parse the 64-hex-char hash.
static bool fetch_sha256(const String& sha_url, uint8_t expected[32]) {
    WiFiClientSecure client;
    client.setCACertBundle(arduino_esp_crt_bundle_attach);
    HTTPClient http;
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    if (!http.begin(client, sha_url)) return false;
    int code = http.GET();
    if (code != HTTP_CODE_OK) { http.end(); return false; }
    String body = http.getString();
    http.end();
    body.trim();
    if (body.length() < 64) return false;
    return hex_to_bytes(body.c_str(), expected, 32);
}

// Stream-download .bin to OTA partition, verify SHA256 in flight.
static bool flash_binary(const String& bin_url, const uint8_t expected_sha[32]) {
    const esp_partition_t* target = esp_ota_get_next_update_partition(NULL);
    if (!target) {
        Serial.println("[OTA] No OTA partition — single-app build, skip");
        return false;
    }

    // Quiesce mining workers
    g_ota_in_progress = true;
    vTaskDelay(500 / portTICK_PERIOD_MS);

    esp_ota_handle_t handle;
    if (esp_ota_begin(target, OTA_SIZE_UNKNOWN, &handle) != ESP_OK) {
        Serial.println("[OTA] esp_ota_begin failed");
        g_ota_in_progress = false;
        return false;
    }

    WiFiClientSecure client;
    client.setCACertBundle(arduino_esp_crt_bundle_attach);
    HTTPClient http;
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.setTimeout(60000);
    if (!http.begin(client, bin_url)) {
        esp_ota_abort(handle);
        g_ota_in_progress = false;
        return false;
    }
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[OTA] Download failed HTTP %d\n", code);
        http.end(); esp_ota_abort(handle);
        g_ota_in_progress = false;
        return false;
    }

    mbedtls_sha256_context sha_ctx;
    mbedtls_sha256_init(&sha_ctx);
    mbedtls_sha256_starts(&sha_ctx, 0);

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buf[1024];
    int total = http.getSize();
    int written = 0;
    bool ok = true;

    while (http.connected() && (total < 0 || written < total)) {
        int avail = stream->available();
        if (avail > 0) {
            int n = stream->readBytes(buf, min(avail, (int)sizeof(buf)));
            if (n <= 0) { ok = false; break; }
            if (esp_ota_write(handle, buf, n) != ESP_OK) { ok = false; break; }
            mbedtls_sha256_update(&sha_ctx, buf, n);
            written += n;
        } else {
            vTaskDelay(1 / portTICK_PERIOD_MS);
        }
    }
    http.end();

    uint8_t computed[32];
    mbedtls_sha256_finish(&sha_ctx, computed);
    mbedtls_sha256_free(&sha_ctx);

    if (!ok || memcmp(computed, expected_sha, 32) != 0) {
        Serial.println("[OTA] SHA256 mismatch — aborting");
        esp_ota_abort(handle);
        g_ota_in_progress = false;
        return false;
    }
    if (esp_ota_end(handle) != ESP_OK) {
        Serial.println("[OTA] esp_ota_end failed");
        g_ota_in_progress = false;
        return false;
    }
    if (esp_ota_set_boot_partition(target) != ESP_OK) {
        Serial.println("[OTA] esp_ota_set_boot_partition failed");
        g_ota_in_progress = false;
        return false;
    }

    Serial.printf("[OTA] Flash complete (%d bytes). Rebooting.\n", written);
    vTaskDelay(200 / portTICK_PERIOD_MS);
    esp_restart();
    return true; // unreachable
}

// Check GitHub for a newer release and, if found, update.
static void ota_check_and_update() {
    Serial.println("[OTA] Checking for updates...");

    // --- 1. Fetch release metadata ---
    WiFiClientSecure client;
    client.setCACertBundle(arduino_esp_crt_bundle_attach);
    HTTPClient http;
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.setTimeout(15000);
    if (!http.begin(client, OTA_API_URL)) return;

    // Filter to only the fields we need — keeps RAM usage low.
    StaticJsonDocument<128> filter;
    filter["tag_name"] = true;
    JsonObject af = filter["assets"].createNestedObject();
    af["name"] = true;
    af["browser_download_url"] = true;

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[OTA] API fetch failed HTTP %d\n", code);
        http.end(); return;
    }

    DynamicJsonDocument doc(6144);
    DeserializationError err = deserializeJson(doc, *http.getStreamPtr(),
                                               DeserializationOption::Filter(filter));
    http.end();

    if (err) { Serial.printf("[OTA] JSON parse error: %s\n", err.c_str()); return; }

    // --- 2. Version compare ---
    const char* tag = doc["tag_name"] | "";
    SemVer latest  = parseSemVer(String(tag));
    SemVer current = parseSemVer(String(CURRENT_VERSION));

    if (!semverNewerThan(latest, current)) {
        Serial.printf("[OTA] Up to date (%s)\n", CURRENT_VERSION);
        return;
    }
    Serial.printf("[OTA] New version available: %s -> %s\n", CURRENT_VERSION, tag);

    // --- 3. Find asset URLs for this board ---
    String prefix  = String(OTA_ASSET_PREFIX) + tag;  // e.g. "NerdMinerAI-NerdminerV2-v2.0.1"
    String bin_url, sha_url;

    JsonArray assets = doc["assets"].as<JsonArray>();
    for (JsonObject asset : assets) {
        String name = asset["name"] | "";
        String url  = asset["browser_download_url"] | "";
        if (name.endsWith(OTA_ASSET_SHA_SFX) && name.startsWith(OTA_ASSET_PREFIX))
            sha_url = url;
        else if (name.endsWith(OTA_ASSET_BIN_SFX) && name.startsWith(OTA_ASSET_PREFIX))
            bin_url = url;
    }

    if (bin_url.isEmpty() || sha_url.isEmpty()) {
        Serial.printf("[OTA] No asset for board=" BOARD_NAME " in release %s\n", tag);
        return;
    }

    // --- 4. Fetch and verify SHA256 sidecar ---
    uint8_t expected_sha[32];
    if (!fetch_sha256(sha_url, expected_sha)) {
        Serial.println("[OTA] Failed to fetch SHA256 sidecar");
        return;
    }
    Serial.println("[OTA] SHA256 sidecar verified, downloading firmware...");

    // --- 5. Flash (diverges: esp_restart() on success) ---
    flash_binary(bin_url, expected_sha);
}

void ota_validate_on_boot() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) != ESP_OK) return;

    // If we booted into a freshly-flashed image that hasn't been validated yet,
    // confirm it's valid now that WiFi + pool will come up normally.
    // We call mark_valid here; if the device crashed before reaching this point,
    // esp-idf's rollback (when enabled) reverts automatically.
    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
        Serial.println("[OTA] Confirming new firmware valid");
        esp_ota_mark_app_valid_cancel_rollback();
    }
}

void runOTATask(void* params) {
    // Give the miner 60 s to connect and stabilize before first OTA check.
    vTaskDelay(OTA_BOOT_DELAY_MS / portTICK_PERIOD_MS);

    for (;;) {
        if (WiFi.status() == WL_CONNECTED) {
            ota_check_and_update();
        }
        vTaskDelay(OTA_CHECK_INTERVAL_MS / portTICK_PERIOD_MS);
    }
}
