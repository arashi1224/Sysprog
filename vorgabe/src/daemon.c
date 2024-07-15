#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "../include/daemon.h"
#include "../include/ringbuf.h"

/* IN THE FOLLOWING IS THE CODE PROVIDED FOR YOU 
 * changing the code will result in points deduction */

/********************************************************************
* NETWORK TRAFFIC SIMULATION: 
* This section simulates incoming messages from various ports using 
* files. Think of these input files as data sent by clients over the
* network to our computer. The data isn't transmitted in a single 
* large file but arrives in multiple small packets. This concept
* is discussed in more detail in the advanced module: 
* Rechnernetze und Verteilte Systeme
*
* To simulate this parallel packet-based data transmission, we use multiple 
* threads. Each thread reads small segments of the files and writes these 
* smaller packets into the ring buffer. Between each packet, the
* thread sleeps for a random time between 1 and 100 us. This sleep
* simulates that data packets take varying amounts of time to arrive.
*********************************************************************/
typedef struct {
    rbctx_t* ctx;
    connection_t* connection;
} w_thread_args_t;

void* write_packets(void* arg) {
    /* extract arguments */
    rbctx_t* ctx = ((w_thread_args_t*) arg)->ctx;
    size_t from = (size_t) ((w_thread_args_t*) arg)->connection->from;
    size_t to = (size_t) ((w_thread_args_t*) arg)->connection->to;
    char* filename = ((w_thread_args_t*) arg)->connection->filename;

    /* open file */
    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        fprintf(stderr, "Cannot open file with name %s\n", filename);
        exit(1);
    }

    /* read file in chunks and write to ringbuffer with random delay */
    unsigned char buf[MESSAGE_SIZE];
    size_t packet_id = 0;
    size_t read = 1;
    while (read > 0) {
        size_t msg_size = MESSAGE_SIZE - 3 * sizeof(size_t);
        read = fread(buf + 3 * sizeof(size_t), 1, msg_size, fp);
        if (read > 0) {
            memcpy(buf, &from, sizeof(size_t));
            memcpy(buf + sizeof(size_t), &to, sizeof(size_t));
            memcpy(buf + 2 * sizeof(size_t), &packet_id, sizeof(size_t));
            while(ringbuffer_write(ctx, buf, read + 3 * sizeof(size_t)) != SUCCESS){
                usleep(((rand() % 50) + 25)); // sleep for a random time between 25 and 75 us
            }
        }
        packet_id++;
        usleep(((rand() % (100 -1)) + 1)); // sleep for a random time between 1 and 100 us
    }
    fclose(fp);
    return NULL;
}

/* END OF PROVIDED CODE */


/********************************************************************/

/* YOUR CODE STARTS HERE */

// 2. filtering functionality
size_t filter(connection_t* connection, unsigned char* message, size_t len) {
    if(connection->to == connection->from) return 0;
    if(connection->to == 42 || connection->from == 42) return 0;
    if(connection->to + connection->from == 42) return 0;

    char* avoid = "malicious";
    size_t not_avail = 0;
    for(size_t i = 0; i < len; i++) {
        if(message[i] == avoid[not_avail]) {
            if(not_avail == 8) return 0;
            not_avail++;
        }
    }

    return 1;
}

// 1. read functionality     --mimic the write, tbh
typedef struct {
    rbctx_t* ctx;
    pthread_mutex_t* mtx;
    pthread_cond_t* sig;
    size_t* lastpacket_id;
} r_thread_args_t;

void* read_packets(void* arg) 
{
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

    unsigned char buf[MESSAGE_SIZE];
    size_t len = MESSAGE_SIZE;

    pthread_mutex_t* port_mtx = ((r_thread_args_t*)arg)->mtx;
    pthread_cond_t* port_sig = ((r_thread_args_t*)arg)->sig;
    size_t* lastpacket_id = ((r_thread_args_t*)arg)->lastpacket_id;
    
     while(1) {
        while (ringbuffer_read(((r_thread_args_t*)arg)->ctx, buf, &len) != SUCCESS) {
            // printf("No message to read\n");
            // pthread_cond_wait(&sig, &mutex);
            len = MESSAGE_SIZE;
            pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
            usleep((rand() % 50) + 25); // sleep for a random time between 25 and 75 us
            pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
        }

        connection_t connection;
        size_t packet_id;
        len = len - 3 * sizeof(size_t);
        unsigned char message[MESSAGE_SIZE];
        memcpy(&connection.from, buf, sizeof(size_t));
        memcpy(&connection.to, buf + sizeof(size_t), sizeof(size_t));
        memcpy(&packet_id, buf + 2 * sizeof(size_t), sizeof(size_t));
        memcpy(&message, buf + 3 * sizeof(size_t), len);

        
        pthread_mutex_lock(&port_mtx[connection.to]);
        if(packet_id != 0 && packet_id <= lastpacket_id[connection.to]) {
            //do something to just skip this 
            pthread_cond_broadcast(&port_sig[connection.to]);
            pthread_mutex_unlock(&port_mtx[connection.to]);
            continue;
        }
        while(packet_id > lastpacket_id[connection.to] + 1 && packet_id != 0) {
            printf("Waiting for correct sequence...\n");
            pthread_cond_wait(&port_sig[connection.to], &port_mtx[connection.to]);
        }

        lastpacket_id[connection.to] = packet_id;  //signify which packet is handled
        if(filter(&connection, message, len)) {
//            3. (thread-safe) write to file functionality
            char portname[20];
            sprintf(portname, "./%d.txt", connection.to);
            FILE* port = fopen(portname, "a");
            if (port == NULL) {
                pthread_cond_broadcast(&port_sig[connection.to]);
                pthread_mutex_unlock(&port_mtx[connection.to]);

                exit(1);
            }

            fwrite(message, sizeof(*message), len, port);
            fclose(port);
            printf("written: %d %d %zu %s \n", connection.from, connection.to, packet_id, message);
        }
        pthread_cond_broadcast(&port_sig[connection.to]);
        pthread_mutex_unlock(&port_mtx[connection.to]);
    }

    return NULL;
}
/*  YOUR CODE ENDS HERE */

