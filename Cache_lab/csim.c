#include "cachelab.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <limits.h>
#include <getopt.h>
#include <string.h>
//#include "cache_functions.h"
// Name: Khanin Udomchoksakul
//ID: Kudomcho
//--------- Cache Structure -------------  
    //argument lists: h: help, v: verbose
    int s, off, h, v;
    // line elements
    typedef struct{
        
        int tag;
        int timestamp;
        int valid;
        
    }block_line, *associativity, **cache; //cache->cache asso->cache line

    //associativity and Set
    int E, S;
//--------- Hit, Miss, Evict-------------  
     int Hit, Miss, Evict;

    char p[800]; //getopt input for tracefiles
    
    //2D array containing sets and E (associativity)
    cache cache_arr = NULL; 

void createCache(){

    cache_arr = (cache)malloc(sizeof(associativity)*S); // allocate size of cache by asso * Set
    int i;
    for(i=0; i<S;i++){
            // allocate size of cache asso by length of cache line * E
            cache_arr[i] = (associativity)malloc(sizeof(block_line)*E);
            int j;
            for(j=0;j<E;j++){ // for each line initialize block values
                cache_arr[i][j].valid = 0;
                cache_arr[i][j].tag =-1; // all default tag bits are 1s
                cache_arr[i][j].timestamp =-1;
            } 
    }

}

void print_cacheInfo()
{
    printf("Usage: ./csim-ref [-hv] -s <S> -E <E> -off <off> -t <traceile>\n"
            "Options:\n"
            "  -h         Optional help flag that prints usage info.\n"
            "  -v         Optional verbose flag that displays trace info.\n"
            "  -s <S>     <s>: Number of set index bits (S = 2s is the number of sets).\n"
            "  -E <E>     Associativity (number of lines per set).\n"
            "  -off <off>  Number of block bits (B = 2b is the block size.\n"
            "  -t <file>  <tracefile>: Name of the valgrind trace to replay.\n\n"
            "Ex:\n"
            "  linux>  ./csim-ref -s 4 -E 1 -off 4 -t traces/yi.trace\n"
            "  linux>  ./csim-ref -v -s 8 -E 2 -off 4 -t traces/yi.trace\n");
}



void checkHitMiss(unsigned int addr){

    int update_tag = addr >> (s+off); // allocate tag bits by rightshift
    // allocate set index bits by addr
    int update_setIndex = (addr ^(update_tag<<(off+s)))>>off;
    int timestamp_max = -2147483648; // assign as minimum val of
    int max_stamp_index = -1;
    //check if hit occurs
    for(int i =0;i<E;i++){
        if(cache_arr[update_setIndex][i].tag == update_tag && cache_arr[update_setIndex][i].valid == 1 ){
            // count Hit
            Hit+=1;
            cache_arr[update_setIndex][i].timestamp = 0;
            
            return; // indicates the function is terminanted. Reference: https://www.tutorialspoint.com/return-from-void-functions-in-cplusplus
        }
         
        // when compulsory miss    
        if(cache_arr[update_setIndex][i].valid == 0){
                
                // Count Miss right away
                Miss += 1;
                // replace with a new tag
                cache_arr[update_setIndex][i].tag = update_tag;
                // assign valid =1 
                cache_arr[update_setIndex][i].valid = 1;
                // begin timestamp
                cache_arr[update_setIndex][i].timestamp = 0;
                // inc Miss
                
                return;
            }
    }
    // evict the cache and miss 
    Miss += 1;
    Evict += 1;
	

    // // update timestamp
    for(int i=0;i<E;i++){
        if(cache_arr[update_setIndex][i].timestamp > timestamp_max){
            timestamp_max = cache_arr[update_setIndex][i].timestamp;
            max_stamp_index = i;
        }
    }
    // assign the LRU tag with the cache tag
    cache_arr[update_setIndex][max_stamp_index].tag = update_tag;
    cache_arr[update_setIndex][max_stamp_index].timestamp = 0;
   
}



void trace_parsing(){

    //reference: https://www.programmingsimplified.com/c-program-read-file
    FILE *fp = fopen(p, "r"); // read mode
    if (fp == NULL){
      
      printf("Error while opening the file.\n");
      exit(-1);
    
    }
    // initialize 3 elements from trace convention: L 10,4 
    int size;
    unsigned int address;   
    char instruction;         
    // scan  L 10,4 until there are no more             
	while(fscanf(fp, " %c %xu,%d\n", &instruction, &address, &size) > 0)
	{
        switch(instruction){
            
            //case 'I': continue;	   
			case 'L':
				checkHitMiss(address);
				break;
			case 'M':
				checkHitMiss(address);  
			case 'S':
				checkHitMiss(address);
        }
        // after checking cache
        for(int i = 0; i < S; ++i)
            for(int j = 0; j < E; ++j)
                if(cache_arr[i][j].valid == 1){
                    cache_arr[i][j].timestamp += 1;
                }
    }
    //close the file after done using
    fclose(fp);
    //free each set of cache after using
    for(int i = 0; i<S;i++){
        free(cache_arr[i]);
    }
    // free the whole cache
    free(cache_arr);

}




int main(int argc, char* argv[])
{
  
    h = 0; 
	v = 0; 
	Hit = Miss = Evict = 0;
	int opt; 
        
    // reference: http://www2.phys.canterbury.ac.nz/dept/docs/manuals/unix/DEC_4.0e_Docs/HTML/MAN/MAN3/0971____.HTM    
	while(-1 != (opt = (getopt(argc, argv, "hvs:E:b:t:"))))
	{
		switch(opt)
		{
			case 'h':
				h = 1;
				print_cacheInfo();
				break;
			case 'v':
				v = 1;
				print_cacheInfo();
				break;
			case 's':
				s = atoi(optarg);// optarg = next argument pointed to and atoi changes string to an integer
				break;
			case 'E':
				E = atoi(optarg);
				break;
			case 'b':
				off = atoi(optarg);
				break;
			case 't':
				strcpy(p, optarg);
				break;
			default:
				print_cacheInfo();
				break;
		}
	}
	
	if(s<=0 || off<=0 || E<=0 || p==NULL){ 
	        return -1;
    }
	S = (1 << s);                
	
	FILE* fp = fopen(p, "r");
	if(fp == NULL)
	{
		printf("Error while opening the file.\n");
		exit(-1);
	}
	
	createCache();  
	trace_parsing(); 

    printSummary(Hit, Miss, Evict);
    
    return 0;
}
