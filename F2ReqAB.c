/*
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>

#define MAX_FILES 100
#define TIME_FORMAT "%Y-%m-%dT%H:%M:%S"
#define BUFFER_SIZE 256

typedef struct {
    char filename[256];
    char input_dir[256];
    char resultado[BUFFER_SIZE];
} ThreadArg;

typedef struct {
    char resultado[BUFFER_SIZE];
} ResultadoSensor;

int total_sensores = 0;
int sensores_processados = 0;
ResultadoSensor resultados[MAX_FILES];
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void strptime(const char * timestamp, char * str, struct tm * tm);

time_t parse_timestamp(const char *timestamp) {
    struct tm tm;
    memset(&tm, 0, sizeof(struct tm));
    strptime(timestamp, TIME_FORMAT, &tm);
    return mktime(&tm);
}

int is_out_of_bounds(const char *filename, double value) {
    if (strstr(filename, "Temperatura"))
        return value < 18 || value > 27;
    if (strstr(filename, "Humidade"))
        return value < 30 || value > 70;
    if (strstr(filename, "PM2.5"))
        return value > 25;
    if (strstr(filename, "PM10"))
        return value > 50;
    if (strstr(filename, "CO2"))
        return value > 1000;
    return 0;
}

void *processar_sensor(void *arg) {
    ThreadArg *data = (ThreadArg *)arg;

    char fullpath[512];
    snprintf(fullpath, sizeof(fullpath), "%s/%s", data->input_dir, data->filename);
    int fd = open(fullpath, O_RDONLY);
    if (fd == -1) {
        perror("Erro ao abrir ficheiro");
        pthread_exit(NULL);
    }

    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        perror("Erro ao obter estatísticas");
        close(fd);
        pthread_exit(NULL);
    }

    char *file_data = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (file_data == MAP_FAILED) {
        perror("Erro ao mapear ficheiro");
        close(fd);
        pthread_exit(NULL);
    }

    close(fd);

    double soma = 0.0, fora_limite = 0.0;
    int count = 0, em_fora_limite = 0;
    time_t tempo_anterior = 0, tempo_atual;
    char sensor[100], timestamp[64];
    double valor;

    char *start = file_data;
    char *end = file_data;

    while (*end != '\n' && end < file_data + sb.st_size) end++;
    start = end + 1;
    end = start;

    while (end < file_data + sb.st_size) {
        if (*end == '\n' || end == file_data + sb.st_size - 1) {
            size_t len = end - start + 1;
            char line[len + 1];
            memcpy(line, start, len);
            line[len] = '\0';

            if (sscanf(line, "%99[^,],%lf,%63s", sensor, &valor, timestamp) == 3) {
                soma += valor;
                count++;
                tempo_atual = parse_timestamp(timestamp);
                if (is_out_of_bounds(data->filename, valor)) {
                    if (em_fora_limite)
                        fora_limite += difftime(tempo_atual, tempo_anterior) / 3600.0;
                    em_fora_limite = 1;
                    tempo_anterior = tempo_atual;
                } else {
                    em_fora_limite = 0;
                    tempo_anterior = tempo_atual;
                }
            }

            start = end + 1;
        }
        end++;
    }

    munmap(file_data, sb.st_size);

    double media = (count > 0) ? soma / count : 0.0;
    snprintf(data->resultado, BUFFER_SIZE, "%ld;%s;%.2f;%.2f\n", pthread_self(), data->filename, media, fora_limite);

    pthread_mutex_lock(&mutex);
    strcpy(resultados[sensores_processados].resultado, data->resultado);
    sensores_processados++;
    pthread_mutex_unlock(&mutex);

    pthread_exit(NULL);
}

void *barra_progresso(void *arg) {
    while (1) {
        pthread_mutex_lock(&mutex);
        int feitos = sensores_processados;
        pthread_mutex_unlock(&mutex);

        int percentagem = (int)(((float)feitos / total_sensores) * 100);
        printf("\rProgresso: [");
        int blocos = percentagem / 10;
        for (int i = 0; i < 10; i++) {
            if (i < blocos) printf("=");
            else if (i == blocos) printf(">");
            else printf(" ");
        }
        printf("] %d%%", percentagem);
        fflush(stdout);

        if (feitos >= total_sensores) break;
        sleep(1);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <pasta_input>\n", argv[0]);
        return 1;
    }

    DIR *dir = opendir(argv[1]);
    if (!dir) {
        perror("Erro ao abrir pasta");
        return 1;
    }

    struct dirent *entry;
    char *filenames[MAX_FILES];

    while ((entry = readdir(dir)) != NULL && total_sensores < MAX_FILES) {
        if (entry->d_type == DT_REG) {
            filenames[total_sensores] = strdup(entry->d_name);
            total_sensores++;
        }
    }
    closedir(dir);

    pthread_t threads[MAX_FILES];
    ThreadArg args[MAX_FILES];

    pthread_t progresso_tid;
    pthread_create(&progresso_tid, NULL, barra_progresso, NULL);

    for (int i = 0; i < total_sensores; i++) {
        strncpy(args[i].filename, filenames[i], sizeof(args[i].filename));
        strncpy(args[i].input_dir, argv[1], sizeof(args[i].input_dir));
        pthread_create(&threads[i], NULL, processar_sensor, &args[i]);
    }

    for (int i = 0; i < total_sensores; i++) {
        pthread_join(threads[i], NULL);
        free(filenames[i]);
    }

    pthread_join(progresso_tid, NULL);
    printf("\nTodos os sensores foram processados.\n");

    FILE *out = fopen("relatorio_final.txt", "w");
    if (!out) {
        perror("Erro ao criar relatorio_final.txt");
        return 1;
    }

    for (int i = 0; i < total_sensores; i++) {
        fprintf(out, "%s", resultados[i].resultado);
    }

    fclose(out);
    printf("Relatório final gerado com %d sensores.\n", total_sensores);
    return 0;
}
*/
