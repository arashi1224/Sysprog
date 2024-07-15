#include "../include/ringbuf.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>

#define CLOCK_REALTIME 0

void ringbuffer_init(rbctx_t *context, void *buffer_location, size_t buffer_size)
{

    /* your solution here */
    context->begin = buffer_location;
    context->end = context->begin + buffer_size;
    context->read = context->begin;
    context->write = context->begin;
    // initialize mutex
    pthread_mutex_init(&context->mtx, NULL);
    pthread_cond_init(&context->sig, NULL);
}

void ringbuffer_destroy(rbctx_t *context)
{
    /* your solution here */
    pthread_mutex_destroy(&context->mtx);
    pthread_cond_destroy(&context->sig);
}

int isRingBufferFull(rbctx_t *context, size_t message_len)
{

    size_t total_len_needed = sizeof(size_t) + message_len; // sizeof(size_t) bytes for the length
    size_t free_space = (context->write >= context->read) ? (context->end - context->begin) - (context->write - context->read) : context->read - context->write;
    if ((total_len_needed >= free_space && context->write >= context->read && ((size_t)context->write) + total_len_needed >= ((size_t)context->end)) || (total_len_needed >= free_space && (context->write < context->read)))
    {
        return 1;
    }

    return 0;
}
int ringbuffer_write(rbctx_t *context, void *message, size_t message_len)
{

    assert(context != NULL);
    assert(message != NULL);

    pthread_mutex_lock(&context->mtx);

    struct timespec timeToWait;
    clock_gettime(CLOCK_REALTIME, &timeToWait);
    timeToWait.tv_sec += 1;

    // If there is not enough space, return the Flag
    while (isRingBufferFull(context, message_len))
    {
        int timer = pthread_cond_timedwait(&context->sig, &context->mtx, &timeToWait);

        if (timer == ETIMEDOUT)
        {
            pthread_mutex_unlock(&context->mtx);
            return RINGBUFFER_FULL;
        }
    }
    ///////////////////////////////////////////////////////////////////////////////////////////////
    // Write the length of the message
    if (context->write + sizeof(size_t) <= context->end)
    {
        memcpy(context->write, &message_len, sizeof(size_t));
    }
    else
    {
        size_t first_part_size = context->end - context->write;
        memcpy(context->write, &message_len, first_part_size);
        memcpy(context->begin, (uint8_t *)&message_len + first_part_size, sizeof(size_t) - first_part_size);
    }

    context->write += sizeof(size_t);
    if (context->write >= context->end)
    {
        context->write -= (context->end - context->begin);
    }

    // printf("Message length written %zu\n", message_len);

    ////////////////////////////////////////////////////////////////////////////////////////////////
    // Write the message data
    if (context->write + message_len <= context->end)
    {
        memcpy(context->write, message, message_len);
    }
    else
    {
        size_t first_part_size = context->end - context->write;
        memcpy(context->write, message, first_part_size);
        memcpy(context->begin, (uint8_t *)message + first_part_size, message_len - first_part_size);
    }

    context->write += message_len;

    // pthread_mutex_lock(&mutex_lock);
    if (context->write >= context->end)
    {
        context->write -= (context->end - context->begin);
    }
    pthread_cond_broadcast(&context->sig);
    pthread_mutex_unlock(&context->mtx);

    return SUCCESS;
}
int ringbuffer_read(rbctx_t *context, void *buffer, size_t *buffer_len)
{

    assert(context != NULL);
    assert(buffer != NULL);
    assert(buffer_len != NULL);

    pthread_mutex_lock(&context->mtx);

    struct timespec timeToWait;
    clock_gettime(CLOCK_REALTIME, &timeToWait);
    timeToWait.tv_sec += 1;

    while (context->read == context->write)
    {
        int timer = pthread_cond_timedwait(&context->sig, &context->mtx, &timeToWait);
        if (timer == ETIMEDOUT)
        {
            pthread_cond_broadcast(&context->sig);
            pthread_mutex_unlock(&context->mtx);
            return RINGBUFFER_EMPTY;
        }
    }
    //////////////////////////////////////////////////////////////////////////////////////////////////
    // Read the length of the message
    size_t message_len = 0;
    if (context->read + sizeof(size_t) <= context->end)
    {
        memcpy(&message_len, context->read, sizeof(size_t));
    }
    else
    {
        size_t first_part_size = context->end - context->read;
        memcpy(&message_len, context->read, first_part_size);
        memcpy((uint8_t *)&message_len + first_part_size, context->begin, sizeof(size_t) - first_part_size);
    }

    // Check if buffer is not large enough
    while (*buffer_len < message_len)
    {

        int timer = pthread_cond_timedwait(&context->sig, &context->mtx, &timeToWait);
        if (timer == ETIMEDOUT)
        {
            pthread_cond_broadcast(&context->sig);
            pthread_mutex_unlock(&context->mtx);
            return OUTPUT_BUFFER_TOO_SMALL;
        }
    }

    *buffer_len = message_len;

    context->read += sizeof(size_t);
    if (context->read >= context->end)
    {
        context->read -= (context->end - context->begin);
    }
    ////////////////////////////////////////////////////////////////////////////////////////////
    // Read the actual message
    if (context->read + message_len <= context->end)
    {
        memcpy(buffer, context->read, message_len);
    }
    else
    {
        size_t first_part_size = context->end - context->read;
        memcpy(buffer, context->read, first_part_size);
        memcpy((uint8_t *)buffer + first_part_size, context->begin, message_len - first_part_size);
    }
    context->read += message_len;
    if (context->read >= context->end)
    {
        context->read -= (context->end - context->begin);
    }

    pthread_mutex_unlock(&context->mtx);
    pthread_cond_broadcast(&context->sig);

    return SUCCESS;
}
