#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <time.h>

#define MAX_FILES 100
#define TIME_FORMAT "%Y-%m-%dT%H:%M:%S"
#define BUFFER_SIZE 256

pid_t child_pids[MAX_FILES];
int pipes[MAX_FILES][2];
int num_children = 0;

// Variável partilhada para progresso
int *sensores_processados;
int total_sensores = 0;

// Função para mostrar barra de progresso
void mostrar_barra_progresso() {
    int feitos = *sensores_processados;
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

ssize_t readn(int fd, void *ptr, size_t n) {
    size_t nleft = n;
    ssize_t nread;
    while (nleft > 0) {
        if ((nread = read(fd, ptr, nleft)) <= 0) {
            if (nread == 0) break;
            return -1;
        }
        nleft -= nread;
        ptr += nread;
    }
    return (n - nleft);
}

ssize_t writen(int fd, const void *ptr, size_t n) {
    size_t nleft = n;
    ssize_t nwritten;
    while (nleft > 0) {
        if ((nwritten = write(fd, ptr, nleft)) <= 0) {
            if (nwritten < 0) return -1;
            break;
        }
        nleft -= nwritten;
        ptr += nwritten;
    }
    return (n - nleft);
}

void handle_sigint(int sig) {
    printf("\n[SINAL] SIGINT recebido. A terminar processos filho...\n");
    for (int i = 0; i < num_children; i++) {
        if (child_pids[i] > 0) {
            kill(child_pids[i], SIGTERM);
            waitpid(child_pids[i], NULL, 0);
        }
    }
    printf("Todos os processos filho terminados.\n");
    exit(0);
}

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

void process_file(const char *input_dir, const char *filename, int pipefd) {
    char fullpath[256];
    snprintf(fullpath, sizeof(fullpath), "%s/%s", input_dir, filename);

    int fd = open(fullpath, O_RDONLY);
    if (fd == -1) {
        perror("Erro ao abrir ficheiro");
        exit(1);
    }

    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        perror("Erro ao obter estatísticas");
        close(fd);
        exit(1);
    }

    char *data = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED) {
        perror("Erro ao mapear ficheiro");
        close(fd);
        exit(1);
    }

    close(fd);

    double soma = 0.0;
    int count = 0;
    double fora_limite = 0.0;
    char sensor[100], timestamp[64];
    double valor;
    time_t tempo_anterior = 0, tempo_atual;
    int em_fora_limite = 0;

    char *start = data;
    char *end = data;

    while (*end != '\n' && end < data + sb.st_size) end++;
    start = end + 1;
    end = start;

    while (end < data + sb.st_size) {
        if (*end == '\n' || end == data + sb.st_size - 1) {
            size_t len = end - start + 1;
            char line[len + 1];
            memcpy(line, start, len);
            line[len] = '\0';

            if (sscanf(line, "%99[^,],%lf,%63s", sensor, &valor, timestamp) == 3) {
                soma += valor;
                count++;
                tempo_atual = parse_timestamp(timestamp);

                if (is_out_of_bounds(filename, valor)) {
                    if (em_fora_limite) {
                        fora_limite += difftime(tempo_atual, tempo_anterior) / 3600.0;
                    }
                    tempo_anterior = tempo_atual;
                    em_fora_limite = 1;
                } else {
                    em_fora_limite = 0;
                    tempo_anterior = tempo_atual;
                }
            }

            start = end + 1;
        }
        end++;
    }

    munmap(data, sb.st_size);

    double media = count > 0 ? soma / count : 0.0;

    char output[BUFFER_SIZE];
    snprintf(output, sizeof(output), "%d;%s;%.2f;%.2f\n", getpid(), filename, media, fora_limite);
    writen(pipefd, output, strlen(output));

    //sleep(1);
    exit(0);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <pasta_input>\n", argv[0]);
        exit(1);
    }

    char *input_dir = argv[1];
    DIR *dir = opendir(input_dir);
    if (!dir) {
        perror("Erro ao abrir pasta");
        exit(1);
    }

    struct dirent *entry;
    char *filenames[MAX_FILES];
    int file_count = 0;

    signal(SIGINT, handle_sigint);

    while ((entry = readdir(dir)) != NULL && file_count < MAX_FILES) {
        if (entry->d_type == DT_REG) {
            filenames[file_count] = strdup(entry->d_name);
            file_count++;
        }
    }
    closedir(dir);

    printf("Foram encontrados %d ficheiros de sensores:\n", file_count);
    for (int i = 0; i < file_count; i++) {
        //sleep(1);
        printf(" -> %s\n", filenames[i]);

        if (pipe(pipes[i]) == -1) {
            perror("Erro a criar pipe");
            exit(1);
        }

        pid_t pid = fork();
        if (pid == 0) {
            close(pipes[i][0]);
            process_file(input_dir, filenames[i], pipes[i][1]);
        } else if (pid > 0) {
            child_pids[num_children++] = pid;
            close(pipes[i][1]);
        } else {
            perror("Erro no fork");
        }
    }

    sensores_processados = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE,
                                 MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (sensores_processados == MAP_FAILED) {
        perror("Erro ao criar mmap partilhado");
        exit(1);
    }
    *sensores_processados = 0;
    total_sensores = file_count;

    FILE *out = fopen("relatorio_final.txt", "w");
    if (!out) {
        perror("Erro ao criar relatorio_final.txt");
        exit(1);
    }

    int sensores_terminados = 0;
    int status;

    while (sensores_terminados < num_children) {
        for (int i = 0; i < num_children; i++) {
            if (child_pids[i] == -1) continue;

            pid_t pid = waitpid(child_pids[i], &status, WNOHANG);
            if (pid > 0) {
                char buffer[BUFFER_SIZE];
                ssize_t n = readn(pipes[i][0], buffer, BUFFER_SIZE);
                if (n > 0) {
                    buffer[n] = '\0';
                    fprintf(out, "%s", buffer);
                }
                close(pipes[i][0]);
                (*sensores_processados)++;
                child_pids[i] = -1;
                sensores_terminados++;
            }
        }

        mostrar_barra_progresso();  // Atualiza sempre a barra
        fflush(stdout);
        usleep(5000000);
    }

    mostrar_barra_progresso(); // Mostra 100% garantido no fim
    printf("\nTodos os processos filho terminaram com sucesso. Relatorio gerado.\n");

    fclose(out);
    for (int i = 0; i < file_count; i++) {
        free(filenames[i]);
    }
    munmap(sensores_processados, sizeof(int));
    return 0;
}
