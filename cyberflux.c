/******************************************************************************
 * cyberflux.c
 *
 * Simulador do CyberFlux, um cyber café futurista.
 *
 * Recursos fixos disponíveis:
 *   - 10 PCs
 *   - 6 Headsets VR
 *   - 8 Cadeiras
 *
 * Clientes chegam ao longo do tempo (openHours, comprimidas) e há um número
 * aleatório de clientes entre [minClients, maxClients].
 *
 * Cada cliente:
 *   - Pode desistir se demorar demais para conseguir o primeiro recurso (PC).
 *   - Tem tipo: GAMER, FREELANCER ou STUDENT. Precisam de diferentes recursos.
 *
 * Modo padrão (forceDeadlock=0) => Técnica "All or Nothing", evitando deadlock
 * sem impor ordem linear. Se não conseguir todos os recursos de uma só vez,
 * libera e tenta de novo, evitando que a thread segure recurso parcial.
 *
 * Modo forçado (forceDeadlock=1) => Alocação parcial, ordens possivelmente
 * conflitantes para criar um cenário de potencial deadlock.
 *
 * Compilar: gcc cyberflux.c -o cyberflux -lpthread
 *
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <unistd.h>
#include <string.h>


// Quantidade de cada recurso
#define NUM_PC      10
#define NUM_VR       6
#define NUM_GC       8
// Ao longo do código me refiro a "cadeira" como "GC" (Gaming Chair) daí fica bem mais bonito todos com 2 letras

// Tempo maximo (ms) que um cliente espera pelo primeiro recurso (PC) antes de desistir
#define MAX_WAIT_BEFORE_GIVEUP 1500

// Estrutura dos parâmetros
typedef struct {
    int minClients;
    int maxClients;
    int openHours;
    int forceDeadlock;  // 0 ou 1
    int verbosity;      // 0 ou 1
} SimulationParameters;

// Tipos de Clientes
typedef enum {
    GAMER,
    FREELANCER,
    STUDENT
} ClientType;

// Estrutura do cliente
typedef struct {
    int id;
    ClientType type;
} Client;

// Semáforos globais
sem_t semPC;
sem_t semVR;
sem_t semGC;

// Proteção de estatísticas
pthread_mutex_t mutexStats = PTHREAD_MUTEX_INITIALIZER;

// Estatísticas
long long totalWaitingTime = 0;
int totalServedClients = 0;
int starvedClients = 0;
int pcUses = 0;
int vrUses = 0;
int gcUses = 0;

// Parâmetros globais
SimulationParameters gParams = {20,50,8,0,0};

/* Retorna tempo atual em milissegundos */
long long currentTimeMillis() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/*
 * Tenta pegar (com timeout) o PC como primeiro recurso.
 * Retorna 1 se conseguiu, 0 se estourou o tempo.
 */
int tryAcquirePC() {
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

    long long limitMs = (now.tv_sec * 1000LL + now.tv_nsec/1000000) + MAX_WAIT_BEFORE_GIVEUP;

    struct timespec tsLimit;
    tsLimit.tv_sec = limitMs / 1000;
    tsLimit.tv_nsec = (limitMs % 1000) * 1000000;

    if (sem_timedwait(&semPC, &tsLimit) == -1) {
        return 0; // não conseguiu em tempo
    }
    pthread_mutex_lock(&mutexStats);
    pcUses++;
    pthread_mutex_unlock(&mutexStats);

    return 1;
}

