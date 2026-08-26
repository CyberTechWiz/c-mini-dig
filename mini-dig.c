// gcc mini-dig.c -o mini-dig
// TODO: Добавить новые типы запросов (в первую очередь NS)
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>

// Структура записи из секции Question
struct dns_question
{
    char *name;
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

// RCODE Type
const char *rcode_type[] = {
    "No error.",
    "Format error; query cannot be interpreted.",
    "Server failure; error in processing at server.",
    "Nonexistent domain; unknown domain referenced.",
    "Not implemented; request not supported in server.",
    "Refused; server unwilling to provide answer.",
    "Name exists but should not (used with updates).",
    "RRSet exists but should not (used with updates).",
    "RRSet does not exist but should (used with updates).",
    "Server not authorized for zone (used with updates).",
    "Name not contained in zone (used with updates)."};

// Функция возвращает строку в зависимости от типа
const char *query_type_to_str(uint8_t type)
{
    switch (type)
    {
    case DNS_A:
        return "A";
    case DNS_AAAA:
        return "AAAA";
    default:
        return "UNKNOWN";
    }
}

// То, что ввел user
struct config
{
    char *server; // const, чтобы нельзя было изменить символы строк
    char *domain_name;
    enum query_type type;
    int family;
};

// Функци для чтения и записи в буфера. Можно было передавать им не указатель, а указатель на указатель, чтобы они его могли двигать, но это лишит их универсальности
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

// Для проверки выхода за границы буфера
bool buffer_boundaries(const uint8_t *p, const uint8_t *buffer, size_t buffer_size, size_t required_byte_count)
{
    return (((size_t)(p - buffer) + required_byte_count) <= buffer_size);
}

// Освобожаю память, выделенную под сообщения и под секции
void free_dns_message(struct dns_message *message)
{
    if (message->questions != NULL)
    {
        for (size_t i = 0; i < message->qdcount; i++)
        {
            free(message->questions[i].name);
            message->questions[i].name = NULL;
        }
        free(message->questions);
        message->questions = NULL;
    }
    if (message->answers != NULL)
    {
        for (size_t i = 0; i < message->ancount; i++)
        {
            free(message->answers[i].name);
            free(message->answers[i].rdata);
            message->answers[i].name = NULL;
            message->answers[i].rdata = NULL;
        }
        free(message->answers);
        message->answers = NULL;
    }
    if (message->authority != NULL)
    {
        for (size_t i = 0; i < message->nscount; i++)
        {
            free(message->authority[i].name);
            free(message->authority[i].rdata);
            message->authority[i].name = NULL;
            message->authority[i].rdata = NULL;
        }
        free(message->authority);
        message->authority = NULL;
    }
    if (message->additional != NULL)
    {
        for (size_t i = 0; i < message->arcount; i++)
        {
            free(message->additional[i].name);
            free(message->additional[i].rdata);
            message->additional[i].name = NULL;
            message->additional[i].rdata = NULL;
        }
        free(message->additional);
        message->additional = NULL;
    }
}

// Принимает указатель на указатель, чтобы иметь возможность изменять сам указатель
// Записывает доменное имя в буфер в формате DNS. В случае успешной записи возвращает true
bool write_domain_name(uint8_t **p, const char *name)
{
    // pos перемещается по словами
    // label перемещается по целому имени
    const char *pos = name;
    const char *label = name;
    size_t wire_name_len = 0; // Хранит длину целого доменного имени
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
            printf("Error: Длина каждого лейбла в domain name не должна превышать 63 символа\n");
            return false;
        }
        wire_name_len = wire_name_len + (pos - label) + 1;
        if (wire_name_len > 255)
        {
            printf("Error: Длина domain name не должна превышать 255 байт.\n");
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
        printf("Error: Длина каждого лейбла в domain name не должна превышать 63 символа\n");
        return false;
    }

    wire_name_len = wire_name_len + (pos - label) + 1;
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

    if (wire_name_len + 1 > 255)
    {
        printf("Error: Длина domain name не должна превышать 255 байт.\n");
        return false;
    }

    // Теперь p указывает на начало следующего поля
    return true;
}

