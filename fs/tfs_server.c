#include "operations.h"
#include "common/common.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>


#define S 20
#define FREE 1
#define TAKEN 0

struct request {
    char op_code;
    int session_id;
    int flags;
    char buffer[FILE_NAME_SIZE];
    char *dynamic_buffer;
    int fhandle;
    size_t len;
};

static FILE *fclis[S];
static const char *server_pipe_name;
static int session_ids[S];
static char client_pipes[S][PIPE_PATH_SIZE];
static pthread_mutex_t id_table_mutex;
static pthread_t threads[S];
static pthread_mutex_t client_mutexes[S];
static pthread_cond_t client_cond_var[S];

static struct request prod_cons_buffer[S][1];

void* tfs_server_thread(void *);
int tfs_mount(char *path);
int tfs_unmount(int id);

int server_init() {
    tfs_init();

    if (pthread_mutex_init(&id_table_mutex, NULL) != 0) {
        return -1;
    }

    for (size_t i = 0; i < S; i++) {
        session_ids[i] = FREE;

        fclis[i] = NULL;

        if (pthread_mutex_init(&client_mutexes[i], NULL) != 0) {
            return -1;
        }

        if (pthread_cond_init(&client_cond_var[i], NULL) != 0) {
            return -1;
        }

        prod_cons_buffer[i][0].op_code = -1;
        size_t *i_pointer = malloc(sizeof(*i_pointer));
        if (i_pointer == NULL) {
            return -1;
        }
        *i_pointer = i;
        if (pthread_create(&threads[i], NULL, tfs_server_thread, (void*)i_pointer) != 0) {
            return -1;
        }

        for (size_t j = 0; j < PIPE_PATH_SIZE; j++) {
            client_pipes[i][j] = '\0';
        }
    }

    return 0;
}

int get_free_session_id() {
    int id = -1;
    if (pthread_mutex_lock(&id_table_mutex) != 0) {
        return -1;
    }

    for (int i = 0; i < S; i++) {
        if (session_ids[i] == FREE) {
            id = i;
            break;
        }
    }
    if (pthread_mutex_unlock(&id_table_mutex) != 0) {
        return -1;
    }

    return id;
}

int valid_id(int id) {
    return id >= 0 && id < S;
}

int session_id_is_free(int id) {
    if (pthread_mutex_lock(&id_table_mutex) != 0) {
        return -1;
    }
    if (session_ids[id] == FREE) {
        return 1;
    }
    if (pthread_mutex_unlock(&id_table_mutex) != 0) {
        return -1;
    }

    return 0;
}

int terminate_session(int id) {
    if (pthread_mutex_lock(&id_table_mutex) != 0) {
        return -1;
    }
    session_ids[id] = FREE;

    for (size_t i = 0; i < PIPE_PATH_SIZE; i++) {
        if (client_pipes[id][i] == '\0') {
            break;
        }
        client_pipes[id][i] = '\0';
    }

    if (pthread_mutex_unlock(&id_table_mutex) != 0) {
        return -1;
    }
    return 0;
}

int send_reply(const void *restrict ptr, FILE *fcli, size_t size) {
    size_t bytes_written;

    bytes_written = fwrite(ptr, size, 1, fcli);
    if ((bytes_written * size) < size) {
        return -1;
    }
    return 0;
}

int treat_open_request(int id, char *name, int flags) {
    FILE *fcli = fclis[id];
    int operation_result;

    // // /* Open client pipe */
    // // if ((fcli = fopen(client_pipes[id], "w")) == NULL) {
    // //     tfs_unmount(id);
    // //     return -1;
    // // }

    operation_result = tfs_open(name, flags);


    if (send_reply(&operation_result, fcli, sizeof(int)) == -1) {
        if (fclose(fcli) != 0) {
            tfs_unmount(id);
            return -1;
        }
        tfs_unmount(id);
        return -1;
    }

    // // if (fclose(fcli) != 0) {
    // //     tfs_unmount(id);
    // //     return -1;
    // // }

    return 0;
}

int treat_close_request(int id, int fhandle) {
    FILE *fcli = fclis[id];
    int operation_result = 0;

    // // /* Open client pipe */
    // // if ((fcli = fopen(client_pipes[id], "w")) == NULL) {
    // //     tfs_unmount(id);
    // //     return -1;
    // // }

    operation_result = tfs_close(fhandle);

    if (send_reply(&operation_result, fcli, sizeof(int)) == -1) {
        if (fclose(fcli) != 0) {
            tfs_unmount(id);
            return -1;
        }
        tfs_unmount(id);
        return -1;
    }

    // // if (fclose(fcli) != 0) {
    // //     tfs_unmount(id);
    // //     return -1;
    // // }

    return 0;
}

