// Test that fork fails gracefully.
// Tiny executable so that the limit can be filling the proc table.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define NUM_CHILDREN 4

int
main(void)
{
    int pid[NUM_CHILDREN];
    int tickets[NUM_CHILDREN] = {10, 20, 40, 80};

    for (int i = 0; i < NUM_CHILDREN; i++) {
        pid[i] = fork();
        if (pid[i] < 0) {
            printf("Fork failed!\n");
            exit(1);
        }

        if (pid[i] == 0) {
            int ret = ticket(getpid(), tickets[i]);
            if (ret != 0) {
                printf("Setting ticket value failed!\n");
                exit(1);
            }

            volatile int counter = 0;
            int start = uptime();
            while(uptime() - start < 200){
                counter++;
            }

            printf(" %d\t\t %d\t\t %d\n", getpid(), tickets[i], counter);
            exit(0);
        }
    }

    printf("PID\t|\tTicket\t|\tCounter\n");
    for(int i = 0; i < NUM_CHILDREN; i++){
        wait(0);
    }

  exit(0);
}
