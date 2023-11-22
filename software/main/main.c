/* BSD Socket API Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include <esp_ota_ops.h>
#include "esp_timer.h"

#include "esp_console.h"
#include "cmd_system.h"
#include "cmd_wifi.h"
#include "cmd_nvs.h"


//#include "protocol_examples_common.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "wifi_manager.h"
#include "http_app.h"

#include "led_strip.h"

#include "driver/uart.h"

enum SYS_STATUS_E {

    WIFI_INIT = 0,
    WIFI_LOST,

    WIFI_CONNECTED,
    WIFI_IDLE,

    LOOK4SAT_CONNECTED,
    LOOK4SAT_IDLE,
    LOOK4SAT_XFER,
};

static uint32_t sys_status = WIFI_INIT;

int64_t last_xfer_systime = 0;
int64_t curr_systime = 0;

#define UART1_TXD    (0)
#define UART1_RXD    (1)
#define UART1_RTS    (UART_PIN_NO_CHANGE)
#define UART1_CTS    (UART_PIN_NO_CHANGE)
#define UART1_PORT_NUM      (1)
#define UART1_BAUD_RATE     (115200)

#define SERVER_PORT                 4533
#define KEEPALIVE_IDLE              5
#define KEEPALIVE_INTERVAL          5
#define KEEPALIVE_COUNT             3

#define ROTATOR_IP  "192.168.123.163"

//#define ROTATOR_CTRL_VIA_TELNET
//#define ROTATOR_CTRL_VIA_HTTP
#define ROTATOR_CTRL_VIA_UART

#define WEB_SERVER          ROTATOR_IP
#define ROTATOR_WEB_PORT    "80"
#define WEB_PATH_RESTART    "/command?commandText=[ESP444]RESTART"
#define WEB_PATH_PREFIX     "/command?commandText="


#define ROTATOR_TELNET_PORT (23)

#define TOLERANCE_DEFAULT  (5)

uint32_t TOLERANCE = TOLERANCE_DEFAULT;

#define RSIZE (1024)
// 42-gear-22
#define XDEGREE2MM(d)   ((d)/9.0)
#define YDEGREE2MM(d)   ((d)/9.0)

char *grbl_get_config = "$$\r\n";
char *grbl_get_pos = "?\r\n";

char *grbl_init_list[] =
{
    "$1=255\r\n",     /* lock motors */
    "$3=3\r\n",       /* invert X and Y direction */
    //"$100=80\r\n",    /* axis-x a4988   16 microstep */
    "$100=1776\r\n",  /* axis-x a4988   16 microstep 42-gear-motor-22-(1710/77) (80*1710)/77 */
    //"$101=160\n", /* axis-y drv8825 32 microstep */
    //"$101=80\n",  /* axis-y a4988   16 microstep */
    "$101=1776\r\n",  /* axis-y a4988   16 microstep 42-gear-motor-22-(1710/77) (80*1710)/77 */
    "$110=50\r\n",
    "$111=50\r\n",
    "$120=25\r\n",
    "$121=25\r\n",
    "G90\r\n",
    "G0 X0 Y0\r\n",
};

struct rot_req {
    float az;
    float el;
};

QueueHandle_t rot_req_queue;

static const char *TAG = "main";

