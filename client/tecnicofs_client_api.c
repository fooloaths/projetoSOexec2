#include "tecnicofs_client_api.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>


static int session_id = -1;
static char const *pipe_path = NULL;
static char const *server_pipe = NULL;
static FILE *fserv, *fcli;

int tfs_mount(char const *client_pipe_path, char const *server_pipe_path) {
    signal(SIGPIPE, SIG_IGN);
    int id;
    size_t size_written = 0;
    char buff[PIPE_PATH_SIZE + 1] = {'\0'};

    if (unlink(client_pipe_path) == -1) {
        if (errno != 2) {
            /* if errno is 2, than unlink simply failed because the pipe was
             * properly deleted at the end of the last session*/
            return -1;
        }
    }

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
    strncpy(buff + sizeof(char), client_pipe_path, PIPE_PATH_SIZE - 1);
    size_written = fwrite(buff, 1, sizeof(buff), fserv);
    if (size_written != sizeof(buff)) {
        /* Error occured or nothing was written */
        return -1;
    }
    
    if (fclose(fserv) != 0) {
        return -1;
    }   


    /* Read from client's named pipe the assigned session id */
    if (fread(&id, 1, sizeof(int), fcli) != sizeof(int)) {
        /* Error occured or nothing was read */
        return -1;
    }

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
    signal(SIGPIPE, SIG_IGN);

    size_t size_written = 0;
    int operation_result;
    char buf[sizeof(char) + sizeof(int)] = {'\0'};

    if ((fserv = fopen(server_pipe, "w")) == NULL) {
        return -1;
    }

    /* Write OP code to server's named pipe */

    char op_code = TFS_OP_CODE_UNMOUNT;
    buf[0] = op_code;
    memcpy(buf + sizeof(char), &session_id, sizeof(int));
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
    if (unlink(pipe_path) == -1) {
        return -1;
    }

    return operation_result;
}

int tfs_open(char const *name, int flags) {
    signal(SIGPIPE, SIG_IGN);

    char fake_name[FILE_NAME_SIZE] = {'\0'};
    size_t size_written; /* OP code, id, name and flags */
    int operation_result;
    char buf[sizeof(char) + sizeof(int) + FILE_NAME_SIZE + sizeof(int)];

    if ((fserv = fopen(server_pipe, "w" )) == NULL) {
        return -1;
    }
    
    strcpy(fake_name, name);

    char opcode = TFS_OP_CODE_OPEN;
    buf[0] = opcode;
    memcpy(buf + sizeof(char), &session_id, sizeof(int));
    memcpy(buf + sizeof(char) + sizeof(int), fake_name, FILE_NAME_SIZE);
    memcpy(buf + sizeof(char) + sizeof(int) + FILE_NAME_SIZE, &flags, sizeof(int));
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
    signal(SIGPIPE, SIG_IGN);


    size_t size_written;
    int operation_result;
    char buf[sizeof(char) + sizeof(int) + sizeof(int)];
    if ((fserv = fopen(server_pipe, "w" )) == NULL) {
        return -1;
    }

    char opcode = TFS_OP_CODE_CLOSE;
    buf[0] = opcode;
    memcpy(buf + sizeof(char), &session_id, sizeof(int));
    memcpy(buf + sizeof(char) + sizeof(int), &fhandle, sizeof(int));
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
    signal(SIGPIPE, SIG_IGN);

    
    size_t size_written;
    ssize_t operation_result;

    if ((sizeof(char) + sizeof(int) + sizeof(int) + sizeof(size_t) + len) > PIPE_BUF) {
        /* If trying to write more than is safe for a pipe */
        len = PIPE_BUF - (sizeof(char) + sizeof(int) + sizeof(int) + sizeof(size_t));
    }
    char buf[sizeof(char) + sizeof(int) + sizeof(int) + sizeof(size_t) + len];



    if ((fserv = fopen(server_pipe, "w" )) == NULL) {
        return -1;
    }

    char opcode = TFS_OP_CODE_WRITE;
    buf[0] = opcode;
    memcpy(buf + sizeof(char), &session_id, sizeof(int));
    memcpy(buf + sizeof(char) + sizeof(int), &fhandle, sizeof(int));
    memcpy(buf + sizeof(char) + sizeof(int) + sizeof(int), &len, sizeof(size_t));
    memcpy(buf + sizeof(char) + sizeof(int) + sizeof(int) + sizeof(size_t), buffer, len);
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
    signal(SIGPIPE, SIG_IGN);

    size_t size_written;
    ssize_t operation_result;
    char buf[sizeof(char) + sizeof(int) + sizeof(int) + sizeof(size_t)];

    if ((fserv = fopen(server_pipe, "w" )) == NULL) {
        return -1;
    }

    if (len > PIPE_BUF) {
        len = PIPE_BUF;
    }

    char opcode = TFS_OP_CODE_READ;
    buf[0] = opcode;
    memcpy(buf + sizeof(char), &session_id, sizeof(int));
    memcpy(buf + sizeof(char) + sizeof(int), &fhandle, sizeof(int));
    memcpy(buf + sizeof(char) + sizeof(int) + sizeof(int), &len, sizeof(size_t));
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
    signal(SIGPIPE, SIG_IGN);

    size_t size_written;
    int operation_result;
    char buf[sizeof(char) + sizeof(int)];

    if ((fserv = fopen(server_pipe, "w" )) == NULL) {
        return -1;
    }

    char opcode = TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED;
    buf[0] = opcode;
    memcpy(buf + sizeof(char), &session_id, sizeof(int));
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