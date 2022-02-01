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
#define S 20 /* confirmar isto com os profs. É suposto ser o nº de possíveis sessões ativas */
#define FREE 1
#define TAKEN 0

//?? is this used?
struct session { /* Não sei se é isto que temos de fazer */
    char const *pipe;
    size_t session_id;
};

struct request {
    char op_code;
    int session_id;
    int flags;
    char buffer[FILE_NAME_SIZE];
    char *dynamic_buffer;
    int fhandle;
    size_t len;
};

static int session_ids[S];
static char client_pipes[S][PIPE_PATH_SIZE];
static pthread_mutex_t id_table_mutex;
static pthread_t threads[S];
//i think this line is useless?
static pthread_mutex_t client_mutexes[S];
static pthread_cond_t client_cond_var[S];

static struct request prod_cons_buffer[S][1];

void* tfs_server_thread(void *);
int tfs_mount(char *path);

int server_init() {
    tfs_init();

    if (pthread_mutex_init(&id_table_mutex, NULL) != 0) {
        return -1;
    }

    for (size_t i = 0; i < S; i++) {
        session_ids[i] = FREE;

        if (pthread_mutex_init(&client_mutexes[i], NULL) != 0) {
            return -1;
        }

        if (pthread_cond_init(&client_cond_var[i], NULL) != 0) {
            return -1;
        }

        prod_cons_buffer[i][0].op_code = -1;
        size_t *i_pointer = malloc(sizeof(*i_pointer));
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
    printf("Get free id: Vamos bloquear\n");
    if (pthread_mutex_lock(&id_table_mutex) != 0) {
        return -1;
    }

    for (int i = 0; i < S; i++) {
        if (session_ids[i] == FREE) {
            id = i;
            break;
        }
    }
    printf("Get free id: Vamos destrancar\n");
    if (pthread_mutex_unlock(&id_table_mutex) != 0) {
        return -1;
    }

    return id;
}

int valid_id(int id) {
    return id >= 0 && id < S;
}

int session_id_is_free(int id) {
    printf("Session if is free: Vamos bloquear o lock\n");
    if (pthread_mutex_lock(&id_table_mutex) != 0) {
        return -1;
    }
    if (session_ids[id] == FREE) {
        return 1;
    }
    printf("Session if is free: Vamos destrancar\n");
    if (pthread_mutex_unlock(&id_table_mutex) != 0) {
        return -1;
    }

    return 0;
}

//TODO alter this to also close thread and mutex if they are not NULL
//TODO alter this so set the buffer[id] to NULL
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
    FILE *fcli;
    int operation_result;

    printf("Treat open request: Vamos abrir o pipe do cliente\n");
    /* Open client pipe */
    if ((fcli = fopen(client_pipes[id], "w")) == NULL) {
        return -1;
    }

    operation_result = tfs_open(name, flags);

    printf("Treat open request: Vamos escrever o resultado\n");
    if (send_reply(&operation_result, fcli, sizeof(int)) == -1) {
        if (fclose(fcli) != 0) {
            return -1;
        }
        return -1;
    }

    printf("Treat open request: Vamos fechar o pipe\n");
    if (fclose(fcli) != 0) {
        return -1;
    }

    return 0;
}

int treat_close_request(int id, int fhandle) {
    FILE *fcli;
    int operation_result = 0;

    /* Open client pipe */
    if ((fcli = fopen(client_pipes[id], "w")) == NULL) {
        return -1;
    }

    operation_result = tfs_close(fhandle);

    if (send_reply(&operation_result, fcli, sizeof(int)) == -1) {
        if (fclose(fcli) != 0) {
            return -1;
        }
        return -1;
    }

    if (fclose(fcli) != 0) {
        return -1;
    }

    return 0;
}

