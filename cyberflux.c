/******************************************************************************
 * cyberflux.c
 *
 * Simulador do CyberFlux, um cyber cafe futurista.
 *
 * Recursos fixos disponiveis:
 *   - 10 PCs
 *   - 6 Headsets VR
 *   - 8 Cadeiras
 *
 * Clientes chegam ao longo do tempo de funcionamento do cafe (openHours),
 * e a quantidade total de clientes e aleatoria, entre [minClients, maxClients].
 *
 * Cada cliente pode desistir se demorar demais para conseguir o primeiro recurso
 * (PC), simulando situacao de "falta de paciencia".
 *
 * Tipos de clientes:
 *   GAMER: PC + VR obrigatorios, cadeira opcional
 *   FREELANCER: PC + cadeira obrigatorios, VR opcional
 *   STUDENT: Apenas PC
 *
 * A alocacao segue ordem fixa (PC -> cadeira -> VR) para evitar deadlock real,
 * mas se --force-deadlock=1 for usado, exibimos alertas quando os recursos
 * estiverem proximo de esgotar, simulando quase-deadlock.
 *
 * A verbosidade (verbosity=1) exibe logs detalhados de cada cliente.
 *
 * Compilacao:
 *   gcc cyberflux.c -o cyberflux -lpthread
 *
 * Exemplo de execucao:
 *   ./cyberflux --clients-min 20 --clients-max 60 --hours 4 --verbose 1
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>
#include <string.h>

/* Quantidade fixa de recursos */
#define NUM_PCS      10
#define NUM_VR       6
#define NUM_CHAIRS   8

/* Tempo maximo (em ms) que um cliente espera pelo primeiro recurso (PC) antes de desistir */
#define MAX_WAIT_BEFORE_GIVEUP 1500

/* -----------
   Parametros de simulacao
   minClients / maxClients: define o range de clientes total
   openHours: tempo de funcionamento do cafe em "horas" (comprimido)
   forceDeadlock: se 1, tenta forcar situacao de quase-deadlock
   verbosity: se 1, exibe logs detalhados
   ----------- */
typedef struct {
    int minClients;
    int maxClients;
    int openHours;
    int forceDeadlock;
    int verbosity;
} SimulationParameters;

/* -----------
   Tipos de clientes
   ----------- */
typedef enum {
    GAMER,       // precisa de PC + VR, cadeira opcional
    FREELANCER,  // precisa de PC + cadeira, VR opcional
    STUDENT      // apenas PC
} ClientType;

/* -----------
   Estrutura do cliente (thread)
   ----------- */
typedef struct {
    int id;
    ClientType type;
    struct timespec arrivalTime; // usado para medir tempo de espera
} Client;

/* -----------
   Semaforos para cada recurso
   ----------- */
sem_t semComputer;
sem_t semHeadset;
sem_t semChair;

/* -----------
   Variaveis de estatistica, protegidas por mutex
   ----------- */
pthread_mutex_t mutexStats = PTHREAD_MUTEX_INITIALIZER;

/* Contadores globais */
long long totalWaitingTime = 0;   // soma de espera (ms)
int totalServedClients = 0;       // quantos conseguiram recursos
int computerUses = 0;            // quantas vezes PC foi alocado
int headsetUses = 0;             // quantas vezes VR foi alocado
int chairUses = 0;               // quantas vezes cadeira foi alocada
int starvedClients = 0;          // quantos desistiram antes de conseguir o PC

/* Parametros globais da simulacao */
SimulationParameters gParams = {
    .minClients    = 20,
    .maxClients    = 120,
    .openHours     = 8,
    .forceDeadlock = 0,
    .verbosity     = 0
};

/* -----------
   Funcao auxiliar: retorna o tempo atual em milissegundos
   ----------- */
long long currentTimeMillis() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* -----------
   Exibe mensagem de ajuda
   ----------- */