// В ближайшее время функция будет заполнять всего одну запись в секции question, так что переполнения буфера для домена длиной не более 255 не может произойти
// Заполняет буфер отправляемого сообщения по содержимому структуры dns-сообщения. Возвращает размер заполненного буфера (DNS-сообщения)
size_t build_dns_message(const struct dns_message *message, uint8_t *send_buffer)
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

    // write_domain_name принимает указатель на указатель, чтобы иметь возможность изменять сам указатель
    if (!write_domain_name(&p, message->questions->name))
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

// Считывает domain name из буфера с полученным dns сообщением
// Принимает указатель на указатель, чтобы иметь возможность изменить сам указатель (адрес) (этот указатель используется для перемещения по буферу)
// При ошибке возвращает NULL
// *p перемещается по полю Query Name, если встречает compression label, current_p переходит по смещению, а потом p переходит к окончанию compression label
char *read_domain_name(uint8_t **p, const uint8_t *buffer, size_t buffer_size)
{
    char *name = malloc(256);
    if (name == NULL)
    {
        printf("Error: Ошибка выделения памяти для чтения domain name (255 bytes), malloc()\n");
        return NULL;
    }

    size_t name_len = 0;
    uint8_t label_len = 0;
    uint8_t *current_p = *p; // Будет переходить по ссылкам
    uint8_t first;           // Будет хранить начало labels
    uint16_t offset;         // Будет хранить смещение из compression label

    size_t compression_jumps = 0; // кол-во переходов по ссылкам
    // Хранит текущее состояние
    bool compressed = false;

    while (true)
    {
        // Проверка, что указатель не вышел за пределы массива
        if (current_p < buffer || current_p >= buffer + buffer_size)
        {
            free(name);
            printf("Error: Указатель вышел за пределы буфера полученного DNS-ответа, read_domain_name()\n");
            return NULL;
        }

        first = current_p[0];

        // Проверка на конец domain name
        if (first == 0)
        {
            if (!compressed) // Если p не в ссылке
            {
                current_p++;
                *p = current_p; // Пропускает 0
            }
            break; // Чтение имени закончено
        }

        // Проверка на наличие compression label
        if ((first & 0b11000000) == 0b11000000)
        {
            // Проверка на то, чтобы следующий байт ссылки не был за границами массива
            if (!buffer_boundaries(current_p, buffer, buffer_size, 2))
            {
                free(name);
                printf("Error: Недостаточно данных для чтения compression label, read_domain_name()\n");
                return NULL;
            }
            // (current_p[0] & 0b00111111) - стираю служебные биты в первом байте, чтобы осталась только часть, отвечающая за смещение
            // ((uint16_t)(current_p[0] & 0b00111111) - преобразую первый байт в тип uint16_t
            // ((uint16_t)(current_p[0] & 0b00111111) << 8) - сдвигаю полученное число на 8 разрядов влева
            // ((uint16_t)(current_p[0] & 0b00111111) << 8) | current_p[1] - особожденные 8 разрядов занимает второй байт смещения
            offset = ((uint16_t)(current_p[0] & 0b00111111) << 8) | current_p[1];
            if (offset >= buffer_size)
            {
                free(name);
                printf("Error: Некорректный compression label: ссылка ведёт за пределы буфера полученного DNS-ответа, read_domain_name()\n");
                return NULL;
            }

            if (offset >= (size_t)(current_p - buffer))
            {
                free(name);
                printf("Error: Некорректный compression label: ссылка не указывает назад, read_domain_name()\n");
                return NULL;
            }

            if (++compression_jumps > 255)
            {
                free(name);
                printf("Error: Слишком много переходов по compression label, read_domain_name()\n");
                return NULL;
            }

            // Если это не вложенная ссылка, то выставляю флаг перехода по ссылке
            if (!compressed)
            {
                *p = current_p + 2;
                compressed = true;
            }
            // Перехожу по ссылке и читаю имя далее
            current_p = (uint8_t *)buffer + offset;
            continue;
        }

        // Корректное начало domain name
        if ((first & 0b11000000) != 0)
        {
            free(name);
            printf("Error: Некорректный первый байт domain name, read_domain_name()\n");
            return NULL;
        }

        // Each label can be up to 63 characters long
        label_len = first;
        if (label_len > 63)
        {
            free(name);
            printf("Error: Each label can be up to 63 characters long, read_domain_name()\n");
            return NULL;
        }

        // Если это не первый label, добавляю точку
        if (name_len != 0)
        {
            name[name_len++] = '.';
        }

        // Проверяю, что размер результирующей строки не выходит за пределы
        if (name_len + label_len >= 256)
        {
            printf("Error: Entire fully qualified domain name is limited to at most 255 characters, read_domain_name()\n");
            free(name);
            return NULL;
        }

        if (!buffer_boundaries(current_p + 1, buffer, buffer_size, label_len))
        {
            printf("Error: Недостаточно данных в буфере для чтения label, read_domain_name()\n");
            free(name);
            return NULL;
        }

        memcpy(name + name_len, current_p + 1, label_len);
        name_len += label_len;
        current_p++;            // пропускаю кол-во символов в label
        current_p += label_len; // пропускаю сам label
    }

    name[name_len] = '\0';
    return name;
}