/* ALOCAÇÃO MODO EVITAR DEADLOCK (forceDeadlock=0)

    UTILIZAMOS A TÉCNICA *ALL OR NOTHING*

   Explicação:
   - Se o cliente precisar de mais de um recurso (ex.: PC + VR + GC),
     tentamos "travar" todos eles de modo atômico, usando sem_trywait.
   - Se falhar em algum, liberamos o que já pegamos e voltamos ao início.
   - Enquanto isso, se demorar muito para pegar o PC, desistimos.
*/
void allocateResourcesNoDeadlock(Client* c) {
    long long startMs = currentTimeMillis();

    // Primeiro, precisamos do PC (sempre). Se não pegar em tempo, desiste.
    // MAS no "all or nothing" a gente precisa travar PC, VR e GC juntos...
    // Entretanto, a prioridade de "tempos" se refere apenas ao PC. Se ele
    // não conseguir o PC "logo", desiste. Então implementamos assim:
    //  1) Tentar PC com timeout
    //  2) Em loop, tentamos VR e GC com trywait, se falhar, liberamos e repetimos.

    // Se for STUDENT, só precisa de PC.
    // Se for GAMER ou FREELANCER, precisa PC+VR+GC.

    // 1) Tenta pegar PC com timeout
    if (!tryAcquirePC()) {
        pthread_mutex_lock(&mutexStats);
        starvedClients++;
        pthread_mutex_unlock(&mutexStats);
        if (gParams.verbosity) {
            printf("Cliente %d desistiu (deu timeout p/ o PC)\n", c->id);
        }
        return;
    }

    // Se chegou aqui, PC está garantido (mas só PC).
    // Se for ESTUDANTE, só fica com o PC e pronto.
    if (c->type == STUDENT) {
        // Usa (sleep) e libera PC
        long long waitMs = currentTimeMillis() - startMs;
        if (gParams.verbosity) {
            printf("Um estudante (ID: %d) conseguiu um PC!)\n", c->id);
        }
        sleep((rand()%5)+1);
        sem_post(&semPC);

        pthread_mutex_lock(&mutexStats);
        totalServedClients++;
        totalWaitingTime += waitMs;
        pthread_mutex_unlock(&mutexStats);

        return;
    }

    // Caso GAMER ou FREELANCER, vamos precisar VR e GC.
    // "All or nothing": tentamos travar VR e GC usando sem_trywait num loop.
    // Se falhar, soltamos tudo e tentamos de novo, mas ver se não estouramos o tempo para o PC.

    int gotAll = 0;
    while (!gotAll) {
        // Tenta VR
        int rVR = sem_trywait(&semVR);
        // Tenta GC
        int rChair = sem_trywait(&semGC);

        if (rVR == 0 && rChair == 0) {
            // Conseguiu VR e GC
            pthread_mutex_lock(&mutexStats);
            vrUses++;
            gcUses++;
            pthread_mutex_unlock(&mutexStats);

            gotAll = 1;
        } else {
            // Falhou em algum => libera o que conseguiu
            if (rVR == 0) {
                sem_post(&semVR);
            }
            if (rChair == 0) {
                sem_post(&semGC);
            }

            // Espera um pouco e tenta de novo, MAS verifica se não passou do timeout para o PC.
            long long elapsed = currentTimeMillis() - startMs;
            if (elapsed > MAX_WAIT_BEFORE_GIVEUP) {
                // Desiste
                // Libera PC também
                sem_post(&semPC);

                pthread_mutex_lock(&mutexStats);
                starvedClients++;
                pthread_mutex_unlock(&mutexStats);

                if (gParams.verbosity) {
                    printf("Cliente %d desistiu (não conseguiu VR+GC no tempo)\n", c->id);
                }
                return;
            }

            usleep(50 * 1000); // 0.05s
        }
    }

    // Chegou aqui => pegamos PC, VR, GC sem ficar com travamento parcial
    long long waitMs = currentTimeMillis() - startMs;
    if (gParams.verbosity) {
        printf("Um %d (", c->id);
        if (c->type == GAMER) printf("gamer");
        else printf("freelancer");
        printf(") obteve PC+VR+GC (ALL-OR-NOTHING). Esperou %lld ms\n", waitMs);
    }

    // Simula o uso do recurso por um tempo aleatório
    sleep((rand()%5)+1);

    // Libera os recursos
    sem_post(&semGC);
    sem_post(&semVR);
    sem_post(&semPC);

    pthread_mutex_lock(&mutexStats);
    totalServedClients++;
    totalWaitingTime += waitMs;
    pthread_mutex_unlock(&mutexStats);
}