void showHelp() {
    printf("Uso: cyberflux [opcoes]\n");
    printf("Opcoes:\n");
    printf("  --clients-min N        Numero minimo de clientes (default: 20)\n");
    printf("  --clients-max N        Numero maximo de clientes (default: 50)\n");
    printf("  --hours H              Horas simuladas de funcionamento (default: 8)\n");
    printf("  --force-deadlock 0|1   Forca quase-deadlock (default: 0)\n");
    printf("  --verbose N            Nivel de verbosidade (0=nenhum,1=detalhado)\n");
    printf("  -h, --help             Exibe esta mensagem de ajuda\n");
}

/* -----------
   Forca alerta de quase-deadlock se forceDeadlock=1 e os recursos estao quase esgotados
   ----------- */
void checkPotentialDeadlock(int forceDeadlock) {
    if (!forceDeadlock) return;

    int valPc, valVr, valChair;
    sem_getvalue(&semComputer, &valPc);
    sem_getvalue(&semHeadset, &valVr);
    sem_getvalue(&semChair, &valChair);

    /* Se todos os recursos estiverem proximo de zero, alertamos */
    if (valPc < 2 && valVr < 2 && valChair < 2) {
        printf("\n[!] ALERTA DE QUASE-DEADLOCK FORCADO!\n");
        printf("    Recursos quase esgotados! PC=%d, VR=%d, Cadeira=%d\n", valPc, valVr, valChair);
        printf("    Se a ordem de alocacao nao fosse PC->Cadeira->VR, poderiamos travar!\n\n");
    }
}

/* -----------
   Tenta adquirir o PC com tempo limite (MAX_WAIT_BEFORE_GIVEUP).
   Se estourar esse tempo, o cliente desiste e incrementa starvedClients.
   ----------- */
int tryAcquireComputer() {
    struct timespec tsNow;
    clock_gettime(CLOCK_REALTIME, &tsNow);

    /* Calcula prazo limite (tsNow + MAX_WAIT_BEFORE_GIVEUP) */
    long long limitMs = (long long)tsNow.tv_sec * 1000 + tsNow.tv_nsec / 1000000 + MAX_WAIT_BEFORE_GIVEUP;

    /* Converte de volta para timespec */
    struct timespec tsLimit;
    tsLimit.tv_sec = limitMs / 1000;
    tsLimit.tv_nsec = (limitMs % 1000) * 1000000;

    /* Tenta sem_timedwait */
    int rc = sem_timedwait(&semComputer, &tsLimit);
    if (rc == -1) {
        // Falha => desistencia
        return 0;
    }
    return 1;
}

/* -----------
   Rotina de cada cliente (thread)
   ----------- */