static void look4sat_handler(const int sock)
{
    int len;
    char rx_buffer[128];

    struct rot_req *pr;

    float prev_az = 0, prev_el = 0;
    float curr_az = 0, curr_el = 0;

    float delta_az, delta_el;

    do {

        //sys_status = LOOK4SAT_IDLE;

        //curr_systime = esp_timer_get_time();
        //ESP_LOGE(TAG, "recv start");

        len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
        if (len < 0) {
            ESP_LOGE(TAG, "Error occurred during receiving: errno %d, reset rotator", errno);
            sys_status = WIFI_IDLE;
            pr = (struct rot_req *)malloc(sizeof(struct rot_req));
            pr->az = 0;
            pr->el = 0;
            xQueueSend(rot_req_queue, &pr, 0);
            prev_az = 0;
            prev_el = 0;

        } else if (len == 0) {
            ESP_LOGW(TAG, "Connection closed, reset rotator");
            sys_status = WIFI_IDLE;
            pr = (struct rot_req *)malloc(sizeof(struct rot_req));
            pr->az = 0;
            pr->el = 0;
            xQueueSend(rot_req_queue, &pr, 0);
            prev_az = 0;
            prev_el = 0;

        } else {
            rx_buffer[len] = 0; // Null-terminate whatever is received and treat it like a string
            ESP_LOGI(TAG, "Received %d bytes: [%s]", len, rx_buffer);

            sys_status = LOOK4SAT_XFER;

            last_xfer_systime = esp_timer_get_time();

            sscanf(rx_buffer, "\\set_pos %f %f", &curr_az, &curr_el);


            delta_az = curr_az > prev_az ? (curr_az - prev_az) : (prev_az - curr_az);
            delta_el = curr_el > prev_el ? (curr_el - prev_el) : (prev_el - curr_el);

            ESP_LOGI(TAG, "prev_az:  %.3f; prev_el:  %.3f", prev_az,  prev_el);
            ESP_LOGI(TAG, "curr_az:  %.3f; curr_el:  %.3f", curr_az,  curr_el);
            ESP_LOGI(TAG, "delta_az: %.3f; delta_el: %.3f", delta_az, delta_el);

#if 1
            if (curr_el >= 0 && prev_el >= 0) {
                if (delta_az >= TOLERANCE || delta_el >= TOLERANCE) {
                    ESP_LOGI(TAG, "send request to rot_req_queue");
                    pr = (struct rot_req *)malloc(sizeof(struct rot_req));
                    pr->az = curr_az;
                    pr->el = curr_el;
                    xQueueSend(rot_req_queue, &pr, 0);
                    prev_az = curr_az;
                    prev_el = curr_el;
                }

            }
#endif

#if 0
            // send() can return less bytes than supplied length.
            // Walk-around for robust implementation.
            int to_write = len;
            while (to_write > 0) {
                int written = send(sock, rx_buffer + (len - to_write), to_write, 0);
                if (written < 0) {
                    ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                }
                to_write -= written;
            }
#endif
        }
    } while (len > 0);
}

static void tcp_server_task(void *pvParameters)
{
    char addr_str[128];
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    int keepAlive = 1;
    int keepIdle = KEEPALIVE_IDLE;
    int keepInterval = KEEPALIVE_INTERVAL;
    int keepCount = KEEPALIVE_COUNT;
    struct sockaddr_storage dest_addr;

    ESP_LOGE(TAG, "%s-%d", __func__, __LINE__);

    if (addr_family == AF_INET) {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(SERVER_PORT);
        ip_protocol = IPPROTO_IP;
    }
#ifdef CONFIG_EXAMPLE_IPV6
    else if (addr_family == AF_INET6) {
        struct sockaddr_in6 *dest_addr_ip6 = (struct sockaddr_in6 *)&dest_addr;
        bzero(&dest_addr_ip6->sin6_addr.un, sizeof(dest_addr_ip6->sin6_addr.un));
        dest_addr_ip6->sin6_family = AF_INET6;
        dest_addr_ip6->sin6_port = htons(SERVER_PORT);
        ip_protocol = IPPROTO_IPV6;
    }
#endif

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#if defined(CONFIG_EXAMPLE_IPV4) && defined(CONFIG_EXAMPLE_IPV6)
    // Note that by default IPV6 binds to both protocols, it is must be disabled
    // if both protocols used at the same time (used in CI)
    setsockopt(listen_sock, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
#endif

    ESP_LOGI(TAG, "Socket created");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAG, "IPPROTO: %d", addr_family);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", SERVER_PORT);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        goto CLEAN_UP;
    }

    while (1) {

        ESP_LOGI(TAG, "Socket listening");

        struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        socklen_t addr_len = sizeof(source_addr);
        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }

        // Set tcp keepalive option
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));
        // Convert ip address to string
        if (source_addr.ss_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
        }
