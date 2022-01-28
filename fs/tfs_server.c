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
//TODO: Verificar se estamos sempre a devolver erro ao cliente (escrevendo no pipe) --> Talvez criar função para isso
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
    tfs_init();
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
        printf("servidor mount: Não havia ID livre\n");
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

    // printf("Servidor unmount: Operação começou\n");
    // printf("Servidor unmount: Vamos ver se o id estava de facto taken\n");
    if (session_id_is_free(id)) {
        /* There was nothing to unmount */
        return -1;
    }

    // printf("Servidor unmount: Vamos abrir o pipe do cliente\n");
    if ((fcli = fopen(client_pipes[id], "w")) == NULL) {
        perror("Erro");
        return -1;
    }

    // printf("Servidor unmount: Vamos terminar a sessão\n");
    terminate_session(id);

    // printf("Servidor unmount: Vamos escrever o resultado\n");
    size_written = fwrite(&operation_result, sizeof(int), 1, fcli);

    if ((size_written * sizeof(int)) < sizeof(int)) {
        return -1;
    }

    // printf("Servidor unmount: Vamos fechar o pipe do cliente\n");
    if (fclose(fcli) != 0) {
        perror("Erro");
        return -1;
    }

    // printf("Servidor unmount: Operação terminada com sucesso\n");

    return 0;
}

int treat_open_request(int id, char *name, int flags) {
    //TODO implementar
    FILE *fcli;
    int operation_result;
    size_t size_written = 0;
    // // // printf("Servidor treat_open_request: A começar a operação\n");

    /* Open client pipe */
    // // // printf("Servidor open request: Vamos abrir o pipe do cliente em write\n");
    if ((fcli = fopen(client_pipes[id], "w")) == NULL) {
        return -1;
    }

    // // // printf("Servidor open request: Vamos chamar tfs open\n");
    operation_result = tfs_open(name, flags);

    // // // printf("Servidor open request: Vamos escrever o resultado no pipe do cliente    \n");
    size_written = fwrite(&operation_result, sizeof(int), 1, fcli);
    if ((size_written * sizeof(int)) < sizeof(int)) {
        return -1;
    }

    if (fclose(fcli) != 0) {
        return -1;
    }

    // // // // printf("Servidor treat_open_request: Operação terminada com sucesso\n");

    return 0;
}

int treat_close_request(int id, int fhandle) {
    FILE *fcli;
    int operation_result = 0;
    size_t size_written = 0;
    // // // printf("Servidor treat_open_request: A começar a operação\n");

    /* Open client pipe */
    // // // printf("Servidor open request: Vamos abrir o pipe do cliente em write\n");
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
    // // // printf("Servidor treat_write_request: A começar a operação\n");

    /* Open client pipe */
    // // // printf("Servidor write request: Vamos abrir o pipe do cliente em write\n");
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
    // // // printf("Servidor treat_write_request: A começar a operação\n");

    /* Open client pipe */
    // // // printf("Servidor write request: Vamos abrir o pipe do cliente em write\n");
    if ((fcli = fopen(client_pipes[id], "w")) == NULL) {
        return -1;
    }

    operation_result = tfs_read(fhandle, buff, len);



    size_written = fwrite(&operation_result, sizeof(ssize_t), 1, fcli);
    if ((size_written * sizeof(ssize_t)) < sizeof(ssize_t)) {
        return -1;
    }

    if (operation_result != -1) {
        size_written = fwrite(buff, sizeof(char), operation_result, fcli);
    
        if ((size_written * sizeof(ssize_t)) < sizeof(ssize_t)) {
            return -1;
        }
    }


    if (fclose(fcli) != 0) {
        return -1;
    }
    // printf("\n\n O número de bytes lidos foi %ld\n\n", operation_result);
    // printf("Servidor tfs read: Operação terminada\n");

    return operation_result;
}

