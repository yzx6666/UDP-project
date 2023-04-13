#ifndef THR_CHANNEL_H__
#define THR_CHANNEL_H__

#include "medialib.h"

int thr_channel_create(struct mlib_listentry_st *ptr);
 
int thr_channel_desctroy(struct mlib_listentry_st *ptr);

int thr_channel_desctroyall();

#endif