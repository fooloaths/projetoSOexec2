#include "operations.h"
#include "common/common.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h> // Not sure se este é preciso
#include <pthread.h>

//TODO: Redifine in config.h
//TODO: Verificar se estamos sempre a devolver erro ao cliente (escrevendo no pipe) --> Talvez criar função para isso
//TODO: Perguntar ao prof se é para fechar (do lado do servidor) o pipe do cliente sempre que termina uma operação ou se é só no unmount
#define S 64 /* confirmar isto com os profs. É suposto ser o nº de possíveis sessões ativas */
#define FREE 1
#define TAKEN 0

//?? is this used?
struct session { /* Não sei se é isto que temos de fazer */
    char const *pipe;
    size_t session_id;
};

struct request {
    char op_code;
    int flags;
    char buffer[FILE_NAME_SIZE];
    char *dynamic_buffer;
    int fhandle;
    size_t len;
};

static int session_ids[S];
static char client_pipes[S][PIPE_PATH_SIZE];
static pthread_t threads[S];
//i think this line is useless?
static pthread_mutex_t mutexes[S];

static struct request *buffer[S][1] = {NULL};

void server_init() {
    tfs_init();
    for (size_t i = 0; i < S; i++) {
        session_ids[i] = FREE;

        pthread_create(&threads[i], NULL, tfs_server_thread, (void *) i);

        for (size_t j = 0; j < PIPE_PATH_SIZE; j++) {
            client_pipes[i][j] = '\0';
        }
    }

}

int get_free_session_id() {
    for (int i = 0; i < S; i++) {
        if (session_ids[i] == FREE) {
            return i;
        }
    }
    return -1;
}

int valid_id(int id) {
    return id >= 0 && id < S;
}

int session_id_is_free(int id) {
    return session_ids[id] == FREE;
}

//TODO alter this to also close thread and mutex if they are not NULL
//TODO alter this so set the buffer[id] to NULL
void terminate_session(int id) {
    session_ids[id] = FREE;

    for (size_t i = 0; i < PIPE_PATH_SIZE; i++) {
        if (client_pipes[id][i] == '\0') {
            break;
        }
        client_pipes[id][i] = '\0';
    }
}

int treat_open_request(int id, char *name, int flags) {
    FILE *fcli;
    int operation_result;
    size_t size_written = 0;

    /* Open client pipe */
    if ((fcli = fopen(client_pipes[id], "w")) == NULL) {
        return -1;
    }

    operation_result = tfs_open(name, flags);

    size_written = fwrite(&operation_result, sizeof(int), 1, fcli);
    if ((size_written * sizeof(int)) < sizeof(int)) {
        return -1;
    }

    if (fclose(fcli) != 0) {
        return -1;
    }

    return 0;
}

int treat_close_request(int id, int fhandle) {
    FILE *fcli;
    int operation_result = 0;
    size_t size_written = 0;

    /* Open client pipe */
    if ((fcli = fopen(client_pipes[id], "w")) == NULL) {
        return -1;
    }

    operation_result = tfs_close(fhandle);

    size_written = fwrite(&operation_result, sizeof(int), 1, fcli);
    if ((size_written * sizeof(int)) < sizeof(int)) {
        return -1;
    }

    if (fclose(fcli) != 0) {
        return -1;
    }

    return operation_result;
}

ssize_t treat_write_request(int id, int fhandle, size_t len, char *buff) {
    FILE *fcli;
    ssize_t operation_result = 0;
    size_t size_written = 0;

    /* Open client pipe */
    if ((fcli = fopen(client_pipes[id], "w")) == NULL) {
        return -1;
    }
    operation_result = tfs_write(fhandle, buff, len);

    size_written = fwrite(&operation_result, sizeof(ssize_t), 1, fcli);
    if ((size_written * sizeof(ssize_t)) < sizeof(ssize_t)) {
        return -1;  
    }

    if (fclose(fcli) != 0) {
        return -1;
    }

    return operation_result;
}