/* ALOCAÇÃO MODO FORÇAR DEADLOCK (forceDeadlock=1)

   Aqui fazemos alocação parcial, cada tipo em ordem diferente.
   Isso pode gerar espera circular.
*/
void allocateResourcesDeadlock(Client* c) {
    long long startMs = currentTimeMillis();

    // Precisamos sempre de PC, mas Gamer e Freelancer também querem VR e GC.
    // E para “forçar” o conflito, definimos ordens distintas para cada tipo:

    if (c->type == STUDENT) {
        // Tenta PC com timeout
        if (!tryAcquirePC()) {
            pthread_mutex_lock(&mutexStats);
            starvedClients++;
            pthread_mutex_unlock(&mutexStats);
            if (gParams.verbosity) {
                printf("ESTUDANTE %d desistiu no PC\n", c->id);
            }
            return;
        }
        // Usa e libera
        long long waitMs = currentTimeMillis() - startMs;
        if (gParams.verbosity) {
            printf("ESTUDANTE %d [FORCE=1] pegou PC e usa (esperou %lld ms)\n", c->id, waitMs);
        }
        sleep((rand()%5)+1);
        sem_post(&semPC);

        pthread_mutex_lock(&mutexStats);
        totalServedClients++;
        totalWaitingTime += waitMs;
        pthread_mutex_unlock(&mutexStats);

    } else if (c->type == GAMER) {
        // Modo conflituoso: GC -> PC -> VR
        // 1) GC (bloqueante)
        sem_wait(&semGC);
        pthread_mutex_lock(&mutexStats);
        gcUses++;
        pthread_mutex_unlock(&mutexStats);

        // 2) PC (com timeout)
        if (!tryAcquirePC()) {
            // libera gc
            sem_post(&semGC);
            pthread_mutex_lock(&mutexStats);
            starvedClients++;
            pthread_mutex_unlock(&mutexStats);
            if (gParams.verbosity) {
                printf("GAMER %d desistiu no PC [FORCE=1]\n", c->id);
            }
            return;
        }

        // 3) VR (bloqueante)
        sem_wait(&semVR);
        pthread_mutex_lock(&mutexStats);
        vrUses++;
        pthread_mutex_unlock(&mutexStats);

        long long waitMs = currentTimeMillis() - startMs;
        if (gParams.verbosity) {
            printf("GAMER %d [FORCE=1] pegou GC->PC->VR (esperou %lld ms)\n", c->id, waitMs);
        }

        // Usa
        sleep((rand()%5)+1);

        // Libera na ordem inversa
        sem_post(&semVR);
        sem_post(&semPC);
        sem_post(&semGC);

        pthread_mutex_lock(&mutexStats);
        totalServedClients++;
        totalWaitingTime += waitMs;
        pthread_mutex_unlock(&mutexStats);

    } else {
        // FREELANCER: VR -> GC -> PC
        // 1) VR (bloqueante)
        sem_wait(&semVR);
        pthread_mutex_lock(&mutexStats);
        vrUses++;
        pthread_mutex_unlock(&mutexStats);

        // 2) GC (bloqueante)
        sem_wait(&semGC);
        pthread_mutex_lock(&mutexStats);
        gcUses++;
        pthread_mutex_unlock(&mutexStats);

        // 3) PC (timeout)
        if (!tryAcquirePC()) {
            // libera VR e GC
            sem_post(&semGC);
            sem_post(&semVR);
            pthread_mutex_lock(&mutexStats);
            starvedClients++;
            pthread_mutex_unlock(&mutexStats);

            if (gParams.verbosity) {
                printf("FREELANCER %d desistiu no PC [FORCE=1]\n", c->id);
            }
            return;
        }

        long long waitMs = currentTimeMillis() - startMs;
        if (gParams.verbosity) {
            printf("FREELANCER %d [FORCE=1] pegou VR->GC->PC (esperou %lld ms)\n",
                   c->id, waitMs);
        }

        sleep((rand()%5)+1);

        sem_post(&semPC);
        sem_post(&semGC);
        sem_post(&semVR);

        pthread_mutex_lock(&mutexStats);
        totalServedClients++;
        totalWaitingTime += waitMs;
        pthread_mutex_unlock(&mutexStats);
    }
}

/*
 * Thread principal de cada cliente
 */
void* clientRoutine(void* arg) {
    Client* c = arg;

    if (gParams.forceDeadlock == 0) {
        // Modo que evita deadlock: all or nothing
        allocateResourcesNoDeadlock(c);
    } else {
        // Modo que pode gerar deadlock
        allocateResourcesDeadlock(c);
    }

    free(c);
    return NULL;
}

/*
 * Exibe ajuda prompt de ajuda com comandos
 *
 */
