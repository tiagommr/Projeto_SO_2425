//
// Created by tiago on 14-05-2025.
//
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
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>

#define MAX_FILES 100
#define TIME_FORMAT "%Y-%m-%dT%H:%M:%S"
#define BUFFER_SIZE 256
#define SOCKET_PATH "/tmp/schoolair_socket"
/*
pid_t child_pids[MAX_FILES];
int num_children = 0;
int *sensores_processados;
int total_sensores = 0;

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
    unlink(SOCKET_PATH);
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

void process_file(const char *input_dir, const char *filename) {
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

    double soma = 0.0, fora_limite = 0.0;
    int count = 0, em_fora_limite = 0;
    time_t tempo_anterior = 0, tempo_atual;
    char sensor[100], timestamp[64];
    double valor;

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

    munmap(data, sb.st_size);

    double media = count > 0 ? soma / count : 0.0;
    char output[BUFFER_SIZE];
    snprintf(output, sizeof(output), "%d;%s;%.2f;%.2f\n", getpid(), filename, media, fora_limite);

    int sockfd;
    struct sockaddr_un addr;
    sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("Erro ao criar socket");
        exit(1);
    }

    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_un)) == -1) {
        perror("Erro ao ligar socket");
        close(sockfd);
        exit(1);
    }

    writen(sockfd, output, strlen(output));
    close(sockfd);
    exit(0);
}

int criar_socket_servidor() {
    int sockfd;
    struct sockaddr_un addr;
    unlink(SOCKET_PATH);

    sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("Erro ao criar socket");
        exit(1);
    }

    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_un)) == -1) {
        perror("Erro ao fazer bind");
        exit(1);
    }

    if (listen(sockfd, MAX_FILES) == -1) {
        perror("Erro no listen");
        exit(1);
    }

    return sockfd;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <pasta_input>\n", argv[0]);
        exit(1);
    }

    signal(SIGINT, handle_sigint);

    char *input_dir = argv[1];
    DIR *dir = opendir(input_dir);
    if (!dir) {
        perror("Erro ao abrir pasta");
        exit(1);
    }

    struct dirent *entry;
    char *filenames[MAX_FILES];
    int file_count = 0;

    while ((entry = readdir(dir)) != NULL && file_count < MAX_FILES) {
        if (entry->d_type == DT_REG) {
            filenames[file_count] = strdup(entry->d_name);
            file_count++;
        }
    }
    closedir(dir);

    printf("Foram encontrados %d ficheiros de sensores:\n", file_count);
    for (int i = 0; i < file_count; i++) {
        printf(" -> %s\n", filenames[i]);
    }

    int server_sock = criar_socket_servidor();

    sensores_processados = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE,
                                 MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (sensores_processados == MAP_FAILED) {
        perror("Erro ao criar mmap");
        exit(1);
    }

    *sensores_processados = 0;
    total_sensores = file_count;

    FILE *out = fopen("relatorio_final.txt", "w");
    if (!out) {
        perror("Erro ao criar relatorio_final.txt");
        exit(1);
    }

    for (int i = 0; i < file_count; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            process_file(input_dir, filenames[i]);
        } else if (pid > 0) {
            child_pids[num_children++] = pid;
        } else {
            perror("Erro no fork");
        }
    }

    int sensores_terminados = 0;
    while (sensores_terminados < num_children) {
        int client_sock = accept(server_sock, NULL, NULL);
        if (client_sock == -1) {
            perror("Erro no accept");
            continue;
        }

        char buffer[BUFFER_SIZE];
        ssize_t n = readn(client_sock, buffer, BUFFER_SIZE);
        if (n > 0) {
            buffer[n] = '\0';
            fprintf(out, "%s", buffer);
        }
        close(client_sock);
        (*sensores_processados)++;
        sensores_terminados++;

        mostrar_barra_progresso();
        fflush(stdout);
    }

    mostrar_barra_progresso();
    printf("\nTodos os processos filho terminaram com sucesso. Relatório gerado.\n");

    fclose(out);
    for (int i = 0; i < file_count; i++) free(filenames[i]);
    munmap(sensores_processados, sizeof(int));
    close(server_sock);
    unlink(SOCKET_PATH);
    return 0;
}
*/