bool read_rr(struct dns_rr *rr, uint8_t **p, const uint8_t *buffer, size_t buffer_size)
{
    rr->name = read_domain_name(p, buffer, buffer_size);
    if (rr->name == NULL)
    {
        return false;
    }
    if (!buffer_boundaries(*p, buffer, buffer_size, 10))
    {
        printf("Error: Недостаточное количество данных в буфере для чтения полей типа, класса, ttl и rdlength записи.\n");
        return false;
    }
    rr->type = read_u16(*p);
    *p += 2;
    rr->class = read_u16(*p);
    *p += 2;
    rr->ttl = read_u32(*p);
    *p += 4;
    rr->rdlength = read_u16(*p);
    *p += 2;

    if (rr->rdlength > 0)
    {
        if (!buffer_boundaries(*p, buffer, buffer_size, rr->rdlength))
        {
            printf("Error: Недостаточное количество данных в буфере для чтения поля RDATA записи (RDLENGTH = %u).\n", rr->rdlength);
            free(rr->rdata);
            rr->rdata = NULL;
            return false;
        }
        rr->rdata = malloc(rr->rdlength);
        if (rr->rdata == NULL)
        {
            printf("Error: Ошибка выделения памяти под RDATA для чтения RR, malloc().\n");
            return false;
        }
        memcpy(rr->rdata, *p, rr->rdlength);
        *p += rr->rdlength;
    }
    else
    {
        rr->rdata = NULL;
    }
    return true;
}

