#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#include "proto.h"
#include "thr_list.h"
#include "server_conf.h"
#include "medialib.h"

static pthread_t tid_list;

//节目单包含的节目数量
static int nr_list_ent;
//节目单信息数组，每一条存储一个节目频道信息
static struct mlib_listentry_st *list_ent;

static void *thr_list(void *p)
{
    int totalsize = sizeof(chnid_t);

    for (int i = 0; i < nr_list_ent; i++)
    {
        totalsize += sizeof(struct msg_listentry_st) + strlen(list_ent[i].desc);
    }
    
    struct msg_list_st *entlistp;
    entlistp = malloc(totalsize);
    if (entlistp == NULL)
    {
        syslog(LOG_ERR, "malloc():%s", strerror(errno));
        exit(1);
    }

    entlistp->chnid = LISTCHNID;
    struct msg_listentry_st *entryp;
    entryp = entlistp->entry;

    for (int i = 0; i < nr_list_ent; i++)
    {
        int size = sizeof(struct msg_listentry_st) + strlen(list_ent[i].desc);

        entryp->chnid = list_ent[i].chnid;
        entryp->len = htons(size);
        strcpy(entryp->desc, list_ent[i].desc);
        entryp = (void *)(((char *)entryp) + size);
    }

    // 在0号频道发送节目单
    while (1)
    {
        int ret = sendto(serverfd, entlistp, totalsize, 0, (void *)&sndaddr, sizeof(sndaddr));
        if (ret < 0)
        {
            syslog(LOG_WARNING, "sendto():%s", strerror(errno));
        }
        else
        {
            syslog(LOG_DEBUG, "send to program list succeed.");
        }
        usleep(1000000);
    }
}

int thr_list_create(struct mlib_listentry_st *listp, int nr_ent)
{
    list_ent = listp;
    nr_list_ent = nr_ent;
    
    int err = pthread_create(&tid_list, NULL, thr_list, NULL);
    if (err)
    {
        syslog(LOG_ERR, "pthread_create():%s", strerror(errno));
        return -1;
    }
    return 0;
}

int thr_list_destroy(void)
{
    pthread_cancel(tid_list);
    pthread_join(tid_list, NULL);
    return 0;
}
