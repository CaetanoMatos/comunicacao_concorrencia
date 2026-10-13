# Servidor de Tempo com Semáforo

Trabalho de Sistemas Operacionais (comunicação e concorrência): implementação de um semáforo rudimentar para controlar, de forma concorrente, o acesso a um recurso distribuído, no caso o atendimento de um servidor de tempo via sockets TCP.

## Objetivo

O código-base fornecido tem um servidor TCP que recebe a hora de um cliente e devolve a sua própria hora local. O trabalho pede que:

1. vários clientes possam tentar se conectar ao mesmo tempo;
2. o servidor lide com eles de forma concorrente;
3. **somente um cliente por vez** acesse o recurso protegido (o atendimento);
4. depois de atendido um cliente, o recurso fique **bloqueado por 2 segundos**;
5. só então o próximo cliente seja atendido, e assim sucessivamente;
6. cada cliente imprima na própria tela a hora recebida do servidor.

O ponto central é garantir **exclusão mútua** na região protegida, e não apenas colocar um atraso artificial no programa.

## O problema do código original

O servidor original é **iterativo**: ele só volta ao `accept()` depois de terminar o cliente anterior. Os clientes simultâneos ficam esperando na fila (backlog) do kernel, e não existem duas execuções disputando nada.

Por isso, colocar apenas um `sleep(2)` no laço **não demonstra um semáforo**:

- os atendimentos já são sequenciais, então não há exclusão mútua a garantir;
- nenhum mecanismo do programa (`sem_wait`/`sem_post`) bloqueia quem tenta entrar; a espera vem da fila do sistema operacional;
- se o servidor se tornasse concorrente, aquele `sleep` não protegeria nada.

Um semáforo só faz sentido quando existem **vários fluxos concorrentes disputando o mesmo recurso**. Então foi preciso primeiro criar concorrência real e depois proteger o recurso.

## Como foi resolvido

1. **Concorrência:** o servidor cria uma **thread por cliente** (`pthread_create`). A thread principal volta imediatamente ao `accept()`, então várias threads de atendimento existem ao mesmo tempo.
2. **Semáforo:** um `sem_t` global, iniciado com valor **1** (semáforo binário), compartilhado por todas as threads.
3. **Região crítica:** cada thread chama `sem_wait()` antes de usar o recurso e `sem_post()` depois.
4. **Bloqueio de 2 s:** o `sleep(2)` fica **dentro** da região crítica, antes do `sem_post()`. Assim o recurso continua tomado durante esses 2 segundos.

## Arquitetura

```
Servidor (thread principal)
   |
accept()  <-- loop; nunca fica bloqueado por causa do semáforo
   |
pthread_create() --> thread de atendimento (uma por cliente)
   |                        |
   |                  read(): hora do cliente (fora da região crítica)
   |                        |
   |                  sem_wait(&semaforo)   <-- os demais ficam bloqueados aqui
   |                        |
   |        ============ REGIÃO CRÍTICA ============
   |                  gettimeofday()  (hora do servidor)
   |                  write(): envia hora ao cliente
   |                  sleep(2)        (recurso mantido bloqueado)
   |        =======================================
   |                        |
   |                  sem_post(&semaforo)   <-- libera o próximo
   |                        |
   |                  close(socket) e a thread termina
   v
volta ao accept()
```

## Estrutura dos arquivos

```
comunicacao_concorrencia/
├── src/
│   ├── servidor_tempo.c
│   └── cliente_tempo.c
├── resultados/
│   ├── saida_servidor_t1.txt
│   ├── saida_servidor_t2.txt
│   ├── saida_servidor_t3.txt
│   ├── saida_cliente_t1.txt
│   ├── saida_clientes_t2.txt
│   └── saida_clientes_t3.txt
├── .gitignore
└── README.md
```

- `src/servidor_tempo.c`: servidor concorrente com semáforo
- `src/cliente_tempo.c`: cliente
- `resultados/`: logs reais do servidor e dos clientes nos três testes
- `bin/`: pasta onde ficam os executáveis gerados na compilação (não é versionada, está no `.gitignore`)

## Conceito de semáforo utilizado

Foi usado um semáforo POSIX não nomeado (`sem_t`) de valor inicial **1**:

- `sem_init(&semaforo, 0, 1)`: cria o semáforo. O `0` indica que é compartilhado entre **threads do mesmo processo**; o `1` significa que o recurso começa livre e cabe um atendimento por vez.
- `sem_wait()`: se o valor é 1, passa e o valor vira 0. Se é 0, a thread **bloqueia** até alguém chamar `sem_post()`. A operação é atômica, então duas threads nunca passam juntas.
- `sem_post()`: devolve o valor para 1 e acorda uma thread que estava bloqueada.

## Região crítica

É tudo o que fica entre `sem_wait()` e `sem_post()`:

