#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <asm/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <numaif.h>
#include <numa.h>
#include <sched.h>
#include <time.h>
#include <pthread.h>

/* Used as argument to thread_start() */
typedef struct thread_info_s
{
    void **x;
    size_t core;
    size_t N;
    size_t M;
    size_t B;
    size_t T;
} thread_info;

#define READ_BLOCK(block, size)\
do{\
    int var, idx;\
    var = idx = 0;\
    while((idx + sizeof(int)) <= size)\
    {\
        var = (*((int *)(block + idx)))++;\
        idx += sizeof(int);\
    }\
}while(0);

#define WRITE_BLOCK(block, size)\
do{\
    int var, idx;\
    idx = 0;\
    var = 0xABABABAB;\
    while((idx + sizeof(int)) <= size)\
    {\
        *((int *)(block + idx)) = var;\
        idx += sizeof(int);\
    }\
}while(0);

/* Utility functions */
void pin_to_core(size_t core)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core, &cpuset);
    //pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
}

struct timespec timespec_diff(struct timespec start, struct timespec end)
{
    struct timespec temp;
    if ((end.tv_nsec-start.tv_nsec)<0) {
        temp.tv_sec = end.tv_sec-start.tv_sec-1;
        temp.tv_nsec = 1000000000+end.tv_nsec-start.tv_nsec;
    } else {
        temp.tv_sec = end.tv_sec-start.tv_sec;
        temp.tv_nsec = end.tv_nsec-start.tv_nsec;
    }
    return temp;
}

int64_t convert_timespec_to_ns(struct timespec *a )
{
    const int64_t a_ns = (a->tv_sec * (int64_t)1000000000) + a->tv_nsec;
    return a_ns;
}

/*Print Functions */
void print_numa_bitmask(struct bitmask *bm)
{
    for(size_t i=0;i<bm->size;++i)
    {
        printf("%d ", numa_bitmask_isbitset(bm, i));
    }
}

void print_timespec(struct timespec *t)
{
    /*Print s:ms:us.ns*/
    printf("%lds:%ldms:%ldus.%ldns",t->tv_sec, t->tv_nsec/(1000*1000),
        (t->tv_nsec%(1000*1000))/1000, t->tv_nsec%1000);
}

/*This thread allocates a piece of memory and pins it to a given core*/
void* thread1(void *arg)
{
    thread_info *t = (thread_info *) arg;
    struct timespec c1, c2, diff;
    size_t N = t->N, M = t->M, core = t->core, T = t->T, B = t->B;

    pin_to_core(core);

    void* y = numa_alloc_local(N);

    //Reduce N by block size to ensure we do not cross the mem bdry
    N = N - (B-1);

    clock_gettime(CLOCK_REALTIME, &c1);

    char c;
    for (size_t i = 0;i<M;++i)
        //for(size_t j = 0;j<N;++j)
        for(size_t j = 0;j<T;++j)
        {
#if 1
#ifndef SEQ
            //*(((int*)y) + ((j * 1009) % N)) += 1;
            *(int *)(y + ((j * 1009) % N)) += 1;
#else
            *(((char*)y) + j) += 1;
#endif
#else
            READ_BLOCK((((char*)y) + ((j * 1009) % N)), B);
#endif
        }

    clock_gettime(CLOCK_REALTIME, &c2);
    diff = timespec_diff(c1, c2);

    printf("Elapsed read/write by same thread that allocated on core %ld : ",
        core);
    print_timespec(&diff);
    printf("\n");

    *(t->x) = y;
}

/*This thread accesses a memory already allocated by one core (and pinned).
The access is simple a "+=", which is a single instruction read and a single
instruction write */

void* thread2(void *arg)
{
    thread_info *t = (thread_info *) arg;
    struct timespec c1, c2, diff;
    size_t N = t->N, M = t->M, core = t->core, T = t->T, B = t->B;
    void *x = *(t->x);
    double access_time;

    N = N - (B-1);
    pin_to_core(core);

    clock_gettime(CLOCK_REALTIME, &c1);

    char c;

    /*Memory access loop.  1 byte of memory is read & written to.
    Try the access "N" times and iterate it "M" times
    */
    for (size_t i = 0;i<M;++i)
        //for(size_t j = 0;j<N/10;++j)
        for(size_t j = 0;j<T;++j)
        {
#if 1
#ifndef SEQ
            //*(int *)(x + ((j * 1009) % N)) += 1;
            //READ_BLOCK((x + ((j * 1009) % N)), B);
            READ_BLOCK((x + ((i+1)* rand()) % N), B);
#else
            *(((char*)x) + j) += 1;
#endif
#else
            READ_BLOCK((((char*)x) + ((j * 1009) % N)), B);
#endif
        }

    clock_gettime(CLOCK_REALTIME, &c2);
    diff = timespec_diff(c1, c2);

    /*printf("Elapsed read/write by thread on core %ld : ", core);
    print_timespec(&diff);
    printf("\n");*/

    //access_time = convert_timespec_to_ns(&diff) / (T*M);
    access_time = convert_timespec_to_ns(&diff) / (M)/ 1000 /1000;
    printf("Access Performance by thread on core %ld : %fms\n", core,access_time);
}

int main(int argc, const char **argv)
{
    void* x;
    pthread_attr_t attr;
    pthread_t t1, t2;
    //Allocate N units of memory, iterate M tumes and each time read T blocks
    size_t N = 10000000, M = 5, T = 10000, B;
    thread_info *tinfo;
    int num_threads = 1;

    N = atoi(argv[1]);
    B = atoi(argv[2]);
    T = atoi(argv[3]);
    printf("%d\n", N);
    //int numcpus = numa_num_task_cpus();
    int numcpus = numa_num_configured_cpus();
    printf("numa_available() %d\n", numa_available());
    numa_set_localalloc();

    struct bitmask* bm = numa_bitmask_alloc(numcpus);
    for (int i=0;i<=numa_max_node();++i)
    {
        numa_node_to_cpus(i, bm);
        printf("numa node %d ",i);
        print_numa_bitmask(bm);
        printf("%ld\n",numa_node_size(i, 0));
    }
    numa_bitmask_free(bm);

    pthread_attr_init(&attr);
    tinfo = malloc(num_threads * sizeof(thread_info));
    tinfo[0].x = &x;
    tinfo[0].core = 0;
    tinfo[0].N = N;
    tinfo[0].M = M;
    tinfo[0].B = B;
    tinfo[0].T = T;
    pthread_create(&t1, &attr, thread1, &tinfo[0]);
    pthread_join(t1, NULL);

    for (size_t i = 0;i<numcpus;++i)
    {
        tinfo[0].core = i;
        pthread_create(&t2, &attr, thread2, &tinfo[0]);
        pthread_join(t2, NULL);
    }

    numa_free(x, N);

    return 0;
}