//  Читает из буфера полученное dns-сообщение и записывает в структуру recv_message. В случае успеха возвращает 1
bool read_dns_message(struct dns_message *message, uint8_t *buffer, size_t buffer_size)
{
    if (buffer_size < 12)
    {
        printf("DNS Header меньше 12 байт.\n");
        return false;
    }
    uint8_t *p = buffer; // любимый указатель для перемещения по буферу
    // Сейчас р указывает на начало DNS-заголовка
    message->id = read_u16(p);
    p += 2;
    message->flags = read_u16(p);
    p += 2;
    message->qdcount = read_u16(p);
    p += 2;
    message->ancount = read_u16(p);
    p += 2;
    message->nscount = read_u16(p);
    p += 2;
    message->arcount = read_u16(p);
    p += 2;

    // По умолчанию все указывают на NULL, для free(NULL) в будущем
    message->questions = NULL;
    message->answers = NULL;
    message->authority = NULL;
    message->additional = NULL;
    // Выделяю память под структуры (questions - указатель на первый элемент массива структур)
    if (message->qdcount > 0)
    {
        message->questions = calloc(message->qdcount, sizeof(struct dns_question));

        if (message->questions == NULL)
        {
            free_dns_message(message);
            printf("Error: Ошибка выделения памяти под Question Section в DNS-ответе, malloc(). Количество записей в секции: QDCOUNT = %u \n", message->qdcount);
            return false;
        }
    }
    if (message->ancount > 0)
    {
        message->answers = calloc(message->ancount, sizeof(struct dns_rr));
        if (message->answers == NULL)
        {
            free_dns_message(message);
            printf("Error: Ошибка выделения памяти под Answer Section в DNS-ответе, malloc(). Количество записей в секции: ANCOUNT = %u \n", message->ancount);
            return false;
        }
    }
    if (message->nscount > 0)
    {
        message->authority = calloc(message->nscount, sizeof(struct dns_rr));
        if (message->authority == NULL)
        {
            free_dns_message(message);
            printf("Error: Ошибка выделения памяти под Authority Section в DNS-ответе, malloc(). Количество записей в секции: NSCOUNT = %u \n", message->nscount);
            return false;
        }
    }
    if (message->arcount > 0)
    {
        message->additional = calloc(message->arcount, sizeof(struct dns_rr));
        if (message->additional == NULL)
        {
            free_dns_message(message);
            printf("Error: Ошибка выделения памяти под Additional Information Section в DNS-ответе, malloc(). Количество записей в секции: ARCOUNT = %u \n", message->arcount);
            return false;
        }
    }

    // Сейчас р указывает на начало первой секции (если они есть)
    for (uint16_t i = 0; i < message->qdcount; i++)
    {
        message->questions[i].name = read_domain_name(&p, buffer, buffer_size);
        if (message->questions[i].name == NULL)
        {
            free_dns_message(message);
            return false;
        }
        if (!buffer_boundaries(p, buffer, buffer_size, 4))
        {
            free_dns_message(message);
            printf("Error: Недостаточное количество данных в буфере для чтения типа и класса записи в секции question.\n");
            return false;
        }
        message->questions[i].type = read_u16(p);
        p += 2;
        message->questions[i].class = read_u16(p);
        p += 2;
    }
    for (uint16_t i = 0; i < message->ancount; i++)
    {
        if (!read_rr(&message->answers[i], &p, buffer, buffer_size))
        {
            free_dns_message(message);
            return false;
        }
    }
    for (uint16_t i = 0; i < message->nscount; i++)
    {
        if (!read_rr(&message->authority[i], &p, buffer, buffer_size))
        {
            free_dns_message(message);
            return false;
        }
    }
    for (uint16_t i = 0; i < message->arcount; i++)
    {
        if (!read_rr(&message->additional[i], &p, buffer, buffer_size))
        {
            free_dns_message(message);
            return false;
        }
    }
    return true;
}

void output_rr(const struct dns_rr *rr)
{
    char ip_addr[INET6_ADDRSTRLEN];
    int family;
    switch (rr->type)
    {
    case DNS_A:
        if (rr->rdlength != 4)
        {
            printf("Warning: Значение rdlength не соответсвует ожидаемому значению для типа записи А!\n");
        }
        family = AF_INET;
        break;
    case DNS_AAAA:
        if (rr->rdlength != 16)
        {
            printf("Warning: Значение rdlength не соответсвует ожидаемому значению для типа записи АAAA!\n");
        }
        family = AF_INET6;
        break;
    default:
        printf("%-30s %-6s %-6u %-10u %-10u\n", rr->name, query_type_to_str(rr->type), rr->class, rr->ttl, rr->rdlength);
    }
    if (inet_ntop(family, rr->rdata, ip_addr, sizeof(ip_addr)) == NULL)
    {
        printf("Ошибка перевода IP-адреса из сетевого в текстовый формат, inet_ntop().\n");
        printf("%-30s %-6s %-6u %-10u %-10u\n", rr->name, query_type_to_str(rr->type), rr->class, rr->ttl, rr->rdlength);
    }
    else
    {
        printf("%-30s %-6s %-6u %-10u %-10u %-39s\n", rr->name, query_type_to_str(rr->type), rr->class, rr->ttl, rr->rdlength, ip_addr);
    }
}