ssize_t treat_write_request(int id, int fhandle, size_t len, char *buff) {
    FILE *fcli;
    ssize_t operation_result = 0;

    /* Open client pipe */
    if ((fcli = fopen(client_pipes[id], "w")) == NULL) {
        return -1;
    }
    printf("LALALALALA queremos escrever %ld\n", len);
    printf("O BUFFER QUE VAMOS ESCREVER é %s\n\n\n", buff);
    printf("buff[1] = %c\n", buff[1]);
    printf("Buff[2] = %c\n", buff[2]);
    printf("Buff[3] = %c\n", buff[3]);
    operation_result = tfs_write(fhandle, buff, len);
    printf("OIOIOIOIOI oq escrevemos na verdade foi %ld\n", operation_result);
        
    if (send_reply(&operation_result, fcli, sizeof(ssize_t)) == -1) {
        if (fclose(fcli) != 0) {
            return -1;
        }
        return -1;  
    }

    if (fclose(fcli) != 0) {
        return -1;
    }

    return 0;
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
        free(buff);
        return -1;
    }

    operation_result = tfs_read(fhandle, buff, len);
    if (send_reply(&operation_result, fcli, sizeof(ssize_t)) == -1) {
        free(buff);
        if (fclose(fcli) != 0) {
            return -1;
        }
        return -1;
    }

    if (operation_result != -1) {
        size_written = fwrite(buff, sizeof(char), (size_t) operation_result, fcli);
    
        if ((size_written * sizeof(ssize_t)) < sizeof(ssize_t)) {
            free(buff);
            if (fclose(fcli) != 0) {
                return -1;
            }
            return -1;
        }
    }


    if (fclose(fcli) != 0) {
        free(buff);
        return -1;
    }

    free(buff);
    return 0;
}