- `gettimeofday()` (hora do servidor);
- `write()` (envio da hora ao cliente);
- `sleep(2)` (recurso mantido bloqueado).

A leitura da hora do cliente e o `close()` ficam **fora** de propósito, para a região crítica ser a menor possível. Essa leitura não usa o recurso protegido.

## Compilação

Na raiz do projeto:

```bash
mkdir -p bin
gcc -Wall -Wextra -pthread src/servidor_tempo.c -o bin/servidor
gcc -Wall -Wextra src/cliente_tempo.c -o bin/cliente
```

A flag `-pthread` é necessária só no servidor (usa `pthread` e `sem_t`).

## Execução

Terminal 1:
```bash
./bin/servidor
```

Terminal 2 (e outros):
```bash
./bin/cliente
```

Para parar o servidor, use Ctrl+C.

## Testando múltiplos clientes

```bash
./bin/cliente & ./bin/cliente & ./bin/cliente & ./bin/cliente & wait
```
ou, para N clientes:
```bash
for i in $(seq 1 5); do ./bin/cliente & done; wait
```

Os testes executados foram:

- **Teste 1:** um cliente (`./bin/cliente`)
- **Teste 2:** três clientes quase simultâneos (`for i in 1 2 3; do ./bin/cliente & done; wait`)
- **Teste 3:** cinco clientes quase simultâneos (`for i in $(seq 1 5); do ./bin/cliente & done; wait`)

## Saída de execuções reais

Os testes foram executados em Linux Mint (gcc 13), com servidor e clientes na mesma máquina (`127.0.0.1`). Os logs completos estão na pasta `resultados/`. PIDs e horários mudam a cada execução.

### Teste 1: um cliente

Servidor:
```
[atend #1] aguardando semaforo (sem_wait)...
[atend #1] ENTROU na regiao critica - server: local time is 1791934248 (20:30:48)
[atend #1] liberou o recurso (sem_post) as 20:30:50
```
Cliente:
```
[cliente 11389] hora recebida do servidor: 20:30:48
```

### Teste 2: três clientes quase simultâneos

Servidor (trecho):
```
[atend #1] ENTROU na regiao critica - server: local time is 1791934338 (20:32:18)
[atend #2] aguardando semaforo (sem_wait)...
[atend #3] aguardando semaforo (sem_wait)...
[atend #1] liberou o recurso (sem_post) as 20:32:20
[atend #2] ENTROU na regiao critica - server: local time is 1791934340 (20:32:20)
[atend #2] liberou o recurso (sem_post) as 20:32:22
[atend #3] ENTROU na regiao critica - server: local time is 1791934342 (20:32:22)
[atend #3] liberou o recurso (sem_post) as 20:32:24
```
Clientes:
```
[cliente 11721] hora recebida do servidor: 20:32:18
[cliente 11723] hora recebida do servidor: 20:32:20
[cliente 11722] hora recebida do servidor: 20:32:22
```

### Teste 3: cinco clientes quase simultâneos

| Atendimento | Entrou na região crítica | Liberou (`sem_post`) | Hora recebida pelo cliente |
|---|---|---|---|
| #1 | 20:34:11 | 20:34:13 | 20:34:11 |
| #2 | 20:34:13 | 20:34:15 | 20:34:13 |
| #3 | 20:34:15 | 20:34:17 | 20:34:15 |
| #4 | 20:34:17 | 20:34:19 | 20:34:17 |
| #5 | 20:34:19 | 20:34:21 | 20:34:19 |

Cada atendimento só entrou depois que o anterior liberou o recurso. Do início do primeiro atendimento (20:34:11) ao fim do último (20:34:21) passaram 10 s. Esse tempo foi calculado pelos horários do log; **não** foi medido com o comando `time`.

## O que os resultados mostram

- **Exclusão mútua:** nos três testes nunca houve duas mensagens "ENTROU na regiao critica" seguidas sem um "liberou o recurso" entre elas.
- **Intervalo de 2 s:** as horas entregues aos clientes ficam espaçadas de 2 s (teste 2: 18, 20, 22; teste 3: 11, 13, 15, 17, 19).
- **Concorrência real:** no teste 3, o atendimento #2 já estava em "aguardando semaforo" antes de o #1 entrar na região crítica, e os atendimentos #3, #4 e #5 foram aceitos enquanto o #1 ainda a ocupava. Ou seja, várias threads existiam ao mesmo tempo e ficaram bloqueadas no `sem_wait()`.
- **Tempo total:** com N clientes simultâneos, o tempo total fica em torno de 2 × N segundos (6 s com 3 clientes, 10 s com 5).

## Alterações realizadas em relação ao código original

