#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#ifndef TCP_MSS
#define TCP_MSS 1460
#endif

// Configuração mínima para lwIP
#define NO_SYS 1
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0
#define LWIP_TCP 1
#define LWIP_UDP 1
#define MEM_ALIGNMENT 4
#define MEM_SIZE 32768
#define TCP_SND_BUF (4 * TCP_MSS)
#define REQUEST_BUFFER_SIZE 2048
#define MEM_LIBC_MALLOC 0
#define MEMP_MEM_MALLOC 0
#define TCP_WND (4 * TCP_MSS)
#define MEMP_NUM_PBUF 16
#define PBUF_POOL_SIZE 32               // Ajuste conforme necessário
#define MEMP_NUM_UDP_PCB 4
#define MEMP_NUM_TCP_PCB 4
#define MEMP_NUM_TCP_SEG 16
#define LWIP_IPV4 1
#define LWIP_ICMP 1
#define LWIP_RAW 1
#define LWIP_DHCP 1
#define LWIP_AUTOIP 1
#define LWIP_DNS 1
#define LWIP_HTTPD 1
#define LWIP_HTTPD_SSI              1  // Habilita SSI
#define LWIP_HTTPD_SUPPORT_POST     1  // Habilita suporte a POST, se necessário
#define LWIP_HTTPD_DYNAMIC_HEADERS 1
#define HTTPD_USE_CUSTOM_FSDATA 0
#define LWIP_HTTPD_CGI 0           // Desative CGI para economizar memória
#define LWIP_NETIF_HOSTNAME 1


#endif /* LWIPOPTS_H */