void* clientRoutine(void* arg) {
    Client* c = (Client*)arg;
    long long arrivalMs = currentTimeMillis();

    /* Verifica se forcaremos alerta de quase-deadlock */
    checkPotentialDeadlock(gParams.forceDeadlock);

    /* 1) Tenta adquirir PC com timeout */
    int gotPc = tryAcquireComputer();
    if (!gotPc) {
        /* Cliente desiste por esperar demais pelo PC */
        pthread_mutex_lock(&mutexStats);
        starvedClients++;
        pthread_mutex_unlock(&mutexStats);

        if (gParams.verbosity > 0) {
            printf("Cliente %d desistiu antes de conseguir PC.\n", c->id);
        }
        free(c);
        return NULL;
    }

    /* Se chegou aqui, pegou PC com sucesso */
    long long afterPcMs = currentTimeMillis();
    long long waitedMs = afterPcMs - arrivalMs;

    /* Incrementa estatisticas de uso de PC */
    pthread_mutex_lock(&mutexStats);
    computerUses++;
    pthread_mutex_unlock(&mutexStats);

    /* Alocacao ordenada:
       - Gamer: VR obrigatorio, cadeira opcional
       - Freelancer: cadeira obrigatoria, VR opcional
       - Student: so PC */
    int tookHeadset = 0;
    int tookChair = 0;

    if (c->type == GAMER) {
        // Pega VR obrigatoriamente
        sem_wait(&semHeadset);
        tookHeadset = 1;
        pthread_mutex_lock(&mutexStats);
        headsetUses++;
        pthread_mutex_unlock(&mutexStats);

        // Cadeira opcional
        if (sem_trywait(&semChair) == 0) {
            tookChair = 1;
            pthread_mutex_lock(&mutexStats);
            chairUses++;
            pthread_mutex_unlock(&mutexStats);
        }
    }
    else if (c->type == FREELANCER) {
        // Pega cadeira obrigatoriamente
        sem_wait(&semChair);
        tookChair = 1;
        pthread_mutex_lock(&mutexStats);
        chairUses++;
        pthread_mutex_unlock(&mutexStats);

        // VR opcional
        if (sem_trywait(&semHeadset) == 0) {
            tookHeadset = 1;
            pthread_mutex_lock(&mutexStats);
            headsetUses++;
            pthread_mutex_unlock(&mutexStats);
        }
    }
    else {
        // Student so precisa do PC
    }

    /* Tempo final apos alocar tudo */
    long long allMs = currentTimeMillis();
    long long totalWaitMs = allMs - arrivalMs;

    if (gParams.verbosity > 0) {
        printf("Cliente %d (", c->id);
        if (c->type == GAMER)       printf("Gamer");
        else if (c->type == FREELANCER)  printf("Freelancer");
        else if (c->type == STUDENT)     printf("Estudante");
        printf(") conseguiu recursos (esperou %lld ms) e esta usando...\n", totalWaitMs);
    }

    /* Simula uso (1..5s) */
    int useTime = (rand() % 5) + 1;
    sleep(useTime);

    /* Libera recursos em ordem inversa */
    if (tookHeadset) sem_post(&semHeadset);
    if (tookChair) sem_post(&semChair);
    sem_post(&semComputer);

    /* Atualiza estatisticas: tempo de espera e clientes atendidos */
    pthread_mutex_lock(&mutexStats);
    totalServedClients++;
    totalWaitingTime += totalWaitMs;
    pthread_mutex_unlock(&mutexStats);

    if (gParams.verbosity > 0) {
        printf("Cliente %d liberou recursos e saiu.\n", c->id);
    }
    free(c);
    return NULL;
}

/* -----------
   Le argumentos de linha de comando e preenche gParams
   ----------- */
void parseArgs(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-h") == 0) || (strcmp(argv[i], "--help") == 0)) {
            showHelp();
            exit(0);
        }
        else if (strcmp(argv[i], "--clients-min") == 0 && i+1 < argc) {
            gParams.minClients = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--clients-max") == 0 && i+1 < argc) {
            gParams.maxClients = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--open-hours") == 0 && i+1 < argc) {
            gParams.openHours = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--force-deadlock") == 0 && i+1 < argc) {
            gParams.forceDeadlock = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--verbose") == 0 && i+1 < argc) {
            gParams.verbosity = atoi(argv[++i]);
        }
        else {
            fprintf(stderr, "Parametro desconhecido: %s\n", argv[i]);
            fprintf(stderr, "Use -h ou --help para ajuda.\n");
        }
    }
}

/* -----------
   MAIN
   - Define quantidade total de clientes (aleatoria em [minClients, maxClients])
   - Abre o café por openHours (cada hora comprimida em 3s, ajustavel)
   - Gera clientes ao longo desse periodo
   - Cada cliente pode desistir se nao conseguir PC em MAX_WAIT_BEFORE_GIVEUP ms
   - Exibe estatisticas ao final
   ----------- */
