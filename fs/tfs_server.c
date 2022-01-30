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
static pthread_t client_threads[S];
static struct request prod_cons_buffer[S];
static int prod_ptr = 0;
static int cons_ptr = 0;

void server_init() {
    tfs_init();
    for (size_t i = 0; i < S; i++) {
        session_ids[i] = FREE;

        for (size_t j = 0; j < PIPE_PATH_SIZE; j++) {
            client_pipes[i][j] = '\0';
        }
    }

    prod_ptr = 0;
    cons_ptr = 0;

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

void terminate_session(int id) {
    session_ids[id] = FREE;

    for (size_t i = 0; i < PIPE_PATH_SIZE; i++) {
        if (client_pipes[id][i] == '\0') {
            break;
        }
        client_pipes[id][i] = '\0';
    }
}

int send_reply(const void *restrict ptr, FILE *fcli, size_t size) {
    size_t bytes_written;

    bytes_written = fwrite(ptr, size, 1, fcli);
    if ((bytes_written * size) < size) {
        return -1;
    }
    return 0;
}

void increment_prod_ptr() {
    if (++prod_ptr == S) {
        prod_ptr = 0;
    }
}

void increment_cons_ptr() {
    if (++cons_ptr == S) {
        cons_ptr = 0;
    }
}


void* tfs_mount(void *args) {
    int id;
    struct request *message = (struct request *) args;
    FILE *fcli;
    if ((fcli = fopen(message->buffer, "w")) == NULL) {
        printf("[ERRO]: Falhou ao abrir o pipe do cliente\n");
        pthread_exit((void *) -1);
    }
    id = get_free_session_id();


    /* update session id and path */
    session_ids[id] = TAKEN;
    strcpy(client_pipes[id], message->buffer);

    /* send operation result to client */
    if (send_reply(&id, fcli, sizeof(int)) == -1) {
        printf("[ERRO]: Falhou ao enviar a resposta ao cliente\n");
        if (fclose(fcli) != 0) {
            /* Failed to close file */
            printf("[ERRO]: Falhou ao fechar o pipe do cliente\n");
        }
        pthread_exit((void *) -1);
    }

    if (fclose(fcli) != 0) {
        /* Failed to close file */
        printf("[ERRO]: Falhou ao fechar o pipe do cliente\n");
        pthread_exit((void *) -1);
    }
    
    pthread_exit(0);

}

void* tfs_unmount(void *args) {
    struct request *message = (struct request *) args;
    int operation_result = 0;
    FILE *fcli;


    if ((fcli = fopen(client_pipes[message->session_id], "w")) == NULL) {
        printf("[ERRO]: Falhou ao abrir o pipe do cliente\n");
        pthread_exit((void *) -1);
    }

    terminate_session(message->session_id);

    /* send operation result to client */
    if (send_reply(&operation_result, fcli, sizeof(int)) == -1) {
        printf("[ERRO]: Falhou ao enviar a resposta ao cliente\n");
        if (fclose(fcli) != 0) {
            /* Failed to close file */
            printf("[ERRO]: Falhou ao fechar o pipe do cliente\n");
        }
        pthread_exit((void *) -1);
    }

    if (fclose(fcli) != 0) {
        printf("[ERRO]: Falhou ao fechar o pipe do cliente\n");
        pthread_exit((void *) -1);
    }

    pthread_exit(0);
}

void* treat_open_request(void *args) {
    FILE *fcli;
    int operation_result;
    struct request *message = (struct request *) args;

    /* Open client pipe */
    if ((fcli = fopen(client_pipes[message->session_id], "w")) == NULL) {
        printf("[ERRO]: Falhou ao abrir o pipe do cliente\n");
        pthread_exit((void *) -1);
    }

    operation_result = tfs_open(message->buffer, message->flags);

    /* send operation result to client */
    if (send_reply(&operation_result, fcli, sizeof(int)) == -1) {
        printf("[ERRO]: Falhou ao enviar a resposta ao cliente\n");
        if (fclose(fcli) != 0) {
            /* Failed to close file */
            printf("[ERRO]: Falhou ao fechar o pipe do cliente\n");
        }
        pthread_exit((void *) -1);
    }

    if (fclose(fcli) != 0) {
        printf("[ERRO]: Falhou ao fechar o pipe do cliente\n");
        pthread_exit((void *) -1);
    }

    pthread_exit(0);

}

void* treat_close_request(void *args) {
    FILE *fcli;
    int operation_result = 0;
    struct request *message = (struct request *) args;

    /* Open client pipe */
    if ((fcli = fopen(client_pipes[message->session_id], "w")) == NULL) {
        printf("[ERRO]: Falhou ao abrir o pipe do cliente\n");
        pthread_exit((void *) -1);
    }

    operation_result = tfs_close(message->fhandle);

    /* send operation result to client */
    if (send_reply(&operation_result, fcli, sizeof(int)) == -1) {
        printf("[ERRO]: Falhou ao enviar a resposta ao cliente\n");
        if (fclose(fcli) != 0) {
            /* Failed to close file */
            printf("[ERRO]: Falhou ao fechar o pipe do cliente\n");
        }
        pthread_exit((void *) -1);
    }

    if (fclose(fcli) != 0) {
        printf("[ERRO]: Falhou ao fechar o pipe do cliente\n");
        pthread_exit((void *) -1);
    }

    pthread_exit(0);
}

void* treat_write_request(void *args) {
    FILE *fcli;
    ssize_t operation_result = 0;
    struct request *message = (struct request *) args;

    /* Open client pipe */
    if ((fcli = fopen(client_pipes[message->session_id], "w")) == NULL) {
        printf("[ERRO]: Falhou ao abrir o pipe do cliente\n");
        pthread_exit((void *) -1);
    }
    operation_result = tfs_write(message->fhandle, message->dynamic_buffer, message->len);

    /* send operation result to client */
    if (send_reply(&operation_result, fcli, sizeof(ssize_t)) == -1) {
        printf("[ERRO]: Falhou ao enviar a resposta ao cliente\n");
        if (fclose(fcli) != 0) {
            /* Failed to close file */
            printf("[ERRO]: Falhou ao fechar o pipe do cliente\n");
        }
        pthread_exit((void *) -1); 
    }

    if (fclose(fcli) != 0) {
        printf("[ERRO]: Falhou ao fechar o pipe do cliente\n");
        pthread_exit((void *) -1);
    }

    pthread_exit(0);
}

void* treat_request_read(void *args) {
    struct request *message = (struct request *) args;
    char *buff = (char *) malloc(sizeof(char) * message->len);
    FILE *fcli;
    ssize_t operation_result = 0;
    size_t size_written = 0;


    if (buff == NULL) {
        operation_result = -1;
    }

    /* Open client pipe */
    if ((fcli = fopen(client_pipes[message->session_id], "w")) == NULL) {
        printf("[ERRO]: Falhou ao abrir o pipe do cliente\n");
        if (buff != NULL) {
            free(buff);
        }
        pthread_exit((void *) -1);
    }

    if (operation_result == -1) {
        printf("[ERRO] (tfs_read): Falhou ao alocar memória para o buffer\n");
        /* Failed to alocate memory for buff */
        if (send_reply(&operation_result, fcli, sizeof(ssize_t)) == -1) {
            printf("[ERRO]: Falhou ao enviar a resposta ao cliente\n");
            if (fclose(fcli) != 0) {
                /* Failed to close file */
                printf("[ERRO]: Falhou ao fechar o pipe do cliente\n");
            }
            pthread_exit((void *) -1);
        }
        pthread_exit((void *) -1);
    }

    operation_result = tfs_read(message->fhandle, message->buffer, message->len);

    /* send operation result to client */
    if (send_reply(&operation_result, fcli, sizeof(ssize_t)) == -1) {
        free(buff);
        printf("[ERRO]: Falhou ao enviar a resposta ao cliente\n");
        if (fclose(fcli) != 0) {
            /* Failed to close file */
            printf("[ERRO]: Falhou ao fechar o pipe do cliente\n");
        }
        pthread_exit((void *) -1);
    }

    if (operation_result != -1) {
        /* Send what was read from tfs */
        size_written = fwrite(buff, sizeof(char), (size_t) operation_result, fcli);
    
        if ((size_written * sizeof(ssize_t)) < sizeof(ssize_t)) {
            printf("[ERRO] (tfs read): Falhou ao dizer ao cliente aquilo que foi lido\n");
            free(buff);
            if (fclose(fcli) != 0) {
                /* Failed to close file */
                printf("[ERRO]: Falhou ao fechar o pipe do cliente\n");
            }
            pthread_exit((void *) -1);
        }
    }


    free(buff);
    if (fclose(fcli) != 0) {
        printf("[ERRO]: Falhou ao fechar o pipe do cliente\n");
        pthread_exit((void *) -1);
    }

    pthread_exit(0);
}

void* treat_request_shutdown(void *args) {
    FILE *fcli;
    ssize_t operation_result = 0;
    struct request *message = (struct request *) args;
    
    /* Open client pipe */
    if ((fcli = fopen(client_pipes[message->session_id], "w")) == NULL) {
        printf("[ERRO]: Falhou ao abrir o pipe do cliente\n");
        pthread_exit((void *) -1);
    }

    operation_result = tfs_destroy_after_all_closed();

    /* send operation result to client */
    if (send_reply(&operation_result, fcli, sizeof(int)) == -1) {
        printf("[ERRO]: Falhou ao enviar a resposta ao cliente\n");
        if (fclose(fcli) != 0) {
            printf("[ERRO]: Falhou ao fechar o pipe do cliente\n");
        }
        pthread_exit((void *) -1);
    }

    if (fclose(fcli) != 0) {
        printf("[ERRO]: Falhou ao fechar o pipe do cliente\n");
        pthread_exit((void *) -1);
    }

    // if (operation_result == -1) {
    //     return -1;
    // }

    //TODO talvez fechar o pipe do servidor e dar unlink

    exit(0);
}

int read_request(char buff, FILE *fserv) {
    char op_code = buff;
    struct request *message = &(prod_cons_buffer[prod_ptr]);
    message->op_code = buff;


    if (op_code == TFS_OP_CODE_MOUNT) {

        if (fread(message->buffer, sizeof(char), PIPE_PATH_SIZE, fserv) != PIPE_PATH_SIZE) {
            printf("Servidor (read request): Falhou ao ler o buffer (TFS_MOUNT)\n");
            return -1;
        }

    }
    else if (op_code == TFS_OP_CODE_UNMOUNT ) {
        if (fread(&(message->session_id), 1, sizeof(int), fserv) != sizeof(int)) {
            printf("Servidor (read request): Falhou ao ler o session id (TFS_UNMOUNT)\n");
            return -1;
        }

    }
    else if (op_code == TFS_OP_CODE_OPEN) {

        if (fread(&(message->session_id), 1, sizeof(int), fserv) != sizeof(int)) {
            printf("Servidor (read request): Falhou ao ler o session id (TFS_OPEN)\n");
            return -1;
        }
        if (fread(message->buffer, sizeof(char), FILE_NAME_SIZE, fserv) != FILE_NAME_SIZE) {
            printf("Servidor (read request): Falhou ao ler o buffer (TFS_OPEN)\n");
            return -1;
        }
        if (fread(&(message->flags), 1, sizeof(int), fserv) != sizeof(int)) {
            printf("Servidor (read request): Falhou ao ler as flags (TFS_OPEN)\n");
            return -1;
        }

    }
    else if (op_code == TFS_OP_CODE_CLOSE) {
        if (fread(&(message->session_id), 1, sizeof(int), fserv) != sizeof(int)) {
            printf("Servidor (read request): Falhou ao ler o session id (TFS_CLOSE)\n");
            return -1;
        }
        if (fread(&(message->fhandle), 1, sizeof(int), fserv) != sizeof(int)) {
            printf("Servidor (read request): Falhou ao ler o fhandle (TFS_CLOSE)\n");
            return -1;
        }

    }
    else if (op_code == TFS_OP_CODE_WRITE) {
        if (fread(&(message->session_id), 1, sizeof(int), fserv) != sizeof(int)) {
            printf("Servidor (read request): Falhou ao ler o session_id (TFS_WRITE)\n");
            return -1;
        }
        if (fread(&(message->fhandle), 1, sizeof(int), fserv) != sizeof(int)) {
            printf("Servidor (read request): Falhou ao ler o fhandle (TFS_WRITE)\n");
            return -1;
        }
        if (fread(&(message->len), 1, sizeof(size_t), fserv) != sizeof(size_t)) {
            printf("Servidor (read request): Falhou ao ler o len (TFS WRITE)\n");
            return -1;
        }
        message->dynamic_buffer = (char *) malloc(sizeof(char) * message->len);
        if (message->dynamic_buffer == NULL) {
            printf("Servidor (read request): Falhou ao alocar memória para o dynamic array (TFS_WRITE)\n");
            return -1;
        }
        if (fread(message->dynamic_buffer, sizeof(char), message->len, fserv) != sizeof(message->dynamic_buffer)) {
            printf("Servidor (read request): Falhou ao ler o dynamic buffer/aquilo que é para escrever (TFS_WRITE)\n");
            return -1;
        }


    }
    else if (op_code == TFS_OP_CODE_READ) {

        if (fread(&(message->session_id), 1, sizeof(int), fserv) != sizeof(int)) {
            printf("Servidor (read request): Falhou ao ler o session id (TFS_READ)\n");
            return -1;
        }
        if (fread(&(message->fhandle), 1, sizeof(int), fserv) != sizeof(int)) {
            printf("Servidor (read request): Falhou ao ler o fhandle (TFS_READ)\n");
            return -1;
        }
        if (fread(&(message->len), 1, sizeof(size_t), fserv) != sizeof(size_t)) {
            printf("Servidor (read request): Falhou ao ler o len (TFS_READ)\n");
            return -1;
        }


    }
    else if (op_code == TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED) {
        if (fread(&(message->session_id), 1, sizeof(int), fserv) != sizeof(int)) {
            printf("Servidor (read request): Falhou ao ler o session id (SHUTDOWN AFTER ALL CLOSED)\n");
            return -1;
        }

    }
    else {
        printf("Servidor (read request): Falhou porque o op code não existia\n");
        return -1;
    }
    increment_prod_ptr();

    return 0;
}

int treat_request() {
    struct request *message = &(prod_cons_buffer[cons_ptr]);
    void *retval;

    if (message->op_code == TFS_OP_CODE_MOUNT) {
        int id = get_free_session_id();
        message->session_id = id;

        pthread_create(&client_threads[id], NULL, tfs_mount,(void*) message);

        //TODO acho que falta dar join aqui

        return 0;
    }
    
    if (!valid_id(message->session_id) || session_id_is_free(message->session_id)) {

        if (prod_ptr != cons_ptr) {
            /* If there are other tasks that can be consumed */
            increment_cons_ptr();
        }
        printf("Servidor (treat request): Falha porque o id não era válido / estava free\n");
        return -1;
    }

    
    if (message->op_code == TFS_OP_CODE_UNMOUNT) {
        pthread_create(&client_threads[message->session_id], NULL, tfs_unmount,(void*) message);
    }
    else if (message->op_code == TFS_OP_CODE_OPEN) {
        pthread_create(&client_threads[message->session_id], NULL, treat_open_request,(void*) message);
    }
    else if (message->op_code == TFS_OP_CODE_CLOSE) {
        pthread_create(&client_threads[message->session_id], NULL, treat_close_request,(void*) message);
        
    }
    else if (message->op_code == TFS_OP_CODE_WRITE) {
        pthread_create(&client_threads[message->session_id], NULL, treat_write_request,(void*) message);
        free(message->dynamic_buffer);
        
    }
    else if (message->op_code == TFS_OP_CODE_READ) {
        pthread_create(&client_threads[message->session_id], NULL, treat_request_read,(void*) message);
        
    }
    else if (message->op_code == TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED) {
        pthread_create(&client_threads[message->session_id], NULL, treat_request_shutdown,(void*) message);
        
    }
    else {
        if (prod_ptr != cons_ptr) {
            /* If there are other tasks that can be consumed */
            increment_cons_ptr();
        }
        printf("Servidor (treat request): Falha porque o opcode não existia\n");
        return -1;
    }

    // TODO fazer o join aqui ou no inicio da função? Inicio parece-me melhor, but idk
    /* Check/Wait if the client had not yet finished last operation */
    if (pthread_join(client_threads[message->session_id], &retval) != 0) {
        printf("Servidor (treat request): Falha ao fazer join da thread\n");
        return -1;
    }
    if (*((int *) retval) == -1) {
        //TODO tratar do erro que ocorreu a tratar do pedido
    }

    increment_cons_ptr();

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

    /* Main loop */
    while (1) {
        /* Read requests from pipe */
        r_buffer = fread(&buff, 1, 1, fserv);
        if (r_buffer == 0) {
            fclose(fserv);
            if ((fserv = fopen(pipename, "r")) == NULL) {
                printf("Servidor (main): Falha ao abrir o pipe do servidor\n");
                return -1;
            }
            continue;
        }

        /* Producer function */
        if (read_request(buff, fserv) == -1) {
            printf("Servidor (main): Falha ao ler o request\n");
            return -1;
        }

        //TODO ver os semaphores e mutexes para isto

        /* Consumer function */
        if (treat_request() == -1) {
            printf("Servidor (main): Falha ao tratar do pedido\n");
            return -1;
        }
    }


    return 0;
}