#ifdef CONFIG_EXAMPLE_IPV6
        else if (source_addr.ss_family == PF_INET6) {
            inet6_ntoa_r(((struct sockaddr_in6 *)&source_addr)->sin6_addr, addr_str, sizeof(addr_str) - 1);
        }
#endif
        ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);
        sys_status = LOOK4SAT_CONNECTED;

        look4sat_handler(sock);

        shutdown(sock, 0);
        close(sock);
    }

CLEAN_UP:
    close(listen_sock);
    vTaskDelete(NULL);
}

int tcp_client_init(char *ip, uint16_t port)
{

    char addr_str[128];
    int addr_family;
    int ip_protocol;
    int sockfd = -1;
    int val = 1;
    int err;
    struct sockaddr_in dest_addr;

    dest_addr.sin_addr.s_addr = inet_addr(ip);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
    addr_family = AF_INET;
    ip_protocol = IPPROTO_IP;
    inet_ntoa_r(dest_addr.sin_addr, addr_str, sizeof(addr_str) - 1);

    ESP_LOGI(TAG, "%s start\r\n", __func__);

    sockfd = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (sockfd < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        goto error;
    }

    ESP_LOGI(TAG, "Socket created, connecting to %s:%d", ip, port);

    err = connect(sockfd, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
        goto error;
    }


    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char *)&val, sizeof(val));

    ESP_LOGI(TAG, "Successfully connected to %d", port);
    return sockfd;

error:
    if (sockfd != -1) {
        ESP_LOGE(TAG, "Shutting down socket and restarting...");
        shutdown(sockfd, 0);
        close(sockfd);
    }
    return -1;
}

int rotator_request_http(int sockfd, char *request, uint32_t req_size, char *response, uint32_t *resp_size)
{

    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    struct in_addr *addr;
    char request_get[256] = {0};
    char recv_buf[64];
    int s, r;

    int err = getaddrinfo(WEB_SERVER, ROTATOR_WEB_PORT, &hints, &res);

    if(err != 0 || res == NULL) {
        ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        return -1;
    }

    /* Code to print the resolved IP.

Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code */
    addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
    ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));

    s = socket(res->ai_family, res->ai_socktype, 0);
    if(s < 0) {
        ESP_LOGE(TAG, "... Failed to allocate socket.");
        freeaddrinfo(res);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        return -1;
    }
    ESP_LOGI(TAG, "... allocated socket");

    if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
        ESP_LOGE(TAG, "... socket connect failed errno=%d", errno);
        close(s);
        freeaddrinfo(res);
        vTaskDelay(4000 / portTICK_PERIOD_MS);
        return -1;
    }

    ESP_LOGI(TAG, "... connected");
    freeaddrinfo(res);

    snprintf(request_get, sizeof(request_get), 
            "GET %s HTTP/1.0\r\nHost: %s:%s\r\nUser-Agent: esp-idf/1.0 esp32\r\n\r\n",
            request, ROTATOR_IP, ROTATOR_WEB_PORT);

    ESP_LOGI(TAG, "request_get: [%s]", request_get);

    if (write(s, request_get, strlen(request_get)) < 0) {

        ESP_LOGE(TAG, "... socket send failed");
        close(s);
        vTaskDelay(4000 / portTICK_PERIOD_MS);
        return -1;
    }
    ESP_LOGI(TAG, "... socket send success");

    struct timeval receiving_timeout;
    receiving_timeout.tv_sec = 5;
    receiving_timeout.tv_usec = 0;
    if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout,
                sizeof(receiving_timeout)) < 0) {
        ESP_LOGE(TAG, "... failed to set socket receiving timeout");
        close(s);
        vTaskDelay(4000 / portTICK_PERIOD_MS);
        return -1;
    }
    ESP_LOGI(TAG, "... set socket receiving timeout success");

    /* Read HTTP response */
    do {
        bzero(recv_buf, sizeof(recv_buf));
        r = read(s, recv_buf, sizeof(recv_buf)-1);
        for(int i = 0; i < r; i++) {
            putchar(recv_buf[i]);
        }
    } while(r > 0);

    ESP_LOGI(TAG, "... done reading from socket. Last read return=%d errno=%d. (%s)", r, errno, strerror(errno));
    close(s);

    return 0;
}