int treat_request_shutdown(int id) {
    FILE *fcli;
    ssize_t operation_result = 0;
    size_t size_written = 0;

    /* Open client pipe */
    // // // printf("Servidor write request: Vamos abrir o pipe do cliente em write\n");
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
    char pipe_path[PIPE_PATH_SIZE]; 
    char path[PIPE_PATH_SIZE];

    // printf("O op_code é %d\n", op_code);
    // printf("Servidor treat_request: Vamos começar a operação\n");
    // printf("Servidor treat_request: o opcode é %d\n", op_code);
    if (op_code == TFS_OP_CODE_MOUNT) {
        /* Skip op code */
        fread(path, sizeof(char), sizeof(path), fserv);
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
        // printf("Servidor treat request: VAMOS TRATAR DE UM UNMOUNT\n");
        fread(&session_id, sizeof(int), 1, fserv);

        if (!valid_id(session_id) || tfs_unmount(session_id) == -1) {
            printf("Servidor treat request (unmount): O pedido falhou\n");
            return -1;
        }
    }
    else if (op_code == TFS_OP_CODE_OPEN) {
        // printf("Servidor treat request: Vamos tratar de um open\n");
        char name[FILE_NAME_SIZE];
        int flags;
        fread(&session_id, sizeof(int), 1, fserv);
        // printf("Leu o session id que é = a %d\n", session_id);
        fread(name, sizeof(char), FILE_NAME_SIZE, fserv);
        // printf("Leu o nome do ficheiro que é %s\n", name);
        fread(&flags, sizeof(int), 1, fserv);
        // printf("Conseguiu ler tudo\n");

        if (!valid_id(session_id) || treat_open_request(session_id, name, flags) == -1) {
            printf("Servidor treat request (open): O pedido falhou\n");
            return -1;
        }
    }
    else if (op_code == TFS_OP_CODE_CLOSE) {
        // printf("Servidor treat request: Vamos tratar de um close\n");
        int fhandle;
        fread(&session_id, sizeof(int), 1, fserv);
        fread(&fhandle, sizeof(int), 1, fserv);

        if (!valid_id(session_id) || treat_close_request(session_id, fhandle) == -1) {
            printf("Servidor treat request (close): O pedido falhou\n");
            return -1;
        }
    }
    else if (op_code == TFS_OP_CODE_WRITE) {
        // printf("Servidor treat request: Vamos tratar de um write\n");
        int fhandle;
        fread(&session_id, sizeof(int), 1, fserv);
        fread(&fhandle, sizeof(int), 1, fserv);
        fread(&len, sizeof(size_t), 1, fserv);
        char buffer[len];

        fread(&buffer, sizeof(char), sizeof(buffer), fserv);

        if (!valid_id(session_id) || treat_write_request(session_id, fhandle, len, buffer) == -1) {
            printf("Servidor treat request (write): O pedido falhou\n");
            return -1;
        }
    }
    else if (op_code == TFS_OP_CODE_READ) {
        // printf("Servidor treat request: Vamos tratar de um read\n");
        int fhandle;
        fread(&session_id, sizeof(int), 1, fserv);
        fread(&fhandle, sizeof(int), 1, fserv);
        fread(&len, sizeof(size_t), 1, fserv);

        if (!valid_id(session_id) || treat_request_read(session_id, fhandle, len) == -1) {
            return -1;
        }
    }
    else if (op_code == TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED) {
        // printf("Servidor treat request: Vamos tratar de um shutdown\n");
        fread(&session_id, sizeof(int), 1, fserv);

        if (!valid_id(session_id) || treat_request_shutdown(session_id) == -1) {
            return -1;
        }
    }
    else {
        printf("Servidor treat_request: O pedido não é válido\n");
        return -1;
    }

    // printf("Servidor treat_request: O pedido foi executado com exito\n");

    return 0;
}



int main(int argc, char **argv) {
    FILE *fserv;
    size_t r_buffer;
    char buff = '\0'; //Valor temporário
    // // int fcli[10];

    if (argc < 2) {
        printf("Plea    se specify the pathname of the server's pipe.\n");
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
    // printf("Servidor: Vamos tentar abrir o pipe do servidor em read\n");
    if ((fserv = fopen(pipename, "r")) == NULL) {
        printf("Servidor: Falhou ao abrir o lado do servidor\n");
        return -1;
    }

    /* TO DO */
    /* Main loop */
    while (1) {
        /* Read requests from pipe */
        // printf("Servidor main loop: Vamos ler o op code\n");
        r_buffer = fread(&buff, 1, 1, fserv);
        if (r_buffer == 0) {
            fclose(fserv);
            if ((fserv = fopen(pipename, "r")) == NULL) {
                printf("Servidor: Falhou ao abrir o lado do servidor\n");
                return -1;
            }
            continue;
        }

        if (treat_request(buff, fserv) == -1) {
            printf("Servidor: Falhou ao tratar do pedido\n");
            return -1;
        }
    }

    return 0;
}