int main(int argc, char** argv) {
    srand(time(NULL));
    parseArgs(argc, argv);

    /* Define quantidade total de clientes de forma aleatoria */
    int totalClientsToCreate = 0;
    if (gParams.maxClients >= gParams.minClients) {
        totalClientsToCreate = rand() % (gParams.maxClients - gParams.minClients + 1) + gParams.minClients;
    } else {
        /* Se por acaso min > max, inverte ou assume so minClients */
        totalClientsToCreate = gParams.minClients;
    }

    printf("=== CYBERFLUX SIMULADOR ===\n");
    printf("Parametros:\n");
    printf(" Mínimo de clientes: %d\n", gParams.minClients);
    printf(" Máximo de clientes: %d\n", gParams.maxClients);
    printf(" Tempo de funcionamento do café: %d\n", gParams.openHours);
    printf(" Verbosidade (1=detalhado, 0=QUASE NADA): %d\n", gParams.verbosity);
    printf(" Forçar Deadlock? (1=sim, 0=não): %d\n", gParams.forceDeadlock);

    /* Inicializa semaforos */
    sem_init(&semComputer, 0, NUM_PCS);
    sem_init(&semHeadset, 0, NUM_VR);
    sem_init(&semChair, 0, NUM_CHAIRS);

    /* Vetor de threads */
    pthread_t* threads = (pthread_t*)malloc(sizeof(pthread_t) * totalClientsToCreate);

    /* Calcula duracao total do cafe, em segundos (ex: 8h => 8*3=24s) */
    int totalSimSecs = gParams.openHours * 3;
    if (totalSimSecs < 1) totalSimSecs = 1;

    long long startMs = currentTimeMillis();
    int createdCount = 0;

    /* Enquanto o café estiver aberto */
    while (1) {
        long long nowMs = currentTimeMillis();
        long long elapsedSecs = (nowMs - startMs) / 1000;

        /* Se acabou o horario do cafe, paramos de criar novos clientes */
        if (elapsedSecs >= totalSimSecs) {
            break;
        }

        /* Criamos um pequeno grupo (0..2) de clientes a cada iteracao, se ainda nao atingimos o total */
        int groupSize = rand() % 3;
        for (int j = 0; j < groupSize; j++) {
            if (createdCount >= totalClientsToCreate) break;

            Client* c = (Client*)malloc(sizeof(Client));
            c->id = createdCount + 1;
            c->type = rand() % 3;  // 0=GAMER,1=FREELANCER,2=STUDENT
            clock_gettime(CLOCK_REALTIME, &c->arrivalTime);

            pthread_create(&threads[createdCount], NULL, clientRoutine, c);
            createdCount++;
        }

        usleep(200000); // 0.2s de intervalo
        if (createdCount >= totalClientsToCreate) {
            break;
        }
    }

    /* Se ainda nao criamos todos os clientes, mas o café fechou, isso significa que esse resto NUNCA entrou efetivamente no café => e nao serao criados. */
    // Assim, se openHours terminou, alguns clientes nem chegaram a ser threads.

    /* Aguarda todas as threads que foram criadas */
    for (int i = 0; i < createdCount; i++) {
        pthread_join(threads[i], NULL);
    }

    /* Estatisticas finais */
    double avgWait = 0.0;
    if (totalServedClients > 0) {
        avgWait = (double)totalWaitingTime / totalServedClients;
    }

    printf("\n--- ESTATISTICAS FINAIS ---\n");
    printf("Total de clientes efetivamente criados: %d\n", createdCount);
    printf("Total de clientes atendidos: %d\n", totalServedClients);
    printf("Clientes que desistiram antes de conseguir PC: %d\n", starvedClients);
    printf("Tempo medio de espera (ms): %.2f\n", avgWait);
    printf("Uso total de PCs: %d\n", computerUses);
    printf("Uso total de VRs: %d\n", headsetUses);
    printf("Uso total de Cadeiras: %d\n", chairUses);

    /* Libera recursos */
    sem_destroy(&semComputer);
    sem_destroy(&semHeadset);
    sem_destroy(&semChair);
    pthread_mutex_destroy(&mutexStats);
    free(threads);

    printf("Simulacao encerrada.\n");
    return 0;
}