int treat_request_shutdown(int id) {
    FILE *fcli;
    ssize_t operation_result = 0;

    /* Open client pipe */
    if ((fcli = fopen(client_pipes[id], "w")) == NULL) {
        return -1;
    }

    operation_result = tfs_destroy_after_all_closed();

    if (send_reply(&operation_result, fcli, sizeof(int)) == -1) {
        if (fclose(fcli) != 0) {
            return -1;
        }
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

int tfs_mount(char *path) {
    int id;
    FILE *fcli;
    if ((fcli = fopen(path, "w")) == NULL) {
        return -1;
    }

    /* Assign operation to a worker thread */
    id = get_free_session_id();
    printf("ID da sessão: %d\n", id);
    if (id != -1) {
        /* Update session info */
        session_ids[id] = TAKEN;
        strcpy(client_pipes[id], path);
    }

    if (send_reply(&id, fcli, sizeof(int)) == -1) {
        printf("Erro ao enviar o id\n");
        if ((fclose(fcli)) != 0) {
            return -1;
        }
        return -1;
    }

    if (fclose(fcli) != 0) {
        printf("Erro ao fechar o fcli\n");
        /* Failed to close file */
        return -1;
    }

    return id;
}

int tfs_unmount(int id) {
    int operation_result = 0;
    int error;
    FILE *fcli;

    if ((fcli = fopen(client_pipes[id], "w")) == NULL) {
        return -1;
    }

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
    size_t len = 0;
    char path[PIPE_PATH_SIZE];
    size_t bytes_read = 0;

    printf("op code inicial: %d\n", op_code);

    printf("Vamos ler um pedido\n");
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
        printf("Mount: Fechou o lock\n");
        message = &(prod_cons_buffer[session_id][0]);
        message->session_id = session_id;
        if (pthread_cond_signal(&client_cond_var[session_id]) != 0) {
            return -1;
        }
        printf("Mount: Deu signal\n");
        if (pthread_mutex_unlock(&client_mutexes[session_id]) != 0) {
            return -1;
        }
        printf("Mount: Desbloqueou o lock\n");

        return 0;
    }
    else {
        /* Operation requires knowing client's id */
        printf("Vamos ler o session id e neste momento é %d\n", session_id);
        if ((bytes_read = fread(&session_id, 1, sizeof(int), fserv)) != sizeof(int)) {
            perror("Erro");
            printf("Lemos este nº de bytes %ld\n", bytes_read);
            printf("O id lido foi %d\n", session_id);
            printf("Falhou ao ler o id, c'est la vie\n");
            return -1;
        }

        printf("Vamos ver se o id é válido\n");
        /* ID provided is not valid to perform any operation */
        if (!valid_id(session_id) || session_id_is_free(session_id)) {
            printf("Não é válido\n");
            return 0;
        }
        printf("É válido\n");
    }
    printf("Vamos bloquear\n");
    if (pthread_mutex_lock(&client_mutexes[session_id]) != 0) {
        return -1;
    }
    message = &(prod_cons_buffer[session_id][0]);
    printf("O id é %d\n", session_id);
    message->session_id = session_id;
    
    if (op_code == TFS_OP_CODE_UNMOUNT ) {
        /*
         * Por agora não faz nada, deixo só aqui por enquanto para ficar legível o que está a acontecer
         *
         */
    }
    else if (op_code == TFS_OP_CODE_OPEN) {
        printf("Vamos ler as coisas do open\n");
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
        printf("Lemos tudo do open\n");
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
    printf("Demos signal e unlock\n");
    printf("Aquando do signal o opcode é %d\n", message->op_code);
    return 0;
}

int treat_request_thread(int session_id) {
    struct request *req = &(prod_cons_buffer[session_id][0]);
    int op_code = req->op_code;

    printf("Servidor (thread): Vamos tratar de um pedido\n");
    if (op_code == TFS_OP_CODE_UNMOUNT) {
        printf("Servidor (thread): Começou o tfs unmount\n");
        if (tfs_unmount(session_id) == -1) {
            req->op_code = -1;
            return -1;
        }
        printf("Servidor (thread): terminou o tfs unmount\n");
    }
    else if (op_code == TFS_OP_CODE_OPEN) {
        printf("Servidor (thread): Começou o tfs open\n");
        char *name = req->buffer;
        int flags = req->flags;
        if (treat_open_request(session_id, name, flags) == -1) {
            req->op_code = -1;
            return -1;
        }
        printf("Servidor (thread): terminou o tfs open\n");
    }
    else if (op_code == TFS_OP_CODE_CLOSE) {
        printf("Servidor (thread): Começou o tfs close\n");
        int fhandle = req->fhandle;
        if (treat_close_request(session_id, fhandle) == -1) {
            req->op_code = -1;
            return -1;
        }
        printf("Servidor (thread): terminou o tfs close\n");
    }
    else if (op_code == TFS_OP_CODE_WRITE) {
        printf("Servidor (thread): Começou o tfs write\n");
        int fhandle = req->fhandle;
        size_t len = req->len;
        char buffer[len];
        memcpy(buffer, req->dynamic_buffer, len);
        if (treat_write_request(session_id, fhandle, len, buffer) == -1) {
            req->op_code = -1;
            return -1;
        }
        printf("Servidor (thread): terminou o tfs write\n");
    }
    else if (op_code == TFS_OP_CODE_READ) {
        printf("Servidor (thread): Começou o tfs read\n");
        int fhandle = req->fhandle;
        size_t len = req->len;
        if (treat_request_read(session_id, fhandle, len) == -1) {
            req->op_code = -1;
            return -1;
        }
        printf("Servidor (thread): terminou o tfs read\n");
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
        printf("Vamos passar o 1º lock do while principal\n");
        if (pthread_mutex_lock(&client_mutexes[id]) != 0) {
            return NULL;
        }
        printf("Conseguimos\n");
        while (message->op_code == -1) {
            printf("Vou dormir\n");
            if (pthread_cond_wait(&client_cond_var[id], &client_mutexes[id]) != 0) {
                //TODO pensar como tratar do erro
                printf("morreu\n");
                pthread_exit(NULL);
            }
        }
        printf("Acordou\n");
        if (treat_request_thread(id) == -1) {
            message->op_code = -1;
            //TODO oq fazer se a thread for morta aqui e o servidor continuar a mandar pedidos???
            pthread_mutex_unlock(&client_mutexes[id]);

            /*
             * Vejo duas hipotesses aqui:
             *  a) Para além de FREE e TAKEN, acrescentamos BROKEN e matamos esta thread
             *  b) Voltamos a meter id = FREE e não matamos a thread
             */ 

            pthread_exit(NULL);
        }
        message->op_code = -1;
        pthread_mutex_unlock(&client_mutexes[id]);
    }
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

    if (server_init() == -1) {
        return -1;
    }

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
            fclose(fserv);
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