ssize_t treat_request_read(int id, int fhandle, size_t len) {
    char *buff = (char *) malloc(sizeof(char) * len);
    FILE *fcli;
    ssize_t operation_result = 0;
    size_t size_written = 0;

    if (buff == NULL) {
        //TODO send reply
        return -1;
    }

    /* Open client pipe */
    if ((fcli = fopen(client_pipes[id], "w")) == NULL) {
        return -1;
    }

    operation_result = tfs_read(fhandle, buff, len);

    size_written = fwrite(&operation_result, 1, sizeof(ssize_t), fcli);
    if ((size_written != sizeof(ssize_t))) {
        return -1;
    }

    if (operation_result != -1) {
        size_written = fwrite(buff, sizeof(char), (size_t) operation_result, fcli);
    
        if ((size_written * sizeof(ssize_t)) < sizeof(ssize_t)) {
            return -1;
        }
    }


    if (fclose(fcli) != 0) {
        return -1;
    }

    free(buff);
    return operation_result;
}

int treat_request_shutdown(int id) {
    FILE *fcli;
    ssize_t operation_result = 0;
    size_t size_written = 0;

    /* Open client pipe */
    if ((fcli = fopen(client_pipes[id], "w")) == NULL) {
        return -1;
    }

    operation_result = tfs_destroy_after_all_closed();

    size_written = fwrite(&operation_result, sizeof(int), 1, fcli);
    if ((size_written * sizeof(int)) < sizeof(int)) {
        return -1;
    }

    if (fclose(fcli) != 0) {
        return -1;
    }

    if (operation_result == -1) {
        return -1;
    }

    exit(0);
}

int treat_request(char buff, FILE *fserv) {
    char op_code = buff;
    int session_id = -1;
    size_t len;
    char path[PIPE_PATH_SIZE];

    // doesnt make sense for mount and unmount to be "parallelized"
    if (op_code == TFS_OP_CODE_MOUNT) {
        /* Skip op code */
        if (fread(path, sizeof(char), sizeof(path), fserv) != sizeof(path)) {
            return -1;
        }
        if (tfs_mount(path) == -1) {
            return -1;
        }
    }
    else if (op_code == TFS_OP_CODE_UNMOUNT ) {
        if (fread(&session_id, 1, sizeof(int), fserv) != sizeof(int)) {
            return -1;
        }

        if (!valid_id(session_id) || tfs_unmount(session_id) == -1) {
            return -1;
        }
    }


    else if (op_code == TFS_OP_CODE_OPEN) {
        struct request *req = (struct request *) malloc(sizeof(struct request));
        if (req == NULL) {
            return -1;
        }
        req->op_code = op_code;
        if (fread(&session_id, 1, sizeof(int), fserv) != sizeof(int)) {
            free(req);
            return -1;
        }
        if (fread(req->buffer, sizeof(char), FILE_NAME_SIZE, fserv) != FILE_NAME_SIZE) {
            free(req);
            return -1;
        }
        if (fread(req->flags, 1, sizeof(int), fserv) != sizeof(int)) {
            free(req);
            return -1;
        }
        if (!valid_id(session_id)) {
            free(req);
            return -1;
        }
        buffer[session_id][0] = req;
    }
    else if (op_code == TFS_OP_CODE_CLOSE) {
        struct request *req = (struct request *) malloc(sizeof(struct request));
        if (req == NULL) {
            return -1;
        }
        req->op_code = op_code;
        if (fread(&session_id, 1, sizeof(int), fserv) != sizeof(int)) {
            free(req);
            return -1;
        }
        if (fread(req->fhandle, 1, sizeof(int), fserv) != sizeof(int)) {
            free(req);
            return -1;
        }
        if !valid_id(session_id) {
            free(req);
            return -1;
        }
        buffer[session_id][0] = req;
    }
    else if (op_code == TFS_OP_CODE_WRITE) {
        struct request *req = (struct request *) malloc(sizeof(struct request));
        if (req == NULL) {
            return -1;
        }
        req->op_code = op_code;
        if (fread(&session_id, 1, sizeof(int), fserv) != sizeof(int)) {
            free(req);
            return -1;
        }
        if (fread(req->fhandle, 1, sizeof(int), fserv) != sizeof(int)) {
            free(req);
            return -1;
        }
        if (fread(req->len, 1, sizeof(size_t), fserv) != sizeof(size_t)) {
            free(req);
            return -1;
        }
        char buffer[len];
        if (fread(req->buffer, sizeof(char), sizeof(buffer), fserv) != sizeof(buffer)) {
            free(req);
            return -1;
        }
        if !valid_id(session_id) {
            free(req);
            return -1;
        }
        buffer[session_id][0] = req;
    }
    else if (op_code == TFS_OP_CODE_READ) {
        struct request *req = (struct request *) malloc(sizeof(struct request));
        if (req == NULL) {
            return -1;
        }
        req->op_code = op_code;
        if (fread(&session_id, 1, sizeof(int), fserv) != sizeof(int)) {
            free(req);
            return -1;
        }
        if (fread(req->fhandle, 1, sizeof(int), fserv) != sizeof(int)) {
            free(req);
            return -1;
        }
        if (fread(req->len, 1, sizeof(size_t), fserv) != sizeof(size_t)) {
            free(req);
            return -1;
        }
        if !valid_id(session_id) {
            free(req);
            return -1;
        }
        buffer[session_id][0] = req;
    }
    else if (op_code == TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED) {
        if (fread(&session_id, 1, sizeof(int), fserv) != sizeof(int)) {
            return -1;
        }

        if (!valid_id(session_id) || treat_request_shutdown(session_id) == -1) {
            return -1;
        }
    }
    else {
        return -1;
    }

    return 0;
}

