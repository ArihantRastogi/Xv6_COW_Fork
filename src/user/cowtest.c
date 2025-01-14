#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PAGESIZE 4096
#define ITERATIONS 1000

void read_test(int *array, int size) {
    int sum = 0;
    for (int i = 0; i < size; i++) {
        sum += array[i]; // Read-only access
    }
}

void write_test(int *array, int size) {
    for (int i = 0; i < size; i++) {
        array[i] = i; // Write access, triggers COW
    }
}

int main() {
    int *memory = (int *)sbrk(PAGESIZE * 10); // Allocate some pages for testing
    int pid;

    // Test 1: Read-only fork
    pid = fork();
    if (pid == 0) { // Child process
        read_test(memory, PAGESIZE * 10 / sizeof(int));
        exit(0);
    }
    wait(0);
    printf("Read Test completed\n");

    // Test 2: Write to memory in forked process
    pid = fork();
    if (pid == 0) { // Child process
        write_test(memory, PAGESIZE * 10 / sizeof(int));
        exit(0);
    }
    wait(0);
    printf("Write Test completed\n");


    printf("Tests completed\n");
    exit(0);
}
