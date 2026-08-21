// gcc mini-dig.c -o mini-dig
// ! Сейчас происходит изменение программы из мешанины в структурированный вид
// TODO: Добавить поддержку IPv6
// TODO: Добавить новые типы запросов (в первую очередь NS)
// TODO: Добавить анализ флагов DNS заголовка
// TODO: Добавить анализ класса и типа в секции Query
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdint.h>
#include <stdbool.h>

// Структура записи из секции Question
struct dns_question
{
  const char *name;
  uint16_t type;
  uint16_t class;
};

// Структура RR из секций answers, authority и additional
struct dns_rr
{
  char *name;
  uint16_t type;
  uint16_t class;
  uint32_t ttl;
  uint16_t rdlength;
  uint8_t *rdata;
};

// Структура DNS сообщения
struct dns_message
{
  uint16_t id;

  uint16_t flags;

  uint16_t qdcount;
  uint16_t ancount;
  uint16_t nscount;
  uint16_t arcount;

  struct dns_question *questions;
  struct dns_rr *answers;
  struct dns_rr *authority;
  struct dns_rr *additional;
};

// Тип запроса
enum query_type
{
  DNS_A = 1,
  DNS_AAAA = 28
};

// То, что ввел user
struct config
{
  const char *server; // const, чтобы нельзя было изменить символы строк
  const char *domain_name;
  enum query_type type;
};

// Читает 2-байтное число побайтово
uint16_t read_u16(const uint8_t *p)
{
  return ((uint16_t)p[0] << 8) | p[1];
}