int treat_request_thread(int id) {
    struct request *req = buffer[id][0];
    int op_code = req->op_code;
    int session_id = id;

    //MOUNT and UNMOUNT are treated in the main thread
    if (op_code == TFS_OP_CODE_OPEN) {
        char *name = req->buffer;
        int flags = req->flags;
        if (treat_open_request(session_id, name, flags) == -1) {
            free(req);
            req = NULL;
            return -1;
        }
        free(req);
        req = NULL;
    }
    else if (op_code == TFS_OP_CODE_CLOSE) {
        int fhandle = req->fhandle;
        treat_close_request(session_id, fhandle) == -1) {
            free(req);
            req = NULL;
            return -1;
        }
        free(req);
        req = NULL;
    }
    else if (op_code == TFS_OP_CODE_WRITE) {
        int fhandle = req->fhandle;
        size_t len = req->len;
        char buffer[len];
        memcpy(buffer, req->buffer, len);
        treat_write_request(session_id, fhandle, len, buffer) == -1) {
            free(req);
            req = NULL;
            return -1;
        }
        free(req);
        req = NULL;
    }
    else if (op_code == TFS_OP_CODE_READ) {
        int fhandle = req->fhandle;
        size_t len = req->len;
        treat_request_read(session_id, fhandle, len) == -1) {
            free(req);
            req = NULL;
            return -1;
        }
        free(req);
        req = NULL;

    }
    //ver se faz sentido ter isto aqui
    // // else if (op_code == TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED) {
    // //     if (fread(&session_id, 1, sizeof(int), fserv) != sizeof(int)) {
    // //         return -1;
    // //     }

    // //     if (!valid_id(session_id) || treat_request_shutdown(session_id) == -1) {
    // //         return -1;
    // //     }
    // // }
    else {
        return -1;
    }

    return 0;
}

void* tfs_server_thread(void* args) {
    int id = *((int *) args);
    while (buffer[id][0] != NULL) {
        printf("HELLO MR I AM WORKING BZZT BZZT MY ID IS %d\n", id);
        if (treat_request_thread(id) == -1) {
            pthread_exit(NULL);
        }
    }
}

