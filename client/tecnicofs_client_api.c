#include "tecnicofs_client_api.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h> // Not sure se este é preciso


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
    size_t size_written = 0;
    char buff[PIPE_PATH_SIZE + 1] = {'\0'};

    unlink(client_pipe_path);

    if (mkfifo(client_pipe_path, 0640) < 0) {
        return -1;
    }

    if ((fcli = fopen(client_pipe_path, "r+")) == NULL) {
        /* Failed to open client's pipe */
        return -1;
    }

    if ((fserv = fopen(server_pipe_path, "w")) == NULL) { //Talvez read write?
        /* Server named pipe not created */
        return -1;
    }

    /* Write to the server's named pipe the path to the client side of the
     * pipe */

    char op_code = TFS_OP_CODE_MOUNT;

    buff[0] = op_code;
    strncpy(buff + 1, client_pipe_path, PIPE_PATH_SIZE - 1);
    printf("Enviou pedido\n");
    size_written = fwrite(buff, 1, sizeof(buff), fserv);
    if (size_written != sizeof(buff)) {
        /* Error occured or nothing was written */
        return -1;
    }
    
    if (fclose(fserv) != 0) {
        return -1;
    }   


    printf("Vai ler pedido\n");
    /* Read from client's named pipe the assigned session id */
    if (fread(&id, 1, sizeof(int), fcli) != sizeof(int)) {
        /* Error occured or nothing was read */
        return -1;
    }
    printf("Já leu\n");

    if (id == -1) {
        /* Failed to mount to tfs server */
        return -1;
    }


    /* Update client info */
    session_id = id;
    pipe_path = client_pipe_path;
    server_pipe = server_pipe_path;


    return 0;
}

int tfs_unmount() {
    /* TODO: Implement this */
    //TODO Concatenar tudo para mandar ao servidor
    //TODO usar uma função auxiliar para mandar a mensagem

    size_t size_written = 0;
    int operation_result;
    char buf[sizeof(char) + sizeof(int)] = {'\0'};

    if ((fserv = fopen(server_pipe, "w")) == NULL) {
        return -1;
    }

    /* Write OP code to server's named pipe */

    char op_code = TFS_OP_CODE_UNMOUNT;
    buf[0] = op_code;
    memcpy(buf + 1, &session_id, sizeof(int));
    size_written = fwrite(&buf, 1, sizeof(buf), fserv);
    if (size_written != sizeof(buf)) {
        /* Failed to write session id to server's named pipe */
        return -1;
    }

    if (fclose(fserv) != 0) {
        /* Failed to close server's named pipe */
        return -1;
    }

    /* Read server's answer */
    if (fread(&operation_result, 1, sizeof(int), fcli) != sizeof(int)) {
        /* Failed to read operation result from client's named pipe */
        return -1;
    }

    /* Close pipes */
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
    size_t size_written; /* OP code, id, name and flags */
    int operation_result;
    char buf[sizeof(char) + sizeof(int) + FILE_NAME_SIZE + sizeof(int)];

    if ((fserv = fopen(server_pipe, "w" )) == NULL) {
        return -1;
    }

    char opcode = TFS_OP_CODE_OPEN;
    buf[0] = opcode;
    memcpy(buf + 1, &session_id, sizeof(int));
    memcpy(buf + 1 + sizeof(int), name, FILE_NAME_SIZE);
    memcpy(buf + 1 + sizeof(int) + FILE_NAME_SIZE, &flags, sizeof(int));
    size_written = fwrite(&buf, 1, sizeof(buf), fserv);
    if (size_written != sizeof(buf)) {
        /* Failed to write session id to server's named pipe */
        return -1;
    }

    if (fclose(fserv) != 0) {
        return -1;
    }


    /* Read answer */
    if (fread(&operation_result, 1, sizeof(int), fcli) != sizeof(int)) {
        /* Failed to read operation result from client's named pipe */
        return -1;
    }

    return operation_result;
}