void output_dns_message(struct dns_message *message)
{
    printf("Transaction ID = 0x%04x \n", (unsigned int)message->id);
    printf("\nFlags = 0x%04x: \nQR = %u \nOpCode = %u%u%u%u \nAA = %u \nTC = %u \nRD = %u \nRA = %u \nZ  = %u \nAD = %u \nCD = %u \nRCODE = %u%u%u%u\n", (unsigned int)message->flags, (unsigned int)(message->flags >> 15) & 1, (unsigned int)(message->flags >> 14) & 1, (unsigned int)(message->flags >> 13) & 1, (unsigned int)(message->flags >> 12) & 1, (unsigned int)(message->flags >> 11) & 1, (unsigned int)(message->flags >> 10) & 1, (unsigned int)(message->flags >> 9) & 1, (unsigned int)(message->flags >> 8) & 1, (unsigned int)(message->flags >> 7) & 1, (unsigned int)(message->flags >> 6) & 1, (unsigned int)(message->flags >> 5) & 1, (unsigned int)(message->flags >> 4) & 1, (unsigned int)(message->flags >> 3) & 1, (unsigned int)(message->flags >> 2) & 1, (unsigned int)(message->flags >> 1) & 1, (unsigned int)(message->flags) & 1);
    // Вывожу значение поля RCODE
    uint8_t rcode_id = (message->flags) & 0b00001111;
    if (rcode_id <= 10) // Чтобы не выйти за границы созданного мной массива rcode_type
    {

        if (rcode_type[rcode_id] != NULL)
        {
            printf("RCODE = %d: %s\n", rcode_id, rcode_type[rcode_id]);
        }
        else
        {
            printf("Unknown RCODE %d\n", rcode_id);
        }
    }
    else
    {
        printf("Unknown RCODE %d\n", rcode_id);
    }
    printf("\nКоличество resource records в секциях: QDCOUNT = 0x%04x, ANCOUNT = 0x%04x, NSCOUNT = 0x%04x, ARCOUNT = 0x%04x. \n\n", (unsigned int)message->qdcount, (unsigned int)message->ancount, (unsigned int)message->nscount, (unsigned int)message->arcount);
    if (message->qdcount > 0)
    {
        printf("\nСекция Question: \n");
        printf("%-30s %-6s %-6s\n", "Name", "Type", "Class");
        for (uint16_t i = 0; i < message->qdcount; i++)
        {
            printf("%-30s %-6s %-6u\n", message->questions[i].name, query_type_to_str(message->questions[i].type), message->questions[i].class);
        }
    }
    else
    {
        printf("Записей в секции Question нет\n");
    }
    if (message->ancount > 0)
    {
        printf("\nСекция Answer: \n");
        printf("%-30s %-6s %-6s %-10s %-10s %-39s\n", "Name", "Type", "Class", "TTL", "RDLENGTH", "RDATA");
        for (uint16_t i = 0; i < message->ancount; i++)
        {
            output_rr(&message->answers[i]);
        }
    }
    else
    {
        printf("\nЗаписей в секции Answer нет\n");
    }
    if (message->nscount > 0)
    {
        printf("\nСекция Authority: \n");
        printf("%-30s %-6s %-6s %-10s %-10s %-39s\n", "Name", "Type", "Class", "TTL", "RDLENGTH", "RDATA");
        for (uint16_t i = 0; i < message->nscount; i++)
        {
            output_rr(&message->authority[i]);
        }
    }
    else
    {
        printf("\nЗаписей в секции Authority нет\n");
    }
    if (message->arcount > 0)
    {
        printf("\nСекция Additional Information: \n");
        printf("%-30s %-6s %-6s %-10s %-10s %-39s\n", "Name", "Type", "Class", "TTL", "RDLENGTH", "RDATA");
        for (uint16_t i = 0; i < message->arcount; i++)
        {
            output_rr(&message->additional[i]);
        }
    }
    else
    {
        printf("\nЗаписей в секции Additional Information нет\n");
    }
}

bool get_system_DNS_server(char *server)
{
    FILE *fp;
    char line[11 + INET6_ADDRSTRLEN];
    fp = fopen("/etc/resolv.conf", "r");
    if (fp == NULL)
    {
        perror("Ошибка открытия файла");
        return false;
    }
    // fgets считывает строку до символа \n или конца файла. В конец строки добавляет \0
    // fgets принимает макисмальный размер буфера и не допускает выход за границы
    while (fgets(line, sizeof(line), fp) != NULL)
    {
        if ((line[0] == '#') || (line[0] == '\n'))
        {
            continue;
        }
        if (strncmp(line, "nameserver ", 11) == 0)
        {
            strcpy(server, line + 11);
            server[strcspn(server, "\n")] = '\0';
            fclose(fp);
            return true;
        }
    }
    fclose(fp);
    return false;
}