ssize_t treat_write_request(int id, int fhandle, size_t len, char *buff) {
    FILE *fcli = fclis[id];
    ssize_t operation_result = 0;

    // // /* Open client pipe */
    // // if ((fcli = fopen(client_pipes[id], "w")) == NULL) {
    // //     tfs_unmount(id);
    // //     return -1;
    // // }
    operation_result = tfs_write(fhandle, buff, len);
        
    if (send_reply(&operation_result, fcli, sizeof(ssize_t)) == -1) {
        if (fclose(fcli) != 0) {
            tfs_unmount(id);
            return -1;
        }
        tfs_unmount(id);
        return -1;  
    }

    // // if (fclose(fcli) != 0) {
    // //     tfs_unmount(id);
    // //     return -1;
    // // }

    return 0;
}

ssize_t treat_request_read(int id, int fhandle, size_t len) {
    char *buff = (char *) malloc(sizeof(char) * len);
    FILE *fcli = fclis[id];
    ssize_t operation_result = 0;
    size_t size_written = 0;

    if (buff == NULL) {
        return -1;
    }

    // // /* Open client pipe */
    // // if ((fcli = fopen(client_pipes[id], "w")) == NULL) {
    // //     free(buff);
    // //     tfs_unmount(id);
    // //     return -1;
    // // }

    operation_result = tfs_read(fhandle, buff, len);
    if (send_reply(&operation_result, fcli, sizeof(ssize_t)) == -1) {
        free(buff);
        if (fclose(fcli) != 0) {
            tfs_unmount(id);
            return -1;
        }
        tfs_unmount(id);
        return -1;
    }

    if (operation_result != -1) {
        size_written = fwrite(buff, sizeof(char), (size_t) operation_result, fcli);
    
        if ((size_written * sizeof(ssize_t)) < sizeof(ssize_t)) {
            free(buff);
            if (fclose(fcli) != 0) {
                tfs_unmount(id);
                return -1;
            }
            tfs_unmount(id);
            return -1;
        }
    }


    // // if (fclose(fcli) != 0) {
    // //     free(buff);
    // //     tfs_unmount(id);
    // //     return -1;
    // // }

    free(buff);
    return 0;
}

int treat_request_shutdown(int id) {
    FILE *fcli;
    ssize_t operation_result = 0;

    /* Open client pipe */
    if ((fcli = fopen(client_pipes[id], "w")) == NULL) {
        tfs_unmount(id);
        return -1;
    }

    operation_result = tfs_destroy_after_all_closed();

    if (send_reply(&operation_result, fcli, sizeof(int)) == -1) {
        if (fclose(fcli) != 0) {
            tfs_unmount(id);
            return -1;
        }
        tfs_unmount(id);
        return -1;
    }

    if (fclose(fcli) != 0) {
        tfs_unmount(id);
        return -1;
    }

    if (operation_result == -1) {
        tfs_unmount(id);
        return -1;
    }

    if (unlink(server_pipe_name) == -1) {
        if (errno != 2) {
            /* if errno is 2, than unlink simply failed because the pipe was
             * properly deleted at the end of the last session*/
            return -1;
        }
    }

    exit(0);
}

int tfs_mount(char *path) {
    int id;
    FILE *fcli;

    printf("hello mount\n");
    if ((fcli = fopen(path, "w")) == NULL) {
        return -1;
    }

    /* Assign operation to a worker thread */
    id = get_free_session_id();
    if (id != -1) {
        /* Update session info */
        session_ids[id] = TAKEN;
        strcpy(client_pipes[id], path);
    }

    printf("hello mount\n");

    fclis[id] = fcli;

    if (send_reply(&id, fcli, sizeof(int)) == -1) {
        if ((fclose(fcli)) != 0) {
            tfs_unmount(id);
            return -1;
        }
        tfs_unmount(id);
        return -1;
    }

    // // if (fclose(fcli) != 0) {
    // //     /* Failed to close file */
    // //     tfs_unmount(id);
    // //     return -1;
    // // }

    

    return id;
}

int tfs_unmount(int id) {
    int operation_result = 0;
    int error;
    FILE *fcli = fclis[id];

    // // if ((fcli = fopen(client_pipes[id], "w")) == NULL) {
    // //     return -1;
    // // }

    error = terminate_session(id);

    if (error == -1 && !session_id_is_free(id)) {
        /* Completely failed to unmount */
        operation_result = -1;
    }

    if (send_reply(&operation_result, fcli, sizeof(int)) == -1) {
        if (fclose(fcli) != 0) {
            return -1;
        }
        return -1;
    }

    if (fclose(fcli) != 0) {
        return -1;
    }

    return error;
}

