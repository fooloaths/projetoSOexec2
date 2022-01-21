#include "tecnicofs_client_api.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
// // #include <unistd.h> // Not sure se este é preciso


#define MOUNT_OP_CODE (char) 1
#define PIPE_NAME_SIZE 40


static int session_id = -1;
char const *pipe_path = NULL;
/*typedef struct s_id {
    int id;
    char const *pipe_path;
} session_id;

static session_id *client;

int session_init(int identification, char const *client_pipe_path) {
    client = (session_id *) malloc(sizeof(session_id));
    
    if (client == NULL) {
        return -1;
    }

    session_id->id = identification;
    session_id->pipe_path = client_pipe_path;

    return 0;
}*/


int tfs_mount(char const *client_pipe_path, char const *server_pipe_path) {
    int id;
    FILE *fserv, *fcli; 
    size_t path_size, size_written = 0;
    printf("Cliente tfs mount: Vamos começar a operação\n");
    printf("Cliente tfs mount: Vamos tentar abrir o pipe do servidor\n");
    if ((fserv = fopen(server_pipe_path, "w")) == NULL) { //Talvez read write?
        /* Server named pipe not created */
        printf("Cliente tfs_mount: Falhou ao tentar abrir o pipe do servidor\n");
        return -1;
    }

    // // // unlink(client_pipe_path); /* Não sei se isto é preciso. Perguntar ao prof/piazza */

    printf("Cliente tfs mount: Vamos tentar fazer mkfifo do pipe do cliente\n");
    if (mkfifo(client_pipe_path, 0777) < 0) {
        /* Failed to create client's named pipe */
        printf("Cliente tfs_mount: Falhou ao tentar criar o pipe do cliente\n");
        return -1;
    }

    printf("Cliente tfs mount: Vamos tentar abrir o pipe do cliente\n");
    if ((fcli = fopen(client_pipe_path, "r+")) == NULL) {
        /* Failed to open client's pipe */
        printf("Cliente tfs_mount: Falhou ao tentar abrir o pipe do cliente");
        return -1;
    }

    /* Write to the server's named pipe the path to the client side of the
     * pipe */
    path_size = strlen(client_pipe_path) + 1; /* Take into account \0 */
    int op_code = TFS_OP_CODE_MOUNT;
    printf("Cliente tfs mount: vamos escrever para o pipe o opcode\n");
    size_written = fwrite(&op_code, sizeof(int), 1, fserv);
    // // size_written = write(fserv, TFS_OP_CODE_MOUNT, sizeof(char));
    if (size_written <= 0) {
        /* Error occured */
        return -1;
    }

    // // size_written = write(fserv, client_pipe_path, path_size);
    printf("Cliente tfs mount: Vamos escrever para o pipe o client pipe path\n");
    size_written = fwrite(client_pipe_path, sizeof(int), 40, fserv); //Falta meter '\0s até encher os 40 slots

    if (size_written <= 0) {
        /* Error occured or nothing was written */
        return -1;
    }

    /* Read from client's named pipe the assigned session id */
    printf("Cliente tfs mount: Vamos tentar ler a resposta do servidor\n");
    fread(&id, sizeof(int), 1, fcli);
    if (id == -1) {
        /* Failed to mount to tfs server */
        return -1;
    }
    
    printf("Cliente tfs mount: Vamos definir o session id\n");
    session_id = id;
    pipe_path = client_pipe_path;

    printf("Cliente tfs mount: Operação concluída com sucesso.\n");

    return 0;
}

int tfs_unmount() {
    /* TODO: Implement this */
    return -1;
}

int tfs_open(char const *name, int flags) {
    /* TODO: Implement this */
    return -1;
}

int tfs_close(int fhandle) {
    // // if (tfs_close(fhandle) == )
    return -1;
}

ssize_t tfs_write(int fhandle, void const *buffer, size_t len) {
    /* TODO: Implement this */
    return -1;
}

ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    /* TODO: Implement this */
    return -1;
}

int tfs_shutdown_after_all_closed() {
    /* TODO: Implement this */
    return -1;
}