// Определить тип адреса: ipv4 или ipv6
int identify_ip_type(const char *ip_str)
{
    if (ip_str == NULL || *ip_str == '\0')
    {
        return 0;
    }
    if (strchr(ip_str, ':') != NULL)
    {
        return AF_INET6;
    }
    if (strchr(ip_str, '.') != NULL)
    {
        return AF_INET;
    }
    return 0;
}

// Адрес DNS-сервера
// Создаю структуру - адрес сокета. (см. содержимое  структуры sockaddr_in в man)
// sockaddr_in - IPv4 Socket Address Structure, defined by including the <netinet/in.h> header.
// memset - Очищаю память структуры адреса сокета. Параметры: указатель на начало структуры, хранит адрес, 0 - все байты структуры станут равны нулю, sizeof вычисляет размер структуры в байтах
// inet_pton преобразует строку с адресом в сетевой формат и сохраняет результат в структуре типа in_addr
struct sockaddr *create_addr(const char *ip_str, uint16_t port, int family, socklen_t *addr_len)
{
    // Проверка на то, что спустя миллион проверок в config.server все еще хранится корректный адрес
    if (ip_str == NULL || *ip_str == '\0')
    {
        return NULL;
    }

    struct sockaddr *server_addr = NULL;
    switch (family)
    {
    case AF_INET:
    {
        struct sockaddr_in *server_addr4 = malloc(sizeof(struct sockaddr_in));
        if (server_addr4 == NULL)
        {
            printf("Error: Ошибка выделения памяти для структуры IPv4 адреса (%zu bytes), malloc()\n", sizeof(struct sockaddr_in));
            return NULL;
        }
        memset(server_addr4, 0, sizeof(*server_addr4));
        server_addr4->sin_family = AF_INET;   // IPv4 protocol family
        server_addr4->sin_port = htons(port); // htons меняет порядок байтов хоста на Big-Endian (сетевой формат)
        if (inet_pton(AF_INET, ip_str, &server_addr4->sin_addr) != 1)
        {
            fprintf(stderr, "Error: invalid DNS server address.\n");
            free(server_addr4);
            return NULL;
        }
        server_addr = (struct sockaddr *)server_addr4;
        *addr_len = sizeof(struct sockaddr_in);
        break;
    }
    case AF_INET6:
    {
        struct sockaddr_in6 *server_addr6 = malloc(sizeof(struct sockaddr_in6));
        if (server_addr6 == NULL)
        {
            printf("Error: Ошибка выделения памяти для структуры IPv6 адреса (%zu bytes), malloc()\n", sizeof(struct sockaddr_in6));
            return NULL;
        }
        memset(server_addr6, 0, sizeof(*server_addr6));
        server_addr6->sin6_family = AF_INET6;
        server_addr6->sin6_port = htons(port);
        if (inet_pton(AF_INET6, ip_str, &server_addr6->sin6_addr) != 1)
        {
            fprintf(stderr, "Error: invalid DNS server address.\n");
            free(server_addr6);
            return NULL;
        }
        server_addr = (struct sockaddr *)server_addr6;
        *addr_len = sizeof(struct sockaddr_in6);
        break;
    }
    default:
    {
        printf("Error: invalid DNS server address.\n");
        return NULL;
    }
    }
    return server_addr;
}