int tfs_close(int fhandle) {
    //TODO IMPLEMENTAR


    size_t size_written; /* OP code, id, name and flags */
    int operation_result;
    char buf[sizeof(char) + sizeof(int) + sizeof(int)];
    if ((fserv = fopen(server_pipe, "w" )) == NULL) {
        return -1;
    }

    char opcode = TFS_OP_CODE_CLOSE;
    buf[0] = opcode;
    memcpy(buf + 1, &session_id, sizeof(int));
    memcpy(buf + 1 + sizeof(int), &fhandle, sizeof(int));
    size_written = fwrite(&buf, 1, sizeof(buf), fserv);
    if (size_written != sizeof(buf)) {
        return -1;
    }

    if (fclose(fserv) != 0) {
        return -1;
    }

    /* Read answer */
    if (fread(&operation_result, 1, sizeof(int), fcli) != sizeof(int)) {
        return -1;
    }

    return operation_result;
}

ssize_t tfs_write(int fhandle, void const *buffer, size_t len) {
    //TODO implementar
    size_t size_written; /* OP code, id, name and flags */
    ssize_t operation_result;
    char buf[sizeof(char) + sizeof(int) + sizeof(int) + sizeof(size_t) + len];

    if ((fserv = fopen(server_pipe, "w" )) == NULL) {
        return -1;
    }

    char opcode = TFS_OP_CODE_WRITE;
    buf[0] = opcode;
    memcpy(buf + 1, &session_id, sizeof(int));
    memcpy(buf + 1 + sizeof(int), &fhandle, sizeof(int));
    memcpy(buf + 1 + sizeof(int) + sizeof(int), &len, sizeof(size_t));
    memcpy(buf + 1 + sizeof(int) + sizeof(int) + sizeof(size_t), buffer, len);
    size_written = fwrite(&buf, 1, sizeof(buf), fserv);
    if (size_written != sizeof(buf)) {
        return -1;
    }

    if (fclose(fserv) != 0) {
        return -1;
    }

    /* Read answer */
    if (fread(&operation_result, 1, sizeof(size_t), fcli) != sizeof(size_t)) {
        return -1;
    }

    return operation_result;
}

ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    size_t size_written;
    ssize_t operation_result;
    char buf[sizeof(char) + sizeof(int) + sizeof(int) + sizeof(size_t)];

    if ((fserv = fopen(server_pipe, "w" )) == NULL) {
        return -1;
    }

    char opcode = TFS_OP_CODE_READ;
    buf[0] = opcode;
    memcpy(buf + 1, &session_id, sizeof(int));
    memcpy(buf + 1 + sizeof(int), &fhandle, sizeof(int));
    memcpy(buf + 1 + sizeof(int) + sizeof(int), &len, sizeof(size_t));
    size_written = fwrite(&buf, 1, sizeof(buf), fserv);
    if (size_written != sizeof(buf)) {
        return -1;
    }

    if (fclose(fserv) != 0) {
        return -1;
    }

    /* Read answer */
    if (fread(&operation_result, 1, sizeof(ssize_t), fcli) != sizeof(ssize_t)) {
        return -1;
    }

    if (operation_result != -1) {
        //safe cast because operation result is always a positive number
        if (fread(buffer, sizeof(char), (size_t) operation_result, fcli) != (size_t) operation_result) {
            return -1;
        }
    }


    return operation_result;
}

int tfs_shutdown_after_all_closed() {
    size_t size_written;
    int operation_result;
    char buf[sizeof(char) + sizeof(int)];

    if ((fserv = fopen(server_pipe, "w" )) == NULL) {
        return -1;
    }

    char opcode = TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED;
    buf[0] = opcode;
    memcpy(buf + 1, &session_id, sizeof(int));
    size_written = fwrite(&buf, 1, sizeof(buf), fserv);
    if (size_written != sizeof(buf)) {
        return -1;
    }

    if (fclose(fserv) != 0) {
        return -1;
    }

    /* Read answer */
    if (fread(&operation_result, 1, sizeof(int), fcli) != sizeof(int)) {
        return -1;
    }


    return operation_result;
}