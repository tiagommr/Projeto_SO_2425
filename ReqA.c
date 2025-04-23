//
// Created by tiago on 19-04-2025.
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
#include <time.h>

#define MAX_FILES 100
#define TIME_FORMAT "%Y-%m-%dT%H:%M:%S" // Formato do timestamp nos ficheiros .csv

pid_t child_pids[MAX_FILES];
int num_children = 0;

// Trata da interrupcção (Ctrl c) para que todos os processos filhos terminem de forma limpa e organizada
void handle_sigint(int sig) {
    printf("\n[SINAL] SIGINT recebido. A terminar processos filho...\n");
    for (int i = 0; i < num_children; i++) {
        if (child_pids[i] > 0) {
            kill(child_pids[i], SIGTERM); // Envia sinal para terminar
            waitpid(child_pids[i], NULL, 0); // Aguarda que o processo termine
        }
    }
    printf("Todos os processos filho foram terminados.\n");
    exit(0);
}

void strptime(const char * timestamp, char * str, struct tm * tm);

// Converte string timestamp para time_t
time_t parse_timestamp(const char *timestamp) {
    struct tm tm;
    memset(&tm, 0, sizeof(struct tm));
    strptime(timestamp, TIME_FORMAT, &tm);
    return mktime(&tm);
}

// Verifica se o valor está fora do intervalo aceitável
int is_out_of_bounds(const char *filename, double value) {
    if (strstr(filename, "temperature"))
        return value < 18 || value > 27;
    if (strstr(filename, "humidity"))
        return value < 30 || value > 70;
    if (strstr(filename, "pm25"))
        return value > 25;
    if (strstr(filename, "pm10"))
        return value > 50;
    if (strstr(filename, "co2"))
        return value > 1000;
    return 0;
}

// Cada processo filho chama esta função para processar 1 ficheiro do sensor
void process_file(const char *input_dir, const char *filename) {
    char fullpath[256];
    snprintf(fullpath, sizeof(fullpath), "%s/%s", input_dir, filename);

    // Abrir ficheiro .csv
    int fd = open(fullpath, O_RDONLY);
    if (fd == -1) {
        perror("Erro ao abrir ficheiro");
        exit(1);
    }

    // Obter tamanho do ficheiro
    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        perror("Erro ao obter estatísticas");
        close(fd);
        exit(1);
    }

    // Mapear ficheiro para leitura
    char *data = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED) {
        perror("Erro ao mapear o ficheiro");
        close(fd);
        exit(1);
    }

    close(fd); // Fechar o descritor de ficheiro referente à abertura de ficheiros .csv

    // Variáveis para o cálculo da média e do tempo fora do limite
    double soma = 0.0;
    int count = 0;
    double fora_limite = 0.0;
    char sensor[100], timestamp[64];
    double valor;
    time_t tempo_anterior = 0, tempo_atual;
    int em_fora_limite = 0;

    char *start = data;
    char *end = data;

    // Ignorar a primeira linha do ficheiro .csv
    while (*end != '\n' && end < data + sb.st_size) end++;
    start = end + 1;
    end = start;

    // Ler o ficheiro linha a linha
    while (end < data + sb.st_size) {
        if (*end == '\n' || end == data + sb.st_size - 1) {
            size_t len = end - start + 1;
            char line[len + 1];
            memcpy(line, start, len);
            line[len] = '\0';

            // Extrair os dados da linha (sensor, valor, timestamp)
            if (sscanf(line, "%99[^,],%lf,%63s", sensor, &valor, timestamp) == 3) {
                soma += valor;
                count++;

                tempo_atual = parse_timestamp(timestamp);

                if (is_out_of_bounds(filename, valor)) {
                    if (em_fora_limite) {
                        // Somar o tempo fora do limite
                        fora_limite += difftime(tempo_atual, tempo_anterior) / 3600.0;
                    } else {
                        em_fora_limite = 1;
                    }
                } else {
                    em_fora_limite = 0;
                }

                tempo_anterior = tempo_atual;
            }

            start = end + 1;
        }
        end++;
    }

    munmap(data, sb.st_size); // libertar a memoria mapeada

    double media = count > 0 ? soma / count : 0.0;

    // Criar o ficheiro de saída para cada processo filho e escrever os resultados
    char output[64];
    snprintf(output, sizeof(output), "resultado_%d.txt", getpid());
    FILE *out = fopen(output, "w");
    if (!out) {
        perror("Erro ao criar ficheiro de saída");
        exit(1);
    }

    // Formato: pid;nome_ficheiro;media;tempo_fora_limite
    fprintf(out, "%d;%s;%.2f;%.2f\n", getpid(), filename, media, fora_limite);
    fclose(out);
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
        printf(" -> %s\n", filenames[i]);
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

    for (int i = 0; i < num_children; i++) {
        waitpid(child_pids[i], NULL, 0);
    }

    for (int i = 0; i < file_count; i++) {
        free(filenames[i]);
    }

    printf("Todos os processos filho terminaram com sucesso.\n");
    return 0;
}