int treat_request(char buff, FILE *fserv) {
    struct request *message;
    char op_code = buff;
    int session_id = 0;
    char path[PIPE_PATH_SIZE];
    size_t bytes_read = 0;

    if (op_code == TFS_OP_CODE_MOUNT) {
        if (fread(path, sizeof(char), sizeof(path), fserv) != sizeof(path)) {
            return -1;
        }
        session_id = tfs_mount(path);
        if (session_id == -1) {
            return -1;
        }

        if (pthread_mutex_lock(&client_mutexes[session_id]) != 0) {
            return -1;
        }
        message = &(prod_cons_buffer[session_id][0]);
        message->session_id = session_id;
        if (pthread_cond_signal(&client_cond_var[session_id]) != 0) {
            return -1;
        }
        if (pthread_mutex_unlock(&client_mutexes[session_id]) != 0) {
            return -1;
        }

        return 0;
    }
    else {
        /* Operation requires knowing client's id */
        if ((bytes_read = fread(&session_id, 1, sizeof(int), fserv)) != sizeof(int)) {
            return -1;
        }

        /* ID provided is not valid to perform any operation */
        if (!valid_id(session_id) || session_id_is_free(session_id)) {
            return 0;
        }
    }
    if (pthread_mutex_lock(&client_mutexes[session_id]) != 0) {
        return -1;
    }
    message = &(prod_cons_buffer[session_id][0]);
    message->session_id = session_id;
    
    if (op_code == TFS_OP_CODE_UNMOUNT ) {
        /*
         * Por agora não faz nada, deixo só aqui por enquanto para ficar legível o que está a acontecer
         *
         */
    }
    else if (op_code == TFS_OP_CODE_OPEN) {
        if (fread(message->buffer, sizeof(char), FILE_NAME_SIZE, fserv) != FILE_NAME_SIZE) {
            message->op_code = -1;
            if (pthread_mutex_unlock(&client_mutexes[session_id]) != 0) {
                return -1;
            }
            return -1;
        }
        if (fread(&(message->flags), 1, sizeof(int), fserv) != sizeof(int)) {
            message->op_code = -1;
            if (pthread_mutex_unlock(&client_mutexes[session_id]) != 0) {
                return -1;
            }
            return -1;
        }
    }
    else if (op_code == TFS_OP_CODE_CLOSE) {

        if (fread(&(message->fhandle), 1, sizeof(int), fserv) != sizeof(int)) {
            message->op_code = -1;
            if (pthread_mutex_unlock(&client_mutexes[session_id]) != 0) {
                return -1;
            }
            return -1;
        }

    }
    else if (op_code == TFS_OP_CODE_WRITE) {

        if (fread(&(message->fhandle), 1, sizeof(int), fserv) != sizeof(int)) {
            message->op_code = -1;
            if (pthread_mutex_unlock(&client_mutexes[session_id]) != 0) {
                return -1;
            }
            return -1;
        }
        if (fread(&(message->len), 1, sizeof(size_t), fserv) != sizeof(size_t)) {
            message->op_code = -1;
            if (pthread_mutex_unlock(&client_mutexes[session_id]) != 0) {
                return -1;
            }
            return -1;
        }
        message->dynamic_buffer = (char *) malloc(sizeof(char) * message->len);
        if (message->dynamic_buffer == NULL) {
            message->op_code = -1;
            if (pthread_mutex_unlock(&client_mutexes[session_id]) != 0) {
                return -1;
            }
            return -1;
        }
        if ((fread(message->dynamic_buffer, sizeof(char), message->len, fserv)) != message->len) {
            message->op_code = -1;
            free(message->dynamic_buffer);
            if (pthread_mutex_unlock(&client_mutexes[session_id]) != 0) {
                return -1;
            }
            return -1;
        }
    }
    else if (op_code == TFS_OP_CODE_READ) {

        if (fread(&(message->fhandle), 1, sizeof(int), fserv) != sizeof(int)) {
            message->op_code = -1;
            if (pthread_mutex_unlock(&client_mutexes[session_id]) != 0) {
                return -1;
            }
            return -1;
        }
        if (fread(&(message->len), 1, sizeof(size_t), fserv) != sizeof(size_t)) {
            message->op_code = -1;
            if (pthread_mutex_unlock(&client_mutexes[session_id]) != 0) {
                return -1;
            }
            return -1;
        }

    }
    else if (op_code == TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED) {
        /*
         * Por agora não faz nada, deixo só aqui por enquanto para ficar legível o que está a acontecer
         *
         */
    }
    else {
        message->op_code = -1;
        if (pthread_mutex_unlock(&client_mutexes[session_id]) != 0) {
            return -1;
        }
        return -1;
    }
    message->op_code = op_code;
    if (pthread_cond_signal(&client_cond_var[session_id]) != 0) {
        return -1;
    }
    if (pthread_mutex_unlock(&client_mutexes[session_id]) != 0) {
        return -1;
    }
    return 0;
}

