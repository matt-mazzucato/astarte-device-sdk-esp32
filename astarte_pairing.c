/*
 * (C) Copyright 2018, Ispirata Srl, info@ispirata.com.
 *
 * SPDX-License-Identifier: LGPL-2.1+ OR Apache-2.0
 */

#include "astarte_pairing.h"

#include <esp_http_client.h>
#include <esp_log.h>

#include <cJSON.h>

#include <string.h>

#define MAX_URL_LENGTH 512
#define MAX_HEADER_LENGTH 1024

#define TAG "ASTARTE_PAIRING"

static esp_err_t http_event_handler(esp_http_client_event_t *evt);
static const char *extract_credentials_secret(cJSON *response);

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA: {
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) {
                ESP_LOGD(TAG, "Got response: %.*s", evt->data_len, (char *) evt->data);
                cJSON **resp_json = (cJSON **) evt->user_data;
                *resp_json = (void *) cJSON_Parse(evt->data);
            }

            break;
        }
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
    }

    return ESP_OK;
}

astarte_err_t astarte_pairing_register_device(const char *base_url, const char *jwt, const char *realm, const char *hw_id)
{
    char url[MAX_URL_LENGTH];
    snprintf(url, MAX_URL_LENGTH, "%s/v1/%s/agent/devices", base_url, realm);

    cJSON *resp = NULL;
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .method = HTTP_METHOD_POST,
        .buffer_size = 2048,
        .user_data = &resp,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    cJSON *root = cJSON_CreateObject();
    cJSON *data = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "data", data);
    cJSON_AddStringToObject(data, "hw_id", hw_id);
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    ESP_ERROR_CHECK(esp_http_client_set_post_field(client, payload, strlen(payload)));

    char auth_header[MAX_HEADER_LENGTH];
    snprintf(auth_header, MAX_HEADER_LENGTH, "Bearer %s", jwt);
    ESP_ERROR_CHECK(esp_http_client_set_header(client, "Authorization", auth_header));
    ESP_ERROR_CHECK(esp_http_client_set_header(client, "Content-Type", "application/json"));

    esp_err_t err = esp_http_client_perform(client);

    astarte_err_t ret = ASTARTE_ERR;
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %d",
                 status_code, esp_http_client_get_content_length(client));
        if (status_code == 201) {
            const char *credentials_secret = extract_credentials_secret(resp);
            ESP_LOGI(TAG, "credentials_secret is %s", credentials_secret);
            ret = ASTARTE_OK;
        }
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    free(payload);
    cJSON_Delete(resp);
    esp_http_client_cleanup(client);

    return ret;
}

static const char *extract_credentials_secret(cJSON *response)
{
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(response, "data");
    const cJSON *credentials_secret = cJSON_GetObjectItemCaseSensitive(data, "credentials_secret");

    if (cJSON_IsString(credentials_secret)) {
        return credentials_secret->valuestring;
    } else {
        ESP_LOGE(TAG, "Error parsing credentials_secret");
        return NULL;
    }
}