int tfs_mount(char *path) {
    int id;
    size_t  bytes_written = 0;
    FILE *fcli;
    if ((fcli = fopen(path, "w")) == NULL) {
        return -1;
    }
    id = get_free_session_id();
    if (id == -1) {
        bytes_written = fwrite(&id, sizeof(int), 1, fcli);
        return -1;
    }

    /* update session id and path */
    session_ids[id] = TAKEN;
    strcpy(client_pipes[id], path);
    bytes_written = fwrite(&id, 1, sizeof(int), fcli);
    if ((bytes_written != sizeof(int))) {
        return -1;
    }

    if (fclose(fcli) != 0) {
        /* Failed to close file */
        return -1;
    }

    return id;
}

//TODO alter to close threads and mutexes if they are not NULL  
int tfs_unmount(int id) {
    int operation_result = 0;
    size_t size_written = 0;
    FILE *fcli;

    if (session_id_is_free(id)) {
        /* There was nothing to unmount */
        return -1;
    }

    if ((fcli = fopen(client_pipes[id], "w")) == NULL) {
        return -1;
    }

    terminate_session(id);

    size_written = fwrite(&operation_result, sizeof(int), 1, fcli);

    if ((size_written * sizeof(int)) < sizeof(int)) {
        return -1;
    }

    if (fclose(fcli) != 0) {
        perror("Erro");
        return -1;
    }

    return 0;
}

int main(int argc, char **argv) {
    FILE *fserv;
    size_t r_buffer;
    char buff = '\0'; //Valor temporário

    if (argc < 2) {
        printf("Please specify the pathname of the server's pipe.\n");
        return 1;
    }

    char *pipename = argv[1];
    printf("Starting TecnicoFS server with pipe called %s\n", pipename);

    unlink(pipename); //Não sei se isto fica aqui

    server_init();

    /* Create server's named pipe */
    if (mkfifo(pipename, 0640) < 0) {
        return -1;
    }

        /* Open server's named pipe */
    if ((fserv = fopen(pipename, "r")) == NULL) {
        return -1;
    }

    /* TO DO */
    /* Main loop */
    while (1) {
        /* Read requests from pipe */
        r_buffer = fread(&buff, 1, 1, fserv);
        if (r_buffer == 0) {
            fclose(fserv);
            if ((fserv = fopen(pipename, "r")) == NULL) {
                return -1;
            }
            continue;
        }

        if (treat_request(buff, fserv) == -1) {
            return -1;
        }
    }

    return 0;
}

/*
 * Function to process request and to place them in the correct queue
 */

// // int read_request(char buff, FILE *fserv) {
// //     char op_code = buff;
// //     struct request *message = (struct request *) malloc(sizeof(struct request));
// //     message->op_code = buff;


