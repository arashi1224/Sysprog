#include "../include/ringbuf.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>

void ringbuffer_init(rbctx_t *context, void *buffer_location, size_t buffer_size)
{
    /* your solution here */
    context->begin = buffer_location;
    context->read = context->begin;
    context->write = context->begin;
    context->end = context->begin+buffer_size;

    pthread_mutex_init(&context->mtx, NULL);

    pthread_cond_init(&context->sig, NULL);
}

int ringbuffer_write(rbctx_t *context, void *message, size_t message_len)
{
    /* your solution here */
    pthread_mutex_lock(&context->mtx);

    //Check if there's enough space
    while((context->write < context->read) && ((size_t)(message_len + sizeof(size_t)) >= (size_t)(context->read - context->write))) {
        int check = 0;
        struct timespec waittime;
        clock_gettime(CLOCK_REALTIME, &waittime);
        waittime.tv_sec += 1;
        check = pthread_cond_timedwait(&context->sig, &context->mtx, &waittime);
        if(check == ETIMEDOUT) {
            pthread_mutex_unlock(&context->mtx);
            return RINGBUFFER_FULL;
        }
    }

    while((context->write >= context->read) && (message_len + sizeof(size_t) >= (size_t)(context->end-context->begin - (context->write - context->read)))) {
        int check = 0;
        struct timespec waittime;
        clock_gettime(CLOCK_REALTIME, &waittime);
        waittime.tv_sec += 1;
        check = pthread_cond_timedwait(&context->sig, &context->mtx, &waittime);
        if(check == ETIMEDOUT) {
            pthread_mutex_unlock(&context->mtx);
            return RINGBUFFER_FULL;
        }
    }

    //write length
    if (context->write + sizeof(size_t) >= context->end) {
        size_t firstpart = context->end - context->write;
        memcpy(context->write, &message_len, firstpart);
        memcpy(context->begin, (uint8_t*)&message_len + firstpart, sizeof(size_t) - firstpart);
    } else {
        memcpy(context->write, &message_len, sizeof(size_t));
    }
    context->write += sizeof(size_t);
    if (context->write >= context->end) {
        context->write -= (context->end - context->begin);
    }

    //write message
    if(context->write < context->read)  {
        memcpy(context->write, message, message_len);
        context->write += message_len;
    }
    else {
        if(context->write + message_len < context->end) {
            memcpy(context->write, message, message_len); 
            context->write += message_len;
        }
        else {
            uint8_t first_part_len = context->end - context->write;
            uint8_t second_part_len = message_len - first_part_len;
            memcpy(context->write, message, first_part_len);
            memcpy(context->begin, message + first_part_len, second_part_len);
            context->write = context->begin + second_part_len;
        }
    }
    pthread_mutex_unlock(&context->mtx);
    pthread_cond_signal(&context->sig);
    return SUCCESS;
}

int ringbuffer_read(rbctx_t *context, void *buffer, size_t *buffer_len)
{
    /* your solution here */
    pthread_mutex_lock(&context->mtx);
    while(context->read == context->write) {
        int check = 0;
        struct timespec waittime;
        clock_gettime(CLOCK_REALTIME, &waittime);
        waittime.tv_sec += 1;
        check = pthread_cond_timedwait(&context->sig, &context->mtx, &waittime);
        if(check == ETIMEDOUT) {
            pthread_mutex_unlock(&context->mtx);
            return RINGBUFFER_EMPTY;
        }
    }

    //read length
    size_t message_len = 0;
    if (context->read + sizeof(size_t) >= context->end) {
        size_t firstpart = context->end - context->read;
        memcpy(&message_len, context->read, firstpart);
        memcpy(((uint8_t*)&message_len) + firstpart, context->begin, sizeof(size_t) - firstpart);
    } 
    else {
        memcpy(&message_len, context->read, sizeof(size_t));
    }

    //Check cond and not change pointer if buffer too small
    while(message_len > *buffer_len) {
        int check = 0;
        struct timespec waittime;
        clock_gettime(CLOCK_REALTIME, &waittime);
        waittime.tv_sec += 1;
        check = pthread_cond_timedwait(&context->sig, &context->mtx, &waittime);
        if(check == ETIMEDOUT) {
            pthread_mutex_unlock(&context->mtx);
            return RINGBUFFER_EMPTY;
        }
    }
    
    context->read += sizeof(size_t);
    if (context->read >= context->end) {
        context->read -= (context->end - context->begin);
    }
    
    //read message
    *buffer_len = message_len;
    if(context->read + message_len >= context->end) {
        int cut = context->end - context->read;
        memcpy(buffer, context->read, cut);
        memcpy(buffer + cut, context->begin, message_len-cut);
        context->read = context->begin + message_len - cut;
        
        pthread_mutex_unlock(&context->mtx);
        pthread_cond_signal(&context->sig);
        return SUCCESS;
    }
    memcpy(buffer, context->read, message_len);
    context->read += message_len;
    
    pthread_mutex_unlock(&context->mtx);
    pthread_cond_signal(&context->sig);
    return SUCCESS;
}

void ringbuffer_destroy(rbctx_t *context)
{
    /* your solution here */
    pthread_mutex_destroy(&context->mtx);

    pthread_cond_destroy(&context->sig);
}
