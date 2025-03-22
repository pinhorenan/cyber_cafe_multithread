# CyberFlux - Simulador de Cyber Café Multithread

Este repositório contém o código-fonte do trabalho da graduação para a disciplina de Sistemas Operacionais. O projeto, intitulado **CyberFlux**, simula o funcionamento de um cyber café futurista, onde clientes de diferentes tipos competem por recursos limitados.

## Descrição do Trabalho

O **CyberFlux** simula um cyber café com os seguintes recursos fixos:
- **10 PCs**
- **6 Headsets VR**
- **8 Cadeiras**

Os clientes são representados por threads e classificados em três tipos:

- **Gamer**: Necessita obrigatoriamente de um PC e de um headset VR; tenta adquirir uma cadeira de forma opcional.
- **Freelancer**: Necessita obrigatoriamente de um PC e de uma cadeira; tenta adquirir um headset VR de forma opcional.
- **Student**: Necessita apenas de um PC.

A simulação se comporta da seguinte forma:
- O café permanece "aberto" por um período determinado (`openHours`), em que novos clientes são gerados.
- O número total de clientes é determinado aleatoriamente em um intervalo configurável (`minClients` a `maxClients`).
- Cada cliente tenta adquirir os recursos necessários seguindo uma ordem fixa (PC → cadeira → VR), para evitar deadlock.
- Caso um cliente demore demais para obter o recurso principal (PC), ele desiste (simulando "falta de paciência").
- Ao final da simulação, o programa exibe estatísticas como:
    - Total de clientes atendidos
    - Tempo médio de espera
    - Número de clientes que desistiram
    - Uso total de cada recurso

## Requisitos

- Sistema Linux ou Windows com WSL/MinGW (para compilação com GCC e pthreads)
- GCC com suporte a pthread (ex.: `gcc` em Linux ou MSYS2/WSL no Windows)

## Compilação

No terminal, execute:

```bash
gcc cyberflux.c -o cyberflux -lpthread
```

## Execução

Após a compilação, execute o programa utilizando os parâmetros de linha de comando:

```bash
./cyberflux [--clients-min N] [--clients-max N] [--open-hours H] [--force-deadlock 0|1] [--verbose N]
```

### Parâmetros:
- `--clients-min N`  
  Define o número mínimo de clientes a serem gerados (default: 20).

- `--clients-max N`  
  Define o número máximo de clientes a serem gerados (default: 50).

- `--open-hours H`  
  Define as horas simuladas de funcionamento do café (default: 8).  
  *Observação:* Cada "hora" simulada é comprimida em 3 segundos.

- `--force-deadlock 0|1`  **(WIP, AINDA NÃO TÁ PRONTO)**
  Ativa (1) ou desativa (0) a tentativa de forçar um cenário de quase-deadlock (default: 0).

- `--verbose N`  
  Define o nível de verbosidade (0 = mínimo, 1 = detalhado; default: 0).

- `-h, --help`  
  Exibe esta mensagem de ajuda.

### Exemplo de execução:
```bash
./cyberflux --clients-min 30 --clients-max 60 --open-hours 4 --force-deadlock 1 --verbose 1
```

## Funcionamento

Durante a simulação, enquanto o café estiver aberto (definido por `openHours`), clientes são gerados aleatoriamente, com o número total de clientes sendo definido de forma aleatória entre `minClients` e `maxClients`. Cada cliente tenta adquirir os recursos conforme seu tipo. Se um cliente não conseguir adquirir o recurso principal (PC) em um tempo máximo (configurado internamente), ele desiste e é contabilizado como "starved" (desistiu).

Ao final do período de funcionamento, o programa espera que todos os clientes criados terminem e exibe as estatísticas finais da simulação.

## TODO

- [ ] Implementar de maneira completa uma situação que force deadlock com o intuito de testar a solução de deadlock.

## Considerações Finais

Este trabalho foi desenvolvido como parte das atividades da disciplina de Sistemas Operacionais, visando aplicar conceitos de threads, sincronização e gerenciamento de recursos. O projeto foi implementado para permitir experimentos com diferentes parâmetros, possibilitando a análise de desempenho e comportamento em cenários variados.