int rotator_request_uart(int sockfd, char *request, uint32_t req_size, char *response, uint32_t *resp_size)
{

    int rlen;

    ESP_LOGE(TAG, "%s write [%s]", __func__, request);

    //uart_write_bytes(UART1_PORT_NUM, "hello\r\n", 7);

    uart_write_bytes(UART1_PORT_NUM, request, req_size);

    rlen = uart_read_bytes(UART1_PORT_NUM, response, 1024, pdMS_TO_TICKS(100)); /* 100ms */
    ESP_LOGE(TAG, "%s read [%s]", __func__, response);

    return 0;
}

int rotator_request_telnet(int sockfd, char *request, uint32_t req_size, char *response, uint32_t *resp_size)
{
    int err, rsize;

    err = send(sockfd, request, req_size, 0);
    if (err < 0) {
        ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
        goto error;
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    rsize = recv(sockfd, response, 1024, 0);
    if (rsize < 0) {
        ESP_LOGE(TAG, "Error occurred during recving: errno %d (%s)", errno, strerror(errno));
        goto error;
    }

    ESP_LOGI(TAG, "req: [%s]; resp: [%s]", request, response);

    *resp_size = rsize;

    return 0;

error:
    return -1;

}


int rotator_init_telnet(int rot_fd)
{
    int i, retval;
    uint32_t init_count;
    char rsp[RSIZE] = {0};
    uint32_t resp_size;

    rotator_request_telnet(rot_fd, grbl_get_config, strlen(grbl_get_config), rsp, &resp_size);

    if (strstr(rsp, grbl_init_list[0]) != NULL)
    {
        ESP_LOGI(TAG, "rotator already configured");
        return 0;
    }

    init_count = sizeof(grbl_init_list) / sizeof(grbl_init_list[0]);

    for (i = 0; i < init_count; i++)
    {
        ESP_LOGI(TAG, "rotator_request [%s] ", grbl_init_list[i]);
        memset(rsp, 0, sizeof(rsp));
        retval = rotator_request_telnet(rot_fd, grbl_init_list[i], strlen(grbl_init_list[i]), rsp,
                &resp_size);

        //fprintf(stderr, "done\n");
        if (retval != 0)
        {
            ESP_LOGE(TAG, "rotator_request [%s] fail\n", grbl_init_list[i]);
            return -1;
        }
    }

    return 0;

}

int rotator_set_position(int rot_fd, float curr_az, float curr_el)
{
    int i;
    int retval;
    static float prev_az, prev_el;

    static float prev_x, curr_x;
    float x[3], delta[3];

    float y;

    char req[RSIZE] = {0};
    char rsp[RSIZE] = {0};
    uint32_t rsp_size;

    float min_value;
    int   min_index;

    /* az:x: 0 - 360 */
    /* el:y: 0 - 90 */
#if 1
    ESP_LOGI(TAG,
              "%s: (prev_x) = (%.3f); (prev_az) = (%.3f); (prev_el) = (%.3f); (curr_az, curr_el) = (%.3f, %.3f)\n",
              __func__, prev_x, prev_az, prev_el, curr_az, curr_el);
#endif

    /* convert degree to mm, 360 degree = 40mm, 1 degree = 0.111mm */
    //x = az * 0.111;
    //y = el * 0.111;

    /* 360 -> 0 */
    if ((prev_az > 270 && prev_az < 360) && (curr_az > 0 && curr_az < 90)) {

        ESP_LOGI(TAG, "%s:%d\n", __func__, __LINE__);

        if (prev_x >= XDEGREE2MM(270))
        {   /* 在顺时针第一圈里，平滑越过第一圈，到顺时针第二圈，如果是顺时针第二圈的时候，还是返回到第一圈内 */
            curr_x = XDEGREE2MM(curr_az) + XDEGREE2MM(360);
        } else {   /* prev_x 也可能为负值(逆时针第一圈或者第二圈)，这个时候重新回到第一圈内 */
            curr_x = XDEGREE2MM(curr_az);
        }

      /* 0 -> 360 */  
    } else if ((prev_az > 0   && prev_az < 90) &&
             (curr_az > 270 && curr_az < 360)) {
        ESP_LOGI(TAG, "%s:%d\n", __func__, __LINE__);


        if (prev_x >= XDEGREE2MM(360)) {
            /* 已经在顺时针第二圈里了，回到第一圈里 */
            curr_x = XDEGREE2MM(curr_az);
        } else {
            /* 在顺时针第一圈里，则直接到逆时针第一圈 */
            /* 或者在逆时针第一圈里，也是直接回到逆时针第一圈里 */
            curr_x = XDEGREE2MM(curr_az) - XDEGREE2MM(360); /* 是个负数 */
        }

        /* reset */
    } else if (curr_az == 0 && curr_el == 0) {
        ESP_LOGI(TAG,  "%s: reset\n", __func__);
        curr_x = 0;
        y = 0;
    } else {
        ESP_LOGI(TAG, "%s-%d prev_x: %.3f\n", __func__, __LINE__, prev_x);

        /* 某个位置可能处于三圈中的某一圈，选择和其最近的一个值 */
        x[0] = XDEGREE2MM(curr_az) - XDEGREE2MM(360);
        x[1] = XDEGREE2MM(curr_az);
        x[2] = XDEGREE2MM(curr_az) + XDEGREE2MM(360);

        delta[0] = prev_x - x[0];
        delta[1] = prev_x - x[1];
        delta[2] = prev_x - x[2];

        /* convert to absolute value, then compare the minimum distance */
        if (delta[0] < 0) { delta[0] = -1 * delta[0]; }

        if (delta[1] < 0) { delta[1] = -1 * delta[1]; }

        if (delta[2] < 0) { delta[2] = -1 * delta[2]; }

        min_value = delta[0];
        min_index = 0;

        for (i = 0; i < 3; i++) {
            if (delta[i] <= min_value)
            {
                min_value = delta[i];
                min_index = i;
            }
        }

        curr_x = x[min_index];
        ESP_LOGI(TAG, "min_index: %d; curr_x: %.3f\n", min_index, curr_x);
    }

    y = YDEGREE2MM(curr_el);
    /**/

#if defined(ROTATOR_CTRL_VIA_TELNET)
    snprintf(req, sizeof(req), "G0 X%.3f Y%.3f\n", curr_x, y);

    retval = rotator_request_telnet(rot_fd, req, strlen(req), rsp, &rsp_size);
#elif defined(ROTATOR_CTRL_VIA_HTTP)
    snprintf(req, sizeof(req), "/command?commandText=G0%%20X%.3f%%20Y%.3f", ROTATOR_IP, curr_x, y);

    retval = rotator_request_http(rot_fd, req, strlen(req), rsp, &rsp_size);
#elif defined(ROTATOR_CTRL_VIA_UART)
    snprintf(req, sizeof(req), "G0 X%.3f Y%.3f\r\n", curr_x, y);

    retval = rotator_request_uart(rot_fd, req, strlen(req), rsp, &rsp_size);
#endif

    if (retval != 0) {
        return retval;
    }

    prev_az = curr_az;
    prev_el = curr_el;

    prev_x  = curr_x;
    return 0;

}

static void rotator_ctrl_task(void *pvParameters)
{
    int rot_fd = 0;
    struct rot_req *pr;

#ifdef ROTATOR_CTRL_VIA_TELNET
    rot_fd = tcp_client_init(ROTATOR_IP, ROTATOR_TELNET_PORT);
    if (rot_fd < 0) {
        ESP_LOGE(TAG, "connect to rotator fail [%s:%d]", ROTATOR_IP, ROTATOR_TELNET_PORT);
    }

    rotator_init_telnet(rot_fd);

#elif defined(ROTATOR_CTRL_VIA_HTTP)
    rot_fd = 0;
#elif defined(ROTATOR_CTRL_VIA_UART)
    rot_fd = 0;
#endif

#if 0
    {
        char req[RSIZE] = {0};
        char rsp[RSIZE] = {0};
        uint32_t rsp_size;

        float x, y;
        x = 0;
        y = 2;

        vTaskDelay(pdMS_TO_TICKS(10000));


        snprintf(req, sizeof(req), "/command?commandText=G0%%20X%.3f%%20Y%.3f", ROTATOR_IP, x, y);

        ESP_LOGE(TAG, "test-[%.3f, %.3f]-[%s]", x, y, req);

        rotator_request_http(rot_fd, req, strlen(req), rsp, &rsp_size);

        vTaskDelay(pdMS_TO_TICKS(10000));

        x = 0;
        y = 0;

        snprintf(req, sizeof(req), "/command?commandText=G0%%20X%.3f%%20Y%.3f", ROTATOR_IP, x, y);
        ESP_LOGE(TAG, "test-[%.3f, %.3f]-[%s]", x, y, req);

        rotator_request_http(rot_fd, req, strlen(req), rsp, &rsp_size);

        while (1) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
#endif


    while (1) {
        if (xQueueReceive(rot_req_queue, &pr, portMAX_DELAY) == pdPASS) {

            ESP_LOGI(TAG, "got request [%.3f, %.3f]", pr->az, pr->el);

            rotator_set_position(rot_fd, pr->az, pr->el);

            free(pr);
        }

    }

}

void cb_connection_ok(void *pvParameter){
    ip_event_got_ip_t* param = (ip_event_got_ip_t*)pvParameter;

    /* transform IP to human readable string */
    char str_ip[16];
    esp_ip4addr_ntoa(&param->ip_info.ip, str_ip, IP4ADDR_STRLEN_MAX);

    ESP_LOGI(TAG, "I have a connection and my IP is %s!", str_ip);

    sys_status = WIFI_CONNECTED;

}

void cb_sta_disconnect(void *pvParameter){

    ESP_LOGI(TAG, "sta disconnected");

    sys_status = WIFI_LOST;
}

extern void ota_task(void *param);

extern const uint8_t config_html_start[] asm("_binary_config_html_start");
extern const uint8_t config_html_end[] asm("_binary_config_html_end");

esp_err_t update_post_handler(httpd_req_t *req)
{
    char buf[1000];
    esp_ota_handle_t ota_handle;
    int remaining = req->content_len;

    const esp_partition_t *ota_partition = esp_ota_get_next_update_partition(NULL);
    ESP_ERROR_CHECK(esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle));

    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));

        // Timeout Error: Just retry
        if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;

        // Serious Error: Abort OTA
        } else if (recv_len <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Protocol Error");
            return ESP_FAIL;
        }

        // Successful Upload: Flash firmware chunk
        if (esp_ota_write(ota_handle, (const void *)buf, recv_len) != ESP_OK) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Flash Error");
            return ESP_FAIL;
        }

        remaining -= recv_len;
    }

    // Validate and switch to new OTA image and reboot
    if (esp_ota_end(ota_handle) != ESP_OK || esp_ota_set_boot_partition(ota_partition) != ESP_OK) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Validation / Activation Error");
            return ESP_FAIL;
    }

    httpd_resp_sendstr(req, "Firmware update complete, rebooting now!\n");

    vTaskDelay(500 / portTICK_PERIOD_MS);

    esp_restart();
    return ESP_OK;
}