void showHelp() {
    printf("Uso: ./cyberflux [opcoes]\n");
    printf("  --clients-min N\n");
    printf("  --clients-max N\n");
    printf("  --open-hours N\n");
    printf("  --force-deadlock 0|1\n");
    printf("  --verbose 0|1\n");
    printf("  -h, --help\n");
}

/*
 * Lê parâmetros de linha de comando
 */
void parseArgs(int argc, char** argv) {
    for (int i=1; i<argc; i++) {
        if(!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")){
            showHelp();
            exit(0);
        } if(!strcmp(argv[i], "--clients-min") && i+1<argc){
            gParams.minClients = atoi(argv[++i]);
        } else if(!strcmp(argv[i], "--clients-max") && i+1<argc){
            gParams.maxClients = atoi(argv[++i]);
        } else if(!strcmp(argv[i], "--open-hours") && i+1<argc){
            gParams.openHours = atoi(argv[++i]);
        } else if(!strcmp(argv[i], "--force-deadlock") && i+1<argc){
            gParams.forceDeadlock = atoi(argv[++i]);
        } else if(!strcmp(argv[i], "--verbose") && i+1<argc){
            gParams.verbosity = atoi(argv[++i]);
        } else {
            fprintf(stderr, "Parametro desconhecido: %s\n", argv[i]);
        }
    }
}

int main(int argc, char** argv) {
    srand(time(NULL));
    parseArgs(argc, argv);

    // Número total de clientes a criar
    int totalClientsToCreate = 0;
    if (gParams.maxClients >= gParams.minClients) {
        totalClientsToCreate =
            rand() % (gParams.maxClients - gParams.minClients + 1)
            + gParams.minClients;
    } else {
        totalClientsToCreate = gParams.minClients;
    }

    printf("=== CYBERFLUX SIM ===\n");
    printf("Modo forceDeadlock=%d (0=evita, 1=forca deadlock)\n", gParams.forceDeadlock);

    // Inicializa semáforos
    sem_init(&semPC, 0, NUM_PC);
    sem_init(&semVR, 0, NUM_VR);
    sem_init(&semGC, 0, NUM_GC);

    // Cria threads
    pthread_t* threads = malloc(sizeof(pthread_t) * totalClientsToCreate);

    // Calcula duração total (openHours * 3s)
    int totalSimSecs = gParams.openHours * 3;
    if (totalSimSecs < 1) totalSimSecs = 1;

    long long startMs = currentTimeMillis();
    int createdCount = 0;

    while (1) {
        long long nowMs = currentTimeMillis();
        long long elapsed = (nowMs - startMs) / 1000;
        if (elapsed >= totalSimSecs) break;

        // cria de 0..2 clientes a cada iteração
        int groupSize = rand() % 3;
        for (int i=0; i<groupSize; i++) {
            if (createdCount >= totalClientsToCreate) break;

            Client* c = malloc(sizeof(Client));
            c->id = createdCount+1;
            c->type = rand() % 3; // 0=GAMER,1=FREELANCER,2=STUDENT

            pthread_create(&threads[createdCount], NULL, clientRoutine, c);
            createdCount++;
        }

        usleep(200000); // 0.2s
        if (createdCount >= totalClientsToCreate) break;
    }

    // Espera todas as threads
    for (int i=0; i<createdCount; i++) {
        pthread_join(threads[i], NULL);
    }

    // Estatísticas
    double avgWait = 0.0;
    if (totalServedClients > 0) {
        avgWait = (double) totalWaitingTime / totalServedClients;
    }

    printf("\n--- ESTATISTICAS ---\n");
    printf("Clientes que visitaram o café: %d\n", createdCount);
    printf("Clientes que conseguiram recursos: %d\n", totalServedClients);
    printf("Clientes que não conseguiram recursos: %d\n", starvedClients);
    printf("Tempo médio de espera (ms): %.2f\n", avgWait);
    printf("Usos PC: %d\n", pcUses);
    printf("Usos VR: %d\n", vrUses);
    printf("Usos GC: %d\n", gcUses);

    // Libera recursos
    sem_destroy(&semPC);
    sem_destroy(&semVR);
    sem_destroy(&semGC);
    pthread_mutex_destroy(&mutexStats);
    free(threads);

    printf("Fim da simulacao.\n");
    return 0;
}
