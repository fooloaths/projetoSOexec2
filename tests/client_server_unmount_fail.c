#include "client/tecnicofs_client_api.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/*  This test is similar to test1.c from the 1st exercise.
    The main difference is that this one explores the
    client-server architecture of the 2nd exercise. */

int main(int argc, char **argv) {



    if (argc < 3) {
        printf("You must provide the following arguments: 'client_pipe_path "
               "server_pipe_path'\n");
        return 1;
    }

    assert(tfs_unmount() == -1);

    printf("Successful test.\n");

    return 0;
}