void output_sender_address(struct sockaddr_storage *addr)
{
    char ip_str[INET6_ADDRSTRLEN];
    uint16_t port;

    switch (addr->ss_family)
    {
    case AF_INET:
    {
        struct sockaddr_in *addr4 = (struct sockaddr_in *)addr;
                port = ntohs(addr4->sin_port);
        if (inet_ntop(AF_INET, &addr4->sin_addr, ip_str, sizeof(ip_str)) != NULL)
        {
            printf("Отправитель (IPv4): %s. Порт отправителя: %u\n", ip_str, port);
        }
        break;
    }

    case AF_INET6:
    {
        struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)addr;
        port = ntohs(addr6->sin6_port);
        if (inet_ntop(AF_INET6, &addr6->sin6_addr, ip_str, sizeof(ip_str)) != NULL)
        {
            printf("Отправитель (IPv6): %s. Порт отправителя: %u\n", ip_str, port);
        }
        break;
    }
    default:
    {
        printf("Неизвестное семейство адресов: %d\n", addr->ss_family);
        break;
    }
    }
}

// Случайная генерация для поля transaction id (кому нужен этот getrandom())
// Функция получает 2 байта из системного генератора случайных чисел Linux
bool generate_transaction_id(uint16_t *id)
{
    /// dev/urandom предоставляет данные из криптографически стойкого генератора случайных данных операционной системы.
    int fd = open("/dev/urandom", O_RDONLY); // Open Read Only — открыть только для чтения.
    if (fd == -1)
    {
        perror("Error: Не удалось открыть /dev/urandom. В заголовке будет использоваться Transaction ID = 0x1234. generate_transaction_id()\n");
        return false;
    }

    ssize_t result = read(fd, id, sizeof(*id));
    close(fd);
    // Если по какой-то причине прочитано не 2 байта для id
    if (result != sizeof(*id))
    {
        printf("Error: Не удалось получить случайный Transaction ID. В заголовке будет использоваться Transaction ID = 0x1234. generate_transaction_id()\n");
        return false;
    }

    return true;
}

