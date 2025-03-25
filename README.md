# CyberFlux - Simulador de Cyber Café Multithread

Este repositório contém o código-fonte do projeto desenvolvido para a disciplina de Sistemas Operacionais. O projeto, intitulado **CyberFlux**, simula o funcionamento de um cyber café futurista, onde diferentes tipos de clientes competem por recursos limitados usando threads e semáforos.

## Descrição do Trabalho

O **CyberFlux** simula um cyber café com recursos limitados, que são:
- **10 PCs**
- **6 Headsets VR**
- **8 Cadeiras ergonômicas (GC - Gaming Chairs)**

Os clientes são representados por threads e classificados em três tipos:

- **GAMER**: Precisa obrigatoriamente de PC, headset VR e cadeira.
- **FREELANCER**: Precisa obrigatoriamente de PC, cadeira e headset VR.
- **STUDENT**: Precisa apenas de um PC.

A simulação funciona da seguinte forma:
- O café permanece aberto por um período definido (`openHours`), com novos clientes sendo gerados ao longo deste período.
- O número total de clientes gerados é definido aleatoriamente, dentro do intervalo configurado (`minClients` a `maxClients`).
- Cada cliente tenta adquirir os recursos necessários simultaneamente (estratégia "All or Nothing"), garantindo que não haja aquisições parciais que poderiam levar ao deadlock.
- Caso o cliente demore muito para conseguir um PC, ele desiste (simulando impaciência ou "starvation").
- Ao fim da simulação, o programa exibe estatísticas detalhadas, incluindo:
  - Número total de clientes atendidos
  - Tempo médio de espera por recursos
  - Número de clientes que desistiram (starvation)
  - Número de vezes que cada recurso foi utilizado

## Requisitos

- Sistema Linux ou Windows com WSL/MinGW (para compilação com GCC e pthreads)
- GCC com suporte a pthread (ex.: `gcc` no Linux ou via MSYS2/WSL no Windows)

## Compilação

Para compilar o projeto, execute no terminal:

```bash
gcc cyberflux.c -o cyberflux -lpthread
```

## Execução

Após compilar, rode o programa com os seguintes parâmetros:

```bash
./cyberflux [--clients-min N] [--clients-max N] [--open-hours H] [--force-deadlock 0|1] [--verbose N]
```

### Parâmetros disponíveis:
- `--clients-min N`: Define o número mínimo de clientes a serem gerados (default: 20).
- `--clients-max N`: Define o número máximo de clientes a serem gerados (default: 50).
- `--open-hours H`: Define a duração simulada do cyber café em horas (cada "hora" simulada é aproximadamente 3 segundos reais; default: 8).
- `--force-deadlock 0|1`: Configura o modo de alocação dos recursos. Com valor `0`, evita deadlocks usando a estratégia "All or Nothing"; com valor `1`, gera propositalmente um cenário com maior chance de deadlock (default: 0).
- `--verbose N`: Controla a exibição de mensagens detalhadas (0 = mínimo, 1 = detalhado; default: 0).
- `-h, --help`: Exibe a mensagem de ajuda.

### Exemplo de execução:

```bash
./cyberflux --clients-min 30 --clients-max 60 --open-hours 4 --force-deadlock 1 --verbose 1
```

Neste exemplo, serão criados entre 30 e 60 clientes durante 4 horas simuladas, com mensagens detalhadas ativadas e o modo de alocação com potencial de deadlock.

## Funcionamento

Durante a simulação, clientes chegam ao cyber café ao longo do dia, sendo criados aleatoriamente até atingir o total configurado. Cada cliente tenta adquirir simultaneamente os recursos necessários. Caso não consiga obter o PC dentro de um tempo limite, ele desiste (starvation).

No fim da simulação, o programa aguarda todas as threads finalizarem e exibe as estatísticas sobre os recursos e o atendimento.

## Melhorias Futuras

- [ ] Implementar mecanismos avançados de escalonamento para maior eficiência.
- [ ] Adicionar novos recursos para aumentar a complexidade da simulação (impressoras, salas de reunião etc.).

## Considerações Finais

Este trabalho permitiu aplicar de forma prática conceitos importantes de Sistemas Operacionais, como threads, semáforos, concorrência, e sincronização, proporcionando uma melhor compreensão dos desafios relacionados ao gerenciamento eficiente de recursos concorrentes.