int treat_request_thread(int session_id) {
    struct request *req = &(prod_cons_buffer[session_id][0]);
    int op_code = req->op_code;

    if (op_code == TFS_OP_CODE_UNMOUNT) {
        if (tfs_unmount(session_id) == -1) {
            req->op_code = -1;
            return -1;
        }
    }
    else if (op_code == TFS_OP_CODE_OPEN) {
        char *name = req->buffer;
        int flags = req->flags;
        if (treat_open_request(session_id, name, flags) == -1) {
            req->op_code = -1;
            return -1;
        }
    }
    else if (op_code == TFS_OP_CODE_CLOSE) {
        int fhandle = req->fhandle;
        if (treat_close_request(session_id, fhandle) == -1) {
            req->op_code = -1;
            return -1;
        }
    }
    else if (op_code == TFS_OP_CODE_WRITE) {
        int fhandle = req->fhandle;
        size_t len = req->len;
        char buffer[len];
        memcpy(buffer, req->dynamic_buffer, len);
        if (treat_write_request(session_id, fhandle, len, buffer) == -1) {
            req->op_code = -1;
            return -1;
        }
        free(req->dynamic_buffer);
    }
    else if (op_code == TFS_OP_CODE_READ) {
        int fhandle = req->fhandle;
        size_t len = req->len;
        if (treat_request_read(session_id, fhandle, len) == -1) {
            req->op_code = -1;
            return -1;
        }
    }
    else if (op_code == TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED) {
        if (treat_request_shutdown(session_id) == -1) {
            req->op_code = -1;
            return -1;
        }
    }
    else {
        req->op_code = -1;
        return -1;
    }
    req->op_code = -1;

    return 0;
}

void* tfs_server_thread(void* args) {
    int id = *((int *) args);
    struct request *message = &prod_cons_buffer[id][0];
    
    while (1) {
        if (pthread_mutex_lock(&client_mutexes[id]) != 0) {
            return NULL;
        }
        while (message->op_code == -1) {
            if (pthread_cond_wait(&client_cond_var[id], &client_mutexes[id]) != 0) {
                pthread_exit(NULL);
            }
        }
        if (treat_request_thread(id) == -1) {
            message->op_code = -1;
            tfs_unmount(id);
            pthread_mutex_unlock(&client_mutexes[id]);
        }
        message->op_code = -1;
        pthread_mutex_unlock(&client_mutexes[id]);
    }
}


int main(int argc, char **argv) {
    FILE *fserv;
    size_t r_buffer;
    char buff = '\0';
    signal(SIGPIPE, SIG_IGN);

    if (argc < 2) {
        printf("Please specify the pathname of the server's pipe.\n");
        return 1;
    }

    char *pipename = argv[1];
    printf("Starting TecnicoFS server with pipe called %s\n", pipename);

    if (unlink(pipename) == -1) {
        if (errno != 2) {
            /* if errno is 2, than unlink simply failed because the pipe was
             * properly deleted at the end of the last session*/
            return -1;
        }
    }   

    printf("here\n");

    if (server_init() == -1) {
        return -1;
    }

    printf("here init\n");

    /* Create server's named pipe */
    if (mkfifo(pipename, 0777) < 0) {
        return -1;
    }

    printf("here 3\n");

        /* Open server's named pipe */
    if ((fserv = fopen(pipename, "r")) == NULL) {
        return -1;
    }

    printf("here 4\n");

    server_pipe_name = pipename;

    /* Main loop */
    while (1) {
        /* Read requests from pipe */
        r_buffer = fread(&buff, 1, 1, fserv);
        if (r_buffer == 0) {
            if (fclose(fserv) != 0) {   
                return -1;
            }
            if ((fserv = fopen(pipename, "r")) == NULL) {
                return -1;
            }
            continue;
        }

        if (treat_request(buff, fserv) == -1) {
            if (fclose(fserv) != 0) {
                return -1;
            }
            return -1;
        }
    }

    return 0;
}