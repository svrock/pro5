//Shirisha Vadlamudi
//Peter Kemper

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <alloca.h>
#include <time.h>
#include "table.c"


int FRAME_SIZE;
#define TOTAL_FRAME_COUNT 256       
#define PAGE_MASK         0xFF00    
#define OFFSET_MASK       0xFF      
#define SHIFT             8         
#define TLB_SIZE          16        
#define PAGE_TABLE_SIZE   256       
#define MAX_ADDR_LEN      10        
#define PAGE_READ_SIZE    256       

typedef enum { false = 0, true = !false } bool;

vmTable_t* tlbTable; 
vmTable_t* pageTable; 
int** dram;

char algo_choice; 
char display_choice; 
char frames_choice;


int nextTLBentry = 0; 
int nextPage = 0;  
int nextFrame = 0; 

FILE* address_file;
FILE* backing_store;

char addressReadBuffer[MAX_ADDR_LEN];
int virtual_addr;
int page_number;
int offset_number;

clock_t start, end;
double cpu_time_used;
int functionCallCount = 0;

signed char fileReadBuffer[PAGE_READ_SIZE];

signed char translatedValue;

void translateAddress();
void readFromStore(int pageNumber);
void replacePageFIFO(int pageNumber, int frameNumber);
void replacePageLRU(int pageNumber, int frameNumber);
int getOldestEntry(int tlbSize);
double getAvgTimeInBackingStore();

int main(int argc, char *argv[])
{
    tlbTable = createVMtable(TLB_SIZE); 
    pageTable = createVMtable(PAGE_TABLE_SIZE); 
    
    int translationCount = 0;
    char* algoName;

    if (argc != 2) {
        fprintf(stderr,"Usage: [executable file] [input file]\n");
        return -1;
    }

    do {
        printf("\nChoose number of Physical Frames? [1: 256, 2: 128]: ");
        scanf("\n%c", &frames_choice);
    } while (frames_choice != '1' && frames_choice != '2');

    if (frames_choice == '1') {
        FRAME_SIZE  = 256;
    } else {
        FRAME_SIZE  = 128;
    }

    dram = dramAllocate(TOTAL_FRAME_COUNT, FRAME_SIZE); 

    backing_store = fopen("BACKING_STORE.bin", "rb");

    if (backing_store == NULL) {
        fprintf(stderr, "Error opening BACKING_STORE.bin %s\n","BACKING_STORE.bin");
        return -1;
    }
    
    address_file = fopen(argv[1], "r");

    if (address_file == NULL) {
        fprintf(stderr, "Error opening %s. Expecting [InputFile].txt or equivalent.\n",argv[1]);
        return -1;
    }

    printf("\nNumber of logical pages: %d\nPage size: %d bytes\nPage Table Size: %d\nTLB Size: %d entries\nNumber of Physical Frames: %d\nPhysical Memory Size: %d bytes", PAGE_TABLE_SIZE, PAGE_READ_SIZE, PAGE_TABLE_SIZE, TLB_SIZE, FRAME_SIZE, PAGE_READ_SIZE * FRAME_SIZE);

    do {
        printf("\nDisplay All Physical Addresses? [y/n]: ");
        scanf("\n%c", &display_choice);
    } while (display_choice != 'n' && display_choice != 'y');

    do {
        printf("Choose TLB Replacement Strategy [1: FIFO, 2: LRU]: ");
        scanf("\n%c", &algo_choice);
    } while (algo_choice != '1' && algo_choice != '2');

    
    while (fgets(addressReadBuffer, MAX_ADDR_LEN, address_file) != NULL) {
        virtual_addr = atoi(addressReadBuffer); // converting from ascii to int

        page_number = getPageNumber(PAGE_MASK, virtual_addr, SHIFT);

        offset_number = getOffset(OFFSET_MASK, virtual_addr);

        translateAddress(algo_choice);
        translationCount++;
    }

    if (algo_choice == '1') {
        algoName = "FIFO";
    }else {
        algoName = "LRU";
    }

    printf("\n-----------------------------------------------------------------------------------\n");
    printf("\nResults Using %s Algorithm: \n", algoName);
    printf("Number of translated addresses = %d\n", translationCount);
    double pfRate = (double)pageTable->pageFaultCount / (double)translationCount;
    double TLBRate = (double)tlbTable->tlbHitCount / (double)translationCount;
    printf("Page Faults = %d\n", pageTable->pageFaultCount);
    printf("Page Fault Rate = %.3f %%\n",pfRate * 100);
    printf("TLB Hits = %d\n", tlbTable->tlbHitCount);
    printf("TLB Hit Rate = %.3f %%\n", TLBRate * 100);
    printf("Average time spent retrieving data from backing store: %.3f millisec\n", getAvgTimeInBackingStore());
    printf("\n-----------------------------------------------------------------------------------\n");

    fclose(address_file);
    fclose(backing_store);

    freeVMtable(&tlbTable);
    freeVMtable(&pageTable);
    freeDRAM(&dram, TOTAL_FRAME_COUNT);
    return 0;
}

