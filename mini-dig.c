// gcc server.c -o server -pthread
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdint.h>

enum query_type {
  A,
  AAAA
};

// То, что ввел user
struct config {
    const char *server; //const, чтобы нельзя было изменить символы строк
    const char *domain;
    enum query_type type;
};

// Выводит mini guide на mini pig
void print_guide(){
      printf("Запуск утилиты выглядит так:\n");
      printf("mini-dig <server> <domain> <type>.\n");
      printf("<server> - IPv4 адрес DNS-сервера, которому будет отправлен запрос. Должен начинаться с символа '@'. Поставьте дифис чтобы был выбран dns-сервер системы.\n");
      printf("<domain> - domain, о котором нужно найти информацию. Поставьте дефис чтобы был выбран домен example.com\n");
      printf("<type> - тип запроса: А - запрос IPv4 адреса для domain или АААА - запрос IPv6 адреса для domain. Поставьте дефис чтобы был выбран A.\n");
      printf("Пример запуска:\n");
      printf("mini-dig @1.1.1.1 example.com A\n");
}

// Считывает domain name из буфера с полученным dns сообщением
// Принимает указатель на указатель, чтобы иметь возможность его изменить (этот указатель используется для перемещения по буферу)
void parse_name(uint8_t *buffer, uint8_t **p) {
  
}

// argv - указатель на первый элемент массива указателей на строки - массив строк (аргументы командой строки - строки)
int main(int argc, char **argv) {

    struct config config;

    if (argc != 4){
      printf("Error: Аргументы командной строки: вы должны ввести 3 аргумента.\n");
      print_guide();
      return EXIT_FAILURE;
    }

    // Обрабатываю первй аргумент - dns сервер
    // argv[1] - указатель на вторую строку массива, argv[1][0] - первый символ этой строки, *argv[1] то же самое, что и argv[1][0]
    if (argv[1][0] == '-'){
      printf("Для отправки DNS-запроса выбран DNS-сервер системы.\n");
      config.server = NULL;
    }
    else if (argv[1][0] == '@') {
      if (argv[1][1] == '\0') {
        print_guide();
        return EXIT_FAILURE;
      }
        config.server = argv[1] + 1; // Получаю указатель на второй символ второй строки массива
    }
    else {
      printf("Error: Аргументы командной строки: DNS-сервер неверен.\n");

      return EXIT_FAILURE; 
    }
    
    // Обрабатываю второй аргумент - domain
    if (argv[2][0] == '-'){
      printf("Для отправки DNS-запроса выбран domain example.com.\n");
      config.domain = "example.com";
    }
    else {
      config.domain = argv[2];
    }

    // Обрабатываю третий аргумент - type
    if (argv[3][0] == '-'){
      printf("Для отправки DNS-запроса выбран type A - запрос IPv4 адреса.\n");
      config.type = A;
    }
    else {
      if (strcmp(argv[3], "A") == 0){
        config.type = A;
      }
      else if (strcmp(argv[3], "AAAA") == 0){
        config.type = AAAA;
      }
      else {
        printf("Error: Аргументы командной строки: тип запроса неверен.\n");
        print_guide();
        return EXIT_FAILURE; 
      }
    }

    // Создаю сокет
    // Socket function returns a file descriptor (sockfd)
    // AF_INET - IPv4 protocol family, datagram - stream socket type, 0 - system's default protocol augment.
    int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0) {
      perror("Error: Ошибка создания сокета :( socket().\n");
      return -1;
    }
    printf("Сокет создан успешно. Номер сокета: %d\n", sock_fd);

      // У резолвера есть 5 секунд, чтобы прислать ответ
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    // Настраиваю сокет, чтобы у него появился таймаут. level is specified as SOL_SOCKET. The  arguments optval and optlen are used to access option values for setsockopt(). Optname and any specified options are passed uninterpreted to the appropriate protocol module for interpretation.
    ssize_t result_of_call = setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    if (result_of_call == -1) {
      perror("Error: Не удалось модифицировать сокет для таймаута, setsockopt().\n");
      return EXIT_FAILURE; 
    }

    // Пока что поддержка только IPv4
    // Адрес DNS-сервера
    // Создаю структуру - адрес сокета. (см. содержимое  структуры sockaddr_in в man)
    struct sockaddr_in server_addr;
    // sockaddr_in - IPv4 Socket Address Structure, defined by including the <netinet/in.h> header.
    // Очищаю память структуры адреса сокета. Параметры: указатель на начало структуры, хранит адрес, 0 - все байты структуры станут равны нулю, sizeof вычисляет размер структуры в байтах
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET; //IPv4 protocol family
    server_addr.sin_port = htons(53); //htons меняет порядок байтов хоста на Big-Endian (сетевой формат)
    // inet_aton преобразует строку с адресом в сетевой формат и сохраняет результат в структуре типа in_addr
    if (inet_aton(config.server, &server_addr.sin_addr) != 1) { //htonl переводит long в Big-Endian, но для 0.0.0.0 это необязатьно
      fprintf(stderr, "Error: invalid DNS server address.\n");
    } 
    

    // Собираю DNS сообщение вручную. Размер ограничен 512 байтами для dns поверх udp
    uint8_t send_buffer[512];
    size_t pos = 0; // текущая позиция
    uint16_t send_transaction_id = 0x1234; //для сравнения с ID DNS-ответа
    
    send_buffer[pos++] = 0x12; // Transaction ID (позже будет генерироваться случайно)
    send_buffer[pos++] = 0x34;
    send_buffer[pos++] = 0b00000001; // query, recursion desired
    send_buffer[pos++] = 0b00000000;

    send_buffer[pos++] = 0; // QDCOUNT
    send_buffer[pos++] = 1;

    send_buffer[pos++] = 0; // ANCOUNT
    send_buffer[pos++] = 0;

    send_buffer[pos++] = 0; // NSCOUNT
    send_buffer[pos++] = 0;

    send_buffer[pos++] = 0; // ARCOUT
    send_buffer[pos++] = 0;

    // Question section
    // Query Name
    const char *label = config.domain; // Указатель для перемещения между словами
    const char *p = config.domain; // Указатель для перемещения по слову
    while (*p != '\0') {
      if (*p != '.') {
        p++;
        continue;
      }
      send_buffer[pos++] = p - label;
      while ((p - label) != 0) {
        send_buffer[pos++] = *label;
        label++;
      }
      p++;
      label++;
      continue;
    }
    send_buffer[pos++] = p - label;
    while ((p - label) != 0) {
        send_buffer[pos++] = *label;
        label++;
    }
    send_buffer[pos++] = 0; // data label заканчивается нулем

