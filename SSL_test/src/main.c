#include "driver/gpio.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_tls.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h> // struct addrinfo
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static const char *payload = "Message from ESP32 ";
static const char *secured_payload = "SSL Message from ESP32 ";

#define EXAMPLE_ESP_WIFI_SSID "<YOUR-WIFI>"
#define EXAMPLE_ESP_WIFI_PASS "<YOUR-PASSWORD>"
#define EXAMPLE_ESP_MAXIMUM_RETRY 10
// ip_address:port of your pc with listening server
#define SERVER_IP_ADDR "<YOUR-IP>"
#define SERVER_PORT 8090
#define SERVER_PORT_SSL 8080

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static const char *TAG = "domino tls example";

static int s_retry_num = 0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP fail");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    // ESP_ERROR_CHECK(esp_netif_init());

    // ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,

            .pmf_cfg = {
                .capable = true,
                .required = false},
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    }
    else
    {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    /* The event will not be processed after unregister */
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(s_wifi_event_group);
}

static void tls_send_task(void *)
{
    // tls library will not check CN in the cert
    esp_tls_cfg_t config = {
        .skip_common_name = true};
    while (1)
    {
        esp_tls_t *esp_tls_handle = esp_tls_init();
        ESP_LOGI(TAG, "TLS Socket created, connecting to %s:%d", SERVER_IP_ADDR, SERVER_PORT_SSL);
        int result = esp_tls_conn_new_sync(SERVER_IP_ADDR, 14, SERVER_PORT_SSL, &config, esp_tls_handle);
        ESP_LOGI(TAG, "Result of ssl connection %d", result);
        if (result < 0)
        {
            ESP_LOGE(TAG, "TLS with %s:%d could not be established", SERVER_IP_ADDR, SERVER_PORT_SSL);
            continue;
        }
        ESP_LOGI(TAG, "TLS message is being send.");
        esp_tls_conn_write(esp_tls_handle, secured_payload, strlen(secured_payload));
        result = esp_tls_conn_destroy(esp_tls_handle);
        ESP_LOGI(TAG, "Result of ssl disconnection %d", result);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

static void tcp_send_task(void *)
{
    char host_ip[] = SERVER_IP_ADDR;
    int addr_family = 0;
    int ip_protocol = 0;

    while (1)
    {
        struct sockaddr_in dest_addr;
        inet_pton(AF_INET, host_ip, &dest_addr.sin_addr);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(SERVER_PORT);
        addr_family = AF_INET;
        ip_protocol = IPPROTO_IP;

        int sock = socket(addr_family, SOCK_STREAM, ip_protocol);
        if (sock < 0)
        {
            ESP_LOGE(TAG, "TCP Unable to create socket: errno %d", errno);
            continue;
        }
        ESP_LOGI(TAG, "TCP Socket created, connecting to %s:%d", host_ip, SERVER_PORT);

        int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err != 0)
        {
            ESP_LOGE(TAG, "TCP Socket unable to connect: errno %d", errno);
            continue;
        }
        ESP_LOGI(TAG, "TCP Successfully connected");

        while (1)
        {
            ESP_LOGI(TAG, "TCP message is being send.");
            int err = send(sock, payload, strlen(payload), 0);
            if (err < 0)
            {
                ESP_LOGE(TAG, "TCP Error occurred during sending: errno %d", errno);
                break;
            }
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }

        if (sock != -1)
        {
            ESP_LOGE(TAG, "Shutting down TCP socket and restarting...");
            shutdown(sock, 0);
            close(sock);
        }
    }
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    // esp_log_level_set("esp_netif_handlers", ESP_LOG_VERBOSE);
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // ethernet_init();
    wifi_init_sta();

    xTaskCreate(&tcp_send_task, "tcp_send_task", 8192, NULL, 5, NULL);
    xTaskCreate(&tls_send_task, "tls_send_task", 8192, NULL, 5, NULL);

    while (1)
    {
        ESP_LOGI(TAG, "main checking");
        vTaskDelay(500);
    }
}