void translateAddress()
{
    int frame_number = -1;

    for (int i = 0; i < tlbTable->length; i++) {
        if (tlbTable->pageNumArr[i] == page_number) {
            frame_number = tlbTable->frameNumArr[i];
            tlbTable->tlbHitCount++;
            break;
        }
    }

    if (frame_number == -1) {
        tlbTable->tlbMissCount++;

        for(int i = 0; i < nextPage; i++){
            if(pageTable->pageNumArr[i] == page_number){
                frame_number = pageTable->frameNumArr[i];
                break;
            }
        }

        if(frame_number == -1) {
            pageTable->pageFaultCount++;

            start = clock();
            readFromStore(page_number);
            cpu_time_used += (double)(clock() - start) / CLOCKS_PER_SEC;
            functionCallCount++;
            frame_number = nextFrame - 1;
        }
    }

    if (algo_choice == '1') {
        replacePageFIFO(page_number, frame_number);
    }
    else {
        replacePageLRU(page_number, frame_number);
    }

    translatedValue = dram[frame_number][offset_number];

    if (display_choice == 'y') {
        printf("\nVirtual address: %d\t\tPhysical address: %d\t\tValue: %d", virtual_addr, (frame_number << SHIFT) | offset_number, translatedValue);
    }
}

void readFromStore(int pageNumber)
{
    if (fseek(backing_store, pageNumber * PAGE_READ_SIZE, SEEK_SET) != 0) {
        fprintf(stderr, "Error seeking in backing store\n");
    }

    if (fread(fileReadBuffer, sizeof(signed char), PAGE_READ_SIZE, backing_store) == 0) {
        fprintf(stderr, "Error reading from backing store\n");
    }

    for (int i = 0; i < PAGE_READ_SIZE; i++) {
        dram[nextFrame][i] = fileReadBuffer[i];
    }

    pageTable->pageNumArr[nextPage] = pageNumber;
    pageTable->frameNumArr[nextPage] = nextFrame;

    nextFrame++;
    nextPage++;
}

void replacePageFIFO(int pageNumber, int frameNumber)
{
    int i;

    for(i = 0; i < nextTLBentry; i++){
        if(tlbTable->pageNumArr[i] == pageNumber){
            break;
        }
    }

    if(i == nextTLBentry){
        if(nextTLBentry < TLB_SIZE){
            tlbTable->pageNumArr[nextTLBentry] = pageNumber;
            tlbTable->frameNumArr[nextTLBentry] = frameNumber;
        }
        else{
            tlbTable->pageNumArr[nextTLBentry - 1] = pageNumber;
            tlbTable->frameNumArr[nextTLBentry - 1] = frameNumber;

            for(i = 0; i < TLB_SIZE - 1; i++){
                tlbTable->pageNumArr[i] = tlbTable->pageNumArr[i + 1];
                tlbTable->frameNumArr[i] = tlbTable->frameNumArr[i + 1];
            }
        }
    }
    else{
        for(i = i; i < nextTLBentry - 1; i++){
            tlbTable->pageNumArr[i] = tlbTable->pageNumArr[i + 1];
            tlbTable->frameNumArr[i] = tlbTable->frameNumArr[i + 1];
        }
        if(nextTLBentry < TLB_SIZE){
            tlbTable->pageNumArr[nextTLBentry] = pageNumber;
            tlbTable->frameNumArr[nextTLBentry] = frameNumber;

        }
        else{
            tlbTable->pageNumArr[nextTLBentry - 1] = pageNumber;
            tlbTable->frameNumArr[nextTLBentry - 1] = frameNumber;
        }
    }
    if(nextTLBentry < TLB_SIZE) {
        nextTLBentry++;
    }
}

void replacePageLRU(int pageNumber, int frameNumber)
{

    bool freeSpotFound = false;
    bool alreadyThere = false;
    int replaceIndex = -1;
    
    for (int i = 0; i < TLB_SIZE; i++) {
        if ((tlbTable->pageNumArr[i] != pageNumber) && (tlbTable->pageNumArr[i] != 0)) {
            tlbTable->entryAgeArr[i]++;
        }
        else if ((tlbTable->pageNumArr[i] != pageNumber) && (tlbTable->pageNumArr[i] == 0)) {
            if (!freeSpotFound) {
                replaceIndex = i;
                freeSpotFound = true;
            }
        }
        else if (tlbTable->pageNumArr[i] == pageNumber) {
            if(!alreadyThere) {
                tlbTable->entryAgeArr[i] = 0;
                alreadyThere = true;
            }
        }
    }

    if (alreadyThere) {
        return;
    }
    else if (freeSpotFound) { 
        tlbTable->pageNumArr[replaceIndex] = pageNumber; 
        tlbTable->frameNumArr[replaceIndex] = frameNumber;
        tlbTable->entryAgeArr[replaceIndex] = 0;
    }
    else { 
        replaceIndex = getOldestEntry(TLB_SIZE);
        tlbTable->pageNumArr[replaceIndex] = pageNumber;    
        tlbTable->frameNumArr[replaceIndex] = frameNumber;
        tlbTable->entryAgeArr[replaceIndex] = 0;

    }
}

int getOldestEntry(int tlbSize) {
  int i, max, index;

  max = tlbTable->entryAgeArr[0];
  index = 0;

  for (i = 1; i < tlbSize; i++) {
    if (tlbTable->entryAgeArr[i] > max) {
       index = i;
       max = tlbTable->entryAgeArr[i];
    }
  }
  return index;
}

double getAvgTimeInBackingStore() {
    double temp = (double) cpu_time_used / (double) functionCallCount;
    return temp * 1000000;
}
