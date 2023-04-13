#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <unistd.h>


#include "thr_channel.h"
#include "server_conf.h"

#include "proto.h"
#include "thr_list.h"
#include "medialib.h"

struct thr_channel_ent_st
{
    chnid_t chnid;
    pthread_t tid;
};

static int tid_nextpos = 0;

struct thr_channel_ent_st thr_channel[CHNNR];

static void *thr_channel_sender(void *ptr)
{
    struct msg_channel_st *sbufp;
    struct thr_channel_ent_st *ent = ptr;

    while (1)
    {
        sbufp = malloc(MSG_CHANNEL_MAX);
        if(sbufp == NULL) {
            syslog(LOG_ERR, "malloc: %s", strerror(errno));
            exit(1);
        }

        int datasize = MAX_DATA;

        sbufp->chnid = ent->chnid;

        int len = mlib_readchn(ent->chnid, sbufp->data, MAX_DATA);
        if (sendto(serverfd, sbufp, len + sizeof(chnid_t), 0, (void *)&sndaddr, sizeof(sndaddr)) < 0)
        {
            syslog(LOG_ERR, "thr_channel(%d):sendto():%s", ent->chnid, strerror(errno));
            break;
        }
        else
        {
            syslog(LOG_DEBUG, "thr_channel(%d): sendto() succeed.", ent->chnid);
        }
        //出让调度器
        sched_yield();
    }
    pthread_exit(NULL);
}

int thr_channel_create(struct mlib_listentry_st *ptr)
{
    int err = pthread_create(&thr_channel[tid_nextpos].tid, NULL, thr_channel_sender, ptr);
    if(err) {
        syslog(LOG_WARNING, "pthread_create: %s", strerror(err));
        return -err;
    }

    thr_channel[tid_nextpos].chnid = ptr->chnid;

    tid_nextpos ++;

    return 0;
}
 
int thr_channel_desctroy(struct mlib_listentry_st *ptr)
{
    for (int i = 0; i < CHNNR; i++)
    {
        if(thr_channel[i].chnid == ptr->chnid) {
            if(pthread_cancel(thr_channel[i].tid) < 0) {
                syslog(LOG_WARNING, "pthread_cancel: the thread of channel %d", ptr->chnid);
                return -ESRCH;
            }
        }
        
        pthread_join(thr_channel[i].tid, NULL);
        thr_channel[i].chnid = -1;
        return 0;
    }
}

int thr_channel_desctroyall()
{
    for (int i = 0; i < CHNNR; i++)
    {
        if(thr_channel[i].chnid > 0) {
            if(pthread_cancel(thr_channel[i].tid) < 0) {
                syslog(LOG_WARNING, "pthread_cancel: the thread of channel %d", thr_channel[i].chnid);
                return -ESRCH;
            }
            pthread_join(thr_channel[i].tid, NULL);
            thr_channel[i].chnid = -1;
        }
        
        return 0;
    }
}