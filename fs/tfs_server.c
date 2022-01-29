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
    bytes_written = fwrite(&id, sizeof(int), 1, fcli);
    if ((bytes_written * sizeof(int)) != sizeof(int)) {
        return -1;
    }

    if (fclose(fcli) != 0) {
        /* Failed to close file */
        return -1;
    }

    return id;
}

int tfs_unmount(int id) {
    //TODO Falta escrever no pipe quando falha
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

int treat_open_request(int id, char *name, int flags) {
    //TODO implementar
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
        char name[FILE_NAME_SIZE];
        int flags;
        if (fread(&session_id, 1, sizeof(int), fserv) != sizeof(int)) {
            return -1;
        }
        if (fread(name, sizeof(char), FILE_NAME_SIZE, fserv) != FILE_NAME_SIZE) {
            return -1;
        }
        if (fread(&flags, 1, sizeof(int), fserv) != sizeof(int)) {
            return -1;
        }

        if (!valid_id(session_id) || treat_open_request(session_id, name, flags) == -1) {
            return -1;
        }
    }
    else if (op_code == TFS_OP_CODE_CLOSE) {
        int fhandle;
        if (fread(&session_id, 1, sizeof(int), fserv) != sizeof(int)) {
            return -1;
        }
        if (fread(&fhandle, 1, sizeof(int), fserv) != sizeof(int)) {
            return -1;
        }

        if (!valid_id(session_id) || treat_close_request(session_id, fhandle) == -1) {
            return -1;
        }
    }
    else if (op_code == TFS_OP_CODE_WRITE) {
        int fhandle;
        if (fread(&session_id, 1, sizeof(int), fserv) != sizeof(int)) {
            return -1;
        }
        if (fread(&fhandle, 1, sizeof(int), fserv) != sizeof(int)) {
            return -1;
        }
        if (fread(&len, 1, sizeof(size_t), fserv) != sizeof(size_t)) {
            return -1;
        }
        char buffer[len];
        if (fread(&buffer, sizeof(char), sizeof(buffer), fserv) != sizeof(buffer)) {
            return -1;
        }

        if (!valid_id(session_id) || treat_write_request(session_id, fhandle, len, buffer) == -1) {
            return -1;
        }
    }
    else if (op_code == TFS_OP_CODE_READ) {
        int fhandle;
        if (fread(&session_id, 1, sizeof(int), fserv) != sizeof(int)) {
            return -1;
        }
        if (fread(&fhandle, 1, sizeof(int), fserv) != sizeof(int)) {
            return -1;
        }
        if (fread(&len, 1, sizeof(size_t), fserv) != sizeof(size_t)) {
            return -1;
        }

        if (!valid_id(session_id) || treat_request_read(session_id, fhandle, len) == -1) {
            return -1;
        }
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
