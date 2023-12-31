#include "aesd-circular-buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

int main(int argc, char **argv)
{
    struct aesd_circular_buffer buf;
    aesd_circular_buffer_init(&buf);

    struct aesd_buffer_entry e1 = {.buffptr="eins", .size=strlen("eins")};
    aesd_circular_buffer_add_entry(&buf, &e1);
    struct aesd_buffer_entry e2 = {.buffptr="zwo"};
    aesd_circular_buffer_add_entry(&buf, &e2);
    struct aesd_buffer_entry e3 = {.buffptr="drei"};
    aesd_circular_buffer_add_entry(&buf, &e3);
    struct aesd_buffer_entry e4 = {.buffptr="vier"};
    aesd_circular_buffer_add_entry(&buf, &e4);
    struct aesd_buffer_entry e5 = {.buffptr="fünf"};
    aesd_circular_buffer_add_entry(&buf, &e5);
    struct aesd_buffer_entry e6 = {.buffptr="sechs"};
    aesd_circular_buffer_add_entry(&buf, &e6);
    struct aesd_buffer_entry e7 = {.buffptr="sieben"};
    aesd_circular_buffer_add_entry(&buf, &e7);

    uint32_t index = 0;
    char testresult1[1000] = {0};
    struct aesd_buffer_entry *entry;
    AESD_CIRCULAR_BUFFER_FOREACH(entry,&buf,index){
        if(entry->buffptr != NULL){
            strcat(testresult1, entry->buffptr);
            entry->size = strlen(entry->buffptr);
        }
    }
    assert(strcmp(testresult1,"einszwodreivierfünfsechssieben") == 0);
    printf("test successful -> einszwodreivierfünfsechssieben\n");
    
    struct aesd_buffer_entry e8 = {.buffptr="acht"};
    aesd_circular_buffer_add_entry(&buf, &e8);
    struct aesd_buffer_entry e9 = {.buffptr="neun"};
    aesd_circular_buffer_add_entry(&buf, &e9);
    struct aesd_buffer_entry e10 = {.buffptr="zehn"};
    aesd_circular_buffer_add_entry(&buf, &e10);
    
    index = 0;
    char testresult2[1000] = {0};
    struct aesd_buffer_entry *entry2;
    AESD_CIRCULAR_BUFFER_FOREACH(entry2,&buf,index){
        if(entry2->buffptr != NULL)
            strcat(testresult2, entry2->buffptr);
    }
    assert(strcmp(testresult2,"einszwodreivierfünfsechssiebenachtneunzehn") == 0);
    printf("test successful -> einszwodreivierfünfsechssiebenachtneunzehn\n");
    
    struct aesd_buffer_entry e11 = {.buffptr="elf"};
    aesd_circular_buffer_add_entry(&buf, &e11);
    index = 0;
    char testresult3[1000] = {0};
    struct aesd_buffer_entry *entry3;
    AESD_CIRCULAR_BUFFER_FOREACH(entry3,&buf,index){
        if(entry3->buffptr != NULL){
            strcat(testresult3, entry3->buffptr);
        }
    }
    assert(strcmp(testresult3,"elfzwodreivierfünfsechssiebenachtneunzehn") == 0);
    printf("test successful -> elfzwodreivierfünfsechssiebenachtneunzehn\n");


    size_t pos;
    struct aesd_buffer_entry *result = aesd_circular_buffer_find_entry_offset_for_fpos(&buf, 0, &pos);
    assert(strcmp(result->buffptr, "zwo") == 0);
    
    result = aesd_circular_buffer_find_entry_offset_for_fpos(&buf, 10, &pos);
    assert(strcmp(result->buffptr, "vier") == 0);
    assert(pos == 3);

    return EXIT_SUCCESS;
}