// gcc mini-dig.c -o mini-dig
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdint.h>
#include <stdbool.h>

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
      printf("(Программа может обработать compression label на весь domain name, Но в случае мешанины из data label и compression label будет выведен некорретный domain name).\n");
}

// Считывает domain name из буфера с полученным dns сообщением
// Принимает указатель на указатель, чтобы иметь возможность изменить сам указатель (адрес) (этот указатель используется для перемещения по буферу)
void parse_name(uint8_t *buffer, uint8_t **p) {
  uint8_t label_len; // Длина имени между точками
  uint8_t *name_start = *p; // Пусть тут хранится прежнее значение указателя, так как перемещаясь по ссылке надо будет вернуться
  uint16_t offset; // Будет хранить смещение в случае обнаружения ссылки
  bool compressed = false; // Поможет понять, надо ли возвращать p на место для дальнейшего парсинга сообщения
  if ((**p & 0xC0) == 0b11000000) { // 1100 0000b = 0Ch, установленные первые два бита - признак compression label
    // **p - указатель на указатель. *p указывает на конкретный байт в массиве uint8_t[512]
    /*
    (*p)[0] - Указывает на первый байт ссылки, скорее всего это 192d
    (*p)[1] - Указывает на смещение
    ((*p)[0] & 0b00111111) - стираю служебные биты в первом байте
    (uint16_t)((*p)[0] & 0x3F) - преобразую первый байт в тип uint16_t
    ((uint16_t)((*p)[0] & 0x3F) << 8) - сдвигаю полученное число на 8 разрядов влева
    ((uint16_t)((*p)[0] & 0x3F) << 8) | (*p)[1]; - особожденные 8 разрядов занимает второй байт смещения*/
    offset = ((uint16_t)((*p)[0] & 0x3F) << 8) | (*p)[1];
    compressed = true;
    *p = buffer + offset;
  }
  while (**p != 0) {
          label_len = **p;
          (*p)++;
          while (label_len != 0){
            putchar(**p);
            label_len--;
            (*p)++;
          }
          putchar('.'); // точка в самом конце доменого имени это не баг, а абсолютный путь
  }
  (*p)++; // Пропускаю завершающий 0
  if (compressed) {
    *p = name_start + 2; // Пропускаю 2 байта compression label
  } 
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
    // ssize_t, так как recvfrom() возвращает этот тип данных
    int result_of_call_int; // Будет принимать возвращаемое значение функциями
    ssize_t result_of_call_bytes_ssize_t; // Будет принимать возвращаемое значение функциями
    result_of_call_int = setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    if (result_of_call_int == -1) {
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
      return EXIT_FAILURE;
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
    result_of_call_bytes_ssize_t = sendto(sock_fd, send_buffer, pos, 0, (struct sockaddr *) &server_addr, sizeof(server_addr));
    if (result_of_call_bytes_ssize_t == -1){  
      perror("Error: Сообщение не отправлено. sendto().\n");
      return EXIT_FAILURE; 
    }
    if (result_of_call_bytes_ssize_t < pos){  
      printf("Отправлено меньше байт, чем сформировано.\n");
    }
    if (result_of_call_bytes_ssize_t > pos){  
      printf("Отправлено больше байт, чем сформировано.\n");
    }
    if (result_of_call_bytes_ssize_t == pos){  
      printf("DNS-сообщение отправлено успешно.\n");
    }

    uint8_t recieve_buffer[512];
    socklen_t addr_len = sizeof(server_addr);
    // Получение ответа
    result_of_call_bytes_ssize_t = recvfrom(sock_fd, recieve_buffer, 512, 0, (struct sockaddr *) &server_addr, &addr_len);
    if (result_of_call_bytes_ssize_t == -1){  
      perror("Error: Не удалось получить датаграммку. recvfrom().\n");
      return EXIT_FAILURE; 
    }  
    
    if (result_of_call_bytes_ssize_t < 12){  
      perror("Error: DNS-сообщение оказалось короче 12 байт (У DNS-сообщений фиксированный заголовок размером 12 байт). recvfrom().\n");
      return EXIT_FAILURE; 
    }  
    
    printf("DNS-ответ получен: %zd байт\n", result_of_call_bytes_ssize_t);
    
    // Записываю поле ID транзакции в 2-х байтовую переменную
    uint16_t id = ((uint16_t)recieve_buffer[0] << 8) | recieve_buffer[1]; // Сдвигаю первый байт на 8 разрядов влево, а второй байт приравниваю второму байту буфера получения
    if (id != send_transaction_id) {
      printf("Полученный DNS-ответ содержит ID-транзакции отличный от отправленного DNS-запроса: %u (был отправлен ID = %u)!\n", (unsigned int)id, (unsigned int)send_transaction_id);
    }

    // Разбор флагов сделать сложнее, он будет реализован позже

    uint16_t recv_qdcount = ((uint16_t)recieve_buffer[4] << 8) | recieve_buffer[5]; 
    uint16_t recv_ancount = ((uint16_t)recieve_buffer[6] << 8) | recieve_buffer[7]; 
    uint16_t recv_nscount = ((uint16_t)recieve_buffer[8] << 8) | recieve_buffer[9]; 
    uint16_t recv_arcount = ((uint16_t)recieve_buffer[10] << 8) | recieve_buffer[11]; 
    printf("Количество resource records в секциях: QDCOUNT = %u, ANCOUNT = %u, NSCOUNT = %u, ARCOUNT = %u. \n", (unsigned int)recv_qdcount, (unsigned int)recv_ancount, (unsigned int)recv_nscount, (unsigned int)recv_arcount);
    p = &recieve_buffer[12]; // p указывает на начало первой секции
    if (recv_qdcount < 1) {
      printf("Записей в секции Question нет\n");
    }
    else if (recv_qdcount != 1) {
      printf("Ух-ты, количество записей QDCOUNT отличается от единицы! Сравните имена запросов с теми, что вводили вы:\n");;
    } 
    printf("Секция Question: \n");
      for (int i = recv_qdcount; i != 0; i--) {
        parse_name(recieve_buffer, &p); // recieve_buffer - адрес первого байта буфера, &p - адрес переменной-указателя
        p += 4; // Пропуск полей query type и query class (по 2 байта каждое)
        printf("\n");  
      }   
    
    // p указывает на начало секции answer
    uint16_t recv_rr_type; // Хранит тип rr полученного dns-сообщения
    uint16_t recv_rr_class; // Хранит класс
    uint32_t recv_rr_ttl;
    uint16_t recv_rr_rdlength;
    uint16_t presumed_recv_rr_rdlength = 0;
    char data[INET6_ADDRSTRLEN];
    if (recv_ancount < 1) {
      printf("Записей в секции Answer нет :(\n");
    }
    else {
    printf("Секция Answer: \n");
    for (int i = recv_ancount; i != 0; i--) {
      // Снова читаю и вывожу доменные имена 
      parse_name(recieve_buffer, &p);
      // p указывает на начало поля Type
      recv_rr_type = ((uint16_t)(p[0]) << 8) | p[1];
      switch (recv_rr_type)
      {
      case 1:
        // В поле RDLENGTH будет значение 4
        printf("\nA: RR type is address record for IPv4. RDLENGHT должен быть равен 4 байтам\n");
        presumed_recv_rr_rdlength = 4;
        break;
      case 28:
        // В поле RDLENGTH будет значение 16
        printf("\nAAAA: RR type is address record for IPv6. RDLENGHT должен быть равен 16 байтам\n");
        presumed_recv_rr_rdlength = 16;
        break;
      default:
        printf("\nТип RR нераспознан\n");
        presumed_recv_rr_rdlength = 0;
        break;
      }
      p += 2; // Смещаю указатель на 2 байта, так как поле Type пройдено
      // p указывает на начало поля Class
      recv_rr_class = ((uint16_t)(p[0]) << 8) | p[1];
      switch (recv_rr_class)
      {
      case 1:
        printf("A: RR class is Internet.\n");
        break;
      default:
        printf("Класс RR нераспознан\n");
        break;
      }
      p += 2;
      // p указывает на начало поля TTL
      // Поле TTL будет потолще, поэтому в uint16_t не влезет
      recv_rr_ttl = ((uint32_t)(p[0]) << 24) | ((uint32_t)(p[1]) << 16) | ((uint32_t)(p[2]) << 8) | p[3];
      printf("Запись будет храниться TTL = %u секунд.\n", (unsigned int)recv_rr_ttl);
      p += 4;
      // p указывает на начало поля RDLENGTH
      recv_rr_rdlength = ((uint16_t)(p[0]) << 8) | p[1];
      printf("Поле данных занимает RDLENGTH = %u байт.\n", (unsigned int)recv_rr_rdlength);
      if (presumed_recv_rr_rdlength != 0 && presumed_recv_rr_rdlength != recv_rr_rdlength) {
        printf("Поле данных занимает не столько байтов, сколько предполагалось на основе типа RR.\n");
      }
      p += 2;
      // p указывает на начало поля RDATA
      switch (recv_rr_type)
      {
      case 1:
        if(inet_ntop(AF_INET, p, data, sizeof(data)) == NULL) {
          printf("Ошибка перевода IPv4 адреса из сетевого в текстовый формат, inet_ntop().\n");
        }
        else {
          printf("Получен IPv4-адрес: %s\n", data);
        }
        break;
      case 28:
        if(inet_ntop(AF_INET6, p, data, sizeof(data)) == NULL) {
          printf("Ошибка перевода IPv6 адреса из сетевого в текстовый формат, inet_ntop().\n");
        }
        else {
          printf("Получен IPv6-адрес: %s\n", data);
        }
        break;
      default:
        printf("\nДанные записи не будут выведены, так как тип RR нераспознан\n");
        printf("\n\nШутка. Вот сие нечто:\n");
        for (uint16_t i = 0; i < recv_rr_rdlength; i++) {
          printf("%02x ", p[i]);
        }
        printf("\n");
        break;
      }
      printf("\n");
      p += recv_rr_rdlength;
    }
    }
    if (recv_nscount < 1) {
      printf("Записей в секции Authority нет :(\n");
    }
    if (recv_arcount < 1) {
      printf("Записей в секции Additional Information нет :(\n");
    }
    return 0;
}






