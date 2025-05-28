// Requisito C: Sistema Produtor-Consumidor com Threads (com escrita direta no ficheiro, input_dir corrigido e barra de progresso)

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
#define BUFFER_SIZE_QUEUE 10

// Elemento da fila (tarefa)
typedef struct {
    char filename[256];
    char input_dir[256];
} TarefaSensor;

TarefaSensor fila[BUFFER_SIZE_QUEUE];
int inicio = 0, fim = 0, total = 0;
int total_sensores = 0;
int sensores_processados = 0;
int done = 0;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_prod = PTHREAD_COND_INITIALIZER;
pthread_cond_t cond_cons = PTHREAD_COND_INITIALIZER;
FILE *out;
char input_dir[256];

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

void mostrar_barra_progresso() {
    int feitos;
    pthread_mutex_lock(&file_mutex);
    feitos = sensores_processados;
    pthread_mutex_unlock(&file_mutex);

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
}

void *barra_thread(void *arg) {
    while (1) {
        mostrar_barra_progresso();
        pthread_mutex_lock(&file_mutex);
        if (sensores_processados >= total_sensores) {
            pthread_mutex_unlock(&file_mutex);
            break;
        }
        pthread_mutex_unlock(&file_mutex);
        sleep(1);
    }
    mostrar_barra_progresso();
    printf("\n");
    return NULL;
}

void inserir_tarefa(TarefaSensor tarefa) {
    pthread_mutex_lock(&mutex);
    while (total == BUFFER_SIZE_QUEUE)
        pthread_cond_wait(&cond_prod, &mutex);
    fila[fim] = tarefa;
    fim = (fim + 1) % BUFFER_SIZE_QUEUE;
    total++;
    pthread_cond_signal(&cond_cons);
    pthread_mutex_unlock(&mutex);
}

TarefaSensor retirar_tarefa() {
    pthread_mutex_lock(&mutex);
    while (total == 0) {
        if (done) {
            pthread_mutex_unlock(&mutex);
            TarefaSensor t = { .filename = "", .input_dir = "" };
            return t;
        }
        pthread_cond_wait(&cond_cons, &mutex);
    }
    TarefaSensor t = fila[inicio];
    inicio = (inicio + 1) % BUFFER_SIZE_QUEUE;
    total--;
    pthread_cond_signal(&cond_prod);
    pthread_mutex_unlock(&mutex);
    return t;
}

void *produtor(void *arg) {
    char **filenames = (char **)arg;
    for (int i = 0; i < total_sensores; i++) {
        TarefaSensor t;
        strncpy(t.filename, filenames[i], sizeof(t.filename));
        strncpy(t.input_dir, input_dir, sizeof(t.input_dir));
        inserir_tarefa(t);
        free(filenames[i]);
    }
    pthread_mutex_lock(&mutex);
    done = 1;
    pthread_cond_broadcast(&cond_cons);
    pthread_mutex_unlock(&mutex);
    return NULL;
}

void *consumidor(void *arg) {
    while (1) {
        TarefaSensor data = retirar_tarefa();
        if (strlen(data.filename) == 0) break;

        char fullpath[512];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", data.input_dir, data.filename);
        int fd = open(fullpath, O_RDONLY);
        if (fd == -1) continue;

        struct stat sb;
        if (fstat(fd, &sb) == -1) {
            close(fd);
            continue;
        }

        char *file_data = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (file_data == MAP_FAILED) {
            close(fd);
            continue;
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
                    if (is_out_of_bounds(data.filename, valor)) {
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
        char resultado[BUFFER_SIZE];
        snprintf(resultado, BUFFER_SIZE, "%ld;%s;%.2f;%.2f\n", pthread_self(), data.filename, media, fora_limite);

        pthread_mutex_lock(&file_mutex);
        fprintf(out, "%s", resultado);
        fflush(out);
        sensores_processados++;
        pthread_mutex_unlock(&file_mutex);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <pasta_input>\n", argv[0]);
        return 1;
    }

    strncpy(input_dir, argv[1], sizeof(input_dir));

    DIR *dir = opendir(argv[1]);
    if (!dir) {
        perror("Erro ao abrir pasta");
        return 1;
    }

    char *filenames[MAX_FILES];
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL && total_sensores < MAX_FILES) {
        if (entry->d_type == DT_REG) {
            filenames[total_sensores] = strdup(entry->d_name);
            total_sensores++;
        }
    }
    closedir(dir);

    out = fopen("relatorio_final.txt", "w");
    if (!out) {
        perror("Erro ao criar relatorio_final.txt");
        return 1;
    }

    pthread_t prod_tid;
    pthread_t cons_tid[4];
    pthread_t barra_tid;

    pthread_create(&barra_tid, NULL, barra_thread, NULL);
    pthread_create(&prod_tid, NULL, produtor, filenames);
    for (int i = 0; i < 4; i++) pthread_create(&cons_tid[i], NULL, consumidor, NULL);

    pthread_join(prod_tid, NULL);
    for (int i = 0; i < 4; i++) pthread_join(cons_tid[i], NULL);
    pthread_join(barra_tid, NULL);

    fclose(out);
    printf("Relatório final gerado com %d sensores.\n", total_sensores);
    return 0;
}
