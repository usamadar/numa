numa
====

Sample code demonstrating exploiting NUMA architecture using libnuma

This code can demonstrate 
        1. Pinning the affinity 
        2. having NUMA local memory alloc (numa_alloc_local) 
        3. Also some other NUMA functions like bitmask 

If you want to compile it, you need the libnuma 

gcc numatest.c -lnuma -lpthread -lrt 
