#include "operations.h"
#include "common/common.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h> // Not sure se este é preciso

//TODO: Redifine in config.h
//TODO: Verificar se estamos sempre a devolver erro ao cliente
//TODO: Perguntar ao prof se é para fechar (do lado do servidor) o pipe do cliente sempre que termina uma operação ou se é só no unmount
#define S 64 /* confirmar isto com os profs. É suposto ser o nº de possíveis sessões ativas */
#define FREE 1
#define TAKEN 0

struct session { /* Não sei se é isto que temos de fazer */
    char const *pipe;
    size_t session_id;
};

static int session_ids[S];
static char client_pipes[S][PIPE_PATH_SIZE];

void server_init() {
    for (size_t i = 0; i < S; i++) {
        session_ids[i] = FREE;

        for (size_t j = 0; j < PIPE_PATH_SIZE; j++) {
            client_pipes[i][j] = '\0';
        }
    }

}

int get_free_session_id() {

    // printf("Servidor get_free_session_id: Vamos começar o loop\n");
    for (int i = 0; i < S; i++) {
        // printf("Servidor get_free_session_id: o índice é %d e o id está ", i);
        if (session_ids[i] == FREE) {
            // printf("FREE\n");
            return i;
        }
        // printf("TAKEN\n");
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

int tfs_mount(char *path) {
    int id;
    size_t  bytes_written = 0;
    FILE *fcli;

    // printf("Servidor tfs_mount: Vamos começar a operação\n");

    // printf("Servidor tfs_mount: Vamos tentar abrir o pipe do client em write mode\n");
    // printf("Servidor tfs_mount: O path do pipe do cliente é %s\n", path);
    if ((fcli = fopen(path, "w")) == NULL) {
        perror("Erro: ");
        /* Failed to open client's side of the pipe */
        printf("Servidor tfs_mount: Falhou ao tentar abrir o pipe do cliente\n");
        return -1;
    }


    // printf("Servidor tfs_mount: Vamos tentar obter o primeiro session id livre\n");
    id = get_free_session_id();
    // printf("Servidor tfs_mount: O id é %d\n", id);
    if (id == -1) {
        bytes_written = fwrite(&id, sizeof(int), 1, fcli);

        return -1;
    }

    /* update session id and path */
    session_ids[id] = TAKEN;
    // printf("Servidor tfs_mount: O path guardado do pipe era %s\n", client_pipes[id]);
    strcpy(client_pipes[id], path);
    // printf("Servidor tfs_mount: O path guardado do pipe agora é %s\n", client_pipes[id]);
    // printf("Servidor tfs_mount: Vamos escrever o resultado no pipe do cliente\n");
    bytes_written = fwrite(&id, sizeof(int), 1, fcli);
    // printf("O número de bytes written é %ld e o sizeof(int) é %ld\n", bytes_written, sizeof(operation_result));
    if ((bytes_written * sizeof(int)) != sizeof(int)) {
        printf("Servidor tfs_mount: Falhou ao escrever o resultado da operação\n");
        return -1;
    }

    // printf("Servidor tfs_mount: Vamos fechar o pipe do cliente\n");
    if (fclose(fcli) != 0) {
        /* Failed to close file */
        printf("Servidor tfs_mount: Falhou ao fechar o pipe\n");
        return -1;
    }

    // printf("Servidor tfs_mount: Operação terminada com exito\n");

    return id;
}

int tfs_unmount(int id) {
    //TODO Falta escrever no pipe quando falha
    int operation_result = 0;
    size_t size_written = 0;
    FILE *fcli;

    if (!valid_id(id)) {
        return -1;
    }

    if (session_id_is_free(id)) {
        /* There was nothing to unmount */
        return -1;
    }

    if ((fcli = fopen(client_pipes[id], "w")) == NULL) {
        perror("Erro");
        return -1;
    }
    size_written = fwrite(&operation_result, sizeof(int), 1, fcli);

    if ((size_written * sizeof(int)) < sizeof(int)) {
        return -1;
    }

    if (fclose(fcli) != 0) {
        perror("Erro");
        return -1;
    }

    terminate_session(id);

    return 0;
}

int treat_request(char *buff, FILE *fserv) {
    int op_code = buff[0], session_id = -1;
    char pipe_path[PIPE_PATH_SIZE];
    char path[PIPE_PATH_SIZE + 1];

    // printf("Servidor treat_request: Vamos começar a operação\n");
    // printf("Servidor treat_request: o opcode é %d\n", op_code);
    if (op_code == TFS_OP_CODE_MOUNT) {
        /* Skip op code */
        fread(path, 1, sizeof(path), fserv);
        // printf("buff: %s\n", path);
        // printf("O buffer na sua inteiridade é %s\n", path);
        // printf("Servidor treat_request: Queremos fazer mount, por isso vamos copiar o client pipe path do buffer\n");
        // printf("pipe path é %s\n", path);
        // printf("Servidor treat_request: Vamos tentar fazer tfs_mount\n");
        if (tfs_mount(path) == -1) {
            printf("Servidor treat_request (mount): O pedido falhou\n");
            return -1;
        }
    }
    else if (op_code == TFS_OP_CODE_UNMOUNT ) {
        fread(&session_id, 1, sizeof(int), fserv);

        if (session_id == -1 || tfs_unmount(session_id) == -1) {
            printf("Servidor treat request (unmount): O pedido falhou\n");
            return -1;
        }
    }
    else if (op_code == TFS_OP_CODE_OPEN) {
    }
    else if (op_code == TFS_OP_CODE_CLOSE) {
    }
    else if (op_code == TFS_OP_CODE_WRITE) {
    }
    else if (op_code == TFS_OP_CODE_READ) {
    }
    else if (op_code == TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED) {
    }
    // else {
    //     //op code does not exist
    //     return -1;
    // }

    printf("Servidor treat_request: O pedido foi executado com exito\n");

    return 0;
}



int main(int argc, char **argv) {
    FILE *fserv;
    size_t r_buffer;
    char buff[41] = {'\0'}; //Valor temporário
    // // int fcli[10];

    if (argc < 2) {
        printf("Please specify the pathname of the server's pipe.\n");
        return 1;
    }

    char *pipename = argv[1];
    printf("Starting TecnicoFS server with pipe called %s\n", pipename);

    unlink(pipename); //Não sei se isto fica aqui

    server_init();
    // // unlink(pipename); /* Não sei se isto é preciso. Perguntar ao prof/piazza */

    /* Create server's named pipe */
    if (mkfifo(pipename, 0640) < 0) {
        printf("Servidor: Falhou ao criar o seu pipe\n");
        return -1;
    }

        /* Open server's named pipe */
        printf("Servidor: Vamos tentar abrir o pipe do servidor em read\n");
        if ((fserv = fopen(pipename, "r")) == NULL) {
            printf("Servidor: Falhou ao abrir o lado do servidor\n");
            return -1;
        }


    /* TO DO */
    /* Main loop */
    while (1) {
        // /* Open server's named pipe */
        // printf("Servidor: Vamos tentar abrir o pipe do servidor em read\n");
        // if ((fserv = fopen(pipename, "r")) == NULL) {
        //     printf("Servidor: Falhou ao abrir o lado do servidor\n");
        //     return -1;
        // }

        /* Read requests from pipe */
        // // // printf("l\n");
        printf("Servidor main loop: Vamos ler o op code\n");
        r_buffer = fread(buff, sizeof(int), 1, fserv); /* OP_CODE + id + missing flags */
        /*if (r_buffer == -1) {
            return -1;
        }        */
        /*if (fclose(fserv) != 0) {
            return -1;
        }*/

        /*
         * TODO Falta ver condição que é para terminar o loop
         * */

        if (treat_request(buff, fserv) == -1) {
            printf("Servidor: Falhou ao tratar do pedido\n");
            return -1;
        }

    }


    // // unlink(pipename); /* Não sei se isto é preciso. Perguntar ao prof/piazza */

    return 0;
}