// Query Type
    switch (config.type)
    {
    case A:
      send_buffer[pos++] = 0;
      send_buffer[pos++] = 1;
      break;
    case AAAA:
      send_buffer[pos++] = 0;
      send_buffer[pos++] = 28;
      break;
    default:
      printf("Error: Аргументы командной строки: тип запроса неверен.\n");
      print_guide();
      return EXIT_FAILURE; 
    }

    send_buffer[pos++] = 0; // Query Class
    send_buffer[pos++] = 1;

    // Отправка dns запроса (см. man sendto)
    result_of_call = sendto(sock_fd, send_buffer, pos, 0, (struct sockaddr *) &server_addr, sizeof(server_addr));
    if (result_of_call == -1){  
      perror("Error: Сообщение не отправлено. sendto().\n");
      return EXIT_FAILURE; 
    }
    if (result_of_call < pos){  
      printf("Отправлено меньше байт, чем сформировано.\n");
    }
    if (result_of_call > pos){  
      printf("Отправлено больше байт, чем сформировано.\n");
    }
    if (result_of_call == pos){  
      printf("DNS-сообщение отправлено успешно.\n");
    }

    uint8_t recieve_buffer[512];
    socklen_t addr_len = sizeof(server_addr);
    // Получение ответа
    result_of_call = recvfrom(sock_fd, recieve_buffer, 512, 0, (struct sockaddr *) &server_addr, &addr_len);
    if (result_of_call == -1){  
      perror("Error: Не удалось получить датаграммку. recvfrom().\n");
      return EXIT_FAILURE; 
    }    
    printf("DNS-ответ получен: %zd байт\n", result_of_call);
    
    // Записываю поле ID транзакции в 2-х байтовую переменную
    uint16_t id = ((uint16_t)recieve_buffer[0] << 8) | recieve_buffer[1]; // Сдвигаю первый байт на 8 разрядов влево, а второй байт приравниваю второму байту буфера получения
    if (id != send_transaction_id) {
      printf("Полученный DNS-ответ содержит ID-транзакции отличный от отправленного DNS-запроса: %u (был отправлен ID = %u)!\n", (unsigned int)id, (unsigned int)send_transaction_id);
    }

    
    uint16_t recv_qdcount = ((uint16_t)recieve_buffer[4] << 8) | recieve_buffer[5]; 
    uint16_t recv_ancount = ((uint16_t)recieve_buffer[6] << 8) | recieve_buffer[7]; 
    uint16_t recv_nscount = ((uint16_t)recieve_buffer[8] << 8) | recieve_buffer[9]; 
    uint16_t recv_arcount = ((uint16_t)recieve_buffer[10] << 8) | recieve_buffer[11]; 
    printf("Количество resource records в секциях: QDCOUNT = %u, ANCOUNT = %u, NSCOUNT = %u, ARCOUNT = %u. \n", (unsigned int)recv_qdcount, (unsigned int)recv_ancount, (unsigned int)recv_nscount, (unsigned int)recv_arcount);
    uint8_t label_len;
    if (recv_qdcount != 1) {
      printf("Ух-ты, количество записей QDCOUNT отличается от единицы! Сравните имена запросов с теми, что вводили вы:\n");;
      p = &recieve_buffer[12];
      for (int i = recv_qdcount; i != 0; i--) {
        while (*p != 0) {
          label_len = *p;
          p++;
          while (label_len != 0){
            putchar(*p);
            label_len--;
            p++;
          }
          putchar('.');
        }
        p += 5; // Пропуск байта 0, которым заканчивается query name и полей query type и query class (по 2 байта каждое)
        printf("\n");  
      }   
    }
    

    return 0;
}






