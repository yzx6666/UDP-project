#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h> /* See NOTES */
#include <sys/socket.h>
#include <unistd.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <net/if.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <proto.h>
#include "client.h"

/*
    -M --mgroup 指定多播组
    -P --port   指定接受端口
    -p --player 指定播放器
    -H --help   显示帮助
*/
struct client_conf_st client_conf = {
    .rcvport = DEFAULT_RCVPORT,
    .mgroup = DEFAULT_MGROUP,
    .player_cmd = DEFAULT_PLAYERCMD};

static void printhelp()
{
    printf("-P --port 指定接受端口\n");
    printf("-H --help   显示帮助\n");
    printf("-p --player 指定播放器\n");
    printf("-M --mgroup 指定多播组\n");
}

static int writen(int fd, const void *buf, size_t len)
{
    int ret;
    int pos = 0;
    while (len > 0)
    {
        ret = write(fd, buf + pos, len);
        if (ret < 0)
        {
            if (errno == EINTR)
                continue;
            perror("write");
            return -1;
        }
        len -= ret;
        pos += ret;
    }
    return pos;
}

int main(int argc, char *argv[])
{
    /*  初始化
        级别：默认值， 配置文件， 环境变量， 命令行参数
    */

    // 处理命令行参数
    int index = 0;
    struct option argarr[] = {{"port", 1, NULL, 'P'}, {"mgroup", 1, NULL, 'M'}, {"player", 1, NULL, 'p'}, {"help", 1, NULL, 'H'}, {NULL, 0, NULL, 0}};
    while (1)
    {
        int c = getopt_long(argc, argv, "M:P:p:H", argarr, &index);
        if (c < 0)
            break;
        switch (c)
        {
        case 'P':
            client_conf.rcvport = optarg;
            break;
        case 'p':
            client_conf.player_cmd = optarg;
            break;
        case 'M':
            client_conf.mgroup = optarg;
            break;
        case 'H':
            printhelp();
            exit(0);
            break;

        default:
            abort();
            break;
        }
    }

    // 创建socket
    int sd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sd < 0)
    {
        perror("socket");
        exit(1);
    }

    // 设置多播
    struct ip_mreqn mreq;
    int err = inet_pton(AF_INET, client_conf.mgroup, &mreq.imr_multiaddr.s_addr);
    if (err < 0)
    {
        perror("inet_pton");
        exit(1);
    }
    inet_pton(AF_INET, "0.0.0.0", &mreq.imr_address.s_addr);
    mreq.imr_ifindex = if_nametoindex("ens33");

    if (setsockopt(sd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
    {
        perror("setsockopt");
        exit(1);
    }

    // 提高效率(控制多播流量的环回)
    int val = 1;
    if (setsockopt(sd, IPPROTO_IP, IP_MULTICAST_LOOP, &val, sizeof(val)) < 0)
    {
        perror("setsockopt");
        exit(1);
    }

    //调整udp丢包
    uint64_t receive_buf_size = 40 * 1024 * 1024; // 40 MB
    setsockopt(sd, SOL_SOCKET, SO_RCVBUF, &receive_buf_size, sizeof(receive_buf_size));
    int disable = 1;
    setsockopt(sd, SOL_SOCKET, SO_NO_CHECK, (void *)&disable, sizeof(disable));

    // 绑定本地地址
    struct sockaddr_in laddr;
    laddr.sin_family = AF_INET;
    laddr.sin_port = htons(atoi(client_conf.rcvport));
    inet_pton(AF_INET, "0.0.0.0", &laddr.sin_addr.s_addr);

    if (bind(sd, (void *)&laddr, sizeof(laddr)) < 0)
    {
        perror("bind");
        exit(1);
    }

    // 创建管道
    int pd[2];
    if (pipe(pd) < 0)
    {
        perror("pipe");
        exit(1);
    }

    // 创建子进程
    pid_t pid = fork();
    if (pid < 0)
    {
        perror("fork");
    }
    else if (pid == 0)
    {
        // 子进程调用解码器
        close(sd);
        close(pd[1]);

        dup2(pd[0], 0);
        if (pd[0] > 0)
            close(pd[0]);
        execl("/bin/sh", "sh", "-c", client_conf.player_cmd, NULL); // 播放
        perror("execl");
        exit(1);
    }
    else
    {
        // 父进程：从网络收包，发送给子进程

        // 收节目单
        struct msg_list_st *msg_list;
        msg_list = malloc(MSG_LIST_MAX);
        if (msg_list == NULL)
        {
            perror("malloc");
            exit(1);
        }
        struct sockaddr_in saddr;
        socklen_t slen = sizeof(saddr);
        int len;
        while (1)
        {
            len = recvfrom(sd, msg_list, MSG_LIST_MAX, 0, (void *)&saddr, &slen);
            if (len < sizeof(struct msg_list_st))
            {
                fprintf(stderr, "message is too small\n");
                continue;
            }
            if (msg_list->chnid != LISTCHNID)
            {
                fprintf(stderr, "chnid is not match\n");
                continue;
            }
            break;
        }

        // 打印节目单并且选择频道
        struct msg_listentry_st *pos;
        for (pos = msg_list->entry; (char *)pos < (char *)msg_list + len; pos = (void *)((char *)pos + ntohs(pos->len)))
        {
            printf("channel %d : %s", pos->chnid, pos->desc);
        }

        /*free list*/
        free(msg_list);

        int ret = 0;
        int chosenid;
        while (ret < 1)
        {
            printf("请选择想听的频道");
            ret = scanf("%d", &chosenid);
            if (ret != 1)
                exit(1);
        }

        fprintf(stdout, "chosenid = %d\n", ret);

        // 收频道包，发送给子进程
        struct msg_channel_st *msg_channel;
        msg_channel = malloc(MSG_CHANNEL_MAX);
        if (msg_channel == NULL)
        {
            perror("malloc");
            exit(1);
        }

        struct sockaddr_in raddr;
        socklen_t rlen = sizeof(raddr);
        while (1)
        {
            len = recvfrom(sd, msg_channel, MSG_CHANNEL_MAX, 0, (void *)&raddr, &rlen);
            if (raddr.sin_addr.s_addr != saddr.sin_addr.s_addr || raddr.sin_port != saddr.sin_port)
            {
                fprintf(stderr, "Ignore: address is not match\n");
                continue;
            }
            if (len < sizeof(struct msg_channel_st))
            {
                fprintf(stderr, "Ignore: message id too small\n");
                continue;
            }
            if (msg_channel->chnid == chosenid)
            {
                fprintf(stdout, "accecpted msg:%d recieved\n", msg_channel->chnid);
                if (writen(pd[1], msg_channel->data, len - sizeof(chnid_t)) < 0)
                {
                    exit(1);
                }
            }
        }

        free(msg_channel);
        close(sd);
    }
    return 0;
}