static esp_err_t my_get_handler(httpd_req_t *req) {

    /* our custom page sits at /helloworld in this example */
    if(strcmp(req->uri, "/config") == 0){

        ESP_LOGI(TAG, "Serving page /config");

        //const char* response = "<html><body><h1>Hello World!</h1></body></html>";
        const char* response = (char *)config_html_start;

        httpd_resp_set_status(req, "200 OK");
        httpd_resp_set_type(req, "text/html");

        httpd_resp_send(req, response, strlen(response));
    }
    else{
        /* send a 404 otherwise */
        httpd_resp_send_404(req);
    }

    return ESP_OK;
}

static esp_err_t my_post_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "enter %s!", __func__);
    if(strcmp(req->uri, "/update") == 0){
        ESP_LOGI(TAG, "enter %s:%d!", __func__, __LINE__);
        update_post_handler(req);
    } else {
        httpd_resp_send_404(req);
    }

    return ESP_OK;
}

/* ---- LED FUNCTIONS START ---- */

#define BLINK_GPIO (8)   //Test Board & Mass Production Board

static uint8_t s_led_state = 0;
static led_strip_handle_t led_strip;
#define LED_COLOR_RED     (0)
#define LED_COLOR_GREEN   (1)
#define LED_COLOR_BLUE    (2)
#define LED_COLOR_YELLOW  (3)

