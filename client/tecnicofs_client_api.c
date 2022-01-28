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
static char const *server_pipe = NULL;
static FILE *fserv, *fcli;

int tfs_mount(char const *client_pipe_path, char const *server_pipe_path) {
    int id;
    // FILE *fserv, *fcli; 
    size_t path_size, size_written = 0;
    char buff[PIPE_PATH_SIZE + 1] = {'\0'};

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
    char op_code = TFS_OP_CODE_MOUNT;
    buff[0] = op_code;
    strncpy(buff + 1, client_pipe_path, path_size);
    size_written = fwrite(buff, 1, sizeof(buff), fserv);
    if (size_written != sizeof(buff)) {
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
    // printf("O operation result é %d\n", id);
    if (id == -1) {
        /* Failed to mount to tfs server */
        return -1;
    }


    /* Update client info */
    session_id = id;
    pipe_path = client_pipe_path;
    server_pipe = server_pipe_path;

    // printf("Cliente tfs mount: Operação concluída com sucesso.\n");

    return 0;
}

int tfs_unmount() {
    /* TODO: Implement this */
    //TODO Concatenar tudo para mandar ao servidor
    //TODO usar uma função auxiliar para mandar a mensagem

    size_t size_written = 0;
    int operation_result;
    char buf[5] = {'\0'};

    // printf("Cliente tfs unmount: Vamos começar a operação\n");


    if ((fserv = fopen(server_pipe, "w")) == NULL) {
        return -1;
    }

    /* Write OP code to server's named pipe */

    char op_code = TFS_OP_CODE_UNMOUNT;
    buf[0] = op_code;
    memccpy(buf + 1, &session_id, sizeof(int), sizeof(int));
    size_written = fwrite(&buf, 1, sizeof(buf), fserv);
    if (size_written != sizeof(buf)) {
        /* Failed to write session id to server's named pipe */
        return -1;
    }

    // printf("Cliente tfs unmount: Vamos fechar o pipe do servidor\n");
    if (fclose(fserv) != 0) {
        /* Failed to close server's named pipe */
        return -1;
    }

    /* Read server's answer */
    // printf("Cliente tfs unmount: Vamos ler o resultado da operação\n");
    fread(&operation_result, sizeof(int), 1, fcli);

    /* Close pipes */
    // printf("Cliente tfs unmount: Vamos fechar o pipe do cliente\n");
    if (fclose(fcli) != 0) {
        /* Failed to close client's named pipe */
        return -1;
    }

    /* Remove client's path */
    unlink(pipe_path);

    return operation_result;
}

int tfs_open(char const *name, int flags) {
    /* TODO: Implement this */
    // printf("Cliente tfs_open: Início da operação\n");
    size_t size_written; /* OP code, id, name and flags */
    // char OP_CODE[2], id[2], flag[2];
    int operation_result;
    char buf[sizeof(char) + sizeof(int) + FILE_NAME_SIZE + sizeof(int)];

    if ((fserv = fopen(server_pipe, "w" )) == NULL) {
        return -1;
    }

    char opcode = TFS_OP_CODE_OPEN;
    buf[0] = opcode;
    memccpy(buf + 1, &session_id, sizeof(int), sizeof(int));
    memccpy(buf + 1 + sizeof(int), name, FILE_NAME_SIZE, FILE_NAME_SIZE);
    memccpy(buf + 1 + sizeof(int) + FILE_NAME_SIZE, &flags, sizeof(int), sizeof(int));
    size_written = fwrite(&buf, 1, sizeof(buf), fserv);

    // // // printf("Cliente tfs open: Já escrevemos tudo, vamos fechar o pipe do servidor\n");
    if (fclose(fserv) != 0) {
        return -1;
    }

    // // // printf("Cliente tfs open: Vamos ler a resposta do servidor\n");

    /* Read answer */
    fread(&operation_result, sizeof(int), 1, fcli);
        //Falta o check for error da syscall/se leu o suficiente

    // // // printf("Cliente tfs_open: O resultado foi %d\n", operation_result);

    return operation_result;
}

int tfs_close(int fhandle) {
    //TODO IMPLEMENTAR

    // printf("Cliente tfs close: Iniciar operação\n");

    size_t size_written; /* OP code, id, name and flags */
    int operation_result;
    char buf[sizeof(char) + sizeof(int) + sizeof(int)];
    // printf("Cliente tfs close: Vamos abrir o pipe do servidor em write\n");
    if ((fserv = fopen(server_pipe, "w" )) == NULL) {
        return -1;
    }

    // printf("Cliente tfs close: Vamos escrever o opcode\n");
    char opcode = TFS_OP_CODE_CLOSE;
    buf[0] = opcode;
    memccpy(buf + 1, &session_id, sizeof(int), sizeof(int));
    memccpy(buf + 1 + sizeof(int), &fhandle, sizeof(int), sizeof(int));
    size_written = fwrite(&buf, 1, sizeof(buf), fserv);
    if (size_written != sizeof(buf)) {
        return -1;
    }

    // printf("Cliente tfs close: Vamos fechar o pipe do servidor\n");
    if (fclose(fserv) != 0) {
        return -1;
    }


    // printf("Cliente tfs close: Vamos ler o resultado\n");
    /* Read answer */
    fread(&operation_result, sizeof(int), 1, fcli);
        //Falta o check for error da syscall/se leu o suficiente

    // // // printf("Cliente tfs_open: O resultado foi %d\n", operation_result);

    // printf("Cliente tfs close: Operação terminada\n");

    return operation_result;
}

ssize_t tfs_write(int fhandle, void const *buffer, size_t len) {
    //TODO implementar

    // printf("Cliente tfs write: Iniciar operação\n");

    size_t size_written; /* OP code, id, name and flags */
    ssize_t operation_result;
    char buf[sizeof(char) + sizeof(int) + sizeof(int) + sizeof(size_t) + len];

    // printf("Cliente tfs write: Vamos abrir o pipe do servidor em write\n");
    if ((fserv = fopen(server_pipe, "w" )) == NULL) {
        return -1;
    }

    // printf("Cliente tfs write: Vamos escrever o opcode\n");
    char opcode = TFS_OP_CODE_WRITE;
    buf[0] = opcode;
    memccpy(buf + 1, &session_id, sizeof(int), sizeof(int));
    memccpy(buf + 1 + sizeof(int), &fhandle, sizeof(int), sizeof(int));
    memccpy(buf + 1 + sizeof(int) + sizeof(int), &len, sizeof(size_t), sizeof(size_t));
    memccpy(buf + 1 + sizeof(int) + sizeof(int) + sizeof(size_t), buffer, len, len);
    size_written = fwrite(&buf, 1, sizeof(buf), fserv);
    if (size_written != sizeof(buf)) {
        return -1;
    }

    // printf("Cliente tfs write: Vamos fechar o pipe do servidor\n");
    if (fclose(fserv) != 0) {
        return -1;
    }

    // printf("Cliente tfs write: Vamos ler o resultado\n");
    /* Read answer */
    fread(&operation_result, sizeof(ssize_t), 1, fcli);


    // printf("Cliente tfs write: Operação terminada\n");

    return operation_result;
}

ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    // printf("Cliente tfs read: Iniciar operação\n");

    size_t size_written;
    ssize_t operation_result;
    char buf[sizeof(char) + sizeof(int) + sizeof(int) + sizeof(size_t)];

    // printf("Cliente tfs read: Vamos abrir o pipe do servidor em write\n");
    if ((fserv = fopen(server_pipe, "w" )) == NULL) {
        return -1;
    }

    // printf("Cliente tfs read: Vamos escrever o opcode\n");
    char opcode = TFS_OP_CODE_READ;
    buf[0] = opcode;
    memccpy(buf + 1, &session_id, sizeof(int), sizeof(int));
    memccpy(buf + 1 + sizeof(int), &fhandle, sizeof(int), sizeof(int));
    memccpy(buf + 1 + sizeof(int) + sizeof(int), &len, sizeof(size_t), sizeof(size_t));
    size_written = fwrite(&buf, 1, sizeof(buf), fserv);
    if (size_written != sizeof(buf)) {
        return -1;
    }

    // printf("Cliente tfs read: Vamos fechar o pipe do servidor\n");
    if (fclose(fserv) != 0) {
        return -1;
    }

    // printf("Cliente tfs read: Vamos ler o resultado\n");
    /* Read answer */
    fread(&operation_result, sizeof(ssize_t), 1, fcli);

    if (operation_result != -1) {
        /* if something was read */
        fread(buffer, sizeof(char), operation_result, fcli);
    }

    // printf("\n\n Oq foi lido foi %s\n\n", (char *) buffer);

    return operation_result;
}

int tfs_shutdown_after_all_closed() {
    // printf("Cliente tfs shutdown: Iniciar operação\n");

    size_t size_written;
    int operation_result;
    char buf[sizeof(char) + sizeof(int)];

    // printf("Cliente tfs shutdown: Vamos abrir o pipe do servidor em write\n");
    if ((fserv = fopen(server_pipe, "w" )) == NULL) {
        return -1;
    }

    // printf("Cliente tfs shutdown: Vamos escrever o opcode\n");
    char opcode = TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED;
    buf[0] = opcode;
    memccpy(buf + 1, &session_id, sizeof(int), sizeof(int));
    size_written = fwrite(&buf, 1, sizeof(buf), fserv);
    if (size_written != sizeof(buf)) {
        return -1;
    }

    // printf("Cliente tfs shutdown: Vamos fechar o pipe do servidor\n");
    if (fclose(fserv) != 0) {
        return -1;
    }

    // printf("Cliente tfs shutdown: Vamos ler o resultado\n");
    /* Read answer */
    fread(&operation_result, sizeof(int), 1, fcli);


    return operation_result;
}