// Выводит mini guide на mini pig
void print_guide()
{
    printf("Запуск утилиты выглядит так:\n");
    printf("./mini-dig <server> <domain> <type>.\n");
    printf("<server> - IPv4 адрес DNS-сервера, которому будет отправлен запрос. Должен начинаться с символа '@'. Поставьте дифис чтобы был выбран dns-сервер системы (будет прочитан первый из файла /etc/resolv.conf).\n");
    printf("<domain> - domain, о котором нужно найти информацию. Поставьте дефис чтобы был выбран домен example.com\n");
    printf("<type> - тип запроса: А - запрос IPv4 адреса для domain или АААА - запрос IPv6 адреса для domain. Поставьте дефис чтобы был выбран A.\n");
    printf("Пример запуска:\n");
    printf("./mini-dig @8.8.8.8 google.com A\n");
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
        char server[INET6_ADDRSTRLEN]; // инициализирую указатель нормальным адресом
        config.server = server;
        if (!get_system_DNS_server(config.server))
        {
            printf("Ошибка чтения DNS сервера системы.");
            return EXIT_FAILURE;
        }
        printf("Выбран DNS-сервер системы. Прочитанный DNS-сервер: %s\n", config.server);
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
    // datagram - stream socket type, 0 - system's default protocol augment
    config.family = identify_ip_type(config.server);
    if (config.family == 0)
    {
        printf("Не удалось определить тип IP-адреса");
        return EXIT_FAILURE;
    }
    int sock_fd = socket(config.family, SOCK_DGRAM, 0);
    if (sock_fd < 0)
    {
        perror("Error: Ошибка создания сокета :( socket().\n");
        return EXIT_FAILURE;
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
        close(sock_fd);
        return EXIT_FAILURE;
    }

    // Создаю универсальную структуру адреса, которую вернет после приведения типа функция динамически создающая нужную структуру (для поддержки IPv4 и IPv6 одновременно)
    socklen_t addr_len; // Нужно будет передать в sendto(), так как размер адреса зависит от структуры sockaddr_in или sockaddr_in6
    struct sockaddr *server_addr = create_addr(config.server, 53, config.family, &addr_len);
    if (server_addr == NULL)
    {
        close(sock_fd);
        return EXIT_FAILURE;
    }

    // ! Собираю DNS сообщение. Размер ограничен 512 байтами для dns поверх udp
    struct dns_question send_questions; // Создаю структуру секции question
    send_questions.type = config.type;
    send_questions.class = 1;
    send_questions.name = config.domain_name;

    struct dns_message send_message; // Сначала заполняю структуру dns сообщения
    if (!generate_transaction_id(&send_message.id))
    {
        send_message.id = 0x1234;
    }
    send_message.flags = 0b0000000100000000; // QR = 0, RD = 1
    send_message.qdcount = 1;
    send_message.ancount = 0;
    send_message.nscount = 0;
    send_message.arcount = 0;
    send_message.questions = &send_questions; // ссылаюсь на созданную структуру секции question
    send_message.answers = NULL;
    send_message.authority = NULL;
    send_message.additional = NULL;

    uint8_t send_buffer[512];

    // Заполняет буфер отправляемого сообщения по содержимого структуры dns-сообщения. Возвращает размер заполненного буфера (DNS-сообщения)
    size_t send_message_len = build_dns_message(&send_message, send_buffer);
    if (send_message_len == 0)
    {
        free(server_addr);
        close(sock_fd);
        return EXIT_FAILURE;
    }

    printf("\nСобираю DNS сообщение: Transaction ID = 0x%04x, flags = 0x%04x, qdcount = 0x%04x, ancount = 0x%04x, nscount = 0x%04x, arcount = 0x%04x. Длина сформированного DNS-запроса: %zu\n", send_message.id, send_message.flags, send_message.qdcount, send_message.ancount, send_message.nscount, send_message.arcount, send_message_len);

    // Отладочная информация
    printf("\nGenerated send buffer: \n");
    hex_dump(send_buffer, send_message_len);
    printf("\n");

    // Отправка dns запроса (см. man sendto)
    result_of_call_bytes_ssize_t = sendto(sock_fd, send_buffer, send_message_len, 0, server_addr, addr_len);
    if (result_of_call_bytes_ssize_t == -1)
    {
        perror("Error: Сообщение не отправлено. sendto().\n");
        free(server_addr);
        close(sock_fd);
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
    // Получение ответа
    struct sockaddr_storage recv_addr;
    socklen_t recv_addr_len = sizeof(recv_addr);
    result_of_call_bytes_ssize_t = recvfrom(sock_fd, recieve_buffer, 512, 0, (struct sockaddr *)&recv_addr, &recv_addr_len);
    if (result_of_call_bytes_ssize_t == -1)
    {
        perror("Error: Не удалось получить датаграммку. recvfrom().\n");
        free(server_addr);
        close(sock_fd);
        return EXIT_FAILURE;
    }

    if (result_of_call_bytes_ssize_t < 12)
    {
        perror("Error: DNS-сообщение оказалось короче 12 байт (У DNS-сообщений фиксированный заголовок размером 12 байт). recvfrom().\n");
        free(server_addr);
        close(sock_fd);
        return EXIT_FAILURE;
    }

    printf("\nDNS-ответ получен: %zd байт\n", result_of_call_bytes_ssize_t);
    // Выводит адрес и порт отправителя
    output_sender_address(&recv_addr);

    printf("\nReceived buffer: \n");
    hex_dump(recieve_buffer, result_of_call_bytes_ssize_t);
    printf("\n");

    //  ! Чтение полученного DNS-сообщения
    //  Создаю структуры для секций, на которые будут указывать поля структуры dns_messag
    struct dns_message recv_message;

    // Вызываю функцию для чтения dns-сообщения
    if (!read_dns_message(&recv_message, recieve_buffer, result_of_call_bytes_ssize_t))
    {
        free(server_addr);
        close(sock_fd);
        return EXIT_FAILURE;
    }

    // Проверяю, что ID отправленного запроса соответствует ID полученного ответа
    if (send_message.id != recv_message.id)
    {
        printf("Полученный DNS-ответ содержит ID-транзакции отличный от отправленного DNS-запроса: ID = %u (был отправлен ID = %u)!\n", (unsigned int)recv_message.id, (unsigned int)send_message.id);
    }

    // Вывожу результат работы программы - полученный DNS-ответ
    output_dns_message(&recv_message);

    free_dns_message(&recv_message);
    free(server_addr);
    close(sock_fd);
    return EXIT_SUCCESS;
}
