# Projeto SchoolAir — Monitorização de Qualidade do Ar

**Unidade Curricular: Sistemas Operativos**  
**Ano letivo:** 2024/2025  
**Autor:** Tiago Rêga (43019@ufp.edu.pt)

## Descrição

O projeto **SchoolAir** simula a monitorização da qualidade do ar em salas de aula através da leitura e processamento de dados de sensores (Temperatura, Humidade, PM2.5, PM10, CO2), armazenados em ficheiros `.csv`. O sistema utiliza **programação concorrente em C (POSIX)** para garantir desempenho e sincronização entre múltiplas threads.

---

## Requisitos Implementados

###  Fase 1 — Multiprocessamento e comunicação entre processos
- Receber parâmetros de entrada e inicializar o sistema - **Implementado**
- Lançar N processos filho e processar dados - **Implementado**
- omunicação entre processos filho e processo pai usando pipes - **Implementado**
- Barra de progresso - **Implementado**
- Comunicação usando Unix Domain Sockets - **Implementado**

###  Fase 2 — Multithreading e Sincronização
- Worker threads e memória partilhada - **Implementado**
- Barra de progresso com threads - **Implementado**
- Sistema produtor-consumidor com threads - **Implementado**


