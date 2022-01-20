#include "operations.h"


#define S 10 /* confirmar isto com os profs. É suposto ser o nº de possíveis sessões ativas */


struct session { /* Não sei se é isto que temos de fazer */
    char const *pipe;
    size_t session_id;
}


//TODO Ver se é preciso parametro size
int treat_request(char *buff) {

    /* TODO processar buffer */

    if (request == REQUEST_MOUNT) {
        /* TODO selecionar o 1º session id livre e devolver esse
         * Se não, o mount falhou */
    }
    else if {
        /* TODO Tratar de outros pedidos */
    }
    else {
        /* TO DO */
    }

}



int main(int argc, char **argv) {
    int fserv, r_buffer;
    int fcli[10];

    if (argc < 2) {
        printf("Please specify the pathname of the server's pipe.\n");
        return 1;
    }

    char *pipename = argv[1];
    printf("Starting TecnicoFS server with pipe called %s\n", pipename);

    unlink(pipename); /* Não sei se isto é preciso. Perguntar ao prof/piazza */

    /* Create server's named pipe */
    if (mkfifo(pipename) < 0) {
        return -1;
    }

    /* Open server's named pipe */
    if ((fserv = open(pipename, O_RDONLY)) < 0) {
        return -1;
    }


    /* TO DO */
    /* Main loop */
    while (1) {
        /* Read requests from pipe */
        r_buffer = read(fserv, buff, tamanho);

        /*
         * TODO Falta ver condição que é para terminar o loop
         * */

        treat_request(buff);    // TODO Add check for errors
    }


    unlink(pipename); /* Não sei se isto é preciso. Perguntar ao prof/piazza */

    return 0;
}