static int led_init()
{
    /* LED strip initialization with the GPIO and pixels number*/
    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,
        .max_leds = 1, // at least one LED on board
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    /* Set all LED off to clear all pixels */
    led_strip_clear(led_strip);
    return 0;
}

static void led_on(uint32_t color)
{
    switch (color) {
        case (LED_COLOR_RED):              /*  R   G   B */
            led_strip_set_pixel(led_strip, 0, 16,  0,  0);
            break;
        case (LED_COLOR_GREEN):
            led_strip_set_pixel(led_strip, 0,  0, 16,  0);
            break;
        case (LED_COLOR_BLUE):
            led_strip_set_pixel(led_strip, 0,  0,  0, 16);
            break;
        case (LED_COLOR_YELLOW):
            led_strip_set_pixel(led_strip, 0, 16, 16,  0);
            break;

        default:
            break;
    }

    /* Refresh the strip to send data */
    led_strip_refresh(led_strip);
}

static void led_off()
{
    led_strip_clear(led_strip);

    //led_strip_set_pixel(led_strip, 0, 0, 0, 0);
    /* Refresh the strip to send data */
    //led_strip_refresh(led_strip);
}

static void led_toggle(uint32_t color)
{
    static uint8_t b = 0;

    if (b) {
        led_on(color);

    } else {
        led_off();
    }

    b = !b;
}