**Servidor**
- Agora cria uma **thread por cliente** (`pthread_create`). Antes atendia um por vez, sem concorrência.
- Foi adicionado o **semáforo** (`sem_init`, `sem_wait`, `sem_post`, `sem_destroy`).
- Foi removido o laço de espera ocupada (`for j...`). O bloqueio de 2 s agora é um `sleep(2)` **dentro** da região crítica.
- `accept(..., (int *)0)` virou `accept(..., NULL, NULL)`. O tipo antigo estava errado, pois o terceiro parâmetro é `socklen_t *`.
- `read` e `write` agora têm o retorno verificado.
- `gettimeofday(&tp, NULL)`: `struct timezone` é obsoleta.
- Adicionado `SO_REUSEADDR`, para reiniciar o servidor sem erro de "address already in use".
- Adicionado `signal(SIGPIPE, SIG_IGN)`, para o servidor não morrer se um cliente desconectar antes do `write`.
- Os sockets agora são fechados nos caminhos de erro, e `exit(0)` virou `exit(EXIT_FAILURE)` nos erros.
- Backlog do `listen` aumentado de 5 para 10.
- Adicionado log com número do atendimento e horário em `HH:MM:SS`.

**Cliente**
- Verificação do retorno de `write` e `read`, com `close` antes de sair em caso de erro.
- `gettimeofday(&tp, NULL)`.
- Impressão da hora também em `HH:MM:SS` e PID nos logs, para distinguir clientes simultâneos.
- Porta 6090, endereço 127.0.0.1, TCP e envio da `struct timeval` foram mantidos.

**Organização do projeto**
- Os códigos foram movidos para `src/`, os logs dos testes para `resultados/` e os executáveis são gerados em `bin/` (ignorada pelo Git).

## Decisões de implementação

- **Threads em vez de `fork()`:** com threads o semáforo é só uma variável global. Com `fork()` seria preciso memória compartilhada (`mmap`/`shm`), `pshared = 1` e tratamento de processos zumbis (`SIGCHLD`/`waitpid`).
- **Semáforo binário (valor 1):** o enunciado pede um cliente por vez no recurso.
- **Threads `detached`:** não precisam de `pthread_join` e liberam seus recursos sozinhas ao terminar.
- **Leitura da hora do cliente fora da região crítica:** ela não usa o recurso protegido.
- **`sleep(2)` dentro da região crítica:** o recurso só é liberado depois dele. O `sleep` apenas define por quanto tempo o recurso fica ocupado; quem garante a exclusão mútua é o par `sem_wait`/`sem_post`.

## Pontos para destacar

- **Interpretação do enunciado:** o "recurso protegido" é o ato de consultar a hora do servidor e responder ao cliente. Os 2 segundos contam **a partir do envio da resposta**, com o semáforo ainda tomado.
- **Quem espera:** o cliente atendido recebe a hora **imediatamente** e termina logo. Os 2 s de bloqueio acontecem no servidor, e quem sente a espera é o **próximo** cliente, que fica parado no `sem_wait()` (e, portanto, esperando o `read` no lado dele).
- **Ordem de atendimento:** o POSIX não garante que as threads bloqueadas em `sem_wait()` sejam liberadas em ordem de chegada. No teste 2, por exemplo, o cliente de PID 11723 foi atendido antes do 11722. Os intervalos de 2 s são sempre respeitados, mas a ordem pode variar.
- **Numeração dos logs:** o número do atendimento (`#N`) é a ordem em que o `accept()` aceitou a conexão, e não o PID do cliente.
- **Resolução dos horários:** os horários usam segundos (`tv_sec`), então só dá para observar o intervalo com precisão de 1 s.

## Limitações

- Os clientes bloqueados no `sem_wait()` não são necessariamente liberados em ordem de chegada (sem garantia de FIFO).
- Não há limite de threads: uma enxurrada de conexões criaria muitas threads.
- O servidor para após 50000 conexões (herdado do código original), e a limpeza final não espera as threads terminarem.
- A `struct timeval` é enviada inteira, como no código original. Em Linux 64 bits ela tem 16 bytes e `htonl`/`ntohl` tratam só 32 bits de cada campo. Funciona porque cliente e servidor rodam na mesma máquina e arquitetura, mas não é um protocolo portável. Além disso, por usar 32 bits, o `tv_sec` transmitido só seria correto até janeiro de 2038.
- O `read` assume que os 16 bytes chegam de uma vez (aceitável no loopback). Um programa de produção leria em laço.
- Os testes foram feitos apenas com 1, 3 e 5 clientes, na mesma máquina (loopback). Não foram feitos testes de carga nem com clientes em outras máquinas.
- O tempo total dos testes 2 e 3 foi calculado pelos horários dos logs, e não medido com `time`.

## Observações

- Desenvolvido e testado em Linux Mint, com gcc 13, a partir do terminal do VS Code.
- Os dois programas são compilados com `-Wall -Wextra` (ver seção Compilação).