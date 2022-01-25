#include "tecnicofs_client_api.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
// // #include <unistd.h> // Not sure se este é preciso


//TODO Pensar nas operações se o cliente já tivesse mounted/unmounted?
//TODO talvez função para concatenar argumentos e enviar o pedido?

#define MOUNT_OP_CODE (char) 1
#define PIPE_NAME_SIZE 40


static int session_id = -1;
static char const *pipe_path = NULL;
static FILE *fserv, *fcli;

int tfs_mount(char const *client_pipe_path, char const *server_pipe_path) {
    int id;
    // FILE *fserv, *fcli; 
    size_t path_size, size_written = 0;
    char buff[PIPE_PATH_SIZE] = {'\0'};

    unlink(client_pipe_path);

    // printf("Cliente tfs mount: Vamos tentar fazer mkfifo do pipe do cliente\n");
    if (mkfifo(client_pipe_path, 0640) < 0) {
        /* Failed to create client's named pipe */
        printf("Cliente tfs_mount: Falhou ao tentar criar o pipe do cliente\n");
        return -1;
    }

    // printf("Cliente tfs mount: Vamos tentar abrir o pipe do cliente\n");
    if ((fcli = fopen(client_pipe_path, "r+")) == NULL) {
        /* Failed to open client's pipe */
        printf("Cliente tfs_mount: Falhou ao tentar abrir o pipe do cliente");
        return -1;
    }

    // printf("Cliente tfs mount: Vamos começar a operação\n");
    // printf("Cliente tfs mount: Vamos tentar abrir o pipe do servidor\n");
    // printf("Cliente tfs_mount: Esse path é %s\n", server_pipe_path);
    if ((fserv = fopen(server_pipe_path, "w")) == NULL) { //Talvez read write?
        /* Server named pipe not created */
        printf("Cliente tfs_mount: Falhou ao tentar abrir o pipe do servidor\n");
        return -1;
    }

    /* Write to the server's named pipe the path to the client side of the
     * pipe */
    path_size = strlen(client_pipe_path) + 1; /* Take into account \0 */
    int op_code = TFS_OP_CODE_MOUNT;
    printf("Cliente tfs mount: vamos escrever para o pipe o opcode\n");
    size_written = fwrite(&op_code, sizeof(int), 1, fserv);
    // // // if (size_written <= 0) {
    if ((size_written * sizeof(int)) < sizeof(int)) {
        /* Error occured */
        return -1;
    }

    // // // size_written = write(fserv, client_pipe_path, path_size);
    strncpy(buff, client_pipe_path, path_size);
    // printf("Cliente tfs mount: Vamos escrever para o pipe do servidor o client pipe path\n");
    size_written = fwrite(buff, sizeof(int), 40, fserv);
    // printf("O buffer é %s\n", buff);
    // size_written = fwrite(client_pipe_path, sizeof(int), 40, fserv); //Falta meter '\0s até encher os 40 slots
    // printf("\nESCREVERAM-SE %ld bytes\n\n", size_written);
    // // // if (size_written <= 0) {
    if ((size_written * sizeof(int)) < (40 * sizeof(int))) {
        /* Error occured or nothing was written */
        return -1;
    }

    // printf("Cliente tfs mount: Vamos fechar o pipe do servidor\n");
    if (fclose(fserv) != 0) {
        return -1;
    }

    // // // // printf("Cliente tfs mount: Vamos tentar fazer mkfifo do pipe do cliente\n");
    // // // // if (mkfifo(client_pipe_path, 0777) < 0) {
    // // // //     /* Failed to create client's named pipe */
    // // // //     printf("Cliente tfs_mount: Falhou ao tentar criar o pipe do cliente\n");
    // // // //     return -1;
    // // // // }

    // // // // printf("Cliente tfs mount: Vamos tentar abrir o pipe do cliente\n");
    // // // // if ((fcli = fopen(client_pipe_path, "r+")) == NULL) {
    // // // //     /* Failed to open client's pipe */
    // // // //     printf("Cliente tfs_mount: Falhou ao tentar abrir o pipe do cliente");
    // // // //     return -1;
    // // // // }

    /* Read from client's named pipe the assigned session id */
    // printf("Cliente tfs mount: Vamos tentar ler a resposta do servidor\n");
    fread(&id, sizeof(int), 1, fcli);
    printf("O operation result é %d\n", id);
    if (id == -1) {
        /* Failed to mount to tfs server */
        return -1;
    }


    /* Update client info */
    session_id = id;
    pipe_path = client_pipe_path;

    printf("Cliente tfs mount: Operação concluída com sucesso.\n");

    return 0;
}

int tfs_unmount() {
    /* TODO: Implement this */
    //TODO Concatenar tudo para mandar ao servidor
    //TODO usar uma função auxiliar para mandar a mensagem
    size_t size_written = 0;
    int operation_result;

    printf("Cliente tfs unmount: Vamos começar a operação\n");

    /* Write OP code to server's named pipe */

    int op_code = TFS_OP_CODE_MOUNT;
    printf("Cliente tfs unmount: vamos escrever para o pipe o opcode\n");
    size_written = fwrite(&op_code, sizeof(int), 1, fserv);
    if ((size_written * sizeof(int)) < sizeof(int)) {
        /* Failed to write OP code to server's named pipe */
        return -1;
    }

    /* Write session id to pipe */
    printf("Cliente tfs unmount: Vamos escrever o session id para o pipe\n");
    size_written = fwrite(&session_id, sizeof(int), 1, fserv);
    if ((size_written * sizeof(int)) < sizeof(int)) {
        /* Failed to write session id to server's named pipe */
        return -1;
    }


    /* Read server's answer */
    fread(&operation_result, sizeof(int), 1, fcli);

    /* Close pipes */

    if (fclose(fcli) != 0) {
        /* Failed to close client's named pipe */
        return -1;
    }

    if (fclose(fserv) != 0) {
        /* Failed to close server's named pipe */
        return -1;
    }

    /* Remove client's path */
    unlink(pipe_path);

    return operation_result;
}

int tfs_open(char const *name, int flags) {
    /* TODO: Implement this */
    printf("Cliente tfs_open: Início da operação\n");
    size_t size_written, size = 1 + 1 + FILE_NAME_SIZE + 1; /* OP code, id, name and flags */
    char *message = (char *) malloc(sizeof(char) * (size + 1));
    // char OP_CODE[2], id[2], flag[2];
    int operation_result;

    if (message == NULL) {
        return -1;
    }
    // message[0] = '\0';
    // OP_CODE[0] = TFS_OP_CODE_OPEN + '0'; OP_CODE[1] = '\0'; //TODO Este código está feio que doi
    // id[0] = session_id + '0'; id[1] = '\0';                 //TODO Perguntar ao prof se há maneira mais bonita de fazer isto
    // flag[0] = flags + '0'; flag[1] = '\0';

    // strcat(message, OP_CODE);
    // strcat(message, id);
    message[0] = TFS_OP_CODE_OPEN + '0';
    message[1] = session_id + '0';
    message[2] = '\0';
    strcat(message, name); //Ver se esta linha está a copiar os 40 bytes do nome e não só até ao '\0'
    message[2 + FILE_NAME_SIZE + 1] = flags + '0'; //TODO será que isto preserva a representação binária da flag? (Perguntar ao prof)
    // strcat(message, flag);

    /* Send request */
    size_written = fwrite(message, sizeof(char), size, fserv);
    if (size_written < size) {
        return -1;
    }

    /* Read answer */
    fread(&operation_result, sizeof(int), 1, fcli);
        //Falta o check for error da syscall/se leu o suficiente

    return operation_result;
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