/********************************************************************/

int simpledaemon(connection_t* connections, int nr_of_connections) {
    /* initialize ringbuffer */
    rbctx_t rb_ctx;
    size_t rbuf_size = 1024;
    void *rbuf = malloc(rbuf_size);
    if (rbuf == NULL) {
        fprintf(stderr, "Error allocation ringbuffer\n");
    }

    ringbuffer_init(&rb_ctx, rbuf, rbuf_size);

    /****************************************************************
    * WRITER THREADS 
    * ***************************************************************/

    /* prepare writer thread arguments */
    w_thread_args_t w_thread_args[nr_of_connections];
    for (int i = 0; i < nr_of_connections; i++) {
        w_thread_args[i].ctx = &rb_ctx;
        w_thread_args[i].connection = &connections[i];
        /* guarantee that port numbers range from MINIMUM_PORT (0) - MAXIMUMPORT */
        if (connections[i].from > MAXIMUM_PORT || connections[i].to > MAXIMUM_PORT ||
            connections[i].from < MINIMUM_PORT || connections[i].to < MINIMUM_PORT) {
            fprintf(stderr, "Port numbers %d and/or %d are too large\n", connections[i].from, connections[i].to);
            exit(1);
        }
    }

    /* start writer threads */
    pthread_t w_threads[nr_of_connections];
    for (int i = 0; i < nr_of_connections; i++) {
        pthread_create(&w_threads[i], NULL, write_packets, &w_thread_args[i]);
    }

    /****************************************************************
    * READER THREADS
    * ***************************************************************/

    pthread_t r_threads[NUMBER_OF_PROCESSING_THREADS];

    /* END OF PROVIDED CODE */
    
    /********************************************************************/

    /* YOUR CODE STARTS HERE */

    // 1. think about what arguments you need to pass to the processing threads
    // 2. start the processing threads

    pthread_mutex_t port_mutex[MAXIMUM_PORT + 1];
    pthread_cond_t port_sig[MAXIMUM_PORT + 1];
    size_t lastpacket_id[MAXIMUM_PORT + 1];
    
    for (int i = 0; i < MAXIMUM_PORT + 1; i++) {
        pthread_mutex_init(&port_mutex[i], NULL);
        pthread_cond_init(&port_sig[i], NULL);
        lastpacket_id[i] = 0;
    }
    
    r_thread_args_t r_thread_args;
    r_thread_args.ctx = &rb_ctx;
    r_thread_args.mtx = port_mutex;
    r_thread_args.sig = port_sig;
    r_thread_args.lastpacket_id = lastpacket_id;

    for(int i = 0; i < NUMBER_OF_PROCESSING_THREADS; i++) {
        pthread_create(&r_threads[i], NULL, read_packets, &r_thread_args);
    }

    /* YOUR CODE ENDS HERE */

    /********************************************************************/



    /* IN THE FOLLOWING IS THE CODE PROVIDED FOR YOU 
     * changing the code will result in points deduction */

    /****************************************************************
     * CLEANUP
     * ***************************************************************/

    /* after 5 seconds JOIN all threads (we should definitely have received all messages by then) */
    printf("daemon: waiting for 5 seconds before canceling reading threads\n");
    sleep(5);
    for (int i = 0; i < NUMBER_OF_PROCESSING_THREADS; i++) {
        pthread_cancel(r_threads[i]);
    }

    /* wait for all threads to finish */
    for (int i = 0; i < nr_of_connections; i++) {
        pthread_join(w_threads[i], NULL);
    }

    /* join all threads */
    for (int i = 0; i < NUMBER_OF_PROCESSING_THREADS; i++) {
        pthread_join(r_threads[i], NULL);
    }


    /* END OF PROVIDED CODE */



    /********************************************************************/
    
    /* YOUR CODE STARTS HERE */

    // use this section to free any memory, destory mutexe etc.
    for (int i = 0; i <= MAXIMUM_PORT; i++) {
        pthread_mutex_destroy(&port_mutex[i]);
        pthread_cond_destroy(&port_sig[i]);
    }

    /* YOUR CODE ENDS HERE */

    /********************************************************************/



    /* IN THE FOLLOWING IS THE CODE PROVIDED FOR YOU 
    * changing the code will result in points deduction */

    free(rbuf);
    ringbuffer_destroy(&rb_ctx);

    return 0;

    /* END OF PROVIDED CODE */
}