/* ---- LED FUNCTIONS END ---- */

char req[1024];
char rsp[1024];
uint32_t rsp_size;


#define BUILD_TIME  __TIME__
#define BUILD_DATE  __DATE__
#define VERSION     "v1.0"

static char sys_banner[] = {"look4sat protocol bridge system buildtime [" BUILD_TIME " " BUILD_DATE "] " VERSION};


#define PROMPT_STR CONFIG_IDF_TARGET

static void initialize_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}


static void console_start()
{

    uint8_t data[256];

    ESP_LOGE(TAG, "%s-%d", __func__, __LINE__);

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    /* Prompt to be printed before each line.
     * This can be customized, made dynamic, etc.
     */
    repl_config.prompt = PROMPT_STR ">";
    repl_config.max_cmdline_length = 1024;

    initialize_nvs();

    esp_console_register_help_command();
    register_system_common();

#if SOC_WIFI_SUPPORTED
    register_wifi();
#endif
    register_nvs();

#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
    esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &repl));

#elif defined(CONFIG_ESP_CONSOLE_USB_CDC)
    esp_console_dev_usb_cdc_config_t hw_config = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_cdc(&hw_config, &repl_config, &repl));

#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    esp_console_dev_usb_serial_jtag_config_t hw_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &repl));

#else
#error Unsupported console type
#endif

    ESP_ERROR_CHECK(esp_console_start_repl(repl));

}



int rotator_init()
{
    esp_err_t err;

    nvs_handle_t handle;
    size_t len = 6;
    int ret = 0;

    err = nvs_open("rotator", NVS_READWRITE, &handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!\r\n", esp_err_to_name(err));
        ret = -1;
        goto exit;
    }

    if ((err = nvs_get_u32(handle, "tolerance", &TOLERANCE)) == ESP_OK) {

        ESP_LOGE(TAG, "read tolerance succ: %ld\r\n", TOLERANCE);
    } else {
        ESP_LOGE(TAG, "read tolerance fail: %s\r\n", esp_err_to_name(err));
        ret = -1;
        goto exit;
    }

exit:
    return ret;
}