// //     if (op_code == TFS_OP_CODE_MOUNT) {
// //         if (fread(message->buffer, sizeof(char), PIPE_PATH_SIZE, fserv) != PIPE_PATH_SIZE) {
// //             printf("Servidor (read request): Falhou ao ler o buffer (TFS_MOUNT)\n");
// //             return -1;
// //         }
// //     }
// //     else if (op_code == TFS_OP_CODE_UNMOUNT ) {
// //         if (fread(&(message->session_id), 1, sizeof(int), fserv) != sizeof(int)) {
// //             printf("Servidor (read request): Falhou ao ler o session id (TFS_UNMOUNT)\n");
// //             return -1;
// //         }
// //     }
// //     else if (op_code == TFS_OP_CODE_OPEN) {
// //         if (fread(&(message->session_id), 1, sizeof(int), fserv) != sizeof(int)) {
// //             printf("Servidor (read request): Falhou ao ler o session id (TFS_OPEN)\n");
// //             return -1;
// //         }
// //         if (fread(message->buffer, sizeof(char), FILE_NAME_SIZE, fserv) != FILE_NAME_SIZE) {
// //             printf("Servidor (read request): Falhou ao ler o buffer (TFS_OPEN)\n");
// //             return -1;
// //         }
// //         if (fread(&(message->flags), 1, sizeof(int), fserv) != sizeof(int)) {
// //             printf("Servidor (read request): Falhou ao ler as flags (TFS_OPEN)\n");
// //             return -1;
// //         }
// //     }
// //     else if (op_code == TFS_OP_CODE_CLOSE) {
// //         if (fread(&(message->session_id), 1, sizeof(int), fserv) != sizeof(int)) {
// //             printf("Servidor (read request): Falhou ao ler o session id (TFS_CLOSE)\n");
// //             return -1;
// //         }
// //         if (fread(&(message->fhandle), 1, sizeof(int), fserv) != sizeof(int)) {
// //             printf("Servidor (read request): Falhou ao ler o fhandle (TFS_CLOSE)\n");
// //             return -1;
// //         }
// //     }
// //     else if (op_code == TFS_OP_CODE_WRITE) {
// //         if (fread(&(message->session_id), 1, sizeof(int), fserv) != sizeof(int)) {
// //             printf("Servidor (read request): Falhou ao ler o session_id (TFS_WRITE)\n");
// //             return -1;
// //         }
// //         if (fread(&(message->fhandle), 1, sizeof(int), fserv) != sizeof(int)) {
// //             printf("Servidor (read request): Falhou ao ler o fhandle (TFS_WRITE)\n");
// //             return -1;
// //         }
// //         if (fread(&(message->len), 1, sizeof(size_t), fserv) != sizeof(size_t)) {
// //             printf("Servidor (read request): Falhou ao ler o len (TFS WRITE)\n");
// //             return -1;
// //         }
// //         message->dynamic_buffer = (char *) malloc(sizeof(char) * message->len);
// //         if (message->dynamic_buffer == NULL) {
// //             printf("Servidor (read request): Falhou ao alocar memória para o dynamic array (TFS_WRITE)\n");
// //             return -1;
// //         }
// //         if (fread(message->dynamic_buffer, sizeof(char), message->len, fserv) != sizeof(message->dynamic_buffer)) {
// //             printf("Servidor (read request): Falhou ao ler o dynamic buffer/aquilo que é para escrever (TFS_WRITE)\n");
// //             return -1;
// //         }
// //     }
// //     else if (op_code == TFS_OP_CODE_READ) {
// //         if (fread(&(message->session_id), 1, sizeof(int), fserv) != sizeof(int)) {
// //             printf("Servidor (read request): Falhou ao ler o session id (TFS_READ)\n");
// //             return -1;
// //         }
// //         if (fread(&(message->fhandle), 1, sizeof(int), fserv) != sizeof(int)) {
// //             printf("Servidor (read request): Falhou ao ler o fhandle (TFS_READ)\n");
// //             return -1;
// //         }
// //         if (fread(&(message->len), 1, sizeof(size_t), fserv) != sizeof(size_t)) {
// //             printf("Servidor (read request): Falhou ao ler o len (TFS_READ)\n");
// //             return -1;
// //         }
// //     }
// //     else if (op_code == TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED) {
// //         if (fread(&(message->session_id), 1, sizeof(int), fserv) != sizeof(int)) {
// //             printf("Servidor (read request): Falhou ao ler o session id (SHUTDOWN AFTER ALL CLOSED)\n");
// //             return -1;
// //         }
// //     }
// //     else {
// //         printf("Servidor (read request): Falhou porque o op code não existia\n");
// //         return -1;
// //     }

// //     return 0;
// // }

// // int treat_request() {
// //     struct request *message = (struct request *) malloc(sizeof(struct request));
// //     void *retval;

// //     if (message->op_code == TFS_OP_CODE_MOUNT) {
// //         int id = get_free_session_id();
// //         message->session_id = id;

// //         pthread_create(&threads[id], NULL, tfs_mount,(void*) message);

// //         //TODO acho que falta dar join aqui
        

