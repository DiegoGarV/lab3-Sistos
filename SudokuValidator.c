#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <omp.h>

#define SIZE 9

int sudoku[SIZE][SIZE];
bool columnas_validas = false;
bool filas_validas = false;
bool subcuadros_validos = false;

// Validad filas
void* validate_rows(void* arg) {
    int i, j;
    omp_set_num_threads(SIZE);
    #pragma omp parallel for private(j) shared(sudoku) schedule(dynamic)
    for (i = 0; i < SIZE; i++) {
        bool seen[SIZE] = { false };
        for (j = 0; j < SIZE; j++) {
            int num = sudoku[i][j];
            if (num < 1 || num > SIZE || seen[num - 1]) {
                printf("Fila %d no válida.\n", i + 1);
                pthread_exit(NULL);
            }
            seen[num - 1] = true;
        }
    }
    printf("Todas las filas son válidas.\n");
    pthread_exit(NULL);
}

// Validad columnas
void* validate_columns(void* arg) {
    pid_t tid = syscall(SYS_gettid);
    printf("El thread que ejecuta el método para ejecutar el metodo de revision de columnas es: %d\n", tid);
    int i, j;

    omp_set_num_threads(SIZE);
    #pragma omp parallel for private(i) shared(sudoku) schedule(dynamic)
    for (j = 0; j < SIZE; j++) {
        pid_t local_tid = syscall(SYS_gettid);
        printf("En la revision de columnas el siguiente es un thread en ejecucion: %d\n", local_tid);
        bool seen[SIZE] = { false };
        for (i = 0; i < SIZE; i++) {
            int num = sudoku[i][j];
            if (num < 1 || num > SIZE || seen[num - 1]) {
                printf("Columna %d no válida.\n", j + 1);
                pthread_exit(NULL);
            }
            seen[num - 1] = true;
        }
    }
    printf("Todas las columnas son válidas.\n");
    pthread_exit(NULL);
}

// Validad subcuadros 3x3
void* validate_subgrids(void* arg) {
    int* pos = (int*)arg;
    int rowStart = pos[0];
    int colStart = pos[1];
    bool seen[SIZE] = { false };

    omp_set_num_threads(3);
    #pragma omp parallel for shared(sudoku) schedule(dynamic)
    for (int i = rowStart; i < rowStart + 3; i++) {
        for (int j = colStart; j < colStart + 3; j++) {
            int num = sudoku[i][j];
            if (num < 1 || num > SIZE || seen[num - 1]) {
                printf("Subcuadro comenzando en [%d,%d] no válido.\n", rowStart + 1, colStart + 1);
                pthread_exit(NULL);
            }
            seen[num - 1] = true;
        }
    }

    printf("Subcuadro comenzando en [%d,%d] es válido.\n", rowStart + 1, colStart + 1);
    pthread_exit(NULL);
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <archivo_sudoku>\n", argv[0]);
        return 1;
    }

    omp_set_num_threads(1);
    omp_set_nested(true);

    const char* filename = argv[1];

    // Abrir el archivo
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        perror("Error al abrir el archivo");
        return 1;
    }

    struct stat st;
    if (fstat(fd, &st) == -1) {
        perror("Error al obtener tamaño del archivo");
        close(fd);
        return 1;
    }

    if (st.st_size < 81) {
        fprintf(stderr, "El archivo no contiene suficientes caracteres.\n");
        close(fd);
        return 1;
    }

    // Mapear a memoria
    char* data = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED) {
        perror("Error al mapear el archivo");
        close(fd);
        return 1;
    }

    // Copiar a la matriz sudoku
    for (int i = 0; i < SIZE * SIZE; i++) {
        char ch = data[i];
        if (ch < '1' || ch > '9') {
            fprintf(stderr, "Carácter inválido en la posición %d: %c\n", i, ch);
            munmap(data, st.st_size);
            close(fd);
            return 1;
        }
        sudoku[i / 9][i % 9] = ch - '0';
    }

    // Cerrar el archivo para evitar fugas de memoria
    munmap(data, st.st_size);
    close(fd);

    // Crear hilos
    pthread_t row_thread, col_thread, subgrid_threads[3];
    int subgrid_args[3][2] = {
        {0, 0}, // subcuadro [1,1]
        {3, 3}, // subcuadro [4,4]
        {6, 6}  // subcuadro [7,7]
    };

    pthread_create(&row_thread, NULL, validate_rows, NULL);
    pthread_create(&col_thread, NULL, validate_columns, NULL);

    for (int i = 0; i < 3; i++) {
        pthread_create(&subgrid_threads[i], NULL, validate_subgrids, subgrid_args[i]);
    }

    // Esperar hilos
    pthread_join(row_thread, NULL);
    pthread_join(col_thread, NULL);
    for (int i = 0; i < 3; i++) {
        pthread_join(subgrid_threads[i], NULL);
    }

    printf("Validación del Sudoku completada.\n");
    printf("El thread en el que se ejecuta main es: %d\n", syscall(SYS_gettid));

    pid_t pid = getpid();
    pid_t ppid = getppid();
    pid_t child = fork();

    if (child == -1) {
        perror("Error al crear el proceso hijo");
        return 1;
    }

    if (child == 0) {
        // Proceso hijo
        char pid_str[20];
        snprintf(pid_str, sizeof(pid_str), "%d", pid); // Usamos el PID del padre, que es el proceso actual

        printf("Proceso hijo ejecutando: ps -p %s -lLf\n", pid_str);
        execlp("ps", "ps", "-p", pid_str, "-lLf", (char *)NULL);
        
        // Si execlp falla
        perror("Error al ejecutar ps");
        exit(1);
    } else {
        // Proceso padre
        printf("Soy el proceso padre con PID %d, mi hijo es %d\n", getpid(), child);
        wait(NULL); // Esperar a que el hijo termine
        printf("Sudoku resuelto!\n\nAntes de terminar el estado de este proceso y sus threads es: \n\n");
        fflush(stdout);

        char command[100];
        snprintf(command, sizeof(command), "ps -p %d -lLf", getpid());
        system(command);
    }

    return 0;
}