void app_main(void)
{
#if 0
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());
#endif

    /* start the wifi manager */

    ESP_LOGE(TAG, "%s\r\n", sys_banner);

    led_init();

    wifi_manager_start();

    /* register a callback as an example to how you can integrate your code with the wifi manager */
    wifi_manager_set_callback(WM_EVENT_STA_GOT_IP, &cb_connection_ok);

    wifi_manager_set_callback(WM_EVENT_STA_DISCONNECTED, &cb_sta_disconnect);

    //http_app_set_handler_hook(HTTP_GET,  &my_get_handler);
    //http_app_set_handler_hook(HTTP_POST, &my_post_handler);


#ifdef ROTATOR_CTRL_VIA_UART
    uart_config_t uart_config = {
         .baud_rate = UART1_BAUD_RATE,
         .data_bits = UART_DATA_8_BITS,
         .parity    = UART_PARITY_DISABLE,
         .stop_bits = UART_STOP_BITS_1,
         .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
         .source_clk = UART_SCLK_APB,
     };
     int intr_alloc_flags = 0;

#if CONFIG_UART_ISR_IN_IRAM
    intr_alloc_flags = ESP_INTR_FLAG_IRAM;
#endif


    ESP_ERROR_CHECK(uart_driver_install(UART1_PORT_NUM, 2048, 0, 0, NULL, intr_alloc_flags));
    ESP_ERROR_CHECK(uart_param_config(UART1_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART1_PORT_NUM, UART1_TXD, UART1_RXD, UART1_RTS, UART1_CTS));

    rotator_init();

    //uart_write_bytes(UART1_PORT_NUM, "hello\r\n", 7);

    ESP_LOGE(TAG, "test start");

    /* test */
    snprintf(req, sizeof(req), "$$\r\n");
    ESP_LOGE(TAG, "%s write [%s]", __func__, req);
    rotator_request_uart(0, req, strlen(req), rsp, &rsp_size);
    ESP_LOGE(TAG, "%s read [%s]", __func__, rsp);

    //rotator_set_position(0, 45, 45);

    ESP_LOGE(TAG, "test end");

#endif

 
    //while(1) { vTaskDelay(1000 / portTICK_PERIOD_MS); }

    rot_req_queue = xQueueCreate(256, sizeof(struct rot_req *));

#define CONFIG_EXAMPLE_IPV4

#ifdef CONFIG_EXAMPLE_IPV4
    xTaskCreate(tcp_server_task, "tcp_server", 4096, (void*)AF_INET, 5, NULL);
#endif

#ifdef CONFIG_EXAMPLE_IPV6
    xTaskCreate(tcp_server_task, "tcp_server", 4096, (void*)AF_INET6, 5, NULL);
#endif

    ESP_LOGE(TAG, "%s-%d", __func__, __LINE__);
    xTaskCreate(rotator_ctrl_task, "rotator_ctrl", 8192, NULL, 5, NULL);

    //uint32_t ota_mode = 0;
    //xTaskCreate(ota_task, "ota_task", 4096, &ota_mode, 5, NULL);

    console_start();

    while (1) {

        switch (sys_status) {
            case (WIFI_INIT):
            case (WIFI_LOST):
                led_toggle(LED_COLOR_RED);
                vTaskDelay(pdMS_TO_TICKS(1000));
                break;
            case (WIFI_CONNECTED):
            case (WIFI_IDLE):
                led_on(LED_COLOR_BLUE);
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
            case (LOOK4SAT_CONNECTED):
            case (LOOK4SAT_IDLE):
                led_on(LED_COLOR_GREEN);
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
            case (LOOK4SAT_XFER):
                led_toggle(LED_COLOR_GREEN);
                vTaskDelay(pdMS_TO_TICKS(200));

                curr_systime = esp_timer_get_time();

                /* 500ms auto timeout state transition */
                if ((curr_systime - last_xfer_systime) > 500000) {
                    sys_status = LOOK4SAT_IDLE;
                }

                break;
            default:
                ESP_LOGE(TAG, "unknown sys_status: %ld\r\n", sys_status);
                vTaskDelay(pdMS_TO_TICKS(1000));
                break;
        }


    }

}