// //         return 0;
// //     }
    
// //     if (!valid_id(message->session_id) || session_id_is_free(message->session_id)) {

// //         if (prod_ptr != cons_ptr) {
// //             /* If there are other tasks that can be consumed */
// //         }
// //         printf("Servidor (treat request): Falha porque o id não era válido / estava free\n");
// //         return -1;
// //     }

    
// //     if (message->op_code == TFS_OP_CODE_UNMOUNT) {
// //         pthread_create(&threads[message->session_id], NULL, tfs_unmount,(void*) message);
// //     }
// //     else if (message->op_code == TFS_OP_CODE_OPEN) {
// //         pthread_create(&threads[message->session_id], NULL, treat_open_request,(void*) message);
// //     }
// //     else if (message->op_code == TFS_OP_CODE_CLOSE) {
// //         pthread_create(&threads[message->session_id], NULL, treat_close_request,(void*) message);
        
// //     }
// //     else if (message->op_code == TFS_OP_CODE_WRITE) {
// //         pthread_create(&threads[message->session_id], NULL, treat_write_request,(void*) message);
// //         free(message->dynamic_buffer);
        
// //     }
// //     else if (message->op_code == TFS_OP_CODE_READ) {
// //         pthread_create(&threads[message->session_id], NULL, treat_request_read,(void*) message);
        
// //     }
// //     else if (message->op_code == TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED) {
// //         pthread_create(&threads[message->session_id], NULL, treat_request_shutdown,(void*) message);
        
// //     }
// //     else {
// //         if (prod_ptr != cons_ptr) {
// //             /* If there are other tasks that can be consumed */
// //         }
// //         printf("Servidor (treat request): Falha porque o opcode não existia\n");
// //         return -1;
// //     }

// //     // TODO fazer o join aqui ou no inicio da função? Inicio parece-me melhor, but idk
// //     /* Check/Wait if the client had not yet finished last operation */
// //     if (pthread_join(threads[message->session_id], &retval) != 0) {
// //         printf("Servidor (treat request): Falha ao fazer join da thread\n");
// //         return -1;
// //     }
// //     if (*((int *) retval) == -1) {
// //         //TODO tratar do erro que ocorreu a tratar do pedido
// //     }


// //     return 0;
// // }



// // int main(int argc, char **argv) {
// //     FILE *fserv;
// //     size_t r_buffer;
// //     char buff = '\0'; //Valor temporário

// //     if (argc < 2) {
// //         printf("Please specify the pathname of the server's pipe.\n");
// //         return 1;
// //     }

// //     char *pipename = argv[1];
// //     printf("Starting TecnicoFS server with pipe called %s\n", pipename);

// //     unlink(pipename); //Não sei se isto fica aqui

// //     server_init();

// //     /* Create server's named pipe */
// //     if (mkfifo(pipename, 0640) < 0) {
// //         return -1;
// //     }

// //         /* Open server's named pipe */
// //     if ((fserv = fopen(pipename, "r")) == NULL) {
// //         return -1;
// //     }

// //     /* Main loop */
// //     while (1) {
// //         /* Read requests from pipe */
// //         r_buffer = fread(&buff, 1, 1, fserv);
// //         if (r_buffer == 0) {
// //             fclose(fserv);
// //             if ((fserv = fopen(pipename, "r")) == NULL) {
// //                 printf("Servidor (main): Falha ao abrir o pipe do servidor\n");
// //                 return -1;
// //             }
// //             continue;
// //         }

// //         /* Producer function */
// //         if (read_request(buff, fserv) == -1) {
// //             printf("Servidor (main): Falha ao ler o request\n");
// //             return -1;
// //         }

// //         //TODO ver os semaphores e mutexes para isto

// //         /* Consumer function */
// //         if (treat_request() == -1) {
// //             printf("Servidor (main): Falha ao tratar do pedido\n");
// //             return -1;
// //         }
// //     }


// //     return 0;
// // }