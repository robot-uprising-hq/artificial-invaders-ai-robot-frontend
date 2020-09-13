// Lifted from the udp server example.

#include "udpserver.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include <sys/param.h>
#include <pb_decode.h>
#include "proto/RobotSystemCommunication.pb.h"

#define PORT 50052

static const char *TAG = "udpsrv";

void udp_server(MotorActionCallback motor_action_cb)
{
    unsigned char rx_buffer[128];
    char addr_str[128];
    int addr_family;
    int ip_protocol;

    while (1) {

#ifdef CONFIG_EXAMPLE_IPV4
        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(PORT);
        addr_family = AF_INET;
        ip_protocol = IPPROTO_IP;
        inet_ntoa_r(dest_addr.sin_addr, addr_str, sizeof(addr_str) - 1);
#else // IPV6
        struct sockaddr_in6 dest_addr;
        bzero(&dest_addr.sin6_addr.un, sizeof(dest_addr.sin6_addr.un));
        dest_addr.sin6_family = AF_INET6;
        dest_addr.sin6_port = htons(PORT);
        addr_family = AF_INET6;
        ip_protocol = IPPROTO_IPV6;
        inet6_ntoa_r(dest_addr.sin6_addr, addr_str, sizeof(addr_str) - 1);
#endif

        int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket created");

        int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err < 0) {
            ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket bound, port %d", PORT);

        while (1) {

            ESP_LOGI(TAG, "Waiting for data");
            struct sockaddr_in6 source_addr; // Large enough for both IPv4 or IPv6
            socklen_t socklen = sizeof(source_addr);
            int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);

            // Error occurred during receiving
            if (len < 0) {
                ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
                break;
            }
                // Data received
            else {
                // Get the sender's ip address as string
                if (source_addr.sin6_family == PF_INET) {
                    inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
                } else if (source_addr.sin6_family == PF_INET6) {
                    inet6_ntoa_r(source_addr.sin6_addr, addr_str, sizeof(addr_str) - 1);
                }

                rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string...
                ESP_LOGI(TAG, "Received %d bytes from %s:", len, addr_str);
                ESP_LOGI(TAG, "%s", rx_buffer);

                {
                    robotsystemcommunication_RobotRequest req =
                            robotsystemcommunication_RobotRequest_init_zero;
                    pb_istream_t stream = pb_istream_from_buffer(rx_buffer, len);

                    bool status;
                    status = pb_decode(&stream, robotsystemcommunication_RobotRequest_fields, &req);

                    if (!status) {
                        ESP_LOGE(TAG, "Decoding failed: %s", PB_GET_ERROR(&stream));
                    } else {
                        ESP_LOGI(TAG, "Got request type: %d", req.which_req);
                        if (req.which_req == robotsystemcommunication_RobotRequest_act_tag) {
                            ESP_LOGI(TAG, "Got action request: %d %d %d", req.req.act.leftMotorAction,
                                    req.req.act.rightMotorAction, req.req.act.actionTimeout);
                            if (motor_action_cb != NULL) {
                                motor_action_cb(req.req.act.leftMotorAction, req.req.act.rightMotorAction,
                                                req.req.act.actionTimeout);
                            }
                        } else if (req.which_req == robotsystemcommunication_RobotRequest_ping_tag) {
                            ESP_LOGI(TAG, "Got ping request");
                        }

                        // TODO: send response
                    }
                }
            }
        }

        if (sock != -1) {
            ESP_LOGE(TAG, "Shutting down socket and restarting...");
            shutdown(sock, 0);
            close(sock);
        }
    }
}