// Читает 4-байтное число побайтово
uint32_t read_u32(const uint8_t *p)
{
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

// Записывает 2-байтное число побайтово
void write_u16(uint8_t *p, uint16_t value)
{
  p[0] = (value >> 8) & 0xFF;
  p[1] = value & 0xFF;
}

// Записывает 4-байтное число побайтово
void write_u32(uint8_t *p, uint32_t value)
{
  p[0] = (value >> 24) & 0xFF;
  p[1] = (value >> 16) & 0xFF;
  p[2] = (value >> 8) & 0xFF;
  p[3] = value & 0xFF;
}

// Принимает указатель на указатель, чтобы иметь возможность изменять сам указатель
// Записывает доменное имя в буфер в формате DNS. В случае успешной записи возвращает true
bool write_dns_name(uint8_t **p, const char *name)
{
  // pos перемещается по словами
  // label перемещается по целому имени
  const char *pos = name;
  const char *label = name;
  while (*pos != '\0')
  {
    if (*pos != '.')
    {
      pos++;
      continue;
    }

    // p указывает на точку
    if ((pos - label) > 63)
    { // Проверка на слишком длинные лейблы
      return false;
    }

    **p = (uint8_t)(pos - label);
    (*p)++;
    while (pos != label)
    {
      **p = (uint8_t)*label;
      label++;
      (*p)++;
    }
    // pos и label сейчас указывают на точку
    pos++;
    label++;
    continue;
  }

  // Когда p = '\0', а label указывает на первый символ top-level domain
  if ((pos - label) > 63)
  { // Проверка на слишком длинные лейблы
    return false;
  }

  **p = (uint8_t)(pos - label);
  (*p)++;
  while (pos != label)
  {
    **p = (uint8_t)*label;
    label++;
    (*p)++;
  }

  // В конец domain name прилепить 0
  **p = 0;
  (*p)++;
  // Теперь p указывает на начало следующего поля
  return true;
}

// TODO: Добавить проверку на выход за пределы буфера. Пока что send_buffer_size не используется
// Заполняет буфер отправляемого сообщения по содержимого структуры dns-сообщения. Возвращает размер заполненного буфера (DNS-сообщения)
size_t build_dns_message(const struct dns_message *message, uint8_t *send_buffer, size_t send_buffer_size)
{
  uint8_t *p = send_buffer; // текущая позиция
  // Сейчас p указывает на первый байт буфера для dns сообщения (send_buffer[0])
  write_u16(p, message->id);
  p += 2;
  write_u16(p, message->flags);
  p += 2;
  write_u16(p, message->qdcount);
  p += 2;
  write_u16(p, message->ancount);
  p += 2;
  write_u16(p, message->nscount);
  p += 2;
  write_u16(p, message->arcount);
  p += 2;
  // Сейчас p указывает на номер первого байта Question section - начало поля Query Name
  // Для исходящего запроса ожидается одна запись Question

  // write_dns_name принимает указатель на указатель, чтобы иметь возможность изменять сам указатель
  if (!write_dns_name(&p, message->questions->name))
  {
    return 0; // вернет 0 байт - ошибка
  }

  // Сейчас p указывает на начало поля Query Type
  write_u16(p, message->questions->type);
  p += 2;
  // Сейчас p указывает на начало поля Query Class
  write_u16(p, message->questions->class);
  p += 2;

  // Сейчас р указывает на начало новой секции
  // Так как это заполнение dns-запроса, все секции кроме Question отсутствуют
  return (p - send_buffer); // Размер в байтах (сколько было заполнено)
}

// Выводит mini guide на mini pig
void print_guide()
{
  printf("Запуск утилиты выглядит так:\n");
  printf("./mini-dig <server> <domain> <type>.\n");
  printf("<server> - IPv4 адрес DNS-сервера, которому будет отправлен запрос. Должен начинаться с символа '@'. Поставьте дифис чтобы был выбран dns-сервер системы.\n");
  printf("<domain> - domain, о котором нужно найти информацию. Поставьте дефис чтобы был выбран домен example.com\n");
  printf("<type> - тип запроса: А - запрос IPv4 адреса для domain или АААА - запрос IPv6 адреса для domain. Поставьте дефис чтобы был выбран A.\n");
  printf("Пример запуска:\n");
  printf("./mini-dig @8.8.8.8 google.com A\n");
  printf("(Программа может обработать compression label на весь domain name, Но в случае мешанины из data label и compression label будет выведен некорретный domain name).\n");
}

// выводит буфер приема/получения в терминал для отладки
void hex_dump(const uint8_t *buffer, size_t len)
{
  for (size_t i = 0; i < len; i += 10)
  {
    printf("%04zx: ", i);
    for (size_t j = 0; j < 10; j++)
    {
      if ((i + j) < len)
      {
        printf("%02x ", buffer[i + j]);
      }
      else
      {
        printf("   ");
      }
    }
    printf("\n");
  }
}

// Считывает domain name из буфера с полученным dns сообщением
// Принимает указатель на указатель, чтобы иметь возможность изменить сам указатель (адрес) (этот указатель используется для перемещения по буферу)
void parse_name(uint8_t *buffer, uint8_t **p)
{
  uint8_t label_len;        // Длина имени между точками
  uint8_t *name_start = *p; // Пусть тут хранится прежнее значение указателя, так как перемещаясь по ссылке надо будет вернуться
  uint16_t offset;          // Будет хранить смещение в случае обнаружения ссылки
  bool compressed = false;  // Поможет понять, надо ли возвращать p на место для дальнейшего парсинга сообщения
  if ((**p & 0xC0) == 0b11000000)
  { // 1100 0000b = 0Ch, установленные первые два бита - признак compression label
    // **p - указатель на указатель. *p указывает на конкретный байт в массиве uint8_t[512]
    /*
    (*p)[0] - Указывает на первый байт ссылки, скорее всего это 192d
    (*p)[1] - Указывает на смещение
    ((*p)[0] & 0b00111111) - стираю служебные биты в первом байте
    (uint16_t)((*p)[0] & 0b00111111) - преобразую первый байт в тип uint16_t
    ((uint16_t)((*p)[0] & 0b00111111) << 8) - сдвигаю полученное число на 8 разрядов влева
    ((uint16_t)((*p)[0] & 0b00111111) << 8) | (*p)[1]; - особожденные 8 разрядов занимает второй байт смещения*/
    offset = ((uint16_t)((*p)[0] & 0b00111111) << 8) | (*p)[1];
    compressed = true;
    *p = buffer + offset;
  }
  while (**p != 0)
  {
    label_len = **p;
    (*p)++;
    while (label_len != 0)
    {
      putchar(**p);
      label_len--;
      (*p)++;
    }
    putchar('.'); // точка в самом конце доменого имени это не баг, а абсолютный путь
  }
  (*p)++; // Пропускаю завершающий 0
  if (compressed)
  {
    *p = name_start + 2; // Пропускаю 2 байта compression label
  }
}

// argv - указатель на первый элемент массива указателей на строки - массив строк (аргументы командой строки - строки)
int main(int argc, char **argv)
{
  printf("\n");

  struct config config;

  if (argc != 4)
  {
    printf("Error: Аргументы командной строки: вы должны ввести 3 аргумента.\n");
    print_guide();
    return EXIT_FAILURE;
  }

  // Обрабатываю первй аргумент - dns сервер
  // argv[1] - указатель на вторую строку массива, argv[1][0] - первый символ этой строки, *argv[1] то же самое, что и argv[1][0]
  if (argv[1][0] == '-')
  {
    printf("Для отправки DNS-запроса выбран DNS-сервер системы.\n");
    config.server = NULL;
  }
  else if (argv[1][0] == '@')
  {
    if (argv[1][1] == '\0')
    {
      print_guide();
      return EXIT_FAILURE;
    }
    config.server = argv[1] + 1; // Получаю указатель на второй символ второй строки массива
  }
  else
  {
    printf("Error: Аргументы командной строки: DNS-сервер неверен.\n");
    return EXIT_FAILURE;
  }

  // Обрабатываю второй аргумент - domain
  if (argv[2][0] == '-')
  {
    printf("Для отправки DNS-запроса выбран domain example.com.\n");
    config.domain_name = "example.com";
  }
  else
  {
    config.domain_name = argv[2];
  }

  // Обрабатываю третий аргумент - type
  if (argv[3][0] == '-')
  {
    printf("Для отправки DNS-запроса выбран type A - запрос IPv4 адреса.\n");
    config.type = DNS_A;
  }
  else
  {
    if (strcmp(argv[3], "A") == 0)
    {
      config.type = DNS_A;
    }
    else if (strcmp(argv[3], "AAAA") == 0)
    {
      config.type = DNS_AAAA;
    }
    else
    {
      printf("Error: Аргументы командной строки: тип запроса неверен.\n");
      print_guide();
      return EXIT_FAILURE;
    }
  }

  // Создаю сокет
  // Socket function returns a file descriptor (sockfd)
  // AF_INET - IPv4 protocol family, datagram - stream socket type, 0 - system's default protocol augment
  int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock_fd < 0)
  {
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
  int result_of_call_int;               // Будет принимать возвращаемое значение функциями
  ssize_t result_of_call_bytes_ssize_t; // Будет принимать возвращаемое значение функциями
  result_of_call_int = setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  if (result_of_call_int == -1)
  {
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
  server_addr.sin_family = AF_INET; // IPv4 protocol family
  server_addr.sin_port = htons(53); // htons меняет порядок байтов хоста на Big-Endian (сетевой формат)
  // inet_aton преобразует строку с адресом в сетевой формат и сохраняет результат в структуре типа in_addr
  if (inet_aton(config.server, &server_addr.sin_addr) != 1)
  { // htonl переводит long в Big-Endian, но для 0.0.0.0 это необязатьно
    fprintf(stderr, "Error: invalid DNS server address.\n");
    return EXIT_FAILURE;
  }

  // ! Собираю DNS сообщение. Размер ограничен 512 байтами для dns поверх udp
  struct dns_question question; // Создаю структуру секции question
  question.type = config.type;
  question.class = 1;
  question.name = config.domain_name;

  struct dns_message message; // Сначала заполняю структуру dns сообщения
  message.id = 0x1234;
  message.flags = 0b0000000100000000; // QR = 0, RD = 1
  message.qdcount = 1;
  message.ancount = 0;
  message.nscount = 0;
  message.arcount = 0;
  message.questions = &question; // ссылаюсь на созданную структуру секции question
  
  uint8_t send_buffer[512];

  // Заполняет буфер отправляемого сообщения по содержимого структуры dns-сообщения. Возвращает размер заполненного буфера (DNS-сообщения)
  size_t send_message_len = build_dns_message(&message, send_buffer, sizeof(send_buffer));
  if (send_message_len == 0)
  {
    printf("Error: Длина каждого лейбла в domain name не должна превышать 63 символа\n");
    return EXIT_FAILURE;
  }

  printf("\nСобираю DNS сообщение: Transaction ID = 0x%04x, flags = 0x%04x, qdcount = 0x%04x, ancount = 0x%04x, nscount = 0x%04x, arcount = 0x%04x. Длина сформированного DNS-запроса: %zu\n", message.id, message.flags, message.qdcount, message.ancount, message.nscount, message.arcount, send_message_len);

  // Отладочная информация
  printf("\nGenerated send buffer: \n");
  hex_dump(send_buffer, send_message_len);
  printf("\n");

  // Отправка dns запроса (см. man sendto)
  result_of_call_bytes_ssize_t = sendto(sock_fd, send_buffer, send_message_len, 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
  if (result_of_call_bytes_ssize_t == -1)
  {
    perror("Error: Сообщение не отправлено. sendto().\n");
    return EXIT_FAILURE;
  }
  if (result_of_call_bytes_ssize_t < send_message_len)
  {
    printf("Отправлено меньше байт, чем сформировано. Отправлено: %zu байт\n", result_of_call_bytes_ssize_t);
  }
  if (result_of_call_bytes_ssize_t > send_message_len)
  {
    printf("Отправлено больше байт, чем сформировано. Отправлено: %zu байт\n", result_of_call_bytes_ssize_t);
  }
  if (result_of_call_bytes_ssize_t == send_message_len)
  {
    printf("DNS-сообщение отправлено успешно. Отправлено: %zu байт\n", result_of_call_bytes_ssize_t);
  }

  uint8_t recieve_buffer[512];
  socklen_t addr_len = sizeof(server_addr);
  // Получение ответа
  result_of_call_bytes_ssize_t = recvfrom(sock_fd, recieve_buffer, 512, 0, (struct sockaddr *)&server_addr, &addr_len);
  if (result_of_call_bytes_ssize_t == -1)
  {
    perror("Error: Не удалось получить датаграммку. recvfrom().\n");
    return EXIT_FAILURE;
  }

  if (result_of_call_bytes_ssize_t < 12)
  {
    perror("Error: DNS-сообщение оказалось короче 12 байт (У DNS-сообщений фиксированный заголовок размером 12 байт). recvfrom().\n");
    return EXIT_FAILURE;
  }

  printf("\nDNS-ответ получен: %zd байт\n", result_of_call_bytes_ssize_t);

  printf("\nReceived buffer: \n");
  hex_dump(recieve_buffer, result_of_call_bytes_ssize_t);
  printf("\n");

  // Записываю поле ID транзакции в 2-х байтовую переменную
  uint16_t id = ((uint16_t)recieve_buffer[0] << 8) | recieve_buffer[1]; // Сдвигаю первый байт на 8 разрядов влево, а второй байт приравниваю второму байту буфера получения
  if (id != message.id)
  {
    printf("Полученный DNS-ответ содержит ID-транзакции отличный от отправленного DNS-запроса: %u (был отправлен ID = %u)!\n", (unsigned int)id, (unsigned int)message.id);
  }

  // Разбор флагов сделать сложнее, он будет реализован позже

  uint16_t recv_qdcount = ((uint16_t)recieve_buffer[4] << 8) | recieve_buffer[5];
  uint16_t recv_ancount = ((uint16_t)recieve_buffer[6] << 8) | recieve_buffer[7];
  uint16_t recv_nscount = ((uint16_t)recieve_buffer[8] << 8) | recieve_buffer[9];
  uint16_t recv_arcount = ((uint16_t)recieve_buffer[10] << 8) | recieve_buffer[11];
  printf("Количество resource records в секциях: QDCOUNT = 0x%04x, ANCOUNT = 0x%04x, NSCOUNT = 0x%04x, ARCOUNT = 0x%04x. \n\n", (unsigned int)recv_qdcount, (unsigned int)recv_ancount, (unsigned int)recv_nscount, (unsigned int)recv_arcount);
  uint8_t *recv_p = &recieve_buffer[12]; // recv_p указывает на начало первой секции
  if (recv_qdcount < 1)
  {
    printf("Записей в секции Question нет\n");
  }
  else if (recv_qdcount != 1)
  {
    printf("Ух-ты, количество записей QDCOUNT отличается от единицы! Сравните имена запросов с теми, что вводили вы:\n");
    ;
  }
  printf("\nСекция Question: \n");
  for (int i = recv_qdcount; i != 0; i--)
  {
    parse_name(recieve_buffer, &recv_p); // recieve_buffer - адрес первого байта буфера, &recv_p - адрес переменной-указателя
    // recv_p указывает на начало Query Type
    recv_p += 4; // Пропуск полей query type и query class (по 2 байта каждое)
    printf("\n");
  }

  // recv_p указывает на начало секции answer
  uint16_t recv_rr_type;  // Хранит тип rr полученного dns-сообщения
  uint16_t recv_rr_class; // Хранит класс
  uint32_t recv_rr_ttl;
  uint16_t recv_rr_rdlength;
  uint16_t presumed_recv_rr_rdlength = 0;
  char data[INET6_ADDRSTRLEN];
  if (recv_ancount < 1)
  {
    printf("Записей в секции Answer нет :(\n");
  }
  else
  {
    printf("\n\nСекция Answer: \n");
    for (int i = recv_ancount; i != 0; i--)
    {
      // Снова читаю и вывожу доменные имена
      parse_name(recieve_buffer, &recv_p);
      // recv_p указывает на начало поля Type
      recv_rr_type = ((uint16_t)(recv_p[0]) << 8) | recv_p[1];
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
      recv_p += 2; // Смещаю указатель на 2 байта, так как поле Type пройдено
      // recv_p указывает на начало поля Class
      recv_rr_class = ((uint16_t)(recv_p[0]) << 8) | recv_p[1];
      switch (recv_rr_class)
      {
      case 1:
        printf("RR class is Internet.\n");
        break;
      default:
        printf("Класс RR нераспознан\n");
        break;
      }
      recv_p += 2;
      // recv_p указывает на начало поля TTL
      // Поле TTL будет потолще, поэтому в uint16_t не влезет
      recv_rr_ttl = ((uint32_t)(recv_p[0]) << 24) | ((uint32_t)(recv_p[1]) << 16) | ((uint32_t)(recv_p[2]) << 8) | recv_p[3];
      printf("Запись будет храниться TTL = %u секунд.\n", (unsigned int)recv_rr_ttl);
      recv_p += 4;
      // recv_p указывает на начало поля RDLENGTH
      recv_rr_rdlength = ((uint16_t)(recv_p[0]) << 8) | recv_p[1];
      printf("Поле данных занимает RDLENGTH = %u байт.\n", (unsigned int)recv_rr_rdlength);
      if (presumed_recv_rr_rdlength != 0 && presumed_recv_rr_rdlength != recv_rr_rdlength)
      {
        printf("Поле данных занимает не столько байтов, сколько предполагалось на основе типа RR.\n");
      }
      recv_p += 2;
      // recv_p указывает на начало поля RDATA
      switch (recv_rr_type)
      {
      case 1:
        if (inet_ntop(AF_INET, recv_p, data, sizeof(data)) == NULL)
        {
          printf("Ошибка перевода IPv4 адреса из сетевого в текстовый формат, inet_ntop().\n");
        }
        else
        {
          printf("Получен IPv4-адрес: %s\n", data);
        }
        break;
      case 28:
        if (inet_ntop(AF_INET6, recv_p, data, sizeof(data)) == NULL)
        {
          printf("Ошибка перевода IPv6 адреса из сетевого в текстовый формат, inet_ntop().\n");
        }
        else
        {
          printf("Получен IPv6-адрес: %s\n", data);
        }
        break;
      default:
        printf("\nДанные записи не будут выведены, так как тип RR нераспознан\n");
        printf("\n\nШутка. Вот сие нечто:\n");
        for (uint16_t i = 0; i < recv_rr_rdlength; i++)
        {
          printf("%02x ", recv_p[i]);
        }
        printf("\n");
        break;
      }
      printf("\n");
      recv_p += recv_rr_rdlength;
    }
  }
  if (recv_nscount < 1)
  {
    printf("\n\nЗаписей в секции Authority нет.\n");
  }
  if (recv_arcount < 1)
  {
    printf("\n\nЗаписей в секции Additional Information нет.\n");
  }

  printf("\n\n\nДамп для отладки:\n");
  printf("\n");
  printf("Generated send buffer: \n");
  hex_dump(send_buffer, send_message_len);
  printf("\n");
  printf("Received buffer: \n");
  hex_dump(recieve_buffer, result_of_call_bytes_ssize_t);

  